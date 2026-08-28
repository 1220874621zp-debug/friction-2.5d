#include "bonewarpeffect.h"

#include "Animators/qrealanimator.h"
#include "Boxes/boxrenderdata.h"
#include "Boxes/bone.h"
#include "canvas.h"
#include "simpletask.h"
#include "ReadWrite/evformat.h"
#include "Private/document.h"
#include "typemenu.h"

#include <QtMath>
#include <QImage>

// ---------------------------------------------------------------------------
// caller: backward linear-blend skinning in scene space
struct BoneWarpRec {
    QMatrix invCurrent;   // currentTotal⁻¹ * ... maps dest px to bone-bind
    QMatrix toBind;       // bindTotal * currentTotal⁻¹ applied at dest px
    QPointF headCur;      // current head (scene px) - weight anchor
    QPointF tailCur;      // current tail
    qreal radius = 150;   // this bone's falloff radius
    qreal strength = 1;   // this bone's warp blend
};

class BoneWarpCaller : public RasterEffectCaller {
    e_OBJECT
public:
    BoneWarpCaller(const qreal maxRadius) :
        RasterEffectCaller(HardwareSupport::cpuOnly, true,
                           // cap the expansion: a huge radius would
                           // allocate enormous per-thread bitmaps and
                           // blow the heap while dragging the slider
                           QMargins(200, 200, 200, 200)) {}

    // every bone carries its own falloff radius / strength (tuned in
    // the top property bar while the bone is selected)
    void addBone(const QMatrix& bindTotal, const QMatrix& currentTotal,
                 const QPointF& headCur, const QPointF& tailCur,
                 const qreal radius, const qreal strength) {
        BoneWarpRec rec;
        rec.toBind = bindTotal*currentTotal.inverted();
        rec.headCur = headCur;
        rec.tailCur = tailCur;
        rec.radius = qMax(10., radius);
        rec.strength = qBound(0., strength, 1.);
        mRecs << rec;
    }

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override {
        if(mRecs.isEmpty()) return;
        if(!mLoggedOnce) {
            mLoggedOnce = true;
            qWarning() << "[BONE-WARP] processing" << mRecs.count()
                       << "bone(s), tile"
                       << data.fTexTile.width() << "x"
                       << data.fTexTile.height();
        }
        // dst starts as uninitialized memory - copy the src tile first
        {
            SkBitmap srcTile;
            if(renderTools.fSrcBtmp.extractSubset(&srcTile, data.fTexTile)) {
                renderTools.fDstBtmp.writePixels(
                            SkPixmap(srcTile.info(),
                                     srcTile.getAddr32(0, 0),
                                     srcTile.rowBytes()), 0, 0);
            } else {
                renderTools.fDstBtmp.eraseColor(SK_ColorTRANSPARENT);
            }
        }
        bool anyStrength = false;
        for(const auto& rec : mRecs) {
            if(rec.strength > 0.) { anyStrength = true; break; }
        }
        if(!anyStrength) return;

        SkPixmap dst;
        if(!renderTools.fDstBtmp.peekPixels(&dst)) return;
        SkPixmap src;
        if(!renderTools.fSrcBtmp.peekPixels(&src)) return;

        const int w = dst.width();
        const int h = dst.height();
        if(mRecs.isEmpty()) return;

        // per-destination-pixel: weights from the distance to the bones'
        // CURRENT segments, blended bind-mapping, bilinear source sample
        for(int y = 0; y < h; y++) {
            for(int x = 0; x < w; x++) {
                const QPointF p(data.fPos.x() + data.fTexTile.x() + x,
                                data.fPos.y() + data.fTexTile.y() + y);
                QMatrix bestM;
                qreal bestW = -1.;
                qreal bestStrength = 1.;
                // dominant bone by per-bone radius falloff
                for(const auto& rec : mRecs) {
                    if(rec.strength <= 0.) continue;
                    const QPointF d = rec.tailCur - rec.headCur;
                    const qreal denom = d.x()*d.x() + d.y()*d.y();
                    const qreal u = denom > 0 ?
                                qBound(0., ((p.x()-rec.headCur.x())*d.x() +
                                            (p.y()-rec.headCur.y())*d.y()) /
                                      denom, 1.) : 0.;
                    const QPointF proj(rec.headCur.x() + u*d.x(),
                                       rec.headCur.y() + u*d.y());
                    const qreal dist2 =
                            (p.x()-proj.x())*(p.x()-proj.x()) +
                            (p.y()-proj.y())*(p.y()-proj.y());
                    const qreal r2 = rec.radius*rec.radius;
                    if(dist2 > r2) continue;
                    const qreal wi = (1. - dist2/r2)*rec.strength;
                    if(wi > bestW) { bestW = wi; bestM = rec.toBind;
                                    bestStrength = rec.strength; }
                }
                if(bestW <= 0.) continue; // untouched by any bone
                // single-dominant-bone mapping blended with identity
                const QPointF q = bestM.map(p);
                QPointF sample = p + (q - p)*bestStrength;
                // a singular/degenerate bind matrix yields NaN sample
                // coordinates; rounding those produces a garbage index
                // into the source pixels (segfault) - bail to
                // transparent instead
                if(!qIsFinite(sample.x()) || !qIsFinite(sample.y())) {
                    *dst.writable_addr32(x, y) = 0;
                    continue;
                }
                // source-space pixel coordinate
                const qreal sx = sample.x() - data.fPos.x() - data.fTexTile.x();
                const qreal sy = sample.y() - data.fPos.y() - data.fTexTile.y();
                if(sx < 0 || sy < 0 || sx >= src.width() || sy >= src.height()) {
                    *dst.writable_addr32(x, y) = 0;
                    continue;
                }
                // clamped bilinear sampling: nearest-neighbor made the
                // bend look broken/pixelated, and rounding at the edge
                // could read one pixel out of bounds
                {
                    const int x0 = qRound(qBound(0., sx, src.width() - 1.));
                    const int y0 = qRound(qBound(0., sy, src.height() - 1.));
                    const int x1 = qMin(x0 + 1, src.width() - 1);
                    const int y1 = qMin(y0 + 1, src.height() - 1);
                    const qreal fx = qBound(0., sx - x0, 1.);
                    const qreal fy = qBound(0., sy - y0, 1.);
                    const auto c00 = src.addr32(x0, y0);
                    const auto c10 = src.addr32(x1, y0);
                    const auto c01 = src.addr32(x0, y1);
                    const auto c11 = src.addr32(x1, y1);
                    uint out[4];
                    for(int ch = 0; ch < 4; ch++) {
                        const qreal a0 = c00[ch] + (c10[ch] - c00[ch])*fx;
                        const qreal a1 = c01[ch] + (c11[ch] - c01[ch])*fx;
                        out[ch] = qRound(a0 + (a1 - a0)*fy);
                    }
                    *dst.writable_addr32(x, y) =
                            (out[0]) | (out[1] << 8) | (out[2] << 16) | (out[3] << 24);
                }
            }
        }
    }
private:
    QList<BoneWarpRec> mRecs;
    bool mLoggedOnce = false;
};

// ---------------------------------------------------------------------------
// effect

BoneWarpEffect::BoneWarpEffect() :
    RasterEffect(QStringLiteral("\u9AA8\u9ABC\u5F2F\u66F2"),
                 HardwareSupport::cpuOnly, false,
                 RasterEffectType::BONE_WARP) {
    mBonesRoot = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("\u9AA8\u9ABC\u94FE"));
    mBonesRoot->setValidator<Bone>();

    ca_addChild(mBonesRoot);

    connect(mBonesRoot.data(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox*) {
        mBindDirty = true;
        // defer: the effect may still be mid-attachment when the
        // target is set (bind auto-attach) - never rebind re-entrantly
        SimpleTask::sScheduleContexted(this, [this]() {
            if(mBindDirty) rebind();
        });
    });
}

void BoneWarpEffect::setChainRoot(Bone* const root) {
    mBonesRoot->setTarget(root);
}

QList<BoneWarpEffect::BindRec> BoneWarpEffect::collectBindRecs() const {
    QList<BindRec> recs;
    const auto root = enve_cast<Bone*>(mBonesRoot->getTarget());
    if(!root) return recs;
    // walk the chain from the root bone down
    QList<Bone*> stack{root};
    while(!stack.isEmpty()) {
        const auto bone = stack.takeLast();
        if(!bone) continue;
        recs.append({bone, bone->getTotalTransform()});
        for(const auto& c : bone->getContained()) {
            if(const auto child = enve_cast<Bone*>(c.data()))
                stack.append(child);
        }
    }
    return recs;
}

void BoneWarpEffect::clearFollowConns() {
    mFollowConn.clear();
}

void BoneWarpEffect::rebind() {
    clearFollowConns();
    mBind = collectBindRecs();
    mBindDirty = false;
    // live follow: any bone change re-renders the host layer
    for(const auto& rec : mBind) {
        mFollowConn << connect(rec.bone, &BoundingBox::prp_absFrameRangeChanged,
                               this, [this](const FrameRange& abs) {
            // the host layer lives INSIDE the followed bone chain:
            // reacting dirties the host, which propagates back up as
            // a bone range change - without the guard the signal chain
            // loops forever (stack overflow)
            if(mInFollow) return;
            // coalesce: many bone signals (and render-finish feedback)
            // collapse into ONE invalidation per event-loop pass, so
            // the render-free-render memory churn loop cannot sustain
            // itself while idle
            SimpleTask::sScheduleContexted(this, [this, abs]() {
                if(mInFollow) return;
                mInFollow = true;
                prp_afterChangedAbsRange(abs);
                mInFollow = false;
            });
        });
    }
    emit rebound();
}

stdsptr<RasterEffectCaller> BoneWarpEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution) Q_UNUSED(data)
    if(mBindDirty) const_cast<BoneWarpEffect*>(this)->rebind();
    if(mBind.isEmpty()) {
        qWarning() << "[BONE-WARP] no bones bound (target unset or "
                      "rebind never ran) - effect is a no-op";
        return nullptr;
    }
    qreal maxRadius = 10.;
    bool anyStrength = false;
    bool anyMoved = false;
    for(const auto& rec : mBind) {
        if(!rec.bone) continue;
        maxRadius = qMax(maxRadius, rec.bone->warpRadius());
        if(rec.bone->warpStrength()*influence > 0.) anyStrength = true;
        // identity check: at the bind pose the warp is mathematically
        // a no-op - skipping it keeps the render pipeline idle (open-
        // project black screen + playback crash: the expensive per-
        // pixel CPU pass ran on EVERY frame even with bones at rest)
        const auto cur = rec.bone->getTotalTransform();
        const QMatrix toBind = rec.bindTotal*cur.inverted();
        if(qAbs(toBind.m11() - 1.) > 0.001 ||
           qAbs(toBind.m22() - 1.) > 0.001 ||
           qAbs(toBind.dx()) > 0.5 ||
           qAbs(toBind.dy()) > 0.5) {
            anyMoved = true;
        }
    }
    if(!anyStrength || !anyMoved) return nullptr;
    const auto caller = enve::make_shared<BoneWarpCaller>(maxRadius);
    for(const auto& rec : mBind) {
        if(!rec.bone) continue;
        const auto cur = rec.bone->getTotalTransform();
        caller->addBone(rec.bindTotal, cur,
                        cur.map(QPointF(0, 0)),
                        cur.map(QPointF(rec.bone->getLength(), 0)),
                        rec.bone->warpRadius(),
                        rec.bone->warpStrength()*influence);
    }
    return caller;
}

void BoneWarpEffect::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    menu->addPlainAction<BoneWarpEffect>(
                QIcon::fromTheme("group"),
                QStringLiteral("\u91CD\u65B0\u7ED1\u5B9A\u59FF\u52BF"),
                [](BoneWarpEffect* const effect) {
        effect->rebind();
        if(Document::sInstance) Document::sInstance->actionFinished();
    });
    RasterEffect::prp_setupTreeViewMenu(menu);
}
