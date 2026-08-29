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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "rastereffectmenucreator.h"

#include "RasterEffects/rastereffectsinclude.h"
#include "RasterEffects/customrastereffectcreator.h"
#include "ShaderEffects/shadereffectcreator.h"
#include "ShaderEffects/shadereffect.h"

void RasterEffectMenuCreator::forEveryEffect(const EffectAdder& add) {

    forEveryEffectCore(add);
    forEveryEffectCustom(add);
    forEveryEffectShader(add);
}

void RasterEffectMenuCreator::forEveryEffectCore(const EffectAdder &add)
{
    add(QObject::tr("Blur"), "", []() { return enve::make_shared<BlurEffect>(); });
    add(QObject::tr("Directional Blur"), QObject::tr("Blur"),
        []() { return enve::make_shared<DirectionalBlurEffect>(); });
    add(QObject::tr("Radial Blur"), QObject::tr("Blur"),
        []() { return enve::make_shared<RadialBlurEffect>(); });
    add(QObject::tr("Shadow"), "", []() { return enve::make_shared<ShadowEffect>(); });
    add(QObject::tr("Motion Blur"), "", []() { return enve::make_shared<MotionBlurEffect>(); });
    add(QObject::tr("Brightness-Contrast"), QObject::tr("Color"),
        []() { return enve::make_shared<BrightnessContrastEffect>(); });
    add(QObject::tr("Colorize"), QObject::tr("Color"),
        []() { return enve::make_shared<ColorizeEffect>(); });
    add(QObject::tr("Invert"), QObject::tr("Color"),
        []() { return enve::make_shared<InvertEffect>(); });
    add(QObject::tr("Tint"), QObject::tr("Color"),
        []() { return enve::make_shared<TintEffect>(); });
    add(QObject::tr("Glow"), QObject::tr("Light"),
        []() { return enve::make_shared<GlowEffect>(); });
    add(QObject::tr("Chromatic Aberration"), QObject::tr("Distort"),
        []() { return enve::make_shared<ChromaticAberrationEffect>(); });
    add(QObject::tr("Wave Warp"), QObject::tr("Distort"),
        []() { return enve::make_shared<WaveWarpEffect>(); });
    add(QObject::tr("Mirror"), QObject::tr("Distort"),
        []() { return enve::make_shared<MirrorEffect>(); });
    add(QObject::tr("Vignette"), QObject::tr("Stylize"),
        []() { return enve::make_shared<VignetteEffect>(); });
    add(QObject::tr("Letterbox"), QObject::tr("Stylize"),
        []() { return enve::make_shared<LetterboxEffect>(); });
    add(QObject::tr("Scanlines"), QObject::tr("Stylize"),
        []() { return enve::make_shared<ScanlinesEffect>(); });
    add(QObject::tr("Edge Detect"), QObject::tr("Stylize"),
        []() { return enve::make_shared<EdgeDetectEffect>(); });
    add(QObject::tr("Pixelate"), QObject::tr("Stylize"),
        []() { return enve::make_shared<PixelateEffect>(); });
    add(QObject::tr("Noise"), QObject::tr("Stylize"),
        []() { return enve::make_shared<NoiseEffect>(); });
    add(QObject::tr("Rain"), QObject::tr("Simulation"),
        []() { return enve::make_shared<RainEffect>(); });
    add(QObject::tr("Wipe"), QObject::tr("Transitions"),
        []() { return enve::make_shared<WipeEffect>(); });
    add(QObject::tr("Noise Fade"), QObject::tr("Transitions"),
        []() { return enve::make_shared<NoiseFadeEffect>(); });
}

void RasterEffectMenuCreator::forEveryEffectCustom(const EffectAdder &add)
{
    CustomRasterEffectCreator::sForEveryEffect(add);
}

void RasterEffectMenuCreator::forEveryEffectShader(const EffectAdder &add)
{
    ShaderEffectCreator::sForEveryEffect(add);
}
