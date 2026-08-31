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

#ifndef VTRACERPROVIDER_H
#define VTRACERPROVIDER_H

#include "core_global.h"

#include <QString>

#include "skia/skiaincludes.h"

namespace VTracer {

struct Options {
    int mode = 2;             // 0 pixel / 1 polygon / 2 spline
    int hierarchical = 0;     // 0 stacked / 1 cutout
    int binary = 0;           // 0 color / 1 binary
    int filterSpeckle = 4;
    int colorPrecision = 6;
    int layerDifference = 16;
    int cornerThreshold = 60;
    int lengthThreshold = 4;
    int maxIterations = 10;
    int spliceThreshold = 45;
    int pathPrecision = 2;
    int maxPaths = 500;
};

enum class TraceStatus {
    Ok,            // svgOut holds the SVG document
    NotAvailable,  // vtracer.dll missing or exports not resolved
    PathLimit,     // too complex: limit exceeded even after auto tightening
    Error          // internal failure, details in errOut (ASCII)
};

CORE_EXPORT bool available();
CORE_EXPORT QString versionString();

// Converts an image to an SVG string. The image is read as
// unpremultiplied RGBA8 regardless of its native format.
CORE_EXPORT TraceStatus traceToSvg(const sk_sp<SkImage> &image,
                                   const Options &opts,
                                   QString &svgOut,
                                   int &pathCountOut,
                                   QString &errOut);

} // namespace VTracer

#endif // VTRACERPROVIDER_H
