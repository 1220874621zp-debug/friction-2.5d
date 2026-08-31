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
*/

#include "CacheHandlers/tmpfileregistry.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>

namespace {

QString normalize(const QString& path)
{
    return QDir::cleanPath(path).toLower();
}

// shared by add/remove/livePaths; TmpSaver and TmpDeleter run on HDD
// worker threads, so access is guarded
QMutex gMutex;
QSet<QString> gFiles;

} // namespace

namespace TmpFileRegistry {

void add(const QString& path)
{
    QMutexLocker lock(&gMutex);
    gFiles.insert(normalize(path));
}

void remove(const QString& path)
{
    QMutexLocker lock(&gMutex);
    gFiles.remove(normalize(path));
}

QSet<QString> livePaths()
{
    QMutexLocker lock(&gMutex);
    return gFiles;
}

} // namespace TmpFileRegistry
