/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef DISPLACEMENTWARPEFFECT_H
#define DISPLACEMENTWARPEFFECT_H

#include "rastereffect.h"

class QrealAnimator;

class CORE_EXPORT DisplacementWarpEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    DisplacementWarpEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

private:
    qsptr<QrealAnimator> mAmount;
    qsptr<QrealAnimator> mFrequency;
    qsptr<QrealAnimator> mSpeed;
    qsptr<QrealAnimator> mDispType; // 0=Water Wave, 1=Turbulence/Heat, 2=Glass Refraction
    qsptr<QrealAnimator> mChromatic;
};

#endif // DISPLACEMENTWARPEFFECT_H
