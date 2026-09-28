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

#ifndef LEVELSEFFECT_H
#define LEVELSEFFECT_H

#include "rastereffect.h"

class ComboBoxProperty;

// PS "Levels" (色阶): remap the tonal range with input black/white
// points, a midtone gamma and output black/white points, either on
// the composite (RGB) or a single channel; alpha passes through
// untouched. Defaults (0 / 1.0 / 255 / 0 / 255) are the identity -
// the effect then yields no caller at all (AE-style passthrough)
class LevelsEffect : public RasterEffect {
public:
    LevelsEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const override;

    // channel ids in the combo property
    enum Channel { RGB = 0, Red = 1, Green = 2, Blue = 3 };

    // raw animators for the PS-style dialog and the unit tests
    ComboBoxProperty *getChannelProperty() const
    { return mChannel.get(); }
    QrealAnimator *getInBlackAnimator() const { return mInBlack.get(); }
    QrealAnimator *getGammaAnimator() const { return mGamma.get(); }
    QrealAnimator *getInWhiteAnimator() const { return mInWhite.get(); }
    QrealAnimator *getOutBlackAnimator() const { return mOutBlack.get(); }
    QrealAnimator *getOutWhiteAnimator() const { return mOutWhite.get(); }

    static constexpr qreal sMinGamma = 0.10;
    static constexpr qreal sMaxGamma = 9.99;
private:
    qsptr<ComboBoxProperty> mChannel;
    qsptr<QrealAnimator> mInBlack;
    qsptr<QrealAnimator> mGamma;
    qsptr<QrealAnimator> mInWhite;
    qsptr<QrealAnimator> mOutBlack;
    qsptr<QrealAnimator> mOutWhite;
};

#endif // LEVELSEFFECT_H
