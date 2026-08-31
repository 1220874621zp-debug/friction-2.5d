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

#ifndef AIDEPTH_PROVIDER_H
#define AIDEPTH_PROVIDER_H

#include "core_global.h"

#include <QString>
#include <vector>

#include "skia/skiaincludes.h"

namespace AiDepth {

enum class DepthStatus {
    Ok,           // depthOut holds the colorized depth map
    NotAvailable, // onnxruntime.dll missing or exports not resolved
    ModelMissing, // model file group not found in the model dirs
    Error         // inference failure, details in errOut
};

struct Options {
    int fInputSize = 518; // longest inference side, multiple of 14
    int fOutputMode = 0;  // 0 grayscale / 1 inverted / 2 false color (JET)
};

CORE_EXPORT bool available();
CORE_EXPORT QString versionString();

// Runs Depth-Anything-V2 on srcImage (any layer render) using the model
// stored in modelDir (see ModelCatalog::resolveModelDir). Returns a depth
// map at the source resolution; safe to call from a worker thread.
CORE_EXPORT DepthStatus runDepth(const sk_sp<SkImage>& srcImage,
                                 const QString& modelDir,
                                 const Options& opts,
                                 sk_sp<SkImage>& depthOut,
                                 QString& errOut);

// Raw single-frame inference: un-normalized disparity values at the
// inference resolution (sides are multiples of 14, long side clamped to
// inputSize). For video pipelines that smooth raw values across frames.
CORE_EXPORT DepthStatus runDepthRaw(const sk_sp<SkImage>& srcImage,
                                    const QString& modelDir,
                                    const int inputSize,
                                    std::vector<float>& depthOut,
                                    int& wOut, int& hOut,
                                    QString& errOut);

// Colorizes a raw depth map with the given value range (values are
// clamped) and resamples the result to dstW x dstH. outputMode as in
// Options; pass mn = 0 and mx = 1 for data that is already normalized.
CORE_EXPORT sk_sp<SkImage> colorizeDepth(const std::vector<float>& data,
                                         const int w, const int h,
                                         const float mn, const float mx,
                                         const int outputMode,
                                         const int dstW, const int dstH);

} // namespace AiDepth

#endif // AIDEPTH_PROVIDER_H
