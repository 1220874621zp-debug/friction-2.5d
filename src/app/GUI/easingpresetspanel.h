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

#ifndef EASINGPRESETSPANEL_H
#define EASINGPRESETSPANEL_H

#include <QWidget>
#include <QList>
#include <QStringList>
#include <QVector>
#include <QPainterPath>
#include <functional>

class QGridLayout;
class KeysView;

// Self-painting button that renders an easing curve preview.
// The curve is sampled from the preset's own JS expression
// (the same definitions+script the bake pipeline executes),
// so the preview always matches the applied result.
// Emits applyRequested(presetId) when clicked.
class EasingCurveButton : public QWidget {
    Q_OBJECT
public:
    EasingCurveButton(const QString &presetId,
                      const QString &title,
                      const QVector<qreal> &samples,
                      QWidget * const parent = nullptr);

    void setChecked(const bool checked);
    const QString &presetId() const { return mPresetId; }
    const QString &title() const { return mTitle; }

signals:
    void applyRequested(const QString &presetId);
protected:
    void paintEvent(QPaintEvent * const e);
    void enterEvent(QEvent * const e);
    void leaveEvent(QEvent * const e);
    void mouseReleaseEvent(QMouseEvent * const e);
private:
    void buildPath();

    QString mPresetId;
    QString mTitle;
    QVector<qreal> mSamples;
    QPainterPath mPath;
    QPointF mStartMarker;
    QPointF mEndMarker;
    bool mHover = false;
    bool mChecked = false;
};

// Panel with a grid of easing curve presets (AE-like).
// Clicking a curve applies it to the currently selected keyframes
// by calling KeysView::graphEasingAction(), which reuses the
// existing expression/bake/undo pipeline.
class EasingPresetsWidget : public QWidget {
    Q_OBJECT
public:
    typedef std::function<KeysView*()> KeysViewGetter;

    explicit EasingPresetsWidget(QWidget * const parent = nullptr);

    void setKeysViewGetter(const KeysViewGetter &getter);

private:
    void applyPreset(const QString &presetId);

    KeysViewGetter mKeysViewGetter;
    QList<QGridLayout*> mGrids;
    QList<EasingCurveButton*> mButtons;
};

#endif // EASINGPRESETSPANEL_H
