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

#include "textanimpresets.h"
#include "textanimdebug.h"

#include "Boxes/textbox.h"
#include "Boxes/textboxrenderdata.h"
#include "Animators/texteffectcollection.h"
#include "Animators/qstringanimator.h"
#include <QRegExp>

#include "skia/skqtconversions.h"
#include "skia/skiaincludes.h"

#include <QDir>
#include "Private/esettings.h"
#include "Private/document.h"
#include "Private/Tasks/taskscheduler.h"
#include <cstdio>
#include <QImage>
#include <QMatrix>
#include <QRectF>

namespace {
QList<TextAnimPreset> gPresets;

void addPreset(const TextAnimPreset& p)
{
    gPresets << p;
}

struct PresetBuilder {
    TextAnimPreset p;
    PresetBuilder& in() { p.category = 0; return *this; }
    PresetBuilder& out() {
        p.category = 1;
        p.kind = TextAnim::sweepOut;
        return *this;
    }
    PresetBuilder& loop() { p.category = 2; return *this; }
    PresetBuilder& letters() { p.fragment = 0; return *this; }
    PresetBuilder& words() { p.fragment = 1; return *this; }
    PresetBuilder& lines() { p.fragment = 2; return *this; }
    PresetBuilder& pos(const qreal x, const qreal y)
    { p.posX = x; p.posY = y; return *this; }
    PresetBuilder& rot(const qreal r)
    { p.rot = r; return *this; }
    PresetBuilder& scale(const qreal s)
    { p.scaleX = s; p.scaleY = s; return *this; }
    PresetBuilder& opacity(const qreal o)
    { p.opacity = o; return *this; }
    PresetBuilder& center()
    { p.pivotCenter = true; return *this; }
    PresetBuilder& dur(const qreal d)
    { p.duration = d; return *this; }
    PresetBuilder& soft(const qreal s)
    { p.softness = s; return *this; }
    PresetBuilder& rtl()
    { p.direction = TextAnimDirection::rightToLeft; return *this; }
    PresetBuilder& wave(const int cycles, const qreal periodSec)
    { p.kind = TextAnim::wave; p.waveCycles = cycles; p.waveTime = periodSec; return *this; }
    PresetBuilder& pulse(const qreal peak = 1.)
    { p.kind = TextAnim::pulse; p.pulsePeak = peak; return *this; }
};

void ensurePresets()
{
    if (!gPresets.isEmpty()) { return; }
    const auto B = [](const char* const id, const QString& name,
                      const QString& desc) {
        PresetBuilder b;
        b.p.id = id;
        b.p.name = name;
        b.p.desc = desc;
        return b;
    };

    // ---- one-shot (applied as entrance or exit) ----
    addPreset(B("rise", QString::fromUtf8("上浮"),
                QString::fromUtf8("入：文字从下方依次浮起；出：上飘并淡出"))
              .in().letters().pos(0, 60).opacity(0).dur(1.2).soft(0.35).p);
    addPreset(B("fade", QString::fromUtf8("淡入淡出"),
                QString::fromUtf8("入：按顺序依次显现；出：依次消失"))
              .in().letters().opacity(0).dur(1.0).soft(0.3).p);
    addPreset(B("slide-l", QString::fromUtf8("左侧划动"),
                QString::fromUtf8("入：从左滑入；出：向左滑出"))
              .in().letters().pos(-90, 0).dur(1.0).soft(0.3).p);
    addPreset(B("slide-r", QString::fromUtf8("右侧划动"),
                QString::fromUtf8("入：从右滑入；出：向右滑出"))
              .in().letters().pos(90, 0).dur(1.0).soft(0.3).p);
    addPreset(B("zoom", QString::fromUtf8("缩放"),
                QString::fromUtf8("入：由小放大弹出；出：缩小并淡出"))
              .in().letters().scale(0).center().dur(1.0).soft(0.3).p);
    addPreset(B("spin", QString::fromUtf8("旋转"),
                QString::fromUtf8("入：旋转缩放飞入；出：旋转缩小消失"))
              .in().letters().rot(120).scale(0.2).opacity(0).center().dur(1.2).soft(0.3).p);
    addPreset(B("typewriter", QString::fromUtf8("打字机"),
                QString::fromUtf8("入：逐字蹦出；出：逐字回删"))
              .in().letters().opacity(0).dur(1.6).soft(0.08).p);
    addPreset(B("drop", QString::fromUtf8("上方落下"),
                QString::fromUtf8("入：从上方逐字落下；出：向上飞离"))
              .in().letters().pos(0, -70).dur(1.0).soft(0.3).p);
    addPreset(B("word-rise", QString::fromUtf8("逐词上浮"),
                QString::fromUtf8("以单词为单位依次浮起 / 离开"))
              .in().words().pos(0, 50).opacity(0).dur(1.2).soft(0.35).p);
    addPreset(B("line-reveal", QString::fromUtf8("逐行展开"),
                QString::fromUtf8("文字按行依次浮现 / 消失"))
              .in().lines().pos(0, 36).opacity(0).dur(1.4).soft(0.4).p);
    addPreset(B("rise-rtl", QString::fromUtf8("右起上浮"),
                QString::fromUtf8("从右向左依次浮起 / 上飘离开"))
              .in().letters().pos(0, 60).opacity(0).rtl().dur(1.2).soft(0.35).p);
    addPreset(B("number-roll", QString::fromUtf8("数字滚动增长"),
                QString::fromUtf8("数字从 0 滚动增长到当前数值（保留前后缀），仅入点"))
              .in().letters().dur(1.6).p);
    gPresets.last().supportsOut = false;

    // ---- loops ----
    addPreset(B("wave", QString::fromUtf8("波浪"),
                QString::fromUtf8("波浪从文字上流过"))
              .loop().letters().pos(0, 14).wave(2, 1.1).p);
    addPreset(B("wave-sway", QString::fromUtf8("风吹摇摆"),
                QString::fromUtf8("文字像被风吹动般摇摆"))
              .loop().letters().rot(8).wave(3, 1.4).p);
    addPreset(B("wave-fade", QString::fromUtf8("波浪闪烁"),
                QString::fromUtf8("明暗呈波浪状流过文字"))
              .loop().letters().opacity(40).wave(3, 0.9).p);
    addPreset(B("breathe", QString::fromUtf8("呼吸"),
                QString::fromUtf8("文字整体轻轻缩放呼吸"))
              .loop().letters().scale(1.08).opacity(80).center().dur(1.6).pulse().p);
    addPreset(B("jitter", QString::fromUtf8("抖动"),
                QString::fromUtf8("文字高频轻微抖动"))
              .loop().letters().pos(3, 2).dur(0.14).pulse().p);
    addPreset(B("pulse-strong", QString::fromUtf8("强调脉冲"),
                QString::fromUtf8("文字快速放大再收回，用于强调"))
              .loop().letters().scale(1.25).opacity(70).center().dur(0.5).pulse().p);
}
}

namespace TextAnimPresets {
int count()
{
    ensurePresets();
    return gPresets.count();
}

const QList<TextAnimPreset>& all()
{
    ensurePresets();
    return gPresets;
}

const TextAnimPreset* byId(const QString& id)
{
    ensurePresets();
    for (const auto& p : gPresets) {
        if (p.id == id) { return &p; }
    }
    return nullptr;
}

bool apply(TextBox* const box,
           const TextAnimPreset& preset,
           const int startFrame,
           const qreal fps,
           const qreal durationScale,
           const bool out)
{
    if (!box) { return false; }
    if (preset.id == "number-roll") {
        // bake text keys counting 0 -> N, preserving any prefix and
        // suffix around the number (e.g. "progress: 42%")
        QRegExp re("(\\D*)(-?\\d+)(\\D*)");
        if (re.indexIn(box->getCurrentValue()) < 0) { return false; }
        const QString prefix = re.cap(1);
        const QString suffix = re.cap(3);
        const int target = re.cap(2).toInt();
        const auto textAnim = box->getStringAnimator();
        if (!textAnim) { return false; }
        const int durF = qMax(2, qRound(preset.duration*durationScale*fps));
        const int steps = qBound(2, qRound(durF/2.), 40);
        for (int i = 0; i <= steps; i++) {
            const int value = qRound(qreal(target)*i/steps);
            const int frame = startFrame + qRound(qreal(durF)*i/steps);
            const auto textKey = enve::make_shared<QStringKey>(
                        prefix + QString::number(value) + suffix,
                        frame, textAnim);
            textAnim->anim_appendKey(textKey);
        }
        return true;
    }
    qreal W = box->getRelBoundingRect().width();
    if (W <= 1.) {
        W = qMax(horizontalAdvance(box->getSkFont(),
                                   box->getCurrentValue()), 10.);
    }
    TextAnimPreset p = preset;
    if (out && p.kind == TextAnim::sweepIn) { p.kind = TextAnim::sweepOut; }
    const auto effect = enve::make_shared<TextEffect>();
    effect->setupFromPreset(p, W, box->getFontSize(),
                            startFrame, fps, durationScale);
    effect->prp_setName(out && p.supportsOut ?
                        p.name + QString::fromUtf8("（出）") : p.name);
    box->getTextEffects()->addChild(effect);
    return true;
}

QList<QImage> renderPreviewSequence(TextBox* const box,
                                    const QList<qreal>& frames,
                                    const QSize& imgSize)
{
    QList<QImage> result;
    if (!box || imgSize.width() < 2 || imgSize.height() < 2) { return result; }

    struct FrameData {
        stdsptr<TextBoxRenderData> data;
        QList<LetterRenderData*> letters;
    };
    QList<FrameData> built;
    QRectF unionBounds;

    for (const auto relFrame : frames) {
        const auto textData = enve::make_shared<TextBoxRenderData>(box);
        textData->fRelFrame = relFrame;
        textData->initialize(box->getTextAtRelFrame(relFrame),
                             box->getSkFont(),
                             box->getLetterSpacingAt(relFrame),
                             box->getWordSpacingAt(relFrame),
                             box->getLineSpacingAt(relFrame),
                             box->getTextHAlignment(),
                             box->getTextVAlignment(),
                             box, nullptr);
        FrameData fd;
        fd.data = textData;
        for (const auto& line : textData->fLines) {
            line->fRelFrame = relFrame;
            line->fOpacity = 100;
            for (const auto& word : line->fWords) {
                word->fRelFrame = relFrame;
                word->fOpacity = 100;
                for (const auto& letter : word->fLetters) {
                    letter->fRelFrame = relFrame;
                    // sceneless: the regular pipeline never assigned
                    // this (setupWithoutRasterEffects bails on null
                    // scene), restore the layer default
                    letter->fOpacity = 100;
                    fd.letters << letter.get();
                }
            }
        }
        if (fd.letters.isEmpty()) { continue; }
        QList<TextEffect*> effects;
        box->getTextEffects()->addEffects(effects);
        for (const auto effect : effects) {
            effect->apply(textData.get());
        }
        // collapse word/line level opacity (effects targeting words or
        // lines multiply the container opacity) onto the letters,
        // since only letters are drawn below
        for (const auto& line : textData->fLines) {
            for (const auto& word : line->fWords) {
                for (const auto& letter : word->fLetters) {
                    letter->fOpacity *= word->fOpacity*line->fOpacity/10000.;
                }
            }
        }
        for (const auto letter : fd.letters) {
            if (letter->fPath.isEmpty()) { continue; }
            SkPath transformed = letter->fPath;
            transformed.transform(toSkMatrix(letter->fTotalTransform));
            const QRectF b = toQRectF(transformed.computeTightBounds());
            unionBounds = unionBounds.isNull() ? b : unionBounds.united(b);
        }
        built << fd;
    }

    if (built.isEmpty() || unionBounds.isEmpty()) { return result; }

    const qreal margin = 0.06;
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
                    info, img.bits(), static_cast<size_t>(img.bytesPerLine()));
        if (!surface) {
            result << img;
            continue;
        }
        const auto canvas = surface->getCanvas();
        canvas->translate(toSkScalar(tx), toSkScalar(ty));
        canvas->scale(toSkScalar(scale), toSkScalar(scale));
        for (const auto letter : fd.letters) {
            canvas->save();
            canvas->concat(toSkMatrix(letter->fTotalTransform));
            SkPaint paint;
            paint.setAntiAlias(true);
            const auto alpha = toSkScalar(letter->fOpacity*0.01);
            if (!letter->fFillPath.isEmpty()) {
                letter->fPaintSettings.applyPainterSettingsSk(paint, alpha);
                canvas->drawPath(letter->fFillPath, paint);
            }
            if (!letter->fOutlinePath.isEmpty()) {
                paint.setShader(nullptr);
                letter->fStrokeSettings.applyPainterSettingsSk(paint, alpha);
                canvas->drawPath(letter->fOutlinePath, paint);
            }
            canvas->restore();
        }
        result << img;
    }
    return result;
}
}

// dev-only: bake a few presets onto a sceneless box and dump the
// rendered frames as PNGs (used by the textanimtest tool)
bool textAnimDebugDumpFrames(const QString& outDir)
{
    // console test env: the app normally creates these singletons
    if (!eSettings::sInstance) {
        const auto settings = new eSettings(4, intKB(8*1024*1024));
        Q_UNUSED(settings)
    }
    if (!Document::sInstance) {
        static TaskScheduler taskScheduler;
        static Document document(taskScheduler);
        Q_UNUSED(document)
    }
    if (!eFilterSettings::sInstance) {
        static eFilterSettings filterSettings;
        Q_UNUSED(filterSettings)
    }
    QDir().mkpath(outDir);
    const QStringList ids = { "rise-in", "typewriter", "fade-out",
                              "wave", "breathe", "line-reveal" };
    for (const auto& id : ids) {
        const auto preset = TextAnimPresets::byId(id);
        if (!preset) { continue; }
        const auto box = enve::make_shared<TextBox>();
        box->setCurrentValue(QString::fromUtf8("你好啊，动画测试\n第二行文本"));
        box->setFontFamilyAndStyle(QString::fromUtf8("微软雅黑"),
                                   SkFontStyle());
        box->setFontSize(64);
        const auto fill = box->getFillSettings();
        if (fill) {
            fill->setPaintType(PaintType::FLATPAINT);
            fill->setCurrentColor(QColor(235, 235, 235), false);
        }
        TextAnimPresets::apply(box.get(), *preset, 0, 24., 1.);
        QList<qreal> frames;
        for (int i = 0; i < 6; i++) { frames << 8.4*i; }
        const auto imgs = TextAnimPresets::renderPreviewSequence(
                    box.get(), frames, QSize(480, 220));
        int n = 0;
        for (const auto& img : imgs) {
            img.save(outDir + "/" + id + "_" +
                     QString::number(n++) + ".png");
        }
    }
    return true;
}
