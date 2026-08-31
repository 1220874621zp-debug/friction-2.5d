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

#include "Depth/aidepthprovider.h"

#include <QDir>
#include <QLibrary>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QVector>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Depth/ort/onnxruntime/onnxruntime_c_api.h"

namespace {

using OrtGetApiBaseFn = const OrtApiBase* (*)(void);

// Loaded once on first use; a missing DLL must not take the app down
// with it, so this is a runtime QLibrary load instead of a link-time
// dependency (same rationale as vtracerprovider).
struct OrtLib {
    QLibrary mLib;
    const OrtApiBase* mBase = nullptr;
    const OrtApi* mApi = nullptr;
    bool mOk = false;

    OrtLib() : mLib(QStringLiteral("onnxruntime")) {
        if (!mLib.load()) { return; }
        const auto fn = reinterpret_cast<OrtGetApiBaseFn>(
                    mLib.resolve("OrtGetApiBase"));
        if (!fn) { return; }
        mBase = fn();
        if (!mBase) { return; }
        mApi = mBase->GetApi(ORT_API_VERSION);
        mOk = mApi != nullptr;
    }
};

OrtLib& ortLib()
{
    static OrtLib lib;
    return lib;
}

unsigned short f32toF16(const float f)
{
    unsigned int x;
    std::memcpy(&x, &f, 4);
    const unsigned int sign = (x >> 16) & 0x8000u;
    const int exp = int((x >> 23) & 0xffu) - 127 + 15;
    const unsigned int mant = x & 0x7fffffu;
    if (exp <= 0) { return static_cast<unsigned short>(sign); }
    if (exp >= 31) { return static_cast<unsigned short>(sign | 0x7c00u); }
    unsigned short h = static_cast<unsigned short>(
                sign | (static_cast<unsigned int>(exp) << 10) | (mant >> 13));
    if ((mant & 0x1fffu) >= 0x0fffu) { h += 1; }
    return h;
}

float f16toF32(const unsigned short h)
{
    const unsigned int sign = (h >> 15) & 1u;
    const unsigned int exp = (h >> 10) & 0x1fu;
    const unsigned int mant = h & 0x3ffu;
    if (exp == 0) { return (mant / 1024.0f) / 16384.0f * (sign ? -1.f : 1.f); }
    if (exp == 31) { return mant ? NAN : INFINITY; }
    const unsigned int bits = (sign << 31)
            | ((exp - 15 + 127) << 23) | (mant << 13);
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}

// classic JET colormap: 0 -> dark blue, 1 -> dark red
void jetColor(const float v, unsigned char& r, unsigned char& g,
              unsigned char& b)
{
    const auto clamp01 = [](const float x) {
        return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    };
    r = static_cast<unsigned char>(clamp01(1.5f - std::abs(4.f * v - 3.f)) * 255.f);
    g = static_cast<unsigned char>(clamp01(1.5f - std::abs(4.f * v - 2.f)) * 255.f);
    b = static_cast<unsigned char>(clamp01(1.5f - std::abs(4.f * v - 1.f)) * 255.f);
}

// Session cache: creating a session re-reads the (up to ~200 MB) weight
// file, so keep one session per model path alive for the app lifetime.
struct SessionCache {
    QMutex mMutex;
    QMap<QString, OrtSession*> mSessions;

    OrtSession* session(const QString& onnxPath, QString& errOut)
    {
        const auto& lib = ortLib();
        QMutexLocker lock(&mMutex);
        const auto it = mSessions.constFind(onnxPath);
        if (it != mSessions.constEnd()) { return it.value(); }

        const OrtApi* const api = lib.mApi;
        if (!api) { errOut = QStringLiteral("onnxruntime not loaded"); return nullptr; }
        static OrtEnv* env = nullptr; // created under mMutex
        if (!env) {
            if (api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                               "AiDepth", &env) != nullptr) {
                errOut = QStringLiteral("CreateEnv failed");
                return nullptr;
            }
        }
        OrtSessionOptions* so = nullptr;
        if (api->CreateSessionOptions(&so) != nullptr) {
            errOut = QStringLiteral("CreateSessionOptions failed");
            return nullptr;
        }
        const int threads = qBound(1, QThread::idealThreadCount(), 8);
        api->SetIntraOpNumThreads(so, threads);
        api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);

        OrtSession* sess = nullptr;
#ifdef Q_OS_WIN
        const std::wstring wpath = onnxPath.toStdWString();
        const OrtStatus* st = api->CreateSession(env, wpath.c_str(), so, &sess);
#else
        const QByteArray upath = onnxPath.toUtf8();
        const OrtStatus* st = api->CreateSession(env, upath.constData(), so, &sess);
#endif
        api->ReleaseSessionOptions(so);
        if (st != nullptr) {
            errOut = QString::fromLatin1(api->GetErrorMessage(st));
            api->ReleaseStatus(const_cast<OrtStatus*>(st));
            return nullptr;
        }
        mSessions.insert(onnxPath, sess);
        return sess;
    }
};

SessionCache& sessionCache()
{
    static SessionCache cache;
    return cache;
}

QString findGraphFile(const QString& modelDir)
{
    const QStringList entries = QDir(modelDir).entryList({QStringLiteral("*.onnx")});
    for (const auto& e : entries) {
        if (!e.endsWith(QStringLiteral(".onnx_data"))) { return e; }
    }
    return QString();
}

// draws src into a new raster surface of the given size (high quality
// filter == bicubic on this Skia)
sk_sp<SkImage> resampleImage(const sk_sp<SkImage>& src,
                             const int w, const int h)
{
    const auto info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType,
                                        kUnpremul_SkAlphaType);
    const auto surface = SkSurface::MakeRaster(info);
    if (!surface) { return nullptr; }
    SkPaint paint;
    paint.setFilterQuality(kHigh_SkFilterQuality);
    surface->getCanvas()->drawImageRect(
                src, SkRect::MakeWH(w, h), &paint);
    surface->getCanvas()->flush();
    return surface->makeImageSnapshot();
}

} // namespace

namespace AiDepth {

bool available()
{
    return ortLib().mOk;
}

QString versionString()
{
    const auto& lib = ortLib();
    if (!lib.mOk || !lib.mBase) {
        return QStringLiteral("onnxruntime (not loaded)");
    }
    return QString::fromLatin1(lib.mBase->GetVersionString());
}

DepthStatus runDepth(const sk_sp<SkImage>& srcImage,
                     const QString& modelDir,
                     const Options& opts,
                     sk_sp<SkImage>& depthOut,
                     QString& errOut)
{
    depthOut = nullptr;
    errOut.clear();

    const auto& lib = ortLib();
    if (!lib.mOk || !lib.mApi) {
        errOut = QStringLiteral("onnxruntime.dll not found or exports missing");
        return DepthStatus::NotAvailable;
    }
    const OrtApi* const api = lib.mApi;

    const QString graphName = findGraphFile(modelDir);
    if (graphName.isEmpty()) {
        errOut = QStringLiteral("no .onnx graph file in model dir");
        return DepthStatus::ModelMissing;
    }
    const QString graphPath = modelDir + QLatin1Char('/') + graphName;

    if (!srcImage) {
        errOut = QStringLiteral("null image");
        return DepthStatus::Error;
    }
    const int srcW = srcImage->width();
    const int srcH = srcImage->height();
    if (srcW <= 0 || srcH <= 0) {
        errOut = QStringLiteral("empty image");
        return DepthStatus::Error;
    }

    // ---- preprocess: bicubic resize keeping aspect, sides as
    // multiples of 14 with the longest side clamped to fInputSize
    const int target = qMax(14, opts.fInputSize - opts.fInputSize % 14);
    int tw, th;
    if (srcW >= srcH) {
        tw = target;
        th = qMax(14, qRound(static_cast<qreal>(srcH) * target / srcW / 14) * 14);
    } else {
        th = target;
        tw = qMax(14, qRound(static_cast<qreal>(srcW) * target / srcH / 14) * 14);
    }

    const auto resized = resampleImage(srcImage, tw, th);
    if (!resized) {
        errOut = QStringLiteral("resize failed");
        return DepthStatus::Error;
    }

    const auto raster = resized->makeRasterImage();
    if (!raster) {
        errOut = QStringLiteral("makeRasterImage failed");
        return DepthStatus::Error;
    }
    std::vector<unsigned char> rgba(static_cast<size_t>(tw) * th * 4);
    const auto info = SkImageInfo::Make(tw, th, kRGBA_8888_SkColorType,
                                        kUnpremul_SkAlphaType);
    if (!raster->readPixels(info, rgba.data(),
                            static_cast<size_t>(tw) * 4, 0, 0)) {
        errOut = QStringLiteral("pixel format conversion failed");
        return DepthStatus::Error;
    }

    // CHW float32, ImageNet normalization
    const size_t plane = static_cast<size_t>(tw) * th;
    std::vector<float> input(3 * plane);
    static const float mean[3] = {0.485f, 0.456f, 0.406f};
    static const float stdv[3] = {0.229f, 0.224f, 0.225f};
    for (int y = 0; y < th; y++) {
        const unsigned char* row = &rgba[static_cast<size_t>(y) * tw * 4];
        for (int x = 0; x < tw; x++) {
            const size_t i = static_cast<size_t>(y) * tw + x;
            for (int c = 0; c < 3; c++) {
                input[c * plane + i] =
                        (row[x * 4 + c] / 255.f - mean[c]) / stdv[c];
            }
        }
    }

    // ---- session + tensor io names
    OrtSession* sess = sessionCache().session(graphPath, errOut);
    if (!sess) {
        errOut = QStringLiteral("session: ") + errOut;
        return DepthStatus::Error;
    }

    OrtAllocator* alloc = nullptr;
    if (api->GetAllocatorWithDefaultOptions(&alloc) != nullptr) {
        errOut = QStringLiteral("GetAllocatorWithDefaultOptions failed");
        return DepthStatus::Error;
    }
    char* inNameC = nullptr;
    char* outNameC = nullptr;
    if (api->SessionGetInputName(sess, 0, alloc, &inNameC) != nullptr ||
        api->SessionGetOutputName(sess, 0, alloc, &outNameC) != nullptr) {
        errOut = QStringLiteral("cannot query tensor names");
        return DepthStatus::Error;
    }

    // feed fp16 only when the graph demands it (fp16 ONNX exports keep
    // a float32 input; verified against the onnx-community files)
    bool wantsFp16 = false;
    {
        OrtTypeInfo* ti = nullptr;
        const OrtTensorTypeAndShapeInfo* tsi = nullptr;
        ONNXTensorElementDataType ty = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        if (api->SessionGetInputTypeInfo(sess, 0, &ti) == nullptr &&
            api->CastTypeInfoToTensorInfo(ti, &tsi) == nullptr) {
            api->GetTensorElementType(tsi, &ty);
            api->ReleaseTypeInfo(ti);
        }
        wantsFp16 = ty == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
    }

    OrtMemoryInfo* mem = nullptr;
    if (api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem) != nullptr) {
        api->AllocatorFree(alloc, inNameC);
        api->AllocatorFree(alloc, outNameC);
        errOut = QStringLiteral("CreateCpuMemoryInfo failed");
        return DepthStatus::Error;
    }

    std::vector<unsigned short> input16;
    const int64_t inDims[4] = {1, 3, th, tw};
    OrtValue* inputVal = nullptr;
    if (wantsFp16) {
        input16.resize(input.size());
        for (size_t i = 0; i < input.size(); i++) { input16[i] = f32toF16(input[i]); }
        if (api->CreateTensorWithDataAsOrtValue(
                    mem, input16.data(), input16.size() * 2, inDims, 4,
                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16,
                    &inputVal) != nullptr) {
            errOut = QStringLiteral("CreateTensor (f16) failed");
            api->AllocatorFree(alloc, inNameC);
            api->AllocatorFree(alloc, outNameC);
            api->ReleaseMemoryInfo(mem);
            return DepthStatus::Error;
        }
    } else {
        if (api->CreateTensorWithDataAsOrtValue(
                    mem, input.data(), input.size() * 4, inDims, 4,
                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                    &inputVal) != nullptr) {
            errOut = QStringLiteral("CreateTensor (f32) failed");
            api->AllocatorFree(alloc, inNameC);
            api->AllocatorFree(alloc, outNameC);
            api->ReleaseMemoryInfo(mem);
            return DepthStatus::Error;
        }
    }

    const char* inNames[1] = {inNameC};
    const char* outNames[1] = {outNameC};
    OrtValue* outputVal = nullptr;
    const OrtStatus* runSt = api->Run(sess, nullptr, inNames,
                                      const_cast<const OrtValue* const*>(&inputVal),
                                      1, outNames, 1, &outputVal);
    api->ReleaseValue(inputVal);
    api->ReleaseMemoryInfo(mem);
    api->AllocatorFree(alloc, inNameC);
    api->AllocatorFree(alloc, outNameC);
    if (runSt != nullptr) {
        errOut = QString::fromLatin1(api->GetErrorMessage(runSt));
        api->ReleaseStatus(const_cast<OrtStatus*>(runSt));
        return DepthStatus::Error;
    }

    // ---- read output: (1,H,W) or (1,1,H,W), float32 or float16
    float* outData = nullptr;
    bool ownOutData = false; // only the fp16 conversion copy is ours to free
    size_t outH = 0;
    size_t outW = 0;
    {
        OrtTensorTypeAndShapeInfo* osi = nullptr;
        if (api->GetTensorTypeAndShape(outputVal, &osi) != nullptr) {
            api->ReleaseValue(outputVal);
            errOut = QStringLiteral("GetTensorTypeAndShape failed");
            return DepthStatus::Error;
        }
        size_t ndim = 0;
        api->GetDimensionsCount(osi, &ndim);
        std::vector<int64_t> odims(ndim);
        api->GetDimensions(osi, odims.data(), ndim);
        ONNXTensorElementDataType oty = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        api->GetTensorElementType(osi, &oty);
        api->ReleaseTensorTypeAndShapeInfo(osi);

        if (ndim < 2) {
            api->ReleaseValue(outputVal);
            errOut = QStringLiteral("unexpected output rank");
            return DepthStatus::Error;
        }
        outH = static_cast<size_t>(odims[ndim - 2]);
        outW = static_cast<size_t>(odims[ndim - 1]);

        if (oty == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            unsigned short* p = nullptr;
            api->GetTensorMutableData(outputVal,
                                      reinterpret_cast<void**>(&p));
            const size_t count = outH * outW;
            float* conv = static_cast<float*>(malloc(count * sizeof(float)));
            if (!conv) {
                api->ReleaseValue(outputVal);
                errOut = QStringLiteral("out of memory (f16 convert)");
                return DepthStatus::Error;
            }
            for (size_t i = 0; i < count; i++) { conv[i] = f16toF32(p[i]); }
            outData = conv;
            ownOutData = true;
        } else {
            api->GetTensorMutableData(outputVal,
                                      reinterpret_cast<void**>(&outData));
        }
        if (!outData) {
            api->ReleaseValue(outputVal);
            errOut = QStringLiteral("output tensor access failed");
            return DepthStatus::Error;
        }
    }

    // ---- postprocess: min-max normalize, colorize, scale to source
    const size_t count = outH * outW;
    float mn = outData[0];
    float mx = outData[0];
    for (size_t i = 1; i < count; i++) {
        if (outData[i] < mn) { mn = outData[i]; }
        if (outData[i] > mx) { mx = outData[i]; }
    }
    const float range = mx - mn;
    const float invRange = range > 1e-6f ? 1.f / range : 0.f;

    std::vector<unsigned char> depthRgba(count * 4);
    for (size_t i = 0; i < count; i++) {
        // larger disparity = closer to camera
        float v = (outData[i] - mn) * invRange;
        if (opts.fOutputMode == 1) { v = 1.f - v; } // inverted: far = bright
        unsigned char r, g, b;
        if (opts.fOutputMode == 2) {
            jetColor(v, r, g, b);
        } else {
            const auto u = static_cast<unsigned char>(v * 255.f + 0.5f);
            r = g = b = u;
        }
        depthRgba[i * 4] = r;
        depthRgba[i * 4 + 1] = g;
        depthRgba[i * 4 + 2] = b;
        depthRgba[i * 4 + 3] = 255;
    }
    if (ownOutData) { free(outData); }
    api->ReleaseValue(outputVal);

    const auto depthInfo = SkImageInfo::Make(
                static_cast<int>(outW), static_cast<int>(outH),
                kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    const auto depthSmall = SkImage::MakeRasterCopy(
                SkPixmap(depthInfo, depthRgba.data(),
                         static_cast<size_t>(outW) * 4));
    if (!depthSmall) {
        errOut = QStringLiteral("depth image build failed");
        return DepthStatus::Error;
    }

    depthOut = resampleImage(depthSmall, srcW, srcH);
    if (!depthOut) {
        errOut = QStringLiteral("depth upscale failed");
        return DepthStatus::Error;
    }
    return DepthStatus::Ok;
}

} // namespace AiDepth
