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
#include "include/core/SkBitmap.h"

#include "themesupport.h"
#include "AI/mcpserver.h"
#include "AI/mcpdispatcher.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
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
            RasterEffectType::CHROMA_KEY
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
        bool hasEvalScript = false;
        bool hasCapture = false;

        for (const auto &val : schema) {
            const auto obj = val.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString();
            if (name == QStringLiteral("friction_get_scene_info")) hasSceneInfo = true;
            if (name == QStringLiteral("friction_create_layer")) hasCreateLayer = true;
            if (name == QStringLiteral("friction_set_keyframe")) hasSetKeyframe = true;
            if (name == QStringLiteral("friction_eval_script")) hasEvalScript = true;
            if (name == QStringLiteral("friction_capture_viewport")) hasCapture = true;
        }

        if (!hasSceneInfo || !hasCreateLayer || !hasSetKeyframe || !hasEvalScript || !hasCapture) {
            throw std::runtime_error("Required MCP tools missing from schema");
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

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Unit Test Summary: " << passed << " passed, " << failed << " failed." << std::endl;
    std::cout << "=========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}
