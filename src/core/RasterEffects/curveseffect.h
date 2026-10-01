/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Roddie and contributors
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

#ifndef CURVESEFFECT_H
#define CURVESEFFECT_H

#include "rastereffect.h"
#include "Animators/staticcomplexanimator.h"

class ComboBoxProperty;

// PS/AE "Curves" (曲线): remap each channel through a spline defined
// by five fixed-input anchors (at input 0 / 64 / 128 / 192 / 255).
// Each anchor's output is a keyframable 0..255 value (default =
// input = identity); alpha passes through untouched. The composite
// (RGB) master curve is applied first, then the per-channel curves -
// matching PS. When every anchor sits at its default the effect
// yields no caller at all (AE-style passthrough).
//
// the anchors live inside four wrapper animators ("RGB"/"红"/"绿"/
// "蓝") whose row renders as a curve editor in the AE properties
// inspector; expanding a wrapper reveals the individual keyframable
// value rows. Children are fixed (no runtime add/remove) so
// StaticComplexAnimator's strict serialized child count holds.

class CORE_EXPORT CurvesChannelAnimator : public StaticComplexAnimator {
    e_OBJECT
protected:
    CurvesChannelAnimator(const QString& name);
public:
    static constexpr int Shadows = 0;    // input 0
    static constexpr int Darks = 1;      // input 64
    static constexpr int Mids = 2;       // input 128
    static constexpr int Lights = 3;     // input 192
    static constexpr int Highlights = 4; // input 255
    static constexpr int Count = 5;

    // anchor input positions, normalized 0..1
    static qreal inputX(const int i);
    // anchor default output levels 0..255 (= the inputs)
    static qreal defaultY(const int i);

    QrealAnimator* getAnchor(const int i) const
    { return enve_cast<QrealAnimator*>(ca_getChildAt(i)); }
};

class CORE_EXPORT CurvesEffect : public RasterEffect {
public:
    CurvesEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const override;

    // channel ids in the combo property
    enum Channel { RGB = 0, Red = 1, Green = 2, Blue = 3 };

    ComboBoxProperty *getChannelProperty() const
    { return mChannel.get(); }

    // wrapper for the combo channel id (0..3)
    CurvesChannelAnimator *getChannelAnimator(const int channel) const
    { return mChannels[qBound(0, channel, 3)].get(); }

    // monotone cubic (Fritsch-Carlson) through the five anchors,
    // sampled to a 256-entry LUT; shared with the GUI editor and the
    // unit tests. y[] are output levels 0..255
    static void buildLUT(const qreal y[CurvesChannelAnimator::Count],
                         uint8_t lut[256]);

    // every anchor at its default (= the identity curve)
    static bool isIdentity(const qreal y[CurvesChannelAnimator::Count]);
private:
    qsptr<ComboBoxProperty> mChannel;
    qsptr<CurvesChannelAnimator> mChannels[4];
};

#endif // CURVESEFFECT_H
