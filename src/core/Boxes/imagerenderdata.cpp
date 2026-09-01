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

#include "imagerenderdata.h"
#include "Boxes/boundingbox.h"

#include <QDebug>

ImageRenderData::ImageRenderData(BoundingBox * const parentBoxT) :
    BoxRenderData(parentBoxT) {
    mDelayDataSet = true;
}

void ImageRenderData::updateRelBoundingRect() {
    if(fImage) fRelBoundingRect =
            QRectF(0, 0, fImage->width(), fImage->height());
    else fRelBoundingRect = QRectF(0, 0, 0, 0);
}

void ImageRenderData::setupRenderData() {
    if(!fImage) loadImageFromHandler();
    if(!fImage) {
        qWarning() << "IMGDRAW: no image after load attempt for"
                   << fParentBox->prp_getName();
    }
    // 2.5D: perspective requires rasterization (direct draw is 2D only)
    if(!fForceRasterize && !hasEffects() && !fHasPerspective) setupDirectDraw();
}

void ImageRenderData::setupDirectDraw() {
    fBaseMargin = QMargins();
    dataSet();
    updateGlobalRect();
    fRenderTransform.reset();
    fRenderTransform.translate(fRelBoundingRect.x(), fRelBoundingRect.y());
    fRenderTransform *= fScaledTransform;
    fRenderTransform.translate(-fGlobalRect.x(), -fGlobalRect.y());
    fUseRenderTransform = true;
    fRenderedImage = fImage;
    fAntiAlias = true;
    finishedProcessing();
}

void ImageRenderData::drawSk(SkCanvas * const canvas) {
    const float x = static_cast<float>(fRelBoundingRect.x());
    const float y = static_cast<float>(fRelBoundingRect.y());
    if(fFilterQuality > kNone_SkFilterQuality) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setFilterQuality(fFilterQuality);
        canvas->drawImage(fImage, x, y, &paint);
    } else if(fImage) canvas->drawImage(fImage, x, y);
}

void ImageContainerRenderData::setContainer(ImageCacheContainer *container) {
    if(!container) return;
    mSrcContainer = container;
    fImage = container->requestImageCopy();
    if(!fImage && container->hasImage()) {
        // the master image exists but the copy failed (allocation
        // pressure / non-raster backing) - without this log the layer
        // just vanishes downstream with a generic IMGDRAW and the
        // real cause (copy failure) stays invisible
        qWarning() << "IMGCOPY: requestImageCopy returned null although"
                      " the container holds an image"
                   << "inMem=" << container->storesDataInMemory();
    }
}

void ImageContainerRenderData::afterProcessing() {
    BoxRenderData::afterProcessing();
    if(mSrcContainer && fImage) {
        mSrcContainer->addImageCopy(fImage);
    }
}
