/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef LIGHTSWEEPEFFECT_H
#define LIGHTSWEEPEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class ColorAnimator;

class CORE_EXPORT LightSweepEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    LightSweepEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

private:
    qsptr<QrealAnimator> mCenter;
    qsptr<QrealAnimator> mAngle;
    qsptr<QrealAnimator> mWidth;
    qsptr<QrealAnimator> mIntensity;
    qsptr<QrealAnimator> mFeather;
    qsptr<ColorAnimator> mColor;
};

#endif // LIGHTSWEEPEFFECT_H
