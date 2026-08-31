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

#ifndef TMPFILEREGISTRY_H
#define TMPFILEREGISTRY_H

#include "core_global.h"

#include <QSet>
#include <QString>

// Tracks which eCache spill files are owned by live cache containers
// (created by TmpSaver, destroyed by TmpDeleter). The preferences cache
// cleaner uses this to skip files that are currently in use. Called from
// HDD worker threads, hence the internal mutex.
namespace TmpFileRegistry {

CORE_EXPORT void add(const QString& path);
CORE_EXPORT void remove(const QString& path);
CORE_EXPORT QSet<QString> livePaths();

} // namespace TmpFileRegistry

#endif // TMPFILEREGISTRY_H
