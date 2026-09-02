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

#ifndef LAYERSTYLESEFFECT_H
#define LAYERSTYLESEFFECT_H

#include "rastereffect.h"

class QrealAnimator;
class ColorAnimator;
class BoolAnimator;
class ComboBoxProperty;

// Photoshop-style layer styles collected in one effect so the
// fixed PS stacking order can be honored: drop shadow below outer
// glow below the layer content, stroke on top. Every style is
// computed from the layer's pristine alpha (the source image is
// sampled directly, never a previous style's output), which is
// what makes PSD files with layer styles render like Photoshop.
// CORE_EXPORT: the effects unit-test executable calls the import
// setters directly (same-module users don't need it)
class CORE_EXPORT LayerStylesEffect : public RasterEffect {
public:
    LayerStylesEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const override;

    // allocation-time margin so the scene bounds account for the
    // styles before any render data exists (blur pattern)
    bool forceMargin() const { return true; }
    QMargins getMargin() const;

    // per-style toggles, used by the layer context menu and the
    // PSD importer ("图层样式" entries create-or-toggle this effect)
    BoolAnimator* shadowEnabled() const { return mShadowEnabled.get(); }
    BoolAnimator* glowEnabled() const { return mGlowEnabled.get(); }
    BoolAnimator* strokeEnabled() const { return mStrokeEnabled.get(); }

    // stroke position combo: 0 = outside, 1 = center, 2 = inside
    ComboBoxProperty* strokePosition() const { return mStrokePosition.get(); }

    // PSD import setters (plain, one-shot values, no keyframes)
    void setShadow(const bool enabled, const qreal angle, const qreal distance,
                   const qreal spread, const qreal size, const qreal opacity,
                   const QColor& color);
    void setGlow(const bool enabled, const qreal spread, const qreal size,
                 const qreal opacity, const QColor& color);
    void setStroke(const bool enabled, const int position, const qreal size,
                   const qreal opacity, const QColor& color);
private:
    qsptr<BoolAnimator> mShadowEnabled;
    qsptr<QrealAnimator> mShadowAngle;
    qsptr<QrealAnimator> mShadowDistance;
    qsptr<QrealAnimator> mShadowSpread;
    qsptr<QrealAnimator> mShadowSize;
    qsptr<QrealAnimator> mShadowOpacity;
    qsptr<ColorAnimator> mShadowColor;

    qsptr<BoolAnimator> mGlowEnabled;
    qsptr<QrealAnimator> mGlowSpread;
    qsptr<QrealAnimator> mGlowSize;
    qsptr<QrealAnimator> mGlowOpacity;
    qsptr<ColorAnimator> mGlowColor;

    qsptr<BoolAnimator> mStrokeEnabled;
    qsptr<ComboBoxProperty> mStrokePosition;
    qsptr<QrealAnimator> mStrokeSize;
    qsptr<QrealAnimator> mStrokeOpacity;
    qsptr<ColorAnimator> mStrokeColor;
};

#endif // LAYERSTYLESEFFECT_H
