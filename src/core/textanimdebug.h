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

// Dev-only debug helper: deliberately kept free of any heavy core
// includes so external test executables can link it without pulling
// unexported template instantiations.

#ifndef TEXTANIMDEBUG_H
#define TEXTANIMDEBUG_H

#include "core_global.h"

#include <QString>

CORE_EXPORT bool textAnimDebugDumpFrames(const QString& outDir);

#endif // TEXTANIMDEBUG_H
