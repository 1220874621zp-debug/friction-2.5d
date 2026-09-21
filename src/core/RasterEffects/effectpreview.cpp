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

namespace {

// preview loop length: two seconds of scene time per cycle
constexpr qreal gPreviewLoopSec = 2.0;
constexpr qreal gPreviewFps = 24.;

enum class Sample {
    text,      // the embedded user-supplied image on a dark gradient
    green      // chroma-green backdrop + the image as foreground
};

// which demo content shows the effect best
Sample sampleFor(const RasterEffectType type) {
    switch (type) {
    case RasterEffectType::CHROMA_KEY:
        return Sample::green;
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
    const qreal s = qMin(w * 0.96 / logo.width(),
                         h * 0.96 / logo.height());
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
    SkCanvas c(bmp);
    SkPaint p;
    p.setAntiAlias(true);

    SkPoint pts[2] = { SkPoint::Make(0, 0), SkPoint::Make(0, h) };
    SkColor cols[2] = { SkColorSetRGB(44, 50, 63), SkColorSetRGB(17, 20, 27) };
    p.setShader(SkGradientShader::MakeLinear(
                    pts, cols, nullptr, 2, SkTileMode::kClamp));
    c.drawRect(SkRect::MakeWH(w, h), p);
    p.setShader(nullptr);

    // the embedded user-supplied sample image, centered
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

SkBitmap makeSample(const RasterEffectType type, const QSize& size) {
    const int w = size.width();
    const int h = size.height();
    switch (sampleFor(type)) {
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
