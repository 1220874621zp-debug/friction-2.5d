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
    text,      // the embedded user-supplied image on a dark gradient
    green,     // chroma-green backdrop + the image as foreground
    liquid,    // fisheye-lens "liquid glass" look on the image
    roughen    // frayed/noisy-edge look (CPU path is a passthrough)
};

// which demo content shows the effect best
Sample sampleFor(const RasterEffectType type) {
    switch (type) {
    case RasterEffectType::CHROMA_KEY:
        return Sample::green;
    case RasterEffectType::LIQUID_GLASS:
        return Sample::liquid;
    case RasterEffectType::ROUGHEN_EDGES:
        return Sample::roughen;
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

// frayed-edge look for roughen edges: the image's rectangle border is
// eaten away by a creeping value noise, leaving a rough ragged rim
// with darkened debris. The real effect's CPU path is a passthrough,
// so the sample itself carries the look (phase drives the creep).
SkBitmap makeRoughenSample(const int w, const int h, const qreal phase) {
    const SkBitmap base = makeTextSample(w, h);
    SkBitmap out;
    out.allocN32Pixels(w, h);
    const qreal bw = w * 0.055;
    const uint32_t* const srcPixels =
            static_cast<const uint32_t*>(base.getPixels());
    uint32_t* const dstPixels =
            static_cast<uint32_t*>(out.getPixels());
    const qreal ph = phase * 6.28318530718;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const qreal d = qMin(qMin(x, y),
                                 qMin(w - 1 - x, h - 1 - y));
            const uint32_t src = srcPixels[y * w + x];
            const qreal n = fxValueNoise(x / 9. + std::cos(ph) * 3.,
                                         y / 9. + std::sin(ph) * 3.);
            if (d < bw * (0.25 + 1.35 * n)) {
                // eaten away: transparent, with sparse dark debris
                dstPixels[y * w + x] = (n > 0.62)
                        ? SkColorSetARGB(90, 40, 38, 36)
                        : 0;
            } else if (d < bw * 1.9) {
                // ragged transition band: darken and roughen
                const qreal k = 1. - (d - bw) / (bw * 0.9);
                const int a = qRound(0.25 * k * 255.);
                const int r8 = SkColorGetR(src) * (255 - a) / 255 + 30 * a / 255;
                const int g8 = SkColorGetG(src) * (255 - a) / 255 + 26 * a / 255;
                const int b8 = SkColorGetB(src) * (255 - a) / 255 + 24 * a / 255;
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
    case RasterEffectType::LATTICE_WARP:
        return { "scale", "缩放", 160., 195. };
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
    // reads layer motion from the box render data - crashes without
    // a real layer behind it
    if (type == RasterEffectType::MOTION_BLUR) { return false; }
    if (type == RasterEffectType::CUSTOM ||
        type == RasterEffectType::CUSTOM_SHADER) { return false; }
    // liquid glass samples the canvas below the layer at composite
    // time; the preview shows a purpose-built "liquid glass" sample
    // instead of running the caller
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
        // liquid glass samples the composite below the layer, roughen
        // edges' and fractal noise's CPU paths are a passthrough /
        // parameter-dead: purpose-built samples carry the looks,
        // animated by their own phase over the loop
        if (type == RasterEffectType::LIQUID_GLASS ||
            type == RasterEffectType::ROUGHEN_EDGES ||
            type == RasterEffectType::FRACTAL_NOISE) {
            for (int i = 0; i < nFrames; i++) {
                const qreal t = i / static_cast<qreal>(nFrames);
                const SkBitmap frame =
                        type == RasterEffectType::LIQUID_GLASS
                        ? makeLiquidSample(imgSize.width(),
                                           imgSize.height(), t)
                        : type == RasterEffectType::ROUGHEN_EDGES
                        ? makeRoughenSample(imgSize.width(),
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
