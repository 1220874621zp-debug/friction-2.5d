#ifndef PARTICLEEFFECT_H
#define PARTICLEEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class ColorAnimator;
class BoolAnimator;
class ComboBoxProperty;

// analytic (stateless, deterministic) particle emitter effect
// every particle's state is a closed-form function of
// (seed, particle id, birth frame, physics parameters), so any
// frame can be rendered independently - fits friction's
// random-access, multi-threaded per-frame render pipeline
class CORE_EXPORT ParticleEffect : public RasterEffect {
public:
    ParticleEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const override;

    bool forceMargin() const override { return true; }
    QMargins getMargin() const override;
private:
    // 发射器
    qsptr<ComboBoxProperty> mEmitterType;
    qsptr<QrealAnimator> mEmitterX;
    qsptr<QrealAnimator> mEmitterY;
    qsptr<QrealAnimator> mEmitterW;
    qsptr<QrealAnimator> mEmitterH;
    qsptr<QrealAnimator> mRate;
    qsptr<QrealAnimator> mBurst;
    qsptr<QrealAnimator> mStartFrame;
    // 粒子外观
    qsptr<ComboBoxProperty> mShape;
    qsptr<QrealAnimator> mStartSize;
    qsptr<QrealAnimator> mEndSize;
    qsptr<QrealAnimator> mSizeVar;
    qsptr<ColorAnimator> mStartColor;
    qsptr<ColorAnimator> mEndColor;
    qsptr<QrealAnimator> mStartOpacity;
    qsptr<QrealAnimator> mEndOpacity;
    qsptr<ComboBoxProperty> mBlendMode;
    qsptr<QrealAnimator> mSpin;
    // 物理
    qsptr<QrealAnimator> mSpeed;
    qsptr<QrealAnimator> mSpeedVar;
    qsptr<QrealAnimator> mDir;
    qsptr<QrealAnimator> mSpread;
    qsptr<QrealAnimator> mGravity;
    qsptr<QrealAnimator> mDrag;
    qsptr<QrealAnimator> mWindX;
    qsptr<QrealAnimator> mWindY;
    qsptr<QrealAnimator> mTurbAmp;
    qsptr<QrealAnimator> mTurbFreq;
    // 全局
    qsptr<QrealAnimator> mLife;
    qsptr<QrealAnimator> mLifeVar;
    qsptr<QrealAnimator> mSeed;
    qsptr<QrealAnimator> mTimeScale;
    qsptr<BoolAnimator> mHideSource;
};

#endif // PARTICLEEFFECT_H
