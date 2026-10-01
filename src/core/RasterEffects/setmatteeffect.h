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

#ifndef SETMATTEEFFECT_H
#define SETMATTEEFFECT_H

#include "rastereffect.h"
#include "Properties/boxtargetproperty.h"

class ComboBoxProperty;

// AE Set Matte: masks THIS layer with another layer's alpha/luma,
// as an entry in the effect stack (unlike the layer-level track
// matte switch). The matte layer keeps drawing normally, several
// layers may sample the same matte, and the effect composes freely
// with the rest of the stack. The pixel treatment reuses
// TrackMatteCaller (alpha / alphaInv / luma / lumaInv).
class CORE_EXPORT SetMatteEffect : public RasterEffect {
public:
    SetMatteEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const;

    FrameRange prp_getIdenticalRelRange(const int relFrame) const;
private:
    qsptr<BoxTargetProperty> mMatteTarget;
    qsptr<ComboBoxProperty> mMode;
    ConnContextQPtr<BoundingBox> mFollowConn;
};

#endif // SETMATTEEFFECT_H
