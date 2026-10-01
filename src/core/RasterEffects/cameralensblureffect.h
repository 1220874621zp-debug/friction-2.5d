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

#ifndef CAMERALENSBLUREFFECT_H
#define CAMERALENSBLUREFFECT_H

#include "rastereffect.h"
#include "Properties/boxtargetproperty.h"

class QrealAnimator;
class BoolAnimator;

// AE Camera Lens Blur approximation: whole-frame defocus blur
// plus threshold-extracted highlights re-blurred and screen-composited
// back on top (bright spots bloom into soft bokeh discs).
//
// With a DEPTH MAP layer attached (AE: Blur Map Layer) the blur radius
// becomes per-pixel: the depth layer's luminance drives it, "focal
// distance" picks the depth that stays sharp and "depth of field" scales
// how fast the blur ramps away from it. That mode builds a small blur
// level pyramid and blends between levels per pixel, so it runs on the
// CPU (the uniform mode keeps its GPU path).
class CORE_EXPORT CameraLensBlurEffect : public RasterEffect {
    e_OBJECT
protected:
    CameraLensBlurEffect();
public:
    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData* const data) const;
    QMargins getMargin() const;
    bool forceMargin() const { return true; }

    // the host's pixels depend on the depth layer's content: a static host
    // under an animated depth map is NOT frame-identical (same pattern as
    // SetMatteEffect / TargetTransformEffect)
    FrameRange prp_getIdenticalRelRange(const int relFrame) const;
protected:
    // depth map block (target/focal/dof/invert) was appended as 4
    // serialized children in format 53; older files carry only
    // radius/threshold/gain (positional child layout)
    int ca_readChildCount(const int evFileVersion) const override;
private:
    qsptr<QrealAnimator> mRadius;
    qsptr<QrealAnimator> mThreshold;
    qsptr<QrealAnimator> mGain;

    qsptr<BoxTargetProperty> mDepthTarget;
    qsptr<QrealAnimator> mFocal;
    qsptr<QrealAnimator> mDof;
    qsptr<BoolAnimator> mInvert;
    ConnContextQPtr<BoundingBox> mFollowConn;
};

#endif // CAMERALENSBLUREFFECT_H
