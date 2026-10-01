/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef GRIDEFFECT_H
#define GRIDEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class QPointFAnimator;
class ColorAnimator;

// AE Grid generator: colored grid lines over the whole layer,
// anchored at a draggable point (0..1 UV over the host box content
// rect), with cell size, line width, invert and blend-back controls.
class CORE_EXPORT GridEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    GridEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

private:
    qsptr<QPointFAnimator> mAnchor;
    qsptr<QrealAnimator> mSizeW;
    qsptr<QrealAnimator> mSizeH;
    qsptr<QrealAnimator> mBorder;
    qsptr<ColorAnimator> mColor;
    qsptr<QrealAnimator> mInvert;
    qsptr<QrealAnimator> mMix; // blend with original 0..100
};

#endif // GRIDEFFECT_H
