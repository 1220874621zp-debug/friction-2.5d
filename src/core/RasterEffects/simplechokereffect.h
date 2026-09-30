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

#ifndef SIMPLECHOKEREFFECT_H
#define SIMPLECHOKEREFFECT_H

#include "rastereffect.h"

// AE "Simple Choker" (Matte): shrink or grow the layer's alpha matte.
// Positive "choke matte" chokes (eats into) the matte, negative spreads
// it outward; colors ride along, edges stay anti-aliased. Internally a
// gaussian-blurred alpha re-leveled asymmetrically around 50% - the
// same family as the layer-styles PS spread formula, so the amount
// saturates smoothly like AE's instead of eroding without bound
class SimpleChokerEffect : public RasterEffect {
public:
    SimpleChokerEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const override;
private:
    qsptr<QrealAnimator> mChokeMatte;
};

#endif // SIMPLECHOKEREFFECT_H
