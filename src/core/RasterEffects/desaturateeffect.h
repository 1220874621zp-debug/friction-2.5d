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

#ifndef DESATURATEEFFECT_H
#define DESATURATEEFFECT_H

#include "rastereffect.h"

class BoolAnimator;

// AE "Black & White" / PS "Desaturate": collapse the image to its
// Rec.601 luminance; alpha passes through untouched. The amount
// parameter blends between the original colors (0) and full
// grayscale (100, the AE/PS default behavior). With "invert" on the
// target flips: instead of keeping the luminance and dropping the
// colors it keeps the colors and drops the luminance (each channel
// minus the channel minimum = the pure chroma component), so full
// amount yields a flat-brightness vivid-color image
class DesaturateEffect : public RasterEffect {
public:
    DesaturateEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const override;
private:
    qsptr<QrealAnimator> mAmount;
    qsptr<BoolAnimator> mInvert;
};

#endif // DESATURATEEFFECT_H
