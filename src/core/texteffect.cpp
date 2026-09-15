/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "texteffect.h"

#include "textanimpresets.h"
#include "Boxes/pathboxrenderdata.h"
#include "Boxes/textboxrenderdata.h"
#include "Boxes/textbox.h"

#include "Animators/qpointfanimator.h"
#include "Animators/transformanimator.h"
#include "MovablePoints/animatedpoint.h"
#include "Expressions/expression.h"
#include "ReadWrite/evformat.h"
#include "ReadWrite/ereadstream.h"
#include "ReadWrite/ewritestream.h"
#include "XML/xevimporter.h"
#include <cmath>

namespace {
static constexpr qreal kPi = 3.14159265358979323846;

static qreal evaluateEasing(const TextEasing easing, const qreal t)
{
    if (t <= 0.0) { return 0.0; }
    if (t >= 1.0) { return 1.0; }

    switch (easing) {
    case TextEasing::smooth:
        // Quintic smootherstep: zero 1st & 2nd derivatives at both ends
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);

    case TextEasing::sharpSnap: {
        // High-velocity exponential ease-out (85% traveled in first 20% of duration)
        const qreal val = 1.0 - std::pow(2.0, -10.0 * t);
        const qreal norm = 1.0 - std::pow(2.0, -10.0);
        return val / norm;
    }

    case TextEasing::overshoot: {
        // Back-out overshoot (~12% rebound past rest target before settling)
        const qreal c1 = 1.70158;
        const qreal c3 = c1 + 1.0;
        const qreal tm1 = t - 1.0;
        return 1.0 + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
    }

    case TextEasing::elastic: {
        // Damped harmonic oscillator with 2 vibrant spring jelly cycles
        const qreal c4 = (2.0 * kPi) / 3.0;
        return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * c4) + 1.0;
    }

    case TextEasing::bounce: {
        // Multi-rebound gravitational bounce (3 bounces against rest floor)
        const qreal n1 = 7.5625;
        const qreal d1 = 2.75;
        if (t < 1.0 / d1) {
            return n1 * t * t;
        } else if (t < 2.0 / d1) {
            const qreal curT = t - (1.5 / d1);
            return n1 * curT * curT + 0.75;
        } else if (t < 2.5 / d1) {
            const qreal curT = t - (2.25 / d1);
            return n1 * curT * curT + 0.9375;
        } else {
            const qreal curT = t - (2.625 / d1);
            return n1 * curT * curT + 0.984375;
        }
    }

    case TextEasing::anticipate: {
        // Anticipate (Back-In-Out): recoils -9% before launching with high velocity
        const qreal c1 = 1.70158;
        const qreal c2 = c1 * 1.525;
        if (t < 0.5) {
            return (std::pow(2.0 * t, 2) * ((c2 + 1.0) * 2.0 * t - c2)) / 2.0;
        } else {
            const qreal tm = 2.0 * t - 2.0;
            return (std::pow(tm, 2) * ((c2 + 1.0) * tm + c2) + 2.0) / 2.0;
        }
    }

    case TextEasing::stepped:
        // Discrete 5-step quantized digital typewriter clock
        return std::floor(t * 5.0) / 5.0;
    }
    return t;
}

// shared JS helpers for the preset-generated expressions
const char* const kPresetDefs =
        "function pSat(t){return t<0?0:(t>1?1:t);}\n"
        "function pSmooth(t){t=pSat(t);return t*t*(3-2*t);}\n"
        "function pSeg(f,a,b){return b<=a?1:pSat((f-a)/(b-a));}\n"
        "function pLin(a,b,t){return a+(b-a)*t;}\n";

// attaches a preset-generated expression; non-undoable on purpose -
// the owning TextEffect is added/removed as a whole (one undo step)
void attachPresetExpr(QrealAnimator* const anim, const QString& script)
{
    if (!anim) { return; }
    try {
        // $frame is required so playback re-evaluates every frame
        auto expr = Expression::sCreate(
                    QStringLiteral("frame = $frame;"),
                    QString::fromUtf8(kPresetDefs), script, anim,
                    Expression::sQrealAnimatorTester);
        anim->setExpression(expr);
    } catch (const std::exception& e) {
        qWarning() << "[text-preset] expression failed:" << e.what();
    } catch (...) {
        qWarning() << "[text-preset] expression failed";
    }
}
}

class TextEffectPoint : public AnimatedPoint {
public:
    TextEffectPoint(QPointFAnimator * const anim,
                    TextEffect* const effect) :
        AnimatedPoint(anim, TYPE_PATH_POINT), mTextEffect(effect) {}

    QPointF getRelativePos() const {
        const qreal height = mTextEffect->getGuideLineHeight();
        const QPointF pos = AnimatedPoint::getRelativePos();
        if(mTextEffect->target() == TextFragmentType::line)
            return {pos.y()*height, pos.x()};
        return {pos.x(), -pos.y()*height};
    }

    void setRelativePos(const QPointF &relPos) {
        const qreal height = mTextEffect->getGuideLineHeight();
        if(mTextEffect->target() == TextFragmentType::line) {
            AnimatedPoint::setRelativePos({relPos.y(), relPos.x()/height});
        } else {
            AnimatedPoint::setRelativePos({relPos.x(), -relPos.y()/height});
        }
    }
private:
    TextEffect * const mTextEffect;
};

TextEffect::TextEffect() : eEffect("text effect") {
    mInfluence = enve::make_shared<QrealAnimator>(
                1, 0, 1, 0.1, "influence");
    mTarget = enve::make_shared<ComboBoxProperty>(
                "target", QStringList() << "letters" << "words" << "lines");
    mMinInfluence = enve::make_shared<QrealAnimator>(
                0, 0, 1, 0.1, "min influence");
    // appended last: keeps the serialized child order of older
    // project files valid
    mStaggerBy = enve::make_shared<ComboBoxProperty>(
                "stagger", QStringList() << "position" << "index");

    mDiminishCont = enve::make_shared<StaticComplexAnimator>("diminish");
    mDiminishInfluence = enve::make_shared<QrealAnimator>(
                1, 0, 1, 0.1, "influence");

    mP1Anim = enve::make_shared<QPointFAnimator>("point 1");
    mP1Anim->getYAnimator()->setValueRange(0, 1);
    mP1Anim->getYAnimator()->setPrefferedValueStep(0.1);
    mP1Anim->setBaseValue(-40, 0);
    mP2Anim = enve::make_shared<QPointFAnimator>("point 2");
    mP2Anim->getYAnimator()->setValueRange(0, 1);
    mP2Anim->getYAnimator()->setPrefferedValueStep(0.1);
    mP2Anim->setBaseValue(-10, 1);
    mP3Anim = enve::make_shared<QPointFAnimator>("point 3");
    mP3Anim->getYAnimator()->setValueRange(0, 1);
    mP3Anim->getYAnimator()->setPrefferedValueStep(0.1);
    mP3Anim->setBaseValue(20, 1);
    mP4Anim = enve::make_shared<QPointFAnimator>("point 4");
    mP4Anim->getYAnimator()->setValueRange(0, 1);
    mP4Anim->getYAnimator()->setPrefferedValueStep(0.1);
    mP4Anim->setBaseValue(50, 0);

    setPointsHandler(enve::make_shared<PointsHandler>());
    mP1Pt = enve::make_shared<TextEffectPoint>(mP1Anim.get(), this);
    mP2Pt = enve::make_shared<TextEffectPoint>(mP2Anim.get(), this);
    mP3Pt = enve::make_shared<TextEffectPoint>(mP3Anim.get(), this);
    mP4Pt = enve::make_shared<TextEffectPoint>(mP4Anim.get(), this);
    getPointsHandler()->appendPt(mP1Pt);
    getPointsHandler()->appendPt(mP2Pt);
    getPointsHandler()->appendPt(mP3Pt);
    getPointsHandler()->appendPt(mP4Pt);

    mDiminishSmoothness = enve::make_shared<QrealAnimator>(
                0.5, 0, 1, 0.1, "smoothness");

    mPeriodicCont = enve::make_shared<StaticComplexAnimator>("periodic");
    mPeriodicInfluence = enve::make_shared<QrealAnimator>(
                0, 0, 1, 0.1, "influence");
    mPeriod = enve::make_shared<QrealAnimator>(
                50, 1, 9999, 5, "period");
    mPeriodicShift = enve::make_shared<QrealAnimator>(
                0, -9999, 9999, 5, "shift");
    mPeriodicSmoothness = enve::make_shared<QrealAnimator>(
                0.5, 0, 1, 0.1, "smoothness");

    mTransform = enve::make_shared<AdvancedTransformAnimator>();
    mTransform->setSVGEventsVisibility(false);

    mBasePathEffects = enve::make_shared<PathEffectCollection>();
    mBasePathEffects->ca_setHiddenWhenEmpty(false);
    mBasePathEffects->prp_setName("path base effects");

    mFillPathEffects = enve::make_shared<PathEffectCollection>();
    mFillPathEffects->ca_setHiddenWhenEmpty(false);
    mFillPathEffects->prp_setName("fill effects");

    mOutlineBasePathEffects = enve::make_shared<PathEffectCollection>();
    mOutlineBasePathEffects->ca_setHiddenWhenEmpty(false);
    mOutlineBasePathEffects->prp_setName("outline base effects");

    mOutlinePathEffects = enve::make_shared<PathEffectCollection>();
    mOutlinePathEffects->ca_setHiddenWhenEmpty(false);
    mOutlinePathEffects->prp_setName("outline effects");

    mRasterEffects = enve::make_shared<RasterEffectCollection>();
    mRasterEffects->ca_setHiddenWhenEmpty(false);

    ca_addChild(mInfluence);
    ca_addChild(mTarget);
    ca_addChild(mMinInfluence);
    ca_addChild(mStaggerBy);

    ca_addChild(mDiminishCont);
    mDiminishCont->ca_addChild(mDiminishInfluence);
    mDiminishCont->ca_addChild(mP1Anim);
    mDiminishCont->ca_addChild(mP2Anim);
    mDiminishCont->ca_addChild(mP3Anim);
    mDiminishCont->ca_addChild(mP4Anim);
    mDiminishCont->ca_addChild(mDiminishSmoothness);
    mDiminishCont->ca_setGUIProperty(mDiminishInfluence.get());

    ca_addChild(mPeriodicCont);
    mPeriodicCont->ca_addChild(mPeriodicInfluence);
    mPeriodicCont->ca_addChild(mPeriod);
    mPeriodicCont->ca_addChild(mPeriodicShift);
    mPeriodicCont->ca_addChild(mPeriodicSmoothness);
    mPeriodicCont->ca_setGUIProperty(mPeriodicInfluence.get());

    ca_addChild(mTransform);
    ca_addChild(mBasePathEffects);
    ca_addChild(mFillPathEffects);
    ca_addChild(mOutlineBasePathEffects);
    ca_addChild(mOutlinePathEffects);
    ca_addChild(mRasterEffects);

    ca_setGUIProperty(mInfluence.get());

    prp_enabledDrawingOnCanvas();
}

void TextEffect::prp_writeProperty_impl(eWriteStream& dst) const
{
    eEffect::prp_writeProperty_impl(dst);
    dst << mCustomPhysics;
    if (mCustomPhysics) {
        dst << static_cast<int>(mEasing);
        dst << mStartFrame;
        dst << mDurFrames;
        dst << static_cast<int>(mDirection);
        dst << static_cast<int>(mKind);
        dst << mStaggerPercent;
    }
}

void TextEffect::prp_readProperty_impl(eReadStream& src)
{
    eEffect::prp_readProperty_impl(src);
    if (src.evFileVersion() >= EvFormat::textPhysicsEasing) {
        src >> mCustomPhysics;
        if (mCustomPhysics) {
            int easingVal = 0, dirVal = 0, kindVal = 0;
            src >> easingVal >> mStartFrame >> mDurFrames >> dirVal >> kindVal >> mStaggerPercent;
            mEasing = static_cast<TextEasing>(easingVal);
            mDirection = static_cast<TextAnimDirection>(dirVal);
            mKind = static_cast<TextAnim::Kind>(kindVal);
        }
    }
}

void TextEffect::writeIdentifierXEV(QDomElement& ele) const
{
    if (mCustomPhysics) {
        ele.setAttribute(QStringLiteral("customPhysics"), 1);
        ele.setAttribute(QStringLiteral("easing"), static_cast<int>(mEasing));
        ele.setAttribute(QStringLiteral("startFrame"), mStartFrame);
        ele.setAttribute(QStringLiteral("durFrames"), mDurFrames);
        ele.setAttribute(QStringLiteral("direction"), static_cast<int>(mDirection));
        ele.setAttribute(QStringLiteral("kind"), static_cast<int>(mKind));
        ele.setAttribute(QStringLiteral("staggerPercent"), mStaggerPercent);
    }
}

void TextEffect::prp_readPropertyXEV_impl(const QDomElement& ele, const XevImporter& imp)
{
    eEffect::prp_readPropertyXEV_impl(ele, imp);
    if (ele.hasAttribute(QStringLiteral("customPhysics"))) {
        mCustomPhysics = ele.attribute(QStringLiteral("customPhysics")).toInt() != 0;
        mEasing = static_cast<TextEasing>(ele.attribute(QStringLiteral("easing")).toInt());
        mStartFrame = ele.attribute(QStringLiteral("startFrame")).toInt();
        mDurFrames = ele.attribute(QStringLiteral("durFrames")).toInt();
        mDirection = static_cast<TextAnimDirection>(ele.attribute(QStringLiteral("direction")).toInt());
        mKind = static_cast<TextAnim::Kind>(ele.attribute(QStringLiteral("kind")).toInt());
        mStaggerPercent = ele.attribute(QStringLiteral("staggerPercent"), QStringLiteral("0.4")).toDouble();
    }
}

bool ptXLess(const QPointF& p1, const QPointF& p2)
{ return p1.x() < p2.x(); }

qreal TextEffect::getGuideLineHeight() const {
    const auto textBox = getFirstAncestor<TextBox>();
    if(textBox) return textBox->getFontSize();
    return 0;
}

qreal TextEffect::getGuideLineWidth() const {
    const auto textBox = getFirstAncestor<TextBox>();
    if(!textBox) return 0;
    const qreal w = textBox->getRelBoundingRect().width();
    if(w > 1.) return w;
    // sceneless boxes (preset preview) never got their cached
    // bounding rect updated: measure the text directly instead
    return qMax(horizontalAdvance(textBox->getSkFont(),
                                  textBox->getCurrentValue()), 1.);
}

void TextEffect::prp_drawCanvasControls(
        SkCanvas * const canvas, const CanvasMode mode,
        const float invScale, const bool ctrlPressed) {
    if(mode != CanvasMode::pointTransform || !isVisible()) return;

    SkPath path;

    //const qreal dimInfl = mDiminishInfluence->getEffectiveValue();
    const QPointF p1 = mP1Anim->getEffectiveValue();
    const QPointF p2 = mP2Anim->getEffectiveValue();
    const QPointF p3 = mP3Anim->getEffectiveValue();
    const QPointF p4 = mP4Anim->getEffectiveValue();

    const qreal minX = qMin4(p1.x(), p2.x(), p3.x(), p4.x());
    const qreal maxX = qMax4(p1.x(), p2.x(), p3.x(), p4.x());
    const qreal height = getGuideLineHeight();

    const qreal smoothness = mDiminishSmoothness->getEffectiveValue();

    QList<QPointF> pList{p1, p2, p3, p4};
    std::sort(pList.begin(), pList.end(), ptXLess);

    QPointF prevPt = pList.first();
    path.moveTo(toSkScalar(prevPt.x()), -toSkScalar(prevPt.y()*height));
    const int iMax = pList.count() - 1;
    for(int i = 1; i <= iMax; i++) {
        const auto& pt = pList.at(i);

        path.cubicTo(toSkScalar(prevPt.x()*(1 - smoothness) + pt.x()*smoothness),
                     -toSkScalar(prevPt.y()*height),
                     toSkScalar(pt.x()*(1 - smoothness) + prevPt.x()*smoothness),
                     -toSkScalar(pt.y()*height),
                     toSkScalar(pt.x()),
                     -toSkScalar(pt.y()*height));

        prevPt = pt;
    }

    SkPath topLine;
    topLine.moveTo(toSkScalar(minX), -toSkScalar(height));
    topLine.lineTo(toSkScalar(maxX), -toSkScalar(height));

    SkPath bottomLine;
    bottomLine.moveTo(toSkScalar(minX), toSkScalar(0));
    bottomLine.lineTo(toSkScalar(maxX), toSkScalar(0));

    SkPath cyclicalPath;
    const qreal periodInfl = mPeriodicInfluence->getEffectiveValue();
    if(!isZero4Dec(periodInfl)) {
        const qreal period = mPeriod->getEffectiveValue();
        const qreal shift = mPeriodicShift->getEffectiveValue();
        const qreal smoothness = mPeriodicSmoothness->getEffectiveValue();
        const qreal width = getGuideLineWidth();

        const qreal firstX = shift - qCeil(shift/period)*period;
        const qreal last = width + period;
        cyclicalPath.moveTo(toSkScalar(firstX), toSkScalar(height));
        for(qreal x = firstX + 0.5*period ; x < last; x += period) {
            const qreal x0 = x - 0.5*period;
            const qreal x1 = x;
            const qreal x2 = x + 0.5*period;

            cyclicalPath.cubicTo(toSkScalar(x0*(1 - smoothness) + x1*smoothness),
                                 toSkScalar(height),
                                 toSkScalar(x1*(1 - smoothness) + x0*smoothness),
                                 0,
                                 toSkScalar(x1),
                                 0);
            cyclicalPath.cubicTo(toSkScalar(x1*(1 - smoothness) + x2*smoothness),
                                 0,
                                 toSkScalar(x2*(1 - smoothness) + x1*smoothness),
                                 toSkScalar(height),
                                 toSkScalar(x2),
                                 toSkScalar(height));
        }
    }

    if(target() == TextFragmentType::line) {
        SkMatrix transform;
        transform.setRotate(90);

        path.transform(transform);
        topLine.transform(transform);
        bottomLine.transform(transform);
        cyclicalPath.transform(transform);
    }
    const auto transform = toSkMatrix(eEffect::getTransform());

    SkiaHelpers::drawOutlineOverlay(canvas, topLine, invScale,
                                    transform, true, 5.f, SK_ColorBLUE);
    SkiaHelpers::drawOutlineOverlay(canvas, bottomLine, invScale,
                                    transform, true, 5.f, SK_ColorBLUE);
    SkiaHelpers::drawOutlineOverlay(canvas, path, invScale,
                                    transform, SK_ColorRED);
    SkiaHelpers::drawOutlineOverlay(canvas, cyclicalPath, invScale,
                                    transform, SK_ColorRED);
    eEffect::prp_drawCanvasControls(canvas, mode, invScale, ctrlPressed);
}

bool TextEffect::SWT_dropSupport(const QMimeData * const data) {
    return mRasterEffects->SWT_dropSupport(data);
}

bool TextEffect::SWT_drop(const QMimeData * const data) {
    if(mRasterEffects->SWT_dropSupport(data))
        return mRasterEffects->SWT_drop(data);
    return false;
}

void TextEffect::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    eEffect::prp_setupTreeViewMenu(menu);
    const PropertyMenu::PlainSelectedOp<TextEffect> dOp =
    [](TextEffect* const eff) {
        const auto parent = eff->getParent<DynamicComplexAnimatorBase<TextEffect>>();
        parent->removeChild(eff->ref<TextEffect>());
    };
    menu->addPlainAction(QIcon::fromTheme("trash"), tr("Delete Effect(s)"), dOp);
}

QMimeData *TextEffect::SWT_createMimeData() {
    return new eMimeData(QList<TextEffect*>() << this);
}

QTransform TextEffect::getTransform(const qreal relFrame,
                                 const qreal influence,
                                 const QPointF& addPivot) const {
    const auto pivotAnim = mTransform->getPivotAnimator();
    const auto posAnim = mTransform->getPosAnimator();
    const auto rotAnim = mTransform->getRotAnimator();
    const auto scaleAnim = mTransform->getScaleAnimator();
    const auto shearAnim = mTransform->getShearAnimator();
    const qreal xScale = scaleAnim->getEffectiveXValue(relFrame);
    const qreal yScale = scaleAnim->getEffectiveYValue(relFrame);
    const qreal shx = shearAnim ? shearAnim->getEffectiveXValue(relFrame) : 0.0;
    const qreal shy = shearAnim ? shearAnim->getEffectiveYValue(relFrame) : 0.0;
    const qreal xPivot = pivotAnim->getEffectiveXValue(relFrame) + addPivot.x();
    const qreal yPivot = pivotAnim->getEffectiveYValue(relFrame) + addPivot.y();
    QTransform transform;
    transform.translate(xPivot + posAnim->getEffectiveXValue(relFrame)*influence,
                        yPivot + posAnim->getEffectiveYValue(relFrame)*influence);
    transform.rotate(rotAnim->getEffectiveValue(relFrame)*influence);
    transform.scale(1 - influence + xScale*influence,
                    1 - influence + yScale*influence);
    if(!isZero4Dec(shx) || !isZero4Dec(shy)) {
        transform.shear(shx * influence, shy * influence);
    }
    transform.translate(-xPivot, -yPivot);
    return transform;
}

void TextEffect::applyToLetter(LetterRenderData * const letterData,
                               const qreal influence) const {
    const qreal relFrame = letterData->fRelFrame;
    if(!isZero4Dec(influence)) {
        const qreal currOpacity = mTransform->getOpacity(relFrame)*0.01;
        const qreal opacity = qBound(0.0, 1.0 + influence*(currOpacity - 1.0), 1.0);

        const auto transform = getTransform(relFrame, influence,
                                            letterData->fLetterPos);
        letterData->applyTransform(transform);
        letterData->fOpacity *= opacity;
    }

    mBasePathEffects->addEffects(relFrame, letterData->fPathEffects, influence);
    mFillPathEffects->addEffects(relFrame, letterData->fFillEffects, influence);
    mOutlineBasePathEffects->addEffects(relFrame, letterData->fOutlineBaseEffects, influence);
    mOutlinePathEffects->addEffects(relFrame, letterData->fOutlineEffects, influence);

    mRasterEffects->addEffects(relFrame, letterData, influence);
}

void TextEffect::applyToWord(WordRenderData * const wordData,
                             const qreal influence) const {
    const qreal relFrame = wordData->fRelFrame;
    if(!isZero4Dec(influence)) {
        const qreal currOpacity = mTransform->getOpacity(relFrame)*0.01;
        const qreal opacity = qBound(0.0, 1.0 + influence*(currOpacity - 1.0), 1.0);

        const auto transform = getTransform(relFrame, influence,
                                            wordData->fWordPos);
        wordData->applyTransform(transform);
        wordData->fOpacity *= opacity;
    }

    mRasterEffects->addEffects(relFrame, wordData, influence);
}

void TextEffect::applyToLine(LineRenderData * const lineData,
                             const qreal influence) const {
    const qreal relFrame = lineData->fRelFrame;
    if(!isZero4Dec(influence)) {
        const qreal currOpacity = mTransform->getOpacity(relFrame)*0.01;
        const qreal opacity = qBound(0.0, 1.0 + influence*(currOpacity - 1.0), 1.0);

        const auto transform = getTransform(relFrame, influence,
                                            lineData->fLinePos);
        lineData->applyTransform(transform);
        lineData->fOpacity *= opacity;
    }

    mRasterEffects->addEffects(relFrame, lineData, influence);
}

QrealSnapshot diminishGuide(const qreal ampl,
                            const QPointF& p1,
                            const QPointF& p2,
                            const QPointF& p3,
                            const QPointF& p4,
                            const qreal smoothness) {
    QList<QPointF> pList{p1, p2, p3, p4};
    std::sort(pList.begin(), pList.end(), ptXLess);

    QrealSnapshot result(ampl, 1, ampl);

    QPointF prevPt = pList.first();
    const int iMax = pList.count() - 1;
    for(int i = 0; i <= iMax; i++) {
        const auto& pt = pList.at(i);
        const auto& nextPt = pList.at(qMin(iMax, i + 1));

        result.appendKey(pt.x()*(1 - smoothness) + prevPt.x()*smoothness, pt.y(),
                         pt.x(), pt.y(),
                         pt.x()*(1 - smoothness) + nextPt.x()*smoothness, pt.y());
        prevPt = pt;
    }

    return result;
}

QrealSnapshot cyclicalGuide(const qreal ampl,
                            const qreal period,
                            const qreal shift,
                            const qreal smoothness,
                            const qreal width) {
    QrealSnapshot result(ampl, 1, ampl);

    const qreal first = shift - qCeil(shift/period)*period;
    const qreal last = width + 2*period;
    for(qreal x = first ; x < last; x += period) {
        const qreal xm1 = x - 0.5*period;
        const qreal x0 = x;
        const qreal x1 = x + 0.5*period;
        const qreal x2 = x + period;

        result.appendKey(x0*(1 - smoothness) + xm1*smoothness, 0,
                         x0, 0,
                         x0*(1 - smoothness) + x1*smoothness, 0);
        result.appendKey(x1*(1 - smoothness) + x0*smoothness, 1,
                         x1, 1,
                         x1*(1 - smoothness) + x2*smoothness, 1);
    }
    return result;
}

void TextEffect::apply(TextBoxRenderData * const textData) const {
    const qreal relFrame = textData->fRelFrame;
    const qreal maxInfl = mInfluence->getEffectiveValue(relFrame);
    if(isZero4Dec(maxInfl)) return;

    if (mCustomPhysics && (mKind == TextAnim::sweepIn || mKind == TextAnim::sweepOut)) {
        const auto computePhysicsInfl = [this, relFrame, maxInfl](const qreal uRaw) -> qreal {
            const qreal u = (mDirection == TextAnimDirection::rightToLeft) ? (1.0 - uRaw) : uRaw;
            const qreal D = qMax(1.0, static_cast<qreal>(mDurFrames));
            const qreal S = qBound(0.05, mStaggerPercent, 0.85);
            const qreal tStagger = S * D;
            const qreal tFrag = qMax(1.0, D - tStagger);
            const qreal fStart = static_cast<qreal>(mStartFrame) + u * tStagger;
            const qreal deltaF = relFrame - fStart;

            qreal tau = 0.0;
            if (deltaF <= 0.0) {
                tau = 0.0;
            } else if (deltaF >= tFrag) {
                tau = 1.0;
            } else {
                tau = deltaF / tFrag;
            }

            const qreal e = evaluateEasing(mEasing, tau);
            const qreal rawInfl = (mKind == TextAnim::sweepIn) ? (1.0 - e) : e;
            return rawInfl * maxInfl;
        };

        const bool byIndex = mStaggerBy->getCurrentValue() == 1;

        switch(target()) {
        case TextFragmentType::letter: {
            int nFragments = 0;
            qreal minX = 1e9, maxX = -1e9;
            for (const auto& line : textData->fLines) {
                for (const auto& word : line->fWords) {
                    for (const auto& letter : word->fLetters) {
                        nFragments++;
                        const qreal lx = letter->fOriginalPos.x();
                        if (lx < minX) minX = lx;
                        if (lx > maxX) maxX = lx;
                    }
                }
            }
            if (nFragments == 0) break;
            const qreal spanX = (maxX > minX) ? (maxX - minX) : 1.0;

            int iFragments = 0;
            for (const auto& line : textData->fLines) {
                for (const auto& word : line->fWords) {
                    for (const auto& letter : word->fLetters) {
                        const qreal u = byIndex ?
                            (nFragments > 1 ? static_cast<qreal>(iFragments) / (nFragments - 1) : 0.0) :
                            qBound(0.0, (letter->fOriginalPos.x() - minX) / spanX, 1.0);
                        iFragments++;
                        const qreal influence = computePhysicsInfl(u);
                        applyToLetter(letter.get(), influence);
                    }
                }
            }
        } break;
        case TextFragmentType::word: {
            int nFragments = 0;
            qreal minX = 1e9, maxX = -1e9;
            for (const auto& line : textData->fLines) {
                for (const auto& word : line->fWords) {
                    nFragments++;
                    const qreal wx = word->fOriginalPos.x();
                    if (wx < minX) minX = wx;
                    if (wx > maxX) maxX = wx;
                }
            }
            if (nFragments == 0) break;
            const qreal spanX = (maxX > minX) ? (maxX - minX) : 1.0;

            int iFragments = 0;
            for (const auto& line : textData->fLines) {
                for (const auto& word : line->fWords) {
                    const qreal u = byIndex ?
                        (nFragments > 1 ? static_cast<qreal>(iFragments) / (nFragments - 1) : 0.0) :
                        qBound(0.0, (word->fOriginalPos.x() - minX) / spanX, 1.0);
                    iFragments++;
                    const qreal influence = computePhysicsInfl(u);
                    applyToWord(word.get(), influence);
                }
            }
        } break;
        case TextFragmentType::line: {
            const int nFragments = textData->fLines.count();
            if (nFragments == 0) break;
            qreal minY = 1e9, maxY = -1e9;
            for (const auto& line : textData->fLines) {
                const qreal ly = line->fOriginalPos.y();
                if (ly < minY) minY = ly;
                if (ly > maxY) maxY = ly;
            }
            const qreal spanY = (maxY > minY) ? (maxY - minY) : 1.0;

            int iFragments = 0;
            for (const auto& line : textData->fLines) {
                const qreal u = byIndex ?
                    (nFragments > 1 ? static_cast<qreal>(iFragments) / (nFragments - 1) : 0.0) :
                    qBound(0.0, (line->fOriginalPos.y() - minY) / spanY, 1.0);
                iFragments++;
                const qreal influence = computePhysicsInfl(u);
                applyToLine(line.get(), influence);
            }
        } break;
        default: break;
        }
        return;
    }

    const qreal minInfl = mMinInfluence->getEffectiveValue(relFrame);
    const qreal ampl = mInfluence->getEffectiveValue(relFrame);
    const qreal period = mPeriod->getEffectiveValue(relFrame);

    const qreal dimInfl = mDiminishInfluence->getEffectiveValue(relFrame);
    const QPointF p1 = mP1Anim->getEffectiveValue(relFrame);
    const QPointF p2 = mP2Anim->getEffectiveValue(relFrame);
    const QPointF p3 = mP3Anim->getEffectiveValue(relFrame);
    const QPointF p4 = mP4Anim->getEffectiveValue(relFrame);
    const qreal dimSmoothness = mDiminishSmoothness->getEffectiveValue(relFrame);

    const qreal perInfl = mPeriodicInfluence->getEffectiveValue(relFrame);
    const qreal perSmoothness = mPeriodicSmoothness->getEffectiveValue(relFrame);
    const qreal perShift = mPeriodicShift->getEffectiveValue(relFrame);
    if(isZero4Dec(dimInfl + perInfl)) return;

    const qreal inflSum = qMin(1., dimInfl + perInfl);
    const auto baseGuide = diminishGuide(ampl, p1, p2, p3, p4, dimSmoothness);
    const qreal guideWidth = getGuideLineWidth();
    const auto sinGuide = cyclicalGuide(ampl, period, perShift, perSmoothness,
                                        guideWidth);
    const bool byIndex = mStaggerBy->getCurrentValue() == 1;
    const auto fragmentX = [byIndex, guideWidth](
                const qreal pos, const int index, const int count) {
        if(!byIndex || count <= 0) return pos;
        return guideWidth*(index + 0.5)/count;
    };
    switch(target()) {
    case TextFragmentType::letter: {
        int nFragments = 0;
        if(byIndex) {
            for(const auto& line : textData->fLines) {
                for(const auto& word : line->fWords) {
                    nFragments += word->fLetters.count();
                }
            }
        }
        int iFragments = 0;
        for(const auto& line : textData->fLines) {
            for(const auto& word : line->fWords) {
                for(const auto& letter : word->fLetters) {
                    const qreal xPos = fragmentX(letter->fOriginalPos.x(),
                                                 iFragments++, nFragments);
                    const qreal baseInfl = baseGuide.getValue(xPos)*dimInfl + inflSum - dimInfl;
                    const qreal sinInfl = sinGuide.getValue(xPos)*perInfl + inflSum - perInfl;
                    const qreal influence = qBound(minInfl, baseInfl*sinInfl, 1.);
                    applyToLetter(letter.get(), influence);
                }
            }
        }
    } break;
    case TextFragmentType::word: {
        int nFragments = 0;
        if(byIndex) {
            for(const auto& line : textData->fLines) {
                nFragments += line->fWords.count();
            }
        }
        int iFragments = 0;
        for(const auto& line : textData->fLines) {
            for(const auto& word : line->fWords) {
                const qreal xPos = fragmentX(word->fOriginalPos.x(),
                                             iFragments++, nFragments);
                const qreal baseInfl = baseGuide.getValue(xPos)*dimInfl + inflSum - dimInfl;
                const qreal sinInfl = sinGuide.getValue(xPos)*perInfl + inflSum - perInfl;
                const qreal influence = qBound(minInfl, baseInfl*sinInfl, 1.);
                applyToWord(word.get(), influence);
            }
        }
    } break;
    case TextFragmentType::line: {
        int nFragments = 0;
        if(byIndex) nFragments = textData->fLines.count();
        int iFragments = 0;
        for(const auto& line : textData->fLines) {
            const qreal yPos = byIndex ?
                        fragmentX(line->fOriginalPos.y(),
                                  iFragments++, nFragments) :
                        line->fOriginalPos.y();
            const qreal baseInfl = baseGuide.getValue(yPos)*dimInfl + inflSum - dimInfl;
            const qreal sinInfl = sinGuide.getValue(yPos)*perInfl + inflSum - perInfl;
            const qreal influence = qBound(minInfl, baseInfl*sinInfl, 1.);
            applyToLine(line.get(), influence);
        }
    } break;
    default: break;
    }
}

TextFragmentType TextEffect::target() const {
    return static_cast<TextFragmentType>(mTarget->getCurrentValue());
}

void TextEffect::setupFromPreset(const TextAnimPreset &preset,
                                 const qreal textWidth,
                                 const qreal fontSize,
                                 const int startFrame,
                                 const qreal fps,
                                 const qreal durationScale) {
    // clear expressions left by a previously applied preset so presets
    // never stack - sweep after wave used to keep the wave phase
    // driver running underneath the new sweep
    const auto clearExpr = [](QrealAnimator* const anim) {
        if (anim && anim->hasExpression()) { anim->setExpression(nullptr); }
    };
    if (mP2Anim) { clearExpr(mP2Anim->getXAnimator()); }
    if (mP3Anim) { clearExpr(mP3Anim->getXAnimator()); }
    clearExpr(mPeriodicShift.get());
    clearExpr(mInfluence.get());

    mTarget->setCurrentValue(preset.fragment);
    mStaggerBy->setCurrentValue(preset.byIndex ? 1 : 0);

    mTransform->setPosition(preset.posX, preset.posY);
    mTransform->setRotation(preset.rot);
    mTransform->setScale(preset.scaleX, preset.scaleY);
    mTransform->setShear(preset.shearX, preset.shearY);
    mTransform->setOpacity(preset.opacity);
    if(preset.pivotCenter) {
        mTransform->setPivot(0.3*fontSize, -0.35*fontSize);
    } else {
        mTransform->setPivot(0, 0);
    }

    const qreal W = qMax(textWidth, 1.);
    const int durF = qMax(1, qRound(preset.duration*durationScale*fps));
    const int F0 = startFrame;
    const int F1 = F0 + durF;

    mCustomPhysics = (preset.kind == TextAnim::sweepIn || preset.kind == TextAnim::sweepOut);
    mEasing = preset.easing;
    mStartFrame = startFrame;
    mDurFrames = durF;
    mDirection = preset.direction;
    mKind = preset.kind;
    mStaggerPercent = preset.staggerPercent;

    const qreal soft = qBound(0.02*W, preset.softness*W, 0.9*W);
    const qreal left = -0.15*W;
    const qreal right = 1.15*W;

    // drives the (x) of a guide control point with an expression:
    // smooth travel from x0 at F0 to x1 at F1; y stays constant
    const auto frontExpr = [F0, F1](QPointFAnimator * const anim,
                                    const qreal x0, const qreal x1,
                                    const qreal y) {
        if(!anim) return;
        const auto yAnim = anim->getYAnimator();
        if(yAnim) yAnim->setCurrentBaseValue(y);
        const QString script = QStringLiteral(
                    "// 扫掠前沿：从 %1 平滑移动到 %2（帧 %3 → %4）\n"
                    "var f = frame;\n"
                    "return pLin(%1, %2, pSmooth(pSeg(f, %3, %4)));")
                .arg(QString::number(x0, 'f', 1),
                     QString::number(x1, 'f', 1),
                     QString::number(F0), QString::number(F1));
        attachPresetExpr(anim->getXAnimator(), script);
    };

    switch(preset.kind) {
    case TextAnim::sweepIn:
    case TextAnim::sweepOut: {
        mDiminishInfluence->setCurrentBaseValue(1);
        mPeriodicInfluence->setCurrentBaseValue(0);

        // value to the left / right of the moving front:
        // entrance:  left = arrived (0), right = start state (1)
        // exit:      left = gone (1),   right = intact (0)
        const bool ltr = preset.direction == TextAnimDirection::leftToRight;
        const qreal inY0 = preset.kind == TextAnim::sweepIn ? 0 : 1;
        const qreal inY1 = preset.kind == TextAnim::sweepIn ? 1 : 0;
        const qreal yLeft = ltr ? inY0 : inY1;
        const qreal yRight = ltr ? inY1 : inY0;

        // anchors sit beyond the band's end positions so the four
        // control points never collide mid-sweep
        mP1Anim->setBaseValue(left - soft, yLeft);
        mP4Anim->setBaseValue(right + soft, yRight);
        // the [P2, P3] band is the moving front; at F0 it sits fully
        // outside the text on one side, at F1 fully past it on the
        // other, so the first/last frame is a clean rest state
        if(ltr) {
            frontExpr(mP2Anim.get(), left - soft, right, yLeft);
            frontExpr(mP3Anim.get(), left, right + soft, yRight);
        } else {
            frontExpr(mP2Anim.get(), right, left - soft, yLeft);
            frontExpr(mP3Anim.get(), right + soft, left, yRight);
        }
    } break;
    case TextAnim::wave: {
        mDiminishInfluence->setCurrentBaseValue(0);
        mPeriodicInfluence->setCurrentBaseValue(1);
        const qreal period = qMax(W/qMax(1, preset.waveCycles), 1.);
        mPeriod->setCurrentBaseValue(period);
        mPeriodicSmoothness->setCurrentBaseValue(0.5);
        // one full period of shift travel per waveTime; the phase
        // keeps advancing forever (the old two-key bake froze the
        // wave after its first period)
        const int waveF = qMax(1, qRound(preset.waveTime*fps));
        attachPresetExpr(mPeriodicShift.get(), QStringLiteral(
                    "// 波浪相位：每 %1 帧推进一个周期（无限循环）\n"
                    "var f = frame;\n"
                    "return %2 * (f - %3) / %1;")
                .arg(QString::number(waveF),
                     QString::number(period, 'f', 1),
                     QString::number(F0)));
    } break;
    case TextAnim::pulse: {
        mDiminishInfluence->setCurrentBaseValue(1);
        mPeriodicInfluence->setCurrentBaseValue(0);
        // flat guide at 1: every fragment gets the same influence,
        // driven over time by the influence animator itself
        mP1Anim->setBaseValue(left, 1);
        mP2Anim->setBaseValue(left + 0.01*W, 1);
        mP3Anim->setBaseValue(right - 0.01*W, 1);
        mP4Anim->setBaseValue(right, 1);
        const qreal peak = qBound(0., preset.pulsePeak, 1.);
        attachPresetExpr(mInfluence.get(), QStringLiteral(
                    "// 脉冲：0 → 峰值 → 0 循环（周期 %1 帧，无限循环）\n"
                    "var f = frame;\n"
                    "return %2 * (0.5 - 0.5 * Math.cos(Math.PI * (f - %3) / %1));")
                .arg(QString::number(2*durF),
                     QString::number(peak),
                     QString::number(F0)));
    } break;
    }
}
