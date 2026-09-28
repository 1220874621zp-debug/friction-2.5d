#ifndef CELVOLUMEEFFECT_H
#define CELVOLUMEEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class ColorAnimator;
class ComboBoxProperty;

// Segments a flat-cel (anime-style flat fill) image into same-color
// regions, then re-shades each region with a 3-stop hue-journey
// gradient (warm / mid / cool stops, HSV interpolation so the middle
// stays vivid). The gradient coordinate is the projection on a
// directional axis - normalized per region, or over the whole image
// for the "one big lighting wash" look - or, in the volumetric mode,
// the distance-field pseudo-normal lambert term. See
// celvolumealgo.h for the pipeline and the reference look notes.
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
    // 3-stop gradient
    qsptr<ComboBoxProperty> mShadeMode;
    qsptr<ComboBoxProperty> mTMode;
    qsptr<QrealAnimator> mGradAngle;
    qsptr<ColorAnimator> mColWarm;
    qsptr<ColorAnimator> mColMid;
    qsptr<ColorAnimator> mColCool;
    qsptr<QrealAnimator> mBgDarken;
    qsptr<QrealAnimator> mMix;
    // volumetric mode
    qsptr<QrealAnimator> mLightAngle;
    qsptr<QrealAnimator> mLightElev;
    qsptr<QrealAnimator> mBump;
    qsptr<QrealAnimator> mAo;
    qsptr<QrealAnimator> mAoWidth;
    qsptr<QrealAnimator> mSmooth;
};

#endif // CELVOLUMEEFFECT_H
