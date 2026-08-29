/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef FILMGRAINEFFECT_H
#define FILMGRAINEFFECT_H

#include "rastereffect.h"

class QrealAnimator;

class CORE_EXPORT FilmGrainEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    FilmGrainEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

private:
    qsptr<QrealAnimator> mAmount;
    qsptr<QrealAnimator> mSize;
    qsptr<QrealAnimator> mSpeed;
    qsptr<QrealAnimator> mColorGrain;
};

#endif // FILMGRAINEFFECT_H
