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

#ifndef LEVELSEFFECTDIALOG_H
#define LEVELSEFFECTDIALOG_H

#include <QDialog>
#include <QPointer>

#include "RasterEffects/levelseffect.h"

class QComboBox;
class QDoubleSpinBox;
class LevelsSlider;
class LevelsHistogram;

// PS-style Levels editor for a single LevelsEffect instance: a
// histogram of the layer's rendered pixels, the input slider with
// its black/gamma/white handles, the output slider with its two
// handles, numeric fields and Auto/Reset. Drags map one-to-one onto
// the effect's animators (one undo step per drag, live re-render);
// everything stays keyframable from the timeline as usual.
class LevelsEffectDialog : public QDialog {
    Q_OBJECT
public:
    // one dialog per effect: re-open focuses the living instance
    static void openFor(LevelsEffect * const effect);

protected:
    void showEvent(QShowEvent * const e) override;
private:
    LevelsEffectDialog(LevelsEffect * const effect,
                       QWidget * const parent);

    void syncFromEffect();
    void refreshHistogram();
    void applyChannel(const int id);

    QrealAnimator *animatorForHandle(const int sliderIdx,
                                     const int handleIdx) const;

    LevelsEffect *effect() const { return mEffect; }

    QPointer<LevelsEffect> mEffect;
    QComboBox *mChannelCombo = nullptr;
    LevelsHistogram *mHistogram = nullptr;
    LevelsSlider *mInputSlider = nullptr;
    LevelsSlider *mOutputSlider = nullptr;
    QDoubleSpinBox *mInBlackSpin = nullptr;
    QDoubleSpinBox *mGammaSpin = nullptr;
    QDoubleSpinBox *mInWhiteSpin = nullptr;
    QDoubleSpinBox *mOutBlackSpin = nullptr;
    QDoubleSpinBox *mOutWhiteSpin = nullptr;
    QPushButton *mAutoButton = nullptr;

    QPointer<QrealAnimator> mDragAnim;
    bool mSyncGuard = false;
};

// The PS gradient bar with draggable handles below it. Input mode
// carries black/gamma/white, output mode black/white (gamma fixed
// at 1). Values are 0..255 levels, gamma 0.1..9.99; the gamma
// handle position maps logarithmically (center = 1.0, ends = 0.1
// and 9.99) exactly like the PS slider feels.
class LevelsSlider : public QWidget {
    Q_OBJECT
public:
    enum Mode { Input, Output };
    enum Handle { Black = 0, Gamma = 1, White = 2 };

    LevelsSlider(const Mode mode, QWidget * const parent = nullptr);

    void setValues(const qreal black, const qreal gamma,
                   const qreal white);
    qreal black() const { return mBlack; }
    qreal gamma() const { return mGamma; }
    qreal white() const { return mWhite; }

    void resetHandle(const int handleIdx);
signals:
    // one press/drag/release pair = one undo step on the animator
    // behind the grabbed handle
    void handlePressed(int handleIdx);
    void valuesChanged(int handleIdx, qreal black, qreal gamma, qreal white);
    void handleReleased(int handleIdx);
protected:
    void paintEvent(QPaintEvent * const e) override;
    void mousePressEvent(QMouseEvent * const e) override;
    void mouseMoveEvent(QMouseEvent * const e) override;
    void mouseReleaseEvent(QMouseEvent * const e) override;
    void mouseDoubleClickEvent(QMouseEvent * const e) override;
    void leaveEvent(QEvent * const e) override;
    QSize sizeHint() const override;
private:
    int handleAt(const QPoint &pos) const;
    qreal handleX(const int handleIdx) const;
    qreal spanX() const;
    void setHandleValue(const int handleIdx, const qreal value);

    const Mode mMode;
    qreal mBlack = 0.;
    qreal mGamma = 1.;
    qreal mWhite = 255.;
    int mDragIdx = -1;
    int mHoverIdx = -1;
};

// 256-bin histogram, log-scaled heights like PS. Channel 0 draws
// the R/G/B histograms overlaid, 1..3 the single channel.
class LevelsHistogram : public QWidget {
    Q_OBJECT
public:
    LevelsHistogram(QWidget * const parent = nullptr);

    void setHistogram(const qint64 hist[4][256], const bool hasData);
    void setChannel(const int channel) { mChannel = channel; update(); }

    bool hasData() const { return mHasData; }
    // channel 0 reads the per-bin max of R/G/B (composite curve
    // target), 1..3 the single channel - the Auto button feeds on it
    qint64 binValue(const int channel, const int bin) const
    {
        if (channel >= 1 && channel <= 3) { return mHist[channel][bin]; }
        return std::max({mHist[1][bin], mHist[2][bin], mHist[3][bin]});
    }
protected:
    void paintEvent(QPaintEvent * const e) override;
    QSize sizeHint() const override;
private:
    qint64 mHist[4][256];
    bool mHasData = false;
    int mChannel = 0;
};

#endif // LEVELSEFFECTDIALOG_H
