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

#include "tmpsaver.h"

#include "appsupport.h"
#include "CacheHandlers/tmpfileregistry.h"

#include <QDir>

TmpSaver::TmpSaver(HddCachableCont* const target) :
    mTarget(target) {}

void TmpSaver::process() {
    // Keep the tmp files in our own cache dir instead of the system temp:
    // Windows Storage Sense / disk cleanup wipes %TEMP%, which would leave
    // the cache containers pointing at deleted files.
    const QString cacheDir = AppSupport::getAppCachePath() + "/eCache";
    QDir().mkpath(cacheDir);
    mTmpFile = qsptr<QTemporaryFile>(
                new QTemporaryFile(cacheDir + "/eCache_XXXXXX"));
    if(mTmpFile->open()) {
        // register before writing so cache cleanup never races a save
        // that is mid-flight
        TmpFileRegistry::add(mTmpFile->fileName());
        eWriteStream dst(mTmpFile.get());
        write(dst);
        mTmpFile->close();
        mSavingSuccessful = true;
    } else {
        mSavingSuccessful = false;
    }
}

void TmpSaver::afterProcessing() {
    if(!mTarget) return;
    if(!mSavingSuccessful) {
        mTarget->setDataSaveFailed();
        return;
    }
    mTarget->setDataSavedToTmpFile(mTmpFile);
}
