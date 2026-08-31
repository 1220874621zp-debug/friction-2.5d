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

#include "motionpathhandler.h"

#include "Animators/qrealanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/graphkey.h"
#include "Animators/qrealkey.h"
#include "Animators/eboxorsound.h"
#include "MovablePoints/pointshandler.h"
#include "Private/document.h"
#include "themesupport.h"
#include "skia/skiaincludes.h"

namespace {

// draggable point for a position keyframe (moves the whole key value)
class MotionKeyPoint final : public MovablePoint {
    e_OBJECT
public:
    MotionKeyPoint(BasicTransformAnimator * const trans,
                   QrealAnimator * const xAnim,
                   QrealAnimator * const yAnim,
                   const int frame)
        : MovablePoint(trans, TYPE_PATH_POINT)
        , mXAnim(xAnim), mYAnim(yAnim), mFrame(frame)
    {
        setRadius(5);
    }

    QPointF getRelativePos() const override {
        return {mXAnim->getBaseValue(mFrame),
                mYAnim->getBaseValue(mFrame)};
    }

    void setRelativePos(const QPointF &relPos) override {
        mXAnim->saveValueToKey(mFrame, relPos.x());
        mYAnim->saveValueToKey(mFrame, relPos.y());
    }

    void startTransform() override {
        mSavedPos = getRelativePos();
        // MovablePoint::moveByAbs drags relative to mSavedRelPos; the
        // base save is what keeps the box from jumping to the origin
        MovablePoint::startTransform();
        mXAnim->prp_startTransform();
        mYAnim->prp_startTransform();
    }

    // AE-style: motion path points are editable with the regular
    // transform tool, not only in node-edit mode
    bool isVisible(const CanvasMode mode) const override {
        return mode == CanvasMode::boxTransform ||
               mode == CanvasMode::pointTransform;
    }

    void finishTransform() override {
        mXAnim->prp_finishTransform();
        mYAnim->prp_finishTransform();
        if (Document::sInstance) { Document::sInstance->actionFinished(); }
    }

    void cancelTransform() override {
        mXAnim->prp_cancelTransform();
        mYAnim->prp_cancelTransform();
        setRelativePos(mSavedPos);
    }

    void drawSk(SkCanvas * const canvas, const CanvasMode mode,
                const float invScale, const bool keyOnCurrent,
                const bool ctrlPressed) override {
        Q_UNUSED(mode) Q_UNUSED(ctrlPressed)
        drawOnAbsPosSk(canvas, toSkPoint(getAbsolutePos()), invScale,
                       toSkColor(ThemeSupport::getThemeColorYellow()),
                       keyOnCurrent);
    }
private:
    QrealAnimator * const mXAnim;
    QrealAnimator * const mYAnim;
    const int mFrame;
    QPointF mSavedPos;
};

// draggable bezier handle point (in = c0, out = c1); keys are
// resolved from the animators by frame on every access, so deleted
// keys can never be touched (the point just stops doing anything)
class MotionHandlePoint final : public MovablePoint {
    e_OBJECT
public:
    enum class Kind { In, Out };

    MotionHandlePoint(BasicTransformAnimator * const trans,
                      QrealAnimator * const xAnim,
                      QrealAnimator * const yAnim,
                      const int frame,
                      const Kind kind)
        : MovablePoint(trans, TYPE_CTRL_POINT)
        , mXAnim(xAnim), mYAnim(yAnim), mFrame(frame), mKind(kind)
    {
        disableSelection();
        setRadius(6);
    }

    GraphKey *xKey() const {
        return mXAnim ? mXAnim->template anim_getKeyAtAbsFrame<GraphKey>(
                    mFrame) : nullptr;
    }
    GraphKey *yKey() const {
        return mYAnim ? mYAnim->template anim_getKeyAtAbsFrame<GraphKey>(
                    mFrame) : nullptr;
    }

    // handle position in parent space: key value + handle value offset
    QPointF getRelativePos() const override {
        const auto xk = xKey();
        const auto yk = yKey();
        if (!xk || !yk) { return {}; }
        const bool isIn = mKind == Kind::In;
        const bool enabled = isIn ? xk->getC0Enabled() : xk->getC1Enabled();
        if (!enabled) {
            // ungrabbed handle: show on the linear extrapolation towards
            // the neighbouring key (AE auto-bezier look) so it can be
            // grabbed; purely visual - nothing is written until dragged
            return extrapolatedPos();
        }
        return {isIn ? xk->getC0Value() : xk->getC1Value(),
                isIn ? yk->getC0Value() : yk->getC1Value()};
    }

    void setRelativePos(const QPointF &relPos) override {
        const auto xk = xKey();
        const auto yk = yKey();
        if (!xk || !yk) { return; }
        const bool isIn = mKind == Kind::In;
        // graph c0/c1 are (frame, value) points: when a handle is
        // enabled the control point also needs a sane TIME - leaving
        // it at the key frame degenerates the easing bezier and
        // visibly shifts the animation
        const auto enableWithDefaultFrame = [](GraphKey * const k,
                                                const bool in) {
            if (in ? k->getC0Enabled() : k->getC1Enabled()) { return; }
            const int here = k->getRelFrame();
            const auto other = in ? k->getNextKeyRelFrame()
                                  : k->getPrevKeyRelFrame();
            const auto fallback = in ? k->getPrevKeyRelFrame()
                                     : k->getNextKeyRelFrame();
            int span = (other == here ? fallback : other) - here;
            if (span == 0) { span = 1; }
            if (in) { k->setC0Frame(here - span/3.); }
            else    { k->setC1Frame(here + span/3.); }
            if (in) { k->setC0Enabled(true); }
            else    { k->setC1Enabled(true); }
        };
        enableWithDefaultFrame(xk, isIn);
        enableWithDefaultFrame(yk, isIn);
        if (isIn) {
            xk->setC0Value(relPos.x());
            yk->setC0Value(relPos.y());
        } else {
            xk->setC1Value(relPos.x());
            yk->setC1Value(relPos.y());
        }
    }

    void startTransform() override {
        // keep mSavedRelPos for the absolute drag base (see MotionKeyPoint)
        MovablePoint::startTransform();
        const auto xk = xKey();
        const auto yk = yKey();
        if (!xk || !yk) { return; }
        xk->startCtrlPointsValueTransform();
        yk->startCtrlPointsValueTransform();
    }

    // AE-style: editable with the regular transform tool as well
    bool isVisible(const CanvasMode mode) const override {
        return mode == CanvasMode::boxTransform ||
               mode == CanvasMode::pointTransform;
    }

    void finishTransform() override {
        const auto xk = xKey();
        const auto yk = yKey();
        if (!xk || !yk) { return; }
        xk->finishCtrlPointsValueTransform();
        yk->finishCtrlPointsValueTransform();
        if (mXAnim) { mXAnim->prp_afterWholeInfluenceRangeChanged(); }
        if (mYAnim) { mYAnim->prp_afterWholeInfluenceRangeChanged(); }
        if (Document::sInstance) { Document::sInstance->actionFinished(); }
    }

    void cancelTransform() override {
        const auto xk = xKey();
        const auto yk = yKey();
        if (!xk || !yk) { return; }
        xk->cancelCtrlPointsValueTransform();
        yk->cancelCtrlPointsValueTransform();
    }

    void drawSk(SkCanvas * const canvas, const CanvasMode mode,
                const float invScale, const bool keyOnCurrent,
                const bool ctrlPressed) override {
        Q_UNUSED(mode) Q_UNUSED(keyOnCurrent) Q_UNUSED(ctrlPressed)
        drawOnAbsPosSk(canvas, toSkPoint(getAbsolutePos()), invScale,
                       toSkColor(ThemeSupport::getThemeColorBlue()),
                       false);
    }
private:
    // linear extrapolation one third of the way towards the neighbour
    // key (mirroring the other side when this side has none); the
    // control points stay collinear with the keys, so the easing
    // curve and the actual animation remain unchanged
    QPointF extrapolatedPos() const {
        const auto xk = xKey();
        const auto yk = yKey();
        if (!xk || !yk) { return {}; }
        const bool isIn = mKind == Kind::In;
        const auto pick = [isIn](GraphKey * const k) {
            auto n = isIn ? k->getPrevKey<GraphKey>()
                          : k->getNextKey<GraphKey>();
            if (!n) { n = isIn ? k->getNextKey<GraphKey>()
                               : k->getPrevKey<GraphKey>(); }
            return n;
        };
        const auto xN = pick(xk);
        const auto yN = pick(yk);
        const qreal t = 1./3.;
        const qreal xHere = xk->getValueForGraph();
        const qreal yHere = yk->getValueForGraph();
        const qreal xThere = xN ? xN->getValueForGraph() : xHere;
        const qreal yThere = yN ? yN->getValueForGraph() : yHere;
        return {xHere + (xThere - xHere)*t,
                yHere + (yThere - yHere)*t};
    }

    QPointer<QrealAnimator> mXAnim;
    QPointer<QrealAnimator> mYAnim;
    const int mFrame;
    const Kind mKind;
};

} // namespace

QDomElement MotionPathHandler::prp_writePropertyXEV_impl(
        const XevExporter& exp) const {
    // overlay only - never serialized
    return exp.createElement("MotionPath");
}

void MotionPathHandler::prp_readPropertyXEV_impl(const QDomElement& ele,
                                                 const XevImporter& imp) {
    Q_UNUSED(ele) Q_UNUSED(imp)
    // overlay only - nothing to read
}

MotionPathHandler::MotionPathHandler(BoxTransformAnimator * const target)
    : Property("motion path")
    , mTarget(target)
{
    // share ONE control block with the Property base: wrapping the raw
    // getter in a second stdsptr would give two independent owners and
    // double-free the handler at destruction (heap corruption when a
    // project is closed/switched)
    const auto points = enve::make_shared<PointsHandler>();
    mPoints = points;
    setPointsHandler(points);
    prp_setName(QStringLiteral("motion path"));
    prp_enabledDrawingOnCanvas();
}

bool MotionPathHandler::shouldDraw() const
{
    // only for selected boxes with 2+ position keys
    const auto pos = mTarget->getPosAnimator();
    if (!pos) { return false; }
    const auto xAnim = pos->getXAnimator();
    if (!xAnim) { return false; }
    if (xAnim->anim_getKeys().count() < 2) { return false; }
    // the handler deliberately lives outside the property tree
    // (overlay, not serialized), so prp_isParentBoxSelected() cannot
    // walk mParent_k here; resolve the owning box through the target
    // animator instead - it is a direct child of the box
    const auto box = mTarget->getFirstAncestor<eBoxOrSound>();
    return box && box->isSelected();
}

void MotionPathHandler::syncPoints()
{
    const auto pos = mTarget->getPosAnimator();
    if (!pos) { return; }
    const auto xAnim = pos->getXAnimator();
    const auto yAnim = pos->getYAnimator();
    if (!xAnim || !yAnim) { return; }

    // rebuild points whenever the key count changes (cheap check;
    // frame moves keep the same point objects and update live)
    const int keyCount = xAnim->anim_getKeys().count();
    if (keyCount == mSyncedKeyCount) { return; }
    mSyncedKeyCount = keyCount;

    mPoints->clear();
    if (keyCount < 2) { return; }

    for (const auto &key : xAnim->anim_getKeys()) {
        auto xKey = const_cast<GraphKey*>(
                    static_cast<GraphKey*>(key));
        const auto yKey = yAnim->template anim_getKeyAtAbsFrame<GraphKey>(
                    xKey->getAbsFrame());
        if (!yKey) { continue; }
        const int frame = xKey->getAbsFrame();
        const auto keyPt = enve::make_shared<MotionKeyPoint>(
                    mTarget, xAnim, yAnim, frame);
        mPoints->appendPt(keyPt);
        const auto inPt = enve::make_shared<MotionHandlePoint>(
                    mTarget, xAnim, yAnim, frame,
                    MotionHandlePoint::Kind::In);
        mPoints->appendPt(inPt);
        const auto outPt = enve::make_shared<MotionHandlePoint>(
                    mTarget, xAnim, yAnim, frame,
                    MotionHandlePoint::Kind::Out);
        mPoints->appendPt(outPt);
    }
}

void MotionPathHandler::prp_drawCanvasControls(
        SkCanvas * const canvas, const CanvasMode mode,
        const float invScale, const bool ctrlPressed)
{
    if (!shouldDraw()) { return; }

    // key/handle points map parent-space values through the parent
    // chain; Property::setPointsHandler seeded them with this
    // handler's (null) transform. Keep them on the target's parent
    // transform so absolute == inheritedTransform * relative, the
    // same mapping the path below uses (idempotent, reparent-safe).
    mPoints->setTransform(mTarget->getParentTransformAnimator());

    syncPoints();

    const auto pos = mTarget->getPosAnimator();
    const auto xAnim = pos->getXAnimator();
    const auto yAnim = pos->getYAnimator();

    // position animator values live in the PARENT coordinate space;
    // the inherited transform (parent chain total) maps them to scene
    const QMatrix m = mTarget->getInheritedTransform();

    // draw the bezier motion path through the position keys: one
    // cubic segment per key pair; the graph c0/c1 values are ABSOLUTE
    // control-point values in (frame, value) space, so the control
    // point in parent space is directly (cValueX, cValueY)
    SkPath path;
    bool started = false;
    GraphKey *prevXKey = nullptr;
    GraphKey *prevYKey = nullptr;
    for (const auto &key : xAnim->anim_getKeys()) {
        auto gk = const_cast<GraphKey*>(
                    static_cast<GraphKey*>(key));
        const auto yKey = yAnim->template anim_getKeyAtAbsFrame<GraphKey>(
                    gk->getAbsFrame());
        if (!yKey) { continue; }
        const int frame = gk->getAbsFrame();
        const QPointF p(xAnim->getBaseValue(frame),
                        yAnim->getBaseValue(frame));
        if (!started) {
            path.moveTo(toSkPoint(m.map(p)));
            started = true;
        } else if (prevXKey && prevYKey) {
            const QPointF c1(prevXKey->getC1Value(),
                             prevYKey->getC1Value());
            const QPointF c0(gk->getC0Value(), yKey->getC0Value());
            path.cubicTo(toSkPoint(m.map(c1)),
                         toSkPoint(m.map(c0)),
                         toSkPoint(m.map(p)));
        }
        prevXKey = gk;
        prevYKey = yKey;
    }

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(1.5f*invScale);
    paint.setColor(SK_ColorBLACK);
    canvas->drawPath(path, paint);
    paint.setStrokeWidth(0.75f*invScale);
    paint.setColor(toSkColor(ThemeSupport::getThemeColorYellow()));
    canvas->drawPath(path, paint);

    // handle lines (key point -> handle points)
    paint.setStrokeWidth(invScale);
    paint.setColor(SK_ColorBLACK);
    for (int i = 0; i + 2 < mPoints->count(); i += 3) {
        const auto keyPt = mPoints->getPointWithId<MovablePoint>(i);
        const auto inPt = mPoints->getPointWithId<MovablePoint>(i + 1);
        const auto outPt = mPoints->getPointWithId<MovablePoint>(i + 2);
        if (!keyPt || !inPt || !outPt) { continue; }
        canvas->drawLine(toSkPoint(keyPt->getAbsolutePos()),
                         toSkPoint(inPt->getAbsolutePos()), paint);
        canvas->drawLine(toSkPoint(keyPt->getAbsolutePos()),
                         toSkPoint(outPt->getAbsolutePos()), paint);
    }

    // draw the points (key points + handle points)
    Property::prp_drawCanvasControls(canvas, mode, invScale, ctrlPressed);
}
