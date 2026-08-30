/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

// Liquid glass backdrop effect, ported from the liquid-glass
// fragment shader of BatchRenderer2D.glsl (Shadertoy adaptation).
//
// The LAYER ITSELF is the glass: whatever the layer draws (vector
// shape or bitmap, at its actual pixel size and position) becomes the
// glass footprint through its alpha. At composite time the backdrop
// below is snapshotted, and inside the footprint the backdrop is
// resampled with the reference shader's radial remap: sample position
// = shape center + (pixel - center) * f_func(edge depth)^refraction.
// The factor is ~0.26 at the rim (strong magnifying edge) and 1.0
// within ~20% depth (flat center), plus an angular rim glow and
// grain. The layer's own pixels are REPLACED by the treated backdrop;
// its alpha only shapes the glass.

#ifndef LIQUIDGLASSEFFECT_H
#define LIQUIDGLASSEFFECT_H

#include "rastereffect.h"
#include "Animators/qrealanimator.h"
#include "Properties/boxtargetproperty.h"

struct LiquidGlassEffectData {
    float mRefraction = 3.f;
    float mNoise = 0.06f;
    float mGlowWeight = 0.35f;
    float mGlowBias = 0.f;
    // crisp rim highlight: narrow band hugging the shape edge, lit on
    // the side facing the light direction (rim normal ~ radial dir)
    float mHighlightI = 0.6f;
    float mHlWidthN = 0.03f;
    float mHlLightX = -0.707f;
    float mHlLightY = 0.707f;
    // picked background layer (AE-style): when set the refraction
    // samples this layer's independently rendered image instead of
    // the live composite below
    bool mUseBgLayer = false;
    stdsptr<BoxRenderData> mBgSample;
};

class LiquidGlassEffect : public RasterEffect {
public:
    LiquidGlassEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const;
private:
    qsptr<QrealAnimator> mRefraction;
    qsptr<QrealAnimator> mNoise;
    qsptr<QrealAnimator> mGlowWeight;
    qsptr<QrealAnimator> mGlowBias;
    qsptr<QrealAnimator> mHighlight;
    qsptr<QrealAnimator> mHlWidth;
    qsptr<QrealAnimator> mHlAngle;
    qsptr<BoxTargetProperty> mBackgroundTarget;
};

#endif // LIQUIDGLASSEFFECT_H
