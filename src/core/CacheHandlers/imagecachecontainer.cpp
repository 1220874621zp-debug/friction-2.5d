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

#include "imagecachecontainer.h"
#include "tmpdeleter.h"
#include "canvas.h"
#include "skia/skiahelpers.h"

ImageCacheContainer::ImageCacheContainer(const FrameRange &range,
                                         HddCachableCacheHandler * const parent) :
    HddCachableRangeCont(range, parent) {}

ImageCacheContainer::ImageCacheContainer(const sk_sp<SkImage> &img,
                                         const FrameRange &range,
                                         HddCachableCacheHandler * const parent) :
    ImageCacheContainer(range, parent) {
    replaceImage(img);
}

void ImageCacheContainer::replaceImage(const sk_sp<SkImage> &img) {
    ImageDataHandler::replaceImage(img);
    afterDataReplaced();
}

int ImageCacheContainer::getByteCount() {
    return getImageByteCount();
}

void ImageCacheContainer::setDataLoadedFromTmpFile(const sk_sp<SkImage> &img) {
    if(!img) {
        // the tmp load delivered nothing (missing/corrupt/empty tmp
        // file): marking the container in-memory with a null image
        // would silently disable every future reload attempt
        // (storesDataInMemory()==true short-circuits
        // scheduleLoadFromTmpFile). Reset to the unloaded state and
        // drop the bad tmp file so the next scheduleLoad falls back
        // to the source file.
        qWarning() << "TMPLOAD-EMPTY: tmp reload delivered no image,"
                      " resetting container for source reload";
        resetTmpLoadTask();
        setDataInMemory(false);
        if(mTmpFile) scheduleDeleteTmpFile();
        return;
    }
    replaceImage(img);
    afterDataLoadedFromTmpFile();
}

int ImageCacheContainer::clearMemory() {
    return ImageDataHandler::clearImageMemory();
}

stdsptr<eHddTask> ImageCacheContainer::createTmpFileDataSaver() {
    return enve::make_shared<ImgSaver>(this, getImage());
}

stdsptr<eHddTask> ImageCacheContainer::createTmpFileDataLoader() {
    const ImgLoader::Func func = [this](sk_sp<SkImage> img) {
        setDataLoadedFromTmpFile(img);
    };
    return enve::make_shared<ImgLoader>(mTmpFile, this, func);
}
