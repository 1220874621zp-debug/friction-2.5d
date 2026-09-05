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
#include "Expressions/expression.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QJSEngine>
#include <QJSValue>
#include <QDebug>

namespace {

// Samples the preset's own JS expression over a normalized 0-100 frame
// range (value domain 0 -> 1), i.e. the exact definitions+script the
// bake pipeline executes, so the preview can never diverge from the
// applied result. Returns an empty vector on failure.
QVector<qreal> sampleEasingPreset(
        QJSEngine &engine,
        const Friction::Core::ExpressionPresets::Expr &preset,
        const int samples)
{
    QVector<qreal> values;
    try {
        Expression::sAddDefinitionsTo(preset.definitions, engine);
        QString script = preset.script;
        script.replace("__START_VALUE__", QStringLiteral("0"));
        script.replace("__END_VALUE__", QStringLiteral("1"));
        script.replace("__START_FRAME__", QStringLiteral("0"));
        script.replace("__END_FRAME__", QStringLiteral("100"));
        // wrap in a function so the script's top-level return works;
        // easing presets read the current frame from "current"
        auto func = engine.evaluate( // call() is non-const
            QStringLiteral("(function(current){%1\n})").arg(script));
        if (!func.isCallable()) {
            throw std::runtime_error("script is not a function");
        }
        values.reserve(samples + 1);
        for (int i = 0; i <= samples; i++) {
            const auto v = func.call({QJSValue(qreal(i)/samples*100)});
            if (!v.isNumber()) {
                throw std::runtime_error("script returned a non-number");
            }
            values.append(v.toNumber());
        }
    } catch (const std::exception &e) {
        qWarning() << "easing preset preview failed"
                   << preset.id << e.what();
        values.clear();
    }
    return values;
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
                                     const QVector<qreal> &samples,
                                     QWidget * const parent)
    : QWidget(parent)
    , mPresetId(presetId)
    , mTitle(title)
    , mSamples(samples)
{
    setFixedSize(QSize(56, 56));
    setCursor(Qt::PointingHandCursor);
    setToolTip(title);
    if (mSamples.count() < 2) {
        // sampling failed; fall back to a linear diagonal
        mSamples = {0., 1.};
    }
    buildPath();
}

void EasingCurveButton::buildPath() {
    qreal minY = 0.;
    qreal maxY = 1.;
    for (const auto v : mSamples) {
        minY = qMin(minY, v);
        maxY = qMax(maxY, v);
    }
    // keep some breathing room for overshooting curves
    if (minY < 0.) { minY -= 0.08 * (maxY - minY); }
    if (maxY > 1.) { maxY += 0.08 * (maxY - minY); }

    const QRectF r = QRectF(QPointF(0, 0), QSizeF(size())).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal margin = 7.;
    const qreal w = r.width() - 2. * margin;
    const qreal h = r.height() - 2. * margin;
    // guard against a zero value span (degenerate constant curve)
    const qreal ySpan = qMax(maxY - minY, 1e-6);
    const auto mapY = [&r, margin, h, ySpan, minY](const qreal v) {
        return r.bottom() - margin - (v - minY) / ySpan * h;
    };

    const int n = mSamples.count();
    for (int i = 0; i < n; i++) {
        const qreal x = r.left() + margin + qreal(i) / (n - 1) * w;
        const qreal y = mapY(mSamples.at(i));
        if (i == 0) { mPath.moveTo(x, y); }
        else { mPath.lineTo(x, y); }
    }
    mStartMarker = QPointF(r.left() + margin, mapY(mSamples.first()));
    mEndMarker = QPointF(r.right() - margin, mapY(mSamples.last()));
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

    if (mPath.isEmpty()) { return; }

    // curve (cached path, only the pen color changes per state)
    QColor curveColor = ThemeSupport::getThemeColorBlue();
    if (mChecked) { curveColor = ThemeSupport::getThemeColorGreen(); }
    p.setPen(QPen(curveColor, 2.));
    p.setBrush(Qt::NoBrush);
    p.drawPath(mPath);

    // end markers
    p.setBrush(curveColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(mStartMarker, 2., 2.);
    p.drawEllipse(mEndMarker, 2., 2.);
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

void EasingCurveButton::mouseReleaseEvent(QMouseEvent * const e) {
    // apply on release inside the button, matching normal button
    // semantics (press + drag away cancels); baking is expensive
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        emit applyRequested(mPresetId);
    }
    QWidget::mouseReleaseEvent(e);
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
    // animation toolbar easing menu); a single shared engine is
    // enough - preset definition names are unique
    QJSEngine engine;
    const auto presets = eSettings::sInstance->fExpressions.getCore(QStringLiteral("Easing"));
    for (const auto &preset : presets) {
        const int group = easingGroupIndex(preset.id);
        if (group < 0) { continue; }
        const auto samples = sampleEasingPreset(engine, preset, 48);
        const auto button = new EasingCurveButton(preset.id, preset.title,
                                                  samples, content);
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
    const auto keysView = mKeysViewGetter ? mKeysViewGetter() : nullptr;
    if (!keysView) { return; }
    // highlight the button only when the preset was actually applied,
    // so the check mark always reflects real state
    if (!keysView->graphEasingAction(presetId)) { return; }
    for (const auto button : mButtons) {
        button->setChecked(button->presetId() == presetId);
    }
}
