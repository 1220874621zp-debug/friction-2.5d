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

#include "effectpreview.h"

#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffectsinclude.h"
#include "RasterEffects/rastereffect.h"
#include "RasterEffects/rastereffectcaller.h"
#include "Animators/complexanimator.h"
#include "Animators/qrealanimator.h"
#include "glhelpers.h"
#include "cpurendertools.h"
#include "skia/skiaincludes.h"
#include "effectpreviewlogo.h"

#include <QtMath>
#include <cstring>

namespace {

// preview loop length: two seconds of scene time per cycle
constexpr qreal gPreviewLoopSec = 2.0;
constexpr qreal gPreviewFps = 24.;

enum class Sample {
    text,      // the embedded user-supplied image on a transparent rim
    green,     // chroma-green backdrop + the image as foreground
    liquid     // fisheye-lens "liquid glass" look on the image
};

// which demo content shows the effect best
Sample sampleFor(const RasterEffectType type) {
    switch (type) {
    case RasterEffectType::CHROMA_KEY:
        return Sample::green;
    case RasterEffectType::LIQUID_GLASS:
        return Sample::liquid;
    default:
        return Sample::text;
    }
}

// this skia tree's SkRect has no center(); not needed anymore since
// the glyph samples were replaced by the embedded wordmark

// draws the embedded user-supplied sample image aspect-fitted
// (contain) into the demo sample, centered
void drawWordmark(SkCanvas& c, SkPaint& p,
                  const int w, const int h, const qreal cyFactor) {
    static const QImage logo = QImage::fromData(
                gFrictionLogoPng, int(gFrictionLogoPngSize), "PNG");
    if (logo.isNull()) { return; }
    const qreal s = qMin(w * 0.84 / logo.width(),
                         h * 0.84 / logo.height());
    const int lw = qRound(logo.width() * s);
    const int lh = qRound(logo.height() * s);
    const QImage scaled = logo.scaled(lw, lh,
                                      Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
    const auto info = SkImageInfo::Make(
                scaled.width(), scaled.height(),
                kN32_SkColorType, kPremul_SkAlphaType);
    const auto skLogo = SkImage::MakeFromRaster(
                SkPixmap(info, scaled.constBits(),
                         static_cast<size_t>(scaled.bytesPerLine())),
                nullptr, nullptr);
    if (!skLogo) { return; }
    const SkRect dst = SkRect::MakeXYWH(
                (w - lw) / 2.f, h * cyFactor - lh / 2.f, lw, lh);
    c.drawImageRect(skLogo,
                    SkRect::MakeWH(scaled.width(), scaled.height()),
                    dst, &p, SkCanvas::kStrict_SrcRectConstraint);
}

SkBitmap makeTextSample(const int w, const int h) {
    SkBitmap bmp;
    bmp.allocN32Pixels(w, h);
    bmp.eraseARGB(0, 0, 0, 0);
    SkCanvas c(bmp);
    SkPaint p;
    p.setAntiAlias(true);

    // transparent canvas + the image centered at 84%: the transparent
    // rim lets outside-the-silhouette output (shadows, glows, blur
    // spill, frayed edges) stay visible instead of being covered by
    // an opaque backdrop
    drawWordmark(c, p, w, h, 0.5);
    return bmp;
}

SkBitmap makeGreenSample(const int w, const int h) {
    SkBitmap bmp;
    bmp.allocN32Pixels(w, h);
    SkCanvas c(bmp);
    SkPaint p;
    p.setAntiAlias(true);

    p.setColor(SkColorSetRGB(0, 177, 64));
    c.drawRect(SkRect::MakeWH(w, h), p);

    // foreground: the friction wordmark survives the key
    drawWordmark(c, p, w, h, 0.5);
    return bmp;
}

// the embedded user-supplied sample image, aspect-fitted (contain)
// into a dark-gradient canvas, with a roaming fisheye lens and rim
// highlight painted over it: the "liquid glass" look (the real effect
// samples the canvas below the layer, which a single-layer preview
// cannot express, so the sample itself carries the look and the lens
// roams slowly to keep the tile alive)
SkBitmap makeLiquidSample(const int w, const int h, const qreal phase) {
    const SkBitmap base = makeTextSample(w, h);
    SkBitmap out;
    out.allocN32Pixels(w, h);
    const qreal cx = w * (0.5 + 0.07 * std::cos(2. * M_PI * phase));
    const qreal cy = h * (0.5 + 0.07 * std::sin(2. * M_PI * phase));
    const qreal R = w * 0.30;
    uint32_t* const dstRow =
            static_cast<uint32_t*>(out.getPixels());
    const uint32_t* const srcPixels =
            static_cast<const uint32_t*>(base.getPixels());
    for (int y = 0; y < h; y++) {
        uint32_t* const dr = dstRow + y * w;
        for (int x = 0; x < w; x++) {
            const qreal dx = x - cx;
            const qreal dy = y - cy;
            const qreal d = std::sqrt(dx * dx + dy * dy);
            if (d < R && R > 0.0001) {
                const qreal r = d / R;
                // magnifying lens: center enlarged, rim undistorted
                const qreal f = 1. - 0.38 * (1. - r * r);
                int sx = qRound(cx + dx * f);
                int sy = qRound(cy + dy * f);
                sx = qBound(0, sx, w - 1);
                sy = qBound(0, sy, h - 1);
                uint32_t px = srcPixels[sy * w + sx];
                // rim highlight ring near the lens edge
                if (r > 0.84) {
                    const qreal k = (r - 0.84) / 0.16;
                    const int a = qRound(150 * k * (1. - k) * 4.);
                    const int r8 = SkColorGetR(px) + a;
                    const int g8 = SkColorGetG(px) + a;
                    const int b8 = SkColorGetB(px) + a;
                    px = SkColorSetARGB(SkColorGetA(px),
                                        qMin(r8, 255), qMin(g8, 255),
                                        qMin(b8, 255));
                }
                // soft top sheen inside the upper half of the lens
                if (dy < 0. && r > 0.35 && r < 0.9) {
                    const int a = qRound(38 * (1. - std::abs(r - 0.62) / 0.28));
                    if (a > 0) {
                        const int r8 = SkColorGetR(px) + a;
                        const int g8 = SkColorGetG(px) + a;
                        const int b8 = SkColorGetB(px) + a;
                        px = SkColorSetARGB(SkColorGetA(px),
                                            qMin(r8, 255), qMin(g8, 255),
                                            qMin(b8, 255));
                    }
                }
                dr[x] = px;
            } else {
                dr[x] = srcPixels[y * w + x];
            }
        }
    }
    return out;
}

// cheap value-noise helpers shared by the procedural samples
// (roughen creep, fractal clouds)
inline qreal fxHash2(const qreal x, const qreal y) {
    const qreal v = std::sin(x * 127.1 + y * 311.7) * 43758.5453;
    return v - std::floor(v);
}

inline qreal fxValueNoise(const qreal x, const qreal y) {
    const qreal a = fxHash2(std::floor(x), std::floor(y));
    const qreal b = fxHash2(std::floor(x) + 1., std::floor(y));
    const qreal c = fxHash2(std::floor(x), std::floor(y) + 1.);
    const qreal d = fxHash2(std::floor(x) + 1., std::floor(y) + 1.);
    const qreal fx = x - std::floor(x);
    const qreal fy = y - std::floor(y);
    const qreal ux = fx * fx * (3. - 2. * fx);
    const qreal uy = fy * fy * (3. - 2. * fy);
    return a + (b - a) * ux + (c - a) * uy +
           (a - b - c + d) * ux * uy;
}

inline qreal fxFbm(const qreal x, const qreal y) {
    qreal sum = 0.;
    qreal amp = 0.5;
    qreal f = 1.;
    for (int o = 0; o < 5; o++) {
        sum += amp * fxValueNoise(x * f, y * f);
        amp *= 0.5;
        f *= 2.;
    }
    return sum;
}

// roughen-edges look: the image's outer boundary is deformed into a
// noisy ragged outline (the rectangle edge wobbles in and out with
// fbm noise, plus a darkened rim band and creased displacement just
// inside), creeping with the phase. The real effect's CPU path is a
// passthrough, so the sample carries the look.
SkBitmap makeRoughenSample(const int w, const int h, const qreal phase) {
    const SkBitmap base = makeTextSample(w, h);
    SkBitmap out;
    out.allocN32Pixels(w, h);
    out.eraseARGB(0, 0, 0, 0);
    const qreal bw = w * 0.11;
    const uint32_t* const srcPixels =
            static_cast<const uint32_t*>(base.getPixels());
    uint32_t* const dstPixels =
            static_cast<uint32_t*>(out.getPixels());
    const qreal ph = phase * 6.28318530718;
    const qreal driftX = std::cos(ph) * 4.;
    const qreal driftY = std::sin(ph) * 4.;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const qreal d = qMin(qMin(x, y),
                                 qMin(w - 1 - x, h - 1 - y));
            // noisy boundary: positive wobble eats into the image,
            // negative lets it bulge outward past the rectangle
            const qreal n = fxFbm(x / 13. + driftX, y / 13. + driftY);
            const qreal wobble = (n - 0.5) * 2. * bw;
            const qreal eff = d - wobble;
            if (eff < -1.5) {
                // outside the deformed boundary: transparent
                continue;
            }
            // creased displacement just inside the rim: jitter the
            // sampled source by the noise gradient
            int sx = x;
            int sy = y;
            if (eff < bw) {
                const qreal k = 1. - qMax(eff, 0.) / bw;
                const qreal jx = (fxValueNoise(x / 7. + driftX * 2.,
                                               y / 7.) - 0.5) * 6. * k;
                const qreal jy = (fxValueNoise(x / 7.,
                                               y / 7. + driftY * 2.) - 0.5) * 6. * k;
                sx = qBound(0, x + qRound(jx), w - 1);
                sy = qBound(0, y + qRound(jy), h - 1);
            }
            const uint32_t src = srcPixels[sy * w + sx];
            if (eff < 2.5) {
                // dark ragged rim line on the deformed boundary
                const int r8 = SkColorGetR(src) / 3;
                const int g8 = SkColorGetG(src) / 3;
                const int b8 = SkColorGetB(src) / 3;
                dstPixels[y * w + x] =
                        SkColorSetARGB(SkColorGetA(src), r8, g8, b8);
            } else if (eff < bw) {
                // darkened crumple band fading back to full color
                const qreal k = (bw - eff) / bw;
                const int a = qRound(120 * k);
                const int r8 = SkColorGetR(src) * (255 - a) / 255;
                const int g8 = SkColorGetG(src) * (255 - a) / 255;
                const int b8 = SkColorGetB(src) * (255 - a) / 255;
                dstPixels[y * w + x] =
                        SkColorSetARGB(SkColorGetA(src), r8, g8, b8);
            } else {
                dstPixels[y * w + x] = src;
            }
        }
    }
    return out;
}

// fractal-noise cloud: fbm value noise in grayscale, drifting with
// the phase (the CPU path of the real effect does not respond to
// parameter changes, so the sample carries the look and the motion)
SkBitmap makeFractalSample(const int w, const int h, const qreal phase) {
    SkBitmap out;
    out.allocN32Pixels(w, h);
    uint32_t* const dstPixels =
            static_cast<uint32_t*>(out.getPixels());
    const qreal ph = phase * 6.28318530718;
    const qreal driftX = std::cos(ph) * 8.;
    const qreal driftY = std::sin(ph) * 8.;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const qreal n = fxFbm(x / 26. + driftX, y / 26. + driftY);
            const int g = qBound(0, qRound(20 + 215 * n), 255);
            dstPixels[y * w + x] = SkColorSetARGB(255, g, g, g);
        }
    }
    return out;
}

// motion-blur look: the image smeared along a direction with a
// breathing trail length (the real effect needs layer motion from
// the box render data; the sample stands in with a classic streak)
SkBitmap makeMotionBlurSample(const int w, const int h, const qreal phase) {
    const SkBitmap base = makeTextSample(w, h);
    SkBitmap out;
    out.allocN32Pixels(w, h);
    out.eraseARGB(0, 0, 0, 0);
    const uint32_t* const srcPixels =
            static_cast<const uint32_t*>(base.getPixels());
    uint32_t* const dstPixels =
            static_cast<uint32_t*>(out.getPixels());
    const qreal breath = 0.5 + 0.5 * std::sin(2. * M_PI * phase);
    const int L = 2 + qRound(14 * breath);
    constexpr int kPasses = 9;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            qreal r = 0., g = 0., b = 0., a = 0., wsum = 0.;
            for (int k = 0; k < kPasses; k++) {
                const qreal u = k / static_cast<qreal>(kPasses - 1) - 0.5;
                const int sx = qRound(x + u * 2. * L);
                const int sy = qRound(y + u * 0.5 * L);
                if (sx < 0 || sx >= w || sy < 0 || sy >= h) { continue; }
                const uint32_t px = srcPixels[sy * w + sx];
                const qreal wk = 1. - std::abs(u) * 1.4;
                if (wk <= 0.) { continue; }
                const qreal pa = SkColorGetA(px) / 255.;
                r += SkColorGetR(px) * pa * wk;
                g += SkColorGetG(px) * pa * wk;
                b += SkColorGetB(px) * pa * wk;
                a += pa * wk;
                wsum += wk;
            }
            if (a > 0.004) {
                const int ia = qBound(0, qRound(a / wsum * 255.), 255);
                const int ir = qBound(0, qRound(r / a), 255);
                const int ig = qBound(0, qRound(g / a), 255);
                const int ib = qBound(0, qRound(b / a), 255);
                dstPixels[y * w + x] =
                        SkColorSetARGB(ia, ir, ig, ib);
            }
        }
    }
    return out;
}

// rain look: opaque black sky with white slanted streaks falling
// (the user asked for exactly this readable readout)
SkBitmap makeRainSample(const int w, const int h, const qreal phase) {
    SkBitmap out;
    out.allocN32Pixels(w, h);
    SkCanvas c(out);
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(SkColorSetRGB(8, 10, 14));
    c.drawRect(SkRect::MakeWH(w, h), p);

    const qreal slope = 0.22; // streak slant dx/dy
    const int nDrops = 90;
    for (int i = 0; i < nDrops; i++) {
        const qreal rx = fxHash2(i * 3.1, 7.7);
        const qreal rs = 0.5 + fxHash2(i * 9.3, 2.9);
        const qreal rl = 10. + 26. * fxHash2(i * 5.7, 4.1);
        const int alpha = qRound(90. + 150. * fxHash2(i * 1.7, 8.3));
        const qreal span = h + rl * 3.;
        const qreal y0 = fxHash2(i * 2.3, 6.1) * span
                         + phase * rs * h * 2.2;
        const qreal yy = std::fmod(y0, span) - rl;
        const qreal xx = std::fmod(rx * w + yy * slope, w + 40.) - 20.;
        p.setColor(SkColorSetARGB(alpha, 225, 232, 240));
        c.drawLine(SkPoint::Make(xx, yy),
                   SkPoint::Make(xx + rl * slope, yy + rl), p);
    }
    return out;
}

// lattice-warp look: the image warped by a breathing gaussian bulge
// with a cyan lattice grid drawn over it, the grid lines bending
// with the same deformation so the warp is directly readable. The
// real caller's resample swallows thin overlay lines, so the sample
// carries both the warp and the grid.
SkBitmap makeLatticeAnimSample(const int w, const int h, const qreal phase) {
    const SkBitmap base = makeTextSample(w, h);
    SkBitmap out;
    out.allocN32Pixels(w, h);
    out.eraseARGB(0, 0, 0, 0);
    const uint32_t* const srcPixels =
            static_cast<const uint32_t*>(base.getPixels());
    uint32_t* const dstPixels =
            static_cast<uint32_t*>(out.getPixels());
    const qreal cx = w / 2.;
    const qreal cy = h / 2.;
    const qreal sig = w * 0.30;
    const qreal amp = 0.30 + 0.22 * std::sin(2. * M_PI * phase);
    const auto bulge = [&](const qreal x, const qreal y) {
        const qreal dx = (x - cx) / sig;
        const qreal dy = (y - cy) / sig;
        return 1. + amp * std::exp(-(dx * dx + dy * dy) / 2.);
    };
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const qreal k = bulge(x, y);
            const int sx = qBound(0, qRound(cx + (x - cx) / k), w - 1);
            const int sy = qBound(0, qRound(cy + (y - cy) / k), h - 1);
            dstPixels[y * w + x] = srcPixels[sy * w + sx];
        }
    }
    // bent grid: rule-grid points pushed through the same bulge
    SkCanvas c(out);
    SkPaint p;
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setColor(SkColorSetARGB(190, 90, 225, 255));
    p.setStrokeWidth(1.6f);
    const int cells = 5;
    const qreal step = 14.;
    for (int i = 0; i <= cells; i++) {
        // vertical rule at x = v*w, bent
        SkPath vLine;
        bool started = false;
        for (qreal y = 0.; y <= h + step / 2.; y += step) {
            const qreal k = bulge(i * w / static_cast<qreal>(cells), y);
            const qreal bx = cx + (i * w / cells - cx) * k;
            const qreal by = cy + (y - cy) * k;
            if (!started) { vLine.moveTo(bx, by); started = true; }
            else { vLine.lineTo(bx, by); }
        }
        c.drawPath(vLine, p);
        // horizontal rule
        SkPath hLine;
        started = false;
        for (qreal x = 0.; x <= w + step / 2.; x += step) {
            const qreal k = bulge(x, i * h / static_cast<qreal>(cells));
            const qreal bx = cx + (x - cx) * k;
            const qreal by = cy + (i * h / cells - cy) * k;
            if (!started) { hLine.moveTo(bx, by); started = true; }
            else { hLine.lineTo(bx, by); }
        }
        c.drawPath(hLine, p);
    }
    return out;
}

SkBitmap makeSample(const RasterEffectType type, const QSize& size) {
    const int w = size.width();
    const int h = size.height();
    switch (sampleFor(type)) {
    case Sample::green: return makeGreenSample(w, h);
    case Sample::liquid: return makeLiquidSample(w, h, 0.);
    case Sample::text:
    default: return makeTextSample(w, h);
    }
}

// per-type explicit parameter sweeps for effects whose animation
// parameter the generic keyword scan misses (progress/time style
// names) or whose parameter lives nested inside a group (lattice
// control points). The value sweeps linearly from "from" to "to"
// over the loop and wraps.
struct NamedScan {
    const char* paramName;
    const char* altName; // second accepted name (translated variants)
    qreal from;
    qreal to;
};

NamedScan namedScanFor(const RasterEffectType type) {
    switch (type) {
    case RasterEffectType::WIPE:
        return { "time", nullptr, 0., 1. };
    case RasterEffectType::NOISE_FADE:
        return { "time", nullptr, 0., 1. };
    case RasterEffectType::SHATTER:
        return { "progress", nullptr, 0., 100. };
    case RasterEffectType::ROUGHEN_EDGES:
        // the CPU path is a passthrough copy (no edge processing):
        // the purpose-built frayed sample carries the look instead,
        // with a creeping noise phase; no parameter sweep needed
        return { nullptr, nullptr, 0., 0. };
    case RasterEffectType::LAYER_STYLES:
        // all styles are static; breathe the shadow distance so the
        // tile is alive (the param name is the Chinese tr source
        // string, living inside the shadow group)
        return { "distance", "距离", 12., 34. };
    case RasterEffectType::PAGE_CURL:
        // factory progress 0 = flat page; sweeping it curls the page
        // across the corner for a real turning look
        return { "progress", nullptr, 0., 100. };
    case RasterEffectType::COLOR_GRADING:
        // all grading params default to 0 (= passthrough); breathing
        // the saturation on top of a warm cine base look
        return { "saturation", nullptr, 5., 60. };
    default:
        return { nullptr, nullptr, 0., 0. };
    }
}

// find a numeric animator by name, descending into nested complex
// animators (lattice warp keeps its u/v/rot/scale inside per-point
// groups two levels down)
QrealAnimator* findAnimatorByName(Property* const prop,
                                  const QString& name,
                                  const QString& alt,
                                  const int depth = 0) {
    if (!prop || depth > 3) { return nullptr; }
    if (auto* const qa = enve_cast<QrealAnimator*>(prop)) {
        const QString n = qa->prp_getName().toLower();
        if (n.contains(name.toLower()) ||
            (!alt.isEmpty() && n.contains(alt.toLower()))) {
            return qa;
        }
        return nullptr;
    }
    if (auto* const ca = enve_cast<ComplexAnimator*>(prop)) {
        const int n = ca->ca_getNumberOfChildren();
        for (int i = 0; i < n; i++) {
            auto* const found = findAnimatorByName(ca->ca_getChildAt(i),
                                                   name, alt, depth + 1);
            if (found) { return found; }
        }
    }
    return nullptr;
}

// locate the "main" numeric parameter for the generic scan: prefer a
// keyword hit on the property name, fall back to the first numeric
// child (effects generally declare their primary parameter first)
QrealAnimator* findMainParam(RasterEffect* const eff) {
    QrealAnimator* firstNumeric = nullptr;
    static const char* keys[] = {
        "radius", "blur", "strength", "amount", "power", "intensity",
        "scale", "size", "distance", "threshold", "spread", "density",
        "angle", "brightness", "contrast", "saturation", "opacity",
        "tolerance", "softness", "count", "speed", "amplitude",
        "frequency", nullptr
    };
    const int nChildren = eff->ca_getNumberOfChildren();
    for (int i = 0; i < nChildren; i++) {
        Property* const prop = eff->ca_getChildAt(i);
        auto* const qa = enve_cast<QrealAnimator*>(prop);
        if (!qa) { continue; }
        if (!firstNumeric) { firstNumeric = qa; }
        const QString name = qa->prp_getName().toLower();
        for (int k = 0; keys[k]; k++) {
            if (name.contains(QString::fromLatin1(keys[k]))) { return qa; }
        }
    }
    return firstNumeric;
}

// one-time default-value tweaks for effects whose factory defaults do
// not show anything interesting on the demo samples
void setupDefaults(RasterEffect* const eff, const RasterEffectType type) {
    const auto setParam = [eff](const char* name, const qreal value) {
        auto* const qa = findAnimatorByName(eff, QString::fromLatin1(name),
                                            QString());
        if (qa) { qa->setCurrentBaseValue(value); }
    };
    switch (type) {
    case RasterEffectType::PAGE_CURL:
        // a fatter curl tube reads as page-turning at thumbnail size
        setParam("radius", 20.);
        break;
    case RasterEffectType::COLOR_GRADING:
        // factory defaults are all zero = no grading at all; set a
        // warm cinematic base so the tile reads as "graded"
        setParam("exposure", 0.35);
        setParam("contrast", 25.);
        setParam("temperature", 18.);
        setParam("tint", -8.);
        break;
    case RasterEffectType::ROUGHEN_EDGES:
        // much stronger frayed edge than the subtle factory default
        setParam("border", 45.);
        setParam("complexity", 5.);
        break;
    case RasterEffectType::LATTICE_WARP:
        // pronounced center bulge handled by the explicit scale sweep
        break;
    case RasterEffectType::LAYER_STYLES: {
        // factory default is all-styles-off = null caller; enable a
        // bold set (same setters the PSD import uses)
        const auto styles = enve_cast<LayerStylesEffect*>(eff);
        if (!styles) { break; }
        styles->shadowEnabled()->setCurrentBoolValue(true);
        styles->glowEnabled()->setCurrentBoolValue(true);
        styles->strokeEnabled()->setCurrentBoolValue(true);
        styles->setShadow(true, 0.0, 20.0, 30.0, 32.0, 100.0,
                          QColor(0, 0, 0));
        styles->setGlow(true, 45.0, 80.0, 95.0, QColor(80, 200, 255));
        styles->setStroke(true, 0, 7, 100, QColor(255, 60, 60));
        break;
    }
    default:
        break;
    }
}

} // namespace

namespace EffectPreview {

bool canPreview(const RasterEffectType type) {
    // motion blur reads layer motion from the box render data and
    // crashes without a real layer behind it - a purpose-built
    // streak sample stands in instead
    if (type == RasterEffectType::CUSTOM ||
        type == RasterEffectType::CUSTOM_SHADER) { return false; }
    return true;
}

QList<QImage> renderEffectFrames(const RasterEffectType type,
                                 const int nFrames,
                                 const QSize& imgSize)
{
    QList<QImage> result;
    if (!canPreview(type)) { return result; }
    if (nFrames < 1 || imgSize.width() < 2 || imgSize.height() < 2) {
        return result;
    }
    try {
        // purpose-built sample paths (their callers cannot produce
        // the look offscreen): liquid glass needs the composite below
        // the layer; roughen edges' CPU path is a passthrough;
        // fractal noise's CPU path ignores parameters; motion blur
        // needs layer motion from the box render data; rain gets a
        // plain black-sky/white-streaks animation per user request
        if (type == RasterEffectType::LIQUID_GLASS ||
            type == RasterEffectType::ROUGHEN_EDGES ||
            type == RasterEffectType::FRACTAL_NOISE ||
            type == RasterEffectType::MOTION_BLUR ||
            type == RasterEffectType::RAIN ||
            type == RasterEffectType::LATTICE_WARP) {
            for (int i = 0; i < nFrames; i++) {
                const qreal t = i / static_cast<qreal>(nFrames);
                const SkBitmap frame =
                        type == RasterEffectType::LIQUID_GLASS
                        ? makeLiquidSample(imgSize.width(),
                                           imgSize.height(), t)
                        : type == RasterEffectType::ROUGHEN_EDGES
                        ? makeRoughenSample(imgSize.width(),
                                            imgSize.height(), t)
                        : type == RasterEffectType::MOTION_BLUR
                        ? makeMotionBlurSample(imgSize.width(),
                                               imgSize.height(), t)
                        : type == RasterEffectType::RAIN
                        ? makeRainSample(imgSize.width(),
                                         imgSize.height(), t)
                        : type == RasterEffectType::LATTICE_WARP
                        ? makeLatticeAnimSample(imgSize.width(),
                                                imgSize.height(), t)
                        : makeFractalSample(imgSize.width(),
                                            imgSize.height(), t);
                QImage img(imgSize, QImage::Format_ARGB32_Premultiplied);
                if (img.sizeInBytes() > 0) {
                    memcpy(img.bits(), frame.getPixels(),
                           static_cast<size_t>(img.sizeInBytes()));
                }
                result << img;
            }
            return result;
        }

        const auto eff = createRasterEffectForNonCustomType(type);
        if (!eff) { return result; }
        setupDefaults(eff.get(), type);

        // explicit named sweep wins over the generic keyword scan
        // (progress/time-style parameters, nested lattice controls)
        const NamedScan named = namedScanFor(type);
        QrealAnimator* namedParam = nullptr;
        if (named.paramName) {
            namedParam = findAnimatorByName(
                        eff.get(), QString::fromLatin1(named.paramName),
                        named.altName ? QString::fromUtf8(named.altName)
                                      : QString());
        }

        // the scan anchors on the factory default so consecutive
        // frames cannot drift
        QrealAnimator* const scanParam = namedParam ? namedParam
                                                    : findMainParam(eff.get());
        const qreal baseVal = scanParam ? scanParam->getCurrentBaseValue() : 0.;
        const qreal targetVal = scanParam ? scanParam->clamped(baseVal * 2.5) : 0.;

        const SkBitmap src = makeSample(type, imgSize);
        const int loopSceneFrames = qMax(2, qRound(gPreviewLoopSec * gPreviewFps));

        // position/axis-type parameters get a gentle oscillation
        // around the default instead of the wide 2.5x sweep: a wide
        // sweep walks e.g. the mirror axis off-canvas and half the
        // loop shows a half-flipped image that reads as broken
        const bool gentleScan = (type == RasterEffectType::MIRROR);

        for (int i = 0; i < nFrames; i++) {
            const qreal t = i / static_cast<qreal>(nFrames);
            const int relFrame = qRound(i * loopSceneFrames / static_cast<qreal>(nFrames));

            if (namedParam) {
                // linear one-way sweep (wipe progress, shatter spread)
                namedParam->setCurrentBaseValue(
                            named.from + (named.to - named.from) * t);
            } else if (scanParam && !qFuzzyIsNull(baseVal)) {
                if (gentleScan) {
                    const qreal v = scanParam->clamped(
                                baseVal + 0.15 * std::sin(2. * M_PI * t));
                    scanParam->setCurrentBaseValue(v);
                } else {
                    const qreal v = baseVal + (targetVal - baseVal)
                                        * (0.5 - 0.5 * std::cos(2. * M_PI * t));
                    scanParam->setCurrentBaseValue(v);
                }
            }

            const auto caller = eff->getEffectCaller(relFrame, 1., 1., nullptr);
            QImage img(imgSize, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            if (caller && !caller->samplesBackdrop()) {
                // the skia raster surface writes straight into the
                // qimage buffer (matching memory layouts)
                SkBitmap dst;
                dst.installPixels(
                            SkImageInfo::Make(imgSize.width(),
                                              imgSize.height(),
                                              kN32_SkColorType,
                                              kPremul_SkAlphaType),
                            img.bits(),
                            static_cast<size_t>(img.bytesPerLine()));
                CpuRenderTools tools{src, dst};
                CpuRenderData data;
                data.fTexTile = SkIRect::MakeWH(imgSize.width(),
                                                imgSize.height());
                data.fWidth = static_cast<uint>(imgSize.width());
                data.fHeight = static_cast<uint>(imgSize.height());
                caller->processCpu(tools, data);
            }
            result << img;
        }
        // restore the factory default so a cached instance is not
        // left mid-scan (defensive; instances are per-call here)
        if (scanParam) { scanParam->setCurrentBaseValue(baseVal); }
    } catch (const std::exception& e) {
        qWarning() << "effect preview render failed:" << e.what();
        return {};
    } catch (...) {
        qWarning() << "effect preview render failed: unknown error";
        return {};
    }
    return result;
}

} // namespace EffectPreview
