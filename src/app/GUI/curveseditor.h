/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Roddie and contributors
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

#ifndef CURVESEDITOR_H
#define CURVESEDITOR_H

#include <QWidget>

#include "smartPointers/ememory.h"

class CurvesEffect;
class CurvesChannelAnimator;

// PS-style curve editor for CurvesEffect: shows the current channel's
// spline (master or R/G/B, chosen by the effect's channel combo) and
// lets the five fixed anchors be dragged vertically. A drag is one
// undo step (start/finishTransform on the channel wrapper, exactly
// like the timeline value rows); double-clicking an anchor resets it.
class CurvesEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurvesEditor(CurvesEffect* effect,
                          QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
private:
    void setChannel(const int channel);
    int handleAt(const QPointF& pos) const;
    QPointF handlePos(const int i) const;
    QRectF plotRect() const;
    qreal valueFromPos(const QPointF& pos) const;
    QColor channelColor() const;

    qptr<CurvesEffect> mEffect;
    int mChannel = 0;
    int mDragHandle = -1;
    int mHoverHandle = -1;
    qptr<CurvesChannelAnimator> mDragWrapper;
};

#endif // CURVESEDITOR_H
