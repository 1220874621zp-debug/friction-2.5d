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
#include <QFile>
#include <QDir>
#include <iostream>
#include <cassert>
#include <cstring>

#include "RasterEffects/rastereffectsinclude.h"
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "Properties/comboboxproperty.h"
#include "Psd/psdfile.h"
#include "include/core/SkBitmap.h"

#include "themesupport.h"
#include "AI/mcpserver.h"
#include "AI/mcpdispatcher.h"
#include "textanimpresets.h"
#include "layeranimpresets.h"
#include "GUI/mainwindow.h"
#include "GUI/canvaswindow.h"
#include "GUI/timelinedockwidget.h"
#include "GUI/RenderWidgets/renderwidget.h"
#include "GUI/RenderWidgets/renderinstancewidget.h"
#include "renderinstancesettings.h"
#include "renderhandler.h"

// Headless test stubs for McpDispatcher GUI references
RenderHandler *RenderHandler::sInstance = nullptr;
TimelineDockWidget *MainWindow::getTimeLineWidget() { return nullptr; }
void TimelineDockWidget::spaceToggle() {}
Canvas *CanvasWindow::getCurrentCanvas() { return nullptr; }
void CanvasWindow::fitCanvasToSize(const bool&) {}
void CanvasWindow::setRulersVisible(bool) {}
RenderInstanceWidget *RenderWidget::addCanvasRenderInstance(Canvas *) { return nullptr; }
void RenderWidget::renderOnly(RenderInstanceWidget *) {}
int RenderWidget::count() { return 0; }
RenderWidget *MainWindow::renderWidget() const { return nullptr; }
// never actually dereferenced: addCanvasRenderInstance returns nullptr,
// so the dispatcher render path bails before touching the settings
RenderInstanceSettings &RenderInstanceWidget::getSettings() {
    static alignas(RenderInstanceSettings) char buf[sizeof(RenderInstanceSettings)];
    return reinterpret_cast<RenderInstanceSettings&>(buf);
}
void MainWindow::toggleTopViewWindow() {}
bool MainWindow::isTopViewVisible() const { return false; }
const QMetaObject MainWindow::staticMetaObject = QMainWindow::staticMetaObject;
const QMetaObject CanvasWindow::staticMetaObject = GLWindow::staticMetaObject;

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
            RasterEffectType::LAYER_STYLES,
            RasterEffectType::PAGE_CURL
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
            RasterEffectType::PIXEL_ART,
            RasterEffectType::PAGE_CURL
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
        int effects = 0;
        for (const auto& rec : psd.layers()) {
            if (!rec.stylesList.isEmpty()) { styled++; }
            effects += rec.stylesList.size();
        }
        std::cout << " layers=" << psd.layers().size()
                  << " styled=" << styled
                  << " effects=" << effects << " ";
        if (styled < 1) {
            throw std::runtime_error("expected at least one styled layer");
        }
    });

    // Test 5c: synthetic multi-instance PSD (2 shadows + 2 glows +
    // 2 strokes in lmfx, plus an lfx2 mirror that must be ignored).
    // Hand-built bytes: locks the '*Multi' key spellings, the lmfx
    // authority rule and the instance-to-effect assembly end to end
    runTest("Test 5c: synthetic multi-instance PSD", [&]() {
        struct Dw {
            QByteArray b;
            void u8v(const quint8 v) { b.append(char(v)); }
            void u16v(const quint16 v) {
                u8v(quint8(v >> 8)); u8v(quint8(v));
            }
            void u32v(const quint32 v) {
                for (int i = 3; i >= 0; i--) { u8v(quint8(v >> (8 * i))); }
            }
            void i32v(const qint32 v) { u32v(quint32(v)); }
            void i16v(const qint16 v) { u16v(quint16(v)); }
            void f64v(const double v) {
                quint64 bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                for (int i = 7; i >= 0; i--) { u8v(quint8(bits >> (8 * i))); }
            }
            void raw(const char* const s, const int n) { b.append(s, n); }
            void id(const char* const s) {
                const int n = int(qstrlen(s));
                if (n == 4) { i32v(0); raw(s, 4); }
                else { i32v(n); raw(s, n); }
            }
            void unit(const char* const unitKey, const double v) {
                raw("UntF", 4); raw(unitKey, 4); f64v(v);
            }
            void boolean(const bool v) { raw("bool", 4); u8v(v ? 1 : 0); }
            void enumV(const char* const type, const char* const value) {
                raw("enum", 4); id(type); id(value);
            }
            void color(const double r, const double g, const double bl) {
                raw("Objc", 4); i32v(0); id("RGBC"); i32v(3);
                id("Rd  "); raw("doub", 4); f64v(r);
                id("Grn "); raw("doub", 4); f64v(g);
                id("Bl  "); raw("doub", 4); f64v(bl);
            }
        };

        const auto shadowObj = [&](const double dist) {
            Dw w;
            w.raw("Objc", 4); w.i32v(0); w.id("DrSh"); w.i32v(7);
            w.id("enab"); w.boolean(true);
            w.id("Opct"); w.unit("#Prc", 40.0);
            w.id("lagl"); w.unit("#Ang", 90.0);
            w.id("Dstn"); w.unit("#Pxl", dist);
            w.id("Ckmt"); w.unit("#Prc", 0.0);
            w.id("blur"); w.unit("#Pxl", 5.0);
            w.id("Clr "); w.color(61.0, 27.0, 5.0);
            return w.b;
        };
        const auto glowObj = [&](const double size) {
            Dw w;
            w.raw("Objc", 4); w.i32v(0); w.id("OrGl"); w.i32v(5);
            w.id("enab"); w.boolean(true);
            w.id("Opct"); w.unit("#Prc", 30.0);
            w.id("Ckmt"); w.unit("#Prc", 0.0);
            w.id("blur"); w.unit("#Pxl", size);
            w.id("Clr "); w.color(0.0, 255.0, 24.0);
            return w.b;
        };
        const auto strokeObj = [&](const double size) {
            Dw w;
            w.raw("Objc", 4); w.i32v(0); w.id("FrFX"); w.i32v(6);
            w.id("enab"); w.boolean(true);
            w.id("Opct"); w.unit("#Prc", 100.0);
            w.id("Sz  "); w.unit("#Pxl", size);
            w.id("Styl"); w.enumV("FTst", "FStF");
            w.id("Md  "); w.enumV("BlnM", "Nrml");
            w.id("Clr "); w.color(255.0, 0.0, 0.0);
            return w.b;
        };

        // lmfx: authoritative, two instances of each type
        Dw lmfxW;
        lmfxW.i32v(0); lmfxW.id("null"); lmfxW.i32v(5);
        lmfxW.id("Scl "); lmfxW.unit("#Prc", 100.0);
        lmfxW.id("masterFXSwitch"); lmfxW.boolean(true);
        lmfxW.id("dropShadowMulti");
        lmfxW.raw("VlLs", 4); lmfxW.i32v(2);
        lmfxW.b.append(shadowObj(20.0)); lmfxW.b.append(shadowObj(50.0));
        lmfxW.id("outerGlowMulti");
        lmfxW.raw("VlLs", 4); lmfxW.i32v(2);
        lmfxW.b.append(glowObj(18.0)); lmfxW.b.append(glowObj(36.0));
        lmfxW.id("frameFXMulti");
        lmfxW.raw("VlLs", 4); lmfxW.i32v(2);
        lmfxW.b.append(strokeObj(3.0)); lmfxW.b.append(strokeObj(8.0));

        // lfx2 mirror: single shadow with a DIFFERENT distance - the
        // assertion on dist proves the mirror was overridden
        Dw lfx2W;
        lfx2W.i32v(0); lfx2W.id("null"); lfx2W.i32v(3);
        lfx2W.id("Scl "); lfx2W.unit("#Prc", 100.0);
        lfx2W.id("masterFXSwitch"); lfx2W.boolean(true);
        lfx2W.id("DrSh"); lfx2W.b.append(shadowObj(5.0));

        Dw lmfxBlock;
        lmfxBlock.raw("8BIM", 4); lmfxBlock.raw("lmfx", 4);
        lmfxBlock.u32v(lmfxW.b.size() + 8);
        lmfxBlock.i32v(0); lmfxBlock.i32v(16);
        lmfxBlock.b.append(lmfxW.b);

        Dw lfx2Block;
        lfx2Block.raw("8BIM", 4); lfx2Block.raw("lfx2", 4);
        lfx2Block.u32v(lfx2W.b.size() + 8);
        lfx2Block.i32v(0); lfx2Block.i32v(16);
        lfx2Block.b.append(lfx2W.b);

        // minimal 1-layer PSD carrying both blocks
        Dw psd;
        psd.raw("8BPS", 4); psd.u16v(1);
        for (int i = 0; i < 6; i++) { psd.u8v(0); }
        psd.u16v(3);                    // channels
        psd.i32v(1); psd.i32v(1);       // h, w
        psd.u16v(8); psd.u16v(3);       // depth, RGB
        psd.u32v(0);                    // color mode data
        psd.u32v(0);                    // image resources
        Dw li;
        li.i16v(1);                     // layer count
        li.i32v(0); li.i32v(0); li.i32v(1); li.i32v(1);  // rect
        li.u16v(1);                     // channel count
        li.i16v(0); li.u32v(2);         // channel id 0 (i16), len 2
        li.raw("8BIM", 4); li.raw("norm", 4);
        li.u8v(255); li.u8v(0); li.u8v(0); li.u8v(0);
        Dw extra;
        extra.u32v(0);                  // mask size
        extra.u32v(0);                  // blending ranges
        extra.u8v(1); extra.raw("a", 1);
        extra.u8v(0); extra.u8v(0);
        extra.b.append(lfx2Block.b);
        extra.b.append(lmfxBlock.b);
        li.u32v(extra.b.size()); li.b.append(extra.b);
        if (li.b.size() & 1) { li.u8v(0); }
        Dw lm;
        lm.u32v(li.b.size() + 4); lm.u32v(li.b.size());
        lm.b.append(li.b);
        psd.b.append(lm.b);
        psd.u16v(1); psd.u16v(0);       // channel data stub
        psd.u16v(1); psd.u16v(0);       // image data stub

        const QString tmpPath = QDir::temp().absoluteFilePath(
                    QStringLiteral("friction_lfx_test.psd"));
        {
            QFile f(tmpPath);
            if (!f.open(QIODevice::WriteOnly)) {
                throw std::runtime_error("cannot write temp psd");
            }
            f.write(psd.b);
        }

        psd::PsdFile psdFile;
        QString err;
        if (!psdFile.load(tmpPath, &err)) {
            throw std::runtime_error(("load failed: " + err).toStdString());
        }
        if (psdFile.layers().isEmpty()) {
            throw std::runtime_error("no layers parsed");
        }
        const auto& rec = psdFile.layers().first();
        std::cout << " effects=" << rec.stylesList.size() << " ";
        if (rec.stylesList.size() != 4) {
            throw std::runtime_error("expected 4 style effects (main + 3 extras)");
        }
        if (!rec.stylesFromLmfx) {
            throw std::runtime_error("lmfx authority flag not set");
        }
        const auto& main = rec.stylesList.first();
        if (!main.shadowEnabled || !main.glowEnabled || !main.strokeEnabled) {
            throw std::runtime_error("main effect missing a style");
        }
        // the mirror said dist 5, lmfx says 20
        if (qAbs(main.shadowDistance - 20.0) > 0.01) {
            throw std::runtime_error("lfx2 mirror was not overridden by lmfx");
        }
        for (int i = 1; i < rec.stylesList.size(); i++) {
            const auto& e = rec.stylesList.at(i);
            const int on = int(e.shadowEnabled) + int(e.glowEnabled)
                           + int(e.strokeEnabled);
            if (on != 1) {
                throw std::runtime_error("extra effect is not solo");
            }
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

    // Test 7: Page Curl CPU math (identity at zero progress, curl at mid)
    runTest("Test 7: Page Curl CPU math", [&]() {
        const auto eff = createRasterEffectForNonCustomType(
                RasterEffectType::PAGE_CURL);
        if (!eff) { throw std::runtime_error("Factory returned null"); }
        // the effect defaults to wave mode; pin curl mode for these checks
        eff->ca_getChildAt<ComboBoxProperty>(0)->setCurrentValue(0);

        SkBitmap src;
        src.allocN32Pixels(128, 128);
        src.eraseARGB(255, 200, 100, 50);

        const auto render = [&](SkBitmap& dst) {
            dst.allocN32Pixels(128, 128);
            dst.eraseARGB(0, 0, 0, 0);
            const auto caller = eff->getEffectCaller(0.0, 1.0, 1.0, nullptr);
            if (!caller) { throw std::runtime_error("null caller"); }
            CpuRenderTools tools{src, dst};
            CpuRenderData data;
            data.fTexTile = SkIRect::MakeXYWH(0, 0, 128, 128);
            caller->processCpu(tools, data);
        };

        // identity: progress 0 must be an exact passthrough
        {
            SkBitmap dst;
            render(dst);
            for (int y = 0; y < 128; y += 5) {
                for (int x = 0; x < 128; x += 5) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) != 255 ||
                        qAbs(int(SkColorGetR(c)) - 200) > 2 ||
                        qAbs(int(SkColorGetG(c)) - 100) > 2 ||
                        qAbs(int(SkColorGetB(c)) - 50) > 2) {
                        throw std::runtime_error("progress 0 is not identity");
                    }
                }
            }
        }

        // mid progress, default direction (right edge rolls leftward):
        // the consumed far-right side empties, the kept left side stays
        // opaque and shaded, and the back face shows near the tube
        const auto prog = eff->ca_getChildAt<QrealAnimator>(1);
        if (!prog) { throw std::runtime_error("no progress animator"); }
        prog->setCurrentBaseValue(50.0);
        {
            SkBitmap dst;
            render(dst);
            // far right: page has left
            if (SkColorGetA(dst.getColor(126, 64)) != 0) {
                throw std::runtime_error("consumed side is not transparent");
            }
            // far left: still opaque, shaded darker than the source
            const SkColor c = dst.getColor(6, 64);
            if (SkColorGetA(c) != 255) {
                throw std::runtime_error("kept side lost opacity");
            }
            if (SkColorGetR(c) >= 200 || SkColorGetR(c) < 60) {
                throw std::runtime_error("kept side not lit/shaded");
            }
            // near the tube the flipped back face (gray) must show:
            // some pixel there has blue >= red, unlike the orange front
            bool sawBack = false;
            for (int y = 20; y < 108 && !sawBack; y += 4) {
                for (int x = 30; x < 66; x += 2) {
                    const SkColor b = dst.getColor(x, y);
                    if (SkColorGetA(b) > 200 &&
                        SkColorGetB(b) >= SkColorGetR(b)) {
                        sawBack = true;
                        break;
                    }
                }
            }
            if (!sawBack) {
                throw std::runtime_error("back face never visible");
            }
        }
        prog->setCurrentBaseValue(0.0);

        // wave mode: the image must stay fully visible - amplitude 0 is
        // an exact passthrough, amplitude 8 keeps every pixel opaque and
        // shaded (no transparency anywhere)
        const auto mode = eff->ca_getChildAt<ComboBoxProperty>(0);
        const auto amp = eff->ca_getChildAt<QrealAnimator>(10);
        if (!mode || !amp) { throw std::runtime_error("no mode/amp animator"); }
        mode->setCurrentValue(1);
        amp->setCurrentBaseValue(0.0);
        {
            SkBitmap dst;
            render(dst);
            const SkColor c = dst.getColor(64, 64);
            if (SkColorGetA(c) != 255 ||
                qAbs(int(SkColorGetR(c)) - 200) > 2 ||
                qAbs(int(SkColorGetG(c)) - 100) > 2 ||
                qAbs(int(SkColorGetB(c)) - 50) > 2) {
                throw std::runtime_error("wave amp 0 is not identity");
            }
        }
        amp->setCurrentBaseValue(8.0);
        {
            SkBitmap dst;
            render(dst);
            for (int y = 0; y < 128; y += 5) {
                for (int x = 0; x < 128; x += 5) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) != 255) {
                        throw std::runtime_error("wave mode lost opacity");
                    }
                }
            }
            const SkColor a = dst.getColor(10, 64);
            const SkColor b = dst.getColor(118, 64);
            if (a == b) {
                throw std::runtime_error("wave shading has no variation");
            }
        }
        // crossed wave keeps everything opaque too
        eff->ca_getChildAt<QrealAnimator>(16)->setCurrentBaseValue(50.0);
        {
            SkBitmap dst;
            render(dst);
            const SkColor c = dst.getColor(64, 10);
            if (SkColorGetA(c) != 255) {
                throw std::runtime_error("crossed wave lost opacity");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(16)->setCurrentBaseValue(0.0);
        mode->setCurrentValue(0);
        amp->setCurrentBaseValue(0.0);

        // page turn (mode 2): mid progress shows the flipped back and
        // keeps a large opaque area; full progress lies flat mirrored
        mode->setCurrentValue(2);
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(50.0); // progress
        {
            SkBitmap dst;
            render(dst);
            int opaque = 0;
            int backish = 0;
            for (int y = 0; y < 128; y += 6) {
                for (int x = 0; x < 128; x += 6) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) == 255) opaque++;
                    if (SkColorGetA(c) == 255 &&
                        qAbs(int(SkColorGetB(c)) - int(SkColorGetR(c))) < 12 &&
                        SkColorGetR(c) > 100) backish++;
                }
            }
            if (opaque < 100) {
                throw std::runtime_error("page turn lost the image");
            }
            if (backish < 8) {
                throw std::runtime_error("page turn back never visible");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(100.0);
        {
            SkBitmap dst;
            render(dst);
            int opaque = 0;
            for (int y = 0; y < 128; y += 6) {
                for (int x = 0; x < 128; x += 6) {
                    if (SkColorGetA(dst.getColor(x, y)) == 255) opaque++;
                }
            }
            if (opaque < 300) {
                throw std::runtime_error("full turn does not lie flat");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(0.0);

        // slant + perspective + spiral on curl mode: no crash, output
        // stays bounded and the far side still empties
        mode->setCurrentValue(0);
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(50.0);
        eff->ca_getChildAt<QrealAnimator>(13)->setCurrentBaseValue(60.0); // slant
        eff->ca_getChildAt<QrealAnimator>(14)->setCurrentBaseValue(40.0); // perspective
        eff->ca_getChildAt<QrealAnimator>(15)->setCurrentBaseValue(30.0); // spiral
        {
            SkBitmap dst;
            render(dst);
            int opaque = 0;
            for (int y = 0; y < 128; y += 6) {
                for (int x = 0; x < 128; x += 6) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) == 255 &&
                        (SkColorGetR(c) > 250 || SkColorGetG(c) > 250)) {
                        throw std::runtime_error("slant/perspective/spline produced unclamped output");
                    }
                    if (SkColorGetA(c) == 255) opaque++;
                }
            }
            if (opaque < 80) {
                throw std::runtime_error("slant/perspective destroyed the image");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(13)->setCurrentBaseValue(0.0);
        eff->ca_getChildAt<QrealAnimator>(14)->setCurrentBaseValue(0.0);
        eff->ca_getChildAt<QrealAnimator>(15)->setCurrentBaseValue(0.0);
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(0.0);

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

    // Test 6: AI MCP Tool Dispatcher & Schema validation
    runTest("Test 6: AI MCP Tool Dispatcher & Schema validation", [&]() {
        Friction::AI::McpDispatcher dispatcher;
        const auto schema = dispatcher.getToolsSchema();
        if (schema.isEmpty()) {
            throw std::runtime_error("McpDispatcher tools schema is empty");
        }

        bool hasSceneInfo = false;
        bool hasCreateLayer = false;
        bool hasSetKeyframe = false;
        bool hasSetKeyframeEasing = false;
        bool hasSetInOutPoint = false;
        bool hasSetLayerOrder = false;
        bool hasEvalScript = false;
        bool hasCapture = false;
        bool hasKeyframeEasingParam = false;
        bool hasRenderMarkup = false;
        bool hasUpdateLayer = false;
        bool hasAnimateLayer = false;
        bool hasGetStoryboard = false;

        for (const auto &val : schema) {
            const auto obj = val.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString();
            if (name == QStringLiteral("friction_get_scene_info")) hasSceneInfo = true;
            if (name == QStringLiteral("friction_create_layer")) hasCreateLayer = true;
            if (name == QStringLiteral("friction_set_keyframe")) {
                hasSetKeyframe = true;
                const auto inputSchema = obj.value(QStringLiteral("inputSchema")).toObject();
                const auto props = inputSchema.value(QStringLiteral("properties")).toObject();
                if (props.contains(QStringLiteral("easing"))) hasKeyframeEasingParam = true;
            }
            if (name == QStringLiteral("friction_set_keyframe_easing")) hasSetKeyframeEasing = true;
            if (name == QStringLiteral("friction_set_in_out_point")) hasSetInOutPoint = true;
            if (name == QStringLiteral("friction_set_layer_order")) hasSetLayerOrder = true;
            if (name == QStringLiteral("friction_eval_script")) hasEvalScript = true;
            if (name == QStringLiteral("friction_capture_viewport")) hasCapture = true;
            if (name == QStringLiteral("friction_render_markup")) hasRenderMarkup = true;
            if (name == QStringLiteral("friction_update_layer")) hasUpdateLayer = true;
            if (name == QStringLiteral("friction_animate_layer")) hasAnimateLayer = true;
            if (name == QStringLiteral("friction_get_storyboard")) hasGetStoryboard = true;
        }

        if (!hasSceneInfo || !hasCreateLayer || !hasSetKeyframe || !hasEvalScript || !hasCapture) {
            throw std::runtime_error("Required MCP tools missing from schema");
        }
        if (!hasSetKeyframeEasing || !hasSetInOutPoint || !hasSetLayerOrder) {
            throw std::runtime_error("Newly added MCP animation tools missing from schema");
        }
        if (!hasKeyframeEasingParam) {
            throw std::runtime_error("friction_set_keyframe schema missing 'easing' parameter");
        }
        if (!hasRenderMarkup || !hasUpdateLayer || !hasAnimateLayer || !hasGetStoryboard) {
            throw std::runtime_error("Advanced AI orchestration tools (markup, update, animate, storyboard) missing from schema");
        }

        // Test tool dispatcher error handling / execution path for newly registered tools
        QJsonObject dummyArgs;
        dummyArgs[QStringLiteral("index")] = 1;
        const auto respEasing = dispatcher.dispatchTool(QStringLiteral("friction_set_keyframe_easing"), dummyArgs);
        if (!respEasing.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_set_keyframe_easing dispatch response malformed");
        }

        const auto respInOut = dispatcher.dispatchTool(QStringLiteral("friction_set_in_out_point"), dummyArgs);
        if (!respInOut.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_set_in_out_point dispatch response malformed");
        }

        dummyArgs[QStringLiteral("order")] = QStringLiteral("top");
        const auto respOrder = dispatcher.dispatchTool(QStringLiteral("friction_set_layer_order"), dummyArgs);
        if (!respOrder.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_set_layer_order dispatch response malformed");
        }

        // Test update_layer dispatch
        QJsonObject updateArgs;
        updateArgs[QStringLiteral("name")] = QStringLiteral("NonExistentLayer");
        updateArgs[QStringLiteral("opacity")] = 50.0;
        const auto respUpdate = dispatcher.dispatchTool(QStringLiteral("friction_update_layer"), updateArgs);
        if (!respUpdate.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_update_layer dispatch response malformed");
        }

        // Test animate_layer dispatch
        QJsonObject animArgs;
        animArgs[QStringLiteral("name")] = QStringLiteral("NonExistentLayer");
        animArgs[QStringLiteral("preset")] = QStringLiteral("pop");
        const auto respAnim = dispatcher.dispatchTool(QStringLiteral("friction_animate_layer"), animArgs);
        if (!respAnim.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_animate_layer dispatch response malformed");
        }

        // Test render_markup dispatch validation (empty markup returns error)
        QJsonObject markupArgs;
        markupArgs[QStringLiteral("markup")] = QStringLiteral("");
        const auto respMarkup = dispatcher.dispatchTool(QStringLiteral("friction_render_markup"), markupArgs);
        if (respMarkup.value(QStringLiteral("success")).toBool() != false) {
            throw std::runtime_error("friction_render_markup should fail gracefully on empty markup");
        }

        // Test storyboard dispatch (handled without crash when window absent in test harness)
        QJsonObject sbArgs;
        sbArgs[QStringLiteral("numFrames")] = 3;
        const auto respSb = dispatcher.dispatchTool(QStringLiteral("friction_get_storyboard"), sbArgs);
        if (!respSb.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_get_storyboard dispatch response malformed");
        }
    });

    // Test 7: AI MCP Server JSON-RPC Protocol Parser
    runTest("Test 7: AI MCP Server JSON-RPC Protocol Parser", [&]() {
        Friction::AI::McpServer server;

        // Test initialize
        QJsonObject initReq;
        initReq[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        initReq[QStringLiteral("id")] = 1;
        initReq[QStringLiteral("method")] = QStringLiteral("initialize");

        const auto initResp = server.processJsonRpc(initReq);
        if (initResp.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
            throw std::runtime_error("Invalid jsonrpc version in response");
        }
        if (initResp.value(QStringLiteral("id")).toInt() != 1) {
            throw std::runtime_error("Mismatch response id in initialize");
        }
        const auto resObj = initResp.value(QStringLiteral("result")).toObject();
        if (!resObj.contains(QStringLiteral("serverInfo"))) {
            throw std::runtime_error("serverInfo missing in initialize result");
        }

        // Test ping
        QJsonObject pingReq;
        pingReq[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        pingReq[QStringLiteral("id")] = 2;
        pingReq[QStringLiteral("method")] = QStringLiteral("ping");

        const auto pingResp = server.processJsonRpc(pingReq);
        if (pingResp.value(QStringLiteral("id")).toInt() != 2) {
            throw std::runtime_error("Mismatch response id in ping");
        }

        // Test tools/list
        QJsonObject listReq;
        listReq[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        listReq[QStringLiteral("id")] = 3;
        listReq[QStringLiteral("method")] = QStringLiteral("tools/list");

        const auto listResp = server.processJsonRpc(listReq);
        const auto listResult = listResp.value(QStringLiteral("result")).toObject();
        if (!listResult.contains(QStringLiteral("tools")) || !listResult.value(QStringLiteral("tools")).isArray()) {
            throw std::runtime_error("Invalid tools array in tools/list result");
        }
    });

    // Test 8: Kinetic Text & Layer Animation Presets
    runTest("Test 8: Kinetic Text & Layer Animation Presets", [&]() {
        const auto& textPresets = TextAnimPresets::all();
        if (textPresets.size() < 160) {
            throw std::runtime_error(QString("Text presets count too low: %1 (expected >= 160)").arg(textPresets.size()).toStdString());
        }

        const auto& layerPresets = LayerAnimPresets::all();
        if (layerPresets.size() < 60) {
            throw std::runtime_error(QString("Layer presets count too low: %1 (expected >= 60)").arg(layerPresets.size()).toStdString());
        }

        const int totalPresets = textPresets.size() + layerPresets.size();
        if (totalPresets < 220) {
            throw std::runtime_error(QString("Total presets count too low: %1 (expected >= 220)").arg(totalPresets).toStdString());
        }

        // Verify key text presets from each archetype exist and have valid fields
        const QStringList keyTextIds = {
            "sharp-snap-rise", "sharp-elastic-pop", "sharp-blade-cut",
            "sharp-double-bounce", "sharp-jelly-squash", "sharp-trampoline",
            "smooth-float-rise", "smooth-cinematic-fade", "smooth-aurora",
            "smooth-par-float", "smooth-bloom-slow",
            "prop-pos-x-left", "prop-scale-uniform", "prop-rot-full-360", "prop-shear-slash-x",
            "prop-scale-wide-8x",
            "3d-flip-y-cw", "3d-corkscrew", "3d-barrel-roll", "3d-door-swing-left",
            "tech-typewriter-std", "tech-number-roll", "tech-matrix-rain",
            "tech-binary-matrix", "tech-crt-scan",
            "loop-sine-wave", "loop-breathe-soft", "loop-heartbeat", "loop-rainbow-wave"
        };
        for (const auto& id : keyTextIds) {
            const auto p = TextAnimPresets::byId(id);
            if (!p) {
                throw std::runtime_error(QString("Missing key text preset: %1").arg(id).toStdString());
            }
            if (p->name.isEmpty() || p->duration <= 0.0 || p->tag.isEmpty()) {
                throw std::runtime_error(QString("Invalid data in text preset: %1").arg(id).toStdString());
            }
        }

        // Verify key text presets have diverse and distinct physical easings
        const auto snapPreset = TextAnimPresets::byId("sharp-snap-rise");
        if (!snapPreset || snapPreset->easing != TextEasing::sharpSnap) {
            throw std::runtime_error("sharp-snap-rise missing sharpSnap easing");
        }
        const auto bouncePreset = TextAnimPresets::byId("sharp-overshoot-down");
        if (!bouncePreset || bouncePreset->easing != TextEasing::bounce) {
            throw std::runtime_error("sharp-overshoot-down missing bounce easing");
        }
        const auto elasticPreset = TextAnimPresets::byId("sharp-elastic-pop");
        if (!elasticPreset || elasticPreset->easing != TextEasing::elastic) {
            throw std::runtime_error("sharp-elastic-pop missing elastic easing");
        }
        const auto anticipatePreset = TextAnimPresets::byId("3d-corkscrew");
        if (!anticipatePreset || anticipatePreset->easing != TextEasing::anticipate) {
            throw std::runtime_error("3d-corkscrew missing anticipate easing");
        }
        const auto steppedPreset = TextAnimPresets::byId("tech-typewriter-std");
        if (!steppedPreset || steppedPreset->easing != TextEasing::stepped) {
            throw std::runtime_error("tech-typewriter-std missing stepped easing");
        }

        // Verify TextEffect setups physics correctly
        const auto effect = enve::make_shared<TextEffect>();
        effect->setupFromPreset(*elasticPreset, 200.0, 48.0, 0, 30.0, 1.0);
        if (!effect->hasCustomPhysics()) {
            throw std::runtime_error("TextEffect failed to initialize custom physics");
        }
        if (effect->getEasing() != TextEasing::elastic) {
            throw std::runtime_error("TextEffect easing mismatch");
        }

        // Verify key layer presets exist and have valid generators
        const QStringList keyLayerIds = {
            "l-fade", "l-pop", "l-drop", "l-flip-x",
            "l-swing", "l-skew-slide", "l-elastic-scale", "l-orbit-3d",
            "l-door-open-l", "l-dive-3d", "l-jelly-wobble", "l-heavy-stamp-jitter",
            "l-sheet-slide-up", "l-glitch-shake", "l-heartbeat-layer"
        };
        for (const auto& id : keyLayerIds) {
            const auto p = LayerAnimPresets::byId(id);
            if (!p) {
                throw std::runtime_error(QString("Missing key layer preset: %1").arg(id).toStdString());
            }
            if (p->name.isEmpty() || p->duration <= 0.0 || (!p->gen && !p->outGen)) {
                throw std::runtime_error(QString("Invalid data or missing generator in layer preset: %1").arg(id).toStdString());
            }
        }
    });

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Unit Test Summary: " << passed << " passed, " << failed << " failed." << std::endl;
    std::cout << "=========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}
