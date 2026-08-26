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

#include "easingpresetspanel.h"
#include "keysview.h"
#include "themesupport.h"
#include "Private/esettings.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <cmath>

// Robert Penner easing equations, normalized to t in [0, 1].
// back/elastic/bounce may overshoot the [0, 1] value range.
namespace {

const qreal PI_FRICTION = 3.14159265358979323846;
const qreal PI_HALF_FRICTION = 1.57079632679489661923;

qreal easeLinear(const qreal t) { return t; }

// Sine
qreal easeInSine(const qreal t) { return 1. - std::cos(t * PI_HALF_FRICTION); }
qreal easeOutSine(const qreal t) { return std::sin(t * PI_HALF_FRICTION); }
qreal easeInOutSine(const qreal t) { return 0.5 * (1. - std::cos(PI_FRICTION * t)); }

// Quad
qreal easeInQuad(const qreal t) { return t * t; }
qreal easeOutQuad(const qreal t) { return 1. - (1. - t) * (1. - t); }
qreal easeInOutQuad(const qreal t) {
    return t < 0.5 ? 2. * t * t : 1. - std::pow(-2. * t + 2., 2.) / 2.;
}

// Cubic
qreal easeInCubic(const qreal t) { return t * t * t; }
qreal easeOutCubic(const qreal t) { return 1. - std::pow(1. - t, 3.); }
qreal easeInOutCubic(const qreal t) {
    return t < 0.5 ? 4. * t * t * t : 1. - std::pow(-2. * t + 2., 3.) / 2.;
}

// Quart
qreal easeInQuart(const qreal t) { return t * t * t * t; }
qreal easeOutQuart(const qreal t) { return 1. - std::pow(1. - t, 4.); }
qreal easeInOutQuart(const qreal t) {
    return t < 0.5 ? 8. * t * t * t * t : 1. - std::pow(-2. * t + 2., 4.) / 2.;
}

// Quint
qreal easeInQuint(const qreal t) { return t * t * t * t * t; }
qreal easeOutQuint(const qreal t) { return 1. - std::pow(1. - t, 5.); }
qreal easeInOutQuint(const qreal t) {
    return t < 0.5 ? 16. * t * t * t * t * t : 1. - std::pow(-2. * t + 2., 5.) / 2.;
}

// Expo
qreal easeInExpo(const qreal t) {
    return t == 0. ? 0. : std::pow(2., 10. * t - 10.);
}
qreal easeOutExpo(const qreal t) {
    return t == 1. ? 1. : 1. - std::pow(2., -10. * t);
}
qreal easeInOutExpo(const qreal t) {
    if (t == 0.) { return 0.; }
    if (t == 1.) { return 1.; }
    return t < 0.5 ? std::pow(2., 20. * t - 10.) / 2.
                   : (2. - std::pow(2., -20. * t + 10.)) / 2.;
}

// Circ
qreal easeInCirc(const qreal t) { return 1. - std::sqrt(1. - t * t); }
qreal easeOutCirc(const qreal t) { return std::sqrt(1. - std::pow(t - 1., 2.)); }
qreal easeInOutCirc(const qreal t) {
    return t < 0.5 ? (1. - std::sqrt(1. - std::pow(2. * t, 2.))) / 2.
                   : (std::sqrt(1. - std::pow(-2. * t + 2., 2.)) + 1.) / 2.;
}

// Back
const qreal BACK_C1 = 1.70158;
const qreal BACK_C2 = BACK_C1 * 1.525;
const qreal BACK_C3 = BACK_C1 + 1.;
qreal easeInBack(const qreal t) { return BACK_C3 * t * t * t - BACK_C1 * t * t; }
qreal easeOutBack(const qreal t) {
    return 1. + BACK_C3 * std::pow(t - 1., 3.) + BACK_C1 * std::pow(t - 1., 2.);
}
qreal easeInOutBack(const qreal t) {
    return t < 0.5
        ? (std::pow(2. * t, 2.) * ((BACK_C2 + 1.) * 2. * t - BACK_C2)) / 2.
        : (std::pow(2. * t - 2., 2.) * ((BACK_C2 + 1.) * (2. * t - 2.) + BACK_C2) + 2.) / 2.;
}

// Elastic
const qreal ELASTIC_C4 = (2. * PI_FRICTION) / 3.;
const qreal ELASTIC_C5 = (2. * PI_FRICTION) / 4.5;
qreal easeInElastic(const qreal t) {
    if (t == 0. || t == 1.) { return t; }
    return -std::pow(2., 10. * t - 10.) * std::sin((10. * t - 10.75) * ELASTIC_C4);
}
qreal easeOutElastic(const qreal t) {
    if (t == 0. || t == 1.) { return t; }
    return std::pow(2., -10. * t) * std::sin((10. * t - 0.75) * ELASTIC_C4) + 1.;
}
qreal easeInOutElastic(const qreal t) {
    if (t == 0. || t == 1.) { return t; }
    return t < 0.5
        ? -(std::pow(2., 20. * t - 10.) * std::sin((20. * t - 11.125) * ELASTIC_C5)) / 2.
        : (std::pow(2., -20. * t + 10.) * std::sin((20. * t - 11.125) * ELASTIC_C5)) / 2. + 1.;
}

// Bounce
qreal easeOutBounce(const qreal t) {
    const qreal n1 = 7.5625;
    const qreal d1 = 2.75;
    if (t < 1. / d1) { return n1 * t * t; }
    if (t < 2. / d1) { const qreal u = t - 1.5 / d1; return n1 * u * u + 0.75; }
    if (t < 2.5 / d1) { const qreal u = t - 2.25 / d1; return n1 * u * u + 0.9375; }
    const qreal u = t - 2.625 / d1;
    return n1 * u * u + 0.984375;
}
qreal easeInBounce(const qreal t) { return 1. - easeOutBounce(1. - t); }
qreal easeInOutBounce(const qreal t) {
    return t < 0.5 ? (1. - easeOutBounce(1. - 2. * t)) / 2.
                   : (1. + easeOutBounce(2. * t - 1.)) / 2.;
}

struct EasingDef {
    const char *id;
    EasingCurveButton::EasingFunc func;
};

const QList<EasingDef> &easingDefs() {
    static const QList<EasingDef> defs = {
        {"easeInSine", &easeInSine},
        {"easeOutSine", &easeOutSine},
        {"easeInOutSine", &easeInOutSine},
        {"easeInQuad", &easeInQuad},
        {"easeOutQuad", &easeOutQuad},
        {"easeInOutQuad", &easeInOutQuad},
        {"easeInCubic", &easeInCubic},
        {"easeOutCubic", &easeOutCubic},
        {"easeInOutCubic", &easeInOutCubic},
        {"easeInQuart", &easeInQuart},
        {"easeOutQuart", &easeOutQuart},
        {"easeInOutQuart", &easeInOutQuart},
        {"easeInQuint", &easeInQuint},
        {"easeOutQuint", &easeOutQuint},
        {"easeInOutQuint", &easeInOutQuint},
        {"easeInExpo", &easeInExpo},
        {"easeOutExpo", &easeOutExpo},
        {"easeInOutExpo", &easeInOutExpo},
        {"easeInCirc", &easeInCirc},
        {"easeOutCirc", &easeOutCirc},
        {"easeInOutCirc", &easeInOutCirc},
        {"easeInBack", &easeInBack},
        {"easeOutBack", &easeOutBack},
        {"easeInOutBack", &easeInOutBack},
        {"easeInElastic", &easeInElastic},
        {"easeOutElastic", &easeOutElastic},
        {"easeInOutElastic", &easeInOutElastic},
        {"easeInBounce", &easeInBounce},
        {"easeOutBounce", &easeOutBounce},
        {"easeInOutBounce", &easeInOutBounce}
    };
    return defs;
}

EasingCurveButton::EasingFunc findEasingFunc(const QString &presetId) {
    for (const auto &def : easingDefs()) {
        if (presetId.endsWith(QLatin1String(def.id))) { return def.func; }
    }
    return &easeLinear;
}

// Group index from the preset id (0 = In, 1 = Out, 2 = In/Out).
int easingGroupIndex(const QString &presetId) {
    if (presetId.contains(QLatin1String("InOut"))) { return 2; }
    if (presetId.contains(QLatin1String("easeIn"))) { return 0; }
    if (presetId.contains(QLatin1String("easeOut"))) { return 1; }
    return -1;
}

} // namespace

EasingCurveButton::EasingCurveButton(const QString &presetId,
                                     const QString &title,
                                     const EasingFunc func,
                                     QWidget * const parent)
    : QWidget(parent)
    , mPresetId(presetId)
    , mTitle(title)
    , mFunc(func)
{
    setFixedSize(QSize(56, 56));
    setCursor(Qt::PointingHandCursor);
    setToolTip(title);
    sampleRange();
}

void EasingCurveButton::sampleRange() {
    if (!mFunc) { return; }
    qreal minY = 0.;
    qreal maxY = 1.;
    const int samples = 33;
    for (int i = 0; i <= samples; i++) {
        const qreal t = qreal(i) / samples;
        const qreal v = mFunc(t);
        minY = qMin(minY, v);
        maxY = qMax(maxY, v);
    }
    // keep some breathing room for overshooting curves
    if (minY < 0.) { minY -= 0.08 * (maxY - minY); }
    if (maxY > 1.) { maxY += 0.08 * (maxY - minY); }
    mMinY = minY;
    mMaxY = maxY;
}

void EasingCurveButton::setChecked(const bool checked) {
    if (mChecked == checked) { return; }
    mChecked = checked;
    update();
}

void EasingCurveButton::paintEvent(QPaintEvent * const e) {
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // background
    QColor bg = ThemeSupport::getThemeAlternateColor();
    if (mHover) { bg = ThemeSupport::getThemeHighlightSelectedColor(45); }
    else if (mChecked) { bg = ThemeSupport::getThemeHighlightSelectedColor(25); }
    p.fillRect(r, bg);

    // border
    QColor border = ThemeSupport::getThemeButtonBorderColor();
    if (mHover || mChecked) { border = ThemeSupport::getThemeHighlightColor(); }
    p.setPen(QPen(border, mChecked ? 2 : 1));
    p.drawRect(r);

    // faint center guides
    p.setPen(QPen(ThemeSupport::getThemeButtonBorderColor(60), 1, Qt::DotLine));
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    p.drawLine(QPointF(r.left() + 2, cy), QPointF(r.right() - 2, cy));
    p.drawLine(QPointF(cx, r.top() + 2), QPointF(cx, r.bottom() - 2));

    if (!mFunc) { return; }

    // curve
    const qreal margin = 7.;
    const qreal w = r.width() - 2. * margin;
    const qreal h = r.height() - 2. * margin;
    const qreal ySpan = mMaxY - mMinY;
    auto mapY = [this, &r, margin, h, ySpan](const qreal v) {
        return r.bottom() - margin - (v - mMinY) / ySpan * h;
    };

    QColor curveColor = ThemeSupport::getThemeColorBlue();
    if (mChecked) { curveColor = ThemeSupport::getThemeColorGreen(); }
    p.setPen(QPen(curveColor, 2.));
    p.setBrush(Qt::NoBrush);

    const int samples = 48;
    QPainterPath path;
    for (int i = 0; i <= samples; i++) {
        const qreal t = qreal(i) / samples;
        const qreal x = r.left() + margin + t * w;
        const qreal y = mapY(mFunc(t));
        if (i == 0) { path.moveTo(x, y); }
        else { path.lineTo(x, y); }
    }
    p.drawPath(path);

    // end markers
    p.setBrush(curveColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(r.left() + margin, mapY(0.)), 2., 2.);
    p.drawEllipse(QPointF(r.right() - margin, mapY(1.)), 2., 2.);
}

void EasingCurveButton::enterEvent(QEvent * const e) {
    Q_UNUSED(e)
    mHover = true;
    update();
}

void EasingCurveButton::leaveEvent(QEvent * const e) {
    Q_UNUSED(e)
    mHover = false;
    update();
}

void EasingCurveButton::mousePressEvent(QMouseEvent * const e) {
    if (e->button() == Qt::LeftButton) {
        emit applyRequested(mPresetId);
    }
    QWidget::mousePressEvent(e);
}

EasingPresetsWidget::EasingPresetsWidget(QWidget * const parent)
    : QWidget(parent)
{
    const auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const auto content = new QWidget();
    const auto contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(6, 6, 6, 6);
    contentLayout->setSpacing(10);

    const QStringList groupTitles = {tr("Ease In"),
                                     tr("Ease Out"),
                                     tr("Ease In/Out")};
    for (int i = 0; i < groupTitles.count(); i++) {
        const auto groupWidget = new QWidget();
        const auto groupLayout = new QVBoxLayout(groupWidget);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(4);

        const auto label = new QLabel(groupTitles.at(i), groupWidget);
        label->setForegroundRole(QPalette::BrightText);

        const auto grid = new QGridLayout();
        grid->setSpacing(4);

        groupLayout->addWidget(label);
        groupLayout->addLayout(grid);
        contentLayout->addWidget(groupWidget);

        mGrids.append(grid);
    }
    contentLayout->addStretch();

    const auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    setPalette(ThemeSupport::getDarkPalette());
    setAutoFillBackground(true);

    // populate from core easing presets (same source as the
    // animation toolbar easing menu)
    const auto presets = eSettings::sInstance->fExpressions.getCore(QStringLiteral("Easing"));
    for (const auto &preset : presets) {
        const int group = easingGroupIndex(preset.id);
        if (group < 0) { continue; }
        const auto button = new EasingCurveButton(preset.id, preset.title,
                                                  findEasingFunc(preset.id),
                                                  content);
        connect(button, &EasingCurveButton::applyRequested,
                this, &EasingPresetsWidget::applyPreset);
        const int idx = mGrids.at(group)->count();
        mGrids.at(group)->addWidget(button, idx / 5, idx % 5);
        mButtons.append(button);
    }
}

void EasingPresetsWidget::setKeysViewGetter(const KeysViewGetter &getter)
{
    mKeysViewGetter = getter;
}

void EasingPresetsWidget::applyPreset(const QString &presetId)
{
    // keep a single checked button (last applied)
    for (const auto button : mButtons) {
        button->setChecked(button->presetId() == presetId);
    }
    const auto keysView = mKeysViewGetter ? mKeysViewGetter() : nullptr;
    if (!keysView) { return; }
    keysView->graphEasingAction(presetId);
}
