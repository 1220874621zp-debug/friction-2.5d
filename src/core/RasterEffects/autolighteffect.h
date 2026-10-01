#ifndef AUTOLIGHTEFFECT_H
#define AUTOLIGHTEFFECT_H

#include "rastereffect.h"
#include "Properties/boxtargetproperty.h"

class QrealAnimator;
class QPointFAnimator;
class ColorAnimator;
class ComboBoxProperty;
class BoolProperty;

// AE-style auto lighting for cel artwork. The lighting field comes
// from the AI depth map (normals from the depth gradient, positional
// light), or from the 三色渐变 volumetric pseudo normal (the cel
// volume effect's shading brain reused as the "normal map"), or from
// the stacked image's own luminance. The field then runs through a
// levels section (threshold + edge hardness: hard or soft shadow
// edge), optional posterize bands, halftone shadows and a rim light.
// See autolightalgo.h for the pipeline.
class CORE_EXPORT AutoLightEffect : public RasterEffect {
public:
    AutoLightEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const;

    FrameRange prp_getIdenticalRelRange(const int relFrame) const;
private:
    // 灯光
    qsptr<ComboBoxProperty> mFieldSrc;
    qsptr<QPointFAnimator> mLightPos;
    qsptr<QrealAnimator> mLightZ;
    qsptr<QrealAnimator> mLightElev;
    qsptr<QrealAnimator> mLightIntensity;
    qsptr<ColorAnimator> mLightColor;
    qsptr<QrealAnimator> mSmooth;
    qsptr<QrealAnimator> mBump;
    qsptr<BoolProperty> mPosterize;
    qsptr<QrealAnimator> mPosterizeLevels;
    // 阴影
    qsptr<QrealAnimator> mThreshold;
    qsptr<QrealAnimator> mHardness;
    qsptr<QrealAnimator> mShadowStrength;
    qsptr<ColorAnimator> mShadowColor;
    qsptr<BoolProperty> mHalftone;
    qsptr<QrealAnimator> mHalftoneScale;
    qsptr<QrealAnimator> mHalftoneVar;
    qsptr<QrealAnimator> mMixOriginal;
    qsptr<BoolProperty> mShadowOnly;
    // 边缘光
    qsptr<BoolProperty> mRimOn;
    qsptr<ColorAnimator> mRimColor;
    qsptr<QrealAnimator> mRimIntensity;
    qsptr<QrealAnimator> mRimWidth;
    qsptr<QrealAnimator> mRimSoftness;
    qsptr<QrealAnimator> mRimOffset;
    qsptr<QrealAnimator> mRimOpacity;
    qsptr<BoolProperty> mRimOnly;
    // 深度图
    qsptr<BoxTargetProperty> mDepthTarget;
    qsptr<BoolProperty> mDepthInvert;
    qsptr<QrealAnimator> mDepthFalloff;
    ConnContextQPtr<BoundingBox> mFollowConn;
};

#endif // AUTOLIGHTEFFECT_H
