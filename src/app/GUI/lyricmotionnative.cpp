#include "lyricmotionnative.h"

#include "lyricmotioncanvas.h"
#include "lyricmotionengine.h"
#include "canvas.h"
#include "Private/document.h"
#include "Boxes/textbox.h"
#include "Boxes/containerbox.h"
#include "Boxes/rectangle.h"
#include "Boxes/circle.h"
#include "Boxes/smartvectorpath.h"
#include "Sound/eindependentsound.h"
#include "Animators/qrealanimator.h"
#include "Animators/qstringanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/transformanimator.h"
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffect.h"
#include "textanimpresets.h"
#include "Scripting/jsapi.h"
#include "appsupport.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <QColor>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJSEngine>
#include <QHash>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <cmath>

namespace {

// ======================================================================
// context + tiny helpers
// ======================================================================

struct Ctx {
    Canvas *scene = nullptr;
    qreal fps = 24;
    qreal cw = 1920;
    qreal ch = 1080;
    QJsonObject fx;
    QJsonArray schemes;
    QJsonArray events;
    QJsonObject fonts;
    int koma = 12;               // bake grid: 12 fps steps (0 = every frame)
    qreal motion = 0.7;
    qreal glitch = 0.55;
    LyricMotionNative::Result *res = nullptr;
};

int fSec(const Ctx &c, const qreal t) { return qRound(t * c.fps); }

int komaStep(const Ctx &c) {
    return c.koma > 0 ? qMax(1, qRound(c.fps / c.koma)) : 1;
}

QColor parseColor(const QJsonValue &v, const QColor &fallback) {
    const QString s = v.toString();
    if (!s.isEmpty()) {
        const QColor c(s);
        if (c.isValid()) { return c; }
    }
    return fallback;
}

struct LocalFont { QString family; int weight; };

// JIZURA's Google families are not vendored: resolve by kind, keep weight
LocalFont localFont(const QJsonObject &fontDef) {
    const QString kind = fontDef.value(QStringLiteral("kind")).toString();
    int weight = fontDef.value(QStringLiteral("weight")).toInt(700);
    weight = qBound(300, weight, 900);
    QString family;
    if (kind == QStringLiteral("mincho") || kind == QStringLiteral("brush")) {
        family = QStringLiteral("Noto Serif CJK JP");
    } else if (kind == QStringLiteral("mono")) {
        family = QStringLiteral("Noto Sans Mono CJK JP");
    } else {
        family = QStringLiteral("Noto Sans CJK JP");
        if (kind == QStringLiteral("display")) { weight = qMax(weight, 900); }
    }
    return {family, weight};
}

// largest font size whose longest rendered line stays within targetWidth
qreal fitSize(const QString &text, const QString &family, const int weight,
              const qreal targetWidth) {
    const auto typeface = SkTypeface::MakeFromName(
                family.toUtf8().constData(),
                SkFontStyle(weight, SkFontStyle::kNormal_Width,
                            SkFontStyle::kUpright_Slant));
    SkFont font(typeface ? typeface : SkTypeface::MakeDefault(), 100);
    qreal widest = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString probe = line.left(96);
        const qreal w = font.measureText(probe.utf16(),
                                         probe.size() * sizeof(char16_t),
                                         SkTextEncoding::kUTF16);
        widest = qMax(widest, w);
    }
    if (widest <= 1) { return 48; }
    return 100.0 * targetWidth / widest;
}

// naive wrap: break around maxChars, preferring spaces for latin runs
QString splitLines(const QString &text, const int maxChars) {
    QStringList in = text.split(QLatin1Char('\n'));
    QStringList out;
    for (const QString &src : in) {
        if (src.length() <= maxChars) { out << src; continue; }
        QString cur;
        for (const QChar &ch : src) {
            cur += ch;
            const bool latin = ch.isLetterOrNumber() && ch.script() != QChar::Script_Han
                    && ch.script() != QChar::Script_Hiragana
                    && ch.script() != QChar::Script_Katakana
                    && ch.script() != QChar::Script_Hangul;
            if ((int)cur.length() >= maxChars && !latin) {
                out << cur; cur.clear();
            } else if ((int)cur.length() >= maxChars && ch.isSpace()) {
                out << cur.trimmed(); cur.clear();
            }
        }
        if (!cur.trimmed().isEmpty()) { out << cur.trimmed(); }
    }
    return out.join(QLatin1Char('\n'));
}

// ======================================================================
// keyframe baking
// ======================================================================

void bakeSpan(QrealAnimator * const anim, const Ctx &c,
              const int f0, const int f1,
              const std::function<qreal(qreal)> &fn) {
    if (!anim || f1 < f0) { return; }
    const int step = komaStep(c);
    for (int f = f0; f <= f1; f += step) {
        const qreal p = (f1 == f0) ? 1.0 : qreal(f - f0) / qreal(f1 - f0);
        anim->saveValueToKey(f, fn(p));
    }
    anim->saveValueToKey(f1, fn(1.0));
}

void bakeSpanXY(QPointFAnimator * const anim, const Ctx &c,
                const int f0, const int f1,
                const std::function<QPointF(qreal)> &fn) {
    if (!anim || f1 < f0) { return; }
    const int step = komaStep(c);
    for (int f = f0; f <= f1; f += step) {
        const qreal p = (f1 == f0) ? 1.0 : qreal(f - f0) / qreal(f1 - f0);
        const QPointF pt = fn(p);
        anim->getXAnimator()->saveValueToKey(f, pt.x());
        anim->getYAnimator()->saveValueToKey(f, pt.y());
    }
    const QPointF end = fn(1.0);
    anim->getXAnimator()->saveValueToKey(f1, end.x());
    anim->getYAnimator()->saveValueToKey(f1, end.y());
}

// ======================================================================
// primitives
// ======================================================================

TextBox *mkText(ContainerBox * const parent, const QString &text,
                const QString &family, const int weight, const qreal size,
                const QColor &fill) {
    const auto box = enve::make_shared<TextBox>();
    parent->addContained(box);
    box->prp_setName(QStringLiteral("歌词"));
    box->setCurrentValue(text);
    box->setFontFamilyAndStyle(
                family,
                SkFontStyle(weight, SkFontStyle::kNormal_Width,
                            SkFontStyle::kUpright_Slant));
    box->setFontSize(qMax(6.0, size));
    // centered alignment: the box origin (and thus the position set by
    // the layout recipes) lands on the text block's visual center —
    // the TextBox default is left/top, which pushed wide lyric lines
    // off the bottom-right of the canvas
    box->setTextHAlignment(Qt::AlignHCenter);
    box->setTextVAlignment(Qt::AlignVCenter);
    box->getFillSettings()->setPaintType(PaintType::FLATPAINT);
    box->getFillSettings()->setCurrentColor(fill);
    return box.get();
}

RectangleBox *mkRect(ContainerBox * const parent, const QRectF &r,
                     const QColor &fill, const qreal radiusX = 0,
                     const qreal radiusY = 0) {
    const auto box = enve::make_shared<RectangleBox>();
    parent->addContained(box);
    box->prp_setName(QStringLiteral("形状"));
    box->setTopLeftPos(QPointF(r.left(), r.top()));
    box->setBottomRightPos(QPointF(r.right(), r.bottom()));
    box->setXRadius(radiusX);
    box->setYRadius(radiusY);
    if (fill.isValid()) {
        box->getFillSettings()->setPaintType(PaintType::FLATPAINT);
        box->getFillSettings()->setCurrentColor(fill);
    } else {
        box->getFillSettings()->setPaintType(PaintType::NOPAINT);
        auto *st = box->getStrokeSettings();
        st->setPaintType(PaintType::FLATPAINT);
    }
    return box.get();
}

Circle *mkCircle(ContainerBox * const parent, const QPointF &center,
                 const qreal radius, const QColor &fill) {
    const auto box = enve::make_shared<Circle>();
    parent->addContained(box);
    box->prp_setName(QStringLiteral("形状"));
    box->setCenter(center);
    box->setRadius(radius);
    if (fill.isValid()) {
        box->getFillSettings()->setPaintType(PaintType::FLATPAINT);
        box->getFillSettings()->setCurrentColor(fill);
    } else {
        box->getFillSettings()->setPaintType(PaintType::NOPAINT);
        auto *st = box->getStrokeSettings();
        st->setPaintType(PaintType::FLATPAINT);
    }
    return box.get();
}

void setStroke(TextBox * const box, const QColor &color, const qreal width) {
    auto *st = box->getStrokeSettings();
    st->setPaintType(PaintType::FLATPAINT);
    st->setCurrentColor(color);
    st->getStrokeWidthAnimator()->setCurrentBaseValue(width);
}

ContainerBox *mkGroup(ContainerBox * const parent, const QString &name) {
    const auto g = enve::make_shared<ContainerBox>(eBoxType::layer);
    parent->addContained(g);
    g->prp_setName(name);
    return g.get();
}

QrealAnimator *opacityAnim(BoundingBox * const box) {
    return box->getBoxTransformAnimator()->getOpacityAnimator();
}

// ======================================================================
// structural motion recipes (the JIZURA look lives in structure —
// mask reveals, char swaps, layered copies — not just preset sweeps)
// ======================================================================

// alpha track-matte rectangle attached above the text box; animating
// the rect's bottom edge sweeps the text in/out (riseMask/shutter/
// blinds/wipe family). W/H describe the covered text block.
void maskRevealRect(Ctx &c, ContainerBox * const group, TextBox * const text,
                    const int f0, const int f1, const qreal W,
                    const qreal H, const bool vertical,
                    const qreal dirSign) {
    const auto matte = enve::make_shared<RectangleBox>();
    group->addContained(matte);
    matte->prp_setName(QStringLiteral("蒙版"));
    matte->getFillSettings()->setPaintType(PaintType::FLATPAINT);
    matte->getFillSettings()->setCurrentColor(Qt::white);
    const QPointF base = text->getTransformAnimator()
            ->getPosAnimator()->getBaseValue();
    matte->getTransformAnimator()->getPosAnimator()->setBaseValue(base);
    if (vertical) {
        matte->setTopLeftPos(QPointF(-W / 2, dirSign > 0 ? 0 : -H));
        matte->setBottomRightPos(QPointF(W / 2, dirSign > 0 ? 0 : -H));
        auto *br = matte->getBottomRightAnimator();
        auto *tl = matte->getTopLeftAnimator();
        if (dirSign > 0) {
            br->getYAnimator()->saveValueToKey(f1, H);
        } else {
            tl->getYAnimator()->saveValueToKey(f1, 0);
        }
        br->getXAnimator()->setCurrentBaseValue(W / 2);
        tl->getXAnimator()->setCurrentBaseValue(-W / 2);
    } else {
        matte->setTopLeftPos(QPointF(dirSign > 0 ? 0 : -W, -H / 2));
        matte->setBottomRightPos(QPointF(dirSign > 0 ? 0 : -W, H / 2));
        auto *br = matte->getBottomRightAnimator();
        auto *tl = matte->getTopLeftAnimator();
        if (dirSign > 0) {
            br->getXAnimator()->saveValueToKey(f1, W / 2);
        } else {
            tl->getXAnimator()->saveValueToKey(f1, 0);
        }
        br->getYAnimator()->setCurrentBaseValue(H / 2);
        tl->getYAnimator()->setCurrentBaseValue(-H / 2);
    }
    text->trackMatteTarget()->setTargetAction(matte.get());
    text->setTrackMatteMode(1); // alpha matte
}

// iris reveal: circular alpha matte whose radius grows from 0
void maskRevealIris(Ctx &c, ContainerBox * const group, TextBox * const text,
                    const int f0, const int f1, const qreal R) {
    const auto matte = enve::make_shared<Circle>();
    group->addContained(matte);
    matte->prp_setName(QStringLiteral("蒙版"));
    matte->getFillSettings()->setPaintType(PaintType::FLATPAINT);
    matte->getFillSettings()->setCurrentColor(Qt::white);
    const QPointF base = text->getTransformAnimator()
            ->getPosAnimator()->getBaseValue();
    matte->getTransformAnimator()->getPosAnimator()->setBaseValue(base);
    matte->setRadius(0);
    if (auto *rh = matte->getHRadiusAnimator()) {
        auto *rx = rh->getXAnimator();
        rx->saveValueToKey(f0, 0);
        rx->saveValueToKey(f1, R);
    }
    if (auto *rv = matte->getVRadiusAnimator()) {
        auto *ry = rv->getXAnimator();
        ry->saveValueToKey(f0, 0);
        ry->saveValueToKey(f1, R);
    }
    text->trackMatteTarget()->setTargetAction(matte.get());
    text->setTrackMatteMode(1);
}

// character-scramble reveal: text keys cycle random glyphs, resolving
// left-to-right into the real text (scramble / decrypt family)
void scrambleKeys(Ctx &c, TextBox * const text, const int f0,
                  const int f1, const quint32 seed) {
    auto *anim = text->getStringAnimator();
    if (!anim || f1 <= f0) { return; }
    const QString target = text->getCurrentValue();
    const int n = target.length();
    if (n < 1) { return; }
    static const QString pool =
            QStringLiteral("AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPpQqRrSsTtUuVvWwXxYyZz0123456789#$%&@?");
    QRandomGenerator rng(seed ? seed : 1);
    const int steps = qBound(3, (f1 - f0) / 2, 24);
    for (int s = 1; s <= steps; s++) {
        const int frame = f0 + (f1 - f0) * s / steps;
        const int solved = qRound(qreal(n) * s / steps);
        QString cur = target.left(solved);
        for (int k = solved; k < n; k++) {
            const QChar &orig = target.at(k);
            cur += orig.isSpace() ? orig
                    : pool.at(rng.bounded(pool.length()));
        }
        anim->anim_appendKey(enve::make_shared<QStringKey>(
                                 cur, frame, anim));
    }
}

// echo/duplicate entrance: N copies of the text fade in staggered and
// slide together (echoIn / trail family); copies sit below the main
QList<TextBox*> echoTrail(Ctx &c, ContainerBox * const group,
                          TextBox * const main, const int f0,
                          const int fInFrames, const qreal size,
                          const QColor &col, const int copies) {
    QList<TextBox*> out;
    const QPointF base = main->getTransformAnimator()
            ->getPosAnimator()->getBaseValue();
    for (int k = copies; k >= 1; k--) {
        auto *echo = mkText(group, main->getCurrentValue(),
                            QStringLiteral("Noto Sans CJK JP"), 700,
                            main->getFontSize(),
                            col.lighter(100 + 10 * k));
        echo->prp_setName(QStringLiteral("残影 %1").arg(k));
        const qreal off = size * 0.22 * k;
        echo->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    base + QPointF(off, -off * 0.2));
        auto *opa = opacityAnim(echo);
        const int fStart = f0 + qRound(qreal(fInFrames) * 0.55);
        const int fEnd = f0 + fInFrames + k * 2;
        opa->saveValueToKey(qMax(f0, fStart - k), 0);
        opa->saveValueToKey(fEnd, 65 - 12 * k);
        out << echo;
    }
    return out;
}

// ======================================================================
// preset + effect helpers
// ======================================================================

bool applyPreset(BoundingBox * const box, const QString &id,
                 const int startFrame, const Ctx &c,
                 const qreal inSec, const bool out) {
    auto *boxText = dynamic_cast<TextBox*>(box);
    if (!boxText) { return false; }
    const auto *p = TextAnimPresets::byId(id);
    if (!p) { return false; }
    const qreal scale = inSec > 0.01 ? inSec / p->duration : 1.0;
    return TextAnimPresets::apply(boxText, *p, startFrame, c.fps, scale, out);
}

RasterEffect *addEffect(BoundingBox * const box, const RasterEffectType type) {
    const auto eff = createRasterEffectForNonCustomType(type);
    if (eff) { box->addRasterEffect(eff); }
    return eff.get();
}

QrealAnimator *qparam(RasterEffect * const eff,
                      const std::initializer_list<const char*> &names) {
    if (!eff) { return nullptr; }
    for (const char * const n : names) {
        if (auto *a = eff->ca_getFirstDescendantWithName<QrealAnimator>(
                    QString::fromUtf8(n))) { return a; }
    }
    return nullptr;
}

// one pulse of `amount`: baseline → peak at f(t) → baseline at f(t+dur)
void pulseParam(QrealAnimator * const anim, const Ctx &c,
                const qreal t, const qreal dur, const qreal peak,
                const qreal baseline) {
    if (!anim) { return; }
    const int f0 = fSec(c, t);
    const int f1 = fSec(c, t + dur);
    anim->setCurrentBaseValue(baseline);
    anim->saveValueToKey(qMax(0, f0 - 1), baseline);
    anim->saveValueToKey(f0, peak);
    anim->saveValueToKey(f1, baseline);
}


// ======================================================================
// cut text layout (family-level recipes)
// ======================================================================

struct CutText {
    QList<TextBox*> boxes;   // main text boxes (animation targets)
    QList<TextBox*> allTexts; // materialized texts incl. decorative
    qreal mainSize = 0;
    QPointF anchor;
};

// kn* kinetic family: word-level choreography — each word is its own
// box with its own sub-window, and every part key gets a distinct
// arrangement + motion hook (the word-swap rhythm stays for all)
CutText kineticWords(Ctx &c, ContainerBox * const group,
                     const QJsonObject &cut, const QString &text,
                     const LocalFont &lf, const QColor &textCol,
                     const QColor &accent, const QColor &sub) {
    CutText ct;
    const QString layout = cut.value(QStringLiteral("layout")).toString();
    QStringList words = text.split(
                QRegularExpression(QStringLiteral("[\\s、，,/]+")),
                Qt::SkipEmptyParts);
    if (words.isEmpty()) { words << text; }
    QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                .toInt(1)) ^ 0x4B4Eu);
    const int f0 = fSec(c, cut.value(QStringLiteral("start")).toDouble());
    const int f1 = qMax(f0 + 2, fSec(c,
                    cut.value(QStringLiteral("end")).toDouble()));
    const int n = words.size();
    // per-word windows: overlap 55% so swaps read as motion
    const int span = qMax(2, (f1 - f0) * 2 / (n + 1));

    // choreography per part key — arrangement variants
    const int arrange = [&]() {
        if (layout == QStringLiteral("knSlamStack")
                || layout == QStringLiteral("knStackAway")) return 1;
        if (layout == QStringLiteral("knSwapCenter")
                || layout == QStringLiteral("knCollide")
                || layout == QStringLiteral("knSeesaw")) return 2;
        if (layout == QStringLiteral("knQuarterTurn")
                || layout == QStringLiteral("knTumble")
                || layout == QStringLiteral("knReflow")) return 3;
        if (layout == QStringLiteral("knPadGrid")
                || layout == QStringLiteral("knGearWords")) return 4;
        if (layout == QStringLiteral("knZoomDive")
                || layout == QStringLiteral("knDiveGlyph")) return 5;
        if (layout == QStringLiteral("knFlowSnap")) return 6;
        if (layout == QStringLiteral("knPathRide")) return 7;
        return int(rng.generate() % 4);
    }();
    for (int k = 0; k < n; k++) {
        const bool big = (k % 3 == 0) || n == 1;
        qreal size = c.ch * (big ? 0.17 : 0.11)
                * (0.9 + 0.25 * rng.generateDouble());
        QPointF pos(c.cw * 0.5, c.ch * 0.5);
        qreal rot = 0;
        switch (arrange) {
        case 1: // 積み上げ: words stack up from the bottom
            pos = QPointF(c.cw * 0.5,
                          c.ch * (0.78 - 0.5 * k / qMax(1, n - 1)));
            size = c.ch * 0.13;
            break;
        case 2: // 入れ替わり/衝突/シーソー: two-side alternation
            pos = QPointF(c.cw * (k % 2 ? 0.7 : 0.3), c.ch * 0.5);
            size = c.ch * 0.16;
            break;
        case 3: // 直角ターン/箱転がし/縦から横へ: quarter-turn steps
            pos = QPointF(c.cw * 0.5,
                          c.ch * (0.26 + 0.5 * k / qMax(1, n - 1)));
            rot = 90.0 * k / qMax(1, n - 1);
            break;
        case 4: // パッド/歯車: pad-grid cells
            pos = QPointF(c.cw * (0.3 + 0.4 * (k % 2)),
                          c.ch * (0.3 + 0.4 * (k / 2 % 2)));
            size = c.ch * 0.13;
            break;
        case 5: // 文字へ潜る: dive — later words grow toward the lens
            pos = QPointF(c.cw * 0.5, c.ch * 0.5);
            size = c.ch * (0.08 + 0.14 * k / qMax(1, n - 1));
            break;
        case 6: // 流れて整列: words settle into a justified row
            pos = QPointF(c.cw * (0.2 + 0.6 * k / qMax(1, n - 1)),
                          c.ch * 0.5);
            size = c.ch * 0.12;
            break;
        case 7: { // ループ軌道: words ride a circle
            const qreal ang = -90.0 + 360.0 * k / n;
            const qreal R = qMin(c.cw, c.ch) * 0.26;
            pos = QPointF(c.cw * 0.5 + qCos(qDegreesToRadians(ang)) * R,
                          c.ch * 0.5 + qSin(qDegreesToRadians(ang)) * R);
            rot = ang + 90;
            size = c.ch * 0.1;
            break;
        }
        default: // relay / alternating rows
            pos = QPointF(c.cw * 0.5, c.ch * (0.32 + 0.36 * (k % 2)));
            break;
        }
        auto *b = mkText(group, words.at(k), lf.family,
                         big ? 900 : 700, size,
                         big ? accent : textCol);
        b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        if (qAbs(rot) > 0.01) {
            b->getTransformAnimator()->getRotAnimator()
                    ->setCurrentBaseValue(rot);
        }
        // the word only lives inside its sub-window
        const int wStart = f0 + qMax(0, (f1 - f0 - span) * k / qMax(1, n - 1));
        const int wEnd = qMin(f1, wStart + span);
        b->createDurationRectangle();
        if (const auto dr = b->getDurationRectangle()) {
            dr->setMinAbsFrame(qMax(f0 - 1, wStart - 1));
            dr->setFramesDuration(qMax(2, wEnd - wStart + 2));
        }
        // per-key motion hooks layered on the word window
        if (layout == QStringLiteral("knTypeSlam")
                || layout == QStringLiteral("knTypeToSlam")) {
            scrambleKeys(c, b, wStart, wStart + qRound(0.2 * c.fps),
                         quint32(cut.value(QStringLiteral("seed"))
                                 .toInt(1)) + k);
        } else if (layout == QStringLiteral("knSeesaw")) {
            auto *ra = b->getTransformAnimator()->getRotAnimator();
            bakeSpan(ra, c, wStart, wEnd, [k](const qreal p) {
                return (k % 2 ? 1 : -1) * 6.0 * qSin(p * 6.2831853);
            });
        } else if (layout == QStringLiteral("knInertia")
                   || layout == QStringLiteral("knLaunch")) {
            auto *px = b->getTransformAnimator()->getPosAnimator()
                    ->getXAnimator();
            const qreal dx = c.cw * (k % 2 ? -0.12 : 0.12);
            bakeSpan(px, c, wStart, wStart + qRound(0.3 * c.fps),
                     [pos, dx](const qreal p) {
                return pos.x() + dx * (1 - p) * (1 - p);
            });
        } else if (layout == QStringLiteral("knZoomDive")
                   || layout == QStringLiteral("knDiveGlyph")) {
            auto *sc = b->getTransformAnimator()->getScaleAnimator();
            auto *sx = sc->getXAnimator();
            sx->saveValueToKey(wStart, 0.4);
            sx->saveValueToKey(wStart + qRound(0.25 * c.fps), 1.0);
            sc->getYAnimator()->saveValueToKey(wStart, 0.4);
            sc->getYAnimator()->saveValueToKey(
                        wStart + qRound(0.25 * c.fps), 1.0);
        } else if (layout == QStringLiteral("knStutterCut")
                   || layout == QStringLiteral("knRhythmCuts")) {
            // hard strobe: opacity flips on the koma grid
            auto *opa = opacityAnim(b);
            bakeSpan(opa, c, wStart, wEnd, [](const qreal p) {
                return (int(p * 24) % 2) ? 100.0 : 35.0;
            });
        }
        static const char *const slams[] = {
            "sharp-slam-front", "sharp-overshoot-down",
            "sharp-snap-word-rise", "sharp-punch-burst" };
        const QString preset = QString::fromUtf8(
                    slams[(k + int(cut.value(QStringLiteral("seed"))
                     .toInt())) % 4]);
        applyPreset(b, preset, wStart, c, 0.32, false);
        ct.boxes << b;
        ct.mainSize = qMax(ct.mainSize, size);
    }
    // per-key backdrops
    if (layout == QStringLiteral("knPadGrid")) {
        for (int gy = 0; gy < 2; gy++) {
            for (int gx = 0; gx < 2; gx++) {
                auto *pad = mkRect(group, QRectF(
                            c.cw * 0.3 - c.ch * 0.1 + gx * c.cw * 0.4,
                            c.ch * 0.3 - c.ch * 0.1 + gy * c.ch * 0.4,
                            c.ch * 0.2, c.ch * 0.2), QColor(),
                            c.ch * 0.03, c.ch * 0.03);
                pad->getStrokeSettings()->setCurrentColor(sub);
                pad->getStrokeSettings()->getStrokeWidthAnimator()
                        ->setCurrentBaseValue(2);
            }
        }
    } else if (layout == QStringLiteral("knPathRide")) {
        auto *orbitRing = mkCircle(group, QPointF(c.cw * 0.5, c.ch * 0.5),
                                   qMin(c.cw, c.ch) * 0.26, QColor());
        orbitRing->getStrokeSettings()->setCurrentColor(sub);
        orbitRing->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(1.5);
    } else if (layout == QStringLiteral("knGearWords")) {
        auto *gear = mkCircle(group, QPointF(c.cw * 0.36, c.ch * 0.5),
                              c.ch * 0.16, QColor());
        gear->getStrokeSettings()->setCurrentColor(accent);
        gear->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(2);
        auto *gear2 = mkCircle(group, QPointF(c.cw * 0.64, c.ch * 0.5),
                                c.ch * 0.11, QColor());
        gear2->getStrokeSettings()->setCurrentColor(sub);
        gear2->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(2);
    } else if (layout == QStringLiteral("knStripSlam")) {
        // 短冊スラム: vertical strips the words slam onto
        for (int k = 0; k < n; k++) {
            mkRect(group, QRectF(c.cw * 0.5 - c.cw * 0.3
                                 + k * c.cw * 0.6 / qMax(1, n - 1),
                                 c.ch * 0.2, c.cw * 0.6 / qMax(1, n - 1) - 4,
                                 c.ch * 0.6),
                   k % 2 ? sub : accent);
        }
    }
    // faint full-line ghost underneath keeps the reading order
    if (n > 1) {
        auto *ghost = mkText(group, text, lf.family, 400,
                             qBound(12.0, c.ch * 0.03, 30.0), sub);
        ghost->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.5, c.ch * 0.92));
        opacityAnim(ghost)->setCurrentBaseValue(70);
    }
    ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    return ct;
}

// ty* editorial family: typeset specimens — rules, cells, cropped
// giants, justified stretch
CutText typoEditorial(Ctx &c, ContainerBox * const group,
                      const QJsonObject &cut, const QString &text,
                      const LocalFont &lf, const QColor &textCol,
                      const QColor &accent, const QColor &sub) {
    CutText ct;
    const QString layout = cut.value(QStringLiteral("layout")).toString();
    const auto rule = [&](const qreal x, const qreal y, const qreal w,
                          const QColor &col, const qreal th = 2) {
        mkRect(group, QRectF(x, y, w, qMax(1.5, th)), col);
    };
    const auto tag = [&](const QString &s, const QPointF &pos,
                         const QColor &col) {
        auto *t = mkText(group, s,
                         QStringLiteral("Noto Sans Mono CJK JP"), 500,
                         qBound(11.0, c.ch * 0.022, 26.0), col);
        t->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        return t;
    };

    if (layout == QStringLiteral("tyKeySplit")) {
        // 大字挟み: a giant lead glyph sandwiching the rest of the line
        const qreal big = c.ch * 0.4;
        auto *lead = mkText(group, text.left(1), lf.family, 900, big,
                            accent);
        lead->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.24, c.ch * 0.46));
        ct.boxes << lead;
        const QString rest = text.mid(1);
        if (!rest.isEmpty()) {
            const qreal size = qMin(fitSize(rest, lf.family, 500,
                                            c.cw * 0.36), c.ch * 0.12);
            auto *r = mkText(group, rest, lf.family, 500, size, textCol);
            r->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.62, c.ch * 0.5));
            ct.boxes << r;
        }
        rule(c.cw * 0.42, c.ch * 0.78, c.cw * 0.3, accent);
        tag(QStringLiteral("KEY"), QPointF(c.cw * 0.24, c.ch * 0.78), sub);
        ct.anchor = QPointF(c.cw * 0.4, c.ch * 0.48);
        ct.mainSize = big * 0.4;
    } else if (layout == QStringLiteral("tyCropGiant")) {
        // 見切れ大文字: an oversized glyph bleeding off the frame edge
        const qreal size = c.ch * 0.9;
        auto *giant = mkText(group, text.right(1), lf.family, 900, size,
                             accent);
        giant->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.97, c.ch * 0.95));
        opacityAnim(giant)->setCurrentBaseValue(30);
        const qreal size2 = qMin(fitSize(text, lf.family, 700, c.cw * 0.62),
                                 c.ch * 0.15);
        const QPointF pos(c.cw * 0.5, c.ch * 0.36);
        auto *m = mkText(group, text, lf.family, 700, size2, textCol);
        m->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        ct.boxes << giant << m;
        rule(c.cw * 0.04, c.ch * 0.52, c.cw * 0.92, sub, 1.5);
        ct.mainSize = size2;
        ct.anchor = pos;
    } else if (layout == QStringLiteral("tyCross")) {
        // 十字組: a vertical and a horizontal arm crossing at the
        // emphasized glyph
        const int mid = qMax(1, text.length() / 2);
        const qreal big = c.ch * 0.2;
        auto *center = mkText(group, QString(text.at(mid - 1)), lf.family,
                              900, big * 1.6, accent);
        center->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.5, c.ch * 0.5));
        ct.boxes << center;
        const QString h = text.left(mid - 1);
        const QString v = text.mid(mid);
        if (!h.isEmpty()) {
            auto *b = mkText(group, h, lf.family, 700, big, textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5 - big * 0.8
                                - h.length() * big * 0.3, c.ch * 0.5));
            ct.boxes << b;
        }
        for (int k = 0; k < v.length() && k < 4; k++) {
            auto *b = mkText(group, QString(v.at(k)), lf.family, 700,
                             big * 0.8, textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5 + big * 1.0,
                                c.ch * 0.32 + big * 1.1 * k));
            ct.boxes << b;
        }
        rule(c.cw * 0.5 - big * 2.4, c.ch * 0.5, big * 4.8, sub, 1.5);
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        ct.mainSize = big;
    } else if (layout == QStringLiteral("tyBandHide")) {
        // 帯隠れ: an accent band crosses the middle of the line; the
        // glyphs peek from above and below it
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.8),
                                c.ch * 0.2);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        const int n = text.length();
        for (int k = 0; k < n; k++) {
            auto *b = mkText(group, QString(text.at(k)), lf.family, 900,
                             size, textCol);
            const bool up = k % 2 == 0;
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5 + (k - (n - 1) / 2.0) * size * 0.95,
                                pos.y() + (up ? -1 : 1) * size * 0.34));
            ct.boxes << b;
        }
        auto *band = mkRect(group, QRectF(0, c.ch * 0.5 - size * 0.28,
                                          c.cw, size * 0.56), accent);
        opacityAnim(band)->setCurrentBaseValue(96);
        ct.anchor = pos;
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tyScaleSteps")) {
        // 級数上げ: type sizes step up glyph by glyph
        const int n = qMax(1, text.length());
        const qreal base = c.ch * 0.07;
        qreal x = c.cw * 0.1;
        for (int k = 0; k < n; k++) {
            const qreal size = base * (1.0 + 0.32 * k);
            auto *b = mkText(group, QString(text.at(k)), lf.family,
                             700, size, k == n - 1 ? accent : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(x + size * 0.5, c.ch * 0.5));
            x += size * 0.92;
            ct.boxes << b;
        }
        rule(c.cw * 0.08, c.ch * 0.68, c.cw * 0.84, sub, 1.5);
        tag(QStringLiteral("STEP %1").arg(n),
            QPointF(c.cw * 0.9, c.ch * 0.86), sub);
        ct.anchor = QPointF(x / 2, c.ch * 0.5);
        ct.mainSize = base * (1.0 + 0.32 * (n - 1));
    } else if (layout == QStringLiteral("tyIndexTable")) {
        // 一覧表: an index table — header row, numbered cells
        const QStringList words = text.split(
                    QRegularExpression(QStringLiteral("[\\s、，,/]+")),
                    Qt::SkipEmptyParts);
        const int n = qMax(1, words.size());
        const int cols = qMin(3, n);
        const int rows = (n + cols - 1) / cols;
        const qreal cw = c.cw * 0.8 / cols;
        const qreal chh = c.ch * 0.5 / qMax(1, rows);
        const qreal size = qMin(cw, chh) * 0.5;
        tag(QStringLiteral("INDEX / %1").arg(n),
            QPointF(c.cw * 0.12, c.ch * 0.2), sub);
        for (int k = 0; k < n; k++) {
            const int col = k % cols, row = k / cols;
            const QRectF cell(c.cw * 0.1 + col * cw,
                              c.ch * 0.3 + row * chh, cw, chh);
            auto *frame = mkRect(group, cell.adjusted(1, 1, -1, -1),
                                 QColor());
            frame->getStrokeSettings()->setCurrentColor(sub);
            frame->getStrokeSettings()->getStrokeWidthAnimator()
                    ->setCurrentBaseValue(1);
            auto *b = mkText(group, words.at(k), lf.family, 700, size,
                             k == 0 ? accent : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        cell.center());
            ct.boxes << b;
            tag(QStringLiteral("%1").arg(k + 1, 2, 10, QLatin1Char('0')),
                QPointF(cell.left() + cw * 0.12, cell.top() + chh * 0.2),
                sub);
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tySplitType")) {
        // 断ち割り: the line split by a vertical rule, halves offset
        const qreal size = qMin(fitSize(text.left(text.length() / 2 + 1),
                                        lf.family, 900, c.cw * 0.38),
                                c.ch * 0.2);
        const int mid = text.length() / 2;
        auto *l = mkText(group, text.left(mid), lf.family, 900, size,
                         textCol);
        l->setTextHAlignment(Qt::AlignRight);
        l->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.44, c.ch * 0.44));
        auto *r = mkText(group, text.mid(mid), lf.family, 900, size,
                         accent);
        r->setTextHAlignment(Qt::AlignLeft);
        r->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.56, c.ch * 0.56));
        ct.boxes << l << r;
        mkRect(group, QRectF(c.cw * 0.5 - 2, c.ch * 0.16, 4,
                             c.ch * 0.68), accent);
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tyErode")) {
        // 削り反復: eroded repeats — offset ghosts with strike rules
        const qreal size = qMin(fitSize(text, lf.family, 700, c.cw * 0.6),
                                c.ch * 0.18);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        for (int k = 2; k >= 0; k--) {
            auto *b = mkText(group, text, lf.family, 700, size,
                             k == 0 ? textCol : sub);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        pos + QPointF(size * 0.05 * k, size * 0.07 * k));
            opacityAnim(b)->setCurrentBaseValue(k == 0 ? 100 : 55 - 15 * k);
            ct.boxes << b;
            if (k > 0) {
                rule(pos.x() - size * 2, pos.y() + size * (0.1 + 0.16 * k),
                     size * 4, accent, 3);
            }
        }
        ct.anchor = pos;
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tyStatCount")) {
        // 字数表示: the line plus a giant glyph-count numeral
        const qreal size = qMin(fitSize(text, lf.family, 700, c.cw * 0.56),
                                c.ch * 0.16);
        const QPointF pos(c.cw * 0.5, c.ch * 0.4);
        auto *m = mkText(group, text, lf.family, 700, size, textCol);
        m->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        ct.boxes << m;
        const int n = text.length();
        auto *num = mkText(group, QString::number(n),
                           QStringLiteral("Noto Sans Mono CJK JP"), 900,
                           c.ch * 0.2, accent);
        num->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.82, c.ch * 0.75));
        tag(QStringLiteral("CHARS"), QPointF(c.cw * 0.82, c.ch * 0.86),
            sub);
        rule(c.cw * 0.08, c.ch * 0.58, c.cw * 0.5, sub, 1.5);
        ct.anchor = pos;
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tyMargin")) {
        // 余白: generous whitespace — small line, page number, one rule
        const qreal size = qMin(fitSize(text, lf.family, 400, c.cw * 0.3),
                                c.ch * 0.08);
        auto *m = mkText(group, text, lf.family, 400, size, textCol);
        m->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.32, c.ch * 0.4));
        m->setTextHAlignment(Qt::AlignLeft);
        ct.boxes << m;
        rule(c.cw * 0.1, c.ch * 0.5, c.cw * 0.2, accent, 1.5);
        tag(QStringLiteral("P.%1").arg(
                qRound(cut.value(QStringLiteral("start")).toDouble()) + 1),
            QPointF(c.cw * 0.1, c.ch * 0.9), sub);
        ct.anchor = QPointF(c.cw * 0.32, c.ch * 0.4);
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tyRotBlock")) {
        // 回転ブロック: the block typeset vertically (rotated 90°)
        const qreal size = qMin(fitSize(text, lf.family, 900, c.ch * 0.6),
                                c.ch * 0.16);
        auto *b = mkText(group, text, lf.family, 900, size, textCol);
        b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.7, c.ch * 0.5));
        b->getTransformAnimator()->getRotAnimator()
                ->setCurrentBaseValue(90);
        ct.boxes << b;
        auto *mark = mkText(group, text.left(2), lf.family, 900,
                            size * 1.4, accent);
        mark->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.3, c.ch * 0.5));
        ct.boxes << mark;
        rule(c.cw * 0.5, c.ch * 0.12, 2, sub, 1);
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tySquare")
               || layout == QStringLiteral("tyGridCells")) {
        // 方形組/升目送り: connected square cells, one glyph each
        const int n = qMax(1, text.length());
        const int cols = layout == QStringLiteral("tyGridCells")
                ? qMin(3, n) : n;
        const int rows = (n + cols - 1) / cols;
        const qreal cell = qMin(c.cw * 0.72 / cols, c.ch * 0.5 / rows);
        const qreal size = cell * 0.62;
        for (int k = 0; k < n; k++) {
            const int col = k % cols, row = k / cols;
            const QPointF p(c.cw * 0.5
                            + (col - (cols - 1) / 2.0) * cell,
                            c.ch * 0.5 + (row - (rows - 1) / 2.0) * cell);
            auto *frame = mkRect(group, QRectF(
                        p.x() - cell / 2, p.y() - cell / 2, cell, cell),
                        k % 2 ? QColor() : sub);
            if (k % 2) {
                frame->getStrokeSettings()->setCurrentColor(sub);
                frame->getStrokeSettings()->getStrokeWidthAnimator()
                        ->setCurrentBaseValue(1.5);
            }
            auto *b = mkText(group, QString(text.at(k)), lf.family, 900,
                             size, k % 3 == 0 ? accent : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(p);
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        ct.mainSize = size;
    } else if (layout == QStringLiteral("tyLineFocus")) {
        // 行中強調: a modest line with one word blown up in accent
        const QStringList words = text.split(
                    QRegularExpression(QStringLiteral("[\\s、，,/]+")),
                    Qt::SkipEmptyParts);
        const int focus = words.size() > 1 ? QRandomGenerator(
                    quint32(cut.value(QStringLiteral("seed")).toInt(1)))
                .bounded(words.size()) : 0;
        const qreal big = qMin(fitSize(words.value(focus), lf.family,
                                       900, c.cw * 0.4), c.ch * 0.24);
        const qreal small = big * 0.42;
        qreal x = c.cw * 0.14;
        for (int k = 0; k < words.size(); k++) {
            const bool hot = k == focus;
            const qreal size = hot ? big : small;
            auto *b = mkText(group, words.at(k), lf.family,
                             hot ? 900 : 500, size,
                             hot ? accent : textCol);
            b->setTextHAlignment(Qt::AlignLeft);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(x + size * 0.3, hot ? c.ch * 0.5 : c.ch * 0.66));
            x += size * 0.62 * words.at(k).length() + big * 0.12;
            ct.boxes << b;
        }
        rule(c.cw * 0.1, c.ch * 0.62, c.cw * 0.8, sub, 1.5);
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.55);
        ct.mainSize = big;
    } else if (layout == QStringLiteral("tyRuleWipe")) {
        // 署線ワイプ: a thick rule the text rides on, shifted off-axis
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.7),
                                c.ch * 0.2);
        const QPointF pos(c.cw * 0.55, c.ch * 0.42);
        auto *m = mkText(group, text, lf.family, 900, size, textCol);
        m->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        ct.boxes << m;
        rule(c.cw * 0.04, pos.y() + size * 0.62, c.cw * 0.92, accent,
             qMax(3.0, size * 0.07));
        tag(QStringLiteral("— %1").arg(
                text.left(qMin(4, text.length()))),
            QPointF(c.cw * 0.08, c.ch * 0.82), sub);
        ct.anchor = pos;
        ct.mainSize = size;
    } else {
        // remaining ty* keys: ruled editorial block with a corner tag
        const qreal size = qMin(fitSize(text, lf.family, 700, c.cw * 0.66),
                                c.ch * 0.17);
        const QPointF pos(c.cw * 0.5, c.ch * 0.48);
        auto *m = mkText(group, text, lf.family, 700, size, textCol);
        m->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        ct.boxes << m;
        rule(c.cw * 0.08, pos.y() - size * 1.1, c.cw * 0.3, accent);
        rule(c.cw * 0.08, pos.y() + size * 1.05, c.cw * 0.84, sub, 1.5);
        tag(QStringLiteral("TYPO/%1").arg(layout.right(4).toUpper()),
            QPointF(c.cw * 0.92, c.ch * 0.1), sub);
        ct.anchor = pos;
        ct.mainSize = size;
    }
    return ct;
}

// ======================================================================
// render-replay materialization: run the vendored renderer per cut,
// record every canvas primitive at the cut's midpoint frame, and lay
// them down as editable friction layers — the web composition (bg,
// decor, text styling) without hand-porting 860 renderers
// ======================================================================

class MatSession {
public:
    explicit MatSession(const qreal canvasW, const qreal canvasH) {
        // empirical guard: the 11th consecutive QJSEngine session in a
        // process crashes inside QV4/drawImage regardless of object
        // lifetimes (8 verified green, 11th deterministic SEGV). Cap
        // the session count and fall back to the hand recipes —
        // restarting the app resets it
        static QAtomicInt sessionCount = 0;
        if (sessionCount.fetchAndAddRelaxed(1) >= 10) {
            mErr = QStringLiteral("物化会话超限(重启恢复)");
            return;
        }
        mFactory = new LyricCanvasFactory(&mEngine);
        mFactory->install(&mEngine);
        QFile stub(QStringLiteral(":/jizura/jizura-stub.js"));
        stub.open(QIODevice::ReadOnly);
        if (!stub.isOpen()) { mErr = QStringLiteral("stub missing"); return; }
        const auto stubR = mEngine.evaluate(
                    QString::fromUtf8(stub.readAll()),
                    QStringLiteral("jizura-stub.js"));
        if (stubR.isError()) { mErr = stubR.toString(); return; }
        QStringList files;
        QDirIterator it(QStringLiteral(":/jizura"),
                        {QStringLiteral("*.js")}, QDir::Files);
        while (it.hasNext()) { files << it.next(); }
        files.sort();
        files.removeAll(QStringLiteral(":/jizura/jizura-stub.js"));
        for (const QString &file : files) {
            QFile f(file);
            f.open(QIODevice::ReadOnly);
            const auto r = mEngine.evaluate(
                        QString::fromUtf8(f.readAll()), file);
            if (r.isError()) {
                mErr = QStringLiteral("%1: %2").arg(
                            QFileInfo(file).fileName(), r.toString());
                return;
            }
        }
        const auto drv = mEngine.evaluate(LyricCanvasDriver::source(),
                                          QStringLiteral("driver"));
        if (drv.isError()) { mErr = QStringLiteral("driver: %1")
                    .arg(drv.toString()); return; }
        mEngine.evaluate(QStringLiteral("__jzInit(%1, %2)")
                         .arg(qRound(canvasW)).arg(qRound(canvasH)));
        // the recording canvas is the driver's st.canvas
        const auto canvasObj = mEngine.globalObject()
                .property(QStringLiteral("__jz"))
                .property(QStringLiteral("canvas"));
        mCanvas = qobject_cast<JsCanvas2D*>(canvasObj.toQObject());
        if (!mCanvas) { mErr = QStringLiteral("no recording canvas"); }
    }

    bool ok() const { return mErr.isEmpty() && mCanvas; }
    const QString &error() const { return mErr; }
    QJSEngine &engine() { return mEngine; }
    JsCanvas2D *canvas() const { return mCanvas; }

private:
    QJSEngine mEngine;
    LyricCanvasFactory *mFactory = nullptr;
    JsCanvas2D *mCanvas = nullptr;
    QString mErr;
};

// QPainterPath → SkPath (move/line/cubic/close subset — the JIZURA
// renderer emits no quads)
SkPath recToSkPath(const QPainterPath &pp) {
    SkPath sk;
    for (int i = 0; i < pp.elementCount(); i++) {
        const QPainterPath::Element &e = pp.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            sk.moveTo(SkPoint::Make(e.x, e.y)); break;
        case QPainterPath::LineToElement:
            sk.lineTo(SkPoint::Make(e.x, e.y)); break;
        case QPainterPath::CurveToElement: {
            const auto &c1 = e;
            const auto &c2 = pp.elementAt(i + 1);
            const auto &p = pp.elementAt(i + 2);
            sk.cubicTo(SkPoint::Make(c1.x, c1.y),
                       SkPoint::Make(c2.x, c2.y),
                       SkPoint::Make(p.x, p.y));
            i += 2;
            break;
        }
        default: break;
        }
    }
    if (pp.elementCount() > 1) {
        const auto &first = pp.elementAt(0);
        const auto &last = pp.elementAt(pp.elementCount() - 1);
        if (qFuzzyCompare(first.x, last.x) && qFuzzyCompare(first.y, last.y)) {
            sk.close();
        }
    }
    return sk;
}

struct MatResult {
    CutText ct;      // reuse: boxes feed the anim pass
    int items = 0;
};

// split materialized texts into animated (lyric-scale) vs static
// (decorative smalls) — see the note in materializeCut
void splitMatAnimated(CutText &ct) {
    const qreal big = ct.mainSize * 0.55;
    ct.boxes.clear();
    for (TextBox *b : ct.allTexts) {
        if (b->getFontSize() >= big) { ct.boxes << b; }
    }
}

// zero-engine per-glyph motion for materialized cuts. The web look
// animates every glyph, but the preset expressions each own a
// QJSEngine (hundreds per scene OOM the machine — see 1e36e8d2f), so
// the lyric-scale glyphs get staggered baked keyframes instead: an
// enter/exit sweep per glyph family that reads as character
// animation at zero runtime cost. Returns false when nothing was
// baked (hard cut both ways, or no lyric-scale glyphs) so the caller
// can fall back to the group-level fade.
bool bakeMatTextAnims(Ctx &c, const QList<TextBox*> &texts,
                      const QJsonObject &cut,
                      const int fStart, const int fEnd,
                      const qreal inDur, const qreal outDur) {
    if (texts.isEmpty()) { return false; }
    const QString enterKey = cut.value(QStringLiteral("enter")).toString();
    const QString exitKey = cut.value(QStringLiteral("exit")).toString();
    const bool hardIn = enterKey == QStringLiteral("cut")
            || enterKey.isEmpty();
    const bool hardOut = exitKey == QStringLiteral("cut")
            || exitKey.isEmpty();
    if (hardIn && hardOut) { return false; }

    const auto hasAny = [](const QString &key,
                           std::initializer_list<const char*> keys) {
        for (const char *k : keys) {
            if (key == QLatin1String(k)) { return true; }
        }
        return false;
    };
    const bool inPop = hasAny(enterKey, {"pop", "bounce", "stamp",
                                         "squashDrop", "rubber",
                                         "magnet", "domino"});
    const bool inDrop = hasAny(enterKey, {"drop", "fall", "unroll",
                                           "fold"});
    const bool inSlideL = enterKey == QLatin1String("slideL");
    const bool inSlideR = enterKey == QLatin1String("slideR");
    const bool inZoom = hasAny(enterKey, {"zoom", "stretch", "blinds",
                                           "iris", "wipe"});
    const bool inSpin = hasAny(enterKey, {"spin", "flipX", "flipY",
                                           "spiralIn", "whip"});
    const bool outFall = hasAny(exitKey, {"fall", "drift"});
    const bool outZoom = hasAny(exitKey, {"explode", "scatter",
                                           "shrink", "stretch",
                                           "zoom"});

    const int n = texts.size();
    const int inSpan = qMax(4, qRound(inDur * c.fps));
    const int outSpan = qMax(4, qRound(outDur * c.fps));
    const int stagIn = qMax(1, inSpan / (n + 2));
    const int stagOut = qMax(1, outSpan / (n + 2));

    const auto easeOut = [](const qreal p) {
        return 1.0 - std::pow(1.0 - p, 3);
    };
    const auto easeBack = [](const qreal p) {
        const qreal c1 = 1.70158;
        const qreal c3 = c1 + 1.0;
        const qreal x = p - 1.0;
        return 1.0 + c3 * x * x * x + c1 * x * x;
    };

    for (int i = 0; i < n; i++) {
        TextBox *b = texts.at(i);
        const auto tr = b->getTransformAnimator();
        QrealAnimator * const opa = opacityAnim(b);
        const QPointF pos0 = tr->getPosAnimator()->getBaseValue();
        const QPointF scl0 = tr->getScaleAnimator()->getBaseValue();
        const qreal rot0 = tr->getRotAnimator()->getCurrentBaseValue();
        const qreal alpha0 = opa->getCurrentBaseValue();

        if (!hardIn) {
            const int fi0 = qMin(fEnd - 1, fStart + i * stagIn);
            const int fi1 = qMin(fEnd - 1, fi0 + inSpan);
            const QPointF posFrom = pos0 + QPointF(
                        inSlideL ? c.cw * 0.07
                        : inSlideR ? -c.cw * 0.07 : 0,
                        inDrop ? -c.ch * 0.12 : 0);
            const QPointF sclFrom = inPop ? QPointF(0.3, 0.3)
                                 : inZoom ? QPointF(1.7, 1.7) : scl0;
            const qreal rotFrom = inSpin ? rot0 - 100 : rot0;
            // pre-hold the start pose from the group's first visible
            // frame: without it the glyph sits at its final spot
            // until its stagger turn arrives, then jumps back to
            // slide in
            const int fPre = qMax(0, fStart - 1);
            tr->getPosAnimator()->getXAnimator()->saveValueToKey(
                        fPre, posFrom.x());
            tr->getPosAnimator()->getYAnimator()->saveValueToKey(
                        fPre, posFrom.y());
            tr->getScaleAnimator()->getXAnimator()->saveValueToKey(
                        fPre, sclFrom.x());
            tr->getScaleAnimator()->getYAnimator()->saveValueToKey(
                        fPre, sclFrom.y());
            tr->getRotAnimator()->saveValueToKey(fPre, rotFrom);
            opa->saveValueToKey(fPre, 0.0);
            if (fi1 > fi0) {
                bakeSpanXY(tr->getPosAnimator(), c, fi0, fi1,
                           [&](const qreal p) -> QPointF {
                    const qreal e = easeOut(p);
                    return QPointF(pos0.x() + (posFrom.x() - pos0.x()) * (1 - e),
                                   pos0.y() + (posFrom.y() - pos0.y()) * (1 - e));
                });
                if (inPop || inZoom) {
                    bakeSpanXY(tr->getScaleAnimator(), c, fi0, fi1,
                               [&](const qreal p) -> QPointF {
                        const qreal e = inPop ? easeBack(p) : easeOut(p);
                        return QPointF(sclFrom.x() + (scl0.x() - sclFrom.x()) * e,
                                       sclFrom.y() + (scl0.y() - sclFrom.y()) * e);
                    });
                }
                if (inSpin) {
                    bakeSpan(tr->getRotAnimator(), c, fi0, fi1,
                             [&](const qreal p) {
                        return rotFrom + (rot0 - rotFrom) * easeOut(p);
                    });
                }
                bakeSpan(opa, c, fi0, fi1, [&](const qreal p) {
                    return alpha0 * easeOut(p);
                });
            }
        }
        if (!hardOut) {
            const int fo1 = qMax(fStart + 1, fEnd - (n - 1 - i) * stagOut);
            const int fo0 = qMax(fStart + 1, fo1 - outSpan);
            const QPointF posTo = pos0 + QPointF(
                        0, outFall ? c.ch * 0.08 : 0);
            const QPointF sclTo = outZoom
                    ? QPointF(scl0.x() * 1.25, scl0.y() * 1.25) : scl0;
            if (fo1 > fo0) {
                bakeSpanXY(tr->getPosAnimator(), c, fo0, fo1,
                           [&](const qreal p) -> QPointF {
                    const qreal e = easeOut(p);
                    return QPointF(pos0.x() + (posTo.x() - pos0.x()) * e,
                                   pos0.y() + (posTo.y() - pos0.y()) * e);
                });
                if (outZoom) {
                    bakeSpanXY(tr->getScaleAnimator(), c, fo0, fo1,
                               [&](const qreal p) -> QPointF {
                        const qreal e = easeOut(p);
                        return QPointF(scl0.x() + (sclTo.x() - scl0.x()) * e,
                                       scl0.y() + (sclTo.y() - scl0.y()) * e);
                    });
                }
                bakeSpan(opa, c, fo0, fo1, [&](const qreal p) {
                    return alpha0 * (1.0 - easeOut(p));
                });
            }
        }
    }
    return true;
}

MatResult materializeCut(Ctx &c, ContainerBox * const group,
                         const QVector<LyricDrawRec> &recs) {
    MatResult out;
    const QFontDatabase fdb;
    int dropped = 0;
    for (const LyricDrawRec &rec : recs) {
        if (out.items > 260) { dropped++; continue; }
        if (rec.alpha < 0.04) { continue; }
        const QColor fill = rec.hasFill ? rec.fillColor : QColor();
        if (rec.kind == LyricDrawRec::Kind::Text) {
            const QString family =
                    fdb.families().contains(rec.family)
                    ? rec.family : QStringLiteral("Noto Sans CJK JP");
            const qreal sizePx = qMax(6.0, rec.pointSize);
            auto *b = mkText(group, rec.text, family,
                             qBound(100, rec.weight, 900), sizePx,
                             fill.isValid() ? fill : Qt::white);
            // recorded pos is the glyph-run baseline-left in world
            // space; friction text is center-aligned → shift to the
            // visual center of the recorded run
            const QPointF center(rec.pos.x() + rec.rect.width() * 0.5,
                                 rec.pos.y() - sizePx * 0.36);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        center);
            if (qAbs(rec.stretchX - 1.0) > 0.03) {
                b->getTransformAnimator()->getScaleAnimator()
                        ->setBaseValue(QPointF(rec.stretchX, 1.0));
            }
            if (qAbs(rec.rotation) > 0.2) {
                b->getTransformAnimator()->getRotAnimator()
                        ->setCurrentBaseValue(rec.rotation);
            }
            if (rec.hasStroke) {
                setStroke(b, rec.strokeColor, qMax(0.8, rec.strokeWidth));
                if (!rec.hasFill) {
                    b->getFillSettings()->setPaintType(PaintType::NOPAINT);
                }
            }
            opacityAnim(b)->setCurrentBaseValue(qBound(0.0, rec.alpha, 1.0)
                                                * 100);
            // animation targets are lyric-scale glyphs only: every
            // animated preset spawns its own QJSEngine expressions, and
            // a materialized cut carries dozens of small decorative
            // texts that must stay static or the scene OOMs
            out.ct.allTexts << b;
            out.ct.mainSize = qMax(out.ct.mainSize, sizePx);
        } else if (rec.kind == LyricDrawRec::Kind::Rect) {
            if (rec.rect.width() < 1 || rec.rect.height() < 1
                    && !rec.hasStroke) { continue; }
            auto *b = mkRect(group, rec.rect,
                             rec.hasFill ? fill : QColor());
            if (rec.hasStroke) {
                b->getStrokeSettings()->setCurrentColor(rec.strokeColor);
                b->getStrokeSettings()->getStrokeWidthAnimator()
                        ->setCurrentBaseValue(qMax(0.8, rec.strokeWidth));
            }
            opacityAnim(b)->setCurrentBaseValue(qBound(0.0, rec.alpha, 1.0)
                                                * 100);
        } else {
            if (rec.path.isEmpty()) { continue; }
            const auto box = enve::make_shared<SmartVectorPath>();
            group->addContained(box);
            box->prp_setName(QStringLiteral("形状"));
            box->loadSkPath(recToSkPath(rec.path));
            if (rec.hasFill) {
                box->getFillSettings()->setPaintType(PaintType::FLATPAINT);
                box->getFillSettings()->setCurrentColor(fill);
            } else {
                box->getFillSettings()->setPaintType(PaintType::NOPAINT);
            }
            if (rec.hasStroke) {
                auto *st = box->getStrokeSettings();
                st->setPaintType(PaintType::FLATPAINT);
                st->setCurrentColor(rec.strokeColor);
                st->getStrokeWidthAnimator()->setCurrentBaseValue(
                            qMax(0.8, rec.strokeWidth));
            }
            opacityAnim(box.get())->setCurrentBaseValue(
                        qBound(0.0, rec.alpha, 1.0) * 100);
        }
        out.items++;
    }
    if (dropped > 0) {
        c.res->notes << QStringLiteral("物化裁剪 %1 项(超上限)").arg(dropped);
    }
    return out;
}


QJsonObject cutParams(const QJsonObject &cut) {
    return cut.value(QStringLiteral("params")).toObject();
}

CutText buildLayout(Ctx &c, ContainerBox * const group,
                    const QJsonObject &cut) {
    const QString layout = cut.value(QStringLiteral("layout")).toString();
    const QString text = cut.value(QStringLiteral("text")).toString();
    const QString lineText = cut.value(QStringLiteral("lineText")).toString();
    const QString note = cut.value(QStringLiteral("note")).toString();
    const bool emph = cut.value(QStringLiteral("emph")).toDouble() > 0;
    const QJsonObject params = cutParams(cut);
    const int schemeIdx = cut.value(QStringLiteral("scheme")).toInt();
    const auto scheme = c.schemes.at(
                ((schemeIdx % c.schemes.size()) + c.schemes.size())
                % c.schemes.size()).toObject();
    const QColor fg = parseColor(scheme.value(QStringLiteral("fg")), Qt::white);
    const QColor accent = parseColor(scheme.value(QStringLiteral("accent")),
                                     QColor(255, 90, 90));
    const QColor sub = parseColor(scheme.value(QStringLiteral("sub")),
                                  QColor(200, 200, 200));
    const QColor textCol = emph ? accent : fg;
    const bool portrait = c.ch > c.cw;

    LocalFont lf{QStringLiteral("Noto Sans CJK JP"), 700};
    const QString fontKey = params.value(QStringLiteral("font")).toString();
    if (!fontKey.isEmpty() && c.fonts.contains(fontKey)) {
        lf = localFont(c.fonts.value(fontKey).toObject());
    }

    CutText ct;
    const auto addNote = [&](const QPointF &below) {
        const QString subText = !note.isEmpty() ? note
                : (params.value(QStringLiteral("sub")).toBool()
                   && lineText != text ? lineText : QString());
        if (subText.isEmpty()) { return; }
        const qreal ns = qBound(14.0, c.ch * 0.028, 34.0);
        auto *b = mkText(group, subText, QStringLiteral("Noto Sans CJK JP"),
                         400, ns, sub);
        b->getTransformAnimator()->getPosAnimator()->setBaseValue(below);
    };

    const auto mkMain = [&](const QString &t, const qreal size,
                            const QPointF &pos) {
        auto *b = mkText(group, t, lf.family, lf.weight, size, textCol);
        b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        ct.boxes << b;
        ct.mainSize = qMax(ct.mainSize, size);
        return b;
    };

    const auto unknown = [&]() {
        c.res->substitutions++;
        c.res->notes << QStringLiteral("布局 %1 → 中央（近似）").arg(layout);
    };

    const auto has = [&layout](const char *s) {
        return layout == QLatin1String(s);
    };

    if (has("center") || layout.isEmpty()) {
        const QString wrapped = splitLines(text, portrait ? 7 : 12);
        const qreal size = qMin(
                    fitSize(wrapped, lf.family, lf.weight, c.cw * 0.82),
                    c.ch * 0.32) * (emph ? 1.12 : 1.0);
        const QPointF pos(c.cw * 0.5 + params.value(QStringLiteral("ox"))
                          .toDouble() * c.cw,
                          c.ch * 0.5 + params.value(QStringLiteral("oy"))
                          .toDouble() * c.ch);
        auto *b = mkMain(wrapped, size, pos);
        ct.anchor = pos;
        if (params.value(QStringLiteral("under")).toBool()) {
            const qreal w = c.cw * 0.3, y = pos.y() + size * 0.62;
            mkRect(group, QRectF(pos.x() - w/2, y, w, qMax(2.0, size*0.03)),
                   accent);
        }
        addNote(QPointF(pos.x(), pos.y() + size * 1.05));
    } else if (has("huge")) {
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.94),
                                c.ch * 0.55);
        auto *b = mkMain(text, size, QPointF(c.cw * 0.5, c.ch * 0.5));
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        if (params.value(QStringLiteral("label")).toBool()) {
            const qreal ls = qBound(18.0, c.ch * 0.035, 44.0);
            auto *l = mkText(group, lineText.left(18),
                             QStringLiteral("Noto Sans CJK JP"), 500, ls, sub);
            l->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.12, c.ch * 0.14));
        }
    } else if (has("title")) {
        const qreal size = qMin(fitSize(text, lf.family, lf.weight,
                                        c.cw * 0.7), c.ch * 0.2);
        const QPointF pos(c.cw * 0.5, c.ch * 0.45);
        mkMain(text, size, pos);
        ct.anchor = pos;
        addNote(QPointF(c.cw * 0.5, c.ch * 0.45 + size * 0.9));
    } else if (has("vcols")) {
        // vertical columns: one text box per column, chars stacked
        QStringList phrases = text.split(QRegularExpression(QStringLiteral(
                    "[/・、\\s]+")), Qt::SkipEmptyParts);
        if (phrases.isEmpty()) { phrases << text; }
        const int nCols = qBound(1, phrases.size(), 4);
        const qreal colW = qMin(c.cw * 0.16, c.ch * 0.6 / qMax(4, text.length()/nCols));
        for (int k = 0; k < nCols; k++) {
            QString col;
            for (const QChar &ch : phrases.at(k % phrases.size())) {
                col += ch; col += QLatin1Char('\n');
            }
            const qreal size = qMin(fitSize(col, lf.family, lf.weight,
                                            c.ch * 0.7), c.ch * 0.11);
            auto *b = mkMain(col, size, QPointF(
                                 c.cw * 0.5 + (k - (nCols - 1) / 2.0) * colW * 1.4,
                                 c.ch * 0.5));
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("marquee")) {
        // faithful port of 06_layouts.js marquee: centered main line
        // plus 2/4 scrolling strips above/below (outline | dim | box)
        const int rows = params.value(QStringLiteral("rows")).toInt(2);
        const QString rowStyle = params.value(QStringLiteral("rowStyle"))
                .toString(QStringLiteral("dim"));
        const qreal speed = params.value(QStringLiteral("speed"))
                .toDouble(0.8);
        const qreal sx = params.value(QStringLiteral("sx")).toDouble(1.25);
        const int f0 = fSec(c, cut.value(QStringLiteral("start")).toDouble());
        const int f1 = qMax(f0 + 2, fSec(c,
                       cut.value(QStringLiteral("end")).toDouble()));
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.84),
                                c.ch * 0.3);
        const qreal rs = size * 0.42;
        const QString unit = text + QStringLiteral("\u3000");
        const qreal period = rs * 0.62 * unit.length() + rs * 0.05;
        const int reps = int(c.cw * 2.4 / period) + 2;
        const QString strip = unit.repeated(qMax(2, reps));
        // strip rows: ±1 (2 rows) or ±1/±2 (4 rows), alternating dir
        const QList<int> ys = rows == 2 ? QList<int>{-1, 1}
                                        : QList<int>{-2, -1, 1, 2};
        for (int r = 0; r < ys.size(); r++) {
            const int k = ys.at(r);
            const qreal y = c.ch * 0.5
                    + (k > 0 ? 1 : -1) * (size * 0.5 + rs * 0.95)
                    + (qAbs(k) - 1) * (k > 0 ? 1 : -1) * rs * 1.25;
            const qreal dir = r % 2 ? 1 : -1;
            auto *b = mkText(group, strip, lf.family, lf.weight, rs,
                             textCol);
            b->getTransformAnimator()->getScaleAnimator()
                    ->setBaseValue(QPointF(sx, 1.0));
            b->getTransformAnimator()->getPosAnimator()->getYAnimator()
                    ->setCurrentBaseValue(y);
            auto *opa = opacityAnim(b);
            if (rowStyle == QStringLiteral("outline")) {
                b->getFillSettings()->setPaintType(PaintType::NOPAINT);
                setStroke(b, textCol, qMax(1.2, rs * 0.02));
                opa->setCurrentBaseValue(88);
            } else if (rowStyle == QStringLiteral("dim")) {
                b->getFillSettings()->setCurrentColor(sub);
                opa->setCurrentBaseValue(36);
            } else { // box: ink band behind, text in bg color
                auto *band = mkRect(group, QRectF(-10, y - rs * 0.62,
                                                  c.cw + 20, rs * 1.24),
                                    parseColor(scheme.value(
                                        QStringLiteral("ink")), Qt::black));
                Q_UNUSED(band);
                b->getFillSettings()->setCurrentColor(parseColor(
                            scheme.value(QStringLiteral("bg")), Qt::white));
            }
            // scroll: off = (lb*speed*W*0.22*dir + r*period*0.37) mod period
            auto *px = b->getTransformAnimator()->getPosAnimator()
                    ->getXAnimator();
            bakeSpan(px, c, f0, f1, [&](const qreal p) {
                const qreal lb = p * (f1 - f0) / c.fps;
                const qreal off = std::fmod(std::fmod(
                            lb * speed * c.cw * 0.22 * dir
                            + r * period * 0.37, period) + period, period)
                        - period / 2;
                return c.cw * 0.5 + off;
            });
        }
        auto *main = mkText(group, text, lf.family, 900, size, textCol);
        main->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.5, c.ch * 0.5));
        main->getTransformAnimator()->getScaleAnimator()
                ->setBaseValue(QPointF(sx, 1.0));
        ct.boxes << main;
        ct.mainSize = size;
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("tile")) {
        // faithful port of 06_layouts.js tile: a wall of small strips
        // scrolling row by row behind the (boxed / hollow) main line
        const int rowsN = params.value(QStringLiteral("rowsN")).toInt(14);
        const QString knock = params.value(QStringLiteral("knock"))
                .toString(QStringLiteral("box"));
        const QString unitText =
                params.value(QStringLiteral("unit")).toString()
                == QStringLiteral("line")
                ? lineText + QStringLiteral("\u3000")
                : text + QStringLiteral("\u3000");
        const int f0 = fSec(c, cut.value(QStringLiteral("start")).toDouble());
        const qreal inDur = cut.value(QStringLiteral("inDur"))
                .toDouble(0.3);
        const qreal rowH = c.ch / rowsN;
        const qreal ts = rowH * 0.72;
        const qreal period = ts * 0.62 * unitText.length() + 2;
        const int reps = int(c.cw * 1.6 / period) + 2;
        QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                    .toInt(1)) ^ 0x711E);
        for (int r = 0; r <= rowsN; r++) {
            // per-row staggered entrance, then a slow alternating drift
            const qreal ap = rng.generateDouble() * inDur * 1.6;
            auto *b = mkText(group, unitText.repeated(qMax(2, reps)),
                             lf.family, 400, ts, sub);
            b->setTextHAlignment(Qt::AlignLeft);
            const qreal dir = r % 2 ? 1 : -1;
            const qreal y = (r + 0.5) * rowH;
            b->getTransformAnimator()->getPosAnimator()->getYAnimator()
                    ->setCurrentBaseValue(y);
            auto *opa = opacityAnim(b);
            opa->setCurrentBaseValue(42);
            const int fIn = f0 + qRound(ap * c.fps);
            opa->saveValueToKey(qMax(f0 - 1, fIn - 1), 0);
            opa->saveValueToKey(fIn + qRound(0.1 * c.fps), 42);
            auto *px = b->getTransformAnimator()->getPosAnimator()
                    ->getXAnimator();
            const int f1 = qMax(f0 + 2, fSec(c, cut.value(
                        QStringLiteral("end")).toDouble()));
            bakeSpan(px, c, f0, f1, [&](const qreal p) {
                const qreal lb = p * (f1 - f0) / c.fps;
                const qreal off = std::fmod(std::fmod(
                            (r % 2) * period * 0.5 + lb * 26 * dir, period)
                        + period, period);
                return -period + off - period * 0.5;
            });
        }
        const QString mt = portrait ? splitLines(text, 5) : text;
        const qreal size = qMin(fitSize(mt, lf.family, 900, c.cw * 0.8),
                                c.ch * 0.3);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        if (knock == QStringLiteral("box")) {
            // bg-colored plate expanding behind the main line
            const qreal w = size * 0.62 * text.length() + size * 0.7;
            const qreal h = size * 1.12 * (mt.count(QLatin1Char('\n')) + 1)
                    + size * 0.56;
            auto *plate = mkRect(group, QRectF(pos.x() - w / 2,
                                               pos.y() - h / 2, w, h),
                                 parseColor(scheme.value(
                                     QStringLiteral("bg")), Qt::black));
            auto *pw = plate->getTransformAnimator()->getScaleAnimator()
                    ->getXAnimator();
            const int fe = f0 + qRound(qBound(0.22, inDur * 1.4, 0.48)
                                       * c.fps);
            pw->saveValueToKey(f0, 0.05);
            pw->saveValueToKey(fe, 1.0);
            plate->getTransformAnimator()->getScaleAnimator()
                    ->getYAnimator()->setCurrentBaseValue(1.0);
            plate->getBoxTransformAnimator()->getPivotAnimator()
                    ->setBaseValue(QPointF(w / 2, h / 2));
        } else {
            // hollow echo of the main line in bg color under it
            auto *hollow = mkText(group, mt, lf.family, 900, size, QColor());
            hollow->getFillSettings()->setPaintType(PaintType::NOPAINT);
            setStroke(hollow, parseColor(scheme.value(
                           QStringLiteral("bg")), Qt::black), size * 0.16);
            hollow->getTransformAnimator()->getPosAnimator()
                    ->setBaseValue(pos);
        }
        auto *main = mkText(group, mt, lf.family, 900, size, textCol);
        main->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
        ct.boxes << main;
        ct.mainSize = size;
        ct.anchor = pos;
    } else if (has("condensed")) {
        // faithful port: the line repeated in N tall-narrow columns
        // (sx 0.42-0.58, sy 1.1-1.3)
        const int count = params.value(QStringLiteral("count")).toInt(1);
        const qreal sx = params.value(QStringLiteral("sx")).toDouble(0.5);
        const qreal sy = params.value(QStringLiteral("sy")).toDouble(1.2);
        const QString flat = text;
        const qreal slot = c.cw * 0.92 / count;
        const qreal size = qMin(fitSize(flat, lf.family, 900,
                                        slot * 0.94) / sx, c.ch * 0.62);
        for (int i = 0; i < count; i++) {
            auto *b = mkText(group, flat, lf.family, 900, size, textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5
                                + (i - (count - 1) / 2.0) * slot,
                                c.ch * 0.5));
            b->getTransformAnimator()->getScaleAnimator()
                    ->setBaseValue(QPointF(sx, sy));
            ct.boxes << b;
        }
        ct.mainSize = size;
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("ring")) {
        const qreal R = qMin(c.cw, c.ch)
                * params.value(QStringLiteral("R")).toDouble(0.32);
        const bool upright = params.value(QStringLiteral("orient"))
                .toString() == QStringLiteral("upright");
        const QString flat = text;
        const int n = qMax(1, (int)flat.length());
        const qreal size = qMin(R * 0.32, c.ch * 0.1);
        for (int k = 0; k < n; k++) {
            const qreal ang = -90.0 + 360.0 * k / n;
            const qreal rad = qDegreesToRadians(ang);
            auto *b = mkText(group, QString(flat.at(k)), lf.family,
                             lf.weight, size, textCol);
            const QPointF pos(c.cw * 0.5 + qCos(rad) * R,
                              c.ch * 0.5 + qSin(rad) * R);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
            if (!upright) {
                b->getTransformAnimator()->getRotAnimator()
                        ->setCurrentBaseValue(ang + 90);
            }
            ct.boxes << b;
        }
        if (params.value(QStringLiteral("center")).toString()
                == QStringLiteral("word")) {
            const qreal size2 = qMin(R * 0.5, c.ch * 0.16);
            mkMain(text.left(8), size2, QPointF(c.cw * 0.5, c.ch * 0.5));
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("wave")) {
        const qreal amp = c.ch * params.value(QStringLiteral("amp"))
                .toDouble(0.12);
        const qreal freq = params.value(QStringLiteral("freq")).toDouble(1.2);
        const int n = qMax(1, (int)text.length());
        const qreal size = qMin(c.cw * 0.7 / n, c.ch * 0.2);
        for (int k = 0; k < n; k++) {
            const qreal ph = freq * 6.2831853 * k / n;
            auto *b = mkText(group, QString(text.at(k)), lf.family,
                             lf.weight, size, textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.15 + c.cw * 0.7 * k / qMax(1, n - 1),
                                c.ch * 0.5 + qSin(ph) * amp));
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("scatter")) {
        const QStringList words = text.split(
                    QRegularExpression(QStringLiteral("[\\s、，,]+")),
                    Qt::SkipEmptyParts);
        QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                    .toInt(1)) ^ 0x9E3779B9);
        const int n = qMax(1, words.size());
        for (int k = 0; k < n; k++) {
            const qreal size = c.ch * (0.09 + 0.06 * rng.generateDouble());
            auto *b = mkText(group, words.at(k % words.size()), lf.family,
                             lf.weight, size,
                             (k % 3 == 0) ? accent : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * (0.2 + 0.6 * rng.generateDouble()),
                                c.ch * (0.25 + 0.5 * rng.generateDouble())));
            b->getTransformAnimator()->getRotAnimator()
                    ->setCurrentBaseValue((rng.generateDouble() - 0.5) * 14);
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("labels")) {
        const qreal size = qMin(fitSize(text, lf.family, lf.weight,
                                        c.cw * 0.6), c.ch * 0.22);
        mkMain(text, size, QPointF(c.cw * 0.5, c.ch * 0.5));
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        const QStringList words = lineText.isEmpty()
                ? QStringList{text} : lineText.split(
                    QRegularExpression(QStringLiteral("[\\s、，,/]+")),
                    Qt::SkipEmptyParts);
        for (int k = 0; k < qMin(4, words.size()); k++) {
            const qreal ls = qBound(14.0, c.ch * 0.026, 32.0);
            const qreal y = c.ch * (k % 2 == 0 ? 0.16 : 0.84);
            const qreal x = c.cw * (0.18 + 0.22 * (k / 2));
            auto *chip = mkRect(group, QRectF(x - ls * 1.4, y - ls * 0.8,
                                              ls * (words.at(k).length() + 2), ls * 1.6),
                                QColor(), ls * 0.2, ls * 0.2);
            chip->getStrokeSettings()->setCurrentColor(sub);
            chip->getStrokeSettings()->getStrokeWidthAnimator()
                    ->setCurrentBaseValue(2);
            auto *t = mkText(group, words.at(k),
                             QStringLiteral("Noto Sans CJK JP"), 500, ls, sub);
            t->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(x, y));
        }
    } else if (has("gloss")) {
        QStringList halves;
        const int mid = qMax(1, text.length() / 2);
        halves << text.left(mid) << text.mid(mid);
        for (int k = 0; k < 2; k++) {
            if (halves.at(k).isEmpty()) { continue; }
            const qreal size = qMin(fitSize(halves.at(k), lf.family,
                                            lf.weight, c.cw * 0.42),
                                    c.ch * 0.3);
            mkMain(halves.at(k), size,
                   QPointF(c.cw * (k == 0 ? 0.3 : 0.7), c.ch * 0.5));
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("type")) {
        const qreal size = qMin(fitSize(text, QStringLiteral(
                                           "Noto Sans Mono CJK JP"), 500,
                                        c.cw * 0.7), c.ch * 0.14);
        const QPointF base(c.cw * 0.5, c.ch * 0.5);
        auto *b = mkMain(text, size, base);
        const int f0 = fSec(c, cut.value(QStringLiteral("start")).toDouble());
        const int f1 = fSec(c, cut.value(QStringLiteral("end")).toDouble());
        // blinking block cursor baked on the koma grid; with centered
        // text the right edge sits at roughly half the advance width
        auto *cursor = mkRect(group, QRectF(0, 0, size * 0.5, size * 0.1),
                              accent);
        cursor->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(base.x() + size * 0.3 * text.length(),
                            base.y() + size * 0.75));
        bakeSpan(opacityAnim(cursor), c, f0, f1, [](const qreal p) {
            return (int(p * 24) % 2) == 0 ? 100.0 : 0.0;
        });
        ct.anchor = base;
    } else if (has("diag")) {
        const qreal ang = params.value(QStringLiteral("ang"))
                .toDouble(portrait ? -14 : 12);
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.8),
                                c.ch * 0.18);
        const bool accentBand = params.value(QStringLiteral("band"))
                .toString() == QStringLiteral("accent");
        auto *band = mkRect(group, QRectF(-c.cw, -size * 0.9, c.cw * 3,
                                          size * 1.8),
                            accentBand ? accent : QColor());
        if (!accentBand) {
            band->getStrokeSettings()->setCurrentColor(accent);
            band->getStrokeSettings()->getStrokeWidthAnimator()
                    ->setCurrentBaseValue(size * 0.06);
        }
        // band spans 3×width so its local center (cw/2, 0) sits at the
        // canvas center: rotation pivots around the frame middle
        band->getBoxTransformAnimator()->getPivotAnimator()->setBaseValue(
                    QPointF(c.cw * 0.5, 0));
        band->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.5, c.ch * 0.5));
        band->getTransformAnimator()->getRotAnimator()
                ->setCurrentBaseValue(-ang);
        auto *b = mkMain(text, size, QPointF(c.cw * 0.5, c.ch * 0.5));
        b->getTransformAnimator()->getRotAnimator()->setCurrentBaseValue(-ang);
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("circle")) {
        const qreal R = qMin(c.cw, c.ch) * 0.3;
        auto *ring = mkCircle(group, QPointF(c.cw * 0.5, c.ch * 0.5), R,
                              QColor());
        ring->getStrokeSettings()->setCurrentColor(accent);
        ring->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(qMax(2.0, R * 0.02));
        const QString wrapped = splitLines(text, 8);
        const qreal size = qMin(fitSize(wrapped, lf.family, lf.weight,
                                        R * 1.3), c.ch * 0.14);
        mkMain(wrapped, size, QPointF(c.cw * 0.5, c.ch * 0.5));
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("stack")) {
        const QString wrapped = splitLines(text, 12);
        const qreal size = qMin(fitSize(wrapped, lf.family, 900, c.cw * 0.8),
                                c.ch * 0.24);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        // echo copies render below the main text (added later = lower)
        for (int k = 2; k >= 1; k--) {
            auto *echo = mkText(group, wrapped, lf.family, lf.weight, size,
                                accent);
            echo->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        pos + QPointF(size * 0.08 * k, size * 0.08 * k));
            opacityAnim(echo)->setCurrentBaseValue(45 - 15 * k);
        }
        mkMain(wrapped, size, pos);
        ct.anchor = pos;
    } else if (has("pill")) {
        const qreal size = qMin(fitSize(text, lf.family, lf.weight,
                                        c.cw * 0.6), c.ch * 0.14);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        const qreal w = size * (text.length() + 2) * 0.55;
        auto *pill = mkRect(group, QRectF(pos.x() - w/2, pos.y() - size * 0.9,
                                          w, size * 1.8), QColor(),
                            size * 0.9, size * 0.9);
        pill->getStrokeSettings()->setCurrentColor(accent);
        pill->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(qMax(2.0, size * 0.06));
        mkMain(text, size, pos);
        ct.anchor = pos;
        addNote(QPointF(pos.x(), pos.y() + size * 1.8));
    } else if (has("mixed")) {
        QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                    .toInt(1)) ^ 0x1234);
        const int n = qMax(1, (int)text.length());
        const qreal size = qMin(c.cw * 0.8 / n, c.ch * 0.3);
        for (int k = 0; k < n; k++) {
            const bool big = k % 2 == 0;
            auto *b = mkText(group, QString(text.at(k)),
                             big ? QStringLiteral("Noto Sans CJK JP")
                                 : QStringLiteral("Noto Serif CJK JP"),
                             big ? 900 : 500, size * (big ? 1.15 : 0.85),
                             big ? textCol : accent);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5 + (k - (n - 1) / 2.0) * size * 0.62,
                                c.ch * 0.5 + (rng.generateDouble() - 0.5)
                                * size * 0.25));
            b->getTransformAnimator()->getRotAnimator()
                    ->setCurrentBaseValue((rng.generateDouble() - 0.5) * 10);
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("interlude")) {
        // empty window: bg/decor only — handled by the caller
    } else if (has("curtain")) {
        // faithful port of 11p_layoutsB curtain (side variant): two
        // velvet panels part from the center; 7 pleats each in
        // alternating shade/highlight; main line sits under them
        const QString variant = params.value(QStringLiteral("variant"))
                .toString(QStringLiteral("side"));
        const bool drape = params.value(QStringLiteral("drape")).toBool(true);
        const bool pleats = params.value(QStringLiteral("pleats"))
                .toBool(true);
        const qreal size = qMin(fitSize(text, lf.family, 900,
                                        drape ? c.cw * 0.7 : c.cw * 0.8),
                                c.ch * 0.2);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        ct.anchor = pos;
        ct.mainSize = size;
        const QColor bgCol = parseColor(scheme.value(
                                           QStringLiteral("bg")),
                                       QColor(24, 24, 28));
        // velvet = mix(bg, accent, 0.5)
        const QColor panelCol = QColor(
                    (bgCol.red() + accent.red()) / 2,
                    (bgCol.green() + accent.green()) / 2,
                    (bgCol.blue() + accent.blue()) / 2);
        const QColor shade = QColor::fromRgbF(
                    panelCol.redF() * 0.78 + bgCol.redF() * 0.22,
                    panelCol.greenF() * 0.78 + bgCol.greenF() * 0.22,
                    panelCol.blueF() * 0.78 + bgCol.blueF() * 0.22);
        const QColor lite = panelCol.lighter(112);
        const qreal f0 = fSec(c, cut.value(QStringLiteral("start"))
                              .toDouble());
        const qreal inDur = cut.value(QStringLiteral("inDur"))
                .toDouble(0.3);
        const int fOpen = f0 + qRound(qBound(0.22, inDur * 1.5, 0.48)
                                      * c.fps);
        const qreal rest = drape ? c.cw * 0.075 : -c.cw * 0.02;
        for (int side = 0; side < 2; side++) {
            const qreal dir = side == 0 ? 1 : -1; // 0 = left panel
            // panel spans from its outer edge to the moving inner edge;
            // animate the inner edge by sliding a half-width panel
            auto *panel = mkRect(group,
                                 QRectF(0, -5, c.cw * 0.5, c.ch + 10),
                                 panelCol);
            auto *px = panel->getTransformAnimator()->getPosAnimator()
                    ->getXAnimator();
            px->saveValueToKey(f0, dir * c.cw * 0.005);
            px->saveValueToKey(fOpen, dir * (rest - c.cw * 0.5));
            px->setCurrentBaseValue(dir * (rest - c.cw * 0.5));
            if (pleats) {
                for (int i = 1; i < 7; i++) {
                    const qreal pxr = c.cw * 0.5 * i / 7.0;
                    auto *pleat = mkRect(group,
                                         QRectF(pxr, 0, 3, c.ch),
                                         i % 2 ? shade : lite);
                    auto *po = opacityAnim(pleat);
                    po->setCurrentBaseValue(55);
                    auto *pp = pleat->getTransformAnimator()
                            ->getPosAnimator()->getXAnimator();
                    pp->saveValueToKey(f0, dir * c.cw * 0.005);
                    pp->saveValueToKey(fOpen, dir * (rest - c.cw * 0.5));
                    pp->setCurrentBaseValue(dir * (rest - c.cw * 0.5));
                }
            }
        }
        // main line goes in AFTER the panels: added later renders
        // below, so the closed curtains actually cover it
        mkMain(text, size, pos);
    } else if (has("rain")) {
        const qreal size = qMin(fitSize(text, lf.family, lf.weight,
                                        c.cw * 0.7), c.ch * 0.14);
        const QPointF pos(c.cw * 0.5, c.ch * 0.42);
        mkMain(text, size, pos);
        ct.anchor = pos;
        QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                    .toInt(1)) ^ 0xA1);
        const int f0 = fSec(c, cut.value(QStringLiteral("start")).toDouble());
        const int f1 = fSec(c, cut.value(QStringLiteral("end")).toDouble());
        for (int k = 0; k < 7; k++) {
            auto *drop = mkText(group, text, lf.family, lf.weight,
                                size * (0.3 + 0.25 * rng.generateDouble()),
                                textCol);
            opacityAnim(drop)->setCurrentBaseValue(30);
            const qreal x = c.cw * rng.generateDouble();
            const qreal span = c.ch * 1.2;
            auto *py = drop->getTransformAnimator()->getPosAnimator()
                    ->getYAnimator();
            py->saveValueToKey(f0, -span * rng.generateDouble());
            py->saveValueToKey(f1, c.ch * (0.3 + rng.generateDouble()));
            drop->getTransformAnimator()->getPosAnimator()->getXAnimator()
                    ->setCurrentBaseValue(x);
        }
    } else if (has("tunnel") || has("zoomRepeat")) {
        // faithful port of the q^L layer flow: K copies at geometric
        // scales q^L, alternate colors, opacity envelope per layer,
        // the phase scrolls over the cut (layers continuously recede
        // toward / rush from the lens)
        const qreal q = params.value(QStringLiteral("q")).toDouble(1.5);
        const qreal speed = params.value(QStringLiteral("speed"))
                .toDouble(0.3);
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.5),
                                c.ch * 0.18);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        const int f0 = fSec(c, cut.value(QStringLiteral("start")).toDouble());
        const int f1 = qMax(f0 + 2, fSec(c, cut.value(QStringLiteral("end"))
                                .toDouble()));
        const int K = 6;
        // outer (largest, faintest) first so inner layers draw on top
        for (int k = K; k >= 0; k--) {
            const qreal s = std::pow(q, -k);
            auto *b = mkText(group, text, lf.family, 900, size,
                             k == 0 ? textCol
                             : (k % 2 ? accent : sub));
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
            b->getTransformAnimator()->getScaleAnimator()
                    ->setBaseValue(QPointF(s, s));
            if (k > 0) {
                b->getFillSettings()->setPaintType(PaintType::NOPAINT);
                setStroke(b, k % 2 ? accent : sub,
                          qMax(1.0, size * 0.02));
            }
            // per-layer opacity envelope over the phase: layer k is
            // loudest while the phase front crosses it
            auto *opa = opacityAnim(b);
            bakeSpan(opa, c, f0, f1, [&](const qreal p) {
                const qreal lb = p * (f1 - f0) / c.fps;
                const qreal ph0 = lb * speed;
                const qreal L = k + ph0 - std::floor(ph0);
                const qreal a = qBound(0.0, (L - 0.15) / 0.5, 1.0)
                        * qBound(0.0, (1.0 - L) / 0.5, 1.0);
                return (k == 0 ? 100 : 62 * a);
            });
            ct.boxes << b;
        }
        ct.anchor = pos;
        ct.mainSize = size;
    } else if (has("frameBox")) {
        const qreal size = qMin(fitSize(text, lf.family, lf.weight,
                                        c.cw * 0.66), c.ch * 0.2);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        mkMain(text, size, pos);
        ct.anchor = pos;
        const qreal m = c.ch * 0.09, w = 3;
        const QColor col = accent;
        mkRect(group, QRectF(m, m, c.cw - 2 * m, w), col);
        mkRect(group, QRectF(m, c.ch - m - w, c.cw - 2 * m, w), col);
        mkRect(group, QRectF(m, m, w, c.ch - 2 * m), col);
        mkRect(group, QRectF(c.cw - m - w, m, w, c.ch - 2 * m), col);
    } else if (has("splitHalves") || has("halfVertical")) {
        // the line breaks into an upper and lower band
        const QString wrapped = splitLines(text, 10);
        const qreal size = qMin(fitSize(wrapped, lf.family, 900, c.cw * 0.6),
                                c.ch * 0.2);
        const qreal gap = c.ch * 0.09;
        const bool vert = has("halfVertical");
        for (int k = 0; k < 2; k++) {
            const QString part = k == 0 ? text.left(text.length() / 2)
                                        : text.mid(text.length() / 2);
            if (part.trimmed().isEmpty()) { continue; }
            auto *b = mkText(group, part, lf.family, 900, size,
                             k == 0 ? textCol : accent);
            const QPointF pos = vert
                    ? QPointF(c.cw * (k == 0 ? 0.5 : 0.5),
                              c.ch * (k == 0 ? 0.34 : 0.66))
                    : QPointF(c.cw * (k == 0 ? 0.27 : 0.73), c.ch * 0.5);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
            ct.boxes << b;
            ct.mainSize = qMax(ct.mainSize, size);
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("orbit")) {
        const qreal R = qMin(c.cw, c.ch) * 0.3;
        const int n = qMax(2, text.length());
        const qreal size = qMin(R * 0.3, c.ch * 0.1);
        for (int k = 0; k < n; k++) {
            const qreal ang = -90.0 + 360.0 * k / n;
            const qreal rad = qDegreesToRadians(ang);
            auto *b = mkText(group, QString(text.at(k)), lf.family,
                             lf.weight, size, textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5 + qCos(rad) * R,
                                c.ch * 0.5 + qSin(rad) * R * 0.6));
            ct.boxes << b;
        }
        const qreal size2 = qMin(R * 0.5, c.ch * 0.16);
        mkMain(text.left(6), size2, QPointF(c.cw * 0.5, c.ch * 0.5));
        auto *orbitRing = mkCircle(group, QPointF(c.cw * 0.5, c.ch * 0.5),
                                   R, QColor());
        orbitRing->getStrokeSettings()->setCurrentColor(sub);
        orbitRing->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(1.5);
        orbitRing->getTransformAnimator()->getScaleAnimator()
                ->setBaseValue(QPointF(1.0, 0.6));
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("mirror")) {
        const qreal size = qMin(fitSize(text, lf.family, lf.weight,
                                        c.cw * 0.72), c.ch * 0.2);
        const QPointF pos(c.cw * 0.5, c.ch * 0.42);
        mkMain(text, size, pos);
        auto *refl = mkText(group, text, lf.family, lf.weight, size, textCol);
        refl->prp_setName(QStringLiteral("镜像"));
        refl->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(pos.x(), pos.y() + size * 1.1));
        refl->getTransformAnimator()->getScaleAnimator()
                ->setBaseValue(QPointF(1.0, -0.85));
        opacityAnim(refl)->setCurrentBaseValue(26);
        ct.anchor = pos;
    } else if (has("arcTop")) {
        const int n = qMax(1, text.length());
        const qreal size = qMin(c.cw * 0.6 / n, c.ch * 0.14);
        const qreal R = c.ch * 0.42;
        for (int k = 0; k < n; k++) {
            const qreal ang = 180.0 + 140.0 * k / qMax(1, n - 1);
            const qreal rad = qDegreesToRadians(ang);
            auto *b = mkText(group, QString(text.at(k)), lf.family,
                             lf.weight, size, textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5 + qCos(rad) * R * 1.3,
                                c.ch * 0.78 + qSin(rad) * R));
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("spiral")) {
        const int n = qMax(1, text.length());
        const qreal size = qMin(c.cw * 0.05, c.ch * 0.07);
        for (int k = 0; k < n; k++) {
            const qreal t = qreal(k) / qMax(1, n - 1);
            const qreal ang = t * 4 * 6.2831853;
            const qreal r = qMin(c.cw, c.ch) * 0.06 * (1 + 3.6 * t);
            auto *b = mkText(group, QString(text.at(k)), lf.family,
                             lf.weight, size, k % 3 == 0 ? accent : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5 + qCos(ang) * r,
                                c.ch * 0.5 + qSin(ang) * r));
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("staircase") || has("zigzag")) {
        const int n = qMax(1, text.length());
        const qreal size = qMin(c.cw * 0.74 / n, c.ch * 0.16);
        const bool stairs = has("staircase");
        for (int k = 0; k < n; k++) {
            auto *b = mkText(group, QString(text.at(k)), lf.family,
                             k % 2 ? 500 : 900, size,
                             k % 4 == 2 ? accent : textCol);
            const qreal yy = stairs
                    ? c.ch * (0.3 + 0.42 * k / qMax(1, n - 1))
                    : c.ch * (k % 2 ? 0.42 : 0.58);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.14 + c.cw * 0.74 * k / qMax(1, n - 1),
                                yy));
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("wordCloud") || has("scatter2")) {
        const QStringList words = text.split(
                    QRegularExpression(QStringLiteral("[\\s、，,]+")),
                    Qt::SkipEmptyParts);
        QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                    .toInt(1)) ^ 0xC10D);
        const int n = qMax(1, words.size());
        for (int k = 0; k < n; k++) {
            const qreal size = c.ch * (0.06 + 0.09 * rng.generateDouble());
            auto *b = mkText(group, words.at(k % words.size()), lf.family,
                             rng.generateDouble() > 0.5 ? 900 : 500, size,
                             rng.generateDouble() > 0.7 ? accent : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * (0.15 + 0.7 * rng.generateDouble()),
                                c.ch * (0.18 + 0.64 * rng.generateDouble())));
            b->getTransformAnimator()->getRotAnimator()
                    ->setCurrentBaseValue((rng.generateDouble() - 0.5) * 8);
            ct.boxes << b;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("kanjiFocus")) {
        const QChar lead = text.at(0);
        const qreal bigSize = c.ch * 0.42;
        auto *b0 = mkText(group, QString(lead), lf.family, 900, bigSize,
                          accent);
        b0->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.5, c.ch * 0.44));
        ct.boxes << b0;
        const QString rest = text.mid(1).trimmed();
        if (!rest.isEmpty()) {
            const qreal size = qMin(fitSize(rest, lf.family, 500,
                                            c.cw * 0.5), c.ch * 0.1);
            auto *b1 = mkText(group, rest, lf.family, 500, size, textCol);
            b1->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5, c.ch * 0.8));
            ct.boxes << b1;
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.44);
        ct.mainSize = bigSize;
    } else if (has("filmstrip") || has("tape")) {
        const qreal size = qMin(fitSize(text, lf.family, lf.weight,
                                        c.cw * 0.6), c.ch * 0.18);
        const QPointF pos(c.cw * 0.5, c.ch * 0.4);
        mkMain(text, size, pos);
        ct.anchor = pos;
        const qreal bandY = c.ch * 0.78, bandH = c.ch * 0.16;
        mkRect(group, QRectF(0, bandY, c.cw, bandH), QColor(16, 16, 18));
        for (int k = 0; k * c.cw * 0.045 < c.cw; k++) {
            mkRect(group, QRectF(c.cw * 0.012 + k * c.cw * 0.045,
                                 bandY + bandH * 0.18, c.cw * 0.02,
                                 bandH * 0.24), QColor(230, 228, 220));
            mkRect(group, QRectF(c.cw * 0.012 + k * c.cw * 0.045,
                                 bandY + bandH * 0.58, c.cw * 0.02,
                                 bandH * 0.24), QColor(230, 228, 220));
        }
    } else if (has("tyRuby")) {
        const int n = qMax(1, text.length());
        const qreal size = qMin(c.cw * 0.7 / n, c.ch * 0.2);
        const qreal rubySize = size * 0.24;
        for (int k = 0; k < n; k++) {
            const QPointF pos(c.cw * 0.5 + (k - (n - 1) / 2.0) * size * 0.92,
                              c.ch * 0.52);
            auto *b = mkText(group, QString(text.at(k)), lf.family,
                             900, size, textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
            ct.boxes << b;
            auto *r = mkText(group, QString(QChar(0x30FF - k % 40)),
                             QStringLiteral("Noto Sans CJK JP"), 500,
                             rubySize, sub);
            r->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(pos.x(), pos.y() - size * 0.78));
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.52);
        ct.mainSize = size;
    } else if (has("quote") || has("genkou")) {
        const QString wrapped = splitLines(text, 12);
        const qreal size = qMin(fitSize(wrapped, lf.family, 500, c.cw * 0.6),
                                c.ch * 0.16);
        const QPointF pos(c.cw * 0.44, c.ch * 0.5);
        mkMain(wrapped, size, pos);
        ct.anchor = pos;
        auto *mark = mkText(group, QStringLiteral("\u201C"),
                            lf.family, 900, size * 1.6, accent);
        mark->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.14, c.ch * 0.3));
        if (has("genkou")) {
            for (int k = 0; k < 6; k++) {
                mkRect(group, QRectF(c.cw * 0.18, c.ch * (0.2 + 0.1 * k),
                                     c.cw * 0.5, 1), sub);
            }
        }
    } else if (has("ticker") || has("lowerThird") || has("subtitleBar")) {
        const qreal size = qMin(fitSize(text, lf.family, 700, c.cw * 0.6),
                                c.ch * 0.08);
        const qreal y = has("lowerThird") ? c.ch * 0.82 : c.ch * 0.88;
        mkRect(group, QRectF(c.cw * 0.06, y - size * 1.0,
                             c.cw * 0.88, size * 2.0),
               QColor(12, 12, 14, 200));
        const QPointF pos(c.cw * 0.5, y);
        mkMain(text, size, pos);
        if (has("ticker")) {
            const int f0 = fSec(c, cut.value(QStringLiteral("start"))
                                .toDouble());
            const int f1 = fSec(c, cut.value(QStringLiteral("end"))
                                .toDouble());
            auto *b = ct.boxes.first();
            auto *px = b->getTransformAnimator()->getPosAnimator()
                    ->getXAnimator();
            const qreal span = c.cw + size * 0.6 * text.length();
            bakeSpan(px, c, f0, f1, [&](const qreal p) {
                return c.cw * 0.5 + span * (0.5 - p);
            });
        }
        ct.anchor = pos;
    } else if (has("depthStack")) {
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.7),
                                c.ch * 0.22);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        for (int k = 3; k >= 1; k--) {
            auto *b = mkText(group, text, lf.family, 900, size,
                             k == 1 ? textCol : sub);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        pos + QPointF(size * 0.06 * k, size * 0.06 * k));
            opacityAnim(b)->setCurrentBaseValue(k == 1 ? 100 : 40 / k);
            ct.boxes << b;
        }
        ct.anchor = pos;
        ct.mainSize = size;
    } else if (has("corners") || has("ruler") || has("tyBaseline")
               || has("tyVRuler")) {
        // editorial corner labels + guide rules
        const qreal size = qMin(fitSize(text, lf.family, 900, c.cw * 0.55),
                                c.ch * 0.2);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        mkMain(text, size, pos);
        ct.anchor = pos;
        const qreal s2 = qBound(12.0, c.ch * 0.024, 30.0);
        const QStringList tags = {
            QStringLiteral("A/01"), QStringLiteral("SEQ."),
            QStringLiteral("00:%1").arg(
                qRound(cut.value(QStringLiteral("start")).toDouble())),
            QStringLiteral("JZ") };
        for (int k = 0; k < 4; k++) {
            auto *t = mkText(group, tags.at(k),
                             QStringLiteral("Noto Sans Mono CJK JP"), 500,
                             s2, sub);
            t->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * (k % 2 ? 0.92 : 0.08),
                                c.ch * (k < 2 ? 0.08 : 0.92)));
        }
        mkRect(group, QRectF(c.cw * 0.06, pos.y() + size * 0.8,
                             c.cw * 0.88, 1.5), accent);
        if (has("tyVRuler") || has("ruler")) {
            for (int k = 1; k < 5; k++) {
                mkRect(group, QRectF(c.cw * 0.06 + c.cw * 0.88 * k / 5.0,
                                     c.ch * 0.86, 8, 1), sub);
            }
        }
    } else if (has("keycaps") || has("ticket") || has("notification")
               || has("chat") || has("searchBar")) {
        // chip/card family: the line inside a framed capsule
        const qreal size = qMin(fitSize(text, lf.family, 700, c.cw * 0.5),
                                c.ch * 0.12);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        const qreal w = size * 0.62 * text.length() + size * 2.2;
        const qreal h = size * 2.4;
        auto *card = mkRect(group, QRectF(pos.x() - w / 2, pos.y() - h / 2,
                                          w, h), QColor(),
                            size * 0.4, size * 0.4);
        const QColor cardCol = has("ticket") ? QColor(238, 232, 218)
                : sub;
        card->getFillSettings()->setPaintType(PaintType::FLATPAINT);
        card->getFillSettings()->setCurrentColor(
                    has("notification") || has("chat")
                    ? QColor(24, 24, 28) : cardCol);
        card->getStrokeSettings()->setCurrentColor(accent);
        card->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(2);
        if (has("ticket")) {
            // perforation dots along the middle
            for (int k = 0; k < 9; k++) {
                mkCircle(group, QPointF(pos.x() - w / 2 + w * k / 8.0,
                                        pos.y()), size * 0.07,
                         QColor(16, 16, 18));
            }
        }
        mkMain(text, size, QPointF(pos.x(), has("ticket")
                                   ? pos.y() - h * 0.22 : pos.y()));
        ct.anchor = pos;
        if (has("searchBar")) {
            auto *lens = mkCircle(group, QPointF(pos.x() + w * 0.46,
                                                 pos.y() - h * 0.46),
                                  size * 0.34, QColor());
            lens->getStrokeSettings()->setCurrentColor(textCol);
            lens->getStrokeSettings()->getStrokeWidthAnimator()
                    ->setCurrentBaseValue(3);
        }
        if (has("notification") || has("chat")) {
            auto *dot = mkCircle(group, QPointF(pos.x() - w * 0.44,
                                                pos.y() - h * 0.44),
                                 size * 0.2, accent);
            Q_UNUSED(dot);
        }
    } else if (has("flipBoard") || has("gridCells") || has("dotMatrix")) {
        // per-glyph cells with swap-in (split-flap board feel)
        const int n = qMax(1, text.length());
        const qreal size = qMin(c.cw * 0.74 / n, c.ch * 0.18);
        const qreal cell = size * 1.35;
        for (int k = 0; k < n; k++) {
            const QPointF p(c.cw * 0.5 + (k - (n - 1) / 2.0) * cell,
                            c.ch * 0.48);
            auto *back = mkRect(group, QRectF(p.x() - cell / 2,
                                              p.y() - cell / 2, cell, cell),
                                has("dotMatrix") ? QColor(18, 18, 22)
                                                 : QColor(),
                                size * 0.12, size * 0.12);
            if (!has("dotMatrix")) {
                back->getStrokeSettings()->setCurrentColor(sub);
                back->getStrokeSettings()->getStrokeWidthAnimator()
                        ->setCurrentBaseValue(1.5);
            } else {
                for (int gy = 0; gy < 3; gy++) {
                    for (int gx = 0; gx < 2; gx++) {
                        mkCircle(group, QPointF(
                                     p.x() - cell * 0.25 + gx * cell * 0.5,
                                     p.y() - cell * 0.3 + gy * cell * 0.3),
                                 size * 0.05, sub);
                    }
                }
            }
            auto *b = mkText(group, QString(text.at(k)), lf.family, 900,
                             size, has("dotMatrix") ? sub : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(p.x(), has("dotMatrix")
                                ? p.y() + cell * 0.62 : p.y()));
            ct.boxes << b;
            if (has("flipBoard")) {
                scrambleKeys(c, b,
                             fSec(c, cut.value(QStringLiteral("start"))
                                  .toDouble()),
                             fSec(c, cut.value(QStringLiteral("start"))
                                  .toDouble())
                             + qRound(0.45 * c.fps),
                             quint32(cut.value(QStringLiteral("seed"))
                                     .toInt(1)) + k);
            }
        }
        ct.mainSize = size;
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.48);
    } else if (has("hanging") || has("sideways") || has("perspective")) {
        const qreal size = qMin(fitSize(text, lf.family, 700, c.cw * 0.6),
                                c.ch * 0.16);
        const QPointF pos(c.cw * 0.5, c.ch * 0.5);
        auto *b = mkMain(text, size, pos);
        if (has("hanging")) {
            // hangs from the top: thread + slight sway
            mkRect(group, QRectF(pos.x() - 1, c.ch * 0.06, 2,
                                 pos.y() - c.ch * 0.06 - size * 0.6),
                   sub);
            b->getBoxTransformAnimator()->getPivotAnimator()->setBaseValue(
                        QPointF(0, -size * 0.6));
            auto *rot = b->getTransformAnimator()->getRotAnimator();
            const int f0 = fSec(c, cut.value(QStringLiteral("start"))
                                .toDouble());
            const int f1 = fSec(c, cut.value(QStringLiteral("end"))
                                .toDouble());
            bakeSpan(rot, c, f0, f1, [](const qreal p) {
                return 3.5 * qSin(p * 6.2831853);
            });
        } else if (has("sideways")) {
            b->getTransformAnimator()->getRotAnimator()
                    ->setCurrentBaseValue(90);
        } else {
            b->getTransformAnimator()->getScaleAnimator()
                    ->setBaseValue(QPointF(1.0, 0.45));
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.5, c.ch * 0.62));
        }
        ct.anchor = pos;
    } else if (has("columnsBig") || has("justified") || has("dropCap")
               || has("hanko")) {
        // editorial columns / drop cap / seal stamp
        if (has("hanko")) {
            const qreal s = c.ch * 0.24;
            const QPointF pos(c.cw * 0.76, c.ch * 0.7);
            auto *seal = mkRect(group, QRectF(pos.x() - s, pos.y() - s,
                                              s * 2, s * 2),
                                QColor(178, 34, 34), s * 0.1, s * 0.1);
            Q_UNUSED(seal);
            const QString mark = text.left(2);
            auto *b = mkText(group, mark,
                             QStringLiteral("Noto Serif CJK JP"), 900,
                             s * 0.9, QColor(255, 244, 230));
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
            ct.boxes << b;
            const qreal size2 = qMin(fitSize(text, lf.family, 700,
                                             c.cw * 0.5), c.ch * 0.12);
            auto *m = mkText(group, text, lf.family, 700, size2, textCol);
            m->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.34, c.ch * 0.4));
            ct.boxes << m;
            ct.mainSize = size2;
        } else if (has("dropCap")) {
            const qreal big = c.ch * 0.34;
            auto *b0 = mkText(group, text.left(1),
                              QStringLiteral("Noto Serif CJK JP"), 900,
                              big, accent);
            b0->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        QPointF(c.cw * 0.2, c.ch * 0.42));
            b0->setTextHAlignment(Qt::AlignLeft);
            ct.boxes << b0;
            const QString rest = text.mid(1).trimmed();
            if (!rest.isEmpty()) {
                const qreal size = qMin(fitSize(rest, lf.family, 400,
                                                c.cw * 0.44), c.ch * 0.1);
                auto *b1 = mkText(group, rest, lf.family, 400, size,
                                  textCol);
                b1->getTransformAnimator()->getPosAnimator()->setBaseValue(
                            QPointF(c.cw * 0.62, c.ch * 0.5));
                ct.boxes << b1;
            }
            ct.anchor = QPointF(c.cw * 0.4, c.ch * 0.45);
        } else {
            const int cols = has("columnsBig") ? 2 : 1;
            const qreal size = qMin(fitSize(text, lf.family, 700,
                                            c.cw * 0.9 / cols),
                                    c.ch * 0.2);
            for (int k = 0; k < cols; k++) {
                const QString part = cols == 1 ? text : k == 0
                        ? text.left(text.length() / 2)
                        : text.mid(text.length() / 2);
                if (part.trimmed().isEmpty()) { continue; }
                auto *b = mkText(group, part, lf.family, 700, size,
                                 k ? accent : textCol);
                const QPointF pos(cols == 1
                                  ? QPointF(c.cw * 0.5, c.ch * 0.5)
                                  : QPointF(c.cw * (k ? 0.72 : 0.28),
                                            c.ch * 0.5));
                b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
                if (cols == 1) {
                    // justified: stretch to the full measure
                    const qreal stretch = 1.0 * c.cw * 0.86
                            / (size * 0.62 * text.length() + 1);
                    b->getTransformAnimator()->getScaleAnimator()
                            ->setBaseValue(QPointF(qBound(1.0, stretch, 1.6),
                                                   1.0));
                }
                ct.boxes << b;
                ct.mainSize = qMax(ct.mainSize, size);
            }
            ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        }
    } else if (has("stickerBomb") || has("equalizer") || has("credits")) {
        if (has("credits")) {
            // end-roll: centered line drifting upward
            const qreal size = qMin(fitSize(text, lf.family, 500,
                                            c.cw * 0.5), c.ch * 0.09);
            const int f0 = fSec(c, cut.value(QStringLiteral("start"))
                                .toDouble());
            const int f1 = fSec(c, cut.value(QStringLiteral("end"))
                                .toDouble());
            auto *b = mkMain(text, size, QPointF(c.cw * 0.5, c.ch * 0.6));
            auto *py = b->getTransformAnimator()->getPosAnimator()
                    ->getYAnimator();
            bakeSpan(py, c, f0, f1, [&](const qreal p) {
                return c.ch * (0.62 - 0.26 * p);
            });
            ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        } else if (has("equalizer")) {
            const qreal size = qMin(fitSize(text, lf.family, 900,
                                            c.cw * 0.5), c.ch * 0.16);
            const QPointF pos(c.cw * 0.5, c.ch * 0.38);
            mkMain(text, size, pos);
            ct.anchor = pos;
            QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                        .toInt(1)) ^ 0xE0);
            const int bars = 18;
            for (int k = 0; k < bars; k++) {
                const qreal h = c.ch * (0.02 + 0.1
                                        * rng.generateDouble());
                auto *bar = mkRect(group, QRectF(
                            c.cw * 0.1 + c.cw * 0.8 * k / bars,
                            c.ch * 0.9 - h, c.cw * 0.8 / bars - 3, h),
                                   k % 5 == 0 ? accent : sub);
                opacityAnim(bar)->setCurrentBaseValue(80);
            }
        } else {
            // sticker shower: rotated labels scattered over the frame
            const QStringList words = text.split(
                        QRegularExpression(QStringLiteral("[\\s、，,/]+")),
                        Qt::SkipEmptyParts);
            QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                        .toInt(1)) ^ 0x57);
            const int n = qMax(3, words.size());
            for (int k = 0; k < n; k++) {
                const qreal size = c.ch * (0.05 + 0.06
                                           * rng.generateDouble());
                const QPointF p(c.cw * (0.12 + 0.76
                                        * rng.generateDouble()),
                                c.ch * (0.15 + 0.7
                                        * rng.generateDouble()));
                const qreal rot = (rng.generateDouble() - 0.5) * 24;
                auto *st = mkRect(group, QRectF(
                            p.x() - size * (words.at(k % words.size())
                                            .length() * 0.4 + 0.6),
                            p.y() - size * 0.9,
                            size * (words.at(k % words.size()).length()
                                    * 0.8 + 1.2), size * 1.8),
                                   k % 3 ? QColor(255, 240, 120)
                                   : accent, size * 0.15, size * 0.15);
                st->getTransformAnimator()->getRotAnimator()
                        ->setCurrentBaseValue(rot);
                auto *b = mkText(group, words.at(k % words.size()),
                                 lf.family, 900, size, QColor(20, 20, 24));
                b->getTransformAnimator()->getPosAnimator()->setBaseValue(p);
                b->getTransformAnimator()->getRotAnimator()
                        ->setCurrentBaseValue(rot);
                ct.boxes << b;
            }
            ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
        }
    } else if (has("bubble") || has("bubbles") || has("circleWords")) {
        // words in outlined bubbles / words around a circle
        const QStringList words = text.split(
                    QRegularExpression(QStringLiteral("[\\s、，,/]+")),
                    Qt::SkipEmptyParts);
        const int n = qMax(1, words.size());
        QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                    .toInt(1)) ^ 0xB08);
        if (has("circleWords")) {
            const qreal R = qMin(c.cw, c.ch) * 0.3;
            const qreal size = qMin(R * 0.3, c.ch * 0.08);
            for (int k = 0; k < n; k++) {
                const qreal ang = -90.0 + 360.0 * k / n;
                const qreal rad = qDegreesToRadians(ang);
                auto *b = mkText(group, words.at(k), lf.family, 700,
                                 size, k % 2 ? textCol : accent);
                b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                            QPointF(c.cw * 0.5 + qCos(rad) * R,
                                    c.ch * 0.5 + qSin(rad) * R));
                ct.boxes << b;
            }
            auto *ring = mkCircle(group, QPointF(c.cw * 0.5, c.ch * 0.5),
                                  R, QColor());
            ring->getStrokeSettings()->setCurrentColor(sub);
            ring->getStrokeSettings()->getStrokeWidthAnimator()
                    ->setCurrentBaseValue(1.5);
        } else {
            for (int k = 0; k < n; k++) {
                const qreal size = c.ch * (0.07 + 0.05
                                           * rng.generateDouble());
                auto *b = mkText(group, words.at(k % words.size()),
                                 lf.family, 700, size, textCol);
                const QPointF pos(c.cw * (0.2 + 0.6
                                          * rng.generateDouble()),
                                  c.ch * (0.25 + 0.5
                                          * rng.generateDouble()));
                b->getTransformAnimator()->getPosAnimator()
                        ->setBaseValue(pos);
                const qreal r = size * (0.8 + 0.35
                                        * words.at(k % words.size())
                                        .length() * 0.3);
                auto *bub = mkCircle(group, pos, r, QColor());
                bub->getStrokeSettings()->setCurrentColor(
                            k % 2 ? accent : sub);
                bub->getStrokeSettings()->getStrokeWidthAnimator()
                        ->setCurrentBaseValue(2);
                ct.boxes << b;
            }
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("panels") || has("splitScreen") || has("crossBands")) {
        // the line split across 2-3 vertical panels / bands
        const int panels = has("crossBands") ? 3 : qMin(3, qMax(2,
                text.length() / 4));
        const qreal size = qMin(fitSize(text.left(
                                     qMax(1, text.length() / panels)),
                                     lf.family, 900, c.cw * 0.4 / panels * 2),
                                c.ch * 0.14);
        for (int k = 0; k < panels; k++) {
            const QString part = text.mid(k * text.length() / panels,
                                          text.length() / panels)
                    .trimmed();
            if (part.isEmpty()) { continue; }
            const bool banded = has("crossBands");
            const QPointF pos(banded
                              ? QPointF(c.cw * 0.5,
                                        c.ch * (0.28 + 0.22 * k))
                              : QPointF(c.cw * (0.5 + (k - (panels - 1) / 2.0)
                                                * 0.34), c.ch * 0.5));
            if (banded && k % 2 == 0) {
                mkRect(group, QRectF(c.cw * 0.1, pos.y() - size * 1.2,
                                     c.cw * 0.8, size * 2.4), accent);
            }
            auto *b = mkText(group, part, lf.family, 900, size,
                             banded && k % 2 == 0 ? QColor(16, 16, 18)
                                                  : textCol);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(pos);
            ct.boxes << b;
            ct.mainSize = qMax(ct.mainSize, size);
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (layout.startsWith(QStringLiteral("kn"))
               || layout == QStringLiteral("bounceLine")
               || layout == QStringLiteral("elastic")) {
        // kinetic word timeline: every word is its own box entering on
        // its own beat — the essence of the kn* family
        ct = kineticWords(c, group, cut, text, lf, textCol, accent, sub);
    } else if (layout.startsWith(QStringLiteral("ty"))) {
        // editorial typesetting family: grid/frame/specimen variants
        ct = typoEditorial(c, group, cut, text, lf, textCol, accent, sub);
    } else {
        unknown();
        // fallback is not a flat center anymore: the seed picks an
        // anchor drift, size ratio, tilt and outline/fill style so two
        // different unknown keys never look identical
        QRandomGenerator rng(quint32(cut.value(QStringLiteral("seed"))
                                    .toInt(1)) ^ qHash(layout));
        const QPointF drift(c.cw * (0.36 + 0.28 * rng.generateDouble()),
                            c.ch * (0.36 + 0.28 * rng.generateDouble()));
        const qreal ratio = 0.7 + 0.5 * rng.generateDouble();
        const qreal tilt = (rng.generateDouble() - 0.5) * 10;
        const bool outline = rng.generateDouble() > 0.72;
        const QString wrapped = splitLines(text, portrait ? 7 : 11);
        const qreal size = qMin(fitSize(wrapped, lf.family, lf.weight,
                                        c.cw * 0.8) * ratio, c.ch * 0.3);
        auto *b = mkText(group, wrapped, lf.family, lf.weight, size,
                         outline ? QColor() : textCol);
        if (outline) {
            setStroke(b, textCol, qMax(1.5, size * 0.03));
            opacityAnim(b)->setCurrentBaseValue(92);
        }
        b->getTransformAnimator()->getPosAnimator()->setBaseValue(drift);
        b->getTransformAnimator()->getRotAnimator()
                ->setCurrentBaseValue(tilt);
        ct.boxes << b;
        ct.mainSize = size;
        ct.anchor = drift;
    }
    return ct;
}

// ======================================================================
// enter / hold / exit recipes
// ======================================================================

QString enterPreset(const QString &key, Ctx &c) {
    const auto has = [&key](const char *s) {
        return key.contains(QLatin1String(s), Qt::CaseInsensitive);
    };
    if (key == QStringLiteral("cut") || key == QStringLiteral("none")) {
        return QString();
    }
    if (key == QStringLiteral("assemble")) {
        const quint32 h = qHash(key) % 4;
        static const char *const kIds[4] = {"sharp-snap-rise",
                    "sharp-snap-drop", "sharp-snap-left", "sharp-snap-right"};
        return QString::fromUtf8(kIds[h]);
    }
    if (key == QStringLiteral("slice")) { return QStringLiteral("sharp-blade-cut"); }
    if (key == QStringLiteral("type")) { return QStringLiteral("tech-typewriter-std"); }
    if (key == QStringLiteral("pop")) { return QStringLiteral("sharp-elastic-pop"); }
    if (key == QStringLiteral("drop")) { return QStringLiteral("sharp-snap-drop"); }
    if (key == QStringLiteral("stretch")) { return QStringLiteral("prop-scale-stretch-x"); }
    if (key == QStringLiteral("wipe")) { return QStringLiteral("smooth-line-slide"); }
    if (key == QStringLiteral("blur")) { return QStringLiteral("smooth-cinematic-fade"); }
    if (key == QStringLiteral("spin")) { return QStringLiteral("sharp-whip-twist"); }
    if (key == QStringLiteral("flicker")) { return QStringLiteral("tech-strobe-alert"); }
    if (key == QStringLiteral("scramble")) { return QStringLiteral("tech-analog-noise"); }
    if (key == QStringLiteral("zoom")) { return QStringLiteral("smooth-focus-zoom"); }
    if (has("stamp") || has("slam")) { return QStringLiteral("sharp-slam-front"); }
    if (has("bounce") || has("squash")) { return QStringLiteral("sharp-overshoot-down"); }
    if (has("domino")) { return QStringLiteral("sharp-domino-fall"); }
    if (has("whip")) { return QStringLiteral("sharp-whip-ccw"); }
    if (has("spiral")) { return QStringLiteral("smooth-twirl-bloom"); }
    if (has("flip")) { return QStringLiteral("3d-flip-y-cw"); }
    if (has("glitch")) { return QStringLiteral("tech-digital-glitch-drop"); }
    if (has("iris") || has("zoomout")) { return QStringLiteral("smooth-focus-zoom"); }
    if (has("neon") || has("flicker")) { return QStringLiteral("tech-strobe-alert"); }
    if (has("wave")) { return QStringLiteral("smooth-line-rise"); }
    if (has("rise") || has("unroll") || has("unfold")) { return QStringLiteral("sharp-snap-rise"); }
    if (has("drop") || has("fall")) { return QStringLiteral("sharp-snap-drop"); }
    if (has("slide")) { return QStringLiteral("smooth-glide-left"); }
    if (has("mask") || has("wipe")) { return QStringLiteral("smooth-line-slide"); }
    if (has("fan")) { return QStringLiteral("smooth-line-slide"); }
    if (has("brush") || has("ink")) { return QStringLiteral("smooth-cinematic-fade"); }
    if (has("cursor")) { return QStringLiteral("tech-cursor-stream"); }
    if (has("skew")) { return QStringLiteral("prop-shear-slash-x"); }
    if (has("dive") || has("divein")) { return QStringLiteral("sharp-recoil-blast"); }
    if (has("inertia")) { return QStringLiteral("sharp-gelatin-settle"); }
    if (has("slam") || has("snaptype")) { return QStringLiteral("sharp-snap-word-slam"); }
    if (has("gear") || has("roll")) { return QStringLiteral("prop-rot-cw-360"); }
    if (has("ruby")) { return QStringLiteral("smooth-word-bloom"); }
    if (has("turn") || has("quarter")) { return QStringLiteral("3d-door-swing-left"); }
    if (has("seesaw") || has("tumble")) { return QStringLiteral("sharp-domino-fall"); }
    c.res->substitutions++;
    return QStringLiteral("smooth-cinematic-fade");
}

QString exitPreset(const QString &key, Ctx &c) {
    const auto has = [&key](const char *s) {
        return key.contains(QLatin1String(s), Qt::CaseInsensitive);
    };
    if (key == QStringLiteral("cut") || key == QStringLiteral("none")) {
        return QString();
    }
    if (key == QStringLiteral("explode")) { return QStringLiteral("sharp-punch-burst"); }
    if (key == QStringLiteral("fall")) { return QStringLiteral("sharp-snap-drop"); }
    if (key == QStringLiteral("drift")) { return QStringLiteral("smooth-float-sink"); }
    if (key == QStringLiteral("slice")) { return QStringLiteral("sharp-blade-slash"); }
    if (key == QStringLiteral("wipe")) { return QStringLiteral("smooth-line-slide"); }
    if (key == QStringLiteral("shrink")) { return QStringLiteral("prop-scale-shrink"); }
    if (key == QStringLiteral("blur")) { return QStringLiteral("smooth-cinematic-fade"); }
    if (key == QStringLiteral("stretch")) { return QStringLiteral("prop-scale-stretch-x"); }
    if (key == QStringLiteral("scatter")) { return QStringLiteral("sharp-rebound-diag"); }
    if (key == QStringLiteral("glitch")) { return QStringLiteral("tech-digital-glitch-drop"); }
    if (has("fan") || has("close")) { return QStringLiteral("smooth-line-slide"); }
    if (has("shatter") || has("burst")) { return QStringLiteral("sharp-punch-burst"); }
    if (has("gravity") || has("sink") || has("under")) { return QStringLiteral("sharp-snap-drop"); }
    if (has("pop") || has("launch")) { return QStringLiteral("sharp-recoil-blast"); }
    if (has("melt") || has("collapse")) { return QStringLiteral("prop-scale-squeeze-y"); }
    if (has("burn") || has("ash")) { return QStringLiteral("tech-analog-noise"); }
    if (has("whip")) { return QStringLiteral("sharp-horizontal-whip"); }
    if (has("dot") || has("shrink")) { return QStringLiteral("prop-scale-shrink"); }
    if (has("bracket") || has("cover") || has("sweep")) { return QStringLiteral("smooth-line-curtain"); }
    if (has("blink") || has("blinkout")) { return QStringLiteral("tech-strobe-alert"); }
    if (has("diag")) { return QStringLiteral("smooth-drift-diag-br"); }
    c.res->substitutions++;
    return QStringLiteral("smooth-cinematic-fade");
}

QString holdPreset(const QString &key, Ctx &c) {
    if (key == QStringLiteral("still") || key.isEmpty()
            || key == QStringLiteral("none")) { return QString(); }
    if (key == QStringLiteral("jitter")) { return QStringLiteral("loop-micro-jitter"); }
    if (key == QStringLiteral("drift")) { return QStringLiteral("loop-gentle-float"); }
    if (key == QStringLiteral("breathe")) { return QStringLiteral("loop-breathe-soft"); }
    if (key == QStringLiteral("wave")) { return QStringLiteral("loop-sine-wave"); }
    if (key.contains(QStringLiteral("glitch"))) { return QStringLiteral("loop-glitch-twitch"); }
    c.res->substitutions++;
    return QStringLiteral("loop-gentle-float");
}

// blur reveals ride on a box-level blur keyed across the phase
void attachBlurSweep(Ctx &c, BoundingBox * const box, const int f0,
                     const int f1, const qreal from, const qreal to) {
    const auto eff = addEffect(box, RasterEffectType::BLUR);
    if (auto *r = qparam(eff, {"radius", "半径"})) {
        r->setCurrentBaseValue(to);
        bakeSpan(r, c, f0, f1, [from, to](const qreal p) {
            return from + (to - from) * p;
        });
    }
}

void applyCutAnims(Ctx &c, const QJsonObject &cut,
                   const QList<TextBox*> &boxes,
                   const int fStart, const int fEnd,
                   const qreal inDur, const qreal outDur,
                   ContainerBox * const group) {
    const QString enterKey = cut.value(QStringLiteral("enter")).toString();
    const QString exitKey = cut.value(QStringLiteral("exit")).toString();
    const QString holdKey = cut.value(QStringLiteral("hold")).toString();
    const int fInEnd = fStart + qRound(inDur * c.fps);
    const int fOutStart = fEnd - qRound(outDur * c.fps);
    const quint32 seed = quint32(cut.value(QStringLiteral("seed"))
                                 .toInt(1));

    // ---- structural enter recipes (box-level, layered under the
    // per-letter presets): mask reveals, char scramble, echo trails,
    // tracking squeeze, glitch bursts
    const auto eq = [&enterKey](const char *s) {
        return enterKey == QLatin1String(s);
    };
    if (!boxes.isEmpty()) {
        TextBox * const main = boxes.first();
        const QString mainText = main->getCurrentValue();
        const int n = qMax(1, mainText.count() -
                           mainText.count(QLatin1Char('\n')));
        const qreal W = main->getFontSize() * 0.62 * n
                + main->getFontSize() * 0.6;
        const int lineCount = mainText.count(QLatin1Char('\n')) + 1;
        const qreal H = main->getFontSize() * 1.15 * lineCount;
        const int f1 = qMax(fStart + 2, fInEnd);
        if (eq("riseMask") || eq("splitJoin")) {
            maskRevealRect(c, group, main, fStart, f1, W, H, true, 1);
        } else if (eq("dropMask")) {
            maskRevealRect(c, group, main, fStart, f1, W, H, true, -1);
        } else if (eq("vSlice") || eq("shutter") || eq("blinds")
                   || eq("tyLineWipe") || eq("checker")) {
            maskRevealRect(c, group, main, fStart, f1, W, H, false, 1);
        } else if (eq("slideL")) {
            maskRevealRect(c, group, main, fStart, f1, W, H, false, -1);
        } else if (eq("iris") || eq("circleIn")) {
            maskRevealIris(c, group, main, fStart, f1,
                           qSqrt(W * W + H * H) * 0.6);
        } else if (eq("scramble") || eq("decode")) {
            scrambleKeys(c, main, fStart, f1, seed);
        } else if (eq("echoIn") || eq("trail")) {
            echoTrail(c, group, main, fStart, fInEnd - fStart,
                      main->getFontSize(), QColor(255, 255, 255), 3);
        } else if (eq("trackIn") || eq("trackOut")) {
            // letter-spacing squeeze approximated with an X scale-in
            auto *sc = main->getTransformAnimator()->getScaleAnimator();
            auto *sx = sc->getXAnimator();
            sx->saveValueToKey(fStart, 0.45);
            sx->saveValueToKey(f1, 1.0);
        } else if (eq("glitchIn")) {
            if (const auto eff = addEffect(main,
                                           RasterEffectType::GLITCH)) {
                if (auto *a = qparam(eff, {"intensity"})) {
                    pulseParam(a, c,
                               cut.value(QStringLiteral("start"))
                               .toDouble(), 0.35, 80, 0);
                }
            }
        }
    }

    const QString enterId = enterPreset(enterKey, c);
    const bool blurEnter = enterKey == QStringLiteral("blur");
    for (int i = 0; i < boxes.size(); i++) {
        auto *box = boxes.at(i);
        const int sf = fStart + (boxes.size() > 1
                ? qRound(qreal(i) / boxes.size() * qRound(inDur * c.fps) * 0.4)
                : 0);
        if (!enterId.isEmpty()) {
            applyPreset(box, enterId, sf, c, qMax(0.05, inDur), false);
        }
        if (blurEnter && fInEnd > fStart + 1) {
            attachBlurSweep(c, box, fStart, fInEnd, 30, 0);
        }
    }
    const QString holdId = holdPreset(holdKey, c);
    if (!holdId.isEmpty()) {
        for (auto *box : boxes) {
            applyPreset(box, holdId, fStart, c, 1.0, false);
        }
    }
    const QString exitId = exitPreset(exitKey, c);
    const bool blurExit = exitKey == QStringLiteral("blur");
    for (int i = 0; i < boxes.size(); i++) {
        auto *box = boxes.at(i);
        const int sf = fOutStart - (boxes.size() > 1
                ? qRound(qreal(boxes.size() - 1 - i) / boxes.size()
                         * qRound(outDur * c.fps) * 0.3) : 0);
        if (!exitId.isEmpty() && fOutStart > fInEnd - 1) {
            applyPreset(box, exitId, sf, c, qMax(0.05, outDur), true);
        }
        if (blurExit && fOutStart < fEnd - 1) {
            attachBlurSweep(c, box, fOutStart, fEnd, 0, 30);
        }
    }
}

// ======================================================================
// decor recipes (core 15 + wa approximations)
// ======================================================================

// returns true when the key was handled
bool buildDecor(Ctx &c, ContainerBox * const group, const QJsonObject &d,
                const QColor &fg, const QColor &accent, const QColor &sub,
                const QPointF &anchor, const qreal mainSize) {
    const QString id = d.value(QStringLiteral("id")).toString();
    QRandomGenerator rng(quint32(d.value(QStringLiteral("seed"))
                                      .toInt(1)) ^ 0xBEEF);
    const auto has = [&id](const char *s) { return id == QLatin1String(s); };
    const qreal alpha = 1.0;
    auto rect = [&](const QRectF &r, const QColor &col,
                    const qreal rx = 0, const qreal ry = 0) {
        auto *b = mkRect(group, r, col, rx, ry);
        opacityAnim(b)->setCurrentBaseValue(alpha * 100);
        return b;
    };
    auto ringShape = [&](const QPointF &ctr, const qreal rad,
                         const QColor &col, const qreal w) {
        auto *ci = mkCircle(group, ctr, rad, QColor());
        ci->getStrokeSettings()->setCurrentColor(col);
        ci->getStrokeSettings()->getStrokeWidthAnimator()
                ->setCurrentBaseValue(w);
        opacityAnim(ci)->setCurrentBaseValue(alpha * 100);
        return ci;
    };

    if (has("grid") || has("asanoha") || has("monoGrid")) {
        const qreal g = c.ch / 8;
        for (qreal x = c.cw * 0.1; x < c.cw; x += g) {
            rect(QRectF(x, 0, 1.5, c.ch), sub);
        }
        for (qreal y = c.ch * 0.1; y < c.ch; y += g) {
            rect(QRectF(0, y, c.cw, 1.5), sub);
        }
        return true;
    }
    if (has("stripes") || has("bars")) {
        for (int k = 0; k < 4; k++) {
            const qreal y = c.ch * (0.12 + 0.24 * k);
            rect(QRectF(0, y, c.cw, c.ch * 0.02 + k * 2), accent);
        }
        return true;
    }
    if (has("blobs") || has("kasumi") || has("namiGashira")) {
        for (int k = 0; k < 4; k++) {
            auto *ci = mkCircle(group,
                                QPointF(c.cw * rng.generateDouble(),
                                        c.ch * rng.generateDouble()),
                                c.ch * (0.06 + 0.1 * rng.generateDouble()),
                                sub);
            opacityAnim(ci)->setCurrentBaseValue(18);
        }
        return true;
    }
    if (has("shapes") || has("kamon")) {
        ringShape(QPointF(c.cw * 0.12, c.ch * 0.2), c.ch * 0.05, accent, 3);
        ringShape(QPointF(c.cw * 0.88, c.ch * 0.8), c.ch * 0.07, accent, 3);
        rect(QRectF(c.cw * 0.85, c.ch * 0.15, c.ch * 0.08, c.ch * 0.08),
             QColor(), c.ch * 0.01, c.ch * 0.01)
                ->getStrokeSettings()->setCurrentColor(accent);
        return true;
    }
    if (has("counter")) {
        const qreal s = qBound(16.0, c.ch * 0.03, 40.0);
        auto *t = mkText(group, QStringLiteral("01"),
                         QStringLiteral("Noto Sans Mono CJK JP"), 700, s,
                         accent);
        t->getTransformAnimator()->getPosAnimator()->setBaseValue(
                    QPointF(c.cw * 0.08, c.ch * 0.08));
        return true;
    }
    if (has("brackets")) {
        const qreal m = c.ch * 0.08, L = c.ch * 0.05, w = 3;
        rect(QRectF(m, m, L, w), fg); rect(QRectF(m, m, w, L), fg);
        rect(QRectF(c.cw - m - L, m, L, w), fg);
        rect(QRectF(c.cw - m - w, m, w, L), fg);
        rect(QRectF(m, c.ch - m - w, L, w), fg);
        rect(QRectF(m, c.ch - m - L, w, L), fg);
        rect(QRectF(c.cw - m - L, c.ch - m - w, L, w), fg);
        rect(QRectF(c.cw - m - w, c.ch - m - L, w, L), fg);
        return true;
    }
    if (has("rings") || has("seigaiha")) {
        for (int k = 0; k < 3; k++) {
            ringShape(QPointF(c.cw * 0.85, c.ch * 0.18),
                      c.ch * (0.05 + 0.035 * k), accent, 2.5);
        }
        return true;
    }
    if (has("dots") || has("petals")) {
        for (int gy = 0; gy < 5; gy++) {
            for (int gx = 0; gx < 9; gx++) {
                auto *ci = mkCircle(group,
                                    QPointF(c.cw * (0.08 + 0.105 * gx),
                                            c.ch * (0.12 + 0.19 * gy)),
                                    c.ch * 0.006, sub);
                opacityAnim(ci)->setCurrentBaseValue(45);
            }
        }
        return true;
    }
    if (has("arrows") || has("leaders")) {
        const qreal y = anchor.y() + mainSize * 0.8;
        rect(QRectF(anchor.x() - c.cw * 0.3, y, c.cw * 0.22, 3), accent);
        rect(QRectF(anchor.x() - c.cw * 0.09, y - 6, 12, 14), accent);
        return true;
    }
    if (has("slash")) {
        for (int k = 0; k < 3; k++) {
            auto *b = rect(QRectF(-50, c.ch * (0.3 + 0.2 * k),
                                  c.cw * 0.35, c.ch * 0.012), accent);
            b->getTransformAnimator()->getRotAnimator()
                    ->setCurrentBaseValue(-24);
        }
        return true;
    }
    if (has("sparks")) {
        const QPointF ctr(c.cw * 0.82, c.ch * 0.2);
        for (int k = 0; k < 8; k++) {
            const qreal a = qDegreesToRadians(45.0 * k);
            auto *b = rect(QRectF(ctr.x(), ctr.y(), c.ch * 0.06, 2.5), fg);
            b->getTransformAnimator()->getRotAnimator()
                    ->setCurrentBaseValue(45.0 * k);
            b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                        ctr + QPointF(qCos(a) * c.ch * 0.04,
                                      qSin(a) * c.ch * 0.04));
        }
        return true;
    }
    if (has("waveform")) {
        const int n = 16;
        for (int k = 0; k < n; k++) {
            const qreal h = c.ch * (0.02 + 0.08 * qAbs(qSin(k * 1.7)));
            rect(QRectF(c.cw * (0.3 + 0.028 * k),
                        anchor.y() + mainSize * 0.75 - h / 2, 6, h), sub);
        }
        return true;
    }
    if (has("barcode")) {
        for (int k = 0; k < 24; k++) {
            const qreal w = 2 + 5 * rng.generateDouble();
            rect(QRectF(c.cw * 0.06 + k * c.cw * 0.018, c.ch * 0.86, w,
                        c.ch * 0.05), fg);
        }
        return true;
    }
    // remaining wa parts (seal/chochin/shimenawa/sensu/tsukiKumo/momiji/
    // brushStroke/hanko/postcard/...) land on the nearest shape recipe
    c.res->substitutions++;
    c.res->notes << QStringLiteral("装饰 %1 → 几何近似").arg(id);
    ringShape(QPointF(c.cw * 0.14, c.ch * 0.82), c.ch * 0.045, accent, 3);
    return true;
}

// ======================================================================
// camera / transitions / events / hud
// ======================================================================

void applyCamera(Ctx &c, ContainerBox * const group,
                 const QJsonObject &cut, const int fStart, const int fEnd) {
    const QString cam = cut.value(QStringLiteral("cam")).toString();
    if (cam.isEmpty() || cam == QStringLiteral("none")) { return; }
    if (cam != QStringLiteral("push")) {
        c.res->substitutions++;
    }
    const auto has = [&cam](const char *s) {
        return cam.contains(QLatin1String(s), Qt::CaseInsensitive);
    };
    group->getBoxTransformAnimator()->getPivotAnimator()->setBaseValue(
                QPointF(c.cw * 0.5, c.ch * 0.5));
    auto *scale = group->getTransformAnimator()->getScaleAnimator();
    if (has("handheld") || has("shake")) {
        const auto eff = addEffect(group, RasterEffectType::SHAKE);
        if (auto *a = qparam(eff, {"amplitude", "振幅"})) {
            a->setCurrentBaseValue(4 + 6 * c.motion);
        }
        return;
    }
    qreal from = 1.0, to = 1.0 + 0.03 * c.motion;
    if (has("crash") || has("punch")) { to = 1.0 + 0.12 * c.motion; }
    if (has("pull") || has("out")) { from = to; to = 1.0; }
    bakeSpanXY(scale, c, fStart, fEnd, [from, to](const qreal p) {
        const qreal s = from + (to - from) * p;
        return QPointF(s, s);
    });
    if (has("dutch") || has("roll")) {
        auto *rot = group->getTransformAnimator()->getRotAnimator();
        bakeSpan(rot, c, fStart, fEnd, [](const qreal p) {
            return -2.5 + 5.0 * p;
        });
    }
    if (has("pan")) {
        auto *pos = group->getTransformAnimator()->getPosAnimator();
        const qreal dx = c.cw * 0.08;
        bakeSpanXY(pos, c, fStart, fEnd, [dx](const qreal p) {
            return QPointF(-dx * p, 0);
        });
    }
}

void applyTransitions(Ctx &c, const QJsonArray &cuts,
                      const QList<ContainerBox*> &cutGroups) {
    for (int i = 1; i < cuts.size(); i++) {
        const auto cut = cuts.at(i).toObject();
        const QString trans = cut.value(QStringLiteral("trans")).toString();
        const qreal transDur = cut.value(QStringLiteral("transDur"))
                .toDouble();
        if (trans.isEmpty() || trans == QStringLiteral("none")
                || transDur <= 0.01 || i >= cutGroups.size()
                || !cutGroups.at(i) || !cutGroups.at(i - 1)) { continue; }
        ContainerBox *prev = cutGroups.at(i - 1);
        ContainerBox *cur = cutGroups.at(i);
        const qreal t0 = cut.value(QStringLiteral("start")).toDouble();
        const int f0 = fSec(c, t0);
        const int fTd = qMax(1, qRound(transDur * c.fps));
        // the outgoing cut stays visible under/inside the transition
        if (const auto dr = prev->getDurationRectangle()) {
            const int newMax = qMax(dr->getMaxAbsFrame(), f0 + fTd + 1);
            dr->setFramesDuration(newMax - dr->getMinAbsFrame() + 1);
        }
        const auto has = [&trans](const char *s) {
            return trans.contains(QLatin1String(s), Qt::CaseInsensitive);
        };
        if (has("wipe") || has("iris") || has("blinds") || has("clock")
                || has("checker") || has("shutter") || has("curtain")) {
            const auto eff = addEffect(cur, RasterEffectType::WIPE);
            if (auto *tm = qparam(eff, {"time", "时间"})) {
                tm->setCurrentBaseValue(0);
                bakeSpan(tm, c, f0, f0 + fTd, [](const qreal p) {
                    return p;
                });
            }
            if (auto *sh = qparam(eff, {"sharpness"})) {
                sh->setCurrentBaseValue(0.15);
            }
        } else if (has("push") || has("cover") || has("slide")
                   || has("whip") || has("swap")) {
            const qreal dx = c.cw * 0.3;
            bakeSpanXY(prev->getTransformAnimator()->getPosAnimator(),
                       c, f0, f0 + fTd, [dx](const qreal p) {
                return QPointF(-dx * p, 0);
            });
            bakeSpanXY(cur->getTransformAnimator()->getPosAnimator(),
                       c, f0, f0 + fTd, [dx](const qreal p) {
                return QPointF(dx * (1 - p), 0);
            });
        } else if (has("zoom")) {
            const auto eff = addEffect(cur, RasterEffectType::ZOOM_BLUR);
            if (auto *a = qparam(eff, {"amount"})) {
                pulseParam(a, c, t0, transDur, 60, 0);
            }
        } else if (has("mosaic") || has("pixel")) {
            const auto eff = addEffect(cur, RasterEffectType::PIXELATE);
            if (auto *a = qparam(eff, {"pixelSize", "像素大小"})) {
                pulseParam(a, c, t0, transDur, 28, 1);
            }
        } else if (has("glitch") || has("dissolve") || has("block")) {
            const auto eff = addEffect(cur, RasterEffectType::GLITCH);
            if (auto *a = qparam(eff, {"intensity"})) {
                pulseParam(a, c, t0, transDur, 70, 0);
            }
        } else if (has("flash")) {
            const auto eff = addEffect(cur, RasterEffectType::INVERT);
            if (auto *a = qparam(eff, {"amount"})) {
                pulseParam(a, c, t0, transDur, 100, 0);
            }
        } else {
            // morph and unknown transitions: crossfade
            c.res->substitutions++;
            auto *prevOpa = opacityAnim(prev);
            auto *curOpa = opacityAnim(cur);
            prevOpa->saveValueToKey(f0, 100);
            prevOpa->saveValueToKey(f0 + fTd, 0);
            curOpa->saveValueToKey(f0, 0);
            curOpa->saveValueToKey(f0 + fTd, 100);
        }
    }
}

void applyRootFx(Ctx &c, ContainerBox * const root) {
    // constant finishing: vignette + scanlines + grain scale with the
    // plan's texture amount; a zero-texture style stays clean (which
    // also keeps headless CPU rendering free of shader-effect tasks)
    const qreal texture = c.fx.value(QStringLiteral("texture")).toDouble(0.5);
    if (texture > 0.03) {
        if (auto *v = qparam(addEffect(root, RasterEffectType::VIGNETTE),
                             {"opacity"})) {
            v->setCurrentBaseValue(30 + 20 * texture);
        }
        if (auto *s = qparam(addEffect(root, RasterEffectType::SCANLINES),
                             {"opacity"})) {
            s->setCurrentBaseValue(12 * texture);
        }
        if (auto *g = qparam(addEffect(root, RasterEffectType::FILM_GRAIN),
                             {"amount"})) {
            g->setCurrentBaseValue(14 * texture);
        }
    }

    // full-screen event pulses
    QHash<QString, QList<QJsonObject>> byType;
    for (const auto &ev : c.events) {
        const auto e = ev.toObject();
        byType[e.value(QStringLiteral("type")).toString()] << e;
    }
    const auto pulses = [&byType](const QString &t) {
        return byType.value(t);
    };
    // each screen-effect family only mounts when the plan actually
    // schedules its events (keeps effect-free scenes clean)
    if (!pulses(QStringLiteral("slice")).isEmpty()
            || !pulses(QStringLiteral("block")).isEmpty()) {
        if (const auto eff = addEffect(root, RasterEffectType::GLITCH)) {
            if (auto *a = qparam(eff, {"intensity"})) {
                a->setCurrentBaseValue(0);
                for (const auto &e : pulses(QStringLiteral("slice"))) {
                    pulseParam(a, c, e.value(QStringLiteral("t")).toDouble(),
                               e.value(QStringLiteral("dur")).toDouble(0.25),
                               85 * e.value(QStringLiteral("amp")).toDouble(0.7),
                               0);
                }
                for (const auto &e : pulses(QStringLiteral("block"))) {
                    pulseParam(a, c, e.value(QStringLiteral("t")).toDouble(),
                               e.value(QStringLiteral("dur")).toDouble(0.25),
                               70 * e.value(QStringLiteral("amp")).toDouble(0.7),
                               0);
                }
            }
        }
    }
    if (!pulses(QStringLiteral("invert")).isEmpty()) {
        if (const auto eff = addEffect(root, RasterEffectType::INVERT)) {
            if (auto *a = qparam(eff, {"amount"})) {
                a->setCurrentBaseValue(0);
                for (const auto &e : pulses(QStringLiteral("invert"))) {
                    pulseParam(a, c, e.value(QStringLiteral("t")).toDouble(),
                               e.value(QStringLiteral("dur")).toDouble(0.1),
                               100, 0);
                }
            }
        }
    }
    if (!pulses(QStringLiteral("zoom")).isEmpty()) {
        if (const auto eff = addEffect(root, RasterEffectType::ZOOM_BLUR)) {
            if (auto *a = qparam(eff, {"amount"})) {
                a->setCurrentBaseValue(0);
                for (const auto &e : pulses(QStringLiteral("zoom"))) {
                    pulseParam(a, c, e.value(QStringLiteral("t")).toDouble(),
                               e.value(QStringLiteral("dur")).toDouble(0.2),
                               55 * e.value(QStringLiteral("amp")).toDouble(0.7),
                               0);
                }
            }
        }
    }
    if (!pulses(QStringLiteral("mosaic")).isEmpty()) {
        if (const auto eff = addEffect(root, RasterEffectType::PIXELATE)) {
            if (auto *a = qparam(eff, {"pixelSize", "像素大小"})) {
                a->setCurrentBaseValue(1);
                for (const auto &e : pulses(QStringLiteral("mosaic"))) {
                    pulseParam(a, c, e.value(QStringLiteral("t")).toDouble(),
                               e.value(QStringLiteral("dur")).toDouble(0.2),
                               20, 1);
                }
            }
        }
    }
    if (!pulses(QStringLiteral("shake")).isEmpty()) {
        if (const auto eff = addEffect(root, RasterEffectType::SHAKE)) {
            if (auto *a = qparam(eff, {"amplitude", "振幅"})) {
                a->setCurrentBaseValue(0);
                for (const auto &e : pulses(QStringLiteral("shake"))) {
                    pulseParam(a, c, e.value(QStringLiteral("t")).toDouble(),
                               e.value(QStringLiteral("dur")).toDouble(0.3),
                               26 * e.value(QStringLiteral("amp")).toDouble(0.7),
                               0);
                }
            }
        }
    }
    // white flash: a full-frame overlay with opacity pulses (added last
    // inside the root's top zone by the caller — see build())
    c.res->notes << QStringLiteral("全屏事件 %1 组").arg(c.events.size());
}

void buildHud(Ctx &c, ContainerBox * const root,
              const QJsonObject &plan) {
    if (!plan.value(QStringLiteral("hud")).toBool()) { return; }
    const qreal dur = plan.value(QStringLiteral("duration")).toDouble();
    const qreal s = qBound(14.0, c.ch * 0.024, 30.0);
    const QColor col(255, 255, 255, 160);
    auto *tc = mkText(root, QStringLiteral("00:00"),
                      QStringLiteral("Noto Sans Mono CJK JP"), 500, s, col);
    tc->getTransformAnimator()->getPosAnimator()->setBaseValue(
                QPointF(c.cw * 0.07, c.ch * 0.05));
    if (auto *anim = tc->getStringAnimator()) {
        for (int sec = 0; sec <= qCeil(dur); sec++) {
            const QString label = QStringLiteral("%1:%2")
                    .arg(sec / 60, 2, 10, QLatin1Char('0'))
                    .arg(sec % 60, 2, 10, QLatin1Char('0'));
            anim->anim_appendKey(enve::make_shared<QStringKey>(
                        label, fSec(c, sec), anim));
        }
    }
    const int cutCount = plan.value(QStringLiteral("cuts")).toArray().size();
    auto *ctr = mkText(root, QStringLiteral("CUT %1/%2").arg(1).arg(cutCount),
                       QStringLiteral("Noto Sans Mono CJK JP"), 500, s, col);
    ctr->getTransformAnimator()->getPosAnimator()->setBaseValue(
                QPointF(c.cw * 0.93, c.ch * 0.05));
}

// ======================================================================
// build
// ======================================================================

void buildFlashOverlay(Ctx &c, ContainerBox * const root) {
    // white flash events + flash transitions ride on one overlay
    if (c.events.isEmpty()) { return; }
    bool hasFlash = false;
    for (const auto &ev : c.events) {
        if (ev.toObject().value(QStringLiteral("type")).toString()
                == QStringLiteral("flash")) { hasFlash = true; break; }
    }
    if (!hasFlash) { return; }
    auto *overlay = mkRect(root, QRectF(0, 0, c.cw, c.ch),
                           QColor(255, 255, 255));
    auto *opa = opacityAnim(overlay);
    opa->setCurrentBaseValue(0);
    for (const auto &ev : c.events) {
        const auto e = ev.toObject();
        if (e.value(QStringLiteral("type")).toString()
                != QStringLiteral("flash")) { continue; }
        pulseParam(opa, c, e.value(QStringLiteral("t")).toDouble(),
                   e.value(QStringLiteral("dur")).toDouble(0.12),
                   85 * e.value(QStringLiteral("amp")).toDouble(0.7), 0);
    }
}

} // namespace

bool LyricMotionNative::build(Canvas * const scene,
                              const QJsonObject &plan,
                              const QJsonObject &fonts,
                              const QString &audioPath,
                              const bool includeAudio,
                              Result * const result,
                              QString * const error,
                              const void * const rawParams) {
    if (!scene || !result) {
        if (error) { *error = QStringLiteral("invalid arguments"); }
        return false;
    }
    const auto cuts = plan.value(QStringLiteral("cuts")).toArray();
    const auto schemes = plan.value(QStringLiteral("style")).toObject()
            .value(QStringLiteral("schemes")).toArray();
    if (cuts.isEmpty() || schemes.isEmpty()) {
        if (error) { *error = QStringLiteral("plan is empty"); }
        return false;
    }

    Friction::Core::beginUndoGroupBatch();
    QString err;
    try {
        Ctx c;
        c.scene = scene;
        c.fps = scene->getFps();
        c.cw = scene->getCanvasWidth();
        c.ch = scene->getCanvasHeight();
        c.fx = plan.value(QStringLiteral("fx")).toObject();
        c.schemes = schemes;
        c.events = plan.value(QStringLiteral("events")).toArray();
        c.fonts = fonts;
        c.koma = c.fx.value(QStringLiteral("koma")).toInt(12);
        if (c.koma == 0 && c.fx.value(QStringLiteral("onTwos")).toBool(true)) {
            c.koma = 12;
        }
        c.motion = c.fx.value(QStringLiteral("motion")).toDouble(0.7);
        c.glitch = c.fx.value(QStringLiteral("glitch")).toDouble(0.55);
        c.res = result;

        // replace the previous generation. The canvas keeps raw
        // pointers to selected boxes (mSelectedBoxes) and draws canvas
        // controls through them every repaint — deselecting an item
        // only clears its own flag, so clearing the canvas selection
        // BEFORE the teardown is what actually prevents a dangling
        // pointer in drawAllCanvasControls (SIGSEGV on repaint)
        const QString groupName = QStringLiteral("歌词动画");
        scene->clearBoxesSelection();
        for (const auto &box : scene->getContainedBoxes()) {
            if (box->getBoxType() == eBoxType::layer &&
                box->prp_getName().startsWith(groupName)) {
                box->removeFromParent_k();
            }
        }
        // audio layer (once per loaded file, outside the lyric group)
        if (includeAudio && !audioPath.isEmpty()) {
            const auto sound = enve::make_shared<eIndependentSound>();
            sound->setFilePath(audioPath);
            scene->getCurrentGroup()->addContained(sound);
            sound->prp_setName(QFileInfo(audioPath).completeBaseName());
        }

        // render-replay pass: plan the same inputs through the real
        // web renderer and materialize each cut's midpoint frame
        std::unique_ptr<MatSession> mat;
        int materializedCuts = 0;
        int matFailStreak = 0;
        // the QV4 state degrades after a handful of frame renders on
        // one engine (mid-plan TypeErrors); rebuild the session when
        // that happens instead of failing every later cut
        int matRebuilds = 0;
        const auto planReplay = [&]() -> bool {
            const auto *params = static_cast<
                    const LyricMotionEngine::Params*>(rawParams);
            const auto js = [](const QString &v) {
                return QString::fromUtf8(QJsonDocument(QJsonArray{v})
                        .toJson(QJsonDocument::Compact)
                        .mid(1).chopped(1));
            };
            QString beatsSrc = QStringLiteral("null");
            if (!params->beats.isEmpty()) {
                QJsonArray arr;
                for (const qreal b : params->beats) { arr.append(b); }
                beatsSrc = QString::fromUtf8(QJsonDocument(arr)
                        .toJson(QJsonDocument::Compact));
            }
            const auto planR = mat->engine().evaluate(
                    QStringLiteral("__jzPlan(%1, %2, %3, %4, %5, %6, %7)")
                    .arg(js(params->style)).arg(params->seed)
                    .arg(QString::number(params->density, 'f', 3))
                    .arg(js(params->lyrics)).arg(beatsSrc)
                    .arg(QString::number(params->audioDuration, 'f', 4))
                    .arg(QString::number(c.fps, 'f', 3)));
            return !planR.isError();
        };
        if (rawParams) {
            mat = std::make_unique<MatSession>(c.cw, c.ch);
            if (mat->ok()) {
                if (!planReplay()) {
                    result->notes << QStringLiteral("渲染重放禁用: plan 失败");
                    mat.reset();
                }
            } else {
                result->notes << QStringLiteral("渲染重放禁用: %1")
                        .arg(mat->error().left(60));
                mat.reset();
            }
        }

        auto rootPtr = enve::make_shared<ContainerBox>(eBoxType::layer);
        scene->getCurrentGroup()->addContained(rootPtr);
        rootPtr->prp_setName(groupName);
        auto * const root = rootPtr.get();
        // z-order: last added renders at the bottom, so overlays (HUD,
        // flash) go in first and cut groups after them
        buildHud(c, root, plan);
        buildFlashOverlay(c, root);

        QList<ContainerBox*> cutGroups(cuts.size(), nullptr);
        for (int i = 0; i < cuts.size(); i++) {
            const auto cut = cuts.at(i).toObject();
            const QString text = cut.value(QStringLiteral("text")).toString();
            const qreal start = cut.value(QStringLiteral("start")).toDouble();
            const qreal end = cut.value(QStringLiteral("end")).toDouble();
            if (text.isEmpty() || end - start < 0.05) { continue; }
            const qreal inDur = cut.value(QStringLiteral("inDur"))
                    .toDouble(0.3);
            const qreal outDur = cut.value(QStringLiteral("outDur"))
                    .toDouble(0.3);
            const int fStart = fSec(c, start);
            const int fEnd = qMax(fStart + 2, fSec(c, end));

            auto * const group = mkGroup(root,
                                         QStringLiteral("切 %1").arg(i + 1));
            group->createDurationRectangle();
            if (const auto dr = group->getDurationRectangle()) {
                dr->setMinAbsFrame(fStart - 1);
                dr->setFramesDuration(fEnd - fStart + 2);
            }
            cutGroups[i] = group;

            // text core: replay-materialized composition first, the
            // hand-written recipe family as fallback
            CutText ct;
            bool usedMat = false;
            if (mat && mat->ok()) {
                const qreal midT = (start + end) * 0.5;
                mat->canvas()->clearRecording();
                mat->canvas()->setRecording(true);
                const auto rr = mat->engine().evaluate(
                            QStringLiteral("__jzRenderTime(%1)")
                            .arg(QString::number(midT, 'f', 4)));
                mat->canvas()->setRecording(false);
                bool replayOk = !rr.isError()
                        && !mat->canvas()->recordingItems().isEmpty();
                if (!replayOk) {
                    matFailStreak++;
                    // rebuild the session once per degradation streak
                    if (matFailStreak >= 2 && matRebuilds < 4) {
                        mat = std::make_unique<MatSession>(c.cw, c.ch);
                        matRebuilds++;
                        if (mat->ok() && planReplay()) {
                            mat->canvas()->clearRecording();
                            mat->canvas()->setRecording(true);
                            const auto rr2 = mat->engine().evaluate(
                                        QStringLiteral("__jzRenderTime(%1)")
                                        .arg(QString::number(midT, 'f', 4)));
                            mat->canvas()->setRecording(false);
                            replayOk = !rr2.isError()
                                    && !mat->canvas()
                                       ->recordingItems().isEmpty();
                        } else {
                            mat.reset();
                        }
                    }
                } else {
                    matFailStreak = 0;
                }
                if (replayOk) {
                    const MatResult mr = materializeCut(
                                c, group, mat->canvas()->recordingItems());
                    ct = mr.ct;
                    splitMatAnimated(ct);
                    ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
                    usedMat = mr.items > 0;
                    if (usedMat) { materializedCuts++; matFailStreak = 0; }
                }
            }
            if (!usedMat) {
                ct = buildLayout(c, group, cut);
            }
            if (ct.boxes.isEmpty()) { continue; }

            // decor (front parts were added by buildLayout before text;
            // back parts go below the text, i.e. added afterwards)
            const int schemeIdx = cut.value(QStringLiteral("scheme")).toInt();
            const auto scheme = c.schemes.at(
                        ((schemeIdx % c.schemes.size()) + c.schemes.size())
                        % c.schemes.size()).toObject();
            const QColor accent = parseColor(
                        scheme.value(QStringLiteral("accent")),
                        QColor(255, 90, 90));
            const QColor sub = parseColor(scheme.value(QStringLiteral("sub")),
                                          QColor(200, 200, 200));
            const QColor fg = parseColor(scheme.value(QStringLiteral("fg")),
                                         Qt::white);
            for (const auto &d : cut.value(QStringLiteral("decor"))
                     .toArray()) {
                buildDecor(c, group, d.toObject(), fg, accent, sub,
                           ct.anchor, ct.mainSize);
            }

            // enter / hold / exit. Materialized cuts get zero-engine
            // baked per-glyph motion (each preset expression owns a
            // QJSEngine — hundreds per scene OOM the machine); the
            // composition is already the web look, the anim stays
            // lightweight. Recipe cuts keep the full preset treatment
            if (usedMat) {
                const bool glyphAnims = bakeMatTextAnims(
                            c, ct.boxes, cut, fStart, fEnd, inDur, outDur);
                if (!glyphAnims) {
                    // fallback: whole-group fade (also the pre-glyph
                    // behavior for cuts with only decorative smalls)
                    const QString enterKey = cut.value(
                                QStringLiteral("enter")).toString();
                    const QString exitKey = cut.value(
                                QStringLiteral("exit")).toString();
                    const bool hardIn = enterKey == QStringLiteral("cut")
                            || enterKey.isEmpty();
                    const bool hardOut = exitKey == QStringLiteral("cut")
                            || exitKey.isEmpty();
                    auto *opa = opacityAnim(group);
                    const int fInEnd = fStart + qRound(inDur * c.fps);
                    const int fOutStart = fEnd - qRound(outDur * c.fps);
                    if (!hardIn && fInEnd > fStart + 1) {
                        bakeSpan(opa, c, fStart, fInEnd, [](const qreal p) {
                            return 100.0 * p;
                        });
                    }
                    if (!hardOut && fOutStart > fInEnd) {
                        bakeSpan(opa, c, fOutStart, fEnd, [](const qreal p) {
                            return 100.0 * (1.0 - p);
                        });
                    }
                }
            } else {
                applyCutAnims(c, cut, ct.boxes, fStart, fEnd, inDur,
                              outDur, group);
            }

            // camera rig on the cut group
            applyCamera(c, group, cut, fStart, fEnd);
            result->cutsBuilt++;
        }

        if (materializedCuts > 0) {
            result->notes << QStringLiteral("渲染重放物化 %1/%2 切")
                    .arg(materializedCuts).arg(result->cutsBuilt);
        }
        applyTransitions(c, cuts, cutGroups);
        applyRootFx(c, root);

        // color ghosting: static chromatic aberration over everything
        const qreal chroma = c.fx.value(QStringLiteral("chroma"))
                .toDouble(0.7);
        if (chroma > 0.05) {
            if (const auto eff = addEffect(root,
                                           RasterEffectType::CHROMATIC_ABERRATION)) {
                if (auto *a = qparam(eff, {"amount"})) {
                    a->setCurrentBaseValue(chroma * 8.0);
                }
            }
        }

        // extend the scene range to hold the whole lyric
        const qreal duration = plan.value(QStringLiteral("duration"))
                .toDouble();
        const int lastFrame = qCeil(duration * c.fps) + 1;
        const auto range = scene->getFrameRange();
        if (range.fMax < lastFrame) {
            scene->setFrameRange(FrameRange{range.fMin, lastFrame});
        }
    } catch (const std::exception &e) {
        err = QString::fromUtf8(e.what());
    }
    Friction::Core::endUndoGroupBatch();
    Document::sInstance->actionFinished();
    if (!err.isEmpty()) {
        if (error) { *error = err; }
        return false;
    }
    return true;
}
