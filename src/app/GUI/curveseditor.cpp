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

#include "curveseditor.h"

#include "RasterEffects/curveseffect.h"
#include "Animators/qrealanimator.h"
#include "Properties/comboboxproperty.h"
#include "Private/document.h"
#include "canvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QLineF>

namespace {
QColor channelColor(const int channel) {
    switch (channel) {
    case CurvesEffect::Red:   return QColor(255, 107, 107);
    case CurvesEffect::Green: return QColor(105, 224, 122);
    case CurvesEffect::Blue:  return QColor(107, 157, 255);
    default:                  return QColor(216, 216, 224);
    }
}

QString channelName(const int channel) {
    switch (channel) {
    case CurvesEffect::Red:   return QObject::tr("红");
    case CurvesEffect::Green: return QObject::tr("绿");
    case CurvesEffect::Blue:  return QObject::tr("蓝");
    default:                  return QStringLiteral("RGB");
    }
}

constexpr qreal gHandleRadius = 4.5;
constexpr qreal gGrabRadius = 12.0;
}

CurvesEditor::CurvesEditor(CurvesEffect* effect,
                           QWidget* parent) :
    QWidget(parent),
    mEffect(effect) {
    setMouseTracking(true);
    setMinimumHeight(190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    if (!mEffect) { return; }

    // repaint (and re-sync) whenever any anchor of any channel moves
    for (int c = 0; c < 4; c++) {
        const auto wrap = mEffect->getChannelAnimator(c);
        if (!wrap) { continue; }
        for (int i = 0; i < CurvesChannelAnimator::Count; i++) {
            const auto anchor = wrap->getAnchor(i);
            if (!anchor) { continue; }
            connect(anchor, &QrealAnimator::effectiveValueChanged,
                    this, qOverload<>(&QWidget::update));
        }
    }

    // follow the effect's channel combo
    const auto combo = mEffect->getChannelProperty();
    if (combo) {
        connect(combo, &ComboBoxProperty::valueChanged,
                this, &CurvesEditor::setChannel);
        setChannel(combo->getCurrentValue());
    }
}

void CurvesEditor::setChannel(const int channel) {
    mChannel = qBound(0, channel, 3);
    update();
}

QRectF CurvesEditor::plotRect() const {
    const qreal pad = gHandleRadius + 4.5;
    return QRectF(rect()).adjusted(pad, pad, -pad, -pad);
}

qreal CurvesEditor::valueFromPos(const QPointF& pos) const {
    const QRectF plot = plotRect();
    if (plot.height() <= 0.) { return 0.; }
    const qreal v = (plot.bottom() - pos.y()) / plot.height() * 255.;
    return qBound(0., v, 255.);
}

qreal CurvesEditor::inputFromPos(const QPointF& pos) const {
    const QRectF plot = plotRect();
    if (plot.width() <= 0.) { return 0.; }
    const qreal v = (pos.x() - plot.left()) / plot.width() * 255.;
    return qBound(0., v, 255.);
}

QPointF CurvesEditor::handlePos(const int i) const {
    const QRectF plot = plotRect();
    const auto wrap = mEffect ? mEffect->getChannelAnimator(mChannel) : nullptr;
    const auto anchor = wrap ? wrap->getAnchor(i) : nullptr;
    const qreal val = anchor ? anchor->getEffectiveValue() :
                               CurvesChannelAnimator::defaultY(i);
    // live input position: the three middle anchors can be dragged along x
    qreal in = CurvesChannelAnimator::inputX(i);
    if (wrap) {
        qreal xs[CurvesChannelAnimator::Count];
        wrap->inputsAt(mEffect ? mEffect->anim_getCurrentRelFrame() : 0., xs);
        in = xs[qBound(0, i, CurvesChannelAnimator::Count - 1)];
    }
    return QPointF(plot.left() + in * plot.width(),
                   plot.bottom() - val / 255. * plot.height());
}

int CurvesEditor::handleAt(const QPointF& pos) const {
    int best = -1;
    qreal bestDist = gGrabRadius;
    for (int i = 0; i < CurvesChannelAnimator::Count; i++) {
        const qreal dist = QLineF(pos, handlePos(i)).length();
        if (dist <= bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

QColor CurvesEditor::channelColor() const {
    return ::channelColor(mChannel);
}

void CurvesEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF plot = plotRect();

    // plot background + frame
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(24, 24, 30));
    p.drawRoundedRect(plot, 3, 3);

    // quarter grid
    p.setPen(QPen(QColor(52, 52, 62), 1));
    for (int g = 1; g < 4; g++) {
        const qreal f = g / 4.;
        p.drawLine(QPointF(plot.left() + f * plot.width(), plot.top()),
                   QPointF(plot.left() + f * plot.width(), plot.bottom()));
        p.drawLine(QPointF(plot.left(), plot.top() + f * plot.height()),
                   QPointF(plot.right(), plot.top() + f * plot.height()));
    }

    // identity diagonal
    p.setPen(QPen(QColor(84, 84, 94), 1, Qt::DashLine));
    p.drawLine(QPointF(plot.left(), plot.bottom()),
               QPointF(plot.right(), plot.top()));

    // the curve itself
    uint8_t lut[256];
    qreal y[CurvesChannelAnimator::Count];
    const auto wrap = mEffect ? mEffect->getChannelAnimator(mChannel) : nullptr;
    for (int i = 0; i < CurvesChannelAnimator::Count; i++) {
        const auto anchor = wrap ? wrap->getAnchor(i) : nullptr;
        y[i] = anchor ? anchor->getEffectiveValue() :
                        CurvesChannelAnimator::defaultY(i);
    }
    CurvesEffect::buildLUT(y, lut);

    p.setPen(QPen(channelColor(), 2));
    p.setBrush(Qt::NoBrush);
    p.setClipRect(plot);
    QPainterPath path;
    for (int px = 0; px <= 255; px += 2) {
        const qreal in = px / 255.;
        const QPointF pt(plot.left() + in * plot.width(),
                         plot.bottom() - lut[px] / 255. * plot.height());
        if (px == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
    }
    p.drawPath(path);
    p.setClipping(false);

    // the five anchors
    for (int i = 0; i < CurvesChannelAnimator::Count; i++) {
        const QPointF pt = handlePos(i);
        const bool active = i == mDragHandle || i == mHoverHandle;
        p.setPen(QPen(active ? QColor(255, 255, 255) : QColor(30, 30, 36),
                      active ? 1.6 : 1));
        p.setBrush(channelColor());
        p.drawEllipse(pt, gHandleRadius, gHandleRadius);
    }

    // channel tag
    p.setPen(channelColor());
    QFont fnt = font();
    fnt.setBold(true);
    p.setFont(fnt);
    p.drawText(plot.adjusted(6, 4, -6, -4).topLeft() + QPointF(0, 12),
               channelName(mChannel));
}

void CurvesEditor::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !mEffect) { return; }
    const int hit = handleAt(e->position());
    if (hit < 0) { return; }
    const auto wrap = mEffect->getChannelAnimator(mChannel);
    const auto anchor = wrap ? wrap->getAnchor(hit) : nullptr;
    if (!wrap || !anchor) { return; }
    mDragHandle = hit;
    mDragWrapper = wrap;
    wrap->prp_startTransform();
    grabMouse();
    update();
}

void CurvesEditor::mouseMoveEvent(QMouseEvent* e) {
    if (mDragHandle >= 0) {
        const auto wrap = mDragWrapper.data();
        const auto anchor = wrap ? wrap->getAnchor(mDragHandle) : nullptr;
        if (wrap && anchor) {
            // the three middle anchors move freely in both axes (like PS);
            // the two ends keep their fixed input and only move vertically
            if (CurvesChannelAnimator::isMovable(mDragHandle)) {
                if (const auto input =
                        wrap->getInputAnimator(mDragHandle)) {
                    input->setCurrentBaseValue(
                                wrap->clampInputAt(mDragHandle,
                                                   inputFromPos(e->position()),
                                                   mEffect ?
                                                       mEffect->anim_getCurrentRelFrame() :
                                                       0.));
                }
            }
            anchor->setCurrentBaseValue(valueFromPos(e->position()));
            refreshCanvas();
        }
    } else {
        const int hover = handleAt(e->position());
        if (hover != mHoverHandle) {
            mHoverHandle = hover;
            update();
        }
    }
}

void CurvesEditor::refreshCanvas() {
    // the edit view paints the cached scene frame: without an explicit
    // update request the curve edit stayed invisible on the canvas until
    // something else forced a repaint
    Document::sInstance->updateScenes();
    if (mEffect) {
        if (const auto scene = mEffect->getParentScene()) {
            scene->requestUpdate();
        }
    }
}

void CurvesEditor::mouseReleaseEvent(QMouseEvent* e) {
    Q_UNUSED(e)
    if (mDragHandle < 0) { return; }
    const auto wrap = mDragWrapper.data();
    if (wrap) {
        wrap->prp_finishTransform();
    }
    Document::sInstance->actionFinished();
    mDragHandle = -1;
    mDragWrapper = nullptr;
    releaseMouse();
    update();
}

void CurvesEditor::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !mEffect) { return; }
    const int hit = handleAt(e->position());
    if (hit < 0) { return; }
    const auto wrap = mEffect->getChannelAnimator(mChannel);
    const auto anchor = wrap ? wrap->getAnchor(hit) : nullptr;
    if (!wrap || !anchor) { return; }
    wrap->prp_startTransform();
    // a double click resets the anchor completely, input position included
    if (const auto input = wrap->getInputAnimator(hit)) {
        input->setCurrentBaseValue(
                    CurvesChannelAnimator::inputX(hit) * 255.);
    }
    anchor->setCurrentBaseValue(CurvesChannelAnimator::defaultY(hit));
    wrap->prp_finishTransform();
    refreshCanvas();
    Document::sInstance->actionFinished();
    update();
}

void CurvesEditor::leaveEvent(QEvent*) {
    if (mHoverHandle != -1) {
        mHoverHandle = -1;
        update();
    }
}
