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

#include "vtracerprovider.h"

#include "vtracer/vtracer_ffi.h"

#include <QLibrary>

#include <cstring>
#include <vector>

using VtracerTraceFn = char* (*)(int, int, const unsigned char*,
                                 int, int, int, int, int, int, int, int,
                                 int, int, int, int,
                                 int*, int*, char*, int);
using VtracerFreeFn = void (*)(char*);
using VtracerVersionFn = char* (*)();

namespace {

// Loaded once on first use; a missing DLL must not take the app down with
// it, so this is a runtime QLibrary load instead of a link-time dependency.
struct VtracerLib {
    QLibrary mLib;
    VtracerTraceFn mTrace = nullptr;
    VtracerFreeFn mFree = nullptr;
    VtracerVersionFn mVersion = nullptr;
    bool mOk = false;

    VtracerLib() : mLib(QStringLiteral("vtracer")) {
        mOk = mLib.load();
        if (!mOk) { return; }
        mTrace = reinterpret_cast<VtracerTraceFn>(
                    mLib.resolve("vtracer_trace_rgba"));
        mFree = reinterpret_cast<VtracerFreeFn>(
                    mLib.resolve("vtracer_free_string"));
        mVersion = reinterpret_cast<VtracerVersionFn>(
                    mLib.resolve("vtracer_ffi_version"));
        mOk = mTrace && mFree && mVersion;
    }
};

VtracerLib &vtracerLib()
{
    static VtracerLib lib;
    return lib;
}

} // namespace

namespace VTracer {

bool available()
{
    return vtracerLib().mOk;
}

QString versionString()
{
    const auto &lib = vtracerLib();
    if (!lib.mOk || !lib.mVersion) { return QStringLiteral("vtracer (not loaded)"); }
    char * const v = lib.mVersion();
    if (!v) { return QStringLiteral("vtracer (unknown)"); }
    const QString result = QString::fromLatin1(v);
    lib.mFree(v);
    return result;
}

TraceStatus traceToSvg(const sk_sp<SkImage> &image,
                       const Options &opts,
                       QString &svgOut,
                       int &pathCountOut,
                       QString &errOut)
{
    svgOut.clear();
    pathCountOut = 0;
    errOut.clear();

    const auto &lib = vtracerLib();
    if (!lib.mOk || !lib.mTrace) {
        errOut = QStringLiteral("vtracer.dll not found or exports missing");
        return TraceStatus::NotAvailable;
    }
    if (!image) {
        errOut = QStringLiteral("null image");
        return TraceStatus::Error;
    }

    // vtracer expects unpremultiplied RGBA8 rows; images on this platform
    // are usually premultiplied BGRA, so convert through readPixels.
    const auto raster = image->makeRasterImage();
    if (!raster) {
        errOut = QStringLiteral("makeRasterImage failed");
        return TraceStatus::Error;
    }
    const int w = raster->width();
    const int h = raster->height();
    if (w <= 0 || h <= 0) {
        errOut = QStringLiteral("empty image");
        return TraceStatus::Error;
    }
    const auto info = SkImageInfo::Make(w, h,
                                        kRGBA_8888_SkColorType,
                                        kUnpremul_SkAlphaType);
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    if (!raster->readPixels(info, rgba.data(),
                            static_cast<size_t>(w) * 4, 0, 0)) {
        errOut = QStringLiteral("pixel format conversion failed");
        return TraceStatus::Error;
    }

    char errBuf[512];
    std::memset(errBuf, 0, sizeof(errBuf));
    int status = VTRACER_ERROR;
    int pathCount = 0;
    char * const svg = lib.mTrace(w, h, rgba.data(),
                                  opts.mode, opts.hierarchical, opts.binary,
                                  opts.filterSpeckle, opts.colorPrecision,
                                  opts.layerDifference, opts.cornerThreshold,
                                  opts.lengthThreshold, opts.maxIterations,
                                  opts.spliceThreshold, opts.pathPrecision,
                                  opts.maxPaths,
                                  &status, &pathCount,
                                  errBuf, int(sizeof(errBuf)));
    pathCountOut = pathCount;
    if (status == VTRACER_PATH_LIMIT) {
        return TraceStatus::PathLimit;
    }
    if (!svg || status != VTRACER_OK) {
        errOut = QString::fromLatin1(errBuf, int(std::strlen(errBuf)));
        if (errOut.isEmpty()) { errOut = QStringLiteral("trace failed"); }
        if (svg) { lib.mFree(svg); }
        return TraceStatus::Error;
    }
    svgOut = QString::fromUtf8(svg);
    lib.mFree(svg);
    return TraceStatus::Ok;
}

} // namespace VTracer
