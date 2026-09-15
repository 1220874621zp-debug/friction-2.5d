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

#include "lottieprovider.h"

#include <QLibrary>

#include <cstring>

using FrskLoadDataFn = void* (*)(const char*, size_t, char*, size_t);
using FrskLoadFileFn = void* (*)(const char*, const char*, char*, size_t);
using FrskAddRefFn = void (*)(void*);
using FrskReleaseFn = void (*)(void*);
using FrskDoubleFn = double (*)(const void*);
using FrskIntFn = int (*)(const void*);
using FrskRenderFn = SkImage* (*)(void*, double);
using FrskVersionFn = const char* (*)();

namespace {

// Loaded once on first use; a missing library must not take the app
// down with it, so this is a runtime QLibrary load instead of a
// link-time dependency.
struct LottieLibInstance {
    QLibrary mLib;
    FrskLoadDataFn mLoadData = nullptr;
    FrskLoadFileFn mLoadFile = nullptr;
    FrskAddRefFn mAddRef = nullptr;
    FrskReleaseFn mRelease = nullptr;
    FrskDoubleFn mDuration = nullptr;
    FrskDoubleFn mFps = nullptr;
    FrskIntFn mWidth = nullptr;
    FrskIntFn mHeight = nullptr;
    FrskRenderFn mRender = nullptr;
    FrskVersionFn mVersion = nullptr;
    bool mOk = false;

    LottieLibInstance() : mLib(QStringLiteral("frictionskottie")) {
        mOk = mLib.load();
        if (!mOk) { return; }
        mLoadData = reinterpret_cast<FrskLoadDataFn>(
                    mLib.resolve("frsk_load_data"));
        mLoadFile = reinterpret_cast<FrskLoadFileFn>(
                    mLib.resolve("frsk_load_file"));
        mAddRef = reinterpret_cast<FrskAddRefFn>(
                    mLib.resolve("frsk_add_ref"));
        mRelease = reinterpret_cast<FrskReleaseFn>(
                    mLib.resolve("frsk_release"));
        mDuration = reinterpret_cast<FrskDoubleFn>(
                    mLib.resolve("frsk_duration"));
        mFps = reinterpret_cast<FrskDoubleFn>(
                    mLib.resolve("frsk_fps"));
        mWidth = reinterpret_cast<FrskIntFn>(
                    mLib.resolve("frsk_width"));
        mHeight = reinterpret_cast<FrskIntFn>(
                    mLib.resolve("frsk_height"));
        mRender = reinterpret_cast<FrskRenderFn>(
                    mLib.resolve("frsk_render"));
        mVersion = reinterpret_cast<FrskVersionFn>(
                    mLib.resolve("frsk_version"));
        mOk = mLoadData && mLoadFile && mAddRef && mRelease &&
              mDuration && mFps && mWidth && mHeight &&
              mRender && mVersion;
    }
};

LottieLibInstance &lottieLib()
{
    static LottieLibInstance lib;
    return lib;
}

} // namespace

namespace LottieLib {

bool available()
{
    return lottieLib().mOk;
}

void* loadFromFile(const QString& path, const QString& baseDir,
                   QString& errOut)
{
    errOut.clear();
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mLoadFile) {
        errOut = QStringLiteral("frictionskottie library not available");
        return nullptr;
    }
    char errBuf[512];
    std::memset(errBuf, 0, sizeof(errBuf));
    void* const handle = lib.mLoadFile(path.toUtf8().constData(),
                                       baseDir.toUtf8().constData(),
                                       errBuf, sizeof(errBuf));
    if (!handle) {
        errOut = QString::fromUtf8(errBuf, int(std::strlen(errBuf)));
        if (errOut.isEmpty()) { errOut = QStringLiteral("lottie load failed"); }
    }
    return handle;
}

void* loadFromData(const QByteArray& json, QString& errOut)
{
    errOut.clear();
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mLoadData) {
        errOut = QStringLiteral("frictionskottie library not available");
        return nullptr;
    }
    char errBuf[512];
    std::memset(errBuf, 0, sizeof(errBuf));
    void* const handle = lib.mLoadData(json.constData(),
                                       size_t(json.size()),
                                       errBuf, sizeof(errBuf));
    if (!handle) {
        errOut = QString::fromUtf8(errBuf, int(std::strlen(errBuf)));
        if (errOut.isEmpty()) { errOut = QStringLiteral("lottie load failed"); }
    }
    return handle;
}

void addRef(void* handle)
{
    const auto& lib = lottieLib();
    if (lib.mOk && lib.mAddRef && handle) { lib.mAddRef(handle); }
}

void releaseHandle(void* handle)
{
    const auto& lib = lottieLib();
    if (lib.mOk && lib.mRelease && handle) { lib.mRelease(handle); }
}

double duration(void* handle)
{
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mDuration || !handle) { return 0; }
    return lib.mDuration(handle);
}

double fps(void* handle)
{
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mFps || !handle) { return 0; }
    return lib.mFps(handle);
}

int width(void* handle)
{
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mWidth || !handle) { return 0; }
    return lib.mWidth(handle);
}

int height(void* handle)
{
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mHeight || !handle) { return 0; }
    return lib.mHeight(handle);
}

sk_sp<SkImage> renderFrame(void* handle, const qreal tSec)
{
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mRender || !handle) { return nullptr; }
    // the returned pointer carries one reference for us
    return sk_sp<SkImage>(lib.mRender(handle, double(tSec)));
}

QString versionString()
{
    const auto& lib = lottieLib();
    if (!lib.mOk || !lib.mVersion) {
        return QStringLiteral("skottie (not loaded)");
    }
    const char* const v = lib.mVersion();
    return v ? QString::fromLatin1(v) : QStringLiteral("skottie (unknown)");
}

} // namespace LottieLib
