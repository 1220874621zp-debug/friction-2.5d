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
// by five anchors. The two end anchors sit at input 0 / 255 (their
// input is fixed, exactly like PS); the three middle ones (dark / mid /
// light) additionally own an input-position animator, so they can be
// dragged freely in both axes and animated over time. Each anchor's
// output is a keyframable 0..255 value (default = input = identity);
// alpha passes through untouched. The composite (RGB) master curve is
// applied first, then the per-channel curves - matching PS. When every
// anchor sits at its default the effect yields no caller at all
// (AE-style passthrough).
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

    // input-position animators exist for the three middle anchors only
    static constexpr int FirstMovable = Darks;
    static constexpr int LastMovable = Lights;
    static constexpr int InputCount = LastMovable - FirstMovable + 1;
    static bool isMovable(const int i)
    { return i >= FirstMovable && i <= LastMovable; }

    // default anchor input positions, normalized 0..1
    static qreal inputX(const int i);
    // anchor default output levels 0..255 (= the inputs)
    static qreal defaultY(const int i);

    QrealAnimator* getAnchor(const int i) const
    { return enve_cast<QrealAnimator*>(ca_getChildAt(i)); }
    // input-position animator (nullptr for the two fixed end anchors)
    QrealAnimator* getInputAnimator(const int i) const;
    // all five input positions at relFrame, ordered and inside 0..1
    void inputsAt(const qreal relFrame, qreal x[Count]) const;
    // clamp a dragged input so the anchors can never cross
    qreal clampInputAt(const int i, const qreal level255,
                       const qreal relFrame) const;

    int ca_readChildCount(const int evFileVersion) const override;
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
    // unit tests. y[] are output levels 0..255; the overload takes the
    // live (draggable) input positions in x[] as well
    static void buildLUT(const qreal y[CurvesChannelAnimator::Count],
                         uint8_t lut[256]);
    static void buildLUT(const qreal x[CurvesChannelAnimator::Count],
                         const qreal y[CurvesChannelAnimator::Count],
                         uint8_t lut[256]);

    // every anchor at its default (= the identity curve)
    static bool isIdentity(const qreal y[CurvesChannelAnimator::Count]);
    static bool isIdentity(const qreal x[CurvesChannelAnimator::Count],
                           const qreal y[CurvesChannelAnimator::Count]);
private:
    qsptr<ComboBoxProperty> mChannel;
    qsptr<CurvesChannelAnimator> mChannels[4];
};

#endif // CURVESEFFECT_H
