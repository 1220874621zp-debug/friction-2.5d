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

#include "Properties/comboboxproperty.h"
#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "MovablePoints/movablepoint.h"
#include "MovablePoints/pointshandler.h"
#include "skia/skqtconversions.h"
#include "Animators/transformanimator.h"
#include "appsupport.h"
#include "XML/xmlexporthelpers.h"
#include "ReadWrite/evformat.h"
#include "typemenu.h"

#include "include/core/SkVertices.h"

#include <QVector>
#include <cmath>

namespace {

// Axis weights for the tensor-product lattice surface, written into
// w[0..n] for the param t in [0,1] over n cells (n+1 controls).
// smooth = false: bilinear FFD (C0, hard creases per cell).
// smooth = true: clamped uniform B-spline of degree min(3, n)
// (C2 - the "soft rubber" feel: influence spreads over degree+1
// controls with smooth falloff; ends interpolate the corner points).
// Clamped knot vector: p+1 zeros, interior knots i-p, then p+1
// copies of n-p+1, domain [0, n-p+1] (NOT [0, n-p] - the latter
// collapses the vector to all-zeros when n == p and blanks the
// render: every mesh vertex falls onto (0,0)).
void axisWeights(const int n, const bool smooth, const qreal t,
                 qreal * const w)
{
    for(int i = 0; i <= n; i++) w[i] = 0.0;
    if(n < 1) { w[0] = 1.0; return; }
    int p = 1;
    if(smooth) p = qMin(3, n);
    // clamped ends: exact interpolation of the corner controls;
    // also sidesteps the empty-support seed at the domain end
    if(t <= 0.0) { w[0] = 1.0; return; }
    if(t >= 1.0) { w[n] = 1.0; return; }
    if(p == 1) {
        const qreal s = t * n;
        int i = int(std::floor(s));
        if(i > n - 1) i = n - 1;
        const qreal f = s - i;
        w[i] = 1.0 - f;
        w[i + 1] = f;
        return;
    }
    const auto knot = [n, p](const int i) -> qreal {
        if(i <= p) return 0.0;
        if(i >= n + 1) return qreal(n - p + 1);
        return qreal(i - p);
    };
    const qreal x = t * (n - p + 1);
    // span: greatest j in [p, n] with knot(j) <= x
    int j = p;
    for(int k = p + 1; k <= n; k++) {
        if(knot(k) <= x) j = k;
        else break;
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

// default cage rest polygon: n vertices inscribed in the unit domain
QPointF cageRestUV(const int i, const int n)
{
    const qreal ang = qreal(i) / n * 2.0 * M_PI;
    return QPointF(0.5 + 0.5 * std::cos(ang),
                   0.5 + 0.5 * std::sin(ang));
}

// Mean value coordinates (Floater 2003 / Hormann) of point p w.r.t.
// the closed polygon V: normalized weights with partition of unity
// inside, smooth extrapolation outside. Written into w[0..k-1].
void cageWeights(const QVector<QPointF>& V, const QPointF &p,
                 qreal * const w)
{
    const int k = V.count();
    for(int i = 0; i < k; i++) w[i] = 0.0;
    if(k < 3) { w[0] = 1.0; return; }
    if(k > 64) { // absurdly dense cage: fall back to uniform
        for(int i = 0; i < k; i++) w[i] = 1.0 / k;
        return;
    }
    // on-vertex guard
    for(int i = 0; i < k; i++) {
        const QPointF d = V.at(i) - p;
        if(d.x()*d.x() + d.y()*d.y() < 1e-12) { w[i] = 1.0; return; }
    }
    double tanA[64];
    for(int i = 0; i < k; i++) {
        const QPointF &a = V.at(i);
        const QPointF &b = V.at((i + 1) % k);
        const QPointF ra = a - p;
        const QPointF rb = b - p;
        const double cross = double(ra.x())*rb.y() - double(ra.y())*rb.x();
        const double dot = double(ra.x())*rb.x() + double(ra.y())*rb.y();
        const double alpha = std::atan2(cross, dot);
        tanA[i] = qBound(-1e12, std::tan(0.5*alpha), 1e12);
    }
    double sum = 0.0;
    for(int i = 0; i < k; i++) {
        const QPointF r = V.at(i) - p;
        const double len = std::sqrt(double(r.x())*r.x() +
                                     double(r.y())*r.y());
        if(len < 1e-9) { w[i] = 1.0; return; }
        const double raw = (tanA[(i + k - 1) % k] + tanA[i]) / len;
        w[i] = raw;
        sum += raw;
    }
    if(std::abs(sum) < 1e-9) { w[0] = 1.0; return; }
    for(int i = 0; i < k; i++) w[i] /= sum;
}

// falloff weight for a point dPx outside the domain boundary:
// 1 inside, fading to 0 across fallPx
qreal falloffWeight(const qreal dPx, const qreal fallPx,
                    const bool smooth)
{
    if(fallPx <= 0.0) return dPx <= 0.0 ? 1.0 : 0.0;
    const qreal t = 1.0 - qBound(0.0, dPx / fallPx, 1.0);
    if(smooth) return t*t*(3.0 - 2.0*t);
    return t;
}

} // namespace

LatticePointValues::LatticePointValues(const QString &name,
                                       qsptr<QrealAnimator> u,
                                       qsptr<QrealAnimator> v,
                                       qsptr<QrealAnimator> rot,
                                       qsptr<QrealAnimator> scale) :
    StaticComplexAnimator(name)
{
    if(!rot) rot = enve::make_shared<QrealAnimator>(
                0.0, -1440.0, 1440.0, 0.1, QObject::tr("角度"));
    if(!scale) scale = enve::make_shared<QrealAnimator>(
                100.0, 1.0, 1000.0, 1.0, QObject::tr("缩放"));
    ca_addChild(u);
    ca_addChild(v);
    ca_addChild(rot);
    ca_addChild(scale);
}

void LatticePointValues::prp_readProperty_impl(eReadStream &src)
{
    const auto& children = ca_getChildren();
    // files saved before the P1 degrees of freedom carry only X/Y
    const int nRead = src.evFileVersion() < EvFormat::latticeWarpP1 ?
                qMin(2, children.count()) : children.count();
    for(int i = 0; i < nRead; i++) {
        children.at(i)->prp_readProperty(src);
    }
}

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

// Draggable canvas handle for one control point. Writes the point's
// u/v pair (normalized over the rest domain); the drag goes through
// the standard transform protocol so it is single-step undoable and
// keyframable like every other property. In cage mode the point
// carries a context menu to insert/delete polygon vertices.
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

    void canvasContextMenu(PointTypeMenu * const menu) override {
        const auto eff = mEffect.data();
        if(!eff || eff->latticeMode() != 1) return;
        if(menu->hasActionsForType<LatticePoint>()) return;
        menu->addedActionsForType<LatticePoint>();
        const PointTypeMenu::PlainSelectedOp<LatticePoint> insOp =
                [eff = mEffect, id = mId](LatticePoint *) {
            if(eff) eff->insertPointAfter(id);
        };
        menu->addPlainAction(QIcon::fromTheme("plus"),
                             QObject::tr("在此点后插入"), insOp);
        const PointTypeMenu::PlainSelectedOp<LatticePoint> delOp =
                [eff = mEffect, id = mId](LatticePoint *) {
            if(eff) eff->removePoint(id);
        };
        menu->addPlainAction(QIcon::fromTheme("trash"),
                             QObject::tr("删除此点"), delOp);
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
    mMode = enve::make_shared<ComboBoxProperty>(
                QObject::tr("变形模式"), QStringList()
                << QObject::tr("晶格")
                << QObject::tr("笼形"));
    mMode->setCurrentValue(0);
    ca_addChild(mMode);

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

    mFieldMode = enve::make_shared<ComboBoxProperty>(
                QObject::tr("场模式"), QStringList()
                << QObject::tr("跟随图层")
                << QObject::tr("固定场（世界）"));
    mFieldMode->setCurrentValue(0);
    ca_addChild(mFieldMode);

    mDensity = enve::make_shared<QrealAnimator>(6.0, 1.0, 16.0, 1.0,
                                                QObject::tr("渲染密度"));
    ca_addChild(mDensity);

    mMarginPct = enve::make_shared<QrealAnimator>(10.0, 0.0, 200.0, 1.0,
                                                  QObject::tr("边距 %"));
    ca_addChild(mMarginPct);

    mFalloff = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0,
                                                QObject::tr("衰减 %"));
    ca_addChild(mFalloff);

    mFalloffType = enve::make_shared<ComboBoxProperty>(
                QObject::tr("衰减曲线"), QStringList()
                << QObject::tr("线性")
                << QObject::tr("平滑"));
    mFalloffType->setCurrentValue(1);
    ca_addChild(mFalloffType);

    mFieldGroup = enve::make_shared<StaticComplexAnimator>(
                QObject::tr("固定场"));
    mFieldX = enve::make_shared<QrealAnimator>(0.0, -100000.0, 100000.0,
                                               1.0, QObject::tr("场 X"));
    mFieldY = enve::make_shared<QrealAnimator>(0.0, -100000.0, 100000.0,
                                               1.0, QObject::tr("场 Y"));
    mFieldW = enve::make_shared<QrealAnimator>(0.0, 0.0, 100000.0,
                                               1.0, QObject::tr("场宽"));
    mFieldH = enve::make_shared<QrealAnimator>(0.0, 0.0, 100000.0,
                                               1.0, QObject::tr("场高"));
    mFieldGroup->ca_addChild(mFieldX);
    mFieldGroup->ca_addChild(mFieldY);
    mFieldGroup->ca_addChild(mFieldW);
    mFieldGroup->ca_addChild(mFieldH);
    ca_addChild(mFieldGroup);

    mPoints = enve::make_shared<LatticePointsGroup>();
    ca_addChild(mPoints);

    // the control set is derived from mode/row/col counts - kept in
    // sync on every change so saved structure and values agree
    connect(mMode.get(), &ComboBoxProperty::valueChanged,
            this, [this](const int) { rebuildPoints(); });
    connect(mRows.get(), &ComboBoxProperty::valueChanged,
            this, [this](const int) { rebuildPoints(); });
    connect(mCols.get(), &ComboBoxProperty::valueChanged,
            this, [this](const int) { rebuildPoints(); });
    // switching to the fixed field captures the current world bounds
    // once, so the field starts out covering the artwork
    connect(mFieldMode.get(), &ComboBoxProperty::valueChanged,
            this, [this](const int v) {
        if(v == 1) captureFieldRect();
    });
    rebuildPoints();

    prp_enabledDrawingOnCanvas();

    // Property::prp_drawCanvasControls draws the handler's points and
    // the canvas dispatches dragging through the same handler.
    setPointsHandler(enve::make_shared<PointsHandler>());
    syncHandler();
}

int LatticeWarpEffect::latticeMode() const
{
    return mMode->getCurrentValue();
}

bool LatticeWarpEffect::isFixedField() const
{
    return mFieldMode->getCurrentValue() == 1;
}

QRectF LatticeWarpEffect::fieldRectScene() const
{
    return QRectF(QPointF(mFieldX->getEffectiveValue(),
                          mFieldY->getEffectiveValue()),
                  QSizeF(mFieldW->getEffectiveValue(),
                         mFieldH->getEffectiveValue()));
}

void LatticeWarpEffect::captureFieldRect()
{
    if(fieldRectScene().width() > 1.0 &&
       fieldRectScene().height() > 1.0) {
        return; // already captured / user-edited
    }
    const auto box = getFirstAncestor<BoundingBox>();
    if(!box) return;
    const auto trans = box->getTransformAnimator();
    if(!trans) return;
    const QRectF world = trans->getTotalTransform().mapRect(
                box->getRelBoundingRect());
    const qreal m = mMarginPct->getEffectiveValue() / 100.0;
    const QRectF out = world.adjusted(-world.width()*m, -world.height()*m,
                                      world.width()*m, world.height()*m);
    mFieldX->setCurrentBaseValue(out.left());
    mFieldY->setCurrentBaseValue(out.top());
    mFieldW->setCurrentBaseValue(out.width());
    mFieldH->setCurrentBaseValue(out.height());
}

int LatticeWarpEffect::pointCount() const
{
    if(latticeMode() == 1) return mPoints->pts().count();
    const int pRows = mRows->getCurrentValue() + 2;
    const int pCols = mCols->getCurrentValue() + 2;
    return pRows * pCols;
}

void LatticeWarpEffect::rebuildPoints()
{
    const bool cage = latticeMode() == 1;
    const int count = cage ? 12 : (mRows->getCurrentValue() + 2) *
                                 (mCols->getCurrentValue() + 2);

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
        qreal u0, v0;
        if(cage) {
            const QPointF r = cageRestUV(id, count);
            u0 = r.x(); v0 = r.y();
        } else {
            const int pCols = mCols->getCurrentValue() + 2;
            const int pRows = mRows->getCurrentValue() + 2;
            u0 = qreal(id % pCols) / (pCols - 1);
            v0 = qreal(id / pCols) / (pRows - 1);
        }
        const auto u = enve::make_shared<QrealAnimator>(
                    u0, -5.0, 6.0, 0.001, QStringLiteral("X"));
        const auto v = enve::make_shared<QrealAnimator>(
                    v0, -5.0, 6.0, 0.001, QStringLiteral("Y"));
        mPoints->addPt(enve::make_shared<LatticePointValues>(
                    QObject::tr("点 %1").arg(id + 1), u, v));
    }
    syncHandler();
}

void LatticeWarpEffect::syncHandler()
{
    const auto handler = getPointsHandler();
    if(!handler) return;
    handler->clear();
    const int count = pointCount();
    for(int i = 0; i < count; i++) {
        handler->appendPt(enve::make_shared<LatticePoint>(this, i));
    }
}

void LatticeWarpEffect::insertPointAfter(const int id)
{
    if(latticeMode() != 1) return;
    const auto& children = mPoints->pts();
    const int n = children.count();
    if(id < 0 || id >= n) return;

    // the rest polygon is index-based, so inserting shifts every
    // rest position - rebase all displacements (uv - rest) onto the
    // new n+1 polygon so nothing jumps
    const int nextId = (id + 1) % n;
    for(int k = 0; k < n; k++) {
        const auto pt = enve_cast<LatticePointValues*>(
                    children.at(k).get());
        if(!pt) continue;
        const auto uA = pt->uAnim();
        const auto vA = pt->vAnim();
        if(!uA || !vA) continue;
        const QPointF cur(uA->getEffectiveValue(),
                          vA->getEffectiveValue());
        const QPointF restOld = cageRestUV(k, n);
        const int kNew = k <= id ? k : k + 1;
        const QPointF restNew = cageRestUV(kNew, n + 1);
        uA->setCurrentBaseValue(cur.x() - restOld.x() + restNew.x());
        vA->setCurrentBaseValue(cur.y() - restOld.y() + restNew.y());
    }

    // new vertex: midpoint of the two current neighbors
    const auto a = enve_cast<LatticePointValues*>(children.at(id).get());
    const auto b = enve_cast<LatticePointValues*>(children.at(nextId).get());
    if(!a || !b) return;
    const QPointF uvA(a->uAnim()->getEffectiveValue(),
                      a->vAnim()->getEffectiveValue());
    const QPointF uvB(b->uAnim()->getEffectiveValue(),
                      b->vAnim()->getEffectiveValue());
    const QPointF mid = (uvA + uvB) * 0.5;
    const auto u = enve::make_shared<QrealAnimator>(
                mid.x(), -5.0, 6.0, 0.001, QStringLiteral("X"));
    const auto v = enve::make_shared<QrealAnimator>(
                mid.y(), -5.0, 6.0, 0.001, QStringLiteral("Y"));
    mPoints->insertPt(enve::make_shared<LatticePointValues>(
                QObject::tr("点 %1").arg(id + 2), u, v), id + 1);
    syncHandler();
}

void LatticeWarpEffect::removePoint(const int id)
{
    if(latticeMode() != 1) return;
    const auto& children = mPoints->pts();
    const int n = children.count();
    if(n <= 4) return; // keep a minimal cage
    if(id < 0 || id >= n) return;

    // rebase displacements onto the n-1 rest polygon (skipping the
    // removed index) so nothing jumps
    for(int k = 0; k < n; k++) {
        if(k == id) continue;
        const auto pt = enve_cast<LatticePointValues*>(
                    children.at(k).get());
        if(!pt) continue;
        const auto uA = pt->uAnim();
        const auto vA = pt->vAnim();
        if(!uA || !vA) continue;
        const QPointF cur(uA->getEffectiveValue(),
                          vA->getEffectiveValue());
        const QPointF restOld = cageRestUV(k, n);
        const int kNew = k < id ? k : k - 1;
        const QPointF restNew = cageRestUV(kNew, n - 1);
        uA->setCurrentBaseValue(cur.x() - restOld.x() + restNew.x());
        vA->setCurrentBaseValue(cur.y() - restOld.y() + restNew.y());
    }

    mPoints->removePt(children.at(id));
    syncHandler();
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

QrealAnimator *LatticeWarpEffect::pointRot(const int id) const
{
    const auto& children = mPoints->pts();
    if(id < 0 || id >= children.count()) return nullptr;
    const auto pt = enve_cast<LatticePointValues*>(children.at(id).get());
    if(!pt) return nullptr;
    return pt->rotAnim();
}

QrealAnimator *LatticeWarpEffect::pointScale(const int id) const
{
    const auto& children = mPoints->pts();
    if(id < 0 || id >= children.count()) return nullptr;
    const auto pt = enve_cast<LatticePointValues*>(children.at(id).get());
    if(!pt) return nullptr;
    return pt->scaleAnim();
}

QRectF LatticeWarpEffect::calcRestRectLocal() const
{
    if(isFixedField()) {
        const QRectF f = fieldRectScene();
        if(f.width() > 1.0 && f.height() > 1.0) {
            const auto box = getFirstAncestor<BoundingBox>();
            if(box) {
                const auto trans = box->getTransformAnimator();
                if(trans) {
                    bool ok = false;
                    const QTransform inv =
                            trans->getTotalTransform().inverted(&ok);
                    if(ok) return inv.mapRect(f);
                }
            }
        }
        // degenerate field or missing transform: follow the layer
    }
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
    const QRectF rest = calcRestRectLocal();
    if(rest.width() <= 0.0 || rest.height() <= 0.0) {
        return QMargins(16, 16, 16, 16);
    }

    const bool cage = latticeMode() == 1;
    const auto& children = mPoints->pts();
    const int count = pointCount();
    const int pCols = mCols->getCurrentValue() + 2;
    const int pRows = mRows->getCurrentValue() + 2;
    qreal maxU = 0.0;
    qreal maxV = 0.0;
    for(int id = 0; id < count && id < children.count(); id++) {
        const auto pt = enve_cast<LatticePointValues*>(
                    children.at(id).get());
        if(!pt) continue;
        const auto uA = pt->uAnim();
        const auto vA = pt->vAnim();
        if(!uA || !vA) continue;
        const qreal u = uA->getEffectiveValue(relFrame);
        const qreal v = vA->getEffectiveValue(relFrame);
        QPointF restUV(u, v);
        if(cage) {
            restUV = cageRestUV(id, count);
        } else if(count == pRows * pCols) {
            restUV = QPointF(qreal(id % pCols) / (pCols - 1),
                             qreal(id / pCols) / (pRows - 1));
        }
        // rotation/scale DOF fling content beyond the displacement
        const auto sA = pt->scaleAnim();
        const qreal s = sA ? sA->getEffectiveValue(relFrame) / 100.0 : 1.0;
        const qreal extra = 0.5 * std::abs(s - 1.0);
        const qreal du = std::abs(u - restUV.x()) + extra;
        const qreal dv = std::abs(v - restUV.y()) + extra;
        if(du > maxU) maxU = du;
        if(dv > maxV) maxV = dv;
    }
    const int mx = qCeil(maxU * rest.width() * resolution) + 4;
    const int my = qCeil(maxV * rest.height() * resolution) + 4;
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
        const int count = pointCount();
        QVector<SkPoint> abs(count);
        bool valid = true;
        for(int i = 0; i < count && valid; i++) {
            const QPointF rel = pointLocalPos(i);
            const QPointF mapped = trans ? trans->mapRelPosToAbs(rel)
                                         : rel;
            if(std::isnan(mapped.x()) || std::isnan(mapped.y())) {
                valid = false;
                break;
            }
            abs[i] = toSkPoint(mapped);
        }

        if(valid) {
            SkPaint line;
            line.setAntiAlias(true);
            line.setColor(SkColorSetARGB(210, 90, 220, 130));
            line.setStyle(SkPaint::kStroke_Style);
            line.setStrokeWidth(1.5f * invScale);
            line.setStrokeCap(SkPaint::kRound_Cap);

            SkPath path;
            if(latticeMode() == 1 && count >= 3) {
                // closed deformed cage
                path.moveTo(abs[0]);
                for(int i = 1; i < count; i++) path.lineTo(abs[i]);
                path.close();
            } else if(latticeMode() == 0 &&
                      count == (mRows->getCurrentValue() + 2) *
                               (mCols->getCurrentValue() + 2)) {
                const int pCols = mCols->getCurrentValue() + 2;
                const int pRows = mRows->getCurrentValue() + 2;
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
            }
            canvas->drawPath(path, line);

            // rest domain outline (faint) for reference
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

            // fixed field: the world-anchored rect, drawn in scene
            // (=canvas) coordinates so it stays put while the layer
            // animates through it
            if(isFixedField()) {
                const QRectF f = fieldRectScene();
                if(f.width() > 1.0 && f.height() > 1.0) {
                    SkPaint fld;
                    fld.setAntiAlias(true);
                    fld.setColor(SkColorSetARGB(220, 255, 170, 60));
                    fld.setStyle(SkPaint::kStroke_Style);
                    fld.setStrokeWidth(1.5f * invScale);
                    SkPath fr;
                    fr.addRect(SkRect::MakeXYWH(
                                toSkScalar(f.left()), toSkScalar(f.top()),
                                toSkScalar(f.width()), toSkScalar(f.height())));
                    canvas->drawPath(fr, fld);
                }
            }
        }
    }
    // default: draw the handler's draggable points
    Property::prp_drawCanvasControls(canvas, mode, invScale, ctrlPressed);
}

namespace {

struct LatticeWarpData {
    int mMode = 0;          // 0 lattice, 1 cage
    int mRows = 2;          // cells vertically (lattice)
    int mCols = 3;          // cells horizontally (lattice)
    int mDensity = 6;       // tessellation quads per cell edge
    bool mSmooth = true;
    qreal mMarginPct = 10.0;
    qreal mFalloff = 0.0;   // fraction of domain min dim, 0 = off
    bool mFalloffSmooth = true;
    bool mFixedField = false;
    // content rect in the rendered bitmap (the bitmap may include
    // transparent margin bands accumulated by margin effects)
    QPointF mContentPos;
    QSizeF mContentSize;
    // fixed field rect in RENDERED pixel scene coords (may be invalid)
    QRectF mFieldRect;
    QVector<QPointF> mUV;   // control u/v
    QVector<qreal> mRot;    // degrees per control
    QVector<qreal> mScale;  // percent per control
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
    effData.mMode = latticeMode();
    effData.mRows = mRows->getCurrentValue() + 1; // cells
    effData.mCols = mCols->getCurrentValue() + 1;
    effData.mDensity = qRound(mDensity->getEffectiveValue(relFrame));
    effData.mSmooth = mSmooth->getCurrentValue() != 0;
    effData.mMarginPct = mMarginPct->getEffectiveValue(relFrame);
    effData.mFalloff = mFalloff->getEffectiveValue(relFrame) / 100.0;
    effData.mFalloffSmooth = mFalloffType->getCurrentValue() != 0;
    effData.mFixedField = isFixedField();

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

    if(effData.mFixedField) {
        const QRectF f = fieldRectScene();
        if(f.width() > 1.0 && f.height() > 1.0) {
            effData.mFieldRect = QRectF(
                        f.left() * resolution, f.top() * resolution,
                        f.width() * resolution, f.height() * resolution);
        } else {
            effData.mFixedField = false; // degenerate: follow
        }
    }

    const int count = pointCount();
    effData.mUV.resize(count);
    effData.mRot.resize(count);
    effData.mScale.resize(count);
    const auto& children = mPoints->pts();
    for(int i = 0; i < count; i++) {
        effData.mUV[i] = QPointF(0.5, 0.5);
        effData.mRot[i] = 0.0;
        effData.mScale[i] = 100.0;
        if(i >= children.count()) continue;
        const auto pt = enve_cast<LatticePointValues*>(
                    children.at(i).get());
        if(!pt) continue;
        const auto uA = pt->uAnim();
        const auto vA = pt->vAnim();
        const auto rA = pt->rotAnim();
        const auto sA = pt->scaleAnim();
        if(!uA || !vA) continue;
        effData.mUV[i] = QPointF(uA->getEffectiveValue(relFrame),
                                 vA->getEffectiveValue(relFrame));
        if(rA) effData.mRot[i] = rA->getEffectiveValue(relFrame);
        if(sA) effData.mScale[i] = sA->getEffectiveValue(relFrame);
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

    // rest domain rect in bitmap coords. Fixed field: the captured
    // world rect resolved against the final global rect origin (the
    // warp stays anchored while the layer animates through it).
    // Follow: content rect outset by the margin percentage (bitmap
    // includes accumulated margin bands of earlier effects, so the
    // lattice cannot assume content starts at (0,0))
    qreal cL = 0.0, cT = 0.0, cW = qreal(w), cH = qreal(h);
    bool haveContent = false;
    if(mData.mContentSize.width() > 0.0 &&
       mData.mContentSize.height() > 0.0) {
        const QPointF origin = mData.mContentPos -
                QPointF(data.fPos.x(), data.fPos.y());
        cL = origin.x();
        cT = origin.y();
        cW = mData.mContentSize.width();
        cH = mData.mContentSize.height();
        haveContent = true;
    }
    qreal restL, restT, restW, restH;
    if(mData.mFixedField && mData.mFieldRect.width() > 0.0) {
        restL = mData.mFieldRect.left() - data.fPos.x();
        restT = mData.mFieldRect.top() - data.fPos.y();
        restW = mData.mFieldRect.width();
        restH = mData.mFieldRect.height();
    } else if(haveContent) {
        const qreal mx = cW * mData.mMarginPct / 100.0;
        const qreal my = cH * mData.mMarginPct / 100.0;
        restL = cL - mx;
        restT = cT - my;
        restW = cW + 2.0 * mx;
        restH = cH + 2.0 * my;
    } else {
        const qreal mx = qreal(w) * mData.mMarginPct / 100.0;
        const qreal my = qreal(h) * mData.mMarginPct / 100.0;
        restL = -mx;
        restT = -my;
        restW = w + 2.0 * mx;
        restH = h + 2.0 * my;
    }
    if(restW <= 1.0 || restH <= 1.0) return;

    const int nPts = mData.mUV.count();
    if(nPts < 1) return;
    const int pCols = mData.mCols + 1;
    const int pRows = mData.mRows + 1;
    const bool useCage = mData.mMode == 1 && nPts >= 3 && nPts <= 64;

    // current control positions, rest positions, per-point DOF
    // affine (R*S - I, applied around the CURRENT point position)
    QVector<QPointF> Pcur(nPts);
    QVector<QPointF> Prest(nPts);
    struct Affine { qreal m00, m01, m10, m11; };
    QVector<Affine> M(nPts);
    for(int i = 0; i < nPts; i++) {
        const QPointF& uv = mData.mUV[i];
        Pcur[i] = QPointF(restL + uv.x() * restW,
                          restT + uv.y() * restH);
        QPointF restUV(uv);
        if(useCage) {
            restUV = cageRestUV(i, nPts);
        } else if(nPts == pRows * pCols) {
            restUV = QPointF(qreal(i % pCols) / mData.mCols,
                             qreal(i / pCols) / mData.mRows);
        }
        Prest[i] = QPointF(restL + restUV.x() * restW,
                           restT + restUV.y() * restH);
        const qreal ang = qDegreesToRadians(mData.mRot[i]);
        const qreal s = mData.mScale[i] / 100.0;
        const qreal cs = std::cos(ang) * s;
        const qreal sn = std::sin(ang) * s;
        M[i] = {cs - 1.0, -sn, sn, cs - 1.0};
    }

    // falloff ring: extend the mesh domain by the falloff distance
    // so the displacement fades out smoothly instead of hard-stopping
    const qreal minDim = qMin(restW, restH);
    const qreal fallPx = mData.mFalloff * minDim;
    const int rings = fallPx > 0.5 ? 6 : 0;
    const qreal extU = rings > 0 ? fallPx / restW : 0.0;
    const qreal extV = rings > 0 ? fallPx / restH : 0.0;

    // render mesh: grid over the (extended) domain, evaluate the
    // surface at vertices only, redraw the source through it
    int md = qMax(1, mData.mDensity);
    while((mData.mRows * md + 1) * (mData.mCols * md + 1) > 65535) md--;
    const int step = qMax(1, md / 2);
    const int nvx = mData.mCols * md + 1 + 2 * rings * step;
    const int nvy = mData.mRows * md + 1 + 2 * rings * step;
    const qreal uMin = -extU, uMax = 1.0 + extU;
    const qreal vMin = -extV, vMax = 1.0 + extV;

    QVector<SkPoint> pos(nvx * nvy);
    QVector<SkPoint> tex(nvx * nvy);
    QVector<SkColor> col(nvx * nvy, SkColorSetARGB(255, 255, 255, 255));
    // alpha fade width for texcoords sampling beyond the bitmap
    // (kills edge-clamp smears); at least the falloff distance
    const qreal fadePx = qMax(4.0, fallPx);

    qreal wu[12]; qreal wv[12];
    qreal wc[64];

    for(int b = 0; b < nvy; b++) {
        const qreal v = nvy > 1 ?
                    vMin + (vMax - vMin) * qreal(b) / (nvy - 1) : 0.0;
        const qreal dOutV = std::max(0.0, std::max(-v, v - 1.0));
        for(int a = 0; a < nvx; a++) {
            const qreal u = nvx > 1 ?
                        uMin + (uMax - uMin) * qreal(a) / (nvx - 1) : 0.0;
            const int id = b * nvx + a;

            // displacement falloff outside the [0,1] domain
            const qreal dOutU = std::max(0.0, std::max(-u, u - 1.0));
            const qreal dPx = std::max(dOutU * restW, dOutV * restH);
            const qreal g = falloffWeight(dPx, fallPx,
                                          mData.mFalloffSmooth);

            const qreal tx = restL + u * restW;
            const qreal ty = restT + v * restH;
            qreal x = tx;
            qreal y = ty;
            if(g > 0.0) {
                if(useCage) {
                    // mean-value coordinates against the REST polygon
                    // (weights constant while the cage deforms)
                    cageWeights(Prest, QPointF(tx, ty), wc);
                    for(int j = 0; j < nPts; j++) {
                        const qreal wj = wc[j] * g;
                        if(wj == 0.0) continue;
                        const QPointF d = Pcur[j] - Prest[j];
                        const qreal rx = tx - Pcur[j].x();
                        const qreal ry = ty - Pcur[j].y();
                        x += wj * (d.x() + M[j].m00*rx + M[j].m01*ry);
                        y += wj * (d.y() + M[j].m10*rx + M[j].m11*ry);
                    }
                } else {
                    axisWeights(mData.mRows, mData.mSmooth, v, wv);
                    axisWeights(mData.mCols, mData.mSmooth, u, wu);
                    for(int j = 0; j < pRows; j++) {
                        if(wv[j] == 0.0) continue;
                        for(int i = 0; i < pCols; i++) {
                            if(wu[i] == 0.0) continue;
                            const int idx = j * pCols + i;
                            if(idx >= nPts) continue;
                            const qreal wgt = wu[i] * wv[j] * g;
                            const QPointF d = Pcur[idx] - Prest[idx];
                            const qreal rx = tx - Pcur[idx].x();
                            const qreal ry = ty - Pcur[idx].y();
                            x += wgt * (d.x() + M[idx].m00*rx
                                        + M[idx].m01*ry);
                            y += wgt * (d.y() + M[idx].m10*rx
                                        + M[idx].m11*ry);
                        }
                    }
                }
            }

            pos[id] = SkPoint::Make(toSkScalar(x), toSkScalar(y));
            tex[id] = SkPoint::Make(toSkScalar(tx), toSkScalar(ty));

            // fade alpha where the texcoord samples outside the
            // source bitmap (edge-clamp smear)
            const qreal dxo = std::max(0.0, std::max(-tx, tx - w));
            const qreal dyo = std::max(0.0, std::max(-ty, ty - h));
            const qreal dOut = std::max(dxo, dyo);
            if(dOut > 0.0) {
                const qreal a = 1.0 - qBound(0.0, dOut / fadePx, 1.0);
                col[id] = SkColorSetARGB(qRound(a * 255.0),
                                         255, 255, 255);
            }
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
                pos.count(), pos.constData(), tex.constData(),
                col.constData(),
                idx.count(), idx.constData());

    SkCanvas canvas(dstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.translate(-data.fTexTile.left(), -data.fTexTile.top());
    // vertex colors modulate the shader (the alpha fades the falloff
    // ring and out-of-bitmap regions)
    canvas.drawVertices(vertices.get(), SkBlendMode::kModulate, paint);
}
