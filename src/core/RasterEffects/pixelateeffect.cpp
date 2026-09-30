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

// Faithful port of the user's Pixelate After Effects plugin
// (AE SDK Examples/Effect/Pixelate). The pipeline mirrors the AE
// source function-for-function: box-average downsample > saturation
// boost > median-cut palette (capped 10000 samples) > banding noise +
// 8x8 Bayer dither + palette quantize > Sobel edge sharpen > nearest
// upscale with honeycomb/scanline overlay > chromatic aberration.
// AE layer pixels are straight (unpremultiplied); the Skia bitmaps
// here are premultiplied, so the downsample reads unpremultiply and
// the final write premultiplies. The pipeline needs the whole image
// (palette, sharpen, aberration neighbors): single thread, and only
// the fTexTile region is written back to dst.

#include "pixelateeffect.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

template<typename T>
inline T ClampVal(T val, T lo, T hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}

struct PixF {
    float r, g, b, a;
};

inline float PixLuminance(const PixF& p) {
    return p.r * 0.299f + p.g * 0.587f + p.b * 0.114f;
}

inline float DeterministicNoise(int x, int y, float seed) {
    const float val = sinf(static_cast<float>(x) * 12.9898f +
                           static_cast<float>(y) * 78.233f +
                           seed * 3.14159f) * 43758.5453f;
    return val - floorf(val);
}

inline float LabDistance(const PixF& c1, const PixF& c2) {
    const auto toLinear = [](float c) -> float {
        return (c > 0.04045f) ? powf((c + 0.055f) / 1.055f, 2.4f) : c / 12.92f;
    };
    const float rl1 = toLinear(c1.r), gl1 = toLinear(c1.g), bl1 = toLinear(c1.b);
    const float rl2 = toLinear(c2.r), gl2 = toLinear(c2.g), bl2 = toLinear(c2.b);
    const float dr = (rl1 - rl2) * 0.30f;
    const float dg = (gl1 - gl2) * 0.59f;
    const float db = (bl1 - bl2) * 0.11f;
    return sqrtf(dr * dr + dg * dg + db * db);
}

inline void BoostSaturation(PixF& p, float intensity) {
    if (intensity <= 0.0f) return;

    const float maxC = std::max({ p.r, p.g, p.b });
    const float minC = std::min({ p.r, p.g, p.b });
    const float delta = maxC - minC;

    if (delta > 0.001f && maxC > 0.001f) {
        const float s = delta / maxC;
        const float boost = intensity * (1.0f + (1.0f - s) * 2.5f);
        const float newS = std::min(s * (1.0f + boost), 1.0f);
        const float factor = (maxC * newS) / delta;

        p.r = ClampVal(maxC - (maxC - p.r) * factor, 0.0f, 1.0f);
        p.g = ClampVal(maxC - (maxC - p.g) * factor, 0.0f, 1.0f);
        p.b = ClampVal(maxC - (maxC - p.b) * factor, 0.0f, 1.0f);
    }

    const float contrast = intensity * 0.25f;
    p.r = ClampVal((p.r - 0.5f) * (1.0f + contrast) + 0.5f, 0.0f, 1.0f);
    p.g = ClampVal((p.g - 0.5f) * (1.0f + contrast) + 0.5f, 0.0f, 1.0f);
    p.b = ClampVal((p.b - 0.5f) * (1.0f + contrast) + 0.5f, 0.0f, 1.0f);
}

inline float HoneycombPattern(int x, int y, float scale) {
    if (scale <= 0.5f) return 0.0f;
    const float sx = static_cast<float>(x) / scale;
    const float sy = static_cast<float>(y) / scale;

    const float root3 = 1.73205080757f;
    const float q = sx * 2.0f / 3.0f;
    const float r = -sx / 3.0f + (root3 / 3.0f) * sy;
    const float s = -q - r;

    const float rq = floorf(q + 0.5f);
    const float rr = floorf(r + 0.5f);
    const float rs = floorf(s + 0.5f);

    const float dq = fabsf(q - rq);
    const float dr = fabsf(r - rr);
    const float ds = fabsf(s - rs);

    const float dist = std::max({ dq, dr, ds });
    const float edgeWidth = 0.10f;
    const float fade = 0.03f;

    if (dist > 0.5f - edgeWidth - fade) {
        const float t = (dist - (0.5f - edgeWidth - fade)) / fade;
        return ClampVal(t, 0.0f, 1.0f);
    }
    return 0.0f;
}

inline float Scanline(int y, float intensity) {
    if (intensity <= 0.0f) return 1.0f;
    const int cycle = y % 4;
    if (cycle == 0 || cycle == 1) {
        return 1.0f - intensity * 0.20f;
    }
    return 1.0f;
}

inline void ApplyChromaticAberration(PixF* pixels, int w, int h, float amount) {
    if (amount <= 0.001f) return;
    std::vector<PixF> temp(pixels, pixels + w * h);
    const int shift = std::max(1, static_cast<int>(amount * 4.0f));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = y * w + x;
            const int rx = ClampVal(x - shift, 0, w - 1);
            const int bx = ClampVal(x + shift, 0, w - 1);
            pixels[idx].r = temp[y * w + rx].r;
            pixels[idx].b = temp[y * w + bx].b;
        }
    }
}

class MedianCutPalette {
public:
    std::vector<PixF> colors;

    void Generate(const std::vector<PixF>& pixels, int numColors) {
        if (numColors < 2) numColors = 2;
        if (numColors > 256) numColors = 256;

        std::vector<std::vector<PixF>> boxes;
        boxes.push_back(pixels);

        while (static_cast<int>(boxes.size()) < numColors) {
            size_t splitIdx = 0;
            float maxRange = 0;
            int splitChannel = 0;

            for (size_t i = 0; i < boxes.size(); ++i) {
                auto& box = boxes[i];
                if (box.size() < 2) continue;

                float minR = 1, maxR = 0, minG = 1, maxG = 0, minB = 1, maxB = 0;
                for (auto& p : box) {
                    minR = std::min(minR, p.r); maxR = std::max(maxR, p.r);
                    minG = std::min(minG, p.g); maxG = std::max(maxG, p.g);
                    minB = std::min(minB, p.b); maxB = std::max(maxB, p.b);
                }

                const float rangeR = maxR - minR;
                const float rangeG = maxG - minG;
                const float rangeB = maxB - minB;
                const float localMax = std::max({ rangeR, rangeG, rangeB });

                if (localMax > maxRange) {
                    maxRange = localMax;
                    splitIdx = i;
                    if (rangeR >= rangeG && rangeR >= rangeB) splitChannel = 0;
                    else if (rangeG >= rangeR && rangeG >= rangeB) splitChannel = 1;
                    else splitChannel = 2;
                }
            }

            if (maxRange < 0.001f) break;

            auto& box = boxes[splitIdx];
            std::sort(box.begin(), box.end(), [splitChannel](const PixF& a, const PixF& b) {
                if (splitChannel == 0) return a.r < b.r;
                if (splitChannel == 1) return a.g < b.g;
                return a.b < b.b;
            });

            const size_t mid = box.size() / 2;
            std::vector<PixF> splitA(box.begin(), box.begin() + mid);
            std::vector<PixF> splitB(box.begin() + mid, box.end());

            boxes.erase(boxes.begin() + splitIdx);
            boxes.push_back(std::move(splitA));
            boxes.push_back(std::move(splitB));
        }

        colors.clear();
        for (auto& box : boxes) {
            if (box.empty()) continue;
            PixF avg = { 0, 0, 0, 0 };
            for (auto& p : box) {
                avg.r += p.r; avg.g += p.g; avg.b += p.b; avg.a += p.a;
            }
            const float inv = 1.0f / static_cast<float>(box.size());
            avg.r *= inv; avg.g *= inv; avg.b *= inv; avg.a *= inv;
            colors.push_back(avg);
        }
    }

    int FindNearest(const PixF& target) const {
        float minDist = 1e10f;
        int idx = 0;
        for (size_t i = 0; i < colors.size(); ++i) {
            const float d = LabDistance(target, colors[i]);
            if (d < minDist) {
                minDist = d;
                idx = static_cast<int>(i);
            }
        }
        return idx;
    }
};

class BayerDither {
    static const int SZ = 8;
    float matrix[SZ][SZ];

public:
    BayerDither() {
        static const int bayer8[SZ][SZ] = {
            {  0, 32,  8, 40,  2, 34, 10, 42 },
            { 48, 16, 56, 24, 50, 18, 58, 26 },
            { 12, 44,  4, 36, 14, 46,  6, 38 },
            { 60, 28, 52, 20, 62, 30, 54, 22 },
            {  3, 35, 11, 43,  1, 33,  9, 41 },
            { 51, 19, 59, 27, 49, 17, 57, 25 },
            { 15, 47,  7, 39, 13, 45,  5, 37 },
            { 63, 31, 55, 23, 61, 29, 53, 21 }
        };
        const float maxVal = static_cast<float>(SZ * SZ);
        for (int y = 0; y < SZ; ++y)
            for (int x = 0; x < SZ; ++x)
                matrix[y][x] = (static_cast<float>(bayer8[y][x]) / maxVal) - 0.5f;
    }

    void Apply(PixF& pixel, int x, int y, float intensity) const {
        const float threshold = matrix[y % SZ][x % SZ] * intensity;
        pixel.r = ClampVal(pixel.r + threshold, 0.0f, 1.0f);
        pixel.g = ClampVal(pixel.g + threshold, 0.0f, 1.0f);
        pixel.b = ClampVal(pixel.b + threshold, 0.0f, 1.0f);
    }
};

inline void EdgeSharpenApply(PixF* pixels, int width, int height, float strength) {
    if (strength <= 0.0f || width < 3 || height < 3) return;

    std::vector<PixF> temp(width * height);
    std::memcpy(temp.data(), pixels, width * height * sizeof(PixF));

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const int idx = y * width + x;

            const float gx =
                -1.0f * PixLuminance(temp[(y - 1) * width + (x - 1)]) +
                -2.0f * PixLuminance(temp[(y) * width + (x - 1)]) +
                -1.0f * PixLuminance(temp[(y + 1) * width + (x - 1)]) +
                 1.0f * PixLuminance(temp[(y - 1) * width + (x + 1)]) +
                 2.0f * PixLuminance(temp[(y) * width + (x + 1)]) +
                 1.0f * PixLuminance(temp[(y + 1) * width + (x + 1)]);

            const float gy =
                -1.0f * PixLuminance(temp[(y - 1) * width + (x - 1)]) +
                -2.0f * PixLuminance(temp[(y - 1) * width + (x)]) +
                -1.0f * PixLuminance(temp[(y - 1) * width + (x + 1)]) +
                 1.0f * PixLuminance(temp[(y + 1) * width + (x - 1)]) +
                 2.0f * PixLuminance(temp[(y + 1) * width + (x)]) +
                 1.0f * PixLuminance(temp[(y + 1) * width + (x + 1)]);

            const float edge = sqrtf(gx * gx + gy * gy);
            const float factor = 1.0f + edge * strength * 2.0f;

            const PixF center = temp[idx];
            const PixF avg = {
                (temp[idx - 1].r + temp[idx + 1].r + temp[idx - width].r + temp[idx + width].r) * 0.25f,
                (temp[idx - 1].g + temp[idx + 1].g + temp[idx - width].g + temp[idx + width].g) * 0.25f,
                (temp[idx - 1].b + temp[idx + 1].b + temp[idx - width].b + temp[idx + width].b) * 0.25f,
                center.a
            };

            pixels[idx].r = ClampVal(center.r + (center.r - avg.r) * factor * strength, 0.0f, 1.0f);
            pixels[idx].g = ClampVal(center.g + (center.g - avg.g) * factor * strength, 0.0f, 1.0f);
            pixels[idx].b = ClampVal(center.b + (center.b - avg.b) * factor * strength, 0.0f, 1.0f);
        }
    }
}

} // namespace

PixelateEffect::PixelateEffect() :
    RasterEffect(QObject::tr("Pixelate"),
                 AppSupport::getRasterEffectHardwareSupport("Pixelate",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::PIXELATE)
{
    mPixelSize = enve::make_shared<QrealAnimator>(12.0, 1.0, 64.0, 1.0, QStringLiteral("像素大小"));
    ca_addChild(mPixelSize);

    mPaletteSize = enve::make_shared<QrealAnimator>(32.0, 2.0, 256.0, 1.0, QStringLiteral("调色板大小"));
    ca_addChild(mPaletteSize);

    mDither = enve::make_shared<QrealAnimator>(7.0, 0.0, 100.0, 1.0, QStringLiteral("抖动强度"));
    ca_addChild(mDither);

    mEdgeSharpness = enve::make_shared<QrealAnimator>(41.0, 0.0, 100.0, 1.0, QStringLiteral("边缘锐化"));
    ca_addChild(mEdgeSharpness);

    mColorBanding = enve::make_shared<QrealAnimator>(4.0, 0.0, 100.0, 1.0, QStringLiteral("色带"));
    ca_addChild(mColorBanding);

    mSaturation = enve::make_shared<QrealAnimator>(16.0, 0.0, 200.0, 1.0, QStringLiteral("饱和度增强"));
    ca_addChild(mSaturation);

    mHoneycomb = enve::make_shared<QrealAnimator>(3.0, 0.0, 100.0, 1.0, QStringLiteral("蜂窝强度"));
    ca_addChild(mHoneycomb);

    mScanline = enve::make_shared<QrealAnimator>(27.0, 0.0, 100.0, 1.0, QStringLiteral("扫描线强度"));
    ca_addChild(mScanline);

    mChromatic = enve::make_shared<QrealAnimator>(67.0, 0.0, 100.0, 1.0, QStringLiteral("色差"));
    ca_addChild(mChromatic);
}

namespace {

struct PixelateData {
    int mPixelSize = 12;
    int mPaletteSize = 32;
    float mDither = 0.07f;
    float mEdgeSharpness = 0.41f;
    float mColorBanding = 0.04f;
    float mSaturation = 0.16f;
    float mHoneycomb = 0.03f;
    float mScanline = 0.27f;
    float mChromatic = 0.67f;
};

} // namespace

class PixelateEffectCaller : public RasterEffectCaller {
public:
    PixelateEffectCaller(const HardwareSupport hwSupport,
                         const PixelateData& data) :
        RasterEffectCaller(hwSupport), mData(data) {}

    // The AE pipeline (whole-image palette, sharpen, aberration) is an
    // order-dependent whole-image computation: force a single thread.
    int cpuThreads(const int available, const int area) const override {
        Q_UNUSED(available)
        Q_UNUSED(area)
        return 1;
    }

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
private:
    const PixelateData mData;
};

stdsptr<RasterEffectCaller> PixelateEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(data)

    PixelateData effData;

    // AE slider units, clamped to the AE valid ranges; percent rows
    // are normalized to 0..1 (saturation 0..2) like SmartRender.
    const qreal rawSize = mPixelSize->getEffectiveValue(relFrame);
    const qreal sceneSize = 1.0 + (rawSize - 1.0) * influence;
    // AE pixel size is in comp pixels; the bitmap is rendered at
    // `resolution` scale, so scale the block to keep the preview
    // identical to the final full-resolution render.
    effData.mPixelSize = qBound(1, qRound(sceneSize * resolution), 64);
    effData.mPaletteSize = qBound(2, qRound(mPaletteSize->getEffectiveValue(relFrame)), 256);
    effData.mDither = mDither->getEffectiveValue(relFrame) / 100.0;
    effData.mEdgeSharpness = mEdgeSharpness->getEffectiveValue(relFrame) / 100.0;
    effData.mColorBanding = mColorBanding->getEffectiveValue(relFrame) / 100.0;
    effData.mSaturation = mSaturation->getEffectiveValue(relFrame) / 100.0;
    effData.mHoneycomb = mHoneycomb->getEffectiveValue(relFrame) / 100.0;
    effData.mScanline = mScanline->getEffectiveValue(relFrame) / 100.0;
    effData.mChromatic = mChromatic->getEffectiveValue(relFrame) / 100.0;

    return enve::make_shared<PixelateEffectCaller>(
                instanceHwSupport(), effData);
}

void PixelateEffectCaller::processCpu(CpuRenderTools& renderTools,
                                      const CpuRenderData& data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;

    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }

    const int srcW = srcBtmp.width();
    const int srcH = srcBtmp.height();
    if (srcW <= 0 || srcH <= 0) return;

    const int outW = srcW;
    const int outH = srcH;
    const int pixelSize = mData.mPixelSize;

    int downW = srcW / pixelSize;
    int downH = srcH / pixelSize;
    if (downW < 1) downW = 1;
    if (downH < 1) downH = 1;

    // Stage 1: box-average downsample, AE RenderPixelArt downsample
    // loop; reads premul and converts to straight like AE layer pixels.
    std::vector<PixF> downsampled(size_t(downW) * downH);

    for (int y = 0; y < downH; ++y) {
        for (int x = 0; x < downW; ++x) {
            const int srcX = x * pixelSize;
            const int srcY = y * pixelSize;

            PixF sum = { 0, 0, 0, 0 };
            int samples = 0;
            const int maxSY = std::min(pixelSize, srcH - srcY);
            const int maxSX = std::min(pixelSize, srcW - srcX);

            for (int sy = 0; sy < maxSY; ++sy) {
                const uchar* row = static_cast<const uchar*>(
                            srcBtmp.getAddr(0, srcY + sy));
                for (int sx = 0; sx < maxSX; ++sx) {
                    const uchar* p = row + (srcX + sx) * 4;
                    const float a = p[3] / 255.0f;
                    if (a > 0.0f) {
                        sum.r += (p[0] / 255.0f) / a;
                        sum.g += (p[1] / 255.0f) / a;
                        sum.b += (p[2] / 255.0f) / a;
                    }
                    sum.a += a;
                    samples++;
                }
            }

            if (samples > 0) {
                const float inv = 1.0f / static_cast<float>(samples);
                auto& d = downsampled[size_t(y) * downW + x];
                d.r = sum.r * inv;
                d.g = sum.g * inv;
                d.b = sum.b * inv;
                d.a = sum.a * inv;
            }
        }
    }

    if (mData.mSaturation > 0.0f) {
        for (auto& p : downsampled) {
            BoostSaturation(p, mData.mSaturation);
        }
    }

    // Stage 3: median-cut palette from up to 10000 sampled blocks.
    std::vector<PixF> paletteSamples;
    const int totalDown = downW * downH;
    int sampleStep = 1;
    if (totalDown > 10000) {
        sampleStep = totalDown / 10000;
        if (sampleStep < 1) sampleStep = 1;
    }
    paletteSamples.reserve(size_t(totalDown / sampleStep + 1));
    for (int i = 0; i < totalDown; i += sampleStep) {
        paletteSamples.push_back(downsampled[size_t(i)]);
    }

    MedianCutPalette palette;
    palette.Generate(paletteSamples, mData.mPaletteSize);

    // Stage 4: banding noise + Bayer dither + palette quantize.
    BayerDither bayer;

    for (int y = 0; y < downH; ++y) {
        for (int x = 0; x < downW; ++x) {
            const int idx = y * downW + x;
            PixF p = downsampled[size_t(idx)];

            if (mData.mColorBanding > 0.0f) {
                const float noise = (DeterministicNoise(x, y, 0.0f) - 0.5f)
                        * mData.mColorBanding * 0.025f;
                p.r = ClampVal(p.r + noise, 0.0f, 1.0f);
                p.g = ClampVal(p.g + noise, 0.0f, 1.0f);
                p.b = ClampVal(p.b + noise, 0.0f, 1.0f);
            }

            if (mData.mDither > 0.0f) {
                bayer.Apply(p, x, y, mData.mDither * 0.5f);
            }

            const int nearest = palette.FindNearest(p);
            downsampled[size_t(idx)] = palette.colors[size_t(nearest)];
            downsampled[size_t(idx)].a = p.a;
        }
    }

    if (mData.mEdgeSharpness > 0.0f) {
        EdgeSharpenApply(downsampled.data(), downW, downH, mData.mEdgeSharpness);
    }

    // Stage 5: nearest upscale + honeycomb + scanline overlay.
    std::vector<PixF> colorLayer(size_t(outW) * outH);

    for (int y = 0; y < outH; ++y) {
        int srcY = (y * downH) / outH;
        srcY = ClampVal(srcY, 0, downH - 1);

        for (int x = 0; x < outW; ++x) {
            int srcX = (x * downW) / outW;
            srcX = ClampVal(srcX, 0, downW - 1);

            PixF col = downsampled[size_t(srcY) * downW + srcX];

            if (mData.mHoneycomb > 0.0f) {
                const float honey = HoneycombPattern(x, y,
                        static_cast<float>(pixelSize));
                if (honey > 0.0f) {
                    const float dark = honey * mData.mHoneycomb * 0.7f;
                    col.r *= (1.0f - dark);
                    col.g *= (1.0f - dark);
                    col.b *= (1.0f - dark);
                }
            }

            if (mData.mScanline > 0.0f) {
                const float scan = Scanline(y, mData.mScanline);
                col.r *= scan;
                col.g *= scan;
                col.b *= scan;
            }

            colorLayer[size_t(y) * outW + x] = col;
        }
    }

    if (mData.mChromatic > 0.001f) {
        ApplyChromaticAberration(colorLayer.data(), outW, outH,
                                 mData.mChromatic);
    }

    // Final write, AE RenderPixelArt tail: straight -> premul for the
    // Skia pipeline, truncating casts like the AE 8-bit path.
    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min(data.fTexTile.right(), outW - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min(data.fTexTile.bottom(), outH - 1);
    const int tileL = data.fTexTile.left();
    const int tileT = data.fTexTile.top();

    for (int yi = yMin; yi <= yMax; ++yi) {
        auto outPixel = static_cast<uchar*>(
                    dstBtmp.getAddr(xMin - tileL, yi - tileT));
        for (int xi = xMin; xi <= xMax; ++xi) {
            const PixF& pf = colorLayer[size_t(yi) * outW + xi];
            const float a = ClampVal(pf.a, 0.0f, 1.0f);
            outPixel[3] = static_cast<uchar>(a * 255.0f);
            outPixel[0] = static_cast<uchar>(ClampVal(pf.r, 0.0f, 1.0f) * a * 255.0f);
            outPixel[1] = static_cast<uchar>(ClampVal(pf.g, 0.0f, 1.0f) * a * 255.0f);
            outPixel[2] = static_cast<uchar>(ClampVal(pf.b, 0.0f, 1.0f) * a * 255.0f);
            outPixel += 4;
        }
    }
}
