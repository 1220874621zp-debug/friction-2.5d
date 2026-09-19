#include "particleeffect.h"

#include "gpurendertools.h"
#include "rastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "Animators/boolanimator.h"
#include "Animators/staticcomplexanimator.h"
#include "Properties/comboboxproperty.h"

#include "appsupport.h"
#include "skia/skqtconversions.h"

#include <QtMath>
#include <QMargins>
#include <vector>

namespace {

// deterministic hash-based random: same (seed, particle id, channel)
// always yields the same value, on any thread, for any render order
uint pcgHash(uint v) {
    v = v * 747796405u + 2891336453u;
    const uint w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (w >> 22u) ^ w;
}

qreal frand(const uint seed, const uint id, const uint channel) {
    const uint h = pcgHash(seed * 374761393u + id * 668265263u
                           + channel * 2246822519u + 0x9E3779B9u);
    return qreal(h & 0xFFFFFFu) / 16777216.0;
}

// (1 - e^(-k*tau)) / k, stable for k*tau ~ 0
qreal phiDrag(const qreal k, const qreal tau) {
    const qreal kt = k * tau;
    if (qAbs(kt) < 1e-4) return tau * (1.0 - kt * 0.5 + kt * kt / 6.0);
    return (1.0 - std::exp(-kt)) / k;
}

// per-particle data fixed at spawn time; all values are offsets from
// the center of the layer's rendered image, resolution-scaled
struct ParticleSpawn {
    float birth = 0;       // spawn frame (sub-frame jittered)
    float life = 0;        // lifetime in tau units (frames)
    float x0 = 0, y0 = 0;  // spawn position
    float vx0 = 0, vy0 = 0;// initial velocity, px/frame
    float sizeMul = 1;
    float spinSign = 1;
    float rot0 = 0;        // degrees
    float w1 = 1, w2 = 1;  // turbulence frequency multipliers
    float phase = 0;
};

// parameters evaluated once at the render frame
struct ParticleFrameData {
    qreal gravX = 0, gravY = 0;  // constant accel, px/frame^2
    qreal drag = 0;              // per-frame decay
    qreal startSize = 6, endSize = 1;
    qreal startOp = 1, endOp = 0;
    QColor startColor, endColor;
    int shape = 0;               // 0 dot, 1 square, 2 velocity line
    int blend = 0;               // 0 normal, 1 add, 2 screen
    qreal turbAmp = 0, turbFreq = 1;
    qreal spin = 0;              // degrees per frame
    bool hideSource = false;
    qreal relFrame = 0;
    qreal timeScale = 1;
};

} // namespace

class ParticleEffectCaller : public RasterEffectCaller {
public:
    ParticleEffectCaller(const HardwareSupport hwSupport,
                         const QMargins& margin,
                         const ParticleFrameData& f,
                         std::vector<ParticleSpawn>&& spawns) :
        RasterEffectCaller(hwSupport, true, margin),
        mF(f), mSpawns(std::move(spawns)) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override {
        const auto& srcBtmp = renderTools.fSrcBtmp;
        const auto& dstBtmp = renderTools.fDstBtmp;
        if (dstBtmp.empty() || dstBtmp.getPixels() == nullptr) return;
        const auto& tile = data.fTexTile;

        SkCanvas canvas(dstBtmp);
        canvas.clear(SK_ColorTRANSPARENT);
        if (!mF.hideSource && !srcBtmp.empty() && srcBtmp.getPixels()) {
            canvas.drawBitmap(srcBtmp,
                              SkIntToScalar(-tile.left()),
                              SkIntToScalar(-tile.top()));
        }

        // particles live in image space; the canvas origin sits at the
        // tile's top-left, and the emitter origin is the image center
        const qreal ox = qreal(srcBtmp.width()) * 0.5;
        const qreal oy = qreal(srcBtmp.height()) * 0.5;

        const bool hasDrag = mF.drag > 1e-4;

        SkBlendMode blendMode = SkBlendMode::kSrcOver;
        if (mF.blend == 1) blendMode = SkBlendMode::kPlus;
        else if (mF.blend == 2) blendMode = SkBlendMode::kScreen;

        const qreal sr = mF.startColor.redF();
        const qreal sg = mF.startColor.greenF();
        const qreal sb = mF.startColor.blueF();
        const qreal er = mF.endColor.redF();
        const qreal eg = mF.endColor.greenF();
        const qreal eb = mF.endColor.blueF();

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setBlendMode(blendMode);

        for (const auto& s : mSpawns) {
            const qreal tau = (mF.relFrame - qreal(s.birth)) * mF.timeScale;
            if (tau < 0.0 || tau >= s.life) continue;
            const qreal t = qBound(0.0, tau / s.life, 1.0);

            // closed-form trajectory with linear drag:
            //   p(tau) = p0 + v0*phi1 + a*(tau - phi1)/k
            //   v(tau) = v0*e^(-k*tau) + a*phi1
            const qreal f1 = phiDrag(hasDrag ? mF.drag : 0.0, tau);
            const qreal f2 = hasDrag ? (tau - f1) / mF.drag
                                     : 0.5 * tau * tau;
            const qreal E = hasDrag ? std::exp(-mF.drag * tau) : 1.0;

            qreal px = s.x0 + s.vx0 * f1 + mF.gravX * f2;
            qreal py = s.y0 + s.vy0 * f1 + mF.gravY * f2;
            const qreal vx = s.vx0 * E + mF.gravX * f1;
            const qreal vy = s.vy0 * E + mF.gravY * f1;
            if (mF.turbAmp > 0.0) {
                px += mF.turbAmp * std::sin(tau * mF.turbFreq * s.w1 + s.phase);
                py += mF.turbAmp * std::cos(tau * mF.turbFreq * s.w2 + s.phase * 1.7);
            }

            const qreal size = qMax(0.0, (mF.startSize + (mF.endSize - mF.startSize) * t)
                                          * s.sizeMul);
            if (size < 0.05) continue;

            const qreal op = mF.startOp + (mF.endOp - mF.startOp) * t;
            if (op <= 0.001) continue;

            const qreal gx = ox + px;
            const qreal gy = oy + py;

            // cull against this tile; pad covers rotated squares and
            // velocity-line tails (up to ~6x the particle size)
            const int pad = int(size * 8.0) + 4;
            if (gx < tile.left() - pad || gx > tile.right() + pad ||
                gy < tile.top() - pad || gy > tile.bottom() + pad) continue;

            const qreal cr = sr + (er - sr) * t;
            const qreal cg = sg + (eg - sg) * t;
            const qreal cb = sb + (eb - sb) * t;
            paint.setColor(SkColorSetARGB(
                    uchar(qBound(0.0, op, 1.0) * 255.0),
                    uchar(cr * 255.0), uchar(cg * 255.0), uchar(cb * 255.0)));

            const SkScalar lx = toSkScalar(gx - tile.left());
            const SkScalar ly = toSkScalar(gy - tile.top());

            if (mF.shape == 2) {
                // velocity line: stretched along current velocity
                const qreal vmag = std::hypot(vx, vy);
                if (vmag < 0.01) {
                    paint.setStyle(SkPaint::kFill_Style);
                    canvas.drawCircle(lx, ly, toSkScalar(size * 0.5), paint);
                } else {
                    const qreal len = qBound(size * 0.5, vmag * 0.6, size * 6.0);
                    paint.setStyle(SkPaint::kStroke_Style);
                    paint.setStrokeCap(SkPaint::kRound_Cap);
                    paint.setStrokeWidth(toSkScalar(qMax(1.0, size * 0.35)));
                    canvas.drawLine(lx - toSkScalar(vx / vmag * len),
                                    ly - toSkScalar(vy / vmag * len),
                                    lx, ly, paint);
                }
            } else if (mF.shape == 1) {
                paint.setStyle(SkPaint::kFill_Style);
                const SkScalar hs = toSkScalar(size * 0.5);
                canvas.save();
                canvas.translate(lx, ly);
                canvas.rotate(toSkScalar(s.rot0 + mF.spin * tau * s.spinSign));
                canvas.drawRect(SkRect::MakeLTRB(-hs, -hs, hs, hs), paint);
                canvas.restore();
            } else {
                paint.setStyle(SkPaint::kFill_Style);
                canvas.drawCircle(lx, ly, toSkScalar(size * 0.5), paint);
            }
        }
    }
private:
    const ParticleFrameData mF;
    const std::vector<ParticleSpawn> mSpawns;
};

ParticleEffect::ParticleEffect() :
    RasterEffect(QObject::tr("粒子"),
                 AppSupport::getRasterEffectHardwareSupport("Particle",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::PARTICLE)
{
    const auto emitterGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("发射器"));
    mEmitterType = enve::make_shared<ComboBoxProperty>(
                QObject::tr("类型"), QStringList()
                << QObject::tr("点") << QObject::tr("线段")
                << QObject::tr("矩形边框") << QObject::tr("矩形内部")
                << QObject::tr("椭圆环") << QObject::tr("椭圆内部"));
    emitterGroup->ca_addChild(mEmitterType);
    mEmitterX = enve::make_shared<QrealAnimator>(0.0, -9999.0, 9999.0, 1.0,
                                                 QObject::tr("位置 X"));
    emitterGroup->ca_addChild(mEmitterX);
    mEmitterY = enve::make_shared<QrealAnimator>(0.0, -9999.0, 9999.0, 1.0,
                                                 QObject::tr("位置 Y"));
    emitterGroup->ca_addChild(mEmitterY);
    mEmitterW = enve::make_shared<QrealAnimator>(100.0, 0.0, 9999.0, 1.0,
                                                 QObject::tr("宽度"));
    emitterGroup->ca_addChild(mEmitterW);
    mEmitterH = enve::make_shared<QrealAnimator>(100.0, 0.0, 9999.0, 1.0,
                                                 QObject::tr("高度"));
    emitterGroup->ca_addChild(mEmitterH);
    mRate = enve::make_shared<QrealAnimator>(2.0, 0.0, 100.0, 0.1,
                                             QObject::tr("每帧数量"));
    emitterGroup->ca_addChild(mRate);
    mBurst = enve::make_shared<QrealAnimator>(0.0, 0.0, 2000.0, 1.0,
                                              QObject::tr("爆发数量"));
    emitterGroup->ca_addChild(mBurst);
    mStartFrame = enve::make_shared<QrealAnimator>(0.0, -9999.0, 9999.0, 1.0,
                                                   QObject::tr("发射开始帧"));
    emitterGroup->ca_addChild(mStartFrame);
    ca_addChild(emitterGroup);

    const auto shapeGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("粒子"));
    mShape = enve::make_shared<ComboBoxProperty>(
                QObject::tr("形状"), QStringList()
                << QObject::tr("圆点") << QObject::tr("方块")
                << QObject::tr("速度线"));
    shapeGroup->ca_addChild(mShape);
    mStartSize = enve::make_shared<QrealAnimator>(6.0, 0.0, 1000.0, 0.1,
                                                  QObject::tr("起始大小"));
    shapeGroup->ca_addChild(mStartSize);
    mEndSize = enve::make_shared<QrealAnimator>(1.0, 0.0, 1000.0, 0.1,
                                                QObject::tr("结束大小"));
    shapeGroup->ca_addChild(mEndSize);
    mSizeVar = enve::make_shared<QrealAnimator>(20.0, 0.0, 100.0, 1.0,
                                                QObject::tr("大小变化"));
    shapeGroup->ca_addChild(mSizeVar);
    mStartColor = enve::make_shared<ColorAnimator>(QObject::tr("起始颜色"));
    mStartColor->setColor(QColor(255, 245, 206, 255));
    shapeGroup->ca_addChild(mStartColor);
    mEndColor = enve::make_shared<ColorAnimator>(QObject::tr("结束颜色"));
    mEndColor->setColor(QColor(255, 90, 0, 255));
    shapeGroup->ca_addChild(mEndColor);
    mStartOpacity = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0,
                                                     QObject::tr("起始不透明度"));
    shapeGroup->ca_addChild(mStartOpacity);
    mEndOpacity = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0,
                                                   QObject::tr("结束不透明度"));
    shapeGroup->ca_addChild(mEndOpacity);
    mBlendMode = enve::make_shared<ComboBoxProperty>(
                QObject::tr("混合模式"), QStringList()
                << QObject::tr("正常") << QObject::tr("加色")
                << QObject::tr("滤色"));
    shapeGroup->ca_addChild(mBlendMode);
    mSpin = enve::make_shared<QrealAnimator>(0.0, -720.0, 720.0, 1.0,
                                             QObject::tr("自转"));
    shapeGroup->ca_addChild(mSpin);
    ca_addChild(shapeGroup);

    const auto physicsGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("物理"));
    mSpeed = enve::make_shared<QrealAnimator>(6.0, 0.0, 100.0, 0.1,
                                              QObject::tr("初速度"));
    physicsGroup->ca_addChild(mSpeed);
    mSpeedVar = enve::make_shared<QrealAnimator>(20.0, 0.0, 100.0, 1.0,
                                                 QObject::tr("速度变化"));
    physicsGroup->ca_addChild(mSpeedVar);
    mDir = enve::make_shared<QrealAnimator>(-90.0, -360.0, 360.0, 1.0,
                                            QObject::tr("方向"));
    physicsGroup->ca_addChild(mDir);
    mSpread = enve::make_shared<QrealAnimator>(25.0, 0.0, 180.0, 1.0,
                                               QObject::tr("扩散角"));
    physicsGroup->ca_addChild(mSpread);
    mGravity = enve::make_shared<QrealAnimator>(0.15, -50.0, 50.0, 0.01,
                                                QObject::tr("重力"));
    physicsGroup->ca_addChild(mGravity);
    mDrag = enve::make_shared<QrealAnimator>(0.02, 0.0, 1.0, 0.01,
                                             QObject::tr("阻力"));
    physicsGroup->ca_addChild(mDrag);
    mWindX = enve::make_shared<QrealAnimator>(0.0, -50.0, 50.0, 0.01,
                                              QObject::tr("风力 X"));
    physicsGroup->ca_addChild(mWindX);
    mWindY = enve::make_shared<QrealAnimator>(0.0, -50.0, 50.0, 0.01,
                                              QObject::tr("风力 Y"));
    physicsGroup->ca_addChild(mWindY);
    mTurbAmp = enve::make_shared<QrealAnimator>(0.0, 0.0, 500.0, 1.0,
                                                QObject::tr("湍流强度"));
    physicsGroup->ca_addChild(mTurbAmp);
    mTurbFreq = enve::make_shared<QrealAnimator>(1.0, 0.0, 10.0, 0.05,
                                                 QObject::tr("湍流频率"));
    physicsGroup->ca_addChild(mTurbFreq);
    ca_addChild(physicsGroup);

    const auto globalGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("全局"));
    mLife = enve::make_shared<QrealAnimator>(30.0, 1.0, 999.0, 1.0,
                                             QObject::tr("寿命（帧）"));
    globalGroup->ca_addChild(mLife);
    mLifeVar = enve::make_shared<QrealAnimator>(20.0, 0.0, 100.0, 1.0,
                                                QObject::tr("寿命变化"));
    globalGroup->ca_addChild(mLifeVar);
    mSeed = enve::make_shared<QrealAnimator>(1.0, 0.0, 9999.0, 1.0,
                                             QObject::tr("随机种子"));
    globalGroup->ca_addChild(mSeed);
    mTimeScale = enve::make_shared<QrealAnimator>(1.0, 0.01, 10.0, 0.05,
                                                  QObject::tr("时间缩放"));
    globalGroup->ca_addChild(mTimeScale);
    mHideSource = enve::make_shared<BoolAnimator>(QObject::tr("隐藏源图内容"));
    mHideSource->setCurrentBoolValue(false);
    globalGroup->ca_addChild(mHideSource);
    ca_addChild(globalGroup);

    // margin-affecting parameters must retrigger the forced-margin
    // machinery so the allocation bounds follow (blur pattern)
    const auto wireMargin = [this](QrealAnimator * const a) {
        connect(a, &QrealAnimator::effectiveValueChanged,
                this, &RasterEffect::forcedMarginChanged);
    };
    wireMargin(mEmitterX.get());
    wireMargin(mEmitterY.get());
    wireMargin(mEmitterW.get());
    wireMargin(mEmitterH.get());
    wireMargin(mSpeed.get());
    wireMargin(mSpeedVar.get());
    wireMargin(mLife.get());
    wireMargin(mLifeVar.get());
    wireMargin(mGravity.get());
    wireMargin(mDrag.get());
    wireMargin(mWindX.get());
    wireMargin(mWindY.get());
    wireMargin(mTurbAmp.get());
    wireMargin(mStartSize.get());
    wireMargin(mEndSize.get());
    wireMargin(mSizeVar.get());
    wireMargin(mTimeScale.get());
}

stdsptr<RasterEffectCaller> ParticleEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(data)

    ParticleFrameData f;
    f.relFrame = relFrame;
    f.timeScale = qMax(0.01, mTimeScale->getEffectiveValue(relFrame));
    f.hideSource = mHideSource->getBoolValue(relFrame);

    f.startSize = mStartSize->getEffectiveValue(relFrame) * resolution;
    f.endSize = mEndSize->getEffectiveValue(relFrame) * resolution;
    const qreal sizeVar = mSizeVar->getEffectiveValue(relFrame) / 100.0;
    f.startOp = mStartOpacity->getEffectiveValue(relFrame) / 100.0 * influence;
    f.endOp = mEndOpacity->getEffectiveValue(relFrame) / 100.0 * influence;
    f.startColor = mStartColor->getColor(relFrame);
    f.endColor = mEndColor->getColor(relFrame);
    f.shape = mShape->getCurrentValue();
    f.blend = mBlendMode->getCurrentValue();
    f.spin = mSpin->getEffectiveValue(relFrame);

    f.gravX = mWindX->getEffectiveValue(relFrame) * resolution;
    f.gravY = (mWindY->getEffectiveValue(relFrame)
               + mGravity->getEffectiveValue(relFrame)) * resolution;
    f.drag = qMax(0.0, mDrag->getEffectiveValue(relFrame));
    f.turbAmp = mTurbAmp->getEffectiveValue(relFrame) * resolution;
    f.turbFreq = qMax(0.0, mTurbFreq->getEffectiveValue(relFrame));

    const uint seed = uint(qRound(mSeed->getEffectiveValue(relFrame)));
    const int emitterType = mEmitterType->getCurrentValue();
    const qreal life = mLife->getEffectiveValue(relFrame);
    const qreal lifeVar = mLifeVar->getEffectiveValue(relFrame) / 100.0;
    const qreal lifeMax = life * (1.0 + lifeVar);
    const qreal speedVar = mSpeedVar->getEffectiveValue(relFrame) / 100.0;
    const int startF = qRound(mStartFrame->getEffectiveValue(relFrame));
    const int burst = qRound(mBurst->getEffectiveValue(relFrame));

    // only frames that can still hold living particles are walked;
    // the fractional-rate accumulator keeps emission deterministic
    const int minBirth = qMax(startF, qCeil(relFrame - lifeMax / f.timeScale));
    const int maxBirth = qFloor(relFrame);

    std::vector<ParticleSpawn> spawns;
    const int hardCap = 20000;
    qreal carry = 0.0;
    for (int fr = minBirth; fr <= maxBirth; fr++) {
        // emitter parameters are sampled at the spawn frame so a
        // keyframed emitter leaves a trail of particles behind it
        const qreal ex = mEmitterX->getEffectiveValue(fr) * resolution;
        const qreal ey = mEmitterY->getEffectiveValue(fr) * resolution;
        const qreal ew = qMax(0.0, mEmitterW->getEffectiveValue(fr) * resolution);
        const qreal eh = qMax(0.0, mEmitterH->getEffectiveValue(fr) * resolution);
        const qreal dirRad = qDegreesToRadians(mDir->getEffectiveValue(fr));
        const qreal spreadRad = qDegreesToRadians(
                    qMax(0.0, mSpread->getEffectiveValue(fr)));
        const qreal speed = qMax(0.0, mSpeed->getEffectiveValue(fr) * resolution);

        const qreal rateF = qMax(0.0, mRate->getEffectiveValue(fr));
        const int n = int(rateF + carry);
        carry = rateF + carry - n;
        const int nBurst = (fr == startF) ? burst : 0;

        const auto addSpawn = [&](const uint id, const qreal birth) {
            ParticleSpawn s;
            s.birth = float(birth);
            s.life = float(qMax(1.0, life * (1.0 + (frand(seed, id, 5) * 2.0 - 1.0) * lifeVar)));
            const qreal u = frand(seed, id, 1);
            const qreal v = frand(seed, id, 2);
            qreal dx = 0.0, dy = 0.0;
            switch (emitterType) {
                case 1: // 线段
                    dx = (u - 0.5) * ew;
                    break;
                case 2: { // 矩形边框
                    const qreal per = 2.0 * (ew + eh);
                    const qreal p = u * per;
                    if (p < ew) { dx = p - ew * 0.5; dy = -eh * 0.5; }
                    else if (p < ew + eh) { dx = ew * 0.5; dy = p - ew - eh * 0.5; }
                    else if (p < 2.0 * ew + eh) { dx = p - ew - eh - ew * 0.5; dy = eh * 0.5; }
                    else { dx = -ew * 0.5; dy = p - 2.0 * ew - eh - eh * 0.5; }
                } break;
                case 3: // 矩形内部
                    dx = (u - 0.5) * ew;
                    dy = (v - 0.5) * eh;
                    break;
                case 4: { // 椭圆环
                    const qreal a = u * 2.0 * M_PI;
                    dx = std::cos(a) * ew * 0.5;
                    dy = std::sin(a) * eh * 0.5;
                } break;
                case 5: { // 椭圆内部
                    const qreal a = u * 2.0 * M_PI;
                    const qreal r = std::sqrt(v);
                    dx = std::cos(a) * ew * 0.5 * r;
                    dy = std::sin(a) * eh * 0.5 * r;
                } break;
                default: // 点
                    break;
            }
            s.x0 = float(ex + dx);
            s.y0 = float(ey + dy);
            const qreal ang = dirRad + (frand(seed, id, 3) * 2.0 - 1.0) * spreadRad;
            const qreal spd = speed * (1.0 + (frand(seed, id, 4) * 2.0 - 1.0) * speedVar);
            s.vx0 = float(std::cos(ang) * spd);
            s.vy0 = float(std::sin(ang) * spd);
            s.sizeMul = float(1.0 + (frand(seed, id, 6) * 2.0 - 1.0) * sizeVar);
            s.spinSign = frand(seed, id, 7) < 0.5 ? -1.0f : 1.0f;
            s.rot0 = float(frand(seed, id, 8) * 360.0);
            s.w1 = float(0.5 + frand(seed, id, 9));
            s.w2 = float(0.5 + frand(seed, id, 10));
            s.phase = float(frand(seed, id, 11) * 2.0 * M_PI);
            spawns.push_back(s);
        };

        for (int j = 0; j < n && int(spawns.size()) < hardCap; j++) {
            const uint id = uint(fr - startF) * 4096u + uint(j);
            addSpawn(id, qreal(fr) + frand(seed, id, 0));
        }
        for (int j = 0; j < nBurst && int(spawns.size()) < hardCap; j++) {
            // burst particles spawn exactly on the emission start frame
            const uint id = uint(fr - startF) * 4096u + 2048u + uint(j);
            addSpawn(id, qreal(fr));
        }
    }

    if (spawns.empty() && !f.hideSource) return nullptr;

    // worst-case reach from the layer-image center -> uniform margin
    const qreal vMax = qMax(0.0, mSpeed->getEffectiveValue(relFrame))
                       * (1.0 + speedVar) * resolution;
    const qreal aMag = std::hypot(f.gravX, f.gravY);
    const qreal f1Max = phiDrag(f.drag, lifeMax);
    const qreal f2Max = f.drag > 1e-4 ? (lifeMax - f1Max) / f.drag
                                      : 0.5 * lifeMax * lifeMax;
    const qreal marginF = vMax * f1Max + aMag * f2Max + f.turbAmp
            + qAbs(mEmitterX->getEffectiveValue(relFrame)) * resolution
            + qAbs(mEmitterY->getEffectiveValue(relFrame)) * resolution
            + 0.75 * (mEmitterW->getEffectiveValue(relFrame)
                      + mEmitterH->getEffectiveValue(relFrame)) * resolution
            + qMax(f.startSize, f.endSize) * (1.0 + sizeVar) * 8.0 + 8.0;
    const int margin = qBound(0, qCeil(marginF), 9999);

    return enve::make_shared<ParticleEffectCaller>(
                instanceHwSupport(), QMargins() + margin,
                f, std::move(spawns));
}

QMargins ParticleEffect::getMargin() const
{
    const qreal lifeMax = mLife->getEffectiveValue()
            * (1.0 + mLifeVar->getEffectiveValue() / 100.0);
    const qreal vMax = mSpeed->getEffectiveValue()
            * (1.0 + mSpeedVar->getEffectiveValue() / 100.0);
    const qreal aMag = std::hypot(mWindX->getEffectiveValue(),
                                  mWindY->getEffectiveValue()
                                  + mGravity->getEffectiveValue());
    const qreal drag = qMax(0.0, mDrag->getEffectiveValue());
    const qreal f1Max = phiDrag(drag, lifeMax);
    const qreal f2Max = drag > 1e-4 ? (lifeMax - f1Max) / drag
                                    : 0.5 * lifeMax * lifeMax;
    const qreal marginF = vMax * f1Max + aMag * f2Max
            + mTurbAmp->getEffectiveValue()
            + qAbs(mEmitterX->getEffectiveValue())
            + qAbs(mEmitterY->getEffectiveValue())
            + 0.75 * (mEmitterW->getEffectiveValue()
                      + mEmitterH->getEffectiveValue())
            + qMax(mStartSize->getEffectiveValue(),
                   mEndSize->getEffectiveValue())
              * (1.0 + mSizeVar->getEffectiveValue() / 100.0) * 8.0 + 8.0;
    return QMargins() + qBound(0, qCeil(marginF), 9999);
}
