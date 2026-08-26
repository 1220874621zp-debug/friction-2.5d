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

#include "sceneframecontainer.h"
#include "../Boxes/boxrenderdata.h"
#include "../canvas.h"

SceneFrameContainer::SceneFrameContainer(
        Canvas * const scene,
        const BoxRenderData * const data,
        const FrameRange &range,
        HddCachableCacheHandler * const parent) :
    ImageCacheContainer(data->fRenderedImage, range, parent),
    fBoxState(data->fBoxStateId),
    fResolution(data->fResolution),
    mScene(scene) {}

stdsptr<eHddTask> SceneFrameContainer::createTmpFileDataLoader() {
    const ImgLoader::Func func = [this](sk_sp<SkImage> img) {
        setDataLoadedFromTmpFile(img);
        // Refresh the canvas only if this frame is still the current one.
        // During playback the playhead may have moved past this frame by
        // the time the load finishes; assigning it then would make the
        // canvas jump backwards (visible as flickering).
        if(mScene) {
            const int curRelFrame = mScene->anim_getCurrentRelFrame();
            if(getRange().inRange(curRelFrame))
                mScene->setSceneFrame(ref<SceneFrameContainer>());
        }
    };
    return enve::make_shared<ImgLoader>(mTmpFile, this, func);
}
