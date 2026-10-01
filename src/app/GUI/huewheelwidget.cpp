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

#include "huewheelwidget.h"

#include "Animators/qrealanimator.h"
#include "Private/document.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

HueWheelWidget::HueWheelWidget(QrealAnimator* hueAnim,
                               QWidget* parent) :
    QWidget(parent),
    mHueAnim(hueAnim) {
    setMinimumSize(110, 110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    if (mHueAnim) {
        connect(mHueAnim, &QrealAnimator::effectiveValueChanged,
                this, qOverload<>(&QWidget::update));
    }
}

QPointF HueWheelWidget::center() const {
    return QPointF(width() / 2., height() / 2.);
}

qreal HueWheelWidget::ringRadius() const {
    return qMin(width(), height()) / 2. - 8.;
}

qreal HueWheelWidget::angleAt(const QPointF& pos) const {
    const QPointF d = pos - center();
    // 0 deg at 3 o'clock, clockwise positive - matches the wheel
    return std::atan2(d.y(), d.x()) * 180. / M_PI;
}

void HueWheelWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPointF c = center();
    const qreal r = ringRadius();
    const qreal ringW = 20.;

    // hue ring: conical gradient 0..360
    QConicalGradient grad(c, 0.);
    for (int a = 0; a <= 360; a += 30) {
        grad.setColorAt(a / 360., QColor::fromHsv(a, 225, 235));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawEllipse(c, r, r);

    // hollow center (window background)
    p.setBrush(palette().window());
    p.drawEllipse(c, r - ringW, r - ringW);

    // hue indicator dot
    const qreal hue = mHueAnim ?
                qBound(-180., mHueAnim->getEffectiveValue(), 180.) : 0.;
    const qreal rad = hue * M_PI / 180.;
    const qreal mid = r - ringW / 2.;
    const QPointF dot(c.x() + mid * std::cos(rad),
                      c.y() + mid * std::sin(rad));
    p.setPen(QPen(QColor(20, 20, 24), 2.5));
    p.setBrush(Qt::white);
    p.drawEllipse(dot, 6., 6.);

    // center readout
    p.setPen(QColor(230, 230, 235));
    QFont fnt = font();
    fnt.setBold(true);
    p.setFont(fnt);
    p.drawText(rect(), Qt::AlignCenter,
               QString::number(qRound(hue)) + QStringLiteral("°"));
}

void HueWheelWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !mHueAnim) { return; }
    mDragging = true;
    mStartAngle = angleAt(e->position());
    mBaseHue = mHueAnim->getCurrentBaseValue();
    mHueAnim->prp_startTransform();
    grabMouse();
    update();
}

void HueWheelWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!mDragging || !mHueAnim) { return; }
    // delta from the press angle, kept in (-180, 180]
    qreal d = angleAt(e->position()) - mStartAngle;
    while (d > 180.) { d -= 360.; }
    while (d <= -180.) { d += 360.; }
    // hue lives in -180..180; wrap like the angle
    qreal v = mBaseHue + d;
    while (v > 180.) { v -= 360.; }
    while (v <= -180.) { v += 360.; }
    mHueAnim->setCurrentBaseValue(v);
    Document::sInstance->updateScenes();
}

void HueWheelWidget::mouseReleaseEvent(QMouseEvent* e) {
    Q_UNUSED(e)
    if (!mDragging) { return; }
    mDragging = false;
    if (mHueAnim) {
        mHueAnim->prp_finishTransform();
    }
    Document::sInstance->actionFinished();
    releaseMouse();
    update();
}

void HueWheelWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !mHueAnim) { return; }
    // reset to 0 as one undo step
    mHueAnim->prp_startTransform();
    mHueAnim->setCurrentBaseValue(0.);
    mHueAnim->prp_finishTransform();
    Document::sInstance->updateScenes();
    Document::sInstance->actionFinished();
    update();
}
