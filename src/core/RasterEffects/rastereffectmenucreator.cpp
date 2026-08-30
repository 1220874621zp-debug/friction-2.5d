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
    add(QObject::tr("Shadow"), "", []() { return enve::make_shared<ShadowEffect>(); });
    add(QObject::tr("Motion Blur"), "", []() { return enve::make_shared<MotionBlurEffect>(); });
    add(QObject::tr("Brightness-Contrast"), QObject::tr("Color"),
        []() { return enve::make_shared<BrightnessContrastEffect>(); });
    add(QObject::tr("Colorize"), QObject::tr("Color"),
        []() { return enve::make_shared<ColorizeEffect>(); });
    add(QObject::tr("Chroma Key"), QObject::tr("Color"),
        []() { return enve::make_shared<ChromaKeyEffect>(); });
    add(QObject::tr("Liquid Glass"), QObject::tr("Distort"),
        []() { return enve::make_shared<LiquidGlassEffect>(); });
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
