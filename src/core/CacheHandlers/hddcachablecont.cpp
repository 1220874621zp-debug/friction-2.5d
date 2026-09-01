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

#include "hddcachablecont.h"
#include "Tasks/etask.h"

HddCachableCont::HddCachableCont() {}

HddCachableCont::~HddCachableCont() {
    if(mTmpFile) scheduleDeleteTmpFile();
}

int HddCachableCont::free_RAM_k() {
    // Preserve the data in a tmp file before dropping it from memory,
    // so it can be reloaded without re-decoding the source files.
    if(!mTmpFile && !mTmpSaveTask) scheduleSaveToTmpFile();
    const int bytes = clearMemory();
    setDataInMemory(false);
    if(!mTmpFile && !mTmpSaveTask) noDataLeft_k();
    return bytes;
}

eTask *HddCachableCont::scheduleDeleteTmpFile() {
    if(!mTmpFile) return nullptr;
    const auto updatable = enve::make_shared<TmpDeleter>(mTmpFile);
    mTmpFile.reset();
    updatable->queTask();
    return updatable.get();
}

eTask *HddCachableCont::scheduleSaveToTmpFile() {
    // a canceled save task blocks all future saves (mTmpSaveTask is
    // only reset on success/failure callbacks) and cancels any loader
    // registered as its dependent - drop it and start over
    if(mTmpSaveTask && mTmpSaveTask->getState() == eTaskState::canceled) {
        mTmpSaveTask.reset();
    }
    if(mTmpSaveTask || mTmpFile) return nullptr;
    mTmpSaveTask = createTmpFileDataSaver();
    mTmpSaveTask->queTask();
    return mTmpSaveTask.get();
}

eTask *HddCachableCont::scheduleLoadFromTmpFile() {
    if(storesDataInMemory()) return nullptr;
    if(mTmpLoadTask) {
        // a canceled loader never runs again and poisons every later
        // dependent (eTaskBase::addDependent cancels dependents of a
        // canceled task) - drop it so a fresh loader is created
        // instead of the container staying evicted forever
        if(mTmpLoadTask->getState() == eTaskState::canceled) {
            mTmpLoadTask.reset();
        } else {
            return mTmpLoadTask.get();
        }
    }
    if(!mTmpSaveTask && !mTmpFile) return nullptr;

    mTmpLoadTask = createTmpFileDataLoader();
    if(mTmpSaveTask)
        mTmpSaveTask->addDependent(mTmpLoadTask.get());
    mTmpLoadTask->queTask();
    return mTmpLoadTask.get();
}

void HddCachableCont::setDataSavedToTmpFile(const qsptr<QTemporaryFile> &tmpFile) {
    mTmpSaveTask.reset();
    mTmpFile = tmpFile;
}

void HddCachableCont::setDataSaveFailed() {
    mTmpSaveTask.reset();
    // free_RAM_k already dropped the in-memory data; without a tmp file
    // there is nothing left to load back. Drop the container so its owner
    // (e.g. ImageFileDataHandler) falls back to reloading from the source
    // file, instead of every later load waiting on this dead save task and
    // ending up with a null image rendered as an empty layer.
    if(!storesDataInMemory() && !mTmpFile) noDataLeft_k();
}

void HddCachableCont::afterDataLoadedFromTmpFile() {
    setDataInMemory(true);
    mTmpLoadTask.reset();
    if(!inUse()) addToMemoryManagment();
}

void HddCachableCont::afterDataReplaced() {
    setDataInMemory(true);
    updateInMemoryManagment();
    if(mTmpFile) scheduleDeleteTmpFile();
}

void HddCachableCont::setDataInMemory(const bool dataInMemory) {
    mDataInMemory = dataInMemory;
}
