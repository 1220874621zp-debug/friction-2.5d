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

#include <QMatrix>
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

struct LayerPresetBuilder {
    LayerAnimPreset p;
    LayerPresetBuilder& in() { p.category = 0; return *this; }
    LayerPresetBuilder& out() { p.category = 1; return *this; }
    LayerPresetBuilder& loop() { p.category = 2; return *this; }
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

    // ---- one-shot (in / out) ----
    addPreset(gPresets, B("l-fade", QString::fromUtf8("淡入淡出"),
                          QString::fromUtf8("入：渐渐显现；出：渐渐消失"))
              .in().dur(0.8).genExpr(&genFadeIn)
              .outGen(&genFadeOut).p);
    addPreset(gPresets, B("l-slide-l", QString::fromUtf8("左侧划动"),
                          QString::fromUtf8("入：从画布左外滑入；出：滑出画布左侧"))
              .in().dur(0.8).genExpr(&genSlideInLeft)
              .outGen(&genSlideOutLeft).p);
    addPreset(gPresets, B("l-slide-r", QString::fromUtf8("右侧划动"),
                          QString::fromUtf8("入：从画布右外滑入；出：滑出画布右侧"))
              .in().dur(0.8).genExpr(&genSlideInRight)
              .outGen(&genSlideOutRight).p);
    addPreset(gPresets, B("l-zoom", QString::fromUtf8("弹性缩放"),
                          QString::fromUtf8("入：放大弹出回弹稳定；出：缩小淡出"))
              .in().dur(0.7).genExpr(&genZoomIn)
              .outGen(&genZoomOut).p);
    addPreset(gPresets, B("l-spin", QString::fromUtf8("旋转"),
                          QString::fromUtf8("入：旋转半圈淡入落定；出：旋转缩小消失"))
              .in().dur(0.9).genExpr(&genSpinIn)
              .outGen(&genSpinOut).p);
    addPreset(gPresets, B("l-pop", QString::fromUtf8("弹出"),
                          QString::fromUtf8("快速弹出并轻微回弹（仅入点）"))
              .in().dur(0.5).genExpr(&genPopIn).p);
    addPreset(gPresets, B("l-drop", QString::fromUtf8("落下弹跳"),
                          QString::fromUtf8("从上方落下触地弹两下（仅入点）"))
              .in().dur(1.0).genExpr(&genDropBounce).p);
    addPreset(gPresets, B("l-sink", QString::fromUtf8("下沉"),
                          QString::fromUtf8("下沉并淡出（仅出点）"))
              .in().dur(0.9).outGen(&genSinkOut).p);

    // ---- loops (infinite: the expression keeps evaluating for any
    // frame past the window start) ----
    addPreset(gPresets, B("l-shake", QString::fromUtf8("抖动"),
                          QString::fromUtf8("高频左右小幅抖动"))
              .loop().dur(0.5).genExpr(&genShake).p);
    addPreset(gPresets, B("l-sway", QString::fromUtf8("摇摆"),
                          QString::fromUtf8("像钟摆一样左右摇摆"))
              .loop().dur(1.2).genExpr(&genSway).p);
    addPreset(gPresets, B("l-hop", QString::fromUtf8("蹦跳"),
                          QString::fromUtf8("原地起跳两次，落地压扁"))
              .loop().dur(1.2).genExpr(&genHop).p);
    addPreset(gPresets, B("l-squash", QString::fromUtf8("弹性拉伸蹦跶"),
                          QString::fromUtf8("原地压扁拉伸交替小幅蹦跶"))
              .loop().dur(0.9).genExpr(&genSquashStretch).p);
    addPreset(gPresets, B("l-breathe", QString::fromUtf8("呼吸"),
                          QString::fromUtf8("缓慢缩放呼吸起伏"))
              .loop().dur(1.6).genExpr(&genBreathe).p);
    addPreset(gPresets, B("l-wobble", QString::fromUtf8("晃动"),
                          QString::fromUtf8("左右平移加轻微旋转晃动"))
              .loop().dur(1.0).genExpr(&genWobble).p);
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
    const auto gen = preset.gen;
    if (!box || !gen) { return; }
    // box transform animators are AdvancedTransformAnimator-based
    // (BoxTransformAnimator); expressions go on the qreal
    // sub-animators (pos/scale x/y, rotation, opacity)
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
    const QPointF p0 = pos ? pos->getBaseValue() : QPointF();
    const QPointF s0 = scale ? scale->getBaseValue() : QPointF(1, 1);
    P.px = p0.x();
    P.py = p0.y();
    P.sx = s0.x();
    P.sy = s0.y();
    P.rot = rotA ? rotA->getCurrentBaseValue() : 0.;
    P.op = opA ? opA->getCurrentBaseValue() : 100.;

    LayerExprScripts inS, outS;
    if (inStartFrame >= 0) {
        P.inF0 = t->prp_absFrameToRelFrameF(inStartFrame);
        gen(P, inS);
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
            << T{ opA, &inS.op, &outS.op };
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
    struct FrameData { QMatrix transform; qreal opacity; SkPath path; };
    QList<FrameData> built;
    QRectF unionBounds;
    const auto pathBox = enve_cast<PathBox*>(box);
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
        SkPath transformed = fd.path;
        transformed.transform(toSkMatrix(fd.transform));
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
        transformed.transform(toSkMatrix(fd.transform));
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
