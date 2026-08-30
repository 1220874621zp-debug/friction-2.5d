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

#include "tmploader.h"

#include <QDebug>

TmpLoader::TmpLoader(const qsptr<QTemporaryFile> &file,
                     HddCachableCont * const target) :
    mTmpFile(file), mTarget(target) {}

void TmpLoader::process() {
    if(!mTmpFile) return;
    if(mTmpFile->open()) {
        eReadStream src(mTmpFile.get());
        read(src);
        mTmpFile->close();
    } else {
        // Do not throw here: an exception cancels every render task waiting
        // on this load (e.g. the tmp file was removed by disk cleanup).
        // Returning with no data lets image containers fall back to
        // reloading from their source file instead.
        qWarning() << "TmpLoader: could not open cache file for reading:"
                   << mTmpFile->fileName();
    }
}

void TmpLoader::beforeProcessing(const Hardware) {
    if(mTarget && !mTmpFile) mTmpFile = mTarget->getTmpFile();
}
