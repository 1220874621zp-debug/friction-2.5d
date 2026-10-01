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

#ifndef HUEWHEELWIDGET_H
#define HUEWHEELWIDGET_H

#include <QWidget>

#include "smartPointers/ememory.h"

class QrealAnimator;

// AE Channel-Hue style color wheel for the Hue/Saturation effect:
// a 360 degree hue ring with a draggable indicator bound to the hue
// animator (degrees, -180..180). A drag is one undo step (start/
// finishTransform like the timeline rows); double-click resets to 0.
class HueWheelWidget : public QWidget {
    Q_OBJECT
public:
    explicit HueWheelWidget(QrealAnimator* hueAnim,
                            QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
private:
    qreal angleAt(const QPointF& pos) const;
    QPointF center() const;
    qreal ringRadius() const;

    qptr<QrealAnimator> mHueAnim;
    bool mDragging = false;
    qreal mBaseHue = 0.;
    qreal mStartAngle = 0.;
};

#endif // HUEWHEELWIDGET_H
