/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef LIQUIDGLASSEFFECT_H
#define LIQUIDGLASSEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class ColorAnimator;

class CORE_EXPORT LiquidGlassEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    LiquidGlassEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;
private:
    qsptr<QrealAnimator> mBlurRadius;
    qsptr<QrealAnimator> mRefraction;
    qsptr<QrealAnimator> mSurfaceNoise;
    qsptr<QrealAnimator> mThickness;
    qsptr<QrealAnimator> mHighlightIntensity;
    qsptr<QrealAnimator> mLightAngle;
    qsptr<QrealAnimator> mHighlightSize;
    qsptr<QrealAnimator> mEdgeSoftness;
    qsptr<QrealAnimator> mMagnification;
    qsptr<ColorAnimator> mGlassTint;
    qsptr<QrealAnimator> mTintOpacity;
};

#endif // LIQUIDGLASSEFFECT_H
