/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
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

#ifndef LOTTIEPROVIDER_H
#define LOTTIEPROVIDER_H

#include "core_global.h"
#include <QString>
#include "skia/skiaincludes.h"

// Runtime binding to frictionskottie (Skia's skottie modules in a
// standalone shared library, see src/skottie-ffi). Loaded with
// QLibrary so a missing library degrades the Lottie layer instead of
// breaking the app - same pattern as the vtracer provider.
namespace LottieLib {

CORE_EXPORT bool available();

// opaque animation handles; release with releaseHandle()
CORE_EXPORT void* loadFromFile(const QString& path, const QString& baseDir,
                               QString& errOut);
CORE_EXPORT void* loadFromData(const QByteArray& json, QString& errOut);

CORE_EXPORT void addRef(void* handle);
CORE_EXPORT void releaseHandle(void* handle);

CORE_EXPORT double duration(void* handle);
CORE_EXPORT double fps(void* handle);
CORE_EXPORT int width(void* handle);
CORE_EXPORT int height(void* handle);

// renders the frame at tSec (seconds) into a transparent raster
// image; null image when the handle is null or rendering failed
CORE_EXPORT sk_sp<SkImage> renderFrame(void* handle, const qreal tSec);

CORE_EXPORT QString versionString();

} // namespace LottieLib

#endif // LOTTIEPROVIDER_H
