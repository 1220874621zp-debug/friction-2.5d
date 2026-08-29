/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef PIXELARTEFFECT_H
#define PIXELARTEFFECT_H

#include "rastereffect.h"

class QrealAnimator;

class CORE_EXPORT PixelArtEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    PixelArtEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;
private:
    qsptr<QrealAnimator> mPixelSize;
    qsptr<QrealAnimator> mPaletteSize;
    qsptr<QrealAnimator> mDither;
    qsptr<QrealAnimator> mEdgeSharpness;
    qsptr<QrealAnimator> mSaturation;
    qsptr<QrealAnimator> mScanline;
    qsptr<QrealAnimator> mChromatic;
};

#endif // PIXELARTEFFECT_H
