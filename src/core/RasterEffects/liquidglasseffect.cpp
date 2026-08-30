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

// Liquid glass backdrop effect, ported from the liquid-glass fragment
// shader of BatchRenderer2D.glsl (Shadertoy adaptation).
//
// The LAYER ITSELF is the glass: its rasterized alpha (device space,
// exactly where the layer is about to be drawn) is the glass
// footprint. Inside the footprint every pixel is shaded with the
// reference shader's radial remap: the sample position is the pixel's
// vector from the shape center scaled by f_func(depth)^refraction,
// where depth is the normalized distance-field distance from the
// shape edge. At the rim the factor drops to ~0.26, so rim pixels show
// content pulled from deep inside (the strong magnifying edge of the
// reference); it saturates to 1.0 within ~20% depth (flat center).
// An angular rim glow and per-pixel grain are added as in the
// reference. The glass REPLACES the layer pixels: the layer's own
// content does not draw, its alpha only shapes the glass.
//
// An AE-style "background layer" picker (a dropdown of the scene's
// layers, tracked by a BoxTargetProperty) chooses what the glass
// refracts: the picked layer's independently rendered image, or --
// in "auto" -- the live composite below the layer.

#include "liquidglasseffect.h"

#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "skia/skqtconversions.h"

#include "appsupport.h"

#include <QRect>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

const float kM_E = 2.718281828459045f;

// Mirrors f_func() of the reference shader (constants hardcoded there)
float f_func(const float x)
{
    const float u_a = 0.7f;
    const float u_b = 2.3f;
    const float u_c = 5.2f;
    const float u_d = 6.9f;
    return 1.f - u_b * std::pow(u_c * kM_E, -u_d * x - u_a);
}

// Mirrors rand() of the reference shader (screen-space grain)
float rand2d(const float x, const float y)
{
    const float d = 12.9898f * x + 78.233f * y;
    float v = std::sin(d) * 43758.5453f;
    v = v - std::floor(v);
    return v;
}

float smoothstepf(const float a, const float b, const float t)
{
    if (b <= a) return t >= b ? 1.f : 0.f;
    const float c = (t - a) / (b - a);
    const float k = c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
    return k * k * (3.f - 2.f * k);
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

    // NOTE: Chinese names must use QStringLiteral (u16 literal, no
    // execution-charset conversion) -- tr() with \u escapes encodes to
    // GBK bytes under MSVC and Qt decodes them as UTF-8 = mojibake
    mHighlight = enve::make_shared<QrealAnimator>(0.6, 0, 2, 0.01,
            QStringLiteral("\u9AD8\u5149\u5F3A\u5EA6")); // 高光强度
    ca_addChild(mHighlight);
    mHlWidth = enve::make_shared<QrealAnimator>(3, 0.5, 20, 0.1,
            QStringLiteral("\u9AD8\u5149\u5BBD\u5EA6")); // 高光宽度
    ca_addChild(mHlWidth);
    mHlAngle = enve::make_shared<QrealAnimator>(135, 0, 360, 1,
            QStringLiteral("\u5149\u7167\u89D2\u5EA6")); // 光照角度
    ca_addChild(mHlAngle);

    // AE-style background layer picker: when a layer is picked, its
    // independently rendered image becomes the refraction source;
    // empty ("auto") keeps the live composite below
    mBackgroundTarget = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("\u80CC\u666F\u56FE\u5C42")); // 背景图层
    mBackgroundTarget->setComboPicker(true);
    connect(mBackgroundTarget.data(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox * const box) {
        Q_UNUSED(box)
        prp_afterWholeInfluenceRangeChanged();
    });
    ca_addChild(mBackgroundTarget);
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
                if (row[x * 4 + 3] > 127) {
                    if (x < bxMin) bxMin = x;
                    if (x > bxMax) bxMax = x;
                    if (y < byMin) byMin = y;
                    if (y > byMax) byMax = y;
                }
            }
        }
        if (bxMax < bxMin) return; // empty shape

        // 2) distance field inside the shape: depth from the edge in
        //    pixels, normalized by the deepest point (the reference
        //    works with the superellipse SDF in radius units, 0 at the
        //    rim and ~1 at the center -- maxDist plays the radius)
        const int bw = bxMax - bxMin + 1;
        const int bh = byMax - byMin + 1;
        std::vector<float> field;
        distanceField(maskB, bxMin, byMin, bw, bh, field);
        float maxDist = 0.f;
        for (const float v : field) maxDist = std::max(maxDist, v);
        if (maxDist < 0.5f) maxDist = 0.5f;

        // 3) backdrop: the picked background layer drawn into device
        //    space (replicating its scene placement), or -- in auto
        //    mode -- the canvas device pixels below the layer
        SkBitmap backB;
        backB.allocPixels(info);
        backB.eraseColor(SK_ColorTRANSPARENT);
        bool haveBg = false;
        if(mData.mUseBgLayer && mData.mBgSample &&
           mData.mBgSample->fRenderedImage) {
            const auto& s = *mData.mBgSample;
            const qreal devRes = box.fResolution > 0. ?
                        box.fResolution : 1.;
            const qreal sRes = s.fResolution > 0. ? s.fResolution : 1.;
            SkMatrix m = canvas->getTotalMatrix();
            if(box.fUseRenderTransform) {
                m.postConcat(toSkMatrix(box.fRenderTransform));
            }
            // scene(1x) -> device -> the sample's res-scaled scene
            // space (the space its fGlobalRect lives in): the two
            // resolutions are the only bridge needed between the
            // placements (plain 2D chains, no perspective/camera)
            m.postScale(toSkScalar(devRes / sRes),
                        toSkScalar(devRes / sRes));
            m.postTranslate(-float(dev.left()), -float(dev.top()));
            SkCanvas bc(backB);
            bc.setMatrix(m);
            if(s.fUseRenderTransform) {
                bc.concat(toSkMatrix(s.fRenderTransform));
            }
            SkPaint bp;
            bp.setFilterQuality(box.fAntiAlias ? kLow_SkFilterQuality
                                               : kNone_SkFilterQuality);
            bc.drawImage(s.fRenderedImage,
                         s.fGlobalRect.x(), s.fGlobalRect.y(), &bp);
            haveBg = true;
        }
        if(!haveBg) {
            // readPixels works for both bitmap-backed CPU canvases --
            // where getSurface() is null and snapshotting is impossible
            // -- and GPU-backed surfaces (it syncs internally)
            if (!canvas->readPixels(backB.pixmap(),
                                    dev.left(), dev.top())) return;
        }

        // 4) radial remap shading inside the shape (all coordinates
        //    are dev-relative pixels)
        SkBitmap dstB;
        dstB.allocPixels(info);
        dstB.eraseColor(SK_ColorTRANSPARENT);
        const float cx = float(bxMin + bxMax) * 0.5f;
        const float cy = float(byMin + byMax) * 0.5f;
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
                const float distN = dist / maxDist;

                // refraction: sample position pulled toward the shape
                // center, factor ~0.26 at the rim (strong bending) and
                // 1.0 within ~20% depth (identity, flat center)
                const float s = std::pow(std::max(f_func(distN), 0.001f),
                                         mData.mRefraction);
                const float sx = cx + (float(x) - cx) * s;
                const float sy = cy + (float(y) - cy) * s;

                float r, g, b, a;
                sampleBilinear(backB, dev.width(), dev.height(),
                               sx, sy, r, g, b, a);

                // angular rim glow: pattern angle around the shape
                // center, band within 6% depth from the edge, faded
                // near the center (as in the reference)
                const float vx = (float(x) - cx) / float(bw) * 2.f;
                const float vy = (float(y) - cy) / float(bh) * 2.f;
                const float glowFactor = std::sin(std::atan2(vy, vx) - 0.5f) *
                        mData.mGlowWeight *
                        smoothstepf(0.f, 0.06f, distN) *
                        smoothstepf(0.f, 0.9f,
                                    2.f * std::sqrt(vx * vx + vy * vy)) +
                        1.f + mData.mGlowBias;
                const float noise = (rand2d(float(x + dev.left()) * 1e-3f,
                                            float(y + dev.top()) * 1e-3f) - 0.5f) *
                        mData.mNoise;

                // crisp rim highlight: narrow band hugging the shape
                // edge, lit only on the side facing the light (the rim
                // normal is approximated by the radial direction for
                // convex shapes); facing^3 keeps the lit arc tight
                float highlight = 0.f;
                if(mData.mHighlightI > 0.f) {
                    const float rlen = std::sqrt(vx * vx + vy * vy);
                    if(rlen > 0.001f) {
                        const float facing = (vx * mData.mHlLightX +
                                              vy * mData.mHlLightY) / rlen;
                        if(facing > 0.f) {
                            const float t = distN / mData.mHlWidthN;
                            const float k = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                            const float band = 1.f - k * k * (3.f - 2.f * k);
                            highlight = mData.mHighlightI * band *
                                        facing * facing * facing;
                        }
                    }
                }

                float rr = r * glowFactor + noise + highlight;
                float gg = g * glowFactor + noise + highlight;
                float bb2 = b * glowFactor + noise + highlight;
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
        //    backdrop with the layer's own opacity (device space; the
        //    bitmaps are dev-relative, so the dev origin is the place)
        if (isZero4Dec(box.fOpacity)) return;
        canvas->save();
        canvas->resetMatrix();
        SkPaint dp;
        dp.setAlpha(static_cast<U8CPU>(qRound(box.fOpacity * 2.55))); // 0..100
        canvas->drawImage(SkImage::MakeFromBitmap(dstB),
                          dev.left(), dev.top(), &dp);
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
    effData.mHighlightI = mHighlight->getEffectiveValue(relFrame);
    effData.mHlWidthN = std::max(static_cast<float>(
                                     mHlWidth->getEffectiveValue(relFrame)) *
                                 0.01f, 0.001f);
    const float hlRad = float(mHlAngle->getEffectiveValue(relFrame)) *
            0.017453293f; // deg -> rad
    effData.mHlLightX = std::cos(hlRad);
    effData.mHlLightY = std::sin(hlRad);

    // queue the picked background layer for an independent render; the
    // dependency delays this box's data until the sample finishes (the
    // track-matte queExternalRender pattern). Picking a box inside this
    // layer's own subtree would recurse (its render queues us again).
    const auto bgBox = mBackgroundTarget ?
                mBackgroundTarget->getTarget() : nullptr;
    const auto parentBox = data ? data->fParentBox.data() : nullptr;
    if(bgBox && bgBox->isVisibleAndInVisibleDurationRect() &&
       bgBox != parentBox &&
       !(parentBox && parentBox->isAncestor(bgBox))) {
        const auto sample = bgBox->queExternalRender(relFrame, true);
        if(sample) {
            sample->addDependent(data);
            effData.mBgSample = sample;
            effData.mUseBgLayer = true;
        }
    }

    return enve::make_shared<LiquidGlassEffectCaller>(
                instanceHwSupport(), effData);
}
