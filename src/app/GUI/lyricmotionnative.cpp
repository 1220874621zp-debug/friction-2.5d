#include "lyricmotionnative.h"

#include "canvas.h"
#include "Private/document.h"
#include "Boxes/textbox.h"
#include "Boxes/containerbox.h"
#include "Boxes/rectangle.h"
#include "Boxes/circle.h"
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
#include <QFileInfo>
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
    qreal mainSize = 0;
    QPointF anchor;
};

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
        const int rows = params.value(QStringLiteral("rows")).toInt(2);
        const bool outline = params.value(QStringLiteral("rowStyle"))
                .toString() == QStringLiteral("outline");
        const qreal size = qMin(c.ch * 0.42 / qMax(2, rows), c.ch * 0.14);
        const QString strip = text + QStringLiteral("　") + text
                + QStringLiteral("　");
        for (int r = 0; r < rows; r++) {
            auto *b = mkText(group, strip, lf.family, lf.weight, size,
                             outline ? QColor() : textCol);
            if (outline) { setStroke(b, textCol, size * 0.06); }
            const qreal y = c.ch * (rows == 1 ? 0.5
                    : (0.28 + 0.44 * r / qreal(rows - 1)));
            const qreal span = c.cw + size * strip.length() * 0.55;
            const int f0 = fSec(c, cut.value(QStringLiteral("start"))
                                .toDouble());
            const int f1 = fSec(c, cut.value(QStringLiteral("end"))
                                .toDouble());
            auto *posX = b->getTransformAnimator()->getPosAnimator()
                    ->getXAnimator();
            const qreal dir = (r % 2 == 0) ? -1 : 1;
            bakeSpan(posX, c, f0, f1, [&, span, dir, y](const qreal p) {
                return c.cw * 0.5 + dir * (span * 0.5 - span * p);
            });
            b->getTransformAnimator()->getPosAnimator()->getYAnimator()
                    ->setCurrentBaseValue(y);
            ct.boxes << b;
            ct.mainSize = qMax(ct.mainSize, size);
        }
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
    } else if (has("tile")) {
        const qreal size = c.ch * 0.08;
        for (int gy = 0; gy < 3; gy++) {
            for (int gx = 0; gx < 3; gx++) {
                const bool center_ = gy == 1 && gx == 1;
                auto *b = mkText(group, text, lf.family, lf.weight, size,
                                 textCol);
                b->getTransformAnimator()->getPosAnimator()->setBaseValue(
                            QPointF(c.cw * (0.22 + 0.28 * gx),
                                    c.ch * (0.22 + 0.28 * gy)));
                opacityAnim(b)->setCurrentBaseValue(center_ ? 100 : 26);
                if (!center_) { ct.boxes << b; }
                else { ct.boxes.prepend(b); }
                ct.mainSize = qMax(ct.mainSize, size);
            }
        }
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
    } else if (has("condensed")) {
        const QString wrapped = splitLines(text, portrait ? 8 : 14);
        const qreal size = qMin(fitSize(wrapped, lf.family, 900, c.cw * 0.9),
                                c.ch * 0.5);
        auto *b = mkMain(wrapped, size, QPointF(c.cw * 0.5, c.ch * 0.5));
        b->getTransformAnimator()->getScaleAnimator()
                ->setBaseValue(QPointF(1.0, 0.62));
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
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
    } else {
        unknown();
        const QString wrapped = splitLines(text, portrait ? 7 : 12);
        const qreal size = qMin(fitSize(wrapped, lf.family, lf.weight,
                                        c.cw * 0.8), c.ch * 0.3);
        mkMain(wrapped, size, QPointF(c.cw * 0.5, c.ch * 0.5));
        ct.anchor = QPointF(c.cw * 0.5, c.ch * 0.5);
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
                   const qreal inDur, const qreal outDur) {
    const QString enterKey = cut.value(QStringLiteral("enter")).toString();
    const QString exitKey = cut.value(QStringLiteral("exit")).toString();
    const QString holdKey = cut.value(QStringLiteral("hold")).toString();
    const int fInEnd = fStart + qRound(inDur * c.fps);
    const int fOutStart = fEnd - qRound(outDur * c.fps);

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
                              QString * const error) {
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

        // replace the previous generation
        const QString groupName = QStringLiteral("歌词动画");
        for (const auto &box : scene->getContainedBoxes()) {
            if (box->getBoxType() == eBoxType::layer &&
                box->prp_getName().startsWith(groupName)) {
                box->setSelected(false);
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

            // text core (layout family recipe)
            const CutText ct = buildLayout(c, group, cut);
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

            // enter / hold / exit
            applyCutAnims(c, cut, ct.boxes, fStart, fEnd, inDur, outDur);

            // camera rig on the cut group
            applyCamera(c, group, cut, fStart, fEnd);
            result->cutsBuilt++;
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
