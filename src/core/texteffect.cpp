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

QMatrix TextEffect::getTransform(const qreal relFrame,
                                 const qreal influence,
                                 const QPointF& addPivot) const {
    const auto pivotAnim = mTransform->getPivotAnimator();
    const auto posAnim = mTransform->getPosAnimator();
    const auto rotAnim = mTransform->getRotAnimator();
    const auto scaleAnim = mTransform->getScaleAnimator();
    const qreal xScale = scaleAnim->getEffectiveXValue(relFrame);
    const qreal yScale = scaleAnim->getEffectiveYValue(relFrame);
    const qreal xPivot = pivotAnim->getEffectiveXValue(relFrame) + addPivot.x();
    const qreal yPivot = pivotAnim->getEffectiveYValue(relFrame) + addPivot.y();
    QMatrix transform;
    transform.translate(xPivot + posAnim->getEffectiveXValue(relFrame)*influence,
                        yPivot + posAnim->getEffectiveYValue(relFrame)*influence);
    transform.rotate(rotAnim->getEffectiveValue(relFrame)*influence);
    transform.scale(1 - influence + xScale*influence,
                    1 - influence + yScale*influence);
    transform.translate(-xPivot, -yPivot);
    return transform;
}

void TextEffect::applyToLetter(LetterRenderData * const letterData,
                               const qreal influence) const {
    const qreal relFrame = letterData->fRelFrame;
    if(!isZero4Dec(influence)) {
        const qreal currOpacity = mTransform->getOpacity(relFrame)*0.01;
        const qreal opacity = 1 + influence*(currOpacity - 1);

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
        const qreal opacity = 1 + influence*(currOpacity - 1);

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
        const qreal opacity = 1 + influence*(currOpacity - 1);

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
    mTarget->setCurrentValue(preset.fragment);
    mStaggerBy->setCurrentValue(preset.byIndex ? 1 : 0);

    mTransform->setPosition(preset.posX, preset.posY);
    mTransform->setRotation(preset.rot);
    mTransform->setScale(preset.scaleX, preset.scaleY);
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
    const qreal soft = qBound(0.02*W, preset.softness*W, 0.9*W);
    const qreal left = -0.15*W;
    const qreal right = 1.15*W;

    // keys the (x, y) of a guide control point: (x0, y0) at F0,
    // interpolating to (x1, y1) at F1
    const auto keyPoint = [F0, F1](QPointFAnimator * const anim,
                                   const qreal x0, const qreal x1,
                                   const qreal y0, const qreal y1) {
        const auto xAnim = anim->getXAnimator();
        const auto yAnim = anim->getYAnimator();
        if(xAnim) {
            xAnim->saveValueToKey(F0, x0);
            xAnim->saveValueToKey(F1, x1);
        }
        if(yAnim) {
            yAnim->saveValueToKey(F0, y0);
            yAnim->saveValueToKey(F1, y1);
        }
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
            keyPoint(mP2Anim.get(), left - soft, right, yLeft, yLeft);
            keyPoint(mP3Anim.get(), left, right + soft, yRight, yRight);
        } else {
            keyPoint(mP2Anim.get(), right, left - soft, yLeft, yLeft);
            keyPoint(mP3Anim.get(), right + soft, left, yRight, yRight);
        }
    } break;
    case TextAnim::wave: {
        mDiminishInfluence->setCurrentBaseValue(0);
        mPeriodicInfluence->setCurrentBaseValue(1);
        const qreal period = qMax(W/qMax(1, preset.waveCycles), 1.);
        mPeriod->setCurrentBaseValue(period);
        mPeriodicSmoothness->setCurrentBaseValue(0.5);
        // one full period of shift travel per waveTime: seamless loop
        const int waveF = qMax(1, qRound(preset.waveTime*fps));
        mPeriodicShift->saveValueToKey(F0, 0);
        mPeriodicShift->saveValueToKey(F0 + waveF, period);
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
        mInfluence->saveValueToKey(F0, 0);
        mInfluence->saveValueToKey(F1, peak);
        mInfluence->saveValueToKey(F1 + durF, 0);
    } break;
    }
}
