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

// Liquid glass (water-droplet) backdrop effect, ported from a
// Shadertoy shader https://gist.github.com/emmachase/25af1fb66daebf0f9989c93d3c8c5fa6
//
// The layer's own alpha (rasterized in device space, exactly where the
// layer is about to be drawn) is the glass shape. A chamfer distance
// field over that mask drives the refraction: pixels near the shape
// edge sample the backdrop from deeper inside (strong bending at the
// rim, identity at the very edge), like looking through a water
// droplet. The angular rim glow uses the field-gradient angle, the
// grain matches the original shader's screen-space noise, and the
// layer's own semi-transparent content composites on top as tint.

#include "liquidglasseffect.h"

#include "Boxes/boxrenderdata.h"
#include "skia/skqtconversions.h"

#include "appsupport.h"

#include <QRect>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

const float kM_E = 2.718281828459045f;

// Mirrors f_func() of the original shader (constants hardcoded there)
float f_func(const float x)
{
    const float u_a = 0.7f;
    const float u_b = 2.3f;
    const float u_c = 5.2f;
    const float u_d = 6.9f;
    return 1.f - u_b * std::pow(u_c * kM_E, -u_d * x - u_a);
}

// Mirrors rand() of the original shader
float rand2d(const float x, const float y)
{
    const float d = 12.9898f * x + 78.233f * y;
    float v = std::sin(d) * 43758.5453f;
    v = v - std::floor(v);
    return v;
}

float smoothstepf(const float t)
{
    const float c = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return c * c * (3.f - 2.f * c);
}

float sat1(const float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// chamfer 3-4 distance transform, inside-positive, in pixels: inside
// pixels start at infinity, outside at zero, two sweeps propagate the
// distance from every inside pixel to the nearest shape edge
void distanceField(const SkBitmap& mask, const int x0, const int y0,
                   const int w, const int h, std::vector<float>& field)
{
    const float kInf = 1e9f;
    field.assign(size_t(w) * h, kInf);
    for (int y = 0; y < h; y++) {
        const uchar* row = static_cast<const uchar*>(
                    mask.getAddr(x0, y0 + y));
        for (int x = 0; x < w; x++) {
            if (row[x * 4 + 3] <= 127) field[size_t(y) * w + x] = 0.f;
        }
    }
    // forward sweep (top-left -> bottom-right)
    for (int y = 0; y < h; y++) {
        float* row = field.data() + size_t(y) * w;
        for (int x = 0; x < w; x++) {
            float d = row[x];
            if (x > 0)     d = std::min(d, row[x - 1] + 3.f);
            if (y > 0) {
                const float* up = field.data() + size_t(y - 1) * w;
                d = std::min(d, up[x] + 3.f);
                if (x > 0)     d = std::min(d, up[x - 1] + 4.f);
                if (x < w - 1) d = std::min(d, up[x + 1] + 4.f);
            }
            row[x] = d;
        }
    }
    // backward sweep
    for (int y = h - 1; y >= 0; y--) {
        float* row = field.data() + size_t(y) * w;
        for (int x = w - 1; x >= 0; x--) {
            float d = row[x];
            if (x < w - 1) d = std::min(d, row[x + 1] + 3.f);
            if (y < h - 1) {
                const float* dn = field.data() + size_t(y + 1) * w;
                d = std::min(d, dn[x] + 3.f);
                if (x < w - 1) d = std::min(d, dn[x + 1] + 4.f);
                if (x > 0)     d = std::min(d, dn[x - 1] + 4.f);
            }
            row[x] = d;
        }
    }
    for (auto& v : field) v = v / 3.f; // chamfer units -> pixels
}

// bilinear premultiplied sample of a bitmap at pixel coordinates
void sampleBilinear(const SkBitmap& src, const int w, const int h,
                    float fx, float fy,
                    float& r, float& g, float& b, float& a)
{
    fx = std::min(std::max(fx, 0.f), float(w - 1));
    fy = std::min(std::max(fy, 0.f), float(h - 1));
    int x0 = int(std::floor(fx));
    int y0 = int(std::floor(fy));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    const float tx = fx - float(x0);
    const float ty = fy - float(y0);
    const uchar* p00 = static_cast<const uchar*>(src.getAddr(x0, y0));
    const uchar* p10 = static_cast<const uchar*>(src.getAddr(x1, y0));
    const uchar* p01 = static_cast<const uchar*>(src.getAddr(x0, y1));
    const uchar* p11 = static_cast<const uchar*>(src.getAddr(x1, y1));
    float out[4];
    const uchar* c[4] = {p00, p10, p01, p11};
    for (int i = 0; i < 4; i++) {
        const float top = c[0][i] / 255.f + (c[1][i] - c[0][i]) / 255.f * tx;
        const float bot = c[2][i] / 255.f + (c[3][i] - c[2][i]) / 255.f * tx;
        out[i] = top + (bot - top) * ty;
    }
    r = out[0]; g = out[1]; b = out[2]; a = out[3];
}

} // namespace

LiquidGlassEffect::LiquidGlassEffect() :
    RasterEffect("liquid-glass",
                 AppSupport::getRasterEffectHardwareSupport("LiquidGlass",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::LIQUID_GLASS)
{
    mRefraction = enve::make_shared<QrealAnimator>(3, 0, 6, 0.05,
                                                   QObject::tr("refraction"));
    ca_addChild(mRefraction);
    mNoise = enve::make_shared<QrealAnimator>(0.06, 0, 0.2, 0.005,
                                              QObject::tr("noise"));
    ca_addChild(mNoise);
    mGlowWeight = enve::make_shared<QrealAnimator>(0.35, 0, 1, 0.01,
                                                   QObject::tr("glow weight"));
    ca_addChild(mGlowWeight);
    mGlowBias = enve::make_shared<QrealAnimator>(0, -1, 1, 0.01,
                                                 QObject::tr("glow bias"));
    ca_addChild(mGlowBias);
}

class LiquidGlassEffectCaller : public RasterEffectCaller {
public:
    LiquidGlassEffectCaller(const HardwareSupport hwSupport,
                            const LiquidGlassEffectData& data) :
        RasterEffectCaller(hwSupport),
        mData(data) {
        // the glass refracts the composition BELOW the layer: diverted
        // to the composite-time backdrop treatment, never the box-image
        // phase
        fSamplesBackdrop = true;
    }

    void processBackdrop(SkCanvas * const canvas,
                         const BoxRenderData& box,
                         const SkPaint& paint) {
        Q_UNUSED(paint)
        if(!canvas || !box.fRenderedImage) return;
        const SkIRect dev = canvas->getDeviceClipBounds();
        if(dev.isEmpty()) return;
        auto* surf = canvas->getSurface();
        if(!surf) return;

        canvas->flush();
        const auto snap = surf->makeImageSnapshot(dev);
        if(!snap) return;

        // 1) the layer's shape: rasterize its own image into a
        //    device-space mask, replicating the standard placement
        //    (this covers both the render and preview paths since both
        //    end up in drawOnParentLayer with the full transform
        //    already on the canvas)
        const auto info = SkImageInfo::MakeN32Premul(dev.width(),
                                                     dev.height());
        SkBitmap maskB;
        maskB.allocPixels(info);
        maskB.eraseColor(SK_ColorTRANSPARENT);
        {
            SkCanvas mc(maskB);
            SkMatrix m = canvas->getTotalMatrix();
            m.postTranslate(-float(dev.left()), -float(dev.top()));
            mc.setMatrix(m);
            if(box.fUseRenderTransform) {
                mc.concat(toSkMatrix(box.fRenderTransform));
            }
            SkPaint mp;
            mp.setAntiAlias(box.fAntiAlias);
            mc.drawImage(box.fRenderedImage,
                         box.fGlobalRect.x(), box.fGlobalRect.y(), &mp);
        }

        // shape bounding box (device space, relative to dev origin)
        int bxMin = dev.width(), bxMax = -1, byMin = dev.height(), byMax = -1;
        for (int y = 0; y < maskB.height(); y++) {
            const uchar* row = static_cast<const uchar*>(
                        maskB.getAddr(0, y));
            for (int x = 0; x < maskB.width(); x++) {
                if (row[x * 4 + 3] > 0) {
                    if (x < bxMin) bxMin = x;
                    if (x > bxMax) bxMax = x;
                    if (y < byMin) byMin = y;
                    if (y > byMax) byMax = y;
                }
            }
        }
        if (bxMax < bxMin) return; // empty shape

        // how much of the bbox actually is shape (diagnostic aid)
        int insideCount = 0;
        for (int y = 0; y < maskB.height(); y++) {
            const uchar* row = static_cast<const uchar*>(
                        maskB.getAddr(0, y));
            for (int x = 0; x < maskB.width(); x++) {
                if (row[x * 4 + 3] > 127) insideCount++;
            }
        }

        // 2) distance field inside the shape
        const int bw = bxMax - bxMin + 1;
        const int bh = byMax - byMin + 1;
        std::vector<float> field;
        distanceField(maskB, bxMin, byMin, bw, bh, field);
        float maxDist = 0.f;
        for (const float v : field) maxDist = std::max(maxDist, v);
        if (maxDist < 0.5f) maxDist = 0.5f;

        // 3) backdrop snapshot as a raster bitmap
        SkBitmap backB;
        backB.allocPixels(info);
        backB.eraseColor(SK_ColorTRANSPARENT);
        if (!snap->readPixels(backB.pixmap(), 0, 0)) return;

        // throttled diagnostic for the runtime debug log
        static int sLog = 0;
        if (sLog++ < 8) {
            qWarning() << "[LG] backdrop dev=" << dev.width() << "x"
                       << dev.height() << "shape=" << bw << "x" << bh
                       << "maxDist=" << int(maxDist)
                       << "inside%=" << int(100.f * insideCount /
                                            float(bw * bh))
                       << "rect=" << box.fGlobalRect
                       << "useT=" << box.fUseRenderTransform
                       << "refr=" << mData.mRefraction;
        }

        // 4) droplet shading inside the shape
        SkBitmap dstB;
        dstB.allocPixels(info);
        dstB.eraseColor(SK_ColorTRANSPARENT);
        const float rimW = 0.06f * maxDist; // as in the original
        for (int y = byMin; y <= byMax; y++) {
            const int fy = y - byMin;
            const uchar* mrow = static_cast<const uchar*>(
                        maskB.getAddr(bxMin, y));
            uchar* drow = static_cast<uchar*>(dstB.getAddr(bxMin, y));
            for (int x = bxMin; x <= bxMax; x++, mrow += 4, drow += 4) {
                const float alpha = mrow[3] / 255.f;
                if (alpha <= 0.001f) continue;
                const int fx = x - bxMin;
                const float dist = field[size_t(fy) * bw + fx];
                if (dist <= 0.f) {
                    // anti-aliased fringe outside the hard mask: leave
                    // the backdrop untouched there
                    continue;
                }
                // inward normal from the field gradient
                const float dl = field[size_t(fy) * bw + std::max(fx - 1, 0)];
                const float dr = field[size_t(fy) * bw + std::min(fx + 1, bw - 1)];
                const float du = field[size_t(std::max(fy - 1, 0)) * bw + fx];
                const float ddn = field[size_t(std::min(fy + 1, bh - 1)) * bw + fx];
                float nx = dr - dl;
                float ny = ddn - du;
                const float nlen = std::sqrt(nx * nx + ny * ny);
                if (nlen > 0.001f) { nx /= nlen; ny /= nlen; }
                else { nx = 0.f; ny = 0.f; }

                // refraction: sample deeper inside, zero at the edge,
                // curve follows the original f_func over dist/maxDist
                const float fval = f_func(dist / maxDist);
                const float disp = dist * (1.f - std::pow(fval,
                                                          mData.mRefraction));
                const float sx = float(x) + nx * disp;
                const float sy = float(y) + ny * disp;

                float r, g, b, a;
                sampleBilinear(backB, dev.width(), dev.height(),
                               sx, sy, r, g, b, a);

                // angular rim glow using the normal angle (the original
                // used the angle around the shape center; silhouette
                // normals rotate around the outline the same way)
                const float glowFactor = std::sin(std::atan2(ny, nx) - 0.5f) *
                        mData.mGlowWeight *
                        smoothstepf(dist / std::max(rimW, 0.001f)) +
                        1.f + mData.mGlowBias;
                const float noise = (rand2d(float(x + dev.left()) * 1e-3f,
                                            float(y + dev.top()) * 1e-3f) - 0.5f) *
                        mData.mNoise;

                float rr = r * glowFactor + noise;
                float gg = g * glowFactor + noise;
                float bb2 = b * glowFactor + noise;
                // keep the premultiplied invariant against the aa alpha
                drow[0] = static_cast<uchar>(
                            sat1(std::min(std::max(rr, 0.f), alpha)) * 255.f + 0.5f);
                drow[1] = static_cast<uchar>(
                            sat1(std::min(std::max(gg, 0.f), alpha)) * 255.f + 0.5f);
                drow[2] = static_cast<uchar>(
                            sat1(std::min(std::max(bb2, 0.f), alpha)) * 255.f + 0.5f);
                drow[3] = mrow[3];
            }
        }

        // 5) the glass REPLACES the layer pixels: draw it over the
        // backdrop with the layer's own opacity (device space)
        if (isZero4Dec(box.fOpacity)) return;
        canvas->save();
        canvas->resetMatrix(); // the snapshot is in device space
        SkPaint dp;
        dp.setAlpha(static_cast<U8CPU>(qRound(box.fOpacity * 2.55)));
        canvas->drawImage(SkImage::MakeFromBitmap(dstB), 0, 0, &dp);
        canvas->restore();
    }
private:
    const LiquidGlassEffectData mData;
};

stdsptr<RasterEffectCaller> LiquidGlassEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    LiquidGlassEffectData effData;
    effData.mRefraction = mRefraction->getEffectiveValue(relFrame);
    effData.mNoise = mNoise->getEffectiveValue(relFrame);
    effData.mGlowWeight = mGlowWeight->getEffectiveValue(relFrame);
    effData.mGlowBias = mGlowBias->getEffectiveValue(relFrame);

    return enve::make_shared<LiquidGlassEffectCaller>(
                instanceHwSupport(), effData);
}
