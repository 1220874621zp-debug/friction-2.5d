/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef FRACTALNOISEEFFECT_H
#define FRACTALNOISEEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class ColorAnimator;

class CORE_EXPORT FractalNoiseEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    FractalNoiseEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

private:
    qsptr<QrealAnimator> mNoiseType; // 0=Perlin, 1=Turbulence, 2=Voronoi/Cells, 3=Ridged
    qsptr<QrealAnimator> mScale;
    qsptr<QrealAnimator> mComplexity;
    qsptr<QrealAnimator> mEvolution;
    qsptr<QrealAnimator> mBrightness;
    qsptr<QrealAnimator> mContrast;
    qsptr<QrealAnimator> mOpacity;
    qsptr<QrealAnimator> mInvert;
    qsptr<ColorAnimator> mColor1;
    qsptr<ColorAnimator> mColor2;
};

#endif // FRACTALNOISEEFFECT_H
