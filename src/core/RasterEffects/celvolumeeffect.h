#ifndef CELVOLUMEEFFECT_H
#define CELVOLUMEEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class BoolAnimator;

// Segments a flat-cel (anime-style flat fill) image into same-color
// regions, then re-shades each region with a volumetric 4-stop
// gradient (shadow / base / bright / highlight). The region's inner
// distance field acts as a height field whose gradient is a pseudo
// surface normal, so every color block gets a directional lit look.
// See celvolumealgo.h for the pipeline.
class CORE_EXPORT CelVolumeEffect : public RasterEffect {
public:
    CelVolumeEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const override;
private:
    // segmentation
    qsptr<QrealAnimator> mColorTol;
    qsptr<QrealAnimator> mMinArea;
    qsptr<BoolAnimator> mProtectDark;
    qsptr<QrealAnimator> mDarkLuma;
    // volume lighting
    qsptr<QrealAnimator> mLightAngle;
    qsptr<QrealAnimator> mLightElev;
    qsptr<QrealAnimator> mBump;
    qsptr<QrealAnimator> mAo;
    qsptr<QrealAnimator> mAoWidth;
    qsptr<QrealAnimator> mSmooth;
    // 4-stop ramp
    qsptr<QrealAnimator> mHiStrength;
    qsptr<QrealAnimator> mHiWarm;
    qsptr<QrealAnimator> mBrightStrength;
    qsptr<QrealAnimator> mShadeStrength;
    qsptr<QrealAnimator> mShadeHue;
    qsptr<QrealAnimator> mSoftness;
    qsptr<QrealAnimator> mMix;
};

#endif // CELVOLUMEEFFECT_H
