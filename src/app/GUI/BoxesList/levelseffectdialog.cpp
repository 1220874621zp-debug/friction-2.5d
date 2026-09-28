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

#include "levelseffectdialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QToolTip>
#include <QVBoxLayout>

#include "Animators/qrealanimator.h"
#include "Properties/comboboxproperty.h"
#include "Boxes/boundingbox.h"
#include "GUI/mainwindow.h"
#include "themesupport.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"

#include <cmath>
#include <cstring>

// ---------- LevelsSlider ----------

namespace {
QPolygonF handlePoly(const qreal x, const int barBottom,
                     const int handleW, const int handleH,
                     const int handleGap)
{
    // triangle pointing up at the bar
    QPolygonF poly;
    poly << QPointF(x - handleW / 2., barBottom + handleH)
         << QPointF(x + handleW / 2., barBottom + handleH)
         << QPointF(x, barBottom + handleGap);
    return poly;
}
} // namespace

LevelsSlider::LevelsSlider(const Mode mode, QWidget * const parent) :
    QWidget(parent), mMode(mode) {}

void LevelsSlider::setCompact(const bool compact)
{
    mCompact = compact;
    if (mCompact) {
        // fit a ~20px property-row slot
        mBarH = 8;
        mHandleW = 9;
        mHandleH = 7;
        mHandleGap = 1;
    } else {
        mBarH = 12;
        mHandleW = 11;
        mHandleH = 8;
        mHandleGap = 2;
    }
    updateGeometry();
    update();
}

QSize LevelsSlider::sizeHint() const
{
    return QSize(240, mBarH + mHandleH + mHandleGap + 4);
}

qreal LevelsSlider::spanX() const
{
    return width() - mHandleW - 2;
}

qreal LevelsSlider::handleX(const int handleIdx) const
{
    const qreal x0 = mHandleW / 2. + 1;
    const qreal span = spanX();
    if (handleIdx == Black) { return x0 + mBlack / 255. * span; }
    if (handleIdx == White) { return x0 + mWhite / 255. * span; }
    const qreal p = (std::log10(qBound(LevelsEffect::sMinGamma, mGamma,
                                       LevelsEffect::sMaxGamma)) + 1.) / 2.;
    return x0 + p * span;
}

int LevelsSlider::handleAt(const QPoint &pos) const
{
    const int n = mMode == Input ? 3 : 2;
    int best = -1;
    qreal bestDist = 8.;
    for (int i = 0; i < n; i++) {
        const qreal d = std::abs(pos.x() - handleX(i));
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void LevelsSlider::setValues(const qreal black, const qreal gamma,
                             const qreal white)
{
    mBlack = black;
    mGamma = gamma;
    mWhite = white;
    update();
}

void LevelsSlider::setHandleValue(const int handleIdx, const qreal value)
{
    if (handleIdx == Black) {
        mBlack = value;
    } else if (handleIdx == White) {
        mWhite = value;
    } else {
        mGamma = value;
    }
}

void LevelsSlider::resetHandle(const int handleIdx)
{
    // double-click resets one handle PS-style, as its own undo step
    const qreal target = handleIdx == Black ? 0. :
                         handleIdx == White ? 255. : 1.;
    setHandleValue(handleIdx, target);
    emit handlePressed(handleIdx);
    emit valuesChanged(handleIdx, mBlack, mGamma, mWhite);
    emit handleReleased(handleIdx);
    update();
}

void LevelsSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF bar(mHandleW / 2. + 1, 1, spanX(), mBarH);
    QLinearGradient grad(bar.left(), 0, bar.right(), 0);
    grad.setColorAt(0, Qt::black);
    grad.setColorAt(1, Qt::white);
    p.fillRect(bar, grad);
    p.setPen(QPen(ThemeSupport::getThemeBaseDarkerColor(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(bar, 2, 2);

    const int n = mMode == Input ? 3 : 2;
    for (int i = 0; i < n; i++) {
        const qreal x = handleX(i);
        const QPolygonF poly = ::handlePoly(x, int(bar.bottom()),
                                             mHandleW, mHandleH,
                                             mHandleGap);
        QColor fill;
        QColor line;
        if (i == Black) {
            fill = QColor(10, 10, 10);
            line = QColor(210, 210, 210);
        } else if (i == White) {
            fill = QColor(245, 245, 245);
            line = QColor(40, 40, 40);
        } else {
            fill = QColor(128, 128, 128);
            line = QColor(220, 220, 220);
        }
        if (i == mDragIdx) {
            fill = ThemeSupport::getThemeHighlightColor();
        } else if (i == mHoverIdx) {
            fill = fill.lighter(160);
        }
        p.setPen(QPen(line, 1));
        p.setBrush(fill);
        p.drawPolygon(poly);
    }
}

void LevelsSlider::mousePressEvent(QMouseEvent * const e)
{
    if (e->button() != Qt::LeftButton) { return; }
    const int idx = handleAt(e->pos());
    if (idx < 0) { return; }
    mDragIdx = idx;
    emit handlePressed(idx);
    update();
}

void LevelsSlider::mouseMoveEvent(QMouseEvent * const e)
{
    if (mDragIdx >= 0) {
        const qreal x0 = mHandleW / 2. + 1;
        const qreal span = spanX();
        const qreal t = qBound(0., (e->pos().x() - x0) / span, 1.);
        if (mDragIdx == Black) {
            const qreal hi = (mMode == Input ? mWhite - 1. : mWhite);
            mBlack = qRound(qBound(0., t * 255., qMin(hi, 253.)));
        } else if (mDragIdx == White) {
            const qreal lo = (mMode == Input ? mBlack + 1. : mBlack);
            mWhite = qRound(qBound(qMax(lo, 2.), t * 255., 255.));
        } else {
            // position -> gamma, log scale centered on 1.0
            mGamma = qBound(LevelsEffect::sMinGamma,
                            std::pow(10., 2. * t - 1.),
                            LevelsEffect::sMaxGamma);
        }
        emit valuesChanged(mDragIdx, mBlack, mGamma, mWhite);
        update();
        if (mCompact) {
            const qreal v = mDragIdx == Black ? mBlack :
                            mDragIdx == White ? mWhite : mGamma;
            const QString label = mDragIdx == Gamma ?
                        tr("灰度系数 %1").arg(v, 0, 'f', 2) :
                        tr("色阶 %1").arg(qRound(v));
            QToolTip::showText(QCursor::pos(), label, this);
        }
        return;
    }
    const int idx = handleAt(e->pos());
    if (idx != mHoverIdx) {
        mHoverIdx = idx;
        setCursor(idx < 0 ? Qt::ArrowCursor : Qt::SizeHorCursor);
        update();
    }
}

void LevelsSlider::mouseReleaseEvent(QMouseEvent * const e)
{
    if (e->button() != Qt::LeftButton || mDragIdx < 0) { return; }
    const int idx = mDragIdx;
    mDragIdx = -1;
    emit handleReleased(idx);
    update();
}

void LevelsSlider::mouseDoubleClickEvent(QMouseEvent * const e)
{
    const int idx = handleAt(e->pos());
    if (idx >= 0) { resetHandle(idx); }
}

void LevelsSlider::leaveEvent(QEvent * const e)
{
    mHoverIdx = -1;
    setCursor(Qt::ArrowCursor);
    update();
    QWidget::leaveEvent(e);
}

// ---------- LevelsHistogram ----------

LevelsHistogram::LevelsHistogram(QWidget * const parent) :
    QWidget(parent)
{
    std::memset(mHist, 0, sizeof(mHist));
    setMouseTracking(true);
}

QSize LevelsHistogram::sizeHint() const
{
    return QSize(256, 100);
}

void LevelsHistogram::setHistogram(const qint64 hist[4][256],
                                   const bool hasData)
{
    std::memcpy(mHist, hist, sizeof(mHist));
    mHasData = hasData;
    update();
}

void LevelsHistogram::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), ThemeSupport::getThemeBaseDarkerColor());

    const QColor grid(ThemeSupport::getThemeBaseColor(60));
    p.setPen(QPen(grid, 1));
    for (int i = 1; i < 4; i++) {
        const int x = width() * i / 4;
        p.drawLine(x, 2, x, height() - 3);
    }
    const int hline = height() / 2;
    p.drawLine(2, hline, width() - 3, hline);

    if (!mHasData) {
        p.setPen(ThemeSupport::getThemeBaseColor(140));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("无直方图数据（画布渲染后点刷新）"));
        return;
    }

    qint64 maxc = 1;
    for (int c = 1; c < 4; c++) {
        for (int i = 0; i < 256; i++) {
            maxc = std::max(maxc, mHist[c][i]);
        }
    }
    const qreal logMax = std::log1p(qreal(maxc));
    const int innerH = height() - 4;

    // composite draws R/G/B overlaid (PS look), single channels
    // draw just their color
    const int channels[3] = {1, 2, 3};
    const QColor colors[3] = {QColor(255, 60, 60),
                              QColor(60, 200, 80),
                              QColor(70, 110, 255)};
    const int nDraw = mChannel == 0 ? 3 : 1;
    p.setClipRect(2, 2, width() - 4, height() - 4);
    for (int k = 0; k < nDraw; k++) {
        const int c = mChannel == 0 ? channels[k] : mChannel;
        QColor col = mChannel == 0 ? colors[k] : colors[mChannel - 1];
        col.setAlpha(mChannel == 0 ? 150 : 220);
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        const qreal binW = qreal(width() - 4) / 256.;
        for (int i = 0; i < 256; i++) {
            const qint64 n = mHist[c][i];
            if (n <= 0) { continue; }
            const qreal h = std::log1p(qreal(n)) / logMax * innerH;
            p.drawRect(QRectF(2 + i * binW, height() - 2 - h,
                              qMax(1., binW + 0.5), h));
        }
    }
}

// ---------- LevelsEffectDialog ----------

namespace {
// one living dialog per effect instance
QHash<LevelsEffect*, QPointer<LevelsEffectDialog>>& openDialogs()
{
    static QHash<LevelsEffect*, QPointer<LevelsEffectDialog>> sOpen;
    return sOpen;
}
} // namespace

LevelsEffectDialog::LevelsEffectDialog(LevelsEffect * const effect,
                                       QWidget * const parent) :
    QDialog(parent), mEffect(effect)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("色阶 (Levels)"));
    setMinimumWidth(320);

    const auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(6);

    // channel row
    const auto channelRow = new QHBoxLayout();
    channelRow->addWidget(new QLabel(tr("通道"), this));
    mChannelCombo = new QComboBox(this);
    mChannelCombo->addItems(effect->getChannelProperty()->getValueNames());
    channelRow->addWidget(mChannelCombo, 1);
    lay->addLayout(channelRow);

    // histogram + input slider + input fields
    mHistogram = new LevelsHistogram(this);
    lay->addWidget(mHistogram);

    mInputSlider = new LevelsSlider(LevelsSlider::Input, this);
    lay->addWidget(mInputSlider);

    const auto inRow = new QHBoxLayout();
    inRow->addWidget(new QLabel(tr("输入色阶"), this));
    mInBlackSpin = new QDoubleSpinBox(this);
    mGammaSpin = new QDoubleSpinBox(this);
    mInWhiteSpin = new QDoubleSpinBox(this);
    for (auto* sb : {mInBlackSpin, mGammaSpin, mInWhiteSpin}) {
        sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
        inRow->addWidget(sb, 1);
    }
    mInBlackSpin->setRange(0., 253.);
    mInBlackSpin->setDecimals(0);
    mGammaSpin->setRange(LevelsEffect::sMinGamma, LevelsEffect::sMaxGamma);
    mGammaSpin->setDecimals(2);
    mGammaSpin->setSingleStep(0.05);
    mInWhiteSpin->setRange(2., 255.);
    mInWhiteSpin->setDecimals(0);
    lay->addLayout(inRow);

    // output slider + output fields
    mOutputSlider = new LevelsSlider(LevelsSlider::Output, this);
    lay->addWidget(mOutputSlider);

    const auto outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(tr("输出色阶"), this));
    mOutBlackSpin = new QDoubleSpinBox(this);
    mOutWhiteSpin = new QDoubleSpinBox(this);
    for (auto* sb : {mOutBlackSpin, mOutWhiteSpin}) {
        sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
        sb->setDecimals(0);
        outRow->addWidget(sb, 1);
    }
    mOutBlackSpin->setRange(0., 254.);
    mOutWhiteSpin->setRange(1., 255.);
    lay->addLayout(outRow);

    // buttons
    const auto btnRow = new QHBoxLayout();
    mAutoButton = new QPushButton(tr("自动"), this);
    mAutoButton->setToolTip(tr("按直方图自动剪切黑白输入场"));
    const auto resetButton = new QPushButton(tr("重置"), this);
    const auto refreshButton = new QPushButton(tr("↻ 直方图"), this);
    refreshButton->setToolTip(tr("重新统计当前图层渲染画面的直方图"));
    btnRow->addWidget(mAutoButton);
    btnRow->addWidget(resetButton);
    btnRow->addStretch(1);
    btnRow->addWidget(refreshButton);
    lay->addLayout(btnRow);

    connect(mChannelCombo, qOverload<int>(&QComboBox::activated),
            this, &LevelsEffectDialog::applyChannel);
    connect(effect->getChannelProperty(), &ComboBoxProperty::valueChanged,
            this, [this](const int id) {
        if (mSyncGuard) { return; }
        mSyncGuard = true;
        mChannelCombo->setCurrentIndex(id);
        mHistogram->setChannel(id);
        mSyncGuard = false;
    });

    // animator -> widget sync (timeline scrubbing, keyframes, undo)
    const auto syncWidgets = [this]() {
        if (mSyncGuard || !mEffect) { return; }
        syncFromEffect();
    };
    connect(effect->getInBlackAnimator(),
            &QrealAnimator::effectiveValueChanged, this, syncWidgets);
    connect(effect->getGammaAnimator(),
            &QrealAnimator::effectiveValueChanged, this, syncWidgets);
    connect(effect->getInWhiteAnimator(),
            &QrealAnimator::effectiveValueChanged, this, syncWidgets);
    connect(effect->getOutBlackAnimator(),
            &QrealAnimator::effectiveValueChanged, this, syncWidgets);
    connect(effect->getOutWhiteAnimator(),
            &QrealAnimator::effectiveValueChanged, this, syncWidgets);

    const auto setAnim = [](QrealAnimator * const a, const qreal v) {
        a->prp_startTransform();
        a->setCurrentBaseValue(v);
        a->prp_finishTransform();
    };

    // slider drags: one undo step per drag on the grabbed animator
    const auto sliderPressed = [this](const int handleIdx) {
        if (!mEffect || mDragAnim) { return; }
        const bool isInput = sender() == mInputSlider;
        QrealAnimator* const a = animatorForHandle(
                    isInput ? LevelsSlider::Input
                            : LevelsSlider::Output, handleIdx);
        if (!a) { return; }
        mDragAnim = a;
        a->prp_startTransform();
    };
    const auto sliderChanged = [this, setAnim](
            const int handleIdx, const qreal black,
            const qreal gamma, const qreal white) {
        if (!mEffect) { return; }
        const bool isInput = sender() == mInputSlider;
        if (mDragAnim) {
            // live drag: push straight into the transforming animator
            qreal v = handleIdx == LevelsSlider::Black ? black :
                      handleIdx == LevelsSlider::White ? white : gamma;
            if (handleIdx == LevelsSlider::Black) {
                v = qBound(0., v, isInput ? 253. : 254.);
            } else if (handleIdx == LevelsSlider::White) {
                v = qBound(isInput ? 2. : 1., v, 255.);
            } else {
                v = qBound(LevelsEffect::sMinGamma, v,
                           LevelsEffect::sMaxGamma);
            }
            mDragAnim->setCurrentBaseValue(v);
        } else {
            // double-click reset path (no drag session): one clean
            // undo step on the reset handle's animator
            QrealAnimator* const a = animatorForHandle(
                        isInput ? LevelsSlider::Input
                                : LevelsSlider::Output, handleIdx);
            if (!a) { return; }
            if (handleIdx == LevelsSlider::Black) {
                setAnim(a, qBound(0., black, 255.));
            } else if (handleIdx == LevelsSlider::White) {
                setAnim(a, qBound(0., white, 255.));
            } else {
                setAnim(a, qBound(LevelsEffect::sMinGamma, gamma,
                                  LevelsEffect::sMaxGamma));
            }
        }
    };
    const auto sliderReleased = [this](const int) {
        if (mDragAnim) {
            mDragAnim->prp_finishTransform();
            mDragAnim.clear();
        }
    };
    connect(mInputSlider, &LevelsSlider::handlePressed,
            this, sliderPressed);
    connect(mOutputSlider, &LevelsSlider::handlePressed,
            this, sliderPressed);
    connect(mInputSlider, &LevelsSlider::valuesChanged,
            this, sliderChanged);
    connect(mOutputSlider, &LevelsSlider::valuesChanged,
            this, sliderChanged);
    connect(mInputSlider, &LevelsSlider::handleReleased,
            this, sliderReleased);
    connect(mOutputSlider, &LevelsSlider::handleReleased,
            this, sliderReleased);

    // numeric fields
    connect(mInBlackSpin, &QDoubleSpinBox::editingFinished, this,
            [this, setAnim]() {
        if (!mEffect) { return; }
        const qreal b = mInBlackSpin->value();
        const qreal w = mEffect->getInWhiteAnimator()->getCurrentBaseValue();
        setAnim(mEffect->getInBlackAnimator(),
                qBound(0., qMin(b, w - 1.), 253.));
        syncFromEffect();
    });
    connect(mGammaSpin, &QDoubleSpinBox::editingFinished, this,
            [this, setAnim]() {
        if (!mEffect) { return; }
        setAnim(mEffect->getGammaAnimator(),
                qBound(LevelsEffect::sMinGamma, mGammaSpin->value(),
                       LevelsEffect::sMaxGamma));
        syncFromEffect();
    });
    connect(mInWhiteSpin, &QDoubleSpinBox::editingFinished, this,
            [this, setAnim]() {
        if (!mEffect) { return; }
        const qreal w = mInWhiteSpin->value();
        const qreal b = mEffect->getInBlackAnimator()->getCurrentBaseValue();
        setAnim(mEffect->getInWhiteAnimator(),
                qBound(2., qMax(w, b + 1.), 255.));
        syncFromEffect();
    });
    connect(mOutBlackSpin, &QDoubleSpinBox::editingFinished, this,
            [this, setAnim]() {
        if (!mEffect) { return; }
        const qreal b = mOutBlackSpin->value();
        const qreal w = mEffect->getOutWhiteAnimator()->getCurrentBaseValue();
        setAnim(mEffect->getOutBlackAnimator(),
                qBound(0., qMin(b, w), 254.));
        syncFromEffect();
    });
    connect(mOutWhiteSpin, &QDoubleSpinBox::editingFinished, this,
            [this, setAnim]() {
        if (!mEffect) { return; }
        const qreal w = mOutWhiteSpin->value();
        const qreal b = mEffect->getOutBlackAnimator()->getCurrentBaseValue();
        setAnim(mEffect->getOutWhiteAnimator(),
                qBound(1., qMax(w, b), 255.));
        syncFromEffect();
    });

    connect(mAutoButton, &QPushButton::clicked, this, [this, setAnim]() {
        if (!mEffect || !mHistogram->hasData()) { return; }
        const int channel = mEffect->getChannelProperty()->getCurrentValue();
        // 0.1% clip on both ends, PS Auto style
        const auto binCount = [this](const int ch, const int i) -> qint64 {
            return mHistogram->binValue(ch, i);
        };
        qint64 total = 0;
        for (int i = 0; i < 256; i++) { total += binCount(channel, i); }
        if (total <= 0) { return; }
        const qint64 clip = qMax<qint64>(1, total / 1000);
        qint64 acc = 0;
        int lo = 0;
        for (int i = 0; i < 256; i++) {
            acc += binCount(channel, i);
            if (acc >= clip) { lo = i; break; }
        }
        acc = 0;
        int hi = 255;
        for (int i = 255; i >= 0; i--) {
            acc += binCount(channel, i);
            if (acc >= clip) { hi = i; break; }
        }
        if (hi <= lo + 1) { return; }
        setAnim(mEffect->getInBlackAnimator(), lo);
        setAnim(mEffect->getInWhiteAnimator(), hi);
        syncFromEffect();
    });

    connect(resetButton, &QPushButton::clicked, this, [this, setAnim]() {
        if (!mEffect) { return; }
        setAnim(mEffect->getInBlackAnimator(), 0.);
        setAnim(mEffect->getGammaAnimator(), 1.);
        setAnim(mEffect->getInWhiteAnimator(), 255.);
        setAnim(mEffect->getOutBlackAnimator(), 0.);
        setAnim(mEffect->getOutWhiteAnimator(), 255.);
        syncFromEffect();
    });

    connect(refreshButton, &QPushButton::clicked,
            this, &LevelsEffectDialog::refreshHistogram);

    // registry upkeep: a stale key would block reopening the dialog
    connect(this, &QObject::destroyed, this, [effect]() {
        openDialogs().remove(effect);
    });

    connect(effect, &QObject::destroyed, this, [this](QObject* obj) {
        openDialogs().remove(reinterpret_cast<LevelsEffect*>(obj));
        mEffect.clear();
        close();
    });

    syncFromEffect();
    refreshHistogram();
}

void LevelsEffectDialog::showEvent(QShowEvent * const e)
{
    refreshHistogram();
    QDialog::showEvent(e);
}

QrealAnimator *LevelsEffectDialog::animatorForHandle(
        const int sliderIdx, const int handleIdx) const
{
    if (!mEffect) { return nullptr; }
    if (sliderIdx == LevelsSlider::Input) {
        if (handleIdx == LevelsSlider::Black) {
            return mEffect->getInBlackAnimator();
        }
        if (handleIdx == LevelsSlider::Gamma) {
            return mEffect->getGammaAnimator();
        }
        if (handleIdx == LevelsSlider::White) {
            return mEffect->getInWhiteAnimator();
        }
    } else {
        if (handleIdx == LevelsSlider::Black) {
            return mEffect->getOutBlackAnimator();
        }
        if (handleIdx == LevelsSlider::White) {
            return mEffect->getOutWhiteAnimator();
        }
    }
    return nullptr;
}

void LevelsEffectDialog::applyChannel(const int id)
{
    if (!mEffect) { return; }
    mEffect->getChannelProperty()->setCurrentValue(id);
    mHistogram->setChannel(id);
}

void LevelsEffectDialog::syncFromEffect()
{
    if (!mEffect) { return; }
    mSyncGuard = true;
    const qreal ib = mEffect->getInBlackAnimator()->getEffectiveValue();
    const qreal g = mEffect->getGammaAnimator()->getEffectiveValue();
    const qreal iw = mEffect->getInWhiteAnimator()->getEffectiveValue();
    const qreal ob = mEffect->getOutBlackAnimator()->getEffectiveValue();
    const qreal ow = mEffect->getOutWhiteAnimator()->getEffectiveValue();
    mInputSlider->setValues(ib, g, iw);
    mOutputSlider->setValues(ob, 1., ow);
    mInBlackSpin->setValue(ib);
    mGammaSpin->setValue(g);
    mInWhiteSpin->setValue(iw);
    mOutBlackSpin->setValue(ob);
    mOutWhiteSpin->setValue(ow);
    const int ch = mEffect->getChannelProperty()->getCurrentValue();
    mChannelCombo->setCurrentIndex(ch);
    mHistogram->setChannel(ch);
    mSyncGuard = false;
}

void LevelsEffectDialog::refreshHistogram()
{
    qint64 hist[4][256];
    std::memset(hist, 0, sizeof(hist));
    bool hasData = false;

    // best-effort source: the parent layer's last canvas render. The
    // effect can sit on an adjustment layer (no own pixels), so this
    // stays a visualization aid, never an input to the math
    if (mEffect) {
        const auto box = mEffect->getFirstAncestor<BoundingBox>();
        if (box) {
            const auto rd = box->drawRenderContainer().getSrcRenderData();
            if (rd && rd->fRenderedImage) {
                const auto raster = rd->fRenderedImage->makeRasterImage();
                if (raster) {
                    SkPixmap pm;
                    if (raster->peekPixels(&pm)) {
                        SkBitmap tmp;
                        // draw into N32: the render image can be
                        // RGBA (texture path) while the loop below
                        // wants BGRA bytes
                        const SkImageInfo info = SkImageInfo::MakeN32Premul(
                                    pm.width(), pm.height());
                        if (tmp.tryAllocPixels(info)) {
                            SkCanvas c(tmp);
                            c.drawImage(raster.get(), 0, 0);
                            SkPixmap tp;
                            if (tmp.peekPixels(&tp)) {
                                // area sampling caps the pass at ~1.5M px
                                const int step = std::max(1,
                                        int(std::sqrt(
                                            qreal(tp.width()) *
                                            qreal(tp.height()) / 1.5e6)));
                                const auto px = static_cast<const uint32_t*>(
                                            tp.addr32());
                                for (int y = 0; y < tp.height(); y += step) {
                                    const auto row = px +
                                            y * tp.rowBytes() / 4;
                                    for (int x = 0; x < tp.width(); x += step) {
                                        const uint32_t c = row[x];
                                        const uint8_t a = (c >> 24) & 0xFF;
                                        if (a == 0) { continue; }
                                        // un-premultiply: PS histograms
                                        // show straight channel values
                                        uint8_t r, g, b;
                                        if (a == 255) {
                                            r = (c >> 16) & 0xFF;
                                            g = (c >> 8) & 0xFF;
                                            b = c & 0xFF;
                                        } else {
                                            r = uint8_t(qMin<qreal>(255.,
                                                qreal((c >> 16) & 0xFF) * 255. / a));
                                            g = uint8_t(qMin<qreal>(255.,
                                                qreal((c >> 8) & 0xFF) * 255. / a));
                                            b = uint8_t(qMin<qreal>(255.,
                                                qreal(c & 0xFF) * 255. / a));
                                        }
                                        hist[1][r]++;
                                        hist[2][g]++;
                                        hist[3][b]++;
                                        hasData = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    mAutoButton->setEnabled(hasData);
    mHistogram->setHistogram(hist, hasData);
}

void LevelsEffectDialog::openFor(LevelsEffect * const effect)
{
    if (!effect) { return; }
    auto& dlg = openDialogs()[effect];
    if (dlg) {
        dlg->raise();
        dlg->activateWindow();
        return;
    }
    dlg = new LevelsEffectDialog(effect, MainWindow::sGetInstance());
    dlg->show();
}
