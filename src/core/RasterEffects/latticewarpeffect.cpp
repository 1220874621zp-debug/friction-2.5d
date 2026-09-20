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

#include "latticewarpeffect.h"

#include "Animators/qrealanimator.h"
#include "Properties/comboboxproperty.h"
#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "MovablePoints/movablepoint.h"
#include "MovablePoints/pointshandler.h"
#include "skia/skqtconversions.h"
#include "Animators/transformanimator.h"
#include "appsupport.h"
#include "XML/xmlexporthelpers.h"

#include "include/core/SkVertices.h"

#include <QVector>
#include <cmath>

namespace {

// Axis weights for the tensor-product lattice surface, written into
// w[0..n] for the param t in [0,1] over n cells (n+1 controls).
// smooth = false: bilinear FFD (C0, hard creases per cell).
// smooth = true: clamped uniform cubic B-spline (C2 - the "soft
// rubber" feel: influence spreads over 4 controls with smooth
// falloff). Falls back to bilinear when there are too few controls.
void axisWeights(const int n, const bool smooth, const qreal t,
                 qreal * const w)
{
    for(int i = 0; i <= n; i++) w[i] = 0.0;
    if(n < 1) { w[0] = 1.0; return; }
    if(!smooth || n < 3) {
        const qreal s = qBound(0.0, t, 1.0) * n;
        int i = int(std::floor(s));
        if(i > n - 1) i = n - 1;
        const qreal f = s - i;
        w[i] = 1.0 - f;
        w[i + 1] = f;
        return;
    }
    const int p = 3;
    // clamped uniform knots over the domain [0, n-p]
    const auto knot = [n, p](const int i) -> qreal {
        if(i <= p) return 0.0;
        if(i > n) return qreal(n - p);
        return qreal(i - p);
    };
    const qreal x = qBound(0.0, t, 1.0) * (n - p);
    // span: greatest j in [p, n] with knot(j) <= x
    int j = n;
    for(int k = p; k <= n; k++) {
        if(knot(k) > x) { j = k - 1; break; }
    }
    // Piegl & Tiller A2.3 basis functions for span j
    qreal N[4]; qreal left[4]; qreal right[4];
    N[0] = 1.0;
    for(int deg = 1; deg <= p; deg++) {
        left[deg] = x - knot(j + 1 - deg);
        right[deg] = knot(j + deg) - x;
        qreal saved = 0.0;
        for(int r = 0; r < deg; r++) {
            const qreal denom = right[r + 1] + left[deg - r];
            qreal temp = 0.0;
            if(denom > 0.0) temp = N[r] / denom;
            N[r] = saved + right[r + 1] * temp;
            saved = left[deg - r] * temp;
        }
        N[deg] = saved;
    }
    for(int r = 0; r <= p; r++) w[j - p + r] = N[r];
}

} // namespace

LatticePointsGroup::LatticePointsGroup() :
    ComplexAnimator(QObject::tr("控制点")) {}

// child-sequential serialization, identical to what
// StaticComplexAnimator provides for its fixed children
void LatticePointsGroup::prp_writeProperty_impl(eWriteStream &dst) const
{
    const auto& children = pts();
    for(const auto& prop : children) prop->prp_writeProperty(dst);
}

void LatticePointsGroup::prp_readProperty_impl(eReadStream &src)
{
    const auto& children = pts();
    for(const auto& prop : children) prop->prp_readProperty(src);
}

void LatticePointsGroup::prp_readPropertyXEV_impl(
        const QDomElement& ele, const XevImporter& imp)
{
    const auto& children = pts();
    for(const auto& c : children) {
        const QString tagName = c->prp_tagNameXEV();
        const auto path = tagName + "/";
        const auto impc = imp.withAssetsPath(path);
        XevExportHelpers::readProperty(ele, impc, tagName, c.get());
    }
}

QDomElement LatticePointsGroup::prp_writePropertyXEV_impl(
        const XevExporter& exp) const
{
    auto result = exp.createElement(prp_tagNameXEV());
    const auto& children = pts();
    for(const auto& c : children) {
        const QString tagName = c->prp_tagNameXEV();
        const auto path = tagName + "/";
        const auto expc = exp.withAssetsPath(path);
        XevExportHelpers::writeProperty(result, *expc, tagName, c.get());
    }
    return result;
}

// Draggable canvas handle for one lattice control point. Writes the
// point's u/v pair (normalized over the rest lattice); the drag goes
// through the standard transform protocol so it is single-step
// undoable and keyframable like every other property.
class LatticePoint : public MovablePoint {
    e_OBJECT
protected:
    LatticePoint(LatticeWarpEffect * const effect, const int id) :
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
        pRing.setColor(SkColorSetARGB(255, 70, 200, 110));
        pRing.setStyle(SkPaint::kStroke_Style);
        pRing.setStrokeWidth(1.5f * invScale);

        canvas->drawCircle(absPos.x(), absPos.y(), r + 2.f*invScale, pShadow);
        canvas->drawCircle(absPos.x(), absPos.y(), r, pFill);
        canvas->drawCircle(absPos.x(), absPos.y(), r, pRing);
    }
private:
    const QPointer<LatticeWarpEffect> mEffect;
    const int mId;
};

LatticeWarpEffect::LatticeWarpEffect() :
    RasterEffect(QObject::tr("晶格变形"),
                 AppSupport::getRasterEffectHardwareSupport("LatticeWarp",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::LATTICE_WARP)
{
    const QStringList dimList = QStringList()
            << QStringLiteral("1") << QStringLiteral("2")
            << QStringLiteral("3") << QStringLiteral("4")
            << QStringLiteral("5") << QStringLiteral("6")
            << QStringLiteral("7") << QStringLiteral("8");

    mRows = enve::make_shared<ComboBoxProperty>(
                QObject::tr("行数"), dimList);
    mRows->setCurrentValue(1); // 2 cells vertically
    ca_addChild(mRows);

    mCols = enve::make_shared<ComboBoxProperty>(
                QObject::tr("列数"), dimList);
    mCols->setCurrentValue(2); // 3 cells horizontally
    ca_addChild(mCols);

    mSmooth = enve::make_shared<ComboBoxProperty>(
                QObject::tr("平滑度"), QStringList()
                << QObject::tr("线性（硬折）")
                << QObject::tr("平滑（软胶）"));
    mSmooth->setCurrentValue(1);
    ca_addChild(mSmooth);

    mDensity = enve::make_shared<QrealAnimator>(6.0, 1.0, 16.0, 1.0,
                                                QObject::tr("渲染密度"));
    ca_addChild(mDensity);

    mMarginPct = enve::make_shared<QrealAnimator>(10.0, 0.0, 200.0, 1.0,
                                                  QObject::tr("边距 %"));
    ca_addChild(mMarginPct);

    mPoints = enve::make_shared<LatticePointsGroup>();
    ca_addChild(mPoints);

    // the point grid is derived from the row/col counts - kept in
    // sync on every change so saved structure and values agree
    connect(mRows.get(), &ComboBoxProperty::valueChanged,
            this, [this](const int) { rebuildPoints(); });
    connect(mCols.get(), &ComboBoxProperty::valueChanged,
            this, [this](const int) { rebuildPoints(); });
    rebuildPoints();

    prp_enabledDrawingOnCanvas();

    // Property::prp_drawCanvasControls draws the handler's points and
    // the canvas dispatches dragging through the same handler.
    setPointsHandler(enve::make_shared<PointsHandler>());
    const auto handler = getPointsHandler();
    const int count = pointCount();
    for(int i = 0; i < count; i++) {
        handler->appendPt(enve::make_shared<LatticePoint>(this, i));
    }
}

int LatticeWarpEffect::pointCount() const
{
    const int pRows = mRows->getCurrentValue() + 2;
    const int pCols = mCols->getCurrentValue() + 2;
    return pRows * pCols;
}

QrealAnimator *LatticeWarpEffect::pointU(const int id) const
{
    const auto& children = mPoints->pts();
    if(id < 0 || id >= children.count()) return nullptr;
    const auto pt = enve_cast<LatticePointValues*>(children.at(id).get());
    if(!pt) return nullptr;
    return pt->uAnim();
}

QrealAnimator *LatticeWarpEffect::pointV(const int id) const
{
    const auto& children = mPoints->pts();
    if(id < 0 || id >= children.count()) return nullptr;
    const auto pt = enve_cast<LatticePointValues*>(children.at(id).get());
    if(!pt) return nullptr;
    return pt->vAnim();
}

void LatticeWarpEffect::rebuildPoints()
{
    // cells per axis -> (cells+1) control points per axis
    const int pCols = mCols->getCurrentValue() + 2;
    const int pRows = mRows->getCurrentValue() + 2;
    const int count = pRows * pCols;

    while(true) {
        const auto& children = mPoints->pts();
        if(children.count() <= count) break;
        const auto last = children.last();
        mPoints->removePt(last);
    }
    while(true) {
        const auto& children = mPoints->pts();
        if(children.count() >= count) break;
        const int id = children.count();
        const int ix = id % pCols;
        const int iy = id / pCols;
        // rest params: uniform grid over [0,1]
        const auto u = enve::make_shared<QrealAnimator>(
                    qreal(ix) / (pCols - 1), -5.0, 6.0, 0.001,
                    QStringLiteral("X"));
        const auto v = enve::make_shared<QrealAnimator>(
                    qreal(iy) / (pRows - 1), -5.0, 6.0, 0.001,
                    QStringLiteral("Y"));
        mPoints->addPt(enve::make_shared<LatticePointValues>(
                    QObject::tr("点 %1").arg(id + 1), u, v));
    }

    // keep the canvas handler in sync with the animator grid
    const auto handler = getPointsHandler();
    if(handler) {
        handler->clear();
        for(int i = 0; i < count; i++) {
            handler->appendPt(enve::make_shared<LatticePoint>(this, i));
        }
    }
}

QRectF LatticeWarpEffect::calcRestRectLocal() const
{
    const auto box = getFirstAncestor<BoundingBox>();
    if(!box) return QRectF(0, 0, 100, 100);
    const QRectF b = box->getRelBoundingRect();
    const qreal m = mMarginPct->getEffectiveValue() / 100.0;
    const qreal dx = b.width() * m;
    const qreal dy = b.height() * m;
    return b.adjusted(-dx, -dy, dx, dy);
}

QPointF LatticeWarpEffect::pointLocalPos(const int id) const
{
    const auto uA = pointU(id);
    const auto vA = pointV(id);
    if(!uA || !vA) return QPointF();
    const QRectF r = calcRestRectLocal();
    return QPointF(r.left() + uA->getEffectiveValue() * r.width(),
                   r.top() + vA->getEffectiveValue() * r.height());
}

void LatticeWarpEffect::setPointLocalPos(const int id, const QPointF &pos)
{
    auto uA = pointU(id);
    auto vA = pointV(id);
    if(!uA || !vA) return;
    const QRectF r = calcRestRectLocal();
    const qreal u = r.width() > 0.0 ?
                (pos.x() - r.left()) / r.width() : 0.0;
    const qreal v = r.height() > 0.0 ?
                (pos.y() - r.top()) / r.height() : 0.0;
    uA->setCurrentBaseValue(u);
    vA->setCurrentBaseValue(v);
}

void LatticeWarpEffect::startPointTransform(const int id)
{
    const auto uA = pointU(id);
    const auto vA = pointV(id);
    if(uA) uA->prp_startTransform();
    if(vA) vA->prp_startTransform();
}

void LatticeWarpEffect::finishPointTransform(const int id)
{
    const auto uA = pointU(id);
    const auto vA = pointV(id);
    if(uA) uA->prp_finishTransform();
    if(vA) vA->prp_finishTransform();
}

void LatticeWarpEffect::cancelPointTransform(const int id)
{
    const auto uA = pointU(id);
    const auto vA = pointV(id);
    if(uA) uA->prp_cancelTransform();
    if(vA) vA->prp_cancelTransform();
}

QMargins LatticeWarpEffect::calcMargin(const qreal relFrame,
                                       const qreal resolution) const
{
    const auto box = getFirstAncestor<BoundingBox>();
    if(!box) return QMargins(16, 16, 16, 16);
    const QRectF b = box->getRelBoundingRect();
    const qreal m = mMarginPct->getEffectiveValue(relFrame) / 100.0;
    const qreal restW = b.width() * (1.0 + 2.0 * m);
    const qreal restH = b.height() * (1.0 + 2.0 * m);

    const int pCols = mCols->getCurrentValue() + 2;
    const int pRows = mRows->getCurrentValue() + 2;
    const int nCols = pCols - 1; // cells
    const int nRows = pRows - 1;
    qreal maxU = 0.0;
    qreal maxV = 0.0;
    const auto& children = mPoints->pts();
    for(int j = 0; j < pRows; j++) {
        for(int i = 0; i < pCols; i++) {
            const int id = j * pCols + i;
            if(id >= children.count()) break;
            const auto pt = enve_cast<LatticePointValues*>(
                        children.at(id).get());
            if(!pt) continue;
            const auto uA = pt->uAnim();
            const auto vA = pt->vAnim();
            if(!uA || !vA) continue;
            const qreal du = std::abs(uA->getEffectiveValue(relFrame)
                                      - qreal(i) / nCols);
            const qreal dv = std::abs(vA->getEffectiveValue(relFrame)
                                      - qreal(j) / nRows);
            if(du > maxU) maxU = du;
            if(dv > maxV) maxV = dv;
        }
    }
    const int mx = qCeil(maxU * restW * resolution) + 2;
    const int my = qCeil(maxV * restH * resolution) + 2;
    return QMargins(mx, my, mx, my);
}

QMargins LatticeWarpEffect::getMargin() const
{
    // planning margin at the current frame (render path passes the
    // exact relFrame into calcMargin from getEffectCaller)
    return calcMargin(mMarginPct->anim_getCurrentRelFrame(), 1.0);
}

void LatticeWarpEffect::prp_drawCanvasControls(
        SkCanvas * const canvas, const CanvasMode mode,
        const float invScale, const bool ctrlPressed)
{
    const auto box = getFirstAncestor<BoundingBox>();
    if(box) {
        const auto trans = box->getTransformAnimator();
        const int pCols = mCols->getCurrentValue() + 2;
        const int pRows = mRows->getCurrentValue() + 2;

        // current (deformed) control positions in canvas coordinates
        QVector<SkPoint> abs(pRows * pCols);
        bool valid = true;
        for(int j = 0; j < pRows && valid; j++) {
            for(int i = 0; i < pCols; i++) {
                const QPointF rel = pointLocalPos(j * pCols + i);
                const QPointF mapped = trans ? trans->mapRelPosToAbs(rel)
                                             : rel;
                if(std::isnan(mapped.x()) || std::isnan(mapped.y())) {
                    valid = false;
                    break;
                }
                abs[j * pCols + i] = toSkPoint(mapped);
            }
        }

        if(valid) {
            SkPaint line;
            line.setAntiAlias(true);
            line.setColor(SkColorSetARGB(210, 90, 220, 130));
            line.setStyle(SkPaint::kStroke_Style);
            line.setStrokeWidth(1.5f * invScale);
            line.setStrokeCap(SkPaint::kRound_Cap);

            SkPath path;
            for(int j = 0; j < pRows; j++) {
                path.moveTo(abs[j * pCols]);
                for(int i = 1; i < pCols; i++) {
                    path.lineTo(abs[j * pCols + i]);
                }
            }
            for(int i = 0; i < pCols; i++) {
                path.moveTo(abs[i]);
                for(int j = 1; j < pRows; j++) {
                    path.lineTo(abs[j * pCols + i]);
                }
            }
            canvas->drawPath(path, line);

            // rest lattice outline (faint) for reference
            const QRectF rest = calcRestRectLocal();
            const QPointF tl = trans ? trans->mapRelPosToAbs(rest.topLeft())
                                     : rest.topLeft();
            const QPointF br = trans ? trans->mapRelPosToAbs(rest.bottomRight())
                                     : rest.bottomRight();
            SkPaint outline;
            outline.setAntiAlias(true);
            outline.setColor(SkColorSetARGB(110, 90, 220, 130));
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

namespace {

struct LatticeWarpData {
    int mRows = 2;         // cells vertically
    int mCols = 3;         // cells horizontally
    int mDensity = 6;      // tessellation quads per cell edge
    bool mSmooth = true;
    qreal mMarginPct = 10.0;
    // content rect in the rendered bitmap: the bitmap may include
    // transparent margin bands accumulated by margin effects, so the
    // lattice cannot assume the content starts at (0,0)
    QPointF mContentPos;
    QSizeF mContentSize;
    QVector<QPointF> mUV;  // row-major (rows+1)x(cols+1)
};

} // namespace

class LatticeWarpEffectCaller : public RasterEffectCaller {
public:
    LatticeWarpEffectCaller(const HardwareSupport hwSupport,
                            const LatticeWarpData& data,
                            const QMargins& margin) :
        RasterEffectCaller(hwSupport, true, margin), mData(data) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
private:
    const LatticeWarpData mData;
};

stdsptr<RasterEffectCaller> LatticeWarpEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(influence)

    LatticeWarpData effData;
    effData.mRows = mRows->getCurrentValue() + 1; // cells
    effData.mCols = mCols->getCurrentValue() + 1;
    effData.mDensity = qRound(mDensity->getEffectiveValue(relFrame));
    effData.mSmooth = mSmooth->getCurrentValue() != 0;
    effData.mMarginPct = mMarginPct->getEffectiveValue(relFrame);

    // scene position of the content top-left (margin-independent);
    // resolved against the final global rect inside processCpu
    QPointF contentScene(0.0, 0.0);
    QSizeF contentSize(0.0, 0.0);
    if(data) {
        const auto scaled = data->fTotalTransform * data->fResolutionScale;
        const QRectF rel = data->fRelBoundingRect;
        contentScene = scaled.map(rel.topLeft());
        contentSize = QSizeF(rel.width() * resolution,
                             rel.height() * resolution);
    }
    effData.mContentPos = contentScene;
    effData.mContentSize = contentSize;

    const int cn = effData.mCols + 1; // controls horizontally
    const int rn = effData.mRows + 1; // controls vertically
    effData.mUV.resize(cn * rn);
    const auto& children = mPoints->pts();
    for(int j = 0; j < rn; j++) {
        for(int i = 0; i < cn; i++) {
            const int id = j * cn + i;
            effData.mUV[id] = QPointF(qreal(i) / effData.mCols,
                                      qreal(j) / effData.mRows);
            if(id >= children.count()) continue;
            const auto pt = enve_cast<LatticePointValues*>(
                        children.at(id).get());
            if(!pt) continue;
            const auto uA = pt->uAnim();
            const auto vA = pt->vAnim();
            if(!uA || !vA) continue;
            effData.mUV[id] = QPointF(uA->getEffectiveValue(relFrame),
                                      vA->getEffectiveValue(relFrame));
        }
    }

    return enve::make_shared<LatticeWarpEffectCaller>(
                instanceHwSupport(), effData,
                calcMargin(relFrame, resolution));
}

void LatticeWarpEffectCaller::processCpu(CpuRenderTools& renderTools,
                                         const CpuRenderData& data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    auto& dstBtmp = renderTools.fDstBtmp;
    if(srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
       dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }
    const int w = srcBtmp.width();
    const int h = srcBtmp.height();
    if(w <= 0 || h <= 0) return;

    // content rect in bitmap coords: resolved from the scene position
    // against the final global rect (data.fPos = bitmap origin in
    // scene coords); fall back to the whole bitmap
    qreal cL = 0.0, cT = 0.0, cW = qreal(w), cH = qreal(h);
    if(mData.mContentSize.width() > 0.0 &&
       mData.mContentSize.height() > 0.0) {
        const QPointF origin = mData.mContentPos -
                QPointF(data.fPos.x(), data.fPos.y());
        cL = origin.x();
        cT = origin.y();
        cW = mData.mContentSize.width();
        cH = mData.mContentSize.height();
    }

    // rest lattice: content rect outset by the margin percentage
    const qreal mx = cW * mData.mMarginPct / 100.0;
    const qreal my = cH * mData.mMarginPct / 100.0;
    const qreal restL = cL - mx;
    const qreal restT = cT - my;
    const qreal restW = cW + 2.0 * mx;
    const qreal restH = cH + 2.0 * my;

    const int rn = mData.mRows + 1;  // controls vertically
    const int cn = mData.mCols + 1;  // controls horizontally

    // current control positions (bitmap space)
    QVector<QPointF> P(cn * rn);
    for(int j = 0; j < rn; j++) {
        for(int i = 0; i < cn; i++) {
            const QPointF& uv = mData.mUV[j * cn + i];
            P[j * cn + i] = QPointF(restL + uv.x() * restW,
                                    restT + uv.y() * restH);
        }
    }

    // render mesh: tessellate the lattice domain, evaluate the
    // surface at vertices only, redraw the source through it
    int md = qMax(1, mData.mDensity);
    while((mData.mRows * md + 1) * (mData.mCols * md + 1) > 65535) md--;
    const int nvx = mData.mCols * md + 1;
    const int nvy = mData.mRows * md + 1;

    QVector<SkPoint> pos(nvx * nvy);
    QVector<SkPoint> tex(nvx * nvy);
    qreal wu[10]; qreal wv[10];
    for(int b = 0; b < nvy; b++) {
        const qreal v = nvy > 1 ? qreal(b) / (nvy - 1) : 0.0;
        axisWeights(mData.mRows, mData.mSmooth, v, wv);
        for(int a = 0; a < nvx; a++) {
            const qreal u = nvx > 1 ? qreal(a) / (nvx - 1) : 0.0;
            axisWeights(mData.mCols, mData.mSmooth, u, wu);
            qreal x = 0.0;
            qreal y = 0.0;
            for(int j = 0; j < rn; j++) {
                if(wv[j] == 0.0) continue;
                for(int i = 0; i < cn; i++) {
                    if(wu[i] == 0.0) continue;
                    const qreal wgt = wu[i] * wv[j];
                    const QPointF& p = P[j * cn + i];
                    x += wgt * p.x();
                    y += wgt * p.y();
                }
            }
            const int id = b * nvx + a;
            pos[id] = SkPoint::Make(toSkScalar(x), toSkScalar(y));
            tex[id] = SkPoint::Make(toSkScalar(restL + u * restW),
                                    toSkScalar(restT + v * restH));
        }
    }

    QVector<uint16_t> idx;
    idx.reserve((nvx - 1) * (nvy - 1) * 6);
    for(int b = 0; b < nvy - 1; b++) {
        for(int a = 0; a < nvx - 1; a++) {
            const int i0 = b * nvx + a;
            const int i1 = i0 + 1;
            const int i2 = i0 + nvx;
            const int i3 = i2 + 1;
            idx << uint16_t(i0) << uint16_t(i2) << uint16_t(i1)
                << uint16_t(i1) << uint16_t(i2) << uint16_t(i3);
        }
    }

    const sk_sp<SkImage> img = SkImage::MakeFromBitmap(srcBtmp);
    if(!img) return;
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setFilterQuality(kMedium_SkFilterQuality);
    paint.setShader(img->makeShader(SkTileMode::kClamp,
                                    SkTileMode::kClamp,
                                    nullptr));
    const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
                SkVertices::kTriangles_VertexMode,
                pos.count(), pos.constData(), tex.constData(), nullptr,
                idx.count(), idx.constData());

    SkCanvas canvas(dstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.translate(-data.fTexTile.left(), -data.fTexTile.top());
    canvas.drawVertices(vertices.get(), SkBlendMode::kSrcOver, paint);
}
