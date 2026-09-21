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

#include <QtMath>

namespace {

// preview loop length: two seconds of scene time per cycle
constexpr qreal gPreviewLoopSec = 2.0;
constexpr qreal gPreviewFps = 24.;

enum class Sample {
    text,      // dark gradient + big white glyph + color bar
    photo,     // synthetic landscape (rich smooth gradients)
    green      // chroma-green backdrop + foreground card
};

// which demo content shows the effect best
Sample sampleFor(const RasterEffectType type) {
    switch (type) {
    case RasterEffectType::CHROMA_KEY:
        return Sample::green;
    case RasterEffectType::PIXELATE:
    case RasterEffectType::HALFTONE:
    case RasterEffectType::PIXEL_ART:
    case RasterEffectType::PAGE_CURL:
    case RasterEffectType::SHATTER:
    case RasterEffectType::SMEAR:
    case RasterEffectType::ROUGHEN_EDGES:
    case RasterEffectType::EDGE_DETECT:
    case RasterEffectType::POSTERIZE:
    case RasterEffectType::MIRROR:
    case RasterEffectType::COLOR_GRADING:
    case RasterEffectType::FILM_GRAIN:
    case RasterEffectType::SCANLINES:
    case RasterEffectType::VIGNETTE:
    case RasterEffectType::LETTERBOX:
    case RasterEffectType::LATTICE_WARP:
        return Sample::photo;
    default:
        return Sample::text;
    }
}

// this skia tree's SkRect has no center(); keep the two call sites
// tidy with a local helper
inline SkScalar rectCenterX(const SkRect& r)
{ return (r.fLeft + r.fRight) / 2.f; }

inline SkScalar rectCenterY(const SkRect& r)
{ return (r.fTop + r.fBottom) / 2.f; }

SkBitmap makeTextSample(const int w, const int h) {
    SkBitmap bmp;
    bmp.allocN32Pixels(w, h);
    SkCanvas c(bmp);
    SkPaint p;
    p.setAntiAlias(true);

    SkPoint pts[2] = { SkPoint::Make(0, 0), SkPoint::Make(0, h) };
    SkColor cols[2] = { SkColorSetRGB(44, 50, 63), SkColorSetRGB(17, 20, 27) };
    p.setShader(SkGradientShader::MakeLinear(
                    pts, cols, nullptr, 2, SkTileMode::kClamp));
    c.drawRect(SkRect::MakeWH(w, h), p);
    p.setShader(nullptr);

    SkFont font;
    font.setSize(h * 0.42f);
    font.setEmbolden(true);
    p.setColor(SK_ColorWHITE);
    const char* txt = "Aa";
    SkRect b;
    font.measureText(txt, strlen(txt), SkTextEncoding::kUTF8, &b, nullptr);
    c.drawString(txt, w / 2.f - rectCenterX(b), h / 2.f - rectCenterY(b),
                 font, p);

    // color bar: gives color effects something to chew on
    const SkColor bar[4] = { SkColorSetRGB(239, 68, 68),
                             SkColorSetRGB(250, 204, 21),
                             SkColorSetRGB(34, 197, 94),
                             SkColorSetRGB(59, 130, 246) };
    const qreal barH = h * 0.11;
    const qreal bw = w / 4.;
    for (int i = 0; i < 4; i++) {
        p.setColor(bar[i]);
        c.drawRect(SkRect::MakeXYWH(i * bw, h - barH, bw, barH), p);
    }
    return bmp;
}

SkBitmap makePhotoSample(const int w, const int h) {
    SkBitmap bmp;
    bmp.allocN32Pixels(w, h);
    SkCanvas c(bmp);
    SkPaint p;
    p.setAntiAlias(true);

    // sky gradient
    SkPoint pts[2] = { SkPoint::Make(0, 0), SkPoint::Make(0, h * 0.68f) };
    SkColor sky[2] = { SkColorSetRGB(96, 165, 250), SkColorSetRGB(254, 240, 214) };
    p.setShader(SkGradientShader::MakeLinear(
                    pts, sky, nullptr, 2, SkTileMode::kClamp));
    c.drawRect(SkRect::MakeWH(w, h), p);
    p.setShader(nullptr);

    // sun
    p.setColor(SkColorSetRGB(253, 224, 71));
    c.drawCircle(w * 0.72f, h * 0.24f, h * 0.11f, p);

    // clouds
    p.setColor(SkColorSetARGB(220, 255, 255, 255));
    c.drawOval(SkRect::MakeXYWH(w * 0.08f, h * 0.16f, w * 0.30f, h * 0.07f), p);
    c.drawOval(SkRect::MakeXYWH(w * 0.16f, h * 0.11f, w * 0.18f, h * 0.06f), p);

    // mountains
    SkPath m1;
    m1.moveTo(0, h * 0.68f);
    m1.lineTo(w * 0.30f, h * 0.30f);
    m1.lineTo(w * 0.62f, h * 0.68f);
    m1.close();
    p.setColor(SkColorSetRGB(101, 116, 74));
    c.drawPath(m1, p);

    SkPath m2;
    m2.moveTo(w * 0.38f, h * 0.68f);
    m2.lineTo(w * 0.66f, h * 0.38f);
    m2.lineTo(w, h * 0.68f);
    m2.close();
    p.setColor(SkColorSetRGB(76, 92, 56));
    c.drawPath(m2, p);

    // meadow
    SkPoint g2[2] = { SkPoint::Make(0, h * 0.68f), SkPoint::Make(0, h) };
    SkColor gr[2] = { SkColorSetRGB(110, 138, 74), SkColorSetRGB(58, 84, 42) };
    p.setShader(SkGradientShader::MakeLinear(
                    g2, gr, nullptr, 2, SkTileMode::kClamp));
    c.drawRect(SkRect::MakeXYWH(0, h * 0.68f, w, h * 0.32f), p);
    p.setShader(nullptr);
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

    // foreground card
    p.setColor(SkColorSetRGB(124, 58, 237));
    c.drawRoundRect(SkRect::MakeXYWH(w * 0.22f, h * 0.28f,
                                     w * 0.56f, h * 0.44f),
                    w * 0.06f, w * 0.06f, p);
    p.setColor(SK_ColorWHITE);
    SkFont font;
    font.setSize(h * 0.20f);
    font.setEmbolden(true);
    const char* txt = "FX";
    SkRect b;
    font.measureText(txt, strlen(txt), SkTextEncoding::kUTF8, &b, nullptr);
    c.drawString(txt, w / 2.f - rectCenterX(b),
                 h * 0.50f - rectCenterY(b), font, p);
    return bmp;
}

SkBitmap makeSample(const RasterEffectType type, const QSize& size) {
    const int w = size.width();
    const int h = size.height();
    switch (sampleFor(type)) {
    case Sample::photo: return makePhotoSample(w, h);
    case Sample::green: return makeGreenSample(w, h);
    case Sample::text:
    default: return makeTextSample(w, h);
    }
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
    if (type == RasterEffectType::LAYER_STYLES) {
        // factory default is all-styles-off = null caller; enable a
        // representative set (same parameter shape as the unittest)
        const auto styles = enve_cast<LayerStylesEffect*>(eff);
        if (!styles) { return; }
        styles->shadowEnabled()->setCurrentBoolValue(true);
        styles->glowEnabled()->setCurrentBoolValue(true);
        styles->strokeEnabled()->setCurrentBoolValue(true);
        styles->setShadow(true, 0.0, 10.0, 0.0, 5.0, 100.0, QColor(0, 0, 0));
        styles->setGlow(true, 42.0, 54.0, 23.0, QColor(120, 190, 255));
    }
}

} // namespace

namespace EffectPreview {

bool canPreview(const RasterEffectType type) {
    // backdrop-sampling effects (liquid glass) run at composite time
    // against the canvas below the layer; a single-layer preview
    // cannot express them
    if (type == RasterEffectType::LIQUID_GLASS) { return false; }
    // reads layer motion from the box render data - crashes without
    // a real layer behind it
    if (type == RasterEffectType::MOTION_BLUR) { return false; }
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
        const auto eff = createRasterEffectForNonCustomType(type);
        if (!eff) { return result; }
        setupDefaults(eff.get(), type);

        // the scan anchors on the factory default so consecutive
        // frames cannot drift
        QrealAnimator* const scanParam = findMainParam(eff.get());
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

            if (scanParam && !qFuzzyIsNull(baseVal)) {
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
