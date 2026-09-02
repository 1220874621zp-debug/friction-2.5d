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

#include <QCoreApplication>
#include <QTranslator>
#include <QDebug>
#include <iostream>
#include <cassert>

#include "RasterEffects/rastereffectsinclude.h"
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "Psd/psdfile.h"
#include "include/core/SkBitmap.h"

#include "themesupport.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // surface qWarning from core (psd parser diagnostics) on stderr:
    // the default Windows handler drops them when no real console
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&,
                              const QString& msg) {
        fprintf(stderr, "[QT%d] %s\n", int(type), qPrintable(msg));
        fflush(stderr);
    });
    int passed = 0;
    int failed = 0;

    const auto runTest = [&](const char* name, auto func) {
        std::cout << "[RUNNING] " << name << " ... ";
        try {
            func();
            std::cout << "PASSED" << std::endl;
            passed++;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
            failed++;
        } catch (...) {
            std::cout << "FAILED (unknown exception)" << std::endl;
            failed++;
        }
    };

    // Test 1: Factory instantiation for all RasterEffectTypes
    runTest("Test 1: createRasterEffectForNonCustomType", [&]() {
        const RasterEffectType types[] = {
            RasterEffectType::BLUR,
            RasterEffectType::SHADOW,
            RasterEffectType::MOTION_BLUR,
            RasterEffectType::WIPE,
            RasterEffectType::NOISE_FADE,
            RasterEffectType::COLORIZE,
            RasterEffectType::BRIGHTNESS_CONTRAST,
            RasterEffectType::VIGNETTE,
            RasterEffectType::CHROMATIC_ABERRATION,
            RasterEffectType::LETTERBOX,
            RasterEffectType::SCANLINES,
            RasterEffectType::GLOW,
            RasterEffectType::DIRECTIONAL_BLUR,
            RasterEffectType::RADIAL_BLUR,
            RasterEffectType::WAVE_WARP,
            RasterEffectType::RAIN,
            RasterEffectType::EDGE_DETECT,
            RasterEffectType::INVERT,
            RasterEffectType::TINT,
            RasterEffectType::PIXELATE,
            RasterEffectType::NOISE,
            RasterEffectType::MIRROR,
            RasterEffectType::GLITCH,
            RasterEffectType::POSTERIZE,
            RasterEffectType::TWIRL,
            RasterEffectType::CHANNEL_BLUR,
            RasterEffectType::HALFTONE,
            RasterEffectType::SHAKE,
            RasterEffectType::DROP_SHADOW,
            RasterEffectType::ZOOM_BLUR,
            RasterEffectType::COLOR_GRADING,
            RasterEffectType::STRIPE,
            RasterEffectType::MOTION_TILE,
            RasterEffectType::FRACTAL_NOISE,
            RasterEffectType::LIGHT_SWEEP,
            RasterEffectType::DISPLACEMENT_WARP,
            RasterEffectType::FILM_GRAIN,
            RasterEffectType::BLACK_WHITE_FLASH,
            RasterEffectType::LIQUID_GLASS,
            RasterEffectType::PIXEL_ART,
            RasterEffectType::CHROMA_KEY,
            RasterEffectType::LAYER_STYLES
        };

        for (const auto t : types) {
            auto eff = createRasterEffectForNonCustomType(t);
            if (!eff) {
                throw std::runtime_error(std::string("Factory returned null for type ") + std::to_string(int(t)));
            }
            if (eff->getEffectType() != t) {
                throw std::runtime_error("Effect type mismatch");
            }
            if (eff->prp_getName().isEmpty()) {
                throw std::runtime_error("Effect name is empty");
            }
        }
    });

    // Test 2: Caller generation and CPU render execution
    runTest("Test 2: CPU Tile Processing", [&]() {
        const RasterEffectType types[] = {
            RasterEffectType::BRIGHTNESS_CONTRAST,
            RasterEffectType::COLORIZE,
            RasterEffectType::VIGNETTE,
            RasterEffectType::CHROMATIC_ABERRATION,
            RasterEffectType::LETTERBOX,
            RasterEffectType::SCANLINES,
            RasterEffectType::GLOW,
            RasterEffectType::DIRECTIONAL_BLUR,
            RasterEffectType::RADIAL_BLUR,
            RasterEffectType::WAVE_WARP,
            RasterEffectType::RAIN,
            RasterEffectType::EDGE_DETECT,
            RasterEffectType::INVERT,
            RasterEffectType::TINT,
            RasterEffectType::PIXELATE,
            RasterEffectType::NOISE,
            RasterEffectType::MIRROR,
            RasterEffectType::GLITCH,
            RasterEffectType::POSTERIZE,
            RasterEffectType::TWIRL,
            RasterEffectType::CHANNEL_BLUR,
            RasterEffectType::HALFTONE,
            RasterEffectType::SHAKE,
            RasterEffectType::DROP_SHADOW,
            RasterEffectType::ZOOM_BLUR,
            RasterEffectType::COLOR_GRADING,
            RasterEffectType::STRIPE,
            RasterEffectType::MOTION_TILE,
            RasterEffectType::FRACTAL_NOISE,
            RasterEffectType::LIGHT_SWEEP,
            RasterEffectType::DISPLACEMENT_WARP,
            RasterEffectType::FILM_GRAIN,
            RasterEffectType::BLACK_WHITE_FLASH,
            RasterEffectType::LIQUID_GLASS,
            RasterEffectType::PIXEL_ART
        };

        SkBitmap srcBtmp;
        srcBtmp.allocN32Pixels(64, 64);
        srcBtmp.eraseARGB(255, 128, 64, 200);

        SkBitmap dstBtmp;
        dstBtmp.allocN32Pixels(64, 64);
        dstBtmp.eraseARGB(0, 0, 0, 0);

        for (const auto t : types) {
            auto eff = createRasterEffectForNonCustomType(t);
            auto caller = eff->getEffectCaller(0.0, 1.0, 1.0, nullptr);
            if (!caller) {
                throw std::runtime_error("Caller is null for type " + std::to_string(int(t)));
            }

            CpuRenderTools tools{srcBtmp, dstBtmp};
            CpuRenderData data;
            data.fTexTile = SkIRect::MakeXYWH(0, 0, 64, 64);

            caller->processCpu(tools, data);
        }
    });

    // Test 2b: Layer Styles effect needs at least one style enabled
    // before a caller exists (default state is all-off = null caller).
    // Mirrors EffectSubTaskSpawner: full-size dst, per-tile subsets,
    // then pixel assertions (styles must land outside the silhouette)
    runTest("Test 2b: Layer Styles CPU render", [&]() {
        const auto eff = createRasterEffectForNonCustomType(
                RasterEffectType::LAYER_STYLES);
        if (!eff) { throw std::runtime_error("Factory returned null"); }
        const auto styles = enve_cast<LayerStylesEffect*>(eff.get());
        if (!styles) { throw std::runtime_error("Not a LayerStylesEffect"); }
        styles->shadowEnabled()->setCurrentBoolValue(true);
        styles->glowEnabled()->setCurrentBoolValue(true);
        styles->strokeEnabled()->setCurrentBoolValue(true);
        // angle 0 -> light from the right, shadow falls left
        styles->setShadow(true, 0.0, 10.0, 0.0, 5.0, 100.0, QColor(0, 0, 0));
        const auto caller = styles->getEffectCaller(0.0, 1.0, 1.0, nullptr);
        if (!caller) { throw std::runtime_error("Layer styles caller is null"); }

        SkBitmap srcBtmp;
        srcBtmp.allocN32Pixels(64, 64);
        srcBtmp.eraseARGB(0, 0, 0, 0);
        {
            SkCanvas c(srcBtmp);
            SkPaint p;
            p.setColor(SkColorSetARGB(255, 128, 64, 200));
            c.drawRect(SkRect::MakeXYWH(16, 16, 32, 32), p);
        }

        SkBitmap dstBtmp;
        dstBtmp.allocN32Pixels(srcBtmp.width(), srcBtmp.height());
        dstBtmp.eraseARGB(0, 0, 0, 0);

        // two vertical tiles, exactly like the spawner subsets them
        const SkIRect tiles[] = { SkIRect::MakeXYWH(0, 0, 32, 64),
                                  SkIRect::MakeXYWH(32, 0, 32, 64) };
        for (const auto& tile : tiles) {
            SkBitmap tileDst;
            if (!dstBtmp.extractSubset(&tileDst, tile)) {
                throw std::runtime_error("extractSubset failed");
            }
            CpuRenderTools tools{srcBtmp, tileDst};
            CpuRenderData data;
            data.fTexTile = tile;
            caller->processCpu(tools, data);
        }

        // silhouette core survives with the original color
        const auto core = static_cast<const uint32_t*>(dstBtmp.getAddr(32, 32));
        if (SkColorGetA(*core) < 250) {
            throw std::runtime_error("layer core lost alpha");
        }
        // stroke ring outside the right edge of the square (x=50)
        const auto rightRing = static_cast<const uint32_t*>(dstBtmp.getAddr(50, 32));
        if (SkColorGetA(*rightRing) < 100
                || SkColorGetR(*rightRing) < 200) {
            throw std::runtime_error("no red stroke ring on the right");
        }
        // shadow left of the square: square spans x[16,48], distance 10
        // -> shadow spans x[6,38]; sample inside it
        const auto shadowPx = static_cast<const uint32_t*>(dstBtmp.getAddr(7, 32));
        if (SkColorGetA(*shadowPx) < 30) {
            throw std::runtime_error("no shadow to the left");
        }

        // --- round 2: with spread/choke (the PSD-import parameter
        // shape that failed on the user's GPU render) ---
        dstBtmp.eraseARGB(0, 0, 0, 0);
        styles->setShadow(true, 90.0, 10.0, 56.0, 7.0, 40.0, QColor(0, 0, 0));
        styles->setGlow(true, 42.0, 54.0, 23.0, QColor(0, 255, 24));
        const auto caller2 = styles->getEffectCaller(0.0, 1.0, 1.0, nullptr);
        if (!caller2) { throw std::runtime_error("caller2 is null"); }
        for (const auto& tile : tiles) {
            SkBitmap tileDst;
            if (!dstBtmp.extractSubset(&tileDst, tile)) {
                throw std::runtime_error("extractSubset failed");
            }
            CpuRenderTools tools{srcBtmp, tileDst};
            CpuRenderData data;
            data.fTexTile = tile;
            caller2->processCpu(tools, data);
        }
        // angle 90 with the PS dial mapping -> shadow straight UP
        // (-10): spans y[6,38]; sample above the square (y=12);
        // shadow is black: tint must dominate, not the layer's purple
        const auto shadowPx2 = static_cast<const uint32_t*>(dstBtmp.getAddr(20, 12));
        if (SkColorGetA(*shadowPx2) < 20
                || SkColorGetB(*shadowPx2) > SkColorGetR(*shadowPx2) + 10) {
            throw std::runtime_error("no choked black shadow above");
        }
        // glow: the user's real PSD parameters (spread 42, size 54,
        // opacity 23%) must stay visible - spread is ignored for glow
        // and the rim is lifted x2, otherwise 0.23*0.5*choke leaves
        // a handful of alpha units invisible to the eye
        styles->setGlow(true, 42.0, 54.0, 23.0, QColor(0, 255, 24));
        const auto caller3 = styles->getEffectCaller(0.0, 1.0, 1.0, nullptr);
        if (!caller3) { throw std::runtime_error("caller3 is null"); }
        dstBtmp.eraseARGB(0, 0, 0, 0);
        for (const auto& tile : tiles) {
            SkBitmap tileDst;
            if (!dstBtmp.extractSubset(&tileDst, tile)) {
                throw std::runtime_error("extractSubset failed");
            }
            CpuRenderTools tools{srcBtmp, tileDst};
            CpuRenderData data;
            data.fTexTile = tile;
            caller3->processCpu(tools, data);
        }
        // (11,32) is 5px left of the square edge: ~48/255 green
        const auto glowPx = static_cast<const uint32_t*>(dstBtmp.getAddr(11, 32));
        if (SkColorGetA(*glowPx) < 30
                || SkColorGetG(*glowPx) < SkColorGetR(*glowPx)) {
            std::cout << " [glow debug:";
            for (int x = 8; x <= 20; x += 2) {
                const auto px = static_cast<const uint32_t*>(dstBtmp.getAddr(x, 32));
                std::cout << " x" << x << "=" << SkColorGetA(*px)
                          << "/g" << SkColorGetG(*px);
            }
            std::cout << "] ";
            throw std::runtime_error("no visible green glow around");
        }
    });

    // Test 5b: PSD layer-styles parsing on a real file passed as
    // argv[1] (skipped when no argument) - offline repro for imports
    runTest("Test 5b: PSD layer styles (real file)", [&]() {
        if (argc < 2) { std::cout << " (skipped, no file) "; return; }
        psd::PsdFile psd;
        QString err;
        if (!psd.load(QString::fromLocal8Bit(argv[1]), &err)) {
            throw std::runtime_error(("load failed: " + err).toStdString());
        }
        int styled = 0;
        for (const auto& rec : psd.layers()) {
            if (rec.styles.hasAny) { styled++; }
        }
        std::cout << " layers=" << psd.layers().size()
                  << " styled=" << styled << " ";
        if (styled < 1) {
            throw std::runtime_error("expected at least one styled layer");
        }
    });

    // Test 3: Menu registry coverage
    runTest("Test 3: RasterEffectMenuCreator coverage", [&]() {
        int count = 0;
        RasterEffectMenuCreator::forEveryEffectCore(
            [&](const QString& name, const QString& cat,
                const RasterEffectMenuCreator::EffectCreator& creator) {
                Q_UNUSED(cat)
                auto eff = creator();
                if (!eff) {
                    throw std::runtime_error("Menu creator produced null effect: " + name.toStdString());
                }
                count++;
            });

        if (count < 20) {
            throw std::runtime_error("Expected at least 20 core effects in menu, found " + std::to_string(count));
        }
    });

    // Test 4: Chinese translation resource load test
    runTest("Test 4: Chinese (zh_CN) Translation Loading", [&]() {
        QTranslator translator;
        const bool loaded = translator.load(":/translations/friction_zh_CN.qm");
        if (!loaded) {
            throw std::runtime_error("Failed to load :/translations/friction_zh_CN.qm resource");
        }
    });

    // Test 5: ThemeSupport presets, accents and style generation test
    runTest("Test 5: ThemeSupport presets and styling", [&]() {
        const auto &presets = ThemeSupport::themePresetList();
        if (presets.size() < 10) {
            throw std::runtime_error("Expected at least 10 theme presets, got " + std::to_string(presets.size()));
        }

        const auto &accents = ThemeSupport::accentPresetList();
        if (accents.size() < 10) {
            throw std::runtime_error("Expected at least 10 accent presets, got " + std::to_string(accents.size()));
        }

        // Test theme switching
        ThemeSupport::setThemeFromId(QStringLiteral("morandi_dark"));
        if (ThemeSupport::themeId() != QStringLiteral("morandi_dark")) {
            throw std::runtime_error("Theme ID mismatch for morandi_dark");
        }
        if (!ThemeSupport::getThemeBaseColor().isValid()) {
            throw std::runtime_error("Invalid base color for morandi_dark");
        }
        if (!ThemeSupport::getThemeHighlightColor().isValid()) {
            throw std::runtime_error("Invalid highlight color for morandi_dark");
        }

        // Test custom radius and scrollbar
        ThemeSupport::setBorderRadius(8);
        if (ThemeSupport::borderRadius() != 8) {
            throw std::runtime_error("Failed to set border radius to 8");
        }
        ThemeSupport::setScrollbarWidth(6);
        if (ThemeSupport::scrollbarWidth() != 6) {
            throw std::runtime_error("Failed to set scrollbar width to 6");
        }

        const QString style = ThemeSupport::getThemeStyle(20);
        if (style.isEmpty()) {
            throw std::runtime_error("Generated theme style string is empty");
        }

        // Restore friction theme
        ThemeSupport::setThemeFromId(QStringLiteral("friction"));
    });

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Unit Test Summary: " << passed << " passed, " << failed << " failed." << std::endl;
    std::cout << "=========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}
