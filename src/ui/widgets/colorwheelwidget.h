#ifndef COLORWHEELWIDGET_H
#define COLORWHEELWIDGET_H

#include <QWidget>

// classic AE/Photoshop-style color picker: hue ring + rotating SV
// triangle. Pure QPainter (no GL), the triangle is rasterized per hue
// change. Drag the ring to change hue, drag inside the triangle to
// change saturation/value; the emitter follows the standard
// start/change/finish editing pattern of the color settings panel
class ColorWheelWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheelWidget(QWidget* const parent = nullptr);

    // sync the wheel cursors to an externally-changed color
    // (no signals emitted)
    void setDisplayedColor(const QColor& color);
    QColor color() const
    { return QColor::fromHsvF(mHue, mSat, mVal, 1.); }
signals:
    void editingStarted();
    void colorChanged(const QColor& color);
    void editingFinished();
protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
private:
    enum class Pick { none, ring, triangle };

    QPointF center() const;
    qreal ringOuter() const;
    qreal ringInner() const;
    qreal triRadius() const;
    // triangle vertices rotated by hue: A = full sat/value,
    // B = no saturation / full value, C = black
    QPointF triVertex(const int i) const;

    void renderTriangle();
    bool pickAt(const QPointF& p);
    void applyFromPosition(const QPointF& p);
    void emitChange();

    qreal mHue = 0;
    qreal mSat = 0;
    qreal mVal = 1;
    Pick mPick = Pick::none;
    QImage mTriangle;
    qreal mTriangleHue = -1;
    qreal mTriangleRadius = -1;
};

#endif // COLORWHEELWIDGET_H
