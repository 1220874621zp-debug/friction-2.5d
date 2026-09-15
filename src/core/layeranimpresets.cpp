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
*/

#include "layeranimpresets.h"

#include "Boxes/boundingbox.h"
#include "Boxes/pathbox.h"
#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Expressions/expression.h"

#include "skia/skqtconversions.h"
#include "skia/skiaincludes.h"

#include <QTransform>
#include <QRectF>

namespace {

// shared JS helpers embedded as the definitions section of every
// preset-generated expression (kept ASCII so it is stable to edit)
const char* const kDefs =
        "function pSat(t){return t<0?0:(t>1?1:t);}\n"
        "function pSmooth(t){t=pSat(t);return t*t*(3-2*t);}\n"
        "function pSeg(f,a,b){return b<=a?1:pSat((f-a)/(b-a));}\n"
        "function pPh(f,f0,p){var u=(f-f0)/p;u=u-Math.floor(u);return u;}\n"
        "function pPw(x,p){if(x<=p[0][0])return p[0][1];"
        "for(var i=1;i<p.length;i++){if(x<=p[i][0]){var a=p[i-1],b=p[i];"
        "return a[1]+(b[1]-a[1])*pSmooth((x-a[0])/(b[0]-a[0]));}}"
        "return p[p.length-1][1];}\n";

QString N(const qreal v)
{
    return QString::number(v, 'g', 12);
}

// --------------------------------------------------- script generators
// each body references `f` (the frame) and clamps before / after its
// window; loops additionally freeze on the rest state before F0

void genFadeIn(const LayerExprParams& P, LayerExprScripts& S)
{
    S.op = QStringLiteral(
                "// 淡入：从 %1 起 %2 帧内显现\n"
                "return %3 * pSmooth(pSeg(f, %1, %1 + %2));")
            .arg(N(P.winF0()), N(P.D), N(P.op));
}

void genFadeOut(const LayerExprParams& P, LayerExprScripts& S)
{
    S.op = QStringLiteral(
                "// 淡出：从 %1 起 %2 帧内消失\n"
                "return %3 * (1 - pSmooth(pSeg(f, %1, %1 + %2)));")
            .arg(N(P.winF0()), N(P.D), N(P.op));
}

void genSlideInLeft(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posX = QStringLiteral(
                "// 左侧划入：从画布左外滑回原位\n"
                "return %1 - %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));")
            .arg(N(P.px), N(P.cw/2 + P.bw), N(P.winF0()), N(P.D));
}

void genSlideInRight(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posX = QStringLiteral(
                "// 右侧划入：从画布右外滑回原位\n"
                "return %1 + %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));")
            .arg(N(P.px), N(P.cw/2 + P.bw), N(P.winF0()), N(P.D));
}

void genSlideOutLeft(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posX = QStringLiteral(
                "// 左侧划出：滑出画布左侧\n"
                "return %1 - %2 * pSmooth(pSeg(f, %3, %3 + %4));")
            .arg(N(P.px), N(P.cw/2 + P.bw), N(P.winF0()), N(P.D));
}

void genSlideOutRight(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posX = QStringLiteral(
                "// 右侧划出：滑出画布右侧\n"
                "return %1 + %2 * pSmooth(pSeg(f, %3, %3 + %4));")
            .arg(N(P.px), N(P.cw/2 + P.bw), N(P.winF0()), N(P.D));
}

void genZoomIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "// 弹性缩放进入：放大弹出回弹落定（横轴）\n"
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 0.001], [1, 1.12], [1.3, 0.94],"
                " [1.6, 1.04], [1.8, 1]]);")
            .arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral(
                "// 弹性缩放进入：放大弹出回弹落定（纵轴）\n"
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 0.001], [1, 1.12], [1.3, 0.94],"
                " [1.6, 1.04], [1.8, 1]]);")
            .arg(N(P.sy), f0, d);
    S.op = QStringLiteral(
                "return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D/3.)));
}

void genZoomOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "// 缩小退场（横轴）\n"
                "return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral(
                "// 缩小退场（纵轴）\n"
                "return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.sy), f0, d);
    S.op = QStringLiteral(
                "return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));")
            .arg(N(P.op), f0, N(0.7*P.D), d);
}

void genSpinIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rot = QStringLiteral(
                "// 旋转进入：转半圈落定\n"
                "return %1 + 180 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.rot), f0, d);
    S.op = QStringLiteral(
                "return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D/2.)));
}

void genSpinOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rot = QStringLiteral(
                "// 旋转退场\n"
                "return %1 - 180 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.rot), f0, d);
    S.op = QStringLiteral(
                "return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));")
            .arg(N(P.op), f0, N(0.6*P.D), d);
}

void genPopIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "// 弹出：快速弹出并轻微回弹（横轴）\n"
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 0.001], [1, 1.18], [1.35, 0.94], [1.6, 1]]);")
            .arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral(
                "// 弹出：快速弹出并轻微回弹（纵轴）\n"
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 0.001], [1, 1.18], [1.35, 0.94], [1.6, 1]]);")
            .arg(N(P.sy), f0, d);
}

void genDropBounce(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posY = QStringLiteral(
                "// 落下弹跳：从上方落下触地弹两下\n"
                "return %1 + pPw((f - %2) / %3,"
                " [[0, %4], [1, %5], [1.3, %6], [1.6, 0]]);")
            .arg(N(P.py), N(P.winF0()), N(P.D),
                 N(-(P.ch/2 + P.bh)), N(P.bh*0.15), N(-P.bh*0.08));
    S.op = QStringLiteral(
                "return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), N(P.winF0()), N(qMax(1., P.D/4.)));
}

void genSinkOut(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posY = QStringLiteral(
                "// 下沉：下沉并淡出\n"
                "return %1 + %2 * pSmooth(pSeg(f, %3, %3 + %4));")
            .arg(N(P.py), N(P.bh + 40.), N(P.winF0()), N(P.D));
    S.op = QStringLiteral(
                "return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));")
            .arg(N(P.op), N(P.winF0()), N(0.8*P.D), N(P.D));
}

void genShake(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto step = N(qMax(1., P.D/8.));
    S.posX = QStringLiteral(
                "// 抖动：左右小幅交替（无限循环）\n"
                "if (f < %1) { return %2; }\n"
                "var k = Math.floor((f - %1) / %3) %% 2;\n"
                "return %2 + (k === 0 ? -3 : 3);")
            .arg(f0, N(P.px), step);
    S.posY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "var k = Math.floor((f - %1) / %3) %% 2;\n"
                "return %2 + (k === 0 ? 1 : -2);")
            .arg(f0, N(P.py), step);
}

void genSway(const LayerExprParams& P, LayerExprScripts& S)
{
    S.rot = QStringLiteral(
                "// 摇摆：钟摆式正弦摆动（无限循环）\n"
                "if (f < %1) { return %2; }\n"
                "return %2 + 6 * Math.sin(2 * Math.PI * pPh(f, %1, %3));")
            .arg(N(P.winF0()), N(P.rot), N(P.D));
}

void genHop(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    const auto hop = N(qMax(P.bh*0.6, 30.));
    S.posY = QStringLiteral(
                "// 蹦跳：原地起跳两次、落地压扁（无限循环）\n"
                "if (f < %1) { return %2; }\n"
                "return %2 - %3 * Math.abs(Math.sin(2 * Math.PI * pPh(f, %1, %4)));")
            .arg(f0, N(P.py), hop, d);
    S.scaleX = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 + 0.12 * Math.cos(4 * Math.PI * pPh(f, %1, %3)));")
            .arg(f0, N(P.sx), d);
    S.scaleY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 - 0.12 * Math.cos(4 * Math.PI * pPh(f, %1, %3)));")
            .arg(f0, N(P.sy), d);
}

void genSquashStretch(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    const auto lift = N(qMax(P.bh*0.3, 14.));
    S.scaleX = QStringLiteral(
                "// 弹性拉伸蹦跶：压扁拉伸交替（无限循环）\n"
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 - 0.2 * Math.cos(2 * Math.PI * pPh(f, %1, %3)));")
            .arg(f0, N(P.sx), d);
    S.scaleY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 + 0.25 * Math.cos(2 * Math.PI * pPh(f, %1, %3)));")
            .arg(f0, N(P.sy), d);
    S.posY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 - %3 * Math.max(0, -Math.cos(2 * Math.PI * pPh(f, %1, %4)));")
            .arg(f0, N(P.py), lift, d);
}

void genBreathe(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "// 呼吸：缓慢缩放起伏（无限循环）\n"
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 + 0.05 * (0.5 - 0.5 * Math.cos(2 * Math.PI * pPh(f, %1, %3))));")
            .arg(f0, N(P.sx), d);
    S.scaleY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 + 0.05 * (0.5 - 0.5 * Math.cos(2 * Math.PI * pPh(f, %1, %3))));")
            .arg(f0, N(P.sy), d);
}

void genWobble(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posX = QStringLiteral(
                "// 晃动：平移加轻微旋转（无限循环）\n"
                "if (f < %1) { return %2; }\n"
                "return %2 + 6 * Math.sin(2 * Math.PI * pPh(f, %1, %3));")
            .arg(f0, N(P.px), d);
    S.rot = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 + 4 * Math.sin(2 * Math.PI * pPh(f, %1, %3));")
            .arg(f0, N(P.rot), d);
}

void genSlideInUp(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posY = QStringLiteral(
                "// 底部划入：从画布下方滑回原位\n"
                "return %1 + %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));")
            .arg(N(P.py), N(P.ch/2 + P.bh), N(P.winF0()), N(P.D));
}

void genSlideOutUp(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posY = QStringLiteral(
                "// 向上划出：滑出画布上方\n"
                "return %1 - %2 * pSmooth(pSeg(f, %3, %3 + %4));")
            .arg(N(P.py), N(P.ch/2 + P.bh), N(P.winF0()), N(P.D));
}

void genSlideInDown(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posY = QStringLiteral(
                "// 顶部划入：从画布上方滑回原位\n"
                "return %1 - %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));")
            .arg(N(P.py), N(P.ch/2 + P.bh), N(P.winF0()), N(P.D));
}

void genSlideOutDown(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posY = QStringLiteral(
                "// 向下划出：滑出画布下方\n"
                "return %1 + %2 * pSmooth(pSeg(f, %3, %3 + %4));")
            .arg(N(P.py), N(P.ch/2 + P.bh), N(P.winF0()), N(P.D));
}

void genZoomFarIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "return %1 * (1 + 3.0 * (1 - pSmooth(pSeg(f, %2, %2 + %3))));")
            .arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral(
                "return %1 * (1 + 3.0 * (1 - pSmooth(pSeg(f, %2, %2 + %3))));")
            .arg(N(P.sy), f0, d);
    S.op = QStringLiteral(
                "return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, d);
}

void genZoomFarOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "return %1 * (1 + 3.0 * pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral(
                "return %1 * (1 + 3.0 * pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.sy), f0, d);
    S.op = QStringLiteral(
                "return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.op), f0, d);
}

void genElasticScale(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 0.001], [0.8, 1.25], [1.1, 0.9], [1.35, 1.05], [1.6, 1]]);")
            .arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral(
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 0.001], [0.8, 1.25], [1.1, 0.9], [1.35, 1.05], [1.6, 1]]);")
            .arg(N(P.sy), f0, d);
    S.op = QStringLiteral(
                "return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D / 3.)));
}

void genSwingIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rot = QStringLiteral(
                "return %1 + pPw((f - %2) / %3,"
                " [[0, 50], [0.7, -22], [1.1, 10], [1.4, -4], [1.7, 0]]);")
            .arg(N(P.rot), f0, d);
    S.op = QStringLiteral(
                "return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D / 3.)));
}

void genSwingOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rot = QStringLiteral(
                "return %1 - 50 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.rot), f0, d);
    S.op = QStringLiteral(
                "return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));")
            .arg(N(P.op), f0, N(0.6 * P.D), d);
}

void genStampIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 2.6], [0.8, 0.95], [1.0, 1.04], [1.2, 1.0]]);")
            .arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral(
                "return %1 * pPw((f - %2) / %3,"
                " [[0, 2.6], [0.8, 0.95], [1.0, 1.04], [1.2, 1.0]]);")
            .arg(N(P.sy), f0, d);
    S.op = QStringLiteral(
                "return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D / 3.)));
}

void genExpandHIn(const LayerExprParams& P, LayerExprScripts& S)
{
    S.scaleX = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.sx), N(P.winF0()), N(P.D));
}

void genExpandHOut(const LayerExprParams& P, LayerExprScripts& S)
{
    S.scaleX = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.sx), N(P.winF0()), N(P.D));
}

void genExpandVIn(const LayerExprParams& P, LayerExprScripts& S)
{
    S.scaleY = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.sy), N(P.winF0()), N(P.D));
}

void genExpandVOut(const LayerExprParams& P, LayerExprScripts& S)
{
    S.scaleY = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.sy), N(P.winF0()), N(P.D));
}

void genSkewSlideInLeft(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posX = QStringLiteral("return %1 - %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));")
            .arg(N(P.px), N(P.cw/2 + P.bw), f0, d);
    S.shearX = QStringLiteral("return %1 + 0.7 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.shx), f0, d);
}

void genSkewSlideOutLeft(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posX = QStringLiteral("return %1 - %2 * pSmooth(pSeg(f, %3, %3 + %4));")
            .arg(N(P.px), N(P.cw/2 + P.bw), f0, d);
    S.shearX = QStringLiteral("return %1 - 0.7 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.shx), f0, d);
}

void genSkewSlideInRight(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posX = QStringLiteral("return %1 + %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));")
            .arg(N(P.px), N(P.cw/2 + P.bw), f0, d);
    S.shearX = QStringLiteral("return %1 - 0.7 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.shx), f0, d);
}

void genSkewSlideOutRight(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posX = QStringLiteral("return %1 + %2 * pSmooth(pSeg(f, %3, %3 + %4));")
            .arg(N(P.px), N(P.cw/2 + P.bw), f0, d);
    S.shearX = QStringLiteral("return %1 + 0.7 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.shx), f0, d);
}

void genFlipXIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rotX = QStringLiteral("return %1 + 90 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.rx), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D / 2.)));
}

void genFlipXOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rotX = QStringLiteral("return %1 - 90 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.rx), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));")
            .arg(N(P.op), f0, N(0.5 * P.D), d);
}

void genFlipYIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rotY = QStringLiteral("return %1 - 90 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D / 2.)));
}

void genFlipYOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rotY = QStringLiteral("return %1 + 90 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));")
            .arg(N(P.op), f0, N(0.5 * P.D), d);
}

void genFlyIn3D(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posZ = QStringLiteral("return %1 - 900 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));")
            .arg(N(P.pz), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, N(qMax(1., P.D / 3.)));
}

void genFlyOut3D(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posZ = QStringLiteral("return %1 + 900 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.pz), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));")
            .arg(N(P.op), f0, N(0.6 * P.D), d);
}

void genSinkIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.posY = QStringLiteral("return %1 + %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));")
            .arg(N(P.py), N(P.bh + 40.), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));")
            .arg(N(P.op), f0, d);
}

void genFloat(const LayerExprParams& P, LayerExprScripts& S)
{
    S.posY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 + 10 * Math.sin(2 * Math.PI * pPh(f, %1, %3));")
            .arg(N(P.winF0()), N(P.py), N(P.D));
}

void genSpinLoop(const LayerExprParams& P, LayerExprScripts& S)
{
    S.rot = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 + 360 * pPh(f, %1, %3);")
            .arg(N(P.winF0()), N(P.rot), N(P.D));
}

void genPulseScale(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 + 0.16 * (0.5 - 0.5 * Math.cos(2 * Math.PI * pPh(f, %1, %3))));")
            .arg(f0, N(P.sx), d);
    S.scaleY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 + 0.16 * (0.5 - 0.5 * Math.cos(2 * Math.PI * pPh(f, %1, %3))));")
            .arg(f0, N(P.sy), d);
}

void genPendulum(const LayerExprParams& P, LayerExprScripts& S)
{
    S.rot = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 + 15 * Math.sin(2 * Math.PI * pPh(f, %1, %3));")
            .arg(N(P.winF0()), N(P.rot), N(P.D));
}

void genJiggle(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.scaleX = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 + 0.08 * Math.sin(4 * Math.PI * pPh(f, %1, %3)));")
            .arg(f0, N(P.sx), d);
    S.scaleY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 * (1 - 0.08 * Math.sin(4 * Math.PI * pPh(f, %1, %3)));")
            .arg(f0, N(P.sy), d);
}

void genOrbit3D(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0());
    const auto d = N(P.D);
    S.rotX = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 + 10 * Math.sin(2 * Math.PI * pPh(f, %1, %3));")
            .arg(f0, N(P.rx), d);
    S.rotY = QStringLiteral(
                "if (f < %1) { return %2; }\n"
                "return %2 + 14 * Math.cos(2 * Math.PI * pPh(f, %1, %3));")
            .arg(f0, N(P.ry), d);
}

// ---------------- 3D Space Extensions
void genDoorOpenLIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotY = QStringLiteral("return %1 - 90 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 2.)));
}
void genDoorOpenLOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotY = QStringLiteral("return %1 - 90 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));").arg(N(P.op), f0, N(0.5 * P.D), d);
}
void genDoorOpenRIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotY = QStringLiteral("return %1 + 90 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 2.)));
}
void genDoorOpenROut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotY = QStringLiteral("return %1 + 90 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2 + %3, %2 + %4)));").arg(N(P.op), f0, N(0.5 * P.D), d);
}
void genDive3DIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 + 60 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.rx), f0, d);
    S.posZ = QStringLiteral("return %1 - 600 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.pz), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genDive3DOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 + 60 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.rx), f0, d);
    S.posZ = QStringLiteral("return %1 - 600 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.pz), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genClimb3DIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 - 60 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.rx), f0, d);
    S.posZ = QStringLiteral("return %1 + 600 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.pz), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genClimb3DOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 - 60 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.rx), f0, d);
    S.posZ = QStringLiteral("return %1 + 600 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.pz), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genAxialRollIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 + 180 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.rx), f0, d);
    S.rotY = QStringLiteral("return %1 + 180 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genAxialRollOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 + 180 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.rx), f0, d);
    S.rotY = QStringLiteral("return %1 + 180 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genTiltSwayIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 + 25 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.rx), f0, d);
    S.rotY = QStringLiteral("return %1 - 25 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genTiltSwayOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 - 25 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.rx), f0, d);
    S.rotY = QStringLiteral("return %1 + 25 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.ry), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genIsometricDrop(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("return %1 + 30 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.rx), f0, d);
    S.rotY = QStringLiteral("return %1 + 45 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.ry), f0, d);
    S.posY = QStringLiteral("return %1 - 220 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.py), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genDepthZoomBurstIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posZ = QStringLiteral("return %1 - 1500 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.pz), f0, d);
    S.scaleX = QStringLiteral("return %1 * (0.1 + 0.9 * pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral("return %1 * (0.1 + 0.9 * pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.sy), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genDepthZoomBurstOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posZ = QStringLiteral("return %1 + 1500 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.pz), f0, d);
    S.scaleX = QStringLiteral("return %1 * (1 + 2.0 * pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral("return %1 * (1 + 2.0 * pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.sy), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genCardUnfold3DIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotY = QStringLiteral("return %1 - 90 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.ry), f0, d);
    S.rotX = QStringLiteral("return %1 - 45 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.rx), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genCardUnfold3DOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotY = QStringLiteral("return %1 + 90 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.ry), f0, d);
    S.rotX = QStringLiteral("return %1 + 45 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.rx), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genOrbitHelix(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rotX = QStringLiteral("if (f < %1) return %2; return %2 + 18 * Math.sin(2 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.rx), d);
    S.rotY = QStringLiteral("if (f < %1) return %2; return %2 + 22 * Math.cos(2 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.ry), d);
    S.posZ = QStringLiteral("if (f < %1) return %2; return %2 + 120 * Math.sin(4 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.pz), d);
}

// ---------------- Physical Dynamics Extensions
void genJellyWobble(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.scaleX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 + 0.45 * Math.sin(t * 12) * Math.exp(-4 * t));").arg(f0, d, N(P.sx));
    S.scaleY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 - 0.45 * Math.sin(t * 12) * Math.exp(-4 * t));").arg(f0, d, N(P.sy));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 3.)));
}
void genRubberBounce(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 - 200 * Math.abs(Math.cos(t * Math.PI * 2.5)) * Math.exp(-3 * t);").arg(f0, d, N(P.py));
    S.scaleY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 + 0.3 * Math.sin(t * 10) * Math.exp(-3.5 * t));").arg(f0, d, N(P.sy));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 4.)));
}
void genHeavyStampJitter(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 - 250 * Math.pow(1 - t, 3);").arg(f0, d, N(P.py));
    S.scaleX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 + 0.3 * Math.sin(t * 20) * Math.exp(-5 * t));").arg(f0, d, N(P.sx));
    S.scaleY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 - 0.25 * Math.sin(t * 20) * Math.exp(-5 * t));").arg(f0, d, N(P.sy));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 4.)));
}
void genMagneticSnap(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 - 180 * (1 - (1 - Math.pow(2, -10 * t)));").arg(f0, d, N(P.px));
    S.shearX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 + 0.35 * Math.sin(t * 14) * Math.exp(-4 * t);").arg(f0, d, N(P.shx));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 4.)));
}
void genPendulumSettle(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rot = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 + 45 * Math.cos(t * 10) * Math.exp(-3 * t);").arg(f0, d, N(P.rot));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 3.)));
}
void genBalloonFloat(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("return %1 + 180 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.py), f0, d);
    S.rot = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 + 8 * Math.sin(t * 8) * (1 - t);").arg(f0, d, N(P.rot));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genShockwavePulse(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.scaleX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (0.2 + 0.8 * (1 + 0.4 * Math.sin(t * Math.PI) * Math.exp(-2 * t)));").arg(f0, d, N(P.sx));
    S.scaleY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (0.2 + 0.8 * (1 + 0.4 * Math.sin(t * Math.PI) * Math.exp(-2 * t)));").arg(f0, d, N(P.sy));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 3.)));
}
void genSpringPop(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.scaleX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 - Math.pow(2, -10 * t) * Math.sin((10 * t - 0.75) * 2 * Math.PI / 3));").arg(f0, d, N(P.sx));
    S.scaleY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 - Math.pow(2, -10 * t) * Math.sin((10 * t - 0.75) * 2 * Math.PI / 3));").arg(f0, d, N(P.sy));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 4.)));
}
void genWhipSnap(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.shearX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 + 0.6 * (1 - t) * Math.cos(t * 12) * Math.exp(-3 * t);").arg(f0, d, N(P.shx));
    S.posX = QStringLiteral("return %1 - 120 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.px), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genAnvilCrash(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 - 300 * Math.pow(1 - t, 4);").arg(f0, d, N(P.py));
    S.scaleY = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 - 0.35 * Math.sin(t * 16) * Math.exp(-4 * t));").arg(f0, d, N(P.sy));
    S.scaleX = QStringLiteral("var t = pSeg(f, %1, %1 + %2); return %3 * (1 + 0.35 * Math.sin(t * 16) * Math.exp(-4 * t));").arg(f0, d, N(P.sx));
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, N(qMax(1., P.D / 4.)));
}

// ---------------- UI Transitions & Loops Extensions
void genSheetSlideUpIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("return %1 + %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));").arg(N(P.py), N(P.ch / 2 + P.bh), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genSheetSlideUpOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("return %1 + %2 * pSmooth(pSeg(f, %3, %3 + %4));").arg(N(P.py), N(P.ch / 2 + P.bh), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genSheetSlideDownIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("return %1 - %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));").arg(N(P.py), N(P.ch / 2 + P.bh), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genSheetSlideDownOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posY = QStringLiteral("return %1 - %2 * pSmooth(pSeg(f, %3, %3 + %4));").arg(N(P.py), N(P.ch / 2 + P.bh), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genSheetSlideLeftIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("return %1 - %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));").arg(N(P.px), N(P.cw / 2 + P.bw), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genSheetSlideLeftOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("return %1 - %2 * pSmooth(pSeg(f, %3, %3 + %4));").arg(N(P.px), N(P.cw / 2 + P.bw), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genSheetSlideRightIn(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("return %1 + %2 * (1 - pSmooth(pSeg(f, %3, %3 + %4)));").arg(N(P.px), N(P.cw / 2 + P.bw), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genSheetSlideRightOut(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("return %1 + %2 * pSmooth(pSeg(f, %3, %3 + %4));").arg(N(P.px), N(P.cw / 2 + P.bw), f0, d);
    S.op = QStringLiteral("return %1 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.op), f0, d);
}
void genIrisZoomPop(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.scaleX = QStringLiteral("return %1 * (0.05 + 0.95 * pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.sx), f0, d);
    S.scaleY = QStringLiteral("return %1 * (0.05 + 0.95 * pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.sy), f0, d);
    S.rot = QStringLiteral("return %1 + 45 * (1 - pSmooth(pSeg(f, %2, %2 + %3)));").arg(N(P.rot), f0, d);
    S.op = QStringLiteral("return %1 * pSmooth(pSeg(f, %2, %2 + %3));").arg(N(P.op), f0, d);
}
void genGlitchShake(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("if (f < %1) return %2; var u = pPh(f, %1, %3); return %2 + (Math.sin(u * 50) > 0.3 ? 6 * Math.sin(u * 80) : 0);").arg(f0, N(P.px), d);
    S.posY = QStringLiteral("if (f < %1) return %2; var u = pPh(f, %1, %3); return %2 + (Math.cos(u * 40) > 0.4 ? 4 * Math.cos(u * 60) : 0);").arg(f0, N(P.py), d);
}
void genEndlessFloat(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("if (f < %1) return %2; return %2 + 8 * Math.sin(2 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.px), d);
    S.posY = QStringLiteral("if (f < %1) return %2; return %2 + 12 * Math.sin(4 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.py), d);
}
void genCompassNeedle(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.rot = QStringLiteral("if (f < %1) return %2; return %2 + 8 * Math.sin(2 * Math.PI * pPh(f, %1, %3)) * Math.cos(4 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.rot), d);
}
void genTensionVibrate(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.posX = QStringLiteral("if (f < %1) return %2; return %2 + 2.5 * Math.sin(20 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.px), d);
    S.posY = QStringLiteral("if (f < %1) return %2; return %2 + 2.5 * Math.cos(20 * Math.PI * pPh(f, %1, %3));").arg(f0, N(P.py), d);
}
void genHeartbeatLayer(const LayerExprParams& P, LayerExprScripts& S)
{
    const auto f0 = N(P.winF0()), d = N(P.D);
    S.scaleX = QStringLiteral("if (f < %1) return %2; var u = pPh(f, %1, %3); var p = (u < 0.2 ? Math.sin(u * 5 * Math.PI) : (u < 0.4 ? 0.6 * Math.sin((u - 0.2) * 5 * Math.PI) : 0)); return %2 * (1 + 0.15 * p);").arg(f0, N(P.sx), d);
    S.scaleY = QStringLiteral("if (f < %1) return %2; var u = pPh(f, %1, %3); var p = (u < 0.2 ? Math.sin(u * 5 * Math.PI) : (u < 0.4 ? 0.6 * Math.sin((u - 0.2) * 5 * Math.PI) : 0)); return %2 * (1 + 0.15 * p);").arg(f0, N(P.sy), d);
}

struct LayerPresetBuilder {
    LayerAnimPreset p;
    LayerPresetBuilder& in() { p.category = 0; return *this; }
    LayerPresetBuilder& out() { p.category = 1; return *this; }
    LayerPresetBuilder& loop() { p.category = 2; return *this; }
    LayerPresetBuilder& tag(const QString& t) { p.tag = t; return *this; }
    LayerPresetBuilder& dur(const qreal d) { p.duration = d; return *this; }
    LayerPresetBuilder& outGen(
            void (*og)(const LayerExprParams&, LayerExprScripts&))
    { p.outGen = og; return *this; }
    LayerPresetBuilder& genExpr(
            void (*g)(const LayerExprParams&, LayerExprScripts&))
    { p.gen = g; return *this; }
};

void addPreset(QList<LayerAnimPreset>& list, const LayerAnimPreset& p)
{
    list << p;
}

QList<LayerAnimPreset> gPresets;

void ensurePresets()
{
    if (!gPresets.isEmpty()) { return; }
    const auto B = [](const char* const id, const QString& name,
                      const QString& desc) {
        LayerPresetBuilder b;
        b.p.id = id;
        b.p.name = name;
        b.p.desc = desc;
        return b;
    };

    // =========================================================================
    // 1. 图层入出场动画（Layer In / Out）
    // =========================================================================
    addPreset(gPresets, B("l-fade", QString::fromUtf8("淡入淡出"),
                          QString::fromUtf8("入：渐渐显现；出：渐渐消失"))
              .in().tag("inout").dur(0.8).genExpr(&genFadeIn)
              .outGen(&genFadeOut).p);
    addPreset(gPresets, B("l-slide-l", QString::fromUtf8("左侧划动"),
                          QString::fromUtf8("入：从画布左外滑入；出：滑出画布左侧"))
              .in().tag("inout").dur(0.8).genExpr(&genSlideInLeft)
              .outGen(&genSlideOutLeft).p);
    addPreset(gPresets, B("l-slide-r", QString::fromUtf8("右侧划动"),
                          QString::fromUtf8("入：从画布右外滑入；出：滑出画布右侧"))
              .in().tag("inout").dur(0.8).genExpr(&genSlideInRight)
              .outGen(&genSlideOutRight).p);
    addPreset(gPresets, B("l-slide-up", QString::fromUtf8("底部滑入"),
                          QString::fromUtf8("入：从下方滑入；出：向上方滑出"))
              .in().tag("inout").dur(0.8).genExpr(&genSlideInUp)
              .outGen(&genSlideOutUp).p);
    addPreset(gPresets, B("l-slide-down", QString::fromUtf8("顶部滑入"),
                          QString::fromUtf8("入：从上方滑入；出：向下方滑出"))
              .in().tag("inout").dur(0.8).genExpr(&genSlideInDown)
              .outGen(&genSlideOutDown).p);
    addPreset(gPresets, B("l-zoom", QString::fromUtf8("弹性缩放"),
                          QString::fromUtf8("入：放大弹出回弹稳定；出：缩小淡出"))
              .in().tag("inout").dur(0.7).genExpr(&genZoomIn)
              .outGen(&genZoomOut).p);
    addPreset(gPresets, B("l-zoom-far", QString::fromUtf8("远景冲入"),
                          QString::fromUtf8("入：远景高倍俯冲冲入；出：冲向镜头前淡出"))
              .in().tag("inout").dur(0.9).genExpr(&genZoomFarIn)
              .outGen(&genZoomFarOut).p);
    addPreset(gPresets, B("l-elastic-scale", QString::fromUtf8("果冻膨胀"),
                          QString::fromUtf8("入：多段果冻弹性膨胀回弹落定"))
              .in().tag("inout").dur(0.8).genExpr(&genElasticScale)
              .outGen(&genZoomOut).p);
    addPreset(gPresets, B("l-spin", QString::fromUtf8("旋转进入"),
                          QString::fromUtf8("入：旋转半圈淡入落定；出：旋转缩小消失"))
              .in().tag("inout").dur(0.9).genExpr(&genSpinIn)
              .outGen(&genSpinOut).p);
    addPreset(gPresets, B("l-swing", QString::fromUtf8("秋千摆动"),
                          QString::fromUtf8("入：单侧悬挂秋千阻尼摇荡落定；出：大角度荡出"))
              .in().tag("inout").dur(1.1).genExpr(&genSwingIn)
              .outGen(&genSwingOut).p);
    addPreset(gPresets, B("l-stamp", QString::fromUtf8("印章重击"),
                          QString::fromUtf8("入：自高处印章重击砸落带有触地微震"))
              .in().tag("inout").dur(0.6).genExpr(&genStampIn).p);
    addPreset(gPresets, B("l-pop", QString::fromUtf8("快速弹出"),
                          QString::fromUtf8("快速弹出并轻微回弹（仅入点）"))
              .in().tag("inout").dur(0.5).genExpr(&genPopIn).p);
    addPreset(gPresets, B("l-drop", QString::fromUtf8("落下弹跳"),
                          QString::fromUtf8("从上方落下触地弹两下（仅入点）"))
              .in().tag("inout").dur(1.0).genExpr(&genDropBounce).p);
    addPreset(gPresets, B("l-sink", QString::fromUtf8("下沉隐退"),
                          QString::fromUtf8("入：从底部浮上站定；出：下沉并淡出"))
              .in().tag("inout").dur(0.9).genExpr(&genSinkIn)
              .outGen(&genSinkOut).p);
    addPreset(gPresets, B("l-expand-h", QString::fromUtf8("水平百叶"),
                          QString::fromUtf8("入：横向百叶拉开展开；出：横向收拢"))
              .in().tag("inout").dur(0.8).genExpr(&genExpandHIn)
              .outGen(&genExpandHOut).p);
    addPreset(gPresets, B("l-expand-v", QString::fromUtf8("垂直拉帘"),
                          QString::fromUtf8("入：垂直拉帘纵向撑开；出：纵向折叠收起"))
              .in().tag("inout").dur(0.8).genExpr(&genExpandVIn)
              .outGen(&genExpandVOut).p);
    addPreset(gPresets, B("l-skew-slide", QString::fromUtf8("左斜切入"),
                          QString::fromUtf8("入：动感斜切姿态从左滑入；出：向左滑离"))
              .in().tag("inout").dur(0.8).genExpr(&genSkewSlideInLeft)
              .outGen(&genSkewSlideOutLeft).p);
    addPreset(gPresets, B("l-skew-slide-r", QString::fromUtf8("右斜切入"),
                          QString::fromUtf8("入：动感斜切姿态从右滑入；出：向右滑离"))
              .in().tag("inout").dur(0.8).genExpr(&genSkewSlideInRight)
              .outGen(&genSkewSlideOutRight).p);

    // =========================================================================
    // 2. 2.5D 空间与翻折（3D Space Transitions）
    // =========================================================================
    addPreset(gPresets, B("l-flip-x", QString::fromUtf8("3D水平翻转"),
                QString::fromUtf8("入：围绕水平轴翻折 90 度入场；出：翻折退场"))
              .in().tag("3d").dur(0.85).genExpr(&genFlipXIn)
              .outGen(&genFlipXOut).p);
    addPreset(gPresets, B("l-flip-y", QString::fromUtf8("3D垂直翻折"),
                QString::fromUtf8("入：围绕垂直轴如翻书般 90 度展开；出：翻折收起"))
              .in().tag("3d").dur(0.85).genExpr(&genFlipYIn)
              .outGen(&genFlipYOut).p);
    addPreset(gPresets, B("l-fly-in-3d", QString::fromUtf8("3D穿梭飞入"),
                QString::fromUtf8("入：从 2.5D 深度空间高速冲向镜头；出：飞向景深处"))
              .in().tag("3d").dur(1.0).genExpr(&genFlyIn3D)
              .outGen(&genFlyOut3D).p);

    // =========================================================================
    // 3. 图层循环动效（Layer Loops）
    // =========================================================================
    addPreset(gPresets, B("l-shake", QString::fromUtf8("剧烈抖动"),
                          QString::fromUtf8("高频左右小幅抖动"))
              .loop().tag("loop").dur(0.5).genExpr(&genShake).p);
    addPreset(gPresets, B("l-sway", QString::fromUtf8("左右摇摆"),
                          QString::fromUtf8("像钟摆一样左右摇摆"))
              .loop().tag("loop").dur(1.2).genExpr(&genSway).p);
    addPreset(gPresets, B("l-hop", QString::fromUtf8("原地蹦跳"),
                          QString::fromUtf8("原地起跳两次，落地压扁"))
              .loop().tag("loop").dur(1.2).genExpr(&genHop).p);
    addPreset(gPresets, B("l-squash", QString::fromUtf8("弹性拉伸蹦跶"),
                          QString::fromUtf8("原地压扁拉伸交替小幅蹦跶"))
              .loop().tag("loop").dur(0.9).genExpr(&genSquashStretch).p);
    addPreset(gPresets, B("l-breathe", QString::fromUtf8("平稳呼吸"),
                          QString::fromUtf8("缓慢缩放呼吸起伏"))
              .loop().tag("loop").dur(1.6).genExpr(&genBreathe).p);
    addPreset(gPresets, B("l-wobble", QString::fromUtf8("综合晃动"),
                          QString::fromUtf8("左右平移加轻微旋转晃动"))
              .loop().tag("loop").dur(1.0).genExpr(&genWobble).p);
    addPreset(gPresets, B("l-float", QString::fromUtf8("悠闲漂浮"),
                          QString::fromUtf8("如水中气泡或空间失重般上下轻浮"))
              .loop().tag("loop").dur(2.2).genExpr(&genFloat).p);
    addPreset(gPresets, B("l-spin-loop", QString::fromUtf8("匀速自转"),
                          QString::fromUtf8("围绕中心点持续 360 度匀速旋转"))
              .loop().tag("loop").dur(2.5).genExpr(&genSpinLoop).p);
    addPreset(gPresets, B("l-pulse-scale", QString::fromUtf8("节拍脉冲"),
                          QString::fromUtf8("跟随音乐节拍般周期性重音缩放"))
              .loop().tag("loop").dur(0.8).genExpr(&genPulseScale).p);
    addPreset(gPresets, B("l-pendulum", QString::fromUtf8("物理钟摆"),
                          QString::fromUtf8("大振幅自然正弦钟摆摇荡"))
              .loop().tag("loop").dur(1.6).genExpr(&genPendulum).p);
    addPreset(gPresets, B("l-jiggle", QString::fromUtf8("果冻微颤"),
                          QString::fromUtf8("X 与 Y 轴反相微震果冻颤动"))
              .loop().tag("loop").dur(0.7).genExpr(&genJiggle).p);
    addPreset(gPresets, B("l-orbit-3d", QString::fromUtf8("3D空间视差"),
                          QString::fromUtf8("2.5D 透视水平与垂直微倾斜悬浮"))
              .loop().tag("3d").dur(2.6).genExpr(&genOrbit3D).p);

    // =========================================================================
    // 4. 新增 2.5D 空间与透视（3D Space Transitions Expanded）
    // =========================================================================
    addPreset(gPresets, B("l-door-open-l", QString::fromUtf8("3D门页左开"),
                          QString::fromUtf8("入：以左侧边框为轴如开门般 90 度展开；出：向内关门退场"))
              .in().tag("3d").dur(0.85).genExpr(&genDoorOpenLIn).outGen(&genDoorOpenLOut).p);
    addPreset(gPresets, B("l-door-open-r", QString::fromUtf8("3D门页右开"),
                          QString::fromUtf8("入：以右侧边框为轴如开门般 90 度展开；出：向内关门退场"))
              .in().tag("3d").dur(0.85).genExpr(&genDoorOpenRIn).outGen(&genDoorOpenROut).p);
    addPreset(gPresets, B("l-dive-3d", QString::fromUtf8("3D俯冲深潜"),
                          QString::fromUtf8("入：俯角 60 度伴随 Z 轴自深空俯冲拉平；出：俯冲冲向深景"))
              .in().tag("3d").dur(0.95).genExpr(&genDive3DIn).outGen(&genDive3DOut).p);
    addPreset(gPresets, B("l-climb-3d", QString::fromUtf8("3D昂首仰冲"),
                          QString::fromUtf8("入：仰角 60 度伴随 Z 轴仰冲归位；出：仰角飞离镜头"))
              .in().tag("3d").dur(0.95).genExpr(&genClimb3DIn).outGen(&genClimb3DOut).p);
    addPreset(gPresets, B("l-axial-roll", QString::fromUtf8("3D轴向翻转"),
                          QString::fromUtf8("入：双轴 180 度立体翻折展开；出：双轴翻折淡出"))
              .in().tag("3d").dur(0.9).genExpr(&genAxialRollIn).outGen(&genAxialRollOut).p);
    addPreset(gPresets, B("l-tilt-sway", QString::fromUtf8("3D视差微倾"),
                          QString::fromUtf8("入：对角空间轻微倾斜摆正；出：向对角空间倾斜淡出"))
              .in().tag("3d").dur(0.8).genExpr(&genTiltSwayIn).outGen(&genTiltSwayOut).p);
    addPreset(gPresets, B("l-isometric-drop", QString::fromUtf8("等轴测降落"),
                          QString::fromUtf8("保持等轴测 3D 俯视视角从高空砸落定格"))
              .in().tag("3d").dur(0.85).genExpr(&genIsometricDrop).p);
    addPreset(gPresets, B("l-depth-zoom-burst", QString::fromUtf8("景深爆发突进"),
                          QString::fromUtf8("入：自极远 1500px 深度爆冲入画；出：冲破镜头飞散"))
              .in().tag("3d").dur(0.8).genExpr(&genDepthZoomBurstIn).outGen(&genDepthZoomBurstOut).p);
    addPreset(gPresets, B("l-card-unfold-3d", QString::fromUtf8("3D折纸展开"),
                          QString::fromUtf8("入：折叠卡片多轴连续舒展铺平；出：翻折闭合"))
              .in().tag("3d").dur(0.9).genExpr(&genCardUnfold3DIn).outGen(&genCardUnfold3DOut).p);
    addPreset(gPresets, B("l-orbit-helix", QString::fromUtf8("3D双轴螺旋"),
                          QString::fromUtf8("在 2.5D 深度空间沿立体双螺旋轨道巡游"))
              .loop().tag("3d").dur(2.8).genExpr(&genOrbitHelix).p);

    // =========================================================================
    // 5. 新增物理动力学与重击模拟（Physical Dynamics Expanded）
    // =========================================================================
    addPreset(gPresets, B("l-jelly-wobble", QString::fromUtf8("多段果冻回弹"),
                QString::fromUtf8("入场后多段果冻流体弹性震颤平复"))
              .in().tag("inout").dur(0.9).genExpr(&genJellyWobble).p);
    addPreset(gPresets, B("l-rubber-bounce", QString::fromUtf8("高弹胶皮弹跳"),
                QString::fromUtf8("重力砸落并带有胶皮高弹形变跳跃"))
              .in().tag("inout").dur(1.0).genExpr(&genRubberBounce).p);
    addPreset(gPresets, B("l-heavy-stamp-jitter", QString::fromUtf8("重锤砸地微震"),
                QString::fromUtf8("超重物坠地砸出地面高频震颤反馈"))
              .in().tag("inout").dur(0.7).genExpr(&genHeavyStampJitter).p);
    addPreset(gPresets, B("l-magnetic-snap", QString::fromUtf8("磁吸瞬切归位"),
                QString::fromUtf8("强磁力瞬时抽吸归位并伴随阻尼微颤"))
              .in().tag("inout").dur(0.6).genExpr(&genMagneticSnap).p);
    addPreset(gPresets, B("l-pendulum-settle", QString::fromUtf8("阻尼钟摆停定"),
                QString::fromUtf8("大角度正弦自由落体摇荡衰减直至停正"))
              .in().tag("inout").dur(1.2).genExpr(&genPendulumSettle).p);
    addPreset(gPresets, B("l-balloon-float", QString::fromUtf8("气球失重升腾"),
                QString::fromUtf8("轻气球般自下方轻柔升起并伴随左右游荡"))
              .in().tag("inout").dur(1.1).genExpr(&genBalloonFloat).p);
    addPreset(gPresets, B("l-shockwave-pulse", QString::fromUtf8("冲击波爆发扩散"),
                QString::fromUtf8("极小内核瞬间爆裂为超大波纹回弹定格"))
              .in().tag("inout").dur(0.75).genExpr(&genShockwavePulse).p);
    addPreset(gPresets, B("l-spring-pop", QString::fromUtf8("弹簧高弹弹出"),
                QString::fromUtf8("弹簧装置高爆发弹出谐振"))
              .in().tag("inout").dur(0.85).genExpr(&genSpringPop).p);
    addPreset(gPresets, B("l-whip-snap", QString::fromUtf8("长鞭疾抽归位"),
                QString::fromUtf8("长鞭高速抽打带有剪切角瞬时甩正"))
              .in().tag("inout").dur(0.65).genExpr(&genWhipSnap).p);
    addPreset(gPresets, B("l-anvil-crash", QString::fromUtf8("铁砧重扣大地"),
                QString::fromUtf8("极速坠地并发生剧烈横向挤压与纵向回弹"))
              .in().tag("inout").dur(0.75).genExpr(&genAnvilCrash).p);

    // =========================================================================
    // 6. 新增 UI 界面转场与持续循环（UI Transitions & Loops Expanded）
    // =========================================================================
    addPreset(gPresets, B("l-sheet-slide-up", QString::fromUtf8("底栏抽屉升起"),
                QString::fromUtf8("入：自底部抽屉轻柔升起；出：滑向底部收起"))
              .in().tag("inout").dur(0.75).genExpr(&genSheetSlideUpIn).outGen(&genSheetSlideUpOut).p);
    addPreset(gPresets, B("l-sheet-slide-down", QString::fromUtf8("顶栏抽屉垂落"),
                QString::fromUtf8("入：自顶部抽屉轻柔垂落；出：滑向顶部收起"))
              .in().tag("inout").dur(0.75).genExpr(&genSheetSlideDownIn).outGen(&genSheetSlideDownOut).p);
    addPreset(gPresets, B("l-sheet-slide-left", QString::fromUtf8("侧栏抽屉左入"),
                QString::fromUtf8("入：自左边缘侧滑展开；出：滑回左边缘"))
              .in().tag("inout").dur(0.75).genExpr(&genSheetSlideLeftIn).outGen(&genSheetSlideLeftOut).p);
    addPreset(gPresets, B("l-sheet-slide-right", QString::fromUtf8("侧栏抽屉右入"),
                QString::fromUtf8("入：自右边缘侧滑展开；出：滑回右边缘"))
              .in().tag("inout").dur(0.75).genExpr(&genSheetSlideRightIn).outGen(&genSheetSlideRightOut).p);
    addPreset(gPresets, B("l-iris-zoom-pop", QString::fromUtf8("快门光圈放大"),
                QString::fromUtf8("伴随轻微旋转如同快门光圈瞬时开合就位"))
              .in().tag("inout").dur(0.7).genExpr(&genIrisZoomPop).p);
    addPreset(gPresets, B("l-glitch-shake", QString::fromUtf8("故障断电震颤"),
                QString::fromUtf8("高频离散跳变的故障电流抖动循环"))
              .loop().tag("loop").dur(0.8).genExpr(&genGlitchShake).p);
    addPreset(gPresets, B("l-endless-float", QString::fromUtf8("深空漫游浮沉"),
                QString::fromUtf8("利萨如图形双轴无重力平滑沉浮漂移"))
              .loop().tag("loop").dur(3.0).genExpr(&genEndlessFloat).p);
    addPreset(gPresets, B("l-compass-needle", QString::fromUtf8("罗盘指针微摇"),
                QString::fromUtf8("航海罗盘磁针般自然左右轻晃"))
              .loop().tag("loop").dur(2.0).genExpr(&genCompassNeedle).p);
    addPreset(gPresets, B("l-tension-vibrate", QString::fromUtf8("高张力微震"),
                QString::fromUtf8("紧绷琴弦般的超高频极微幅颤动"))
              .loop().tag("loop").dur(0.5).genExpr(&genTensionVibrate).p);
    addPreset(gPresets, B("l-heartbeat-layer", QString::fromUtf8("图层心跳重音"),
                QString::fromUtf8("模拟心脏舒张收缩的双重节拍缩放脉动"))
              .loop().tag("loop").dur(1.0).genExpr(&genHeartbeatLayer).p);
}
}

namespace LayerAnimPresets {
int count()
{
    ensurePresets();
    return gPresets.count();
}

const QList<LayerAnimPreset>& all()
{
    ensurePresets();
    return gPresets;
}

const LayerAnimPreset* byId(const QString& id)
{
    ensurePresets();
    for (const auto& p : gPresets) {
        if (p.id == id) { return &p; }
    }
    return nullptr;
}

void apply(BoundingBox* const box,
           const LayerAnimPreset& preset,
           const int inStartFrame,
           const int outStartFrame,
           const qreal fps,
           const qreal durationScale,
           const qreal canvasW,
           const qreal canvasH,
           const bool action)
{
    if (!box || (!preset.gen && !preset.outGen)) { return; }
    // box transform animators are AdvancedTransformAnimator-based
    // (BoxTransformAnimator); expressions go on the qreal
    // sub-animators (pos/scale x/y, rotation, opacity, shear, 3D)
    const auto t = dynamic_cast<AdvancedTransformAnimator*>(
                box->getTransformAnimator());
    if (!t) { return; }
    const int durF = qMax(2, qRound(preset.duration*durationScale*fps));
    const QRectF rel = box->getRelBoundingRect();

    LayerExprParams P;
    P.D = durF;
    P.cw = qMax(canvasW, 2.);
    P.ch = qMax(canvasH, 2.);
    P.bw = qMax(rel.width(), 2.);
    P.bh = qMax(rel.height(), 2.);
    const auto pos = t->getPosAnimator();
    const auto scale = t->getScaleAnimator();
    const auto rotA = t->getRotAnimator();
    const auto opA = t->getOpacityAnimator();
    const auto shear = t->getShearAnimator();
    const auto rotX = t->getRotXAnimator();
    const auto rotY = t->getRotYAnimator();
    const auto zPos = t->getZPosAnimator();

    const QPointF p0 = pos ? pos->getBaseValue() : QPointF();
    const QPointF s0 = scale ? scale->getBaseValue() : QPointF(1, 1);
    const QPointF sh0 = shear ? shear->getBaseValue() : QPointF();
    P.px = p0.x();
    P.py = p0.y();
    P.sx = s0.x();
    P.sy = s0.y();
    P.rot = rotA ? rotA->getCurrentBaseValue() : 0.;
    P.op = opA ? opA->getCurrentBaseValue() : 100.;
    P.shx = sh0.x();
    P.shy = sh0.y();
    P.rx = rotX ? rotX->getCurrentBaseValue() : 0.;
    P.ry = rotY ? rotY->getCurrentBaseValue() : 0.;
    P.pz = zPos ? zPos->getCurrentBaseValue() : 0.;

    LayerExprScripts inS, outS;
    if (inStartFrame >= 0 && preset.gen) {
        P.inF0 = t->prp_absFrameToRelFrameF(inStartFrame);
        preset.gen(P, inS);
    }
    if (outStartFrame >= 0 && preset.outGen) {
        // generators read the active window via winF0(); hide the
        // entrance window while generating the exit scripts
        const qreal savedInF0 = P.inF0;
        P.inF0 = -1;
        P.outF0 = t->prp_absFrameToRelFrameF(outStartFrame);
        preset.outGen(P, outS);
        P.inF0 = savedInF0;
    }

    const bool uses3D = (!inS.rotX.isEmpty() || !outS.rotX.isEmpty() ||
                         !inS.rotY.isEmpty() || !outS.rotY.isEmpty() ||
                         !inS.posZ.isEmpty() || !outS.posZ.isEmpty());
    if (uses3D && !t->is3DEnabled()) {
        t->set3DEnabled(true);
    }

    struct T { QrealAnimator* anim; const QString* inSc; const QString* outSc; };
    QList<T> targets;
    targets << T{ pos ? pos->getXAnimator() : nullptr,
                  &inS.posX, &outS.posX }
            << T{ pos ? pos->getYAnimator() : nullptr,
                  &inS.posY, &outS.posY }
            << T{ scale ? scale->getXAnimator() : nullptr,
                  &inS.scaleX, &outS.scaleX }
            << T{ scale ? scale->getYAnimator() : nullptr,
                  &inS.scaleY, &outS.scaleY }
            << T{ rotA, &inS.rot, &outS.rot }
            << T{ opA, &inS.op, &outS.op }
            << T{ shear ? shear->getXAnimator() : nullptr,
                  &inS.shearX, &outS.shearX }
            << T{ shear ? shear->getYAnimator() : nullptr,
                  &inS.shearY, &outS.shearY }
            << T{ rotX, &inS.rotX, &outS.rotX }
            << T{ rotY, &inS.rotY, &outS.rotY }
            << T{ zPos, &inS.posZ, &outS.posZ };
    for (const auto& tgt : targets) {
        if (!tgt.anim) { continue; }
        const bool hasIn = tgt.inSc && !tgt.inSc->isEmpty();
        const bool hasOut = tgt.outSc && !tgt.outSc->isEmpty();
        if (!hasIn && !hasOut) {
            // clear any expression a previously applied preset left on
            // this animator - presets must not stack (A then B used to
            // leave A's motion driving the channels B does not touch)
            if (tgt.anim->hasExpression()) {
                if (action) { tgt.anim->setExpressionAction(nullptr); }
                else { tgt.anim->setExpression(nullptr); }
            }
            continue;
        }
        QString script;
        if (hasIn && hasOut) {
            // one expression per animator: the exit window takes over
            // from its start frame on (before it, outSeg already
            // evaluates to the rest state anyway)
            script = QStringLiteral(
                        "var f = frame;\n"
                        "function dIn() { %1 }\n"
                        "function dOut() { %2 }\n"
                        "// 出场窗口（%3 帧起）开始后由出场接管\n"
                        "if (f >= %3) { return dOut(); }\n"
                        "return dIn();")
                    .arg(*tgt.inSc, *tgt.outSc, N(P.outF0));
        } else {
            script = QStringLiteral("var f = frame;\n%1")
                    .arg(hasIn ? *tgt.inSc : *tgt.outSc);
        }
        try {
            // $frame is required: its per-frame signal is the only
            // thing re-evaluating the current-frame cache on playback
            auto expr = Expression::sCreate(
                        QStringLiteral("frame = $frame;"),
                        QString::fromUtf8(kDefs), script, tgt.anim,
                        Expression::sQrealAnimatorTester);
            if (action) { tgt.anim->setExpressionAction(expr); }
            else { tgt.anim->setExpression(expr); }
        } catch (const std::exception& e) {
            qWarning() << "[layer-preset] expression failed for"
                       << tgt.anim->prp_getName() << ":" << e.what();
        } catch (...) {
            qWarning() << "[layer-preset] expression failed for"
                       << tgt.anim->prp_getName();
        }
    }
}

QList<QImage> renderPreviewSequence(BoundingBox* const box,
                                     const QList<qreal>& frames,
                                     const QSize& imgSize,
                                     const QImage& content)
{
    QList<QImage> result;
    if (!box || imgSize.width() < 2 || imgSize.height() < 2) {
        return result;
    }
    sk_sp<SkImage> contentSk;
    if (!content.isNull()) {
        const auto info = SkImageInfo::Make(
                    content.width(), content.height(),
                    kN32_SkColorType, kPremul_SkAlphaType);
        contentSk = SkImage::MakeFromRaster(
                    SkPixmap(info, content.constBits(),
                             static_cast<size_t>(content.bytesPerLine())),
                    nullptr, nullptr);
    }
    // collect the transformed bounds of every frame first so all
    // frames share one stable fit
    struct FrameData {
        QTransform transform;
        SkMatrix totalTransform;
        qreal opacity = 100;
        SkPath path;
    };
    QList<FrameData> built;
    QRectF unionBounds;
    const auto pathBox = enve_cast<PathBox*>(box);
    const auto advT = dynamic_cast<AdvancedTransformAnimator*>(
                box->getTransformAnimator());
    for (const auto relFrame : frames) {
        FrameData fd;
        fd.transform = box->getRelativeTransformAtFrame(relFrame);
        fd.opacity = box->getOpacity(relFrame);
        if (pathBox) {
            fd.path = pathBox->getRelativePath(relFrame);
        }
        if (fd.path.isEmpty()) {
            const QRectF rel = box->getRelBoundingRect();
            SkPath rect;
            rect.addRoundRect(toSkRect(rel), 8, 8);
            fd.path = rect;
        }
        fd.totalTransform = toSkMatrix(fd.transform);
        if (advT && advT->is3DEnabled() && advT->has3DTransformAtFrame(relFrame)) {
            fd.totalTransform.preConcat(advT->get3DTransformAtFrame(relFrame));
        }
        SkPath transformed = fd.path;
        transformed.transform(fd.totalTransform);
        const QRectF b = toQRectF(transformed.computeTightBounds());
        unionBounds = unionBounds.isNull() ? b : unionBounds.united(b);
        built << fd;
    }
    if (built.isEmpty() || unionBounds.isEmpty()) { return result; }

    const qreal margin = 0.08;
    const qreal availW = imgSize.width()*(1. - 2.*margin);
    const qreal availH = imgSize.height()*(1. - 2.*margin);
    const qreal scale = qMin(availW/qMax(unionBounds.width(), 1.),
                             availH/qMax(unionBounds.height(), 1.));
    const qreal tx = imgSize.width()/2. - scale*unionBounds.center().x();
    const qreal ty = imgSize.height()/2. - scale*unionBounds.center().y();

    for (const auto& fd : built) {
        QImage img(imgSize, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        const auto info = SkImageInfo::Make(imgSize.width(),
                                             imgSize.height(),
                                             kN32_SkColorType,
                                             kPremul_SkAlphaType);
        const auto surface = SkSurface::MakeRasterDirect(
                    info, img.bits(),
                    static_cast<size_t>(img.bytesPerLine()));
        if (!surface) {
            result << img;
            continue;
        }
        const auto canvas = surface->getCanvas();
        canvas->translate(toSkScalar(tx), toSkScalar(ty));
        canvas->scale(toSkScalar(scale), toSkScalar(scale));
        SkPath transformed = fd.path;
        transformed.transform(fd.totalTransform);
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setAlphaf(static_cast<float>(fd.opacity/100.));
        if (contentSk) {
            // draw the content image aspect-fitted into the
            // transformed content bounds
            const SkRect dstR = transformed.getBounds();
            const SkRect srcR = SkRect::MakeWH(
                        contentSk->width(), contentSk->height());
            canvas->drawImageRect(contentSk, srcR, dstR, &paint,
                                  SkCanvas::kStrict_SrcRectConstraint);
        } else {
            paint.setColor(QColor(235, 235, 235,
                                  qRound(fd.opacity*2.55)).rgba());
            canvas->drawPath(transformed, paint);
        }
        result << img;
    }
    return result;
}
}
