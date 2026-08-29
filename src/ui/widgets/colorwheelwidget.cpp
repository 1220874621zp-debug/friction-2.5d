#include "colorwheelwidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QtMath>

namespace {
const qreal TAU = 6.28318530717958647692;
}

ColorWheelWidget::ColorWheelWidget(QWidget* const parent) :
    QWidget(parent) {
    setMinimumSize(180, 180);
    setMouseTracking(false);
    setCursor(Qt::CrossCursor);
}

QPointF ColorWheelWidget::center() const
{ return QPointF(width()*0.5, height()*0.5); }

qreal ColorWheelWidget::ringOuter() const
{ return qMin(width(), height())*0.5 - 2; }

qreal ColorWheelWidget::ringInner() const
{ return ringOuter() - qMax(10., qMin(width(), height())*0.075); }

qreal ColorWheelWidget::triRadius() const
{ return ringInner()*0.92; }

// hue 0 at 3 o'clock, counterclockwise (widget y grows down)
static QPointF dirForHue(const qreal hue) {
    const qreal a = TAU*hue;
    return QPointF(qCos(a), -qSin(a));
}

QPointF ColorWheelWidget::triVertex(const int i) const {
    const QPointF c = center();
    const QPointF dir = dirForHue(mHue + i/3.);
    return c + dir*triRadius();
}

void ColorWheelWidget::setDisplayedColor(const QColor& color) {
    // keep the current hue for achromatic colors: QColor::hueF is
    // undefined (negative) when saturation or value is 0, which made
    // the triangle jump and spin wildly while dragging near the
    // white/black edge
    const qreal s = color.hsvSaturationF();
    const qreal v = color.valueF();
    if(s > 0.001 && v > 0.001) {
        mHue = qBound(0., color.hueF(), 1.);
    }
    mSat = qBound(0., s, 1.);
    mVal = qBound(0., v, 1.);
    update();
}

void ColorWheelWidget::renderTriangle() {
    const qreal r = triRadius();
    if(qAbs(mTriangleHue - mHue) < 0.0005 &&
       qAbs(mTriangleRadius - r) < 0.5) return;
    mTriangleHue = mHue;
    mTriangleRadius = r;
    const int size = 2*int(r + 0.5) + 2;
    if(size < 4) { mTriangle = QImage(); return; }
    mTriangle = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
    mTriangle.fill(Qt::transparent);
    const QPointF aDir = dirForHue(mHue);
    const QPointF bDir = dirForHue(mHue + 1./3.);
    const QPointF cDir = dirForHue(mHue + 2./3.);
    const QPointF Apos = QPointF(size*0.5, size*0.5) + aDir*triRadius();
    const QPointF Bpos = QPointF(size*0.5, size*0.5) + bDir*triRadius();
    const QPointF Cpos = QPointF(size*0.5, size*0.5) + cDir*triRadius();
    for(int y = 0; y < size; y++) {
        for(int x = 0; x < size; x++) {
            const QPointF p(x + 0.5, y + 0.5);
            // barycentric coordinates of p w.r.t. A B C
            const QPointF v0 = Bpos - Apos;
            const QPointF v1 = Cpos - Apos;
            const QPointF v2 = p - Apos;
            const qreal d00 = QPointF::dotProduct(v0, v0);
            const qreal d01 = QPointF::dotProduct(v0, v1);
            const qreal d11 = QPointF::dotProduct(v1, v1);
            const qreal d20 = QPointF::dotProduct(v2, v0);
            const qreal d21 = QPointF::dotProduct(v2, v1);
            const qreal den = d00*d11 - d01*d01;
            if(qAbs(den) < 0.00001) continue;
            const qreal beta = (d11*d20 - d01*d21)/den;   // weight of B
            const qreal gamma = (d00*d21 - d01*d20)/den;  // weight of C
            const qreal alpha = 1. - beta - gamma;        // weight of A
            if(alpha < 0. || beta < 0. || gamma < 0.) continue;
            // the triangle is a LINEAR RGB interpolation between the
            // vertices A = full color, B = white, C = black:
            //   v = 1 - gamma,  s = alpha/(alpha + beta)
            const qreal sum = alpha + beta;
            const qreal sat = sum > 0.00001 ? alpha/sum : 0.;
            const QColor c = QColor::fromHsvF(
                        mHue, qBound(0., sat, 1.),
                        qBound(0., 1. - gamma, 1.));
            mTriangle.setPixel(x, y, c.rgba());
        }
    }
}

bool ColorWheelWidget::pickAt(const QPointF& p) {
    const QPointF d = p - center();
    const qreal dist = qSqrt(QPointF::dotProduct(d, d));
    if(dist >= ringInner() && dist <= ringOuter()) {
        mPick = Pick::ring;
    } else if(dist < ringInner()) {
        mPick = Pick::triangle;
    } else {
        mPick = Pick::none;
    }
    return mPick != Pick::none;
}

void ColorWheelWidget::applyFromPosition(const QPointF& p) {
    const QPointF d = p - center();
    if(mPick == Pick::ring) {
        // pixel angle to hue (counterclockwise, 0 at 3 o'clock)
        qreal hue = qAtan2(-d.y(), d.x())/TAU;
        if(hue < 0.) hue += 1.;
        if(qAbs(hue - mHue) > 0.0001) {
            mHue = hue;
            update();
        }
        return;
    }
    if(mPick != Pick::triangle) return;
    const QPointF A = triVertex(0);
    const QPointF B = triVertex(1);
    const QPointF C = triVertex(2);
    const QPointF v0 = B - A;
    const QPointF v1 = C - A;
    const QPointF v2 = p - A;
    const qreal d00 = QPointF::dotProduct(v0, v0);
    const qreal d01 = QPointF::dotProduct(v0, v1);
    const qreal d11 = QPointF::dotProduct(v1, v1);
    const qreal d20 = QPointF::dotProduct(v2, v0);
    const qreal d21 = QPointF::dotProduct(v2, v1);
    const qreal den = d00*d11 - d01*d01;
    if(qAbs(den) < 0.00001) return;
    const qreal beta = (d11*d20 - d01*d21)/den;
    const qreal gamma = (d00*d21 - d01*d20)/den;
    const qreal alpha = 1. - beta - gamma;
    if(alpha < -0.02 || beta < -0.02 || gamma < -0.02) return; // outside
    const qreal sum = alpha + beta;
    mSat = sum > 0.00001 ? qBound(0., alpha/sum, 1.) : 0.;
    mVal = qBound(0., 1. - gamma, 1.);
    update();
}

void ColorWheelWidget::emitChange() {
    emit colorChanged(color());
}

void ColorWheelWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = center();
    // hue ring: 1-degree segments colored directly (self-consistent
    // with the pick math - no conical-gradient angle conventions)
    QPen pen;
    pen.setWidthF(qMax(2., ringOuter() - ringInner()));
    const qreal rMid = (ringOuter() + ringInner())*0.5;
    const QRectF arcRect(c.x() - rMid, c.y() - rMid, 2*rMid, 2*rMid);
    for(int i = 0; i < 360; i++) {
        pen.setColor(QColor::fromHsvF(i/360., 1., 1.));
        p.setPen(pen);
        p.drawArc(arcRect, i*16, 16);
    }
    // SV triangle (rasterized per hue)
    renderTriangle();
    if(!mTriangle.isNull()) {
        p.drawImage(QPointF(c.x() - mTriangle.width()*0.5,
                            c.y() - mTriangle.height()*0.5),
                    mTriangle);
        // triangle outline
        pen.setColor(Qt::black);
        pen.setWidthF(1.);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF() << triVertex(0) << triVertex(1) << triVertex(2));
    }
    // ring cursor
    const QPointF ringDir = dirForHue(mHue);
    pen.setColor(Qt::white);
    pen.setWidthF(2.);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c + ringDir*rMid, 4., 4.);
    pen.setColor(Qt::black);
    pen.setWidthF(1.);
    p.drawEllipse(c + ringDir*rMid, 5.5, 5.5);
    // triangle cursor: P = s*v*A + (1-s)*v*B + (1-v)*C
    const QPointF A = triVertex(0);
    const QPointF B = triVertex(1);
    const QPointF C = triVertex(2);
    const QPointF pos = mSat*mVal*A
            + (1. - mSat)*mVal*B + (1. - mVal)*C;
    p.setBrush(Qt::NoBrush);
    pen.setColor(Qt::white);
    pen.setWidthF(2.);
    p.setPen(pen);
    p.drawEllipse(pos, 4., 4.);
    pen.setColor(Qt::black);
    pen.setWidthF(1.);
    p.drawEllipse(pos, 5.5, 5.5);
}

void ColorWheelWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    mTriangleHue = -1; // force re-raster at the new size
    mTriangle = QImage();
}

void ColorWheelWidget::mousePressEvent(QMouseEvent* e) {
    if(pickAt(e->pos())) {
        emit editingStarted();
        applyFromPosition(e->pos());
        emitChange();
        e->accept();
    } else e->ignore();
}

void ColorWheelWidget::mouseMoveEvent(QMouseEvent* e) {
    if(mPick == Pick::none) return;
    applyFromPosition(e->pos());
    emitChange();
    e->accept();
}

void ColorWheelWidget::mouseReleaseEvent(QMouseEvent* e) {
    if(mPick == Pick::none) return;
    mPick = Pick::none;
    emit editingFinished();
    e->accept();
}
