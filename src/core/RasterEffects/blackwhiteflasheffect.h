/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef BLACKWHITEFLASHEFFECT_H
#define BLACKWHITEFLASHEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class ColorAnimator;

class CORE_EXPORT BlackWhiteFlashEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    BlackWhiteFlashEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;
private:
    qsptr<QrealAnimator> mThreshold;
    qsptr<QrealAnimator> mContrast;
    qsptr<QrealAnimator> mLightIntensity;
    qsptr<QrealAnimator> mLightLength;
    qsptr<QrealAnimator> mEdgeIntensity;
    qsptr<QrealAnimator> mInvert;
    qsptr<ColorAnimator> mFlashColor;
    qsptr<ColorAnimator> mBgColor;
};

#endif // BLACKWHITEFLASHEFFECT_H
