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
    add(QObject::tr("Channel Blur"), QObject::tr("Blur"),
        []() { return enve::make_shared<ChannelBlurEffect>(); });
    add(QObject::tr("Zoom Blur"), QObject::tr("Blur"),
        []() { return enve::make_shared<ZoomBlurEffect>(); });
    add(QObject::tr("Camera Lens Blur"), QObject::tr("Blur"),
        []() { return enve::make_shared<CameraLensBlurEffect>(); });
    add(QObject::tr("Shadow"), "", []() { return enve::make_shared<ShadowEffect>(); });
    add(QObject::tr("Drop Shadow"), QObject::tr("Light"),
        []() { return enve::make_shared<DropShadowEffect>(); });
    add(QObject::tr("Motion Blur"), "", []() { return enve::make_shared<MotionBlurEffect>(); });
    add(QObject::tr("残影 (Echo)"), QObject::tr("Time"),
        []() { return enve::make_shared<EchoEffect>(); });
    add(QObject::tr("Brightness-Contrast"), QObject::tr("Color"),
        []() { return enve::make_shared<BrightnessContrastEffect>(); });
    add(QObject::tr("Colorize"), QObject::tr("Color"),
        []() { return enve::make_shared<ColorizeEffect>(); });
    add(QObject::tr("Color Grading"), QObject::tr("Color"),
        []() { return enve::make_shared<ColorGradingEffect>(); });
    add(QObject::tr("Invert"), QObject::tr("Color"),
        []() { return enve::make_shared<InvertEffect>(); });
    add(QObject::tr("Tint"), QObject::tr("Color"),
        []() { return enve::make_shared<TintEffect>(); });
    add(QObject::tr("Posterize"), QObject::tr("Color"),
        []() { return enve::make_shared<PosterizeEffect>(); });
    add(QObject::tr("阈值 (Threshold)"), QObject::tr("Color"),
        []() { return enve::make_shared<ThresholdEffect>(); });
    add(QObject::tr("去色 (Desaturate)"), QObject::tr("Color"),
        []() { return enve::make_shared<DesaturateEffect>(); });
    add(QObject::tr("色阶 (Levels)"), QObject::tr("Color"),
        []() { return enve::make_shared<LevelsEffect>(); });
    add(QObject::tr("色相/饱和度 (Hue-Saturation)"), QObject::tr("Color"),
        []() { return enve::make_shared<HueSaturationEffect>(); });
    add(QObject::tr("简单阻塞 (Simple Choker)"), QObject::tr("Matte"),
        []() { return enve::make_shared<SimpleChokerEffect>(); });
    add(QObject::tr("设置遮罩 (Set Matte)"), QObject::tr("Matte"),
        []() { return enve::make_shared<SetMatteEffect>(); });
    add(QObject::tr("Chroma Key"), QObject::tr("Color"),
        []() { return enve::make_shared<ChromaKeyEffect>(); });
    add(QObject::tr("Glow"), QObject::tr("Light"),
        []() { return enve::make_shared<GlowEffect>(); });
    add(QObject::tr("Chromatic Aberration"), QObject::tr("Distort"),
        []() { return enve::make_shared<ChromaticAberrationEffect>(); });
    add(QObject::tr("Wave Warp"), QObject::tr("Distort"),
        []() { return enve::make_shared<WaveWarpEffect>(); });
    add(QObject::tr("Mirror"), QObject::tr("Distort"),
        []() { return enve::make_shared<MirrorEffect>(); });
    add(QObject::tr("Twirl"), QObject::tr("Distort"),
        []() { return enve::make_shared<TwirlEffect>(); });
    add(QObject::tr("Shake"), QObject::tr("Distort"),
        []() { return enve::make_shared<ShakeEffect>(); });
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
    add(QObject::tr("Glitch"), QObject::tr("Stylize"),
        []() { return enve::make_shared<GlitchEffect>(); });
    add(QObject::tr("Halftone"), QObject::tr("Stylize"),
        []() { return enve::make_shared<HalftoneEffect>(); });
    add(QObject::tr("Motion Tile"), QObject::tr("Stylize"),
        []() { return enve::make_shared<MotionTileEffect>(); });
    add(QObject::tr("Fractal Noise"), QObject::tr("Generate"),
        []() { return enve::make_shared<FractalNoiseEffect>(); });
    add(QObject::tr("渐变 (Ramp)"), QObject::tr("Generate"),
        []() { return enve::make_shared<RampEffect>(); });
    add(QObject::tr("网格 (Grid)"), QObject::tr("Generate"),
        []() { return enve::make_shared<GridEffect>(); });
    add(QObject::tr("Light Sweep"), QObject::tr("Light"),
        []() { return enve::make_shared<LightSweepEffect>(); });
    add(QObject::tr("Displacement Warp"), QObject::tr("Distort"),
        []() { return enve::make_shared<DisplacementWarpEffect>(); });
    add(QObject::tr("Film Grain"), QObject::tr("Stylize"),
        []() { return enve::make_shared<FilmGrainEffect>(); });
    add(QObject::tr("Black-White Flash"), QObject::tr("Stylize"),
        []() { return enve::make_shared<BlackWhiteFlashEffect>(); });
    add(QObject::tr("Liquid Glass"), QObject::tr("Distort"),
        []() { return enve::make_shared<LiquidGlassEffect>(); });
    add(QObject::tr("Pixel Art"), QObject::tr("Stylize"),
        []() { return enve::make_shared<PixelArtEffect>(); });
    add(QObject::tr("Rain"), QObject::tr("Simulation"),
        []() { return enve::make_shared<RainEffect>(); });
    add(QObject::tr("Wipe"), QObject::tr("Transitions"),
        []() { return enve::make_shared<WipeEffect>(); });
    add(QObject::tr("Stripe"), QObject::tr("Transitions"),
        []() { return enve::make_shared<StripeEffect>(); });
    add(QObject::tr("Noise Fade"), QObject::tr("Transitions"),
        []() { return enve::make_shared<NoiseFadeEffect>(); });
    add(QObject::tr("CC Smear"), QObject::tr("Distort"),
        []() { return enve::make_shared<SmearEffect>(); });
    add(QObject::tr("Shatter"), QObject::tr("Simulation"),
        []() { return enve::make_shared<ShatterEffect>(); });
    add(QObject::tr("粒子"), QObject::tr("Simulation"),
        []() { return enve::make_shared<ParticleEffect>(); });
    add(QObject::tr("卷页 (Page Curl)"), QObject::tr("Distort"),
        []() { return enve::make_shared<PageCurlEffect>(); });
    add(QObject::tr("晶格变形"), QObject::tr("Distort"),
        []() { return enve::make_shared<LatticeWarpEffect>(); });
    add(QObject::tr("三色渐变 (Cel Volume)"), QObject::tr("Stylize"),
        []() { return enve::make_shared<CelVolumeEffect>(); });
    add(QObject::tr("毛边粗糙化 (Roughen Edges)"), QObject::tr("Stylize"),
        []() { return enve::make_shared<RoughenEdgesEffect>(); });
    add(QObject::tr("图层样式"), "",
        []() { return enve::make_shared<LayerStylesEffect>(); });
    add(QObject::tr("边角定位 (Corner Pin)"), QObject::tr("Distort"),
        []() { return enve::make_shared<CornerPinEffect>(); });
    add(QObject::tr("湍流置换 (Turbulent Displace)"), QObject::tr("Distort"),
        []() { return enve::make_shared<TurbulentDisplaceEffect>(); });
}

void RasterEffectMenuCreator::forEveryEffectCustom(const EffectAdder &add)
{
    CustomRasterEffectCreator::sForEveryEffect(add);
}

void RasterEffectMenuCreator::forEveryEffectShader(const EffectAdder &add)
{
    ShaderEffectCreator::sForEveryEffect(add);
}
