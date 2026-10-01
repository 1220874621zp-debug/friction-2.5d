/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef RAMPEFFECT_H
#define RAMPEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class QPointFAnimator;
class ColorAnimator;
class ComboBoxProperty;

// AE Gradient Ramp generator: linear or radial two-color gradient
// spanning draggable start/end points (0..1 UV over the host box
// content rect), composited "over" the source and blended back with
// the original like AE's Blend With Original.
class CORE_EXPORT RampEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    RampEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

private:
    qsptr<QPointFAnimator> mStartPoint;
    qsptr<ColorAnimator> mStartColor;
    qsptr<QPointFAnimator> mEndPoint;
    qsptr<ColorAnimator> mEndColor;
    qsptr<ComboBoxProperty> mShape; // 0 = linear, 1 = radial
    qsptr<QrealAnimator> mMix;      // blend with original 0..100
};

#endif // RAMPEFFECT_H
