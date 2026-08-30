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

#include "boxsinglewidget.h"
#include "swt_abstraction.h"
#include "singlewidgettarget.h"
#include "optimalscrollarena/scrollwidgetvisiblepart.h"
#include "widgets/colorsettingswidget.h"
#include <QPointer>

#include "Boxes/containerbox.h"
#include "widgets/qrealanimatorvalueslider.h"
#include "boxscroller.h"
#include "GUI/keysview.h"
#include "pointhelpers.h"
#include "GUI/BoxesList/boolpropertywidget.h"
#include "boxtargetwidget.h"
#include "Properties/boxtargetproperty.h"
#include "Properties/comboboxproperty.h"
#include "Animators/qstringanimator.h"
#include "RasterEffects/rastereffectcollection.h"
#include "Properties/boolproperty.h"
#include "Properties/boolpropertycontainer.h"
#include "Animators/qpointfanimator.h"
#include "Boxes/bone.h"
#include "Boxes/bonelayer.h"
#include "Boxes/pathbox.h"
#include "canvas.h"
#include "BlendEffects/blendeffectcollection.h"
#include "BlendEffects/blendeffectboxshadow.h"
#include "Sound/eindependentsound.h"
#include "GUI/propertynamedialog.h"
#include "Animators/SmartPath/smartpathcollection.h"

#include "typemenu.h"
#include "themesupport.h"
#include "Animators/transformanimator.h"
#include "Animators/qrealanimator.h"
#include "GUI/global.h"
#include "TransformEffects/parenteffect.h"
#include <functional>
#include "TransformEffects/transformeffectcollection.h"

#include <QApplication>
#include <QPainter>

#include <QtMath>
#include <cmath>
#include <QDesktopWidget>

#include <QMessageBox>

#include "Boxes/circle.h"
#include "Boxes/rectangle.h"
#include "Boxes/solidlayer.h"
#include "Boxes/cameralayer.h"
#include "Boxes/nullobject.h"
#include "Sound/evideosound.h"
#include "Boxes/internallinkgroupbox.h"
#include "Boxes/imagesequencebox.h"

QPixmap* BoxSingleWidget::VISIBLE_ICON;

QString translatePropertyName(const QString& name) {
    static const QHash<QString, QString> map = {
        { QStringLiteral("transform"), BoxSingleWidget::tr("transform") },
        { QStringLiteral("fill"), BoxSingleWidget::tr("fill") },
        { QStringLiteral("outline"), BoxSingleWidget::tr("outline") },
        { QStringLiteral("thickness"), BoxSingleWidget::tr("thickness") },
        { QStringLiteral("color"), BoxSingleWidget::tr("color") },
        { QStringLiteral("brush settings"), BoxSingleWidget::tr("brush settings") },
        { QStringLiteral("width"), BoxSingleWidget::tr("width") },
        { QStringLiteral("pressure"), BoxSingleWidget::tr("pressure") },
        { QStringLiteral("time"), BoxSingleWidget::tr("time") },
        { QStringLiteral("gradient points"), BoxSingleWidget::tr("gradient points") },
        { QStringLiteral("translation"), BoxSingleWidget::tr("translation") },
        { QStringLiteral("rotation"), BoxSingleWidget::tr("rotation") },
        { QStringLiteral("rotate"), BoxSingleWidget::tr("rotate") },
        { QStringLiteral("scale"), BoxSingleWidget::tr("scale") },
        { QStringLiteral("shear"), BoxSingleWidget::tr("shear") },
        { QStringLiteral("opacity"), BoxSingleWidget::tr("opacity") },
        { QStringLiteral("alpha"), BoxSingleWidget::tr("alpha") },
        { QStringLiteral("red"), BoxSingleWidget::tr("red") },
        { QStringLiteral("green"), BoxSingleWidget::tr("green") },
        { QStringLiteral("blue"), BoxSingleWidget::tr("blue") },
        { QStringLiteral("hue"), BoxSingleWidget::tr("hue") },
        { QStringLiteral("saturation"), BoxSingleWidget::tr("saturation") },
        { QStringLiteral("lightness"), BoxSingleWidget::tr("lightness") },
        { QStringLiteral("value"), BoxSingleWidget::tr("value") },
        { QStringLiteral("pivot"), BoxSingleWidget::tr("pivot") },
        { QStringLiteral("x"), BoxSingleWidget::tr("x") },
        { QStringLiteral("y"), BoxSingleWidget::tr("y") },
        { QStringLiteral("z-index"), BoxSingleWidget::tr("z-index") },
        { QStringLiteral("size"), BoxSingleWidget::tr("size") },
        { QStringLiteral("offset"), BoxSingleWidget::tr("offset") },
        { QStringLiteral("text"), BoxSingleWidget::tr("text") },
        { QStringLiteral("frame"), BoxSingleWidget::tr("frame") },
        { QStringLiteral("frame step"), BoxSingleWidget::tr("frame step") },
        { QStringLiteral("flip book"), BoxSingleWidget::tr("flip book") },
        { QStringLiteral("seed"), BoxSingleWidget::tr("seed") },
        { QStringLiteral("spacing"), BoxSingleWidget::tr("spacing") },
        { QStringLiteral("smoothness"), BoxSingleWidget::tr("smoothness") },
        { QStringLiteral("periodic"), BoxSingleWidget::tr("periodic") },
        { QStringLiteral("displacement"), BoxSingleWidget::tr("displacement") },
        { QStringLiteral("diminish"), BoxSingleWidget::tr("diminish") },
        { QStringLiteral("blur radius"), BoxSingleWidget::tr("blur radius") },
        { QStringLiteral("round radius"), BoxSingleWidget::tr("round radius") },
        { QStringLiteral("horizontal radius"), BoxSingleWidget::tr("horizontal radius") },
        { QStringLiteral("vertical radius"), BoxSingleWidget::tr("vertical radius") },
        { QStringLiteral("top left"), BoxSingleWidget::tr("top left") },
        { QStringLiteral("bottom right"), BoxSingleWidget::tr("bottom right") },
        { QStringLiteral("center"), BoxSingleWidget::tr("center") },
        { QStringLiteral("point 1"), BoxSingleWidget::tr("point 1") },
        { QStringLiteral("point 2"), BoxSingleWidget::tr("point 2") },
        { QStringLiteral("point 3"), BoxSingleWidget::tr("point 3") },
        { QStringLiteral("point 4"), BoxSingleWidget::tr("point 4") },
        { QStringLiteral("point1"), BoxSingleWidget::tr("point1") },
        { QStringLiteral("point2"), BoxSingleWidget::tr("point2") },
        { QStringLiteral("max deviation"), BoxSingleWidget::tr("max deviation") },
        { QStringLiteral("max length"), BoxSingleWidget::tr("max length") },
        { QStringLiteral("min length"), BoxSingleWidget::tr("min length") },
        { QStringLiteral("length based"), BoxSingleWidget::tr("length based") },
        { QStringLiteral("length inc"), BoxSingleWidget::tr("length inc") },
        { QStringLiteral("path-wise"), BoxSingleWidget::tr("path-wise") },
        { QStringLiteral("segment length"), BoxSingleWidget::tr("segment length") },
        { QStringLiteral("samples count"), BoxSingleWidget::tr("samples count") },
        { QStringLiteral("clip"), BoxSingleWidget::tr("clip") },
        { QStringLiteral("clip path"), BoxSingleWidget::tr("clip path") },
        { QStringLiteral("target"), BoxSingleWidget::tr("target") },
        { QStringLiteral("link target"), BoxSingleWidget::tr("link target") },
        { QStringLiteral("Empty Link"), BoxSingleWidget::tr("Empty Link") },
        { QStringLiteral("fill effects"), BoxSingleWidget::tr("fill effects") },
        { QStringLiteral("outline effects"), BoxSingleWidget::tr("outline effects") },
        { QStringLiteral("outline base effects"), BoxSingleWidget::tr("outline base effects") },
        { QStringLiteral("path base effects"), BoxSingleWidget::tr("path base effects") },
        { QStringLiteral("Raster Effects"), BoxSingleWidget::tr("Raster Effects") },
        { QStringLiteral("raster effects"), BoxSingleWidget::tr("Raster Effects") },
        { QStringLiteral("Path Effects"), BoxSingleWidget::tr("Path Effects") },
        { QStringLiteral("path effects"), BoxSingleWidget::tr("Path Effects") },
        { QStringLiteral("Blend Effects"), BoxSingleWidget::tr("Blend Effects") },
        { QStringLiteral("blend effects"), BoxSingleWidget::tr("Blend Effects") },
        { QStringLiteral("Transform Effects"), BoxSingleWidget::tr("Transform Effects") },
        { QStringLiteral("transform effects"), BoxSingleWidget::tr("Transform Effects") },
        // 2.5D billboard properties
        // NOTE: must use QStringLiteral (wide literal) for the Chinese text:
        // tr() takes a narrow (char*) literal, MSVC encodes \uXXXX escapes to
        // GBK, but tr() decodes as UTF-8 -> mojibake. QStringLiteral on MSVC
        // uses L"" wide literals where \uXXXX is a correct wchar_t code unit.
        { QStringLiteral("3D rotation X"),
          QStringLiteral("3D \u65CB\u8F6C X") },
        { QStringLiteral("3D rotation Y"),
          QStringLiteral("3D \u65CB\u8F6C Y") },
        { QStringLiteral("3D position Z"),
          QStringLiteral("3D \u4F4D\u79FB Z") },
        { QStringLiteral("3D perspective"),
          QStringLiteral("3D \u900F\u89C6") },
        // effect names
        { QStringLiteral("blur"), QStringLiteral("\u9AD8\u65AF\u6A21\u7CCA (Blur)") },
        { QStringLiteral("directional blur"), QStringLiteral("\u65B9\u5411\u6A21\u7CCA (Directional Blur)") },
        { QStringLiteral("radial blur"), QStringLiteral("\u5F84\u5411\u6A21\u7CCA (Radial Blur)") },
        { QStringLiteral("channel blur"), QStringLiteral("\u901A\u9053\u6A21\u7CCA (Channel Blur)") },
        { QStringLiteral("shadow"), QStringLiteral("\u9634\u5F71 (Shadow)") },
        { QStringLiteral("motion blur"), QStringLiteral("\u8FD0\u52A8\u6A21\u7CCA (Motion Blur)") },
        { QStringLiteral("brightness contrast"), QStringLiteral("\u4EAE\u5EA6\u5BF9\u6BD4\u5EA6 (Brightness-Contrast)") },
        { QStringLiteral("brightness-contrast"), QStringLiteral("\u4EAE\u5EA6\u5BF9\u6BD4\u5EA6 (Brightness-Contrast)") },
        { QStringLiteral("colorize"), QStringLiteral("\u989C\u8272\u66FF\u6362 (Colorize)") },
        { QStringLiteral("invert"), QStringLiteral("\u53CD\u8272 (Invert)") },
        { QStringLiteral("tint"), QStringLiteral("\u53CC\u8272\u8C03\u8272 (Tint)") },
        { QStringLiteral("posterize"), QStringLiteral("\u8272\u8C03\u5206\u79BB (Posterize)") },
        { QStringLiteral("glow"), QStringLiteral("\u8F89\u5149\u53D1\u5149 (Glow)") },
        { QStringLiteral("chromatic aberration"), QStringLiteral("RGB\u8272\u5dee (Chromatic Aberration)") },
        { QStringLiteral("wave warp"), QStringLiteral("\u6CE2\u6D6A\u626D\u66F2 (Wave Warp)") },
        { QStringLiteral("mirror"), QStringLiteral("\u955C\u50CF\u5BF9\u79F0 (Mirror)") },
        { QStringLiteral("twirl"), QStringLiteral("\u65CB\u8F6C\u626D\u66F2 (Twirl)") },
        { QStringLiteral("vignette"), QStringLiteral("\u6697\u89D2\u7FA1\u5316 (Vignette)") },
        { QStringLiteral("letterbox"), QStringLiteral("\u7535\u5F71\u906E\u5E45 (Letterbox)") },
        { QStringLiteral("scanlines"), QStringLiteral("CRT\u626B\u63CF\u7EBF (Scanlines)") },
        { QStringLiteral("edge detect"), QStringLiteral("\u8FB9\u7F18\u68C0\u6D4B (Edge Detect)") },
        { QStringLiteral("pixelate"), QStringLiteral("\u50CF\u7D20\u5316 (Pixelate)") },
        { QStringLiteral("noise"), QStringLiteral("\u80F6\u7247\u566A\u70B9 (Noise)") },
        { QStringLiteral("glitch"), QStringLiteral("\u6545\u969C\u827A\u672F (Glitch)") },
        { QStringLiteral("halftone"), QStringLiteral("\u534A\u8272\u8C03\u7F51\u70B9 (Halftone)") },
        { QStringLiteral("rain"), QStringLiteral("\u7A0B\u5E8F\u5316\u96E8\u6C34 (Rain)") },
        { QStringLiteral("wipe"), QStringLiteral("\u7EBF\u6027\u64E6\u9664 (Wipe)") },
        { QStringLiteral("noise fade"), QStringLiteral("\u566A\u6CE2\u6D88\u9690 (Noise Fade)") },
        { QStringLiteral("shake"), QStringLiteral("\u753B\u9762\u9707\u52A8 (Shake)") },
        { QStringLiteral("drop shadow"), QStringLiteral("\u67D4\u548C\u6295\u5F71 (Drop Shadow)") },
        { QStringLiteral("zoom blur"), QStringLiteral("\u7F29\u653E\u5149\u8292\u6A21\u7CCA (Zoom Blur)") },
        { QStringLiteral("color grading"), QStringLiteral("\u4E13\u4E1A\u8C03\u8272 (Color Grading)") },
        { QStringLiteral("stripe"), QStringLiteral("\u6761\u7EB9\u767E\u53F6\u7A97 (Stripe)") },
        { QStringLiteral("motion tile"), QStringLiteral("\u8FD0\u52A8\u5E73\u94FA (Motion Tile)") },
        { QStringLiteral("fractal noise"), QStringLiteral("\u5206\u5F62\u6742\u8272 (Fractal Noise)") },
        { QStringLiteral("fractalNoise"), QStringLiteral("\u5206\u5F62\u6742\u8272 (Fractal Noise)") },
        { QStringLiteral("light sweep"), QStringLiteral("\u626B\u5149 (Light Sweep)") },
        { QStringLiteral("lightSweep"), QStringLiteral("\u626B\u5149 (Light Sweep)") },
        { QStringLiteral("displacement warp"), QStringLiteral("\u7F6E\u6362\u626D\u66F2 (Displacement Warp)") },
        { QStringLiteral("displacementWarp"), QStringLiteral("\u7F6E\u6362\u626D\u66F2 (Displacement Warp)") },
        { QStringLiteral("film grain"), QStringLiteral("\u80F6\u7247\u9897\u7C92 (Film Grain)") },
        { QStringLiteral("filmGrain"), QStringLiteral("\u80F6\u7247\u9897\u7C92 (Film Grain)") },
        { QStringLiteral("black white flash"), QStringLiteral("\u9ED1\u767D\u95EA (Black-White Flash)") },
        { QStringLiteral("blackWhiteFlash"), QStringLiteral("\u9ED1\u767D\u95EA (Black-White Flash)") },
        { QStringLiteral("liquid glass"), QStringLiteral("\u6D41\u4F53\u6bdb\u73BB\u7483 (Liquid Glass)") },
        { QStringLiteral("liquidGlass"), QStringLiteral("\u6D41\u4F53\u6bdb\u73BB\u7483 (Liquid Glass)") },
        { QStringLiteral("pixel art"), QStringLiteral("\u590D\u53E4\u50CF\u7D20\u753B (Pixel Art)") },
        { QStringLiteral("pixelArt"), QStringLiteral("\u590D\u53E4\u50CF\u7D20\u753B (Pixel Art)") },
        { QStringLiteral("chroma-key"), QStringLiteral("\u6263\u7EFF\u62A0\u50CF (Chroma Key)") },
        { QStringLiteral("chroma key"), QStringLiteral("\u6263\u7EFF\u62A0\u50CF (Chroma Key)") },
        { QStringLiteral("key color"), QStringLiteral("\u62A0\u50CF\u989C\u8272 (Key Color)") },
        { QStringLiteral("keying method"), QStringLiteral("\u62A0\u50CF\u7B97\u6CD5 (Keying Method)") },
        { QStringLiteral("matte black"), QStringLiteral("\u906E\u7F69\u9ED1\u573A (Matte Black)") },
        { QStringLiteral("matte white"), QStringLiteral("\u906E\u7F69\u767D\u573A (Matte White)") },
        { QStringLiteral("matte highlights"), QStringLiteral("\u906E\u7F69\u9AD8\u5149 (Matte Highlights)") },
        { QStringLiteral("matte shadows"), QStringLiteral("\u906E\u7F69\u9634\u5F71 (Matte Shadows)") },
        { QStringLiteral("edge softness"), QStringLiteral("\u8FB9\u7F18\u67D4\u5316 (Edge Softness)") },
        { QStringLiteral("hair detail"), QStringLiteral("\u6bdb\u53D1\u7EC6\u8282 (Hair Detail)") },
        { QStringLiteral("edge defringe"), QStringLiteral("\u8FB9\u7F18\u53BB\u8FB9 (Edge Defringe)") },
        { QStringLiteral("spill reduction"), QStringLiteral("\u6EA2\u8272\u6291\u5236 (Spill Reduction)") },
        { QStringLiteral("spill balance"), QStringLiteral("\u6EA2\u8272\u5E73\u8861 (Spill Balance)") },
        // path effects
        { QStringLiteral("displace"), QStringLiteral("\u7F6E\u6362 (Displace)") },
        { QStringLiteral("spatial displace"), QStringLiteral("\u7A7A\u95F4\u7F6E\u6362 (Spatial Displace)") },
        { QStringLiteral("dash"), QStringLiteral("\u865A\u7EBF (Dash)") },
        { QStringLiteral("duplicate"), QStringLiteral("\u526F\u672C\u751F\u6210 (Duplicate)") },
        { QStringLiteral("sub-path"), QStringLiteral("\u5B50\u8DEF\u5F84 (Sub-Path)") },
        { QStringLiteral("sub path"), QStringLiteral("\u5B50\u8DEF\u5F84 (Sub-Path)") },
        { QStringLiteral("solidify"), QStringLiteral("\u5B9E\u4F53\u5316 (Solidify)") },
        { QStringLiteral("sum"), QStringLiteral("\u8F6E\u5ED3\u878D\u5408 (Sum)") },
        { QStringLiteral("lines"), QStringLiteral("\u7EBF\u6761\u586B\u5145 (Lines)") },
        { QStringLiteral("zigzag"), QStringLiteral("\u6CE2\u6D6A\u6298\u7EBF (ZigZag)") },
        { QStringLiteral("subdivide"), QStringLiteral("\u7EC6\u5206 (Subdivide)") },
        // blend effects
        { QStringLiteral("move"), QStringLiteral("\u4F4D\u79FB (Move)") },
        { QStringLiteral("targeted"), QStringLiteral("\u76EE\u6807\u5B9A\u5411 (Targeted)") },
        // transform effects
        { QStringLiteral("track"), QStringLiteral("\u8DDF\u8E2A (Track)") },
        { QStringLiteral("follow path"), QStringLiteral("\u6CBF\u8DEF\u5F84\u8FD0\u52A8 (Follow Path)") },
        { QStringLiteral("follow object"), QStringLiteral("\u8DDF\u968F\u5BF9\u8C61 (Follow Object)") },
        { QStringLiteral("follow object relative"), QStringLiteral("\u76F8\u5BF9\u8DDF\u968F\u5BF9\u8C61 (Follow Object Relative)") },
        { QStringLiteral("parent"), QStringLiteral("\u7236\u7EA7\u7ED1\u5B9A (Parent)") },
        { QStringLiteral("effects"), QStringLiteral("\u7279\u6548 (Effects)") },
        // effect parameters
        { QStringLiteral("edge"), QStringLiteral("\u8FB9\u7F18\u5F3A\u5EA6 (Edge)") },
        { QStringLiteral("bgcolor"), QStringLiteral("\u80CC\u666F\u989C\u8272 (Background Color)") },
        { QStringLiteral("roughness"), QStringLiteral("\u8868\u9762\u7C97\u7CD9\u5EA6 (Roughness)") },
        { QStringLiteral("complexity"), QStringLiteral("\u590D\u6742\u5EA6 (Complexity)") },
        { QStringLiteral("evolution"), QStringLiteral("\u6F14\u5316 (Evolution)") },
        { QStringLiteral("color1"), QStringLiteral("\u989C\u8272 1 (Color 1)") },
        { QStringLiteral("color2"), QStringLiteral("\u989C\u8272 2 (Color 2)") },
        { QStringLiteral("colorGrain"), QStringLiteral("\u5F69\u8272\u9897\u7C92 (Color Grain)") },
        { QStringLiteral("chromatic"), QStringLiteral("\u8272\u6563\u5206\u79BB (Chromatic)") },
        { QStringLiteral("dispType"), QStringLiteral("\u7F6E\u6362\u7C7B\u578B (Displacement Type)") },
        { QStringLiteral("noiseType"), QStringLiteral("\u6742\u8272\u7C7B\u578B (Noise Type)") },
        { QStringLiteral("invert"), QStringLiteral("\u53CD\u76F8 (Invert)") },
        { QStringLiteral("threshold"), QStringLiteral("\u9608\u503C (Threshold)") },
        { QStringLiteral("intensity"), QStringLiteral("\u5F3A\u5EA6 (Intensity)") },
        { QStringLiteral("amount"), QStringLiteral("\u6570\u91CF/\u5F3A\u5EA6 (Amount)") },
        { QStringLiteral("density"), QStringLiteral("\u5BC6\u5EA6 (Density)") },
        { QStringLiteral("speed"), QStringLiteral("\u901F\u5EA6 (Speed)") },
        { QStringLiteral("angle"), QStringLiteral("\u89D2\u5EA6 (Angle)") },
        { QStringLiteral("frequency"), QStringLiteral("\u9891\u7387 (Frequency)") },
        { QStringLiteral("phase"), QStringLiteral("\u76F8\u4F4D (Phase)") },
        { QStringLiteral("amplitude"), QStringLiteral("\u632F\u5E45 (Amplitude)") },
        { QStringLiteral("randomness"), QStringLiteral("\u968F\u673A\u5EA6 (Randomness)") },
        { QStringLiteral("distance"), QStringLiteral("\u8DDD\u79BB (Distance)") },
        { QStringLiteral("softness"), QStringLiteral("\u67D4\u548C\u5EA6 (Softness)") },
        { QStringLiteral("exposure"), QStringLiteral("\u66DD\u5149 (Exposure)") },
        { QStringLiteral("contrast"), QStringLiteral("\u5BF9\u6BD4\u5EA6 (Contrast)") },
        { QStringLiteral("saturation"), QStringLiteral("\u9971\u548C\u5EA6 (Saturation)") },
        { QStringLiteral("temperature"), QStringLiteral("\u8272\u6E29 (Temperature)") },
        { QStringLiteral("tint"), QStringLiteral("\u8272\u5F69\u5E73\u8861/\u8272\u8C03 (Tint)") },
        { QStringLiteral("transition"), QStringLiteral("\u8FC7\u6E21\u8FDB\u5EA6 (Transition)") },
        { QStringLiteral("feather"), QStringLiteral("\u7FBD\u5316 (Feather)") },
        { QStringLiteral("tileCountX"), QStringLiteral("\u6C34\u5E73\u5E73\u94FA\u6570 (Tile Count X)") },
        { QStringLiteral("tileCountY"), QStringLiteral("\u5782\u76F4\u5E73\u94FA\u6570 (Tile Count Y)") },
        { QStringLiteral("offsetX"), QStringLiteral("\u6C34\u5E73\u504F\u79FB (Offset X)") },
        { QStringLiteral("offsetY"), QStringLiteral("\u5782\u76F4\u5E73\u94FA (Offset Y)") },
        { QStringLiteral("mirrorEdges"), QStringLiteral("\u955C\u50CF\u8FB9\u7F18 (Mirror Edges)") },
        { QStringLiteral("radius"), QStringLiteral("\u534A\u5F84 (Radius)") },
        { QStringLiteral("samples"), QStringLiteral("\u91C7\u6837\u6570 (Samples)") },
        { QStringLiteral("levels"), QStringLiteral("\u8272\u9636\u6570 (Levels)") },
        { QStringLiteral("red blur"), QStringLiteral("\u7EA2\u8272\u901A\u9053\u6A21\u7CCA (Red Blur)") },
        { QStringLiteral("green blur"), QStringLiteral("\u7EFF\u8272\u901A\u9053\u6A21\u7CCA (Green Blur)") },
        { QStringLiteral("blue blur"), QStringLiteral("\u84DD\u8272\u901A\u9053\u6A21\u7CCA (Blue Blur)") },
        { QStringLiteral("falloff"), QStringLiteral("\u8870\u51CF (Falloff)") },
        { QStringLiteral("monochrome"), QStringLiteral("\u5355\u8272\u6A21\u5F0F (Monochrome)") },
        { QStringLiteral("aspect ratio"), QStringLiteral("\u753B\u5E45\u6BD4\u4F8B (Aspect Ratio)") },
        { QStringLiteral("blend"), QStringLiteral("\u6DF7\u5408\u5EA6 (Blend)") },
        { QStringLiteral("map black"), QStringLiteral("\u6620\u5C04\u9ED1\u8272 (Map Black)") },
        { QStringLiteral("map white"), QStringLiteral("\u6620\u5C04\u767D\u8272 (Map White)") },
        { QStringLiteral("block size"), QStringLiteral("\u65B9\u5757\u5927\u5C0F (Block Size)") },
        { QStringLiteral("line count"), QStringLiteral("\u6761\u7EB9\u6570\u91CF (Line Count)") },
    };
    if(name.startsWith(QStringLiteral("Mask: "))) {
        return QStringLiteral("\u8499\u7248: ") + name.mid(6);
    }
    return map.value(name, name);
}

namespace {
// AE-style layer label color swatch shown in place of the type icon
QPixmap* labelColorPixmap(const QColor& color) {
    static QHash<QString, QPixmap*> cache;
    const QString key = color.name();
    const auto it = cache.find(key);
    if(it != cache.end()) return it.value();
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const int logical = eSizesUI::widget;
    auto pm = new QPixmap(QSize(logical, logical) * dpr);
    pm->setDevicePixelRatio(dpr);
    pm->fill(Qt::transparent);
    QPainter p(pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal m = logical * 0.14;
    QRectF r(m, m, logical - 2*m, logical - 2*m);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1));
    p.setBrush(color);
    p.drawRoundedRect(r, logical * 0.18, logical * 0.18);
    p.end();
    cache.insert(key, pm);
    return pm;
}

// Translate Skia blend mode names for display.
QString translateBlendModeName(const QString& name) {
    static const QHash<QString, QString> map = {
        { QStringLiteral("Clear"), BoxSingleWidget::tr("Clear") },
        { QStringLiteral("Src"), BoxSingleWidget::tr("Src") },
        { QStringLiteral("Dst"), BoxSingleWidget::tr("Dst") },
        { QStringLiteral("SrcOver"), BoxSingleWidget::tr("SrcOver") },
        { QStringLiteral("DstOver"), BoxSingleWidget::tr("DstOver") },
        { QStringLiteral("SrcIn"), BoxSingleWidget::tr("SrcIn") },
        { QStringLiteral("DstIn"), BoxSingleWidget::tr("DstIn") },
        { QStringLiteral("SrcOut"), BoxSingleWidget::tr("SrcOut") },
        { QStringLiteral("DstOut"), BoxSingleWidget::tr("DstOut") },
        { QStringLiteral("SrcATop"), BoxSingleWidget::tr("SrcATop") },
        { QStringLiteral("DstATop"), BoxSingleWidget::tr("DstATop") },
        { QStringLiteral("Xor"), BoxSingleWidget::tr("Xor") },
        { QStringLiteral("Plus"), BoxSingleWidget::tr("Plus") },
        { QStringLiteral("Modulate"), BoxSingleWidget::tr("Modulate") },
        { QStringLiteral("Screen"), BoxSingleWidget::tr("Screen") },
        { QStringLiteral("Overlay"), BoxSingleWidget::tr("Overlay") },
        { QStringLiteral("Darken"), BoxSingleWidget::tr("Darken") },
        { QStringLiteral("Lighten"), BoxSingleWidget::tr("Lighten") },
        { QStringLiteral("ColorDodge"), BoxSingleWidget::tr("ColorDodge") },
        { QStringLiteral("ColorBurn"), BoxSingleWidget::tr("ColorBurn") },
        { QStringLiteral("HardLight"), BoxSingleWidget::tr("HardLight") },
        { QStringLiteral("SoftLight"), BoxSingleWidget::tr("SoftLight") },
        { QStringLiteral("Difference"), BoxSingleWidget::tr("Difference") },
        { QStringLiteral("Exclusion"), BoxSingleWidget::tr("Exclusion") },
        { QStringLiteral("Multiply"), BoxSingleWidget::tr("Multiply") },
        { QStringLiteral("Hue"), BoxSingleWidget::tr("Hue") },
        { QStringLiteral("Saturation"), BoxSingleWidget::tr("Saturation") },
        { QStringLiteral("Color"), BoxSingleWidget::tr("Color") },
        { QStringLiteral("Luminosity"), BoxSingleWidget::tr("Luminosity") },
    };
    return map.value(name, name);
}
}

QPixmap* BoxSingleWidget::INVISIBLE_ICON;
QPixmap* BoxSingleWidget::BOX_CHILDREN_VISIBLE_ICON;
QPixmap* BoxSingleWidget::BOX_CHILDREN_HIDDEN_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_CHILDREN_VISIBLE_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_CHILDREN_HIDDEN_ICON;
QPixmap* BoxSingleWidget::LOCKED_ICON;
QPixmap* BoxSingleWidget::UNLOCKED_ICON;
QPixmap* BoxSingleWidget::MUTED_ICON;
QPixmap* BoxSingleWidget::UNMUTED_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_RECORDING_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_NOT_RECORDING_ICON;
QPixmap* BoxSingleWidget::ANIMATOR_DESCENDANT_RECORDING_ICON;
QPixmap* BoxSingleWidget::C_ICON;
QPixmap* BoxSingleWidget::G_ICON;
QPixmap* BoxSingleWidget::CG_ICON;
QPixmap* BoxSingleWidget::GRAPH_PROPERTY_ICON;
QPixmap* BoxSingleWidget::PROMOTE_TO_LAYER_ICON;
QPixmap* BoxSingleWidget::ICON_3D_ON;
QPixmap* BoxSingleWidget::ICON_3D_OFF;
QPixmap* BoxSingleWidget::ICON_RESET;
QPixmap* BoxSingleWidget::ICON_SOLO_ON;
QPixmap* BoxSingleWidget::ICON_SOLO_OFF;
QPixmap* BoxSingleWidget::ICON_SHY_ON;
QPixmap* BoxSingleWidget::ICON_SHY_OFF;
QPixmap* BoxSingleWidget::ICON_FX_ON;
QPixmap* BoxSingleWidget::ICON_FX_OFF;
QPixmap* BoxSingleWidget::ICON_MB_ON;
QPixmap* BoxSingleWidget::ICON_MB_OFF;
QPixmap* BoxSingleWidget::ICON_T_ON;
QPixmap* BoxSingleWidget::ICON_T_OFF;
QPixmap* BoxSingleWidget::ICON_LINKNODE_ON;
QPixmap* BoxSingleWidget::ICON_LINKNODE_OFF;
QPixmap* BoxSingleWidget::ICON_TM_ALPHA;
QPixmap* BoxSingleWidget::ICON_TM_ALPHAINV;
QPixmap* BoxSingleWidget::ICON_TM_LUMA;
QPixmap* BoxSingleWidget::ICON_TM_LUMAINV;
QPixmap* BoxSingleWidget::ICON_TM_OFF;

QPixmap* BoxSingleWidget::BOX_PATH;
QPixmap* BoxSingleWidget::BOX_CIRCLE;
QPixmap* BoxSingleWidget::BOX_RECT;
QPixmap* BoxSingleWidget::BOX_TEXT;
QPixmap* BoxSingleWidget::BOX_NULL;
QPixmap* BoxSingleWidget::BOX_CAMERA;
QPixmap* BoxSingleWidget::BOX_IMAGE;
QPixmap* BoxSingleWidget::BOX_VIDEO;
QPixmap* BoxSingleWidget::BOX_SOUND;
QPixmap* BoxSingleWidget::BOX_BONE;
QPixmap* BoxSingleWidget::BOX_BONELAYER;
QPixmap* BoxSingleWidget::BOX_SOLID;
QPixmap* BoxSingleWidget::BOX_GROUP;
QPixmap* BoxSingleWidget::BOX_LINK;
QPixmap* BoxSingleWidget::BOX_SEQ;

bool BoxSingleWidget::sStaticPixmapsLoaded = false;

#include "GUI/global.h"
#include "GUI/mainwindow.h"
#include "clipboardcontainer.h"
#include "Timeline/durationrectangle.h"
#include "GUI/coloranimatorbutton.h"
#include "canvas.h"
#include "PathEffects/patheffect.h"
#include "PathEffects/patheffectcollection.h"
#include "Sound/esoundobjectbase.h"

#include "widgets/ecombobox.h"

#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QMenu>
#include <QInputDialog>
#include <QPainter>

eComboBox* createCombo(QWidget* const parent)
{
    const auto result = new eComboBox(parent);
    result->setWheelMode(eComboBox::WheelMode::enabledWithCtrl);
    result->setFocusPolicy(Qt::NoFocus);
    return result;
}

// button that separates a simple click from a press-and-drag gesture:
// click emits clicked(), moving past the drag threshold emits dragStarted()
class ParentLinkButton : public PixmapActionButton {
    Q_OBJECT
public:
    explicit ParentLinkButton(QWidget* const parent)
        : PixmapActionButton(parent) {}
signals:
    void clicked();
    void dragStarted();
protected:
    void mousePressEvent(QMouseEvent* e) override {
        mStartPos = e->pos();
        mDragging = false;
        PixmapActionButton::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if(!mDragging && (e->pos() - mStartPos).manhattanLength()
                       > QApplication::startDragDistance()) {
            mDragging = true;
            emit dragStarted();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if(!mDragging) emit clicked();
        mDragging = false;
    }
private:
    QPoint mStartPos;
    bool mDragging = false;
};

// does the box have a ParentEffect with a bound target?
static bool boxHasParentLink(BoundingBox* const box) {
    const auto coll = box->getTransformEffectCollection();
    if(!coll) return false;
    const int n = coll->ca_getNumberOfChildren();
    for(int i = 0; i < n; i++) {
        const auto eff = enve_cast<ParentEffect*>(coll->getChild(i));
        if(eff && eff->parentTargetProperty()->getTarget()) return true;
    }
    return false;
}

// topmost selection highlight: a transparent-for-input child raised
// above every widget in the row, painting the blue wash + border on
// top of the combos/buttons instead of underneath them
class RowHighlightOverlay : public QWidget {
public:
    explicit RowHighlightOverlay(BoxSingleWidget * const owner) :
        QWidget(owner), mOwner(owner) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if(!mOwner->isSelectedRow()) return;
        QPainter p(this);
        p.fillRect(rect(), QColor(70, 130, 220, 60));
        p.setPen(QPen(QColor(120, 170, 255), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -2, -2));
        p.end();
    }
private:
    BoxSingleWidget * const mOwner;
};

BoxSingleWidget::BoxSingleWidget(BoxScroller * const parent)
    : SingleWidget(parent)
    , mParent(parent)
{
    mMainLayout = new QHBoxLayout(this);
    setLayout(mMainLayout);
    mMainLayout->setSpacing(0);
    mMainLayout->setContentsMargins(0, 0, 0, 0);
    mMainLayout->setAlignment(Qt::AlignLeft);

    mBoxButton = new PixmapActionButton(this);
    mBoxButton->setToolTip(tr("Set layer label color"));
    mBoxButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        // AE-style label color replaces the type icon when set
        if(const auto ebs0 = enve_cast<eBoxOrSound*>(target)) {
            const QColor lc = ebs0->getLabelColor();
            if(lc.isValid()) return labelColorPixmap(lc);
        }
        if (enve_cast<Circle*>(target)) {
            return BoxSingleWidget::BOX_CIRCLE;
        } else if (enve_cast<SolidLayer*>(target)) {
            return BoxSingleWidget::BOX_SOLID;
        } else if (enve_cast<RectangleBox*>(target)) {
            return BoxSingleWidget::BOX_RECT;
        } else if (enve_cast<TextBox*>(target)) {
            return BoxSingleWidget::BOX_TEXT;
        } else if (enve_cast<BoneLayer*>(target)) {
            return BoxSingleWidget::BOX_BONELAYER;
        } else if (enve_cast<Bone*>(target)) {
            return BoxSingleWidget::BOX_BONE;
        } else if (enve_cast<CameraLayer*>(target)) {
            return BoxSingleWidget::BOX_CAMERA;
        } else if (enve_cast<NullObject*>(target)) {
            return BoxSingleWidget::BOX_NULL;
        } else if (enve_cast<ImageBox*>(target)) {
            return BoxSingleWidget::BOX_IMAGE;
        } else if (enve_cast<VideoBox*>(target)) {
            return BoxSingleWidget::BOX_VIDEO;
        } else if (enve_cast<eVideoSound*>(target) ||
                   enve_cast<eSound*>(target)) {
            return BoxSingleWidget::BOX_SOUND;
        } else if (enve_cast<InternalLinkGroupBox*>(target)) {
            return BoxSingleWidget::BOX_LINK;
        } else if (enve_cast<ImageSequenceBox*>(target)) {
            return BoxSingleWidget::BOX_SEQ;
        } else if (enve_cast<PathBox*>(target)) {
            return BoxSingleWidget::BOX_PATH;
        } else if (enve_cast<ContainerBox*>(target)) {
            return BoxSingleWidget::BOX_GROUP;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mBoxButton);
    // click the swatch/icon to pick a label color (AE label palette)
    connect(mBoxButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto ebs = enve_cast<eBoxOrSound*>(mTarget->getTarget());
        if (!ebs) { return; }
        // guarded: the nested menu loop may outlive the layer
        const QPointer<eBoxOrSound> ebsGuard = ebs;
        QMenu menu(this);
        const QColor colors[] = {
            QColor(232, 32, 45),    // red
            QColor(240, 140, 30),   // orange
            QColor(232, 215, 32),   // yellow
            QColor(60, 180, 75),    // green
            QColor(0, 170, 180),    // cyan
            QColor(32, 100, 230),   // blue
            QColor(150, 60, 220),   // purple
            QColor(130, 130, 130),  // gray
        };
        const QString names[] = {
            BoxSingleWidget::tr("Red"),  // red
            BoxSingleWidget::tr("Orange"),  // orange
            BoxSingleWidget::tr("Yellow"),  // yellow
            BoxSingleWidget::tr("Green"),  // green
            BoxSingleWidget::tr("Cyan"),  // cyan
            BoxSingleWidget::tr("Blue"),  // blue
            BoxSingleWidget::tr("Purple"),  // purple
            BoxSingleWidget::tr("Gray"),  // gray
        };
        for(int i = 0; i < 8; i++) {
            const QColor c = colors[i];
            menu.addAction(QIcon(*labelColorPixmap(c)), names[i],
                           this, [ebsGuard, c]() {
                if(ebsGuard) ebsGuard->setLabelColor(c);
                Document::sInstance->actionFinished();
            });
        }
        menu.addSeparator();
        menu.addAction(tr("No Color"), // no color
                       this, [ebsGuard]() {
            if(ebsGuard) ebsGuard->setLabelColor(QColor());
            Document::sInstance->actionFinished();
        });
        menu.exec(QCursor::pos());
    });

    mRecordButton = new PixmapActionButton(this);
    mRecordButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (enve_cast<eBoxOrSound*>(target)) {
            return static_cast<QPixmap*>(nullptr);
        } else if (const auto asCAnim = enve_cast<ComplexAnimator*>(target)) {
            if (asCAnim->anim_isRecording()) {
                return BoxSingleWidget::ANIMATOR_RECORDING_ICON;
            } else {
                if (asCAnim->anim_isDescendantRecording()) {
                    return BoxSingleWidget::ANIMATOR_DESCENDANT_RECORDING_ICON;
                }
                return BoxSingleWidget::ANIMATOR_NOT_RECORDING_ICON;
            }
        } else if (const auto asAnim = enve_cast<Animator*>(target)) {
            if (asAnim->anim_isRecording()) {
                return BoxSingleWidget::ANIMATOR_RECORDING_ICON;
            }
            return BoxSingleWidget::ANIMATOR_NOT_RECORDING_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mRecordButton);
    connect(mRecordButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchRecordingAction);

    mContentButton = new PixmapActionButton(this);
    mContentButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        if (mTarget->childrenCount() == 0) {
            return static_cast<QPixmap*>(nullptr);
        }
        const auto target = mTarget->getTarget();
        if (enve_cast<eBoxOrSound*>(target)) {
            if (mTarget->contentVisible()) {
                return BoxSingleWidget::BOX_CHILDREN_VISIBLE_ICON;
            }
            return BoxSingleWidget::BOX_CHILDREN_HIDDEN_ICON;
        } else {
            if (mTarget->contentVisible()) {
                return BoxSingleWidget::ANIMATOR_CHILDREN_VISIBLE_ICON;
            }
            return BoxSingleWidget::ANIMATOR_CHILDREN_HIDDEN_ICON;
        }
    });

    mMainLayout->addWidget(mContentButton);
    connect(mContentButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchContentVisibleAction);

    mVisibleButton = new PixmapActionButton(this);
    mVisibleButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (const auto ebos = enve_cast<eBoxOrSound*>(target)) {
            if (enve_cast<eSound*>(target)) {
                if (ebos->isVisible()) { return BoxSingleWidget::UNMUTED_ICON; }
                return BoxSingleWidget::MUTED_ICON;
            } else if (ebos->isVisible()) { return BoxSingleWidget::VISIBLE_ICON; }
            return BoxSingleWidget::INVISIBLE_ICON;
        } else if (const auto eEff = enve_cast<eEffect*>(target)) {
            if (eEff->isVisible()) { return BoxSingleWidget::VISIBLE_ICON; }
            return BoxSingleWidget::INVISIBLE_ICON;
        } /*else if (enve_cast<GraphAnimator*>(target)) {
            const auto bsvt = static_cast<BoxScroller*>(mParent);
            const auto keysView = bsvt->getKeysView();
            if (keysView) { return BoxSingleWidget::GRAPH_PROPERTY_ICON; }
            return static_cast<QPixmap*>(nullptr);
        }*/
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mVisibleButton);
    connect(mVisibleButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchBoxVisibleAction);

    mLockedButton = new PixmapActionButton(this);
    mLockedButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (const auto box = enve_cast<BoundingBox*>(target)) {
            if (box->isLocked()) { return BoxSingleWidget::LOCKED_ICON; }
            return BoxSingleWidget::UNLOCKED_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mLockedButton);
    connect(mLockedButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::switchBoxLockedAction);

    // AE-style A/V column switches: solo (S) and shy (H)
    mSoloButton = new PixmapActionButton(this);
    mSoloButton->setToolTip(tr("Solo (show only soloed layers/sounds)"));
    mSoloButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        const auto ebs = enve_cast<eBoxOrSound*>(target);
        if (!ebs) { return static_cast<QPixmap*>(nullptr); }
        return ebs->isSolo() ? BoxSingleWidget::ICON_SOLO_ON
                             : BoxSingleWidget::ICON_SOLO_OFF;
    });
    mMainLayout->addWidget(mSoloButton);
    connect(mSoloButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto ebs = enve_cast<eBoxOrSound*>(mTarget->getTarget());
        if (!ebs) { return; }
        ebs->switchSolo();
        Document::sInstance->actionFinished();
    });

    mShyButton = new PixmapActionButton(this);
    mShyButton->setToolTip(tr("Shy (hide this row when 'Hide Shy Layers' is enabled in Filters)"));
    mShyButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        const auto ebs = enve_cast<eBoxOrSound*>(target);
        if (!ebs) { return static_cast<QPixmap*>(nullptr); }
        return ebs->isShy() ? BoxSingleWidget::ICON_SHY_ON
                            : BoxSingleWidget::ICON_SHY_OFF;
    });
    mMainLayout->addWidget(mShyButton);
    connect(mShyButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto ebs = enve_cast<eBoxOrSound*>(mTarget->getTarget());
        if (!ebs) { return; }
        ebs->switchShy();
        Document::sInstance->actionFinished();
    });

    m3DButton = new PixmapActionButton(this);
    m3DButton->setToolTip(tr("Toggle 3D layer (2.5D billboard: X/Y rotation, Z depth)"));
    m3DButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        const auto box = enve_cast<BoundingBox*>(target);
        if (!box) { return static_cast<QPixmap*>(nullptr); }
        const auto trans = box->getBoxTransformAnimator();
        if (!trans) { return static_cast<QPixmap*>(nullptr); }
        return trans->is3DEnabled() ? BoxSingleWidget::ICON_3D_ON
                                    : BoxSingleWidget::ICON_3D_OFF;
    });

    mMainLayout->addWidget(m3DButton);
    connect(m3DButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto target = mTarget->getTarget();
        const auto box = enve_cast<BoundingBox*>(target);
        if (!box) { return; }
        const auto trans = box->getBoxTransformAnimator();
        if (!trans) { return; }
        trans->set3DEnabled(!trans->is3DEnabled());
        Document::sInstance->actionFinished();
    });

    // AE-style switches column: master FX toggle (fx) and preserve
    // underlying transparency (T)
    mFxButton = new PixmapActionButton(this);
    mFxButton->setToolTip(tr("Toggle all effects on this layer"));
    mFxButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) { return static_cast<QPixmap*>(nullptr); }
        return box->getEffectsEnabled() ? BoxSingleWidget::ICON_FX_ON
                                        : BoxSingleWidget::ICON_FX_OFF;
    });
    mMainLayout->addWidget(mFxButton);
    connect(mFxButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) { return; }
        box->switchEffectsEnabled();
        Document::sInstance->actionFinished();
    });

    // per-layer motion blur switch (gates the MotionBlur raster effect
    // sampling; the scene-wide master lives in the View menu)
    mMbButton = new PixmapActionButton(this);
    mMbButton->setToolTip(tr("Motion blur on this layer"));
    mMbButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) { return static_cast<QPixmap*>(nullptr); }
        return box->isMbEnabled() ? BoxSingleWidget::ICON_MB_ON
                                  : BoxSingleWidget::ICON_MB_OFF;
    });
    mMainLayout->addWidget(mMbButton);
    connect(mMbButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) { return; }
        box->switchMbEnabled();
        Document::sInstance->actionFinished();
        mMbButton->update();
    });

    mTButton = new PixmapActionButton(this);
    mTButton->setToolTip(tr("Preserve underlying transparency (paint only where layers below are opaque)"));
    mTButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) { return static_cast<QPixmap*>(nullptr); }
        return box->getPreserveAlpha() ? BoxSingleWidget::ICON_T_ON
                                       : BoxSingleWidget::ICON_T_OFF;
    });
    mMainLayout->addWidget(mTButton);
    connect(mTButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) { return; }
        box->switchPreserveAlpha();
        Document::sInstance->actionFinished();
    });

    // node-link parenting: click to pick the parent from a menu,
    // press-and-drag onto another layer row to link directly
    mParentLinkButton = new ParentLinkButton(this);
    mParentLinkButton->setToolTip(tr("Parent link (click to pick, drag onto a layer row)"));
    mParentLinkButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
        if (!box) { return static_cast<QPixmap*>(nullptr); }
        return boxHasParentLink(box) ? BoxSingleWidget::ICON_LINKNODE_ON
                                     : BoxSingleWidget::ICON_LINKNODE_OFF;
    });
    mMainLayout->addWidget(mParentLinkButton);
    connect(mParentLinkButton, &ParentLinkButton::clicked,
            this, &BoxSingleWidget::showParentLinkMenu);
    connect(mParentLinkButton, &ParentLinkButton::dragStarted,
            this, &BoxSingleWidget::startParentLinkDrag);

    mHwSupportButton = new PixmapActionButton(this);
    mHwSupportButton->setToolTip(tr("Adjust GPU/CPU Processing"));
    mHwSupportButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        if (const auto rEff = enve_cast<RasterEffect*>(target)) {
            if (rEff->instanceHwSupport() == HardwareSupport::cpuOnly) {
                return BoxSingleWidget::C_ICON;
            } else if (rEff->instanceHwSupport() == HardwareSupport::gpuOnly) {
                return BoxSingleWidget::G_ICON;
            }
            return BoxSingleWidget::CG_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mHwSupportButton);
    connect(mHwSupportButton, &BoxesListActionButton::pressed, this, [this]() {
        if (!mTarget) { return; }
        const auto target = mTarget->getTarget();
        if (const auto sEff = enve_cast<ShaderEffect*>(target)) { return; }
        if (const auto rEff = enve_cast<RasterEffect*>(target)) {
            rEff->switchInstanceHwSupport();
            Document::sInstance->actionFinished();
        }
    });

    mFillWidget = new QWidget(this);
    mMainLayout->addWidget(mFillWidget);
    mFillWidget->setObjectName("transparentWidget");
    // fixed-width name column: names start at the same x on every row
    // and the widgets behind it line up in consistent columns (short
    // names leave the column blank instead of shifting everything)
    mFillWidget->setFixedWidth(eSizesUI::widget*8);

    mPromoteToLayerButton = new PixmapActionButton(this);
    mPromoteToLayerButton->setToolTip(tr("Promote to Layer"));
    mPromoteToLayerButton->setPixmapChooser([this]() {
        const auto targetGroup = getPromoteTargetGroup();
        if (targetGroup) {
            return BoxSingleWidget::PROMOTE_TO_LAYER_ICON;
        }
        return static_cast<QPixmap*>(nullptr);
    });

    mMainLayout->addWidget(mPromoteToLayerButton);
    connect(mPromoteToLayerButton, &BoxesListActionButton::pressed,
            this, [this]() {
        const auto targetGroup = getPromoteTargetGroup();
        if (targetGroup) {
            targetGroup->promoteToLayer();
            Document::sInstance->actionFinished();
        }
    });

    mValueSlider = new QrealAnimatorValueSlider(nullptr, this);
    mMainLayout->addWidget(mValueSlider, Qt::AlignRight);

    mSecondValueSlider = new QrealAnimatorValueSlider(nullptr, this);
    mMainLayout->addWidget(mSecondValueSlider, Qt::AlignRight);

    // reset-to-default button for layer transform parameters
    mResetButton = new PixmapActionButton(this);
    mResetButton->setToolTip(tr("Reset to default value"));
    mResetButton->setPixmapChooser([this]() {
        if (!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto target = mTarget->getTarget();
        const auto prop = enve_cast<Property*>(target);
        if (!prop) { return static_cast<QPixmap*>(nullptr); }
        if (!enve_cast<QrealAnimator*>(prop) &&
            !enve_cast<QPointFAnimator*>(prop)) {
            return static_cast<QPixmap*>(nullptr);
        }
        if (!prop->getFirstAncestor<AdvancedTransformAnimator>()) {
            return static_cast<QPixmap*>(nullptr);
        }
        return BoxSingleWidget::ICON_RESET;
    });
    mMainLayout->addWidget(mResetButton);
    connect(mResetButton, &BoxesListActionButton::pressed,
            this, &BoxSingleWidget::resetPropertyAction);

    mColorButton = new ColorAnimatorButton(nullptr, this);
    mMainLayout->addWidget(mColorButton, Qt::AlignRight);
    mColorButton->setFixedHeight(mColorButton->height() - 2);
    mColorButton->setContentsMargins(0, 1, 0, 1);

    mPropertyComboBox = createCombo(this);
    mMainLayout->addWidget(mPropertyComboBox);

    mBlendModeCombo = createCombo(this);
    mMainLayout->addWidget(mBlendModeCombo);
    mBlendModeCombo->setObjectName("blendModeCombo");

    for(int modeId = int(SkBlendMode::kSrcOver);
        modeId <= int(SkBlendMode::kLastMode); modeId++) {
        const auto mode = static_cast<SkBlendMode>(modeId);
        mBlendModeCombo->addItem(translateBlendModeName(SkBlendMode_Name(mode)), modeId);
    }

    mBlendModeCombo->insertSeparator(8);
    mBlendModeCombo->insertSeparator(14);
    mBlendModeCombo->insertSeparator(21);
    mBlendModeCombo->insertSeparator(25);
    connect(mBlendModeCombo, qOverload<int>(&QComboBox::activated),
            this, &BoxSingleWidget::setCompositionMode);
    mBlendModeCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

    // parent link combo: sits behind the blend mode ("覆盖") dropdown and
    // mirrors the node-link parent of this layer; opening the popup
    // rebuilds the candidate list, picking an entry re-links
    mParentLinkCombo = createCombo(this);
    mMainLayout->addWidget(mParentLinkCombo);
    mParentLinkCombo->setObjectName("parentLinkCombo");
    mParentLinkCombo->setToolTip(tr(
        "Parent link (click to pick, drag onto a layer row)"));
    mParentLinkCombo->addItem(tr("No Parent")); // 无父级
    mParentLinkCombo->setVisible(false);
    // NOTE: showPopup is NOT a signal in Qt5 (it is a protected virtual
    // method) - connecting to it logs "signal not found" at runtime and
    // never fires; the eventFilter refreshes candidates on press instead
    mParentLinkCombo->installEventFilter(this);
    connect(mParentLinkCombo, qOverload<int>(&QComboBox::activated),
            this, [this](const int index) {
        if(mParentLinkComboBuilding) return;
        const auto scene = mParent ? mParent->currentScene() : nullptr;
        const auto box = currentLinkedBox();
        if(!scene || !box) return;
        BoundingBox* picked = nullptr;
        if(index > 0) {
            const quintptr raw =
                mParentLinkCombo->itemData(index).value<quintptr>();
            picked = reinterpret_cast<BoundingBox*>(raw);
        }
        if(picked == box) return;
        if(!picked && !boxHasParentLink(box)) return;
        scene->linkParentLevel(box, picked);
        Document::sInstance->actionFinished();
        refreshParentLinkCombo();
    });
    mParentLinkCombo->setSizePolicy(QSizePolicy::Maximum,
                                    QSizePolicy::Minimum);
    // constant width so the column lines up across all rows regardless
    // of the longest candidate name
    mParentLinkCombo->setFixedWidth(eSizesUI::widget*5);

    // AE-style track matte: ONE dropdown picks the matte layer, and a
    // glyph button next to it cycles the matte mode (alpha / inverted /
    // luma / inverted luma)
    mTrkMatLayerCombo = createCombo(this);
    mMainLayout->addWidget(mTrkMatLayerCombo);
    mTrkMatLayerCombo->setObjectName("trackMatteLayerCombo");
    mTrkMatLayerCombo->setToolTip(tr("Track matte layer"));
    mTrkMatLayerCombo->addItem(tr("None"));
    mTrkMatLayerCombo->setVisible(false);
    mTrkMatLayerCombo->installEventFilter(this);
    connect(mTrkMatLayerCombo, qOverload<int>(&QComboBox::activated),
            this, [this](const int index) {
        if(mTrkMatBuilding) return;
        const auto scene = mParent ? mParent->currentScene() : nullptr;
        const auto box = currentLinkedBox();
        if(!scene || !box) return;
        BoundingBox* picked = nullptr;
        if(index > 0) {
            const quintptr raw =
                mTrkMatLayerCombo->itemData(index).value<quintptr>();
            picked = reinterpret_cast<BoundingBox*>(raw);
        }
        if(picked == box) return;
        box->trackMatteTarget()->setTargetAction(picked);
        if(picked && box->getTrackMatteMode() <= 0) {
            box->setTrackMatteMode(1); // default: alpha matte
        }
        box->prp_afterWholeInfluenceRangeChanged();
        Document::sInstance->actionFinished();
        mTrkMatModeButton->update();
    });
    mTrkMatLayerCombo->setSizePolicy(QSizePolicy::Maximum,
                                     QSizePolicy::Minimum);
    mTrkMatLayerCombo->setFixedWidth(eSizesUI::widget*5);

    mTrkMatModeButton = new PixmapActionButton(this);
    mTrkMatModeButton->setToolTip(tr("Track matte mode"));
    mTrkMatModeButton->setPixmapChooser([this]() {
        if(!mTarget) { return static_cast<QPixmap*>(nullptr); }
        const auto box = currentLinkedBox();
        if(!box) { return static_cast<QPixmap*>(nullptr); }
        const bool hasMatte = box->trackMatteTarget() &&
                box->trackMatteTarget()->getTarget();
        if(!hasMatte) return BoxSingleWidget::ICON_TM_OFF;
        switch(box->getTrackMatteMode()) {
        case 2: return BoxSingleWidget::ICON_TM_ALPHAINV;
        case 3: return BoxSingleWidget::ICON_TM_LUMA;
        case 4: return BoxSingleWidget::ICON_TM_LUMAINV;
        default: return BoxSingleWidget::ICON_TM_ALPHA;
        }
    });
    mMainLayout->addWidget(mTrkMatModeButton);
    connect(mTrkMatModeButton, &BoxesListActionButton::pressed,
            this, [this]() {
        const auto scene = mParent ? mParent->currentScene() : nullptr;
        const auto box = currentLinkedBox();
        if(!scene || !box) return;
        if(!box->trackMatteTarget() ||
           !box->trackMatteTarget()->getTarget()) {
            return; // no matte layer picked yet - nothing to switch
        }
        const int next = box->getTrackMatteMode() % 4 + 1; // cycle 1..4
        box->setTrackMatteMode(next);
        Document::sInstance->actionFinished();
        mTrkMatModeButton->update();
    });
    mTrkMatModeButton->setVisible(false);

    mPathBlendModeCombo = createCombo(this);
    mMainLayout->addWidget(mPathBlendModeCombo);
    mPathBlendModeCombo->addItems(QStringList() << tr("Normal") <<
                                  tr("Add") << tr("Remove") << tr("Remove reverse") <<
                                  tr("Intersect") << tr("Exclude") << tr("Divide"));
    connect(mPathBlendModeCombo, qOverload<int>(&QComboBox::activated),
            this, &BoxSingleWidget::setPathCompositionMode);
    mPathBlendModeCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

    mFillTypeCombo = createCombo(this);
    mMainLayout->addWidget(mFillTypeCombo);
    mFillTypeCombo->addItems(QStringList() << tr("Winding") << tr("Even-odd"));
    connect(mFillTypeCombo, qOverload<int>(&QComboBox::activated),
            this, &BoxSingleWidget::setFillType);
    mFillTypeCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

    mPropertyComboBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    mBoxTargetWidget = new BoxTargetWidget(this);
    mMainLayout->addWidget(mBoxTargetWidget);

    mCheckBox = new BoolPropertyWidget(this);
    mMainLayout->addWidget(mCheckBox);

    //eSizesUI::widget.addHalfSpacing(mMainLayout);

    // selection highlight above every row child (see the class at the
    // bottom of this file); must be created last and raised
    mSelOverlay = new RowHighlightOverlay(this);
    mSelOverlay->raise();

    hide();
    connectAppFont(this);
}

ContainerBox* BoxSingleWidget::getPromoteTargetGroup() {
    if(!mTarget) return nullptr;
    const auto target = mTarget->getTarget();
    ContainerBox* targetGroup = nullptr;
    if(const auto box = enve_cast<ContainerBox*>(target)) {
        if(box->isGroup()) targetGroup = box;
    } else if(enve_cast<RasterEffectCollection*>(target) ||
              enve_cast<BlendEffectCollection*>(target)) {
        const auto pTarget = static_cast<Property*>(target);
        const auto parentBox = pTarget->getFirstAncestor<BoundingBox>();
        if(parentBox && parentBox->isGroup()) {
            targetGroup = static_cast<ContainerBox*>(parentBox);
        }
    }
    // rig containers must never be promoted: the operation rewrites the
    // serialized type and the bone layer / bone would be lost on reload
    if(targetGroup) {
        const auto type = targetGroup->getBoxType();
        if(type == eBoxType::bone || type == eBoxType::boneLayer) {
            return nullptr;
        }
    }
    return targetGroup;
}

void BoxSingleWidget::setCompositionMode(const int id) {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();

    if(const auto boxTarget = enve_cast<BoundingBox*>(target)) {
        const int modeId = mBlendModeCombo->itemData(id).toInt();
        const auto mode = static_cast<SkBlendMode>(modeId);
        boxTarget->setBlendModeSk(mode);
    }
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::setPathCompositionMode(const int id) {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();

    if(const auto pAnim = enve_cast<SmartPathAnimator*>(target)) {
        pAnim->setMode(static_cast<SmartPathAnimator::Mode>(id));
    }
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::setFillType(const int id) {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();

    if(const auto pAnim = enve_cast<SmartPathCollection*>(target)) {
        pAnim->setFillType(static_cast<SkPathFillType>(id));
    }
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::setComboProperty(ComboBoxProperty* const combo) {
    if(!combo) return mPropertyComboBox->hide();
    mPropertyComboBox->clear();
    mPropertyComboBox->addItems(combo->getValueNames());
    mPropertyComboBox->setCurrentIndex(combo->getCurrentValue());
    mTargetConn << connect(combo, &ComboBoxProperty::valueChanged,
                           mPropertyComboBox, &QComboBox::setCurrentIndex);
    mTargetConn << connect(mPropertyComboBox,
                           qOverload<int>(&QComboBox::activated),
                           this, [combo](const int id) {
        combo->setCurrentValue(id);
        Document::sInstance->actionFinished();
    });
    mPropertyComboBox->show();
}

void BoxSingleWidget::handlePropertySelectedChanged(const Property *prop)
{
    if (const auto graph = enve_cast<GraphAnimator*>(prop)) {
        const auto bsvt = static_cast<BoxScroller*>(mParent);
        const auto keysView = bsvt->getKeysView();
        if (keysView) {
            const bool graphSelected = keysView->graphIsSelected(graph);
            const bool isSelected = prop->prp_isSelected();
            if (graphSelected) {
                if (!isSelected) { keysView->graphRemoveViewedAnimator(graph); }
            } else {
                if (isSelected) { keysView->graphAddViewedAnimator(graph); }
            }
            Document::sInstance->actionFinished();
        }
    }
}

ColorAnimator *BoxSingleWidget::getColorTarget() const {
    const auto swt = mTarget->getTarget();
    ColorAnimator * color = nullptr;
    if(const auto ca = enve_cast<ComplexAnimator*>(swt)) {
        color = enve_cast<ColorAnimator*>(swt);
        if(!color) {
            const auto guiProp = ca->ca_getGUIProperty();
            color = enve_cast<ColorAnimator*>(guiProp);
        }
    }
    return color;
}

void BoxSingleWidget::clearAndHideValueAnimators() {
    mValueSlider->setTarget(nullptr);
    mValueSlider->hide();
    mSecondValueSlider->setTarget(nullptr);
    mSecondValueSlider->hide();
}

void BoxSingleWidget::setTargetAbstraction(SWT_Abstraction *abs) {
    mTargetConn.clear();
    SingleWidget::setTargetAbstraction(abs);
    if(!abs) return;
    const auto target = abs->getTarget();
    const auto prop = enve_cast<Property*>(target);
    if(!prop) return;
    mTargetConn << connect(prop, &SingleWidgetTarget::SWT_changedDisabled,
                           this, qOverload<>(&QWidget::update));
    mTargetConn << connect(prop, &Property::prp_nameChanged,
                           this, qOverload<>(&QWidget::update));

    const auto boolProperty = enve_cast<BoolProperty*>(prop);
    const auto boolPropertyContainer = enve_cast<BoolPropertyContainer*>(prop);
    const auto boolAnimator = enve_cast<BoolAnimator*>(prop);
    const auto boxTargetProperty = enve_cast<BoxTargetProperty*>(prop);
    const auto comboBoxProperty = enve_cast<ComboBoxProperty*>(prop);
    const auto animator = enve_cast<Animator*>(prop);
    const auto graphAnimator = enve_cast<GraphAnimator*>(prop);
    const auto complexAnimator = enve_cast<ComplexAnimator*>(prop);
    const auto eboxOrSound = enve_cast<eBoxOrSound*>(prop);
    const auto eindependentSound = enve_cast<eIndependentSound*>(prop);
    const auto eeffect = enve_cast<eEffect*>(prop);
    const auto rasterEffect = enve_cast<RasterEffect*>(prop);
    const auto boundingBox = enve_cast<BoundingBox*>(prop);

    mMainLayout->setContentsMargins(0, 0, boundingBox ? 0 : 5, 0);
    mContentButton->setVisible(complexAnimator);
    mRecordButton->setVisible(animator && !eboxOrSound);
    mVisibleButton->setVisible(eboxOrSound || eeffect || graphAnimator);
    mLockedButton->setVisible(boundingBox);
    mSoloButton->setVisible(eboxOrSound);
    mShyButton->setVisible(eboxOrSound);
    mFxButton->setVisible(boundingBox);
    mTButton->setVisible(boundingBox);
    mResetButton->setVisible(
                (enve_cast<QrealAnimator*>(prop) ||
                 enve_cast<QPointFAnimator*>(prop)) &&
                prop->getFirstAncestor<AdvancedTransformAnimator>());
    m3DButton->setVisible(boundingBox);
    if(boundingBox) {
        if(const auto trans = boundingBox->getBoxTransformAnimator()) {
            mTargetConn << connect(trans, &AdvancedTransformAnimator::box3DChanged,
                                   this, [this]() { m3DButton->update(); });
        }
    }
    mHwSupportButton->setVisible(rasterEffect);
    {
        const auto targetGroup = getPromoteTargetGroup();
        if(boundingBox && targetGroup) {
            mTargetConn << connect(targetGroup,
                                   &ContainerBox::switchedGroupLayer,
                                   this, [this](const eBoxType type) {
                mBlendModeCombo->setEnabled(type == eBoxType::layer);
            });
        }
        mPromoteToLayerButton->setVisible(targetGroup);
        if(targetGroup) {
            mTargetConn << connect(targetGroup, &ContainerBox::switchedGroupLayer,
                                   this, [this](const eBoxType type) {
                mPromoteToLayerButton->setVisible(type == eBoxType::group);
            });
        }
    }
    mBoxTargetWidget->setVisible(boxTargetProperty);
    mCheckBox->setVisible(boolProperty || boolPropertyContainer ||
                         boolAnimator);

    mPropertyComboBox->setVisible(comboBoxProperty);

    mParentLinkCombo->setVisible(false);
    mMbButton->setVisible(false);
    mTrkMatLayerCombo->setVisible(false);
    mTrkMatModeButton->setVisible(false);

    mPathBlendModeVisible = false;
    mBlendModeVisible = false;
    mFillTypeVisible = false;
    // sync highlight with the layer's current selection state (the row
    // may be (re)assigned to a layer that is already selected)
    mSelected = false;
    if(const auto ebs = enve_cast<eBoxOrSound*>(prop)) {
        mSelected = ebs->isSelected();
    }

    mColorButton->setColorTarget(nullptr);
    mValueSlider->setTarget(nullptr);
    mSecondValueSlider->setTarget(nullptr);

    bool valueSliderVisible = false;
    bool secondValueSliderVisible = false;
    bool colorButtonVisible = false;

    if(boundingBox) {
        mBlendModeVisible = true;
        const auto blendName = SkBlendMode_Name(boundingBox->getBlendMode());
        mBlendModeCombo->setCurrentText(blendName);
        mBlendModeCombo->setEnabled(!boundingBox->isGroup());
        mTargetConn << connect(boundingBox, &BoundingBox::blendModeChanged,
                               this, [this](const SkBlendMode mode) {
            mBlendModeCombo->setCurrentText(SkBlendMode_Name(mode));
        });

        // parent-link combo: visible on every layer row; shows the
        // current node-link parent and allows switching it directly
        const auto ebsBox = enve_cast<eBoxOrSound*>(boundingBox);
        if(ebsBox) {
            mParentLinkCombo->setVisible(true);
            mMbButton->setVisible(true);
            mTrkMatLayerCombo->setVisible(true);
            // the mode button stays put on every layer row (dimmed when
            // no matte layer is picked) so the row layout never shifts
            mTrkMatModeButton->setVisible(true);
            rebuildTrkMatLayerCandidates();
            rebuildParentLinkCandidates();
            mTargetConn << connect(ebsBox, &eBoxOrSound::parentChanged,
                                   this, [this](ContainerBox*) {
                rebuildParentLinkCandidates();
            });
        }
    } else if(enve_cast<eSoundObjectBase*>(prop)) {
    } else if(boolProperty) {
        mCheckBox->setTarget(boolProperty);
        mTargetConn << connect(boolProperty, &BoolProperty::valueChanged,
                               this, [this]() { mCheckBox->update(); });
    } else if(boolPropertyContainer) {
        mCheckBox->setTarget(boolPropertyContainer);
        mTargetConn << connect(boolPropertyContainer,
                               &BoolPropertyContainer::valueChanged,
                               this, [this]() { mCheckBox->update(); });
    } else if(comboBoxProperty) {
        setComboProperty(comboBoxProperty);
    } else if(boolAnimator) {
        // a 0/1 channel with no meaningful transition - a checkbox
        // matches the semantics (the value slider implies continuous
        // interpolation); repaint when the frame changes so the check
        // follows the keys during playback/scrubbing
        mCheckBox->setTarget(boolAnimator);
        mTargetConn << connect(boolAnimator,
                               &Property::prp_currentFrameChanged,
                               this, [this](const UpdateReason) {
            mCheckBox->update();
        });
    } else if(const auto qra = enve_cast<QrealAnimator*>(prop)) {
        mValueSlider->setTarget(qra);
        valueSliderVisible = true;
        mValueSlider->setIsLeftSlider(false);
    } else if(complexAnimator) {
        if(const auto col = enve_cast<ColorAnimator*>(prop)) {
            colorButtonVisible = true;
            mColorButton->setColorTarget(col);
        } else if(const auto coll = enve_cast<SmartPathCollection*>(prop)) {
            mFillTypeVisible = true;
            mFillTypeCombo->setCurrentIndex(static_cast<int>(coll->getFillType()));
            mTargetConn << connect(coll, &SmartPathCollection::fillTypeChanged,
                                   this, [this](const SkPathFillType type) {
                mFillTypeCombo->setCurrentIndex(static_cast<int>(type));
            });
        }
        if(complexAnimator && !abs->contentVisible()) {
            if(enve_cast<QPointFAnimator*>(prop)) {
                updateValueSlidersForQPointFAnimator();
                valueSliderVisible = mValueSlider->isVisible();
                secondValueSliderVisible = mSecondValueSlider->isVisible();
            } else {
                const auto guiProp = complexAnimator->ca_getGUIProperty();
                if(const auto qra = enve_cast<QrealAnimator*>(guiProp)) {
                    valueSliderVisible = true;
                    mValueSlider->setTarget(qra);
                    mValueSlider->setIsLeftSlider(false);
                    mSecondValueSlider->setTarget(nullptr);
                } else if(const auto col = enve_cast<ColorAnimator*>(guiProp)) {
                    mColorButton->setColorTarget(col);
                    colorButtonVisible = true;
                } else if(const auto combo = enve_cast<ComboBoxProperty*>(guiProp)) {
                    setComboProperty(combo);
                }
            }
        }
    } else if(boxTargetProperty) {
        mBoxTargetWidget->setTargetProperty(boxTargetProperty);
    } else if(const auto path = enve_cast<SmartPathAnimator*>(prop)) {
        mPathBlendModeVisible = true;
        mPathBlendModeCombo->setCurrentIndex(static_cast<int>(path->getMode()));
        mTargetConn << connect(path, &SmartPathAnimator::pathBlendModeChagned,
                               this, [this](const SmartPathAnimator::Mode mode) {
            mPathBlendModeCombo->setCurrentIndex(static_cast<int>(mode));
        });
    }

    if(animator) {
        mTargetConn << connect(animator, &Animator::anim_isRecordingChanged,
                               this, [this]() { mRecordButton->update(); });
    }
    if(eeffect) {
        if(rasterEffect) {
            mTargetConn << connect(rasterEffect, &RasterEffect::hardwareSupportChanged,
                                   this, [this]() { mHwSupportButton->update(); });
        }

        mTargetConn << connect(eeffect, &eEffect::effectVisibilityChanged,
                               this, [this]() { mVisibleButton->update(); });
    }
    if(boundingBox || eindependentSound) {
        const auto ptr = static_cast<eBoxOrSound*>(prop);
        mTargetConn << connect(ptr, &eBoxOrSound::visibilityChanged,
                               this, [this]() { mVisibleButton->update(); });
        mTargetConn << connect(ptr, &eBoxOrSound::selectionChanged,
                               this, [this](const bool selected) {
            mSelected = selected;
            update();
        });
        mTargetConn << connect(ptr, &eBoxOrSound::lockedChanged,
                               this, [this]() { mLockedButton->update(); });
        mTargetConn << connect(ptr, &eBoxOrSound::soloChanged,
                               this, [this]() { mSoloButton->update(); });
        mTargetConn << connect(ptr, &eBoxOrSound::shyChanged,
                               this, [this]() { mShyButton->update(); });
        mTargetConn << connect(ptr, &eBoxOrSound::labelColorChanged,
                               this, [this](const QColor&) {
            mBoxButton->update();
            update(); // full row (label color wash)
        });
    }
    if(boundingBox) {
        mTargetConn << connect(boundingBox, &BoundingBox::effectsEnabledChanged,
                               this, [this]() { mFxButton->update(); });
        mTargetConn << connect(boundingBox, &BoundingBox::preserveAlphaChanged,
                               this, [this]() { mTButton->update(); });
    }
    if(!boundingBox && !eindependentSound) {
        mTargetConn << connect(prop, &Property::prp_selectionChanged,
                               this, qOverload<>(&QWidget::update));
        mTargetConn << connect(prop, &Property::prp_selectionChanged,
                               this, [this, prop]() { handlePropertySelectedChanged(prop); });
    }

    mValueSlider->setVisible(valueSliderVisible);
    mSecondValueSlider->setVisible(secondValueSliderVisible);
    mColorButton->setVisible(colorButtonVisible);

    updateCompositionBoxVisible();
    updatePathCompositionBoxVisible();
    updateFillTypeBoxVisible();
}

void BoxSingleWidget::loadStaticPixmaps(int iconSize)
{
    if (sStaticPixmapsLoaded) { return; }
    if (!ThemeSupport::hasIconSize(iconSize)) {
        QMessageBox::warning(nullptr,
                             tr("Scaling issues"),
                             tr("<p>Requested icon size <b>%1</b> is not available,"
                                " expect blurry icons and possible UI size issues. This is usually related to font scaling.</p>"
                                "<p>Disable <b>'HiDPI PassThrough'</b> in preferences, then restart Friction.</p>").arg(iconSize));
    }
    const auto pixmapSize = ThemeSupport::getIconSize(iconSize);
    qDebug() << "pixmaps size" << pixmapSize;
    VISIBLE_ICON = new QPixmap(QIcon::fromTheme("visible").pixmap(pixmapSize));
    INVISIBLE_ICON = new QPixmap(QIcon::fromTheme("hidden").pixmap(pixmapSize));
    BOX_CHILDREN_VISIBLE_ICON = new QPixmap(QIcon::fromTheme("visible-child").pixmap(pixmapSize));
    BOX_CHILDREN_HIDDEN_ICON = new QPixmap(QIcon::fromTheme("hidden-child").pixmap(pixmapSize));
    ANIMATOR_CHILDREN_VISIBLE_ICON = new QPixmap(QIcon::fromTheme("visible-child-small").pixmap(pixmapSize));
    ANIMATOR_CHILDREN_HIDDEN_ICON = new QPixmap(QIcon::fromTheme("hidden-child-small").pixmap(pixmapSize));
    LOCKED_ICON = new QPixmap(QIcon::fromTheme("locked").pixmap(pixmapSize));
    UNLOCKED_ICON = new QPixmap(QIcon::fromTheme("unlocked").pixmap(pixmapSize));
    MUTED_ICON = new QPixmap(QIcon::fromTheme("muted").pixmap(pixmapSize));
    UNMUTED_ICON = new QPixmap(QIcon::fromTheme("unmuted").pixmap(pixmapSize));
    ANIMATOR_RECORDING_ICON = new QPixmap(QIcon::fromTheme("record").pixmap(pixmapSize));
    ANIMATOR_NOT_RECORDING_ICON = new QPixmap(QIcon::fromTheme("norecord").pixmap(pixmapSize));
    ANIMATOR_DESCENDANT_RECORDING_ICON = new QPixmap(QIcon::fromTheme("record-child").pixmap(pixmapSize));
    C_ICON = new QPixmap(QIcon::fromTheme("cpu-active").pixmap(pixmapSize));
    G_ICON = new QPixmap(QIcon::fromTheme("gpu-active").pixmap(pixmapSize));
    CG_ICON = new QPixmap(QIcon::fromTheme("cpu-gpu").pixmap(pixmapSize));
    GRAPH_PROPERTY_ICON = new QPixmap(QIcon::fromTheme("graph_property_2").pixmap(pixmapSize));
    PROMOTE_TO_LAYER_ICON = new QPixmap(QIcon::fromTheme("layer").pixmap(pixmapSize));

    // 2.5D layer toggle icons: hexagon text glyphs (vector font shapes,
    // rasterized at the actual device size -> always sharp, no scaling).
    // on  = U+2B22 black hexagon (white),
    // off = U+2B21 white hexagon  (dim outline)
    {
        const qreal dpr = qApp->desktop()->devicePixelRatioF();
        const auto makeHexIcon = [&pixmapSize, dpr](const ushort glyph,
                                                    const QColor& color) {
            auto pm = new QPixmap(pixmapSize * dpr);
            pm->setDevicePixelRatio(dpr);
            pm->fill(Qt::transparent);
            QPainter p(pm);
            p.setRenderHint(QPainter::TextAntialiasing);
            QFont f = qApp->font();
            f.setPixelSize(qRound(pixmapSize.height() * 0.82));
            f.setBold(true);
            p.setFont(f);
            p.setPen(color);
            p.drawText(QRectF(QPointF(0, 0), pixmapSize),
                       Qt::AlignCenter, QChar(glyph));
            p.end();
            return pm;
        };
        ICON_3D_ON = makeHexIcon(0x2B22, QColor(255, 255, 255));
        ICON_3D_OFF = makeHexIcon(0x2B21, QColor(150, 150, 150));
    }

    // reset icon: clockwise open circle arrow glyph U+21BB (vector font
    // shape, rasterized at the actual device size -> always sharp)
    {
        const qreal dpr = qApp->desktop()->devicePixelRatioF();
        auto pm = new QPixmap(pixmapSize * dpr);
        pm->setDevicePixelRatio(dpr);
        pm->fill(Qt::transparent);
        QPainter p(pm);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont f = qApp->font();
        f.setPixelSize(qRound(pixmapSize.height() * 0.86));
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        p.drawText(QRectF(QPointF(0, 0), pixmapSize),
                   Qt::AlignCenter, QChar(0x21BB));
        p.end();
        ICON_RESET = pm;
    }

    // AE-style layer switch glyphs: plain text characters rasterized at
    // the actual device size (S = solo, H = shy, fx = effects, T = preserve
    // underlying transparency); bright = active, dim = inactive
    {
        const qreal dpr = qApp->desktop()->devicePixelRatioF();
        const auto makeTextIcon = [&pixmapSize, dpr](const QString& text,
                                                     const QColor& color,
                                                     const qreal sizeFactor) {
            auto pm = new QPixmap(pixmapSize * dpr);
            pm->setDevicePixelRatio(dpr);
            pm->fill(Qt::transparent);
            QPainter p(pm);
            p.setRenderHint(QPainter::TextAntialiasing);
            QFont f = qApp->font();
            f.setPixelSize(qRound(pixmapSize.height() * sizeFactor));
            f.setBold(true);
            p.setFont(f);
            p.setPen(color);
            p.drawText(QRectF(QPointF(0, 0), pixmapSize),
                       Qt::AlignCenter, text);
            p.end();
            return pm;
        };
        const QColor active(255, 255, 255);
        const QColor inactive(150, 150, 150);
        ICON_SOLO_ON = makeTextIcon("S", active, 0.78);
        ICON_SOLO_OFF = makeTextIcon("S", inactive, 0.78);
        ICON_SHY_ON = makeTextIcon("H", active, 0.78);
        ICON_SHY_OFF = makeTextIcon("H", inactive, 0.78);
        ICON_FX_ON = makeTextIcon("fx", active, 0.62);
        ICON_FX_OFF = makeTextIcon("fx", inactive, 0.62);
        ICON_MB_ON = makeTextIcon("MB", active, 0.52);
        ICON_MB_OFF = makeTextIcon("MB", inactive, 0.52);
        ICON_T_ON = makeTextIcon("T", active, 0.78);
        ICON_T_OFF = makeTextIcon("T", inactive, 0.78);
        ICON_LINKNODE_ON = makeTextIcon(QChar(0x25CE), active, 0.86);
        ICON_LINKNODE_OFF = makeTextIcon(QChar(0x25CE), inactive, 0.86);
        ICON_TM_ALPHA = makeTextIcon(QChar(0x25CF), active, 0.82);    // ●
        ICON_TM_ALPHAINV = makeTextIcon(QChar(0x25CB), active, 0.82); // ○
        ICON_TM_LUMA = makeTextIcon(QChar(0x25D0), active, 0.82);     // ◐
        ICON_TM_LUMAINV = makeTextIcon(QChar(0x25D1), active, 0.82);  // ◑
        ICON_TM_OFF = makeTextIcon(QChar(0x25CF), inactive, 0.82);    // dim ●
    }

    BOX_PATH = new QPixmap(QIcon::fromTheme("pathCreate").pixmap(pixmapSize));
    BOX_CIRCLE = new QPixmap(QIcon::fromTheme("circleCreate").pixmap(pixmapSize));
    BOX_RECT = new QPixmap(QIcon::fromTheme("rectCreate").pixmap(pixmapSize));
    BOX_TEXT = new QPixmap(QIcon::fromTheme("textCreate").pixmap(pixmapSize));
    BOX_NULL = new QPixmap(QIcon::fromTheme("nullCreate").pixmap(pixmapSize));
    // camera layer icon: rounded body + lens circle
    {
        const int isz = qMax(8, pixmapSize.width()*4/5);
        const QColor col = ThemeSupport::getThemeColorYellow();
        QPixmap pc(isz, isz); pc.fill(Qt::transparent);
        QPainter c(&pc); c.setRenderHint(QPainter::Antialiasing);
        QPen cpen(col); cpen.setWidthF(2.0); cpen.setCapStyle(Qt::RoundCap);
        c.setPen(cpen);
        c.setBrush(Qt::NoBrush);
        c.drawRoundedRect(QRectF(0.10*isz, 0.30*isz, 0.80*isz, 0.52*isz),
                          0.12*isz, 0.12*isz);
        c.drawEllipse(QPointF(0.5*isz, 0.56*isz), 0.17*isz, 0.17*isz);
        c.drawLine(QPointF(0.32*isz, 0.30*isz),
                   QPointF(0.40*isz, 0.18*isz));
        c.drawLine(QPointF(0.40*isz, 0.18*isz),
                   QPointF(0.60*isz, 0.18*isz));
        c.drawLine(QPointF(0.60*isz, 0.18*isz),
                   QPointF(0.68*isz, 0.30*isz));
        c.end();
        BOX_CAMERA = new QPixmap(pc);
    }
    {
        // bone row icon: the U+1F9B4 glyph (text presentation, never a
        // colored emoji); bone layer: layered two-segment glyph mark
        const int isz = qMax(8, pixmapSize.width()*4/5);
        const QColor col = ThemeSupport::getThemeColorYellow();
        {
            QPixmap pb(isz, isz); pb.fill(Qt::transparent);
            QPainter b(&pb);
            QFont f(QStringLiteral("Segoe UI Symbol"));
            f.setPixelSize(qRound(isz*0.9));
            b.setFont(f);
            b.setPen(col);
            const QString glyph = QString(QChar(0xD83E)) +
                    QChar(0xDCB4) + QChar(0xFE0E);
            b.drawText(pb.rect(), Qt::AlignCenter, glyph);
            b.end();
            BOX_BONE = new QPixmap(pb);
        }
        QPixmap pl(isz, isz); pl.fill(Qt::transparent);
        QPainter l(&pl); l.setRenderHint(QPainter::Antialiasing);
        QPen pen(col); pen.setWidthF(2.2); pen.setCapStyle(Qt::RoundCap);
        l.setPen(pen);
        l.drawLine(QPointF(0.15*isz, 0.85*isz), QPointF(0.55*isz, 0.45*isz));
        l.drawLine(QPointF(0.45*isz, 0.55*isz), QPointF(0.85*isz, 0.15*isz));
        l.end();
        BOX_BONELAYER = new QPixmap(pl);
        // solid layer: filled rounded square with border (flat-color
        // plane)
        QPixmap ps(isz, isz); ps.fill(Qt::transparent);
        QPainter s(&ps); s.setRenderHint(QPainter::Antialiasing);
        const QRectF r(0.14*isz, 0.26*isz, 0.72*isz, 0.48*isz);
        s.setPen(QPen(col, 2.2, Qt::SolidLine, Qt::RoundCap,
                               Qt::RoundJoin));
        s.setBrush(col);
        s.drawRoundedRect(r, 0.12*isz, 0.12*isz);
        s.end();
        BOX_SOLID = new QPixmap(ps);
    }
    BOX_IMAGE = new QPixmap(QIcon::fromTheme("image-x-generic").pixmap(pixmapSize));
    BOX_VIDEO = new QPixmap(QIcon::fromTheme("file_movie").pixmap(pixmapSize));
    BOX_SOUND = new QPixmap(QIcon::fromTheme("audio-x-generic").pixmap(pixmapSize));
    BOX_GROUP = new QPixmap(QIcon::fromTheme("group").pixmap(pixmapSize));
    BOX_LINK = new QPixmap(QIcon::fromTheme("linked").pixmap(pixmapSize));
    BOX_SEQ = new QPixmap(QIcon::fromTheme("renderlayers").pixmap(pixmapSize));

    sStaticPixmapsLoaded = true;
}

void BoxSingleWidget::clearStaticPixmaps()
{
    if (!sStaticPixmapsLoaded) { return; }

    delete VISIBLE_ICON;
    delete INVISIBLE_ICON;
    delete BOX_CHILDREN_VISIBLE_ICON;
    delete BOX_CHILDREN_HIDDEN_ICON;
    delete ANIMATOR_CHILDREN_VISIBLE_ICON;
    delete ANIMATOR_CHILDREN_HIDDEN_ICON;
    delete LOCKED_ICON;
    delete UNLOCKED_ICON;
    delete MUTED_ICON;
    delete UNMUTED_ICON;
    delete ANIMATOR_RECORDING_ICON;
    delete ANIMATOR_NOT_RECORDING_ICON;
    delete ANIMATOR_DESCENDANT_RECORDING_ICON;
    delete PROMOTE_TO_LAYER_ICON;
    delete C_ICON;
    delete G_ICON;
    delete CG_ICON;
    delete GRAPH_PROPERTY_ICON;
    delete ICON_3D_ON;
    delete ICON_3D_OFF;
    delete ICON_RESET;
    delete ICON_SOLO_ON;
    delete ICON_SOLO_OFF;
    delete ICON_SHY_ON;
    delete ICON_SHY_OFF;
    delete ICON_FX_ON;
    delete ICON_FX_OFF;
    delete ICON_T_ON;
    delete ICON_T_OFF;
    delete ICON_LINKNODE_ON;
    delete ICON_LINKNODE_OFF;

    delete BOX_PATH;
    delete BOX_CIRCLE;
    delete BOX_RECT;
    delete BOX_TEXT;
    delete BOX_NULL;
    delete BOX_BONE;
    delete BOX_BONELAYER;
    delete BOX_SOLID;
    delete BOX_CAMERA;
    delete BOX_IMAGE;
    delete BOX_VIDEO;
    delete BOX_SOUND;
    delete BOX_GROUP;
    delete BOX_LINK;
    delete BOX_SEQ;
}

void BoxSingleWidget::mousePressEvent(QMouseEvent *event) {
    if(!mTarget) return;
    if(event->x() < mFillWidget->x() ||
       event->x() > mFillWidget->x() + mFillWidget->width()) return;
    const auto target = mTarget->getTarget();
    if(event->button() == Qt::RightButton) {
        setSelected(true);
        QMenu menu(this);

        if(const auto pTarget = enve_cast<Property*>(target)) {
            const bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
            if(enve_cast<BoundingBox*>(target) || enve_cast<eIndependentSound*>(target)) {
                const auto box = static_cast<eBoxOrSound*>(target);
                if(!box->isSelected()) box->selectionChangeTriggered(shiftPressed);
            } else {
                if(!pTarget->prp_isSelected()) pTarget->prp_selectionChangeTriggered(shiftPressed);
            }
            PropertyMenu pMenu(&menu, mParent->currentScene(), MainWindow::sGetInstance());
            pTarget->prp_setupTreeViewMenu(&pMenu);
        }
        // timeline tracks: sounds carry no BoundingBox context menu (the
        // regular Merge into Track action lives there), so selected
        // same-kind sibling sound rows can merge right from this menu.
        // Captures are guarded - the nested menu loop may outlive them
        if(const auto bos = enve_cast<eBoxOrSound*>(target)) {
            if(bos->isAudioKind() && bos->isSelected()) {
                const auto parent = bos->getParentGroup();
                if(parent) {
                    QList<QPointer<eBoxOrSound>> mates;
                    for(const auto& c : parent->getContained()) {
                        if(c && c != bos && c->isSelected() &&
                           c->isAudioKind()) {
                            mates << c.data();
                        }
                    }
                    if(!mates.isEmpty() && mParent->currentScene()) {
                        const QPointer<Canvas> sceneGuard =
                                mParent->currentScene();
                        const QPointer<eBoxOrSound> bosGuard = bos;
                        menu.addSeparator();
                        menu.addAction(tr("Merge into Track"), this,
                                       [sceneGuard, bosGuard, mates]() {
                            if(!sceneGuard || !bosGuard) return;
                            QList<eBoxOrSound*> live;
                            for(const auto& m : mates) {
                                if(m) live << m.data();
                            }
                            sceneGuard->combineIntoTrack(bosGuard, live);
                        });
                    }
                }
            }
        }
        // timeline track: allow a member to leave its track. The new
        // active row of the remaining members is revealed synchronously
        // here; without that it only pops up when the queued
        // enforceTrack runs one event-loop tick later, visibly flashing
        // the row list through a second rebuild pass
        if(const auto bos2 = enve_cast<eBoxOrSound*>(target)) {
            if(bos2->isInTrack()) {
                const int tid = bos2->trackId();
                eBoxOrSound* nextActive = nullptr;
                if(const auto parent = bos2->getParentGroup()) {
                    for(const auto& c : parent->getContained()) {
                        if(c && c != bos2 && c->trackId() == tid) {
                            nextActive = c.data();
                            break;
                        }
                    }
                }
                menu.addSeparator();
                menu.addAction(tr("Detach from Track"), this,
                               [bos2Q = QPointer<eBoxOrSound>(bos2),
                                nextQ = QPointer<eBoxOrSound>(nextActive)]() {
                    if(!bos2Q) return;
                    bos2Q->setTrackId(-1);
                    // same visual end-state the queued enforcement will
                    // settle into, but applied within the same pass
                    if(nextQ && nextQ->isHiddenByTrack()) {
                        nextQ->setHiddenByTrack(false);
                    }
                    Document::sInstance->actionFinished();
                });
            }
        }
        // convert a plain group (e.g. a flattened PSD import root)
        // into a bone layer: Moho-style the artwork then lives in the
        // bone layer while the bones drive it
        if(const auto cont = enve_cast<ContainerBox*>(target)) {
            if(cont->getBoxType() == eBoxType::group && !cont->isLink()) {
                menu.addSeparator();
                menu.addAction(
                            BoxSingleWidget::tr("Convert to Bone Layer"),
                            this,
                            [contQ = QPointer<ContainerBox>(cont),
                             sceneQ = QPointer<Canvas>(
                                 mParent->currentScene())]() {
                    if(!contQ) return;
                    const auto bl = BoneLayer::convertFromGroup(contQ);
                    if(bl && sceneQ) {
                        sceneQ->clearBoxesSelection();
                        sceneQ->addBoxToSelection(bl);
                    }
                });
            }
        }
        // layer-side bone binding: list every bone in the scene, picking
        // one re-parents the currently selected layers into it (world
        // position preserved) - Moho-style bind flow
        if(const auto box = enve_cast<BoundingBox*>(target)) {
            if(!enve_cast<Bone*>(box) && !enve_cast<BoneLayer*>(box) &&
               mParent && mParent->currentScene()) {
                // bound layer: offer a direct unbind back to the bone
                // layer (single layer, world appearance preserved)
                if(const auto hostBone =
                        enve_cast<Bone*>(box->getParentGroup())) {
                    menu.addSeparator();
                    menu.addAction(
                                BoxSingleWidget::tr("Unbind from Bone"),
                                this,
                                [boxQ = QPointer<BoundingBox>(box),
                                 boneQ = QPointer<Bone>(hostBone)]() {
                        if(boxQ && boneQ) boneQ->unbindLayer(boxQ);
                    });
                }
                QList<QPointer<Bone>> bones;
                std::function<void(ContainerBox*)> walkBones =
                        [&walkBones, &bones](ContainerBox* const cont) {
                    for(const auto& b : cont->getContainedBoxes()) {
                        if(const auto bone = enve_cast<Bone*>(b)) {
                            bones << bone;
                            walkBones(bone);
                        } else if(const auto g =
                                  enve_cast<ContainerBox*>(b)) {
                            walkBones(g);
                        }
                    }
                };
                walkBones(mParent->currentScene());
                if(!bones.isEmpty()) {
                    menu.addSeparator();
                    auto bindMenu = menu.addMenu(
                                BoxSingleWidget::tr("Bind to Bone"));
                    for(const auto& boneQ : bones) {
                        if(!boneQ) continue;
                        bindMenu->addAction(boneQ->prp_getName(),
                                            this, [boneQ]() {
                            if(boneQ) boneQ->bindSelectedLayers();
                        });
                    }
                }
            }
        }
        menu.exec(event->globalPos());
        setSelected(false);
    } else {
        mDragPressPos = event->pos().x() > mFillWidget->x();
        mDragStartPos = event->pos();
    }
    Document::sInstance->actionFinished();
}

void BoxSingleWidget::mouseMoveEvent(QMouseEvent *event) {
    if(!mTarget) return;
    if(!mDragPressPos) return;
    if(!(event->buttons() & Qt::LeftButton)) return;
    const auto dist = (event->pos() - mDragStartPos).manhattanLength();
    if(dist < QApplication::startDragDistance()) return;
    const auto drag = new QDrag(this);
    {
        const auto prop = static_cast<Property*>(mTarget->getTarget());
        const QString name = translatePropertyName(prop->prp_getName());
        const int nameWidth = QApplication::fontMetrics().horizontalAdvance(name);
        QPixmap pixmap(mFillWidget->x() + nameWidth + eSizesUI::widget, height());
        render(&pixmap);
        drag->setPixmap(pixmap);
    }
    connect(drag, &QDrag::destroyed, this, &BoxSingleWidget::clearSelected);

    const auto mimeData = mTarget->getTarget()->SWT_createMimeData();
    if(!mimeData) return;
    setSelected(true);
    drag->setMimeData(mimeData);

    drag->installEventFilter(MainWindow::sGetInstance());
    drag->exec(Qt::CopyAction | Qt::MoveAction);
}

void BoxSingleWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!mTarget) { return; }
    const auto target = mTarget->getTarget();

    const auto bbox = enve_cast<BoundingBox*>(target);
    if (event->button() == Qt::MidButton && bbox) {
        PropertyNameDialog::sRenameBox(bbox, this);
        return;
    }

    if (event->x() < mFillWidget->x() ||
        event->x() > mFillWidget->x() + mFillWidget->width()) { return; }
    setSelected(false);

    if (pointToLen(event->pos() - mDragStartPos) > eSizesUI::widget/2) { return; }

    const bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
    if (enve_cast<BoundingBox*>(target) || enve_cast<eIndependentSound*>(target)) {
        const auto boxTarget = static_cast<eBoxOrSound*>(target);
        boxTarget->selectionChangeTriggered(shiftPressed);
        Document::sInstance->actionFinished();
    } else if (const auto pTarget = enve_cast<Property*>(target)) {
        pTarget->prp_selectionChangeTriggered(shiftPressed);
    }
}

void BoxSingleWidget::enterEvent(QEvent *)
{
#ifdef Q_OS_MAC
    setFocus();
#endif
    mHover = true;
    update();
}

void BoxSingleWidget::leaveEvent(QEvent *)
{
#ifdef Q_OS_MAC
    KeyFocusTarget::KFT_sSetLastTarget();
#endif
    mHover = false;
    update();
}

#ifdef Q_OS_MAC
void BoxSingleWidget::keyPressEvent(QKeyEvent *event)
{
    if (mHover) {
        MainWindow::sGetInstance()->processBoxesListKeyEvent(event);
    }
    SingleWidget::keyPressEvent(event);
}
#endif

void BoxSingleWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    Q_UNUSED(e)
    switchContentVisibleAction();
}

void BoxSingleWidget::prp_drawTimelineControls(QPainter * const p,
                               const qreal pixelsPerFrame,
                               const FrameRange &viewedFrames) {
    if(isHidden() || !mTarget) return;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        asAnim->prp_drawTimelineControls(
                    p, pixelsPerFrame, viewedFrames, eSizesUI::widget);
    }
}

Key* BoxSingleWidget::getKeyAtPos(const int pressX,
                                  const qreal pixelsPerFrame,
                                  const int minViewedFrame) {
    if(isHidden() || !mTarget) return nullptr;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        return asAnim->anim_getKeyAtPos(pressX, minViewedFrame,
                                        pixelsPerFrame, KEY_RECT_SIZE);
    }
    return nullptr;
}

TimelineMovable* BoxSingleWidget::getRectangleMovableAtPos(
                            const int pressX,
                            const qreal pixelsPerFrame,
                            const int minViewedFrame) {
    if(isHidden() || !mTarget) return nullptr;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        return asAnim->anim_getTimelineMovable(
                    pressX, minViewedFrame, pixelsPerFrame);
    }
    return nullptr;
}

eBoxOrSound* BoxSingleWidget::getTrackClipAtPos(
                            const int pressX,
                            const qreal pixelsPerFrame,
                            const int minViewedFrame) {
    if(isHidden() || !mTarget) return nullptr;
    const auto bos = enve_cast<eBoxOrSound*>(mTarget->getTarget());
    if(!bos || !bos->isInTrack()) return nullptr;
    return bos->trackMemberAtX(pressX, minViewedFrame, pixelsPerFrame);
}

void BoxSingleWidget::getKeysInRect(const QRectF &selectionRect,
                                    const qreal pixelsPerFrame,
                                    QList<Key*>& listKeys) {
    if(isHidden() || !mTarget) return;
    const auto target = mTarget->getTarget();
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        asAnim->anim_getKeysInRect(selectionRect, pixelsPerFrame,
                                   listKeys, KEY_RECT_SIZE);
    }
}

void BoxSingleWidget::paintEvent(QPaintEvent *) {
    if(!mTarget) return;
    QPainter p(this);
    const auto target = mTarget->getTarget();
    const auto prop = enve_cast<Property*>(target);
    if(!prop) return;
    if(prop->SWT_isDisabled()) p.setOpacity(.5);

    int nameX = mFillWidget->x();

    if (mHover) { p.fillRect(rect(), ThemeSupport::getThemeHighlightColor(40)); }

    // AE-style label color wash on the layer name area
    const auto bsLabel = enve_cast<eBoxOrSound*>(prop);
    if (bsLabel) {
        const QColor lc = bsLabel->getLabelColor();
        if (lc.isValid()) {
            QColor wash = lc;
            wash.setAlpha(40);
            p.fillRect(mFillWidget->geometry(), wash);
        }
    }

    const auto bsTarget = enve_cast<eBoxOrSound*>(prop);
    if (!bsTarget && prop->prp_isSelected()) {
        p.fillRect(mFillWidget->geometry(),
                   ThemeSupport::getThemeHighlightSelectedColor(25));
    }
    if (bsTarget) {
        nameX += eSizesUI::widget/4;
        const bool ss = enve_cast<eSoundObjectBase*>(prop);
        if (ss || enve_cast<BoundingBox*>(prop)) {
            p.fillRect(rect(), QColor(0, 0, 0, 50));
            if (bsTarget->isSelected()) {
                p.fillRect(mFillWidget->geometry(),
                           ThemeSupport::getThemeHighlightSelectedColor(50));
                p.setPen(Qt::white);
            } else {
                p.setPen(Qt::white);
            }
        } else if (enve_cast<BlendEffectBoxShadow*>(prop)) {
            p.fillRect(rect(), QColor(0, 255, 125, 50));
            nameX += eSizesUI::widget;
        }
    } else if(!enve_cast<ComplexAnimator*>(prop)) {
        if(const auto graphAnim = enve_cast<GraphAnimator*>(prop)) {
            const auto bswvp = static_cast<BoxScroller*>(mParent);
            const auto keysView = bswvp->getKeysView();
            if(keysView) {
                const bool selected = keysView->graphIsSelected(graphAnim);
                if(selected) {
                    const int id = keysView->graphGetAnimatorId(graphAnim);
                    const auto color = id >= 0 ?
                                keysView->sGetAnimatorColor(id) :
                                QColor(Qt::black);
                    const QRect visRect(mVisibleButton->pos(),
                                        mVisibleButton->size());
                    const int adj = qRound(4*qreal(mVisibleButton->width())/20);
                    p.fillRect(visRect.adjusted(adj, adj, -adj, -adj), color);
                }
            }
            if(const auto path = enve_cast<SmartPathAnimator*>(prop)) {
                const QRect colRect(QPoint{nameX, 0},
                                    QSize{eSizesUI::widget, eSizesUI::widget});
                p.setPen(Qt::NoPen);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setBrush(path->getPathColor());
                const int radius = qRound(eSizesUI::widget*0.2);
                p.drawEllipse(colRect.center() + QPoint(0, 2),
                              radius, radius);
                p.setRenderHint(QPainter::Antialiasing, false);
                nameX += eSizesUI::widget;
            }
        } else nameX += eSizesUI::widget;

        if(!enve_cast<Animator*>(prop)) nameX += eSizesUI::widget;
        p.setPen(Qt::white);
    } else { //if(enve_cast<ComplexAnimator*>(target)) {
        p.setPen(Qt::white);
    }

    // track membership chip: glyph + member count, shifts the name right
    if (bsTarget && bsTarget->isInTrack()) {
        const int members = bsTarget->trackMembers().count();
        const QString badge = QStringLiteral("T") +
                              QString::number(members);
        const int chipW = p.fontMetrics().horizontalAdvance(badge) + 8;
        const int chipH = qRound(eSizesUI::widget*0.62);
        const QRect chip(nameX, (eSizesUI::widget - chipH)/2, chipW, chipH);
        p.setRenderHint(QPainter::Antialiasing);
        QColor chipCol = bsTarget->getLabelColor();
        if (!chipCol.isValid()) { chipCol = QColor(70, 130, 220); }
        chipCol.setAlpha(150);
        p.setPen(Qt::NoPen);
        p.setBrush(chipCol);
        p.drawRoundedRect(chip, 4, 4);
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, badge);
        p.setRenderHint(QPainter::Antialiasing, false);
        nameX += chipW + 4;
    }

    const QRect textRect(nameX, 0, width() - nameX - eSizesUI::widget, eSizesUI::widget);
    const QString name = translatePropertyName(prop->prp_getName());
    QTextOption opts(Qt::AlignVCenter);
    opts.setWrapMode(QTextOption::NoWrap);
    // keep long names inside the fixed name column - they must never
    // draw under the widgets that follow it
    p.setClipRect(mFillWidget->geometry());
    p.drawText(textRect, name, opts);
    p.setClipRect(rect());
    // selection highlight lives on the topmost overlay (mSelOverlay)
    p.end();
}

// the layer row this widget represents, as a linkable box
BoundingBox *BoxSingleWidget::currentLinkedBox() {
    if(!mTarget) return nullptr;
    return enve_cast<BoundingBox*>(mTarget->getTarget());
}

// first existing ParentEffect of this box (the node-link carrier)
static ParentEffect* findParentEffect(BoundingBox* const box) {
    const auto coll = box->getTransformEffectCollection();
    if(!coll) return nullptr;
    const int n = coll->ca_getNumberOfChildren();
    for(int i = 0; i < n; i++) {
        if(const auto pe = enve_cast<ParentEffect*>(coll->getChild(i)))
            return pe;
    }
    return nullptr;
}

// rebuild the dropdown entries: "无父级" plus every scene box that is
// neither this box nor inside its subtree (those would form a cycle);
// preselects the currently linked parent, if any
void BoxSingleWidget::rebuildParentLinkCandidates() {
    mParentLinkComboBuilding = true;
    const QSignalBlocker blocker(mParentLinkCombo);
    const auto scene = mParent ? mParent->currentScene() : nullptr;
    const auto box = currentLinkedBox();
    mParentLinkCombo->clear();
    mParentLinkCombo->addItem(tr("No Parent"));
    int match = 0;
    if(box && scene) {
        BoundingBox* cur = nullptr;
        if(const auto pe = findParentEffect(box)) {
            cur = enve_cast<BoundingBox*>(
                        pe->parentTargetProperty()->getTarget());
        }
        std::function<void(ContainerBox*)> walk =
                [this, &walk, &match, box, &cur](ContainerBox* const cont) {
            for(const auto b : cont->getContainedBoxes()) {
                if(!b || b == box || box->isAncestor(b)) continue;
                // exclude boxes whose link chain already reaches this
                // box - picking one would close a link-graph cycle
                if(b->hasInParentLinkChain(box)) continue;
                mParentLinkCombo->addItem(
                            b->prp_getName(),
                            QVariant::fromValue(reinterpret_cast<quintptr>(b)));
                if(b == cur) match = mParentLinkCombo->count() - 1;
                if(const auto g = enve_cast<ContainerBox*>(b)) walk(g);
            }
        };
        walk(scene);
    }
    mParentLinkCombo->setCurrentIndex(match);
    mParentLinkComboBuilding = false;
}

// matte layer candidates: every scene box except this one and its own
// subtree (those would matte with themselves / their dependents)
void BoxSingleWidget::rebuildTrkMatLayerCandidates() {
    mTrkMatBuilding = true;
    const QSignalBlocker blocker(mTrkMatLayerCombo);
    const auto scene = mParent ? mParent->currentScene() : nullptr;
    const auto box = currentLinkedBox();
    mTrkMatLayerCombo->clear();
    mTrkMatLayerCombo->addItem(tr("None"));
    int match = 0;
    if(box && scene) {
        BoundingBox* cur = box->trackMatteTarget() ?
                    box->trackMatteTarget()->getTarget() : nullptr;
        std::function<void(ContainerBox*)> walk =
                [this, &walk, &match, box, &cur](ContainerBox* const cont) {
            for(const auto b : cont->getContainedBoxes()) {
                if(!b || b == box || box->isAncestor(b)) continue;
                // exclude boxes whose matte chain already reaches this
                // box - picking one would close a matte cycle
                if(b->matteChainReaches(box)) continue;
                mTrkMatLayerCombo->addItem(
                            b->prp_getName(),
                            QVariant::fromValue(reinterpret_cast<quintptr>(b)));
                if(b == cur) match = mTrkMatLayerCombo->count() - 1;
                if(const auto g = enve_cast<ContainerBox*>(b)) walk(g);
            }
        };
        walk(scene);
    }
    mTrkMatLayerCombo->setCurrentIndex(match);
    mTrkMatBuilding = false;
}

// lightweight text sync between popups: relabels/creates the single
// "current parent" entry without walking the whole scene tree; full
// candidate list refreshes on the next popup open or row re-assign
void BoxSingleWidget::refreshParentLinkCombo() {
    const auto combo = mParentLinkCombo;
    const auto box = currentLinkedBox();
    QString txt = tr("No Parent");
    quintptr curRaw = 0;
    if(box && mTarget) {
        if(const auto pe = findParentEffect(box)) {
            if(const auto par = enve_cast<BoundingBox*>(
                       pe->parentTargetProperty()->getTarget())) {
                txt = par->prp_getName();
                curRaw = reinterpret_cast<quintptr>(par);
            }
        }
    }
    // find an entry carrying the current parent pointer
    int idx = -1;
    for(int i = 0; i < combo->count(); i++) {
        if(combo->itemData(i).value<quintptr>() == curRaw &&
           curRaw != 0) { idx = i; break; }
    }
    if(idx < 0 && !combo->itemText(0).isEmpty() &&
       combo->itemData(0).isNull()) {
        // reuse slot 0 when it is the placeholder
        combo->setItemText(0, txt);
        combo->setItemData(0, QVariant::fromValue(curRaw));
        idx = 0;
    } else if(idx >= 0) {
        combo->setItemText(idx, txt);
    } else {
        combo->insertItem(0, txt, QVariant::fromValue(curRaw));
        idx = 0;
    }
    const QSignalBlocker blocker(combo);
    combo->setCurrentIndex(idx);
}

void BoxSingleWidget::showParentLinkMenu() {
    if (!mTarget) { return; }
    const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
    if (!box) { return; }
    const auto scene = mParent->currentScene();
    if (!scene) { return; }
    // guarded: the nested menu loop may outlive scene/box
    const QPointer<Canvas> sceneGuard = scene;
    const QPointer<BoundingBox> boxGuard = box;
    QMenu menu(this);
    {
        const auto act = menu.addAction(
                    BoxSingleWidget::tr("No Parent")); // 无父级
        connect(act, &QAction::triggered, this,
                [sceneGuard, boxGuard]() {
            if(sceneGuard && boxGuard) {
                sceneGuard->linkParentLevel(boxGuard, nullptr);
            }
        });
        act->setDisabled(!boxHasParentLink(box));
    }
    const auto addBoxes = [&](ContainerBox* const cont, const auto& self) -> void {
        for(const auto& b : cont->getContainedBoxes()) {
            if(b == box) continue;
            if(box->isAncestor(b)) continue; // not own descendants
            if(b->hasInParentLinkChain(box)) continue; // link cycles
            const auto act = menu.addAction(b->prp_getName());
            connect(act, &QAction::triggered, this,
                    [sceneGuard, boxGuard, bQ = QPointer<BoundingBox>(b)]() {
                if(sceneGuard && boxGuard && bQ) {
                    sceneGuard->linkParentLevel(boxGuard, bQ);
                }
            });
            if(const auto g = enve_cast<ContainerBox*>(b)) {
                self(g, self);
            }
        }
    };
    addBoxes(scene, addBoxes);
    menu.exec(QCursor::pos());
}

void BoxSingleWidget::startParentLinkDrag() {
    if (!mTarget) { return; }
    const auto box = enve_cast<BoundingBox*>(mTarget->getTarget());
    if (!box) { return; }
    // anchor for the AE-style connector line drawn by BoxScroller
    const QPoint srcCenter =
            mParentLinkButton->mapToGlobal(
                mParentLinkButton->rect().center());
    BoxScroller::plDragStarted(srcCenter);
    const auto drag = new QDrag(this);
    auto mime = new QMimeData();
    QByteArray raw;
    QDataStream ds(&raw, QIODevice::WriteOnly);
    ds << quintptr(box);
    mime->setData(parentLinkMimeType(), raw);
    drag->setMimeData(mime);
    QPixmap pm(24, 24);
    pm.fill(QColor(120, 170, 255));
    drag->setPixmap(pm);
    drag->exec(Qt::CopyAction);
    BoxScroller::plDragEnded();
}

void BoxSingleWidget::switchContentVisibleAction() {
    if(!mTarget) return;
    mTarget->switchContentVisible();
    Document::sInstance->actionFinished();
    //mParent->callUpdaters();
}

void BoxSingleWidget::switchRecordingAction() {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();
    if(!target) return;
    if(const auto asAnim = enve_cast<Animator*>(target)) {
        asAnim->anim_switchRecording();
        Document::sInstance->actionFinished();
        update();
    }
}

void BoxSingleWidget::switchBoxVisibleAction() {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();
    if(!target) return;
    if(const auto ebos = enve_cast<eBoxOrSound*>(target)) {
        ebos->switchVisible();
    } else if(const auto eEff = enve_cast<eEffect*>(target)) {
        eEff->switchVisible();
    } /*else if(const auto graph = enve_cast<GraphAnimator*>(target)) {
        const auto bsvt = static_cast<BoxScroller*>(mParent);
        const auto keysView = bsvt->getKeysView();
        if(keysView) {
            if(keysView->graphIsSelected(graph)) {
                keysView->graphRemoveViewedAnimator(graph);
            } else {
                keysView->graphAddViewedAnimator(graph);
            }
            Document::sInstance->actionFinished();
        }
    }*/
    Document::sInstance->actionFinished();
    update();
}

void BoxSingleWidget::switchBoxLockedAction() {
    if(!mTarget) return;
    static_cast<BoundingBox*>(mTarget->getTarget())->switchLocked();
    Document::sInstance->actionFinished();
    update();
}

void BoxSingleWidget::resetPropertyAction() {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();
    const auto prop = enve_cast<Property*>(target);
    if(!prop) return;
    const auto trans = prop->getFirstAncestor<AdvancedTransformAnimator>();
    if(!trans) return;

    if(const auto qpf = enve_cast<QPointFAnimator*>(prop)) {
        // defaults: translation/shear/pivot (0,0), scale (1,1)
        QPointF def(0, 0);
        if(trans->getScaleAnimator() == qpf) def = QPointF(1, 1);
        qpf->prp_startTransform();
        qpf->setBaseValue(def);
        qpf->prp_finishTransform();
    } else if(const auto qra = enve_cast<QrealAnimator*>(prop)) {
        // defaults: rot/rotX/rotY/zPos/pos x,y/shear/pivot = 0,
        //           opacity = 100, perspective = 800, scale x,y = 1
        qreal def = 0;
        if(trans->getOpacityAnimator() == qra) def = 100;
        else if(trans->getPerspectiveAnimator() == qra) def = 800;
        else if(trans->getScaleAnimator()->getXAnimator() == qra ||
                trans->getScaleAnimator()->getYAnimator() == qra) def = 1;
        qra->prp_startTransform();
        qra->setCurrentBaseValue(def);
        qra->prp_finishTransform();
    }
    Document::sInstance->actionFinished();
    update();
}

void BoxSingleWidget::updateValueSlidersForQPointFAnimator() {
    if(!mTarget) return;
    const auto target = mTarget->getTarget();
    const auto asQPointFAnim = enve_cast<QPointFAnimator*>(target);
    if(!asQPointFAnim || mTarget->contentVisible()) return;
    if(width() - mFillWidget->x() > 10*eSizesUI::widget) {
        mValueSlider->setTarget(asQPointFAnim->getXAnimator());
        mValueSlider->show();
        mValueSlider->setIsLeftSlider(true);
        mSecondValueSlider->setTarget(asQPointFAnim->getYAnimator());
        mSecondValueSlider->show();
        mSecondValueSlider->setIsRightSlider(true);
    } else {
        clearAndHideValueAnimators();
    }
}

void BoxSingleWidget::updatePathCompositionBoxVisible() {
    if(!mTarget) return;
    if(mPathBlendModeVisible && width() - mFillWidget->x() > 8*eSizesUI::widget) {
        mPathBlendModeCombo->show();
    } else mPathBlendModeCombo->hide();
}

void BoxSingleWidget::updateCompositionBoxVisible() {
    if(!mTarget) return;
    if(mBlendModeVisible && width() - mFillWidget->x() > 10*eSizesUI::widget) {
        mBlendModeCombo->show();
    } else mBlendModeCombo->hide();
}

void BoxSingleWidget::updateFillTypeBoxVisible() {
    if(!mTarget) return;
    if(mFillTypeVisible && width() - mFillWidget->x() > 8*eSizesUI::widget) {
        mFillTypeCombo->show();
    } else mFillTypeCombo->hide();
}

void BoxSingleWidget::resizeEvent(QResizeEvent *) {
    updateCompositionBoxVisible();
    updatePathCompositionBoxVisible();
    updateFillTypeBoxVisible();
    updateValueSlidersForQPointFAnimator();
    if(mSelOverlay) mSelOverlay->setGeometry(rect());
}

// refresh popup candidates right before the combo opens (showPopup is
// not a signal in Qt5, so an event filter on the press is used instead)
bool BoxSingleWidget::eventFilter(QObject *obj, QEvent *event) {
    if(event->type() == QEvent::MouseButtonPress) {
        if(obj == mParentLinkCombo) rebuildParentLinkCandidates();
        else if(obj == mTrkMatLayerCombo) rebuildTrkMatLayerCandidates();
    }
    return QWidget::eventFilter(obj, event);
}

void BoxSingleWidget::selOverlayUpdate() {
    if(mSelOverlay) mSelOverlay->update();
}

#include "boxsinglewidget.moc"
