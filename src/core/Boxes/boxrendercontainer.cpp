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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "boxrendercontainer.h"
#include "boxrenderdata.h"
#include "skia/skiahelpers.h"

void RenderContainer::drawSk(SkCanvas * const canvas,
                             const SkFilterQuality filter) const {
    if(!mSrcRenderData) return;
    canvas->save();
    canvas->concat(mPaintTransform);
    SkPaint paint;
    paint.setFilterQuality(filter);
    mSrcRenderData->drawOnParentLayer(canvas, paint);
    canvas->restore();
}

void RenderContainer::updatePaintTransformGivenNewTotalTransform(
                                    const SkMatrix &fullTransform) {
    // The stale bitmap was rasterized with mFullTransform (which ends in the
    // render resolution scale), so mapping pixel -> new canvas is
    // F_new * F_old^-1 - the 1/res pixel step cancels against the resolution
    // baked into F_old. Both matrices include the scene camera, so dragging
    // under a rotated/tilted camera no longer bounces between the wrongly
    // compensated stale bitmap and the freshly rendered one.
    SkMatrix inverted;
    if(mFullTransform.invert(&inverted)) {
        mPaintTransform = SkMatrix::Concat(fullTransform, inverted);
    } else {
        // degenerate old transform (e.g. scale animated through zero):
        // fall back to plain resolution scaling
        const qreal invRes = 1/mResolutionFraction;
        SkMatrix paintTransform;
        paintTransform.setScale(toSkScalar(invRes), toSkScalar(invRes));
        mPaintTransform = paintTransform;
    }
}

void RenderContainer::clear() {
    mImageSk.reset();
    mSrcRenderData.reset();
}

void RenderContainer::setSrcRenderData(BoxRenderData * const data) {
    mTransform = data->fTotalTransform;
    mFullTransform = data->getFullRenderTransform();
    mResolutionFraction = data->fResolution;
    mImageSk = data->fRenderedImage;
    mGlobalRect = data->fGlobalRect;
    mAntiAlias = data->fAntiAlias;
    SkMatrix paintTransform;
    paintTransform.setScale(toSkScalar(1/mResolutionFraction),
                            toSkScalar(1/mResolutionFraction));
    mPaintTransform = paintTransform;
    mSrcRenderData = data->ref<BoxRenderData>();
}
