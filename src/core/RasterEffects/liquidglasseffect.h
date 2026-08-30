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

// Liquid glass (water-droplet) backdrop effect, ported from a
// Shadertoy shader https://gist.github.com/emmachase/25af1fb66daebf0f9989c93d3c8c5fa6
//
// The LAYER ITSELF is the glass: whatever the layer draws (vector
// shape or bitmap, at its actual pixel size and position) becomes the
// glass footprint through its alpha. At composite time the backdrop
// below is snapshotted, and inside the footprint the backdrop is
// refracted along the shape-edge inward normals (distance-field
// driven, like light bending through a water droplet) with an angular
// rim glow and grain. The layer's own semi-transparent content then
// composites on top as the glass tint.

#ifndef LIQUIDGLASSEFFECT_H
#define LIQUIDGLASSEFFECT_H

#include "rastereffect.h"
#include "Animators/qrealanimator.h"

struct LiquidGlassEffectData {
    float mRefraction = 3.f;
    float mNoise = 0.06f;
    float mGlowWeight = 0.35f;
    float mGlowBias = 0.f;
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
};

#endif // LIQUIDGLASSEFFECT_H
