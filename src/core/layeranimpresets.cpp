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

#include "layeranimpresets.h"

#include "Boxes/boundingbox.h"
#include "Boxes/pathbox.h"
#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"

#include "skia/skqtconversions.h"
#include "skia/skiaincludes.h"

#include <QMatrix>
#include <QRectF>

namespace {

void key(QrealAnimator* const a, const int f, const qreal v)
{
    if (a) { a->saveValueToKey(f, v); }
}

void keyPt(QPointFAnimator* const a, const int f,
           const qreal x, const qreal y)
{
    if (!a) { return; }
    if (a->getXAnimator()) { a->getXAnimator()->saveValueToKey(f, x); }
    if (a->getYAnimator()) { a->getYAnimator()->saveValueToKey(f, y); }
}

// ---------------------------------------------------------- bake recipes

void bakeFadeIn(AdvancedTransformAnimator* const t, const int F0,
                const int durF, const qreal, const qreal,
                const qreal, const qreal)
{
    const auto op = t->getOpacityAnimator();
    const qreal end = op ? op->getCurrentBaseValue() : 100.;
    key(op, F0, 0);
    key(op, F0 + durF, end);
}

void bakeFadeOut(AdvancedTransformAnimator* const t, const int F0,
                 const int durF, const qreal, const qreal,
                 const qreal, const qreal)
{
    const auto op = t->getOpacityAnimator();
    const qreal start = op ? op->getCurrentBaseValue() : 100.;
    key(op, F0, start);
    key(op, F0 + durF, 0);
}

void bakeSlideInLeft(AdvancedTransformAnimator* const t, const int F0,
                     const int durF, const qreal cw, const qreal,
                     const qreal bw, const qreal)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    keyPt(pos, F0, cur.x() - cw/2 - bw, cur.y());
    keyPt(pos, F0 + durF, cur.x(), cur.y());
}

void bakeSlideInRight(AdvancedTransformAnimator* const t, const int F0,
                      const int durF, const qreal cw, const qreal,
                      const qreal bw, const qreal)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    keyPt(pos, F0, cur.x() + cw/2 + bw, cur.y());
    keyPt(pos, F0 + durF, cur.x(), cur.y());
}

void bakeSlideInTop(AdvancedTransformAnimator* const t, const int F0,
                    const int durF, const qreal, const qreal ch,
                    const qreal, const qreal bh)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    keyPt(pos, F0, cur.x(), cur.y() - ch/2 - bh);
    keyPt(pos, F0 + durF, cur.x(), cur.y());
}

void bakeSlideInBottom(AdvancedTransformAnimator* const t, const int F0,
                       const int durF, const qreal, const qreal ch,
                       const qreal, const qreal bh)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    keyPt(pos, F0, cur.x(), cur.y() + ch/2 + bh);
    keyPt(pos, F0 + durF, cur.x(), cur.y());
}

void bakeSlideOutLeft(AdvancedTransformAnimator* const t, const int F0,
                      const int durF, const qreal cw, const qreal,
                      const qreal bw, const qreal)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    keyPt(pos, F0, cur.x(), cur.y());
    keyPt(pos, F0 + durF, cur.x() - cw/2 - bw, cur.y());
}

void bakeSlideOutRight(AdvancedTransformAnimator* const t, const int F0,
                       const int durF, const qreal cw, const qreal,
                       const qreal bw, const qreal)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    keyPt(pos, F0, cur.x(), cur.y());
    keyPt(pos, F0 + durF, cur.x() + cw/2 + bw, cur.y());
}

void bakeZoomIn(AdvancedTransformAnimator* const t, const int F0,
                const int durF, const qreal, const qreal,
                const qreal, const qreal)
{
    const auto scale = t->getScaleAnimator();
    const QPointF s = scale ? scale->getBaseValue() : QPointF(1, 1);
    const auto op = t->getOpacityAnimator();
    keyPt(scale, F0, 0.001, 0.001);
    keyPt(scale, F0 + durF, s.x()*1.12, s.y()*1.12);
    keyPt(scale, F0 + durF + durF*0.3, s.x()*0.94, s.y()*0.94);
    keyPt(scale, F0 + durF + durF*0.6, s.x()*1.04, s.y()*1.04);
    keyPt(scale, F0 + durF + durF*0.8, s.x(), s.y());
    key(op, F0, 0);
    key(op, F0 + qMax(1, durF/3), 100);
}

void bakeZoomOut(AdvancedTransformAnimator* const t, const int F0,
                 const int durF, const qreal, const qreal,
                 const qreal, const qreal)
{
    const auto scale = t->getScaleAnimator();
    const auto op = t->getOpacityAnimator();
    const QPointF s = scale ? scale->getBaseValue() : QPointF(1, 1);
    keyPt(scale, F0, s.x(), s.y());
    keyPt(scale, F0 + durF, 0.001, 0.001);
    key(op, F0 + durF*0.7, 100);
    key(op, F0 + durF, 0);
}

void bakeSpinIn(AdvancedTransformAnimator* const t, const int F0,
                const int durF, const qreal, const qreal,
                const qreal, const qreal)
{
    const auto rot = t->getRotAnimator();
    const auto op = t->getOpacityAnimator();
    const qreal cur = rot ? rot->getCurrentBaseValue() : 0.;
    key(rot, F0, cur + 180);
    key(rot, F0 + durF, cur);
    key(op, F0, 0);
    key(op, F0 + durF/2, 100);
}

void bakeSpinOut(AdvancedTransformAnimator* const t, const int F0,
                const int durF, const qreal, const qreal,
                const qreal, const qreal)
{
    const auto rot = t->getRotAnimator();
    const auto op = t->getOpacityAnimator();
    const qreal cur = rot ? rot->getCurrentBaseValue() : 0.;
    key(rot, F0, cur);
    key(rot, F0 + durF, cur - 180);
    key(op, F0 + durF*0.6, 100);
    key(op, F0 + durF, 0);
}

void bakePopIn(AdvancedTransformAnimator* const t, const int F0,
               const int durF, const qreal, const qreal,
               const qreal, const qreal)
{
    const auto scale = t->getScaleAnimator();
    const QPointF s = scale ? scale->getBaseValue() : QPointF(1, 1);
    keyPt(scale, F0, 0.001, 0.001);
    keyPt(scale, F0 + durF, s.x()*1.18, s.y()*1.18);
    keyPt(scale, F0 + durF + durF*0.35, s.x()*0.94, s.y()*0.94);
    keyPt(scale, F0 + durF + durF*0.6, s.x(), s.y());
}

void bakeDropBounce(AdvancedTransformAnimator* const t, const int F0,
                    const int durF, const qreal, const qreal ch,
                    const qreal, const qreal bh)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    const auto op = t->getOpacityAnimator();
    keyPt(pos, F0, cur.x(), cur.y() - ch/2 - bh);
    keyPt(pos, F0 + durF, cur.x(), cur.y() + bh*0.15);
    keyPt(pos, F0 + durF + durF*0.3, cur.x(), cur.y() - bh*0.08);
    keyPt(pos, F0 + durF + durF*0.6, cur.x(), cur.y());
    key(op, F0, 0);
    key(op, F0 + qMax(1, durF/4), 100);
}

void bakeSinkOut(AdvancedTransformAnimator* const t, const int F0,
                 const int durF, const qreal, const qreal,
                 const qreal, const qreal bh)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    const auto op = t->getOpacityAnimator();
    keyPt(pos, F0, cur.x(), cur.y());
    keyPt(pos, F0 + durF, cur.x(), cur.y() + bh + 40);
    key(op, F0 + durF*0.8, 100);
    key(op, F0 + durF, 0);
}

void bakeShake(AdvancedTransformAnimator* const t, const int F0,
               const int durF, const qreal, const qreal,
               const qreal, const qreal)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    const int step = qMax(1, durF/8);
    for (int i = 0; i <= 8; i++) {
        const int f = F0 + i*step;
        if (i % 2 == 0) { keyPt(pos, f, cur.x() - 3, cur.y() + 1); }
        else { keyPt(pos, f, cur.x() + 3, cur.y() - 2); }
    }
    keyPt(pos, F0 + 8*step, cur.x(), cur.y());
}

void bakeSway(AdvancedTransformAnimator* const t, const int F0,
              const int durF, const qreal, const qreal,
              const qreal, const qreal)
{
    const auto rot = t->getRotAnimator();
    const qreal cur = rot ? rot->getCurrentBaseValue() : 0.;
    const int step = qMax(1, durF/8);
    for (int i = 0; i <= 8; i++) {
        const int f = F0 + i*step;
        key(rot, f, cur + (i % 2 == 0 ? -6. : 6.));
    }
    key(rot, F0 + 8*step, cur);
}

void bakeHop(AdvancedTransformAnimator* const t, const int F0,
             const int durF, const qreal, const qreal,
             const qreal, const qreal bh)
{
    const auto pos = t->getPosAnimator();
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    const auto scale = t->getScaleAnimator();
    const QPointF s = scale ? scale->getBaseValue() : QPointF(1, 1);
    const qreal hopH = qMax(bh*0.6, 30.);
    const int half = qMax(2, durF/2);   // one hop
    for (int h = 0; h < 2; h++) {
        const int b = F0 + h*half;      // ground contact
        // squash at contact, stretch mid-air
        keyPt(scale, b, s.x()*1.15, s.y()*0.85);
        keyPt(scale, b + half/2, s.x()*0.95, s.y()*1.06);
        keyPt(pos, b + half/8, cur.x(), cur.y());
        keyPt(pos, b + half/2, cur.x(), cur.y() - hopH);
        keyPt(pos, b + half*7/8, cur.x(), cur.y());
    }
    keyPt(scale, F0 + durF, s.x(), s.y());
    keyPt(pos, F0 + durF, cur.x(), cur.y());
}

void bakeSquashStretch(AdvancedTransformAnimator* const t, const int F0,
                       const int durF, const qreal, const qreal,
                       const qreal, const qreal bh)
{
    const auto scale = t->getScaleAnimator();
    const auto pos = t->getPosAnimator();
    const QPointF s = scale ? scale->getBaseValue() : QPointF(1, 1);
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    const int quarter = qMax(2, durF/4);
    for (int i = 0; i <= 4; i++) {
        const int f = F0 + i*quarter;
        if (i % 2 == 0) {
            keyPt(scale, f, s.x()*0.8, s.y()*1.25);
            keyPt(pos, f, cur.x(), cur.y());
        } else {
            keyPt(scale, f, s.x()*1.2, s.y()*0.85);
            keyPt(pos, f, cur.x(), cur.y() - qMax(bh*0.3, 14.));
        }
    }
    keyPt(scale, F0 + durF, s.x(), s.y());
    keyPt(pos, F0 + durF, cur.x(), cur.y());
}

void bakeBreathe(AdvancedTransformAnimator* const t, const int F0,
                 const int durF, const qreal, const qreal,
                 const qreal, const qreal)
{
    const auto scale = t->getScaleAnimator();
    const QPointF s = scale ? scale->getBaseValue() : QPointF(1, 1);
    const int half = qMax(2, durF/2);
    for (int c = 0; c <= 4; c++) {
        const int f = F0 + c*half;
        if (c % 2 == 0) { keyPt(scale, f, s.x(), s.y()); }
        else { keyPt(scale, f, s.x()*1.05, s.y()*1.05); }
    }
}

void bakeWobble(AdvancedTransformAnimator* const t, const int F0,
                const int durF, const qreal, const qreal,
                const qreal, const qreal)
{
    const auto rot = t->getRotAnimator();
    const auto pos = t->getPosAnimator();
    const qreal r = rot ? rot->getCurrentBaseValue() : 0.;
    const QPointF cur = pos ? pos->getBaseValue() : QPointF();
    const int quarter = qMax(2, durF/8);
    for (int i = 0; i <= 8; i++) {
        const int f = F0 + i*quarter;
        const qreal sign = i % 2 == 0 ? -1. : 1.;
        key(rot, f, r + 4.*sign);
        keyPt(pos, f, cur.x() + 6.*sign, cur.y());
    }
    key(rot, F0 + 8*quarter, r);
    keyPt(pos, F0 + 8*quarter, cur.x(), cur.y());
}

struct LayerPresetBuilder {
    LayerAnimPreset p;
    LayerPresetBuilder& in() { p.category = 0; return *this; }
    LayerPresetBuilder& out() { p.category = 1; return *this; }
    LayerPresetBuilder& loop() { p.category = 2; return *this; }
    LayerPresetBuilder& dur(const qreal d) { p.duration = d; return *this; }
    LayerPresetBuilder& outRecipe(
            void (*ob)(AdvancedTransformAnimator*, int, int,
                       qreal, qreal, qreal, qreal))
    { p.outBake = ob; return *this; }
    LayerPresetBuilder& recipe(
            void (*bake)(AdvancedTransformAnimator*, int, int,
                         qreal, qreal, qreal, qreal))
    { p.bake = bake; return *this; }
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
              .in().dur(0.8).recipe(&bakeFadeIn)
              .outRecipe(&bakeFadeOut).p);
    addPreset(gPresets, B("l-slide-l", QString::fromUtf8("左侧划动"),
                          QString::fromUtf8("入：从画布左外滑入；出：滑出画布左侧"))
              .in().dur(0.8).recipe(&bakeSlideInLeft)
              .outRecipe(&bakeSlideOutLeft).p);
    addPreset(gPresets, B("l-slide-r", QString::fromUtf8("右侧划动"),
                          QString::fromUtf8("入：从画布右外滑入；出：滑出画布右侧"))
              .in().dur(0.8).recipe(&bakeSlideInRight)
              .outRecipe(&bakeSlideOutRight).p);
    addPreset(gPresets, B("l-zoom", QString::fromUtf8("弹性缩放"),
                          QString::fromUtf8("入：放大弹出回弹稳定；出：缩小淡出"))
              .in().dur(0.7).recipe(&bakeZoomIn)
              .outRecipe(&bakeZoomOut).p);
    addPreset(gPresets, B("l-spin", QString::fromUtf8("旋转"),
                          QString::fromUtf8("入：旋转半圈淡入落定；出：旋转缩小消失"))
              .in().dur(0.9).recipe(&bakeSpinIn)
              .outRecipe(&bakeSpinOut).p);
    addPreset(gPresets, B("l-pop", QString::fromUtf8("弹出"),
                          QString::fromUtf8("快速弹出并轻微回弹（仅入点）"))
              .in().dur(0.5).recipe(&bakePopIn).p);
    addPreset(gPresets, B("l-drop", QString::fromUtf8("落下弹跳"),
                          QString::fromUtf8("从上方落下触地弹两下（仅入点）"))
              .in().dur(1.0).recipe(&bakeDropBounce).p);
    addPreset(gPresets, B("l-sink", QString::fromUtf8("下沉"),
                          QString::fromUtf8("下沉并淡出（仅出点）"))
              .in().dur(0.9).recipe(nullptr)
              .outRecipe(&bakeSinkOut).p);

    // ---- loops ----
    addPreset(gPresets, B("l-shake", QString::fromUtf8("抖动"),
                          QString::fromUtf8("高频左右小幅抖动"))
              .loop().dur(0.5).recipe(&bakeShake).p);
    addPreset(gPresets, B("l-sway", QString::fromUtf8("摇摆"),
                          QString::fromUtf8("像钟摆一样左右摇摆"))
              .loop().dur(1.2).recipe(&bakeSway).p);
    addPreset(gPresets, B("l-hop", QString::fromUtf8("蹦跳"),
                          QString::fromUtf8("原地起跳两次，落地压扁"))
              .loop().dur(1.2).recipe(&bakeHop).p);
    addPreset(gPresets, B("l-squash", QString::fromUtf8("弹性拉伸蹦跶"),
                          QString::fromUtf8("原地压扁拉伸交替小幅蹦跶"))
              .loop().dur(0.9).recipe(&bakeSquashStretch).p);
    addPreset(gPresets, B("l-breathe", QString::fromUtf8("呼吸"),
                          QString::fromUtf8("缓慢缩放呼吸起伏"))
              .loop().dur(1.6).recipe(&bakeBreathe).p);
    addPreset(gPresets, B("l-wobble", QString::fromUtf8("晃动"),
                          QString::fromUtf8("左右平移加轻微旋转晃动"))
              .loop().dur(1.0).recipe(&bakeWobble).p);
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
           const int startFrame,
           const qreal fps,
           const qreal durationScale,
           const qreal canvasW,
           const qreal canvasH,
           const bool out)
{
    const auto bake = out ? preset.outBake : preset.bake;
    if (!box || !bake) { return; }
    // box transform animators are AdvancedTransformAnimator-based
    // (BoxTransformAnimator); bake needs the opacity animator too
    const auto t = dynamic_cast<AdvancedTransformAnimator*>(
                box->getTransformAnimator());
    if (!t) { return; }
    const int durF = qMax(2, qRound(preset.duration*durationScale*fps));
    const QRectF rel = box->getRelBoundingRect();
    bake(t, startFrame, durF,
         qMax(canvasW, 2.), qMax(canvasH, 2.),
         qMax(rel.width(), 2.), qMax(rel.height(), 2.));
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
