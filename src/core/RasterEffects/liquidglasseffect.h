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

// Liquid glass (magnifier) distortion ported from a Shadertoy shader
// https://gist.github.com/emmachase/25af1fb66daebf0f9989c93d3c8c5fa6
// The CPU path mirrors liquidglasseffect.frag step by step, colors are
// premultiplied throughout (clamped against alpha after the glow).

#ifndef LIQUIDGLASSEFFECT_H
#define LIQUIDGLASSEFFECT_H

#include "rastereffect.h"
#include "Animators/qrealanimator.h"

struct LiquidGlassEffectData {
    // center in GL uv space (y is UP; the panel-facing animator is
    // 0 = top and flipped by getEffectCaller)
    float mCenterX = 0.5f;
    float mCenterY = 0.5f;
    float mSize = 0.15f;
    float mShapeN = 3.f;
    float mRefraction = 3.f;
    float mNoise = 0.06f;
    float mGlowWeight = 0.35f;
    float mGlowBias = 0.f;
    // render surface size in pixels (from BoxRenderData::fGlobalRect,
    // needed for the aspect correction; setVars has no other access)
    float mTexW = 1.f;
    float mTexH = 1.f;
};

class LiquidGlassEffect : public RasterEffect {
public:
    LiquidGlassEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const;
private:
    qsptr<QrealAnimator> mCenterX;
    qsptr<QrealAnimator> mCenterY;
    qsptr<QrealAnimator> mSize;
    qsptr<QrealAnimator> mShapeN;
    qsptr<QrealAnimator> mRefraction;
    qsptr<QrealAnimator> mNoise;
    qsptr<QrealAnimator> mGlowWeight;
    qsptr<QrealAnimator> mGlowBias;
};

#endif // LIQUIDGLASSEFFECT_H
