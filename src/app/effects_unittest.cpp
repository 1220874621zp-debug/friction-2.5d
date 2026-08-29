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
            RasterEffectType::MIRROR
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
            RasterEffectType::MIRROR
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

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Unit Test Summary: " << passed << " passed, " << failed << " failed." << std::endl;
    std::cout << "=========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}
