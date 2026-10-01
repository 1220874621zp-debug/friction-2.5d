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

#include "cornerpineffect.h"

#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "MovablePoints/movablepoint.h"
#include "MovablePoints/pointshandler.h"
#include "Animators/transformanimator.h"
#include "skia/skqtconversions.h"
#include "appsupport.h"
#include "typemenu.h"

#include "include/core/SkVertices.h"

#include <QVector>
#include <cmath>

CornerPinPointValues::CornerPinPointValues(const QString &name,
                                           qsptr<QrealAnimator> u,
                                           qsptr<QrealAnimator> v) :
    StaticComplexAnimator(name) {
    if(!u) u = enve::make_shared<QrealAnimator>(0.0, -8.0, 8.0, 0.001,
                                                QStringLiteral("X"));
    if(!v) v = enve::make_shared<QrealAnimator>(0.0, -8.0, 8.0, 0.001,
                                                QStringLiteral("Y"));
    ca_addChild(u);
    ca_addChild(v);
}

namespace {

// quad arrays are in square-corner order:
// index 0 = UL (maps (0,0)), 1 = UR ((1,0)), 2 = LR ((1,1)), 3 = LL ((0,1));
// property/pin order is UL, UR, LL, LR - gSquareIndex converts
constexpr int gSquareIndex[4] = {0, 1, 3, 2};

QPointF bilinearQuad(const QPointF* const q,
                     const qreal u, const qreal v) {
    const QPointF top = q[0] + (q[1] - q[0]) * u;
    const QPointF bot = q[3] + (q[2] - q[3]) * u;
    return top + (bot - top) * v;
}

bool finiteQuad(const QPointF* const q) {
    for(int i = 0; i < 4; i++) {
        if(std::isnan(q[i].x()) || std::isnan(q[i].y()) ||
           std::isinf(q[i].x()) || std::isinf(q[i].y())) return false;
    }
    return true;
}

// row-major 3x3
struct Mat3 {
    double m[9];
};

Mat3 mul3x3(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) {
            double s = 0.0;
            for(int k = 0; k < 3; k++)
                s += a.m[i*3 + k] * b.m[k*3 + j];
            r.m[i*3 + j] = s;
        }
    }
    return r;
}

bool inv3x3(const Mat3& a, Mat3& out) {
    const double c0 =  a.m[4]*a.m[8] - a.m[5]*a.m[7];
    const double c1 = -(a.m[3]*a.m[8] - a.m[5]*a.m[6]);
    const double c2 =  a.m[3]*a.m[7] - a.m[4]*a.m[6];
    const double det = a.m[0]*c0 + a.m[1]*c1 + a.m[2]*c2;
    if(std::abs(det) < 1e-12) return false;
    const double id = 1.0 / det;
    out.m[0] = c0 * id;
    out.m[1] = (a.m[2]*a.m[7] - a.m[1]*a.m[8]) * id;
    out.m[2] = (a.m[1]*a.m[5] - a.m[2]*a.m[4]) * id;
    out.m[3] = c1 * id;
    out.m[4] = (a.m[0]*a.m[8] - a.m[2]*a.m[6]) * id;
    out.m[5] = (a.m[2]*a.m[3] - a.m[0]*a.m[5]) * id;
    out.m[6] = c2 * id;
    out.m[7] = (a.m[1]*a.m[6] - a.m[0]*a.m[7]) * id;
    out.m[8] = (a.m[0]*a.m[4] - a.m[1]*a.m[3]) * id;
    return true;
}

// unit square -> quad homography (Heckbert). Returns false for
// degenerate quads (three collinear corners).
bool squareToQuad(const QPointF* const q, Mat3& out) {
    const double x0 = q[0].x(), y0 = q[0].y();
    const double x1 = q[1].x(), y1 = q[1].y();
    const double x2 = q[2].x(), y2 = q[2].y();
    const double x3 = q[3].x(), y3 = q[3].y();
    const double dx1 = x1 - x2, dy1 = y1 - y2;
    const double dx2 = x3 - x2, dy2 = y3 - y2;
    const double den = dx1*dy2 - dy1*dx2;
    if(std::abs(den) < 1e-12) return false;
    const double sx = x0 - x1 + x2 - x3;
    const double sy = y0 - y1 + y2 - y3;
    const double g = (sx*dy2 - sy*dx2) / den;
    const double h = (dx1*sy - dy1*sx) / den;
    out.m[0] = x1 - x0 + g*x1;
    out.m[1] = x3 - x0 + h*x3;
    out.m[2] = x0;
    out.m[3] = y1 - y0 + g*y1;
    out.m[4] = y3 - y0 + h*y3;
    out.m[5] = y0;
    out.m[6] = g;
    out.m[7] = h;
    out.m[8] = 1.0;
    return true;
}

// quad -> quad homography: H = Hdst * Hsrc^-1. Fails when the
// source quad is degenerate or the map folds through infinity
// (perspective denominator not positive on every source corner).
bool quadToQuad(const QPointF* const src, const QPointF* const dst,
                Mat3& out) {
    Mat3 hSrc;
    Mat3 hDst;
    if(!squareToQuad(src, hSrc)) return false;
    if(!squareToQuad(dst, hDst)) return false;
    Mat3 hSrcInv;
    if(!inv3x3(hSrc, hSrcInv)) return false;
    const Mat3 h = mul3x3(hDst, hSrcInv);
    for(int i = 0; i < 4; i++) {
        const double w = h.m[6]*src[i].x() + h.m[7]*src[i].y() + h.m[8];
        if(!(w > 1e-9)) return false;
    }
    out = h;
    return true;
}

QPointF mapPoint(const Mat3& h, const QPointF& p) {
    const double w = h.m[6]*p.x() + h.m[7]*p.y() + h.m[8];
    return QPointF((h.m[0]*p.x() + h.m[1]*p.y() + h.m[2]) / w,
                   (h.m[3]*p.x() + h.m[4]*p.y() + h.m[5]) / w);
}

// rest u/v per pin, property order UL, UR, LL, LR
QPointF restUV(const int id) {
    switch(id) {
    case 1: return QPointF(1.0, 0.0);
    case 2: return QPointF(0.0, 1.0);
    case 3: return QPointF(1.0, 1.0);
    default: return QPointF(0.0, 0.0);
    }
}

} // namespace

struct CornerPinData {
    bool mHaveQuad = false;   // false: the whole bitmap is the domain
    QPointF mQuadSrc[4];      // content quad, rendered scene px
    QPointF mUV[4];           // pin u/v, property order UL UR LL LR
};

// Draggable canvas handle for one pin corner. Writes the corner's
// u/v pair (normalized over the content bounds) through the standard
// transform protocol so it is single-step undoable and keyframable
// like every other property. The context menu offers resetting this
// corner or all four back to the content bounds.
class CornerPinHandle : public MovablePoint {
    e_OBJECT
protected:
    CornerPinHandle(CornerPinEffect * const effect, const int id) :
        MovablePoint(MovablePointType::TYPE_GRADIENT_POINT),
        mEffect(effect), mId(id) {
        setRadius(8);
        setSelectionEnabled(false);
    }
public:
    bool isVisible(const CanvasMode mode) const {
        Q_UNUSED(mode)
        return true;
    }

    QPointF getRelativePos() const {
        const auto eff = mEffect.data();
        if(!eff) return QPointF();
        return eff->pointLocalPos(mId);
    }

    void setRelativePos(const QPointF &relPos) {
        const auto eff = mEffect.data();
        if(!eff) return;
        eff->setPointLocalPos(mId, relPos);
    }

    void startTransform() {
        MovablePoint::startTransform();
        const auto eff = mEffect.data();
        if(!eff) return;
        eff->startPointTransform(mId);
    }

    void finishTransform() {
        const auto eff = mEffect.data();
        if(!eff) return;
        eff->finishPointTransform(mId);
    }

    void cancelTransform() {
        const auto eff = mEffect.data();
        if(!eff) return;
        eff->cancelPointTransform(mId);
    }

    void canvasContextMenu(PointTypeMenu * const menu) override {
        const auto eff = mEffect.data();
        if(!eff) return;
        if(menu->hasActionsForType<CornerPinHandle>()) return;
        menu->addedActionsForType<CornerPinHandle>();
        const PointTypeMenu::PlainSelectedOp<CornerPinHandle> resetOp =
                [eff = mEffect, id = mId](CornerPinHandle *) {
            if(eff) eff->resetPoint(id);
        };
        menu->addPlainAction(QIcon::fromTheme("view-refresh"),
                             QObject::tr("复位此角"), resetOp);
        const PointTypeMenu::PlainSelectedOp<CornerPinHandle> resetAllOp =
                [eff = mEffect](CornerPinHandle *) {
            if(eff) eff->resetAllPoints();
        };
        menu->addPlainAction(QIcon::fromTheme("view-refresh"),
                             QObject::tr("复位全部角点"), resetAllOp);
    }

    void drawSk(SkCanvas * const canvas,
                const CanvasMode mode,
                const float invScale,
                const bool keyOnCurrent,
                const bool ctrlPressed) {
        Q_UNUSED(mode)
        Q_UNUSED(keyOnCurrent)
        Q_UNUSED(ctrlPressed)

        const auto eff = mEffect.data();
        if(!eff) return;

        // Lazy-sync the host box transform so hit-testing maps the
        // same way as drawing (the effect is constructed before it
        // knows its host box).
        const auto box = eff->getFirstAncestor<BoundingBox>();
        if(!box) return;
        if(getTransform() != box->getTransformAnimator()) {
            setTransform(box->getTransformAnimator());
        }

        const SkPoint absPos = toSkPoint(getAbsolutePos());
        const float r = 5.f * invScale;

        SkPaint pShadow;
        pShadow.setAntiAlias(true);
        pShadow.setColor(SkColorSetARGB(160, 0, 0, 0));
        pShadow.setStyle(SkPaint::kFill_Style);

        SkPaint pFill;
        pFill.setAntiAlias(true);
        pFill.setColor(SkColorSetARGB(255, 255, 255, 255));
        pFill.setStyle(SkPaint::kFill_Style);

        SkPaint pRing;
        pRing.setAntiAlias(true);
        pRing.setColor(SkColorSetARGB(255, 255, 160, 40));
        pRing.setStyle(SkPaint::kStroke_Style);
        pRing.setStrokeWidth(1.5f * invScale);

        canvas->drawCircle(absPos.x(), absPos.y(), r + 2.f*invScale, pShadow);
        canvas->drawCircle(absPos.x(), absPos.y(), r, pFill);
        canvas->drawCircle(absPos.x(), absPos.y(), r, pRing);
    }
private:
    const QPointer<CornerPinEffect> mEffect;
    const int mId;
};

class CornerPinEffectCaller : public RasterEffectCaller {
public:
    CornerPinEffectCaller(const HardwareSupport hwSupport,
                          const CornerPinData& data,
                          const QMargins& margin) :
        RasterEffectCaller(hwSupport, true, margin), mData(data) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
private:
    const CornerPinData mData;
};

CornerPinEffect::CornerPinEffect() :
    RasterEffect(QObject::tr("边角定位 (Corner Pin)"),
                 AppSupport::getRasterEffectHardwareSupport("CornerPin",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::CORNER_PIN) {
    static const char* pointNames[4] = {"上左", "上右", "下左", "下右"};
    for(int i = 0; i < 4; i++) {
        const QPointF rest = restUV(i);
        const auto u = enve::make_shared<QrealAnimator>(
                    rest.x(), -8.0, 8.0, 0.001, QStringLiteral("X"));
        const auto v = enve::make_shared<QrealAnimator>(
                    rest.y(), -8.0, 8.0, 0.001, QStringLiteral("Y"));
        mPts[i] = enve::make_shared<CornerPinPointValues>(
                    QObject::tr(pointNames[i]), u, v);
        ca_addChild(mPts[i]);
        connect(u.get(), &QrealAnimator::effectiveValueChanged,
                this, &RasterEffect::forcedMarginChanged);
        connect(v.get(), &QrealAnimator::effectiveValueChanged,
                this, &RasterEffect::forcedMarginChanged);
    }

    prp_enabledDrawingOnCanvas();

    // Property::prp_drawCanvasControls draws the handler's points and
    // the canvas dispatches dragging through the same handler.
    setPointsHandler(enve::make_shared<PointsHandler>());
    syncHandler();
}

void CornerPinEffect::syncHandler() {
    const auto handler = getPointsHandler();
    if(!handler) return;
    handler->clear();
    for(int i = 0; i < 4; i++) {
        handler->appendPt(enve::make_shared<CornerPinHandle>(this, i));
    }
}

int CornerPinEffect::pointCount() const { return 4; }

CornerPinPointValues *CornerPinEffect::point(const int id) const {
    if(id < 0 || id > 3) return nullptr;
    return mPts[id].get();
}

QRectF CornerPinEffect::calcRestRectLocal() const {
    const auto box = getFirstAncestor<BoundingBox>();
    if(!box) return QRectF(0, 0, 100, 100);
    return box->getRelBoundingRect();
}

QPointF CornerPinEffect::pointLocalPos(const int id) const {
    const auto pt = point(id);
    if(!pt) return QPointF();
    const auto uA = pt->uAnim();
    const auto vA = pt->vAnim();
    if(!uA || !vA) return QPointF();
    const QRectF r = calcRestRectLocal();
    return QPointF(r.left() + uA->getEffectiveValue() * r.width(),
                   r.top() + vA->getEffectiveValue() * r.height());
}

void CornerPinEffect::setPointLocalPos(const int id, const QPointF &pos) {
    auto pt = point(id);
    if(!pt) return;
    auto uA = pt->uAnim();
    auto vA = pt->vAnim();
    if(!uA || !vA) return;
    const QRectF r = calcRestRectLocal();
    const qreal u = r.width() > 0.0 ?
                (pos.x() - r.left()) / r.width() : 0.0;
    const qreal v = r.height() > 0.0 ?
                (pos.y() - r.top()) / r.height() : 0.0;
    uA->setCurrentBaseValue(u);
    vA->setCurrentBaseValue(v);
}

void CornerPinEffect::startPointTransform(const int id) {
    auto pt = point(id);
    if(!pt) return;
    pt->prp_startTransform();
}

void CornerPinEffect::finishPointTransform(const int id) {
    auto pt = point(id);
    if(!pt) return;
    pt->prp_finishTransform();
}

void CornerPinEffect::cancelPointTransform(const int id) {
    auto pt = point(id);
    if(!pt) return;
    pt->prp_cancelTransform();
}

void CornerPinEffect::resetPoint(const int id) {
    auto pt = point(id);
    if(!pt) return;
    const QPointF rest = restUV(id);
    pt->prp_startTransform();
    pt->uAnim()->setCurrentBaseValue(rest.x());
    pt->vAnim()->setCurrentBaseValue(rest.y());
    pt->prp_finishTransform();
}

void CornerPinEffect::resetAllPoints() {
    prp_startTransform();
    for(int i = 0; i < 4; i++) {
        auto pt = point(i);
        if(!pt) continue;
        const QPointF rest = restUV(i);
        pt->uAnim()->setCurrentBaseValue(rest.x());
        pt->vAnim()->setCurrentBaseValue(rest.y());
    }
    prp_finishTransform();
}

QMargins CornerPinEffect::calcMargin(const qreal relFrame,
                                     const qreal resolution) const {
    const QRectF rest = calcRestRectLocal();
    if(rest.width() <= 0.0 || rest.height() <= 0.0) {
        return QMargins(4, 4, 4, 4);
    }
    QPointF quad[4]; // square order
    for(int i = 0; i < 4; i++) {
        const auto pt = point(gSquareIndex[i]);
        const auto uA = pt ? pt->uAnim() : nullptr;
        const auto vA = pt ? pt->vAnim() : nullptr;
        const qreal u = uA ? uA->getEffectiveValue(relFrame) : 0.0;
        const qreal v = vA ? vA->getEffectiveValue(relFrame) : 0.0;
        quad[i] = QPointF(rest.left() + u * rest.width(),
                          rest.top() + v * rest.height());
    }
    QRectF qBBox = QRectF(quad[0], quad[2]).normalized();
    for(int i = 1; i < 4; i++) {
        qBBox = qBBox.united(QRectF(quad[i], quad[i]));
    }
    const int mx = qCeil(qMax(0.0, rest.left() - qBBox.left())
                         * resolution) + 2;
    const int my = qCeil(qMax(0.0, rest.top() - qBBox.top())
                         * resolution) + 2;
    const int mr = qCeil(qMax(0.0, qBBox.right() - rest.right())
                         * resolution) + 2;
    const int mb = qCeil(qMax(0.0, qBBox.bottom() - rest.bottom())
                         * resolution) + 2;
    return QMargins(mx, my, mr, mb);
}

QMargins CornerPinEffect::getMargin() const {
    return calcMargin(anim_getCurrentRelFrame(), 1.0);
}

void CornerPinEffect::prp_drawCanvasControls(
        SkCanvas * const canvas, const CanvasMode mode,
        const float invScale, const bool ctrlPressed) {
    Q_UNUSED(mode)
    Q_UNUSED(ctrlPressed)
    const auto box = getFirstAncestor<BoundingBox>();
    if(box) {
        const auto trans = box->getTransformAnimator();
        QPointF quad[4];
        bool valid = true;
        for(int i = 0; i < 4 && valid; i++) {
            const QPointF rel = pointLocalPos(gSquareIndex[i]);
            const QPointF mapped = trans ? trans->mapRelPosToAbs(rel)
                                         : rel;
            if(std::isnan(mapped.x()) || std::isnan(mapped.y())) {
                valid = false;
            } else {
                quad[i] = mapped;
            }
        }

        if(valid) {
            SkPaint pin;
            pin.setAntiAlias(true);
            pin.setColor(SkColorSetARGB(210, 255, 160, 40));
            pin.setStyle(SkPaint::kStroke_Style);
            pin.setStrokeWidth(1.5f * invScale);
            pin.setStrokeCap(SkPaint::kRound_Cap);

            SkPath path;
            path.moveTo(toSkPoint(quad[0]));
            path.lineTo(toSkPoint(quad[1]));
            path.lineTo(toSkPoint(quad[2]));
            path.lineTo(toSkPoint(quad[3]));
            path.close();
            canvas->drawPath(path, pin);

            // rest domain outline (faint) for reference
            const QRectF rest = calcRestRectLocal();
            const QPointF tl = trans ?
                        trans->mapRelPosToAbs(rest.topLeft()) :
                        rest.topLeft();
            const QPointF br = trans ?
                        trans->mapRelPosToAbs(rest.bottomRight()) :
                        rest.bottomRight();
            SkPaint outline;
            outline.setAntiAlias(true);
            outline.setColor(SkColorSetARGB(110, 255, 160, 40));
            outline.setStyle(SkPaint::kStroke_Style);
            outline.setStrokeWidth(1.f * invScale);
            SkPath rect;
            rect.addRect(SkRect::MakeLTRB(toSkScalar(tl.x()),
                                          toSkScalar(tl.y()),
                                          toSkScalar(br.x()),
                                          toSkScalar(br.y())));
            canvas->drawPath(rect, outline);
        }
    }
    // default: draw the handler's draggable points
    Property::prp_drawCanvasControls(canvas, mode, invScale, ctrlPressed);
}

stdsptr<RasterEffectCaller> CornerPinEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(influence)

    CornerPinData ed;
    for(int i = 0; i < 4; i++) {
        const auto pt = point(i);
        const auto uA = pt ? pt->uAnim() : nullptr;
        const auto vA = pt ? pt->vAnim() : nullptr;
        const qreal u = uA ? uA->getEffectiveValue(relFrame) : 0.0;
        const qreal v = vA ? vA->getEffectiveValue(relFrame) : 0.0;
        ed.mUV[i] = QPointF(u, v);
    }
    if(data) {
        // content quad in rendered scene pixels: the full render
        // transform (layer transform + resolution, camera/perspective
        // aware) applied to the content bounds corners. The final
        // bitmap offset is resolved at processCpu time (fGlobalRect
        // is not settled yet at this point).
        // NOTE: data->fRelBoundingRect is still EMPTY at assembly time
        // (it fills in dataSet()/updateRelBoundingRect, which runs
        // AFTER this pass) - reading it collapsed the source quad to a
        // point and any moved pin erased the whole layer. The box's own
        // rect holds the same value and is valid right now.
        const SkMatrix full = data->getFullRenderTransform();
        const QRectF rel = data->fParentBox ?
                    data->fParentBox->getRelBoundingRect() :
                    data->fRelBoundingRect;
        const QPointF relC[4] = {
            rel.topLeft(), rel.topRight(),
            rel.bottomRight(), rel.bottomLeft()
        };
        for(int i = 0; i < 4; i++) {
            ed.mQuadSrc[i] = toQTransform(full).map(relC[i]);
        }
        ed.mHaveQuad = true;

        // caller margin straight from rendered-space bounds (source
        // quad vs destination quad): unlike calcMargin this also
        // honours layer scale/rotation, which the local-space math
        // could not (a 200% scaled layer clipped its dragged corners).
        QRectF srcBox = QRectF(ed.mQuadSrc[0], ed.mQuadSrc[2]).normalized();
        for(int i = 1; i < 4; i++) {
            srcBox = srcBox.united(QRectF(ed.mQuadSrc[i], ed.mQuadSrc[i]));
        }
        QPointF dstQ[4];
        for(int i = 0; i < 4; i++) {
            const QPointF uv = ed.mUV[gSquareIndex[i]];
            dstQ[i] = bilinearQuad(ed.mQuadSrc, uv.x(), uv.y());
        }
        QRectF dstBox = QRectF(dstQ[0], dstQ[2]).normalized();
        for(int i = 1; i < 4; i++) {
            dstBox = dstBox.united(QRectF(dstQ[i], dstQ[i]));
        }
        const QMargins margin = QMargins(
                    qCeil(qMax(0.0, srcBox.left() - dstBox.left())) + 2,
                    qCeil(qMax(0.0, srcBox.top() - dstBox.top())) + 2,
                    qCeil(qMax(0.0, dstBox.right() - srcBox.right())) + 2,
                    qCeil(qMax(0.0, dstBox.bottom() - srcBox.bottom())) + 2);
        return enve::make_shared<CornerPinEffectCaller>(
                    instanceHwSupport(), ed, margin);
    }

    return enve::make_shared<CornerPinEffectCaller>(
                instanceHwSupport(), ed,
                calcMargin(relFrame, resolution));
}

void CornerPinEffectCaller::processCpu(CpuRenderTools& renderTools,
                                       const CpuRenderData& data) {
    const auto& srcBtmp = renderTools.fSrcBtmp;
    auto& dstBtmp = renderTools.fDstBtmp;
    if(srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
       dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }
    const int w = srcBtmp.width();
    const int h = srcBtmp.height();
    if(w <= 0 || h <= 0) return;

    // source content quad in bitmap pixels
    QPointF qSrc[4]; // square order UL UR LR LL
    if(mData.mHaveQuad) {
        const QPointF off(data.fPos.x(), data.fPos.y());
        for(int i = 0; i < 4; i++) {
            qSrc[i] = mData.mQuadSrc[i] - off;
        }
    } else {
        qSrc[0] = QPointF(0, 0);
        qSrc[1] = QPointF(w, 0);
        qSrc[2] = QPointF(w, h);
        qSrc[3] = QPointF(0, h);
    }
    if(!finiteQuad(qSrc)) return;

    // destination quad: the pins, positioned on the source quad
    QPointF qDst[4];
    bool identity = true;
    for(int i = 0; i < 4; i++) {
        const QPointF uv = mData.mUV[gSquareIndex[i]];
        if(std::isnan(uv.x()) || std::isnan(uv.y())) return;
        const QPointF rest = restUV(gSquareIndex[i]);
        if(std::abs(uv.x() - rest.x()) > 1e-9 ||
           std::abs(uv.y() - rest.y()) > 1e-9) identity = false;
        qDst[i] = bilinearQuad(qSrc, uv.x(), uv.y());
    }

    const sk_sp<SkImage> img = SkImage::MakeFromBitmap(srcBtmp);
    if(!img) return;
    SkCanvas canvas(dstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.translate(-data.fTexTile.left(), -data.fTexTile.top());

    if(identity) {
        // AE passthrough: no resampling drift at the default state
        canvas.drawImage(img, 0.f, 0.f);
        return;
    }

    Mat3 hom;
    if(!quadToQuad(qSrc, qDst, hom)) return; // degenerate: render empty

    // tessellation density: destination extent plus the projective
    // bowing (how far the true map pulls the quad center off the
    // bilinear quad) so strong perspective gets denser meshes
    QRectF dBox = QRectF(qDst[0], qDst[2]).normalized();
    for(int i = 1; i < 4; i++) dBox = dBox.united(QRectF(qDst[i], qDst[i]));
    const qreal maxDim = qMax(dBox.width(), dBox.height());
    const QPointF srcMid = bilinearQuad(qSrc, 0.5, 0.5);
    const QPointF dstMidB = bilinearQuad(qDst, 0.5, 0.5);
    const QPointF dstMidH = mapPoint(hom, srcMid);
    const qreal dev = std::hypot(dstMidH.x() - dstMidB.x(),
                                 dstMidH.y() - dstMidB.y());
    const int nDiv = qBound(12, int(std::ceil(maxDim / 12.0 + dev * 2.0)),
                            96);

    const int nv = (nDiv + 1) * (nDiv + 1);
    QVector<SkPoint> pos(nv);
    QVector<SkPoint> tex(nv);
    QVector<SkColor> col(nv, SkColorSetARGB(255, 255, 255, 255));
    for(int iv = 0; iv <= nDiv; iv++) {
        const qreal v = qreal(iv) / nDiv;
        for(int iu = 0; iu <= nDiv; iu++) {
            const qreal u = qreal(iu) / nDiv;
            const int id = iv * (nDiv + 1) + iu;
            const QPointF s = bilinearQuad(qSrc, u, v);
            const QPointF d = mapPoint(hom, s);
            pos[id] = SkPoint::Make(toSkScalar(d.x()), toSkScalar(d.y()));
            tex[id] = SkPoint::Make(toSkScalar(s.x()), toSkScalar(s.y()));
        }
    }

    QVector<uint16_t> idx;
    idx.reserve(nDiv * nDiv * 6);
    for(int iv = 0; iv < nDiv; iv++) {
        for(int iu = 0; iu < nDiv; iu++) {
            const int i0 = iv * (nDiv + 1) + iu;
            const int i1 = i0 + 1;
            const int i2 = i0 + nDiv + 1;
            const int i3 = i2 + 1;
            idx << uint16_t(i0) << uint16_t(i2) << uint16_t(i1)
                << uint16_t(i1) << uint16_t(i2) << uint16_t(i3);
        }
    }

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setFilterQuality(kMedium_SkFilterQuality);
    paint.setShader(img->makeShader(SkTileMode::kClamp,
                                    SkTileMode::kClamp,
                                    nullptr));
    const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
                SkVertices::kTriangles_VertexMode,
                pos.count(), pos.constData(), tex.constData(),
                col.constData(),
                idx.count(), idx.constData());

    canvas.drawVertices(vertices.get(), SkBlendMode::kModulate, paint);
}
