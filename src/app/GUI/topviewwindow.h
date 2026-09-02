#ifndef TOPVIEWWINDOW_H
#define TOPVIEWWINDOW_H

#include <QPointer>

#include "widgets/glwindow.h"
#include "smartPointers/ememory.h"
#include "canvas.h"

class CameraLayer;

// AE-style orthographic top view (the X/Z plane) in a floating
// window: layers with their 3D switch on draw as footprint segments
// at their depth (zPos), the scene camera as an icon with a view
// cone derived from the pinhole equivalent of pan/zoom/tilt. This is
// a pure vector overlay - it never rasterizes layer pixels and never
// touches the render cache. Drags map to layer x/zPos and camera
// panX/panY through the standard prp_start/finishTransform pipeline
// (undo + auto-key).
class TopViewWindow : public GLWindow {
    Q_OBJECT
public:
    TopViewWindow(Document& document, QWidget* const parent = nullptr);

    void setScene(Canvas* const scene);
    void fitToContent();
protected:
    void renderSk(SkCanvas* const canvas) override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
private:
    struct Footprint {
        QPointer<BoundingBox> fBox;
        qreal fXMin = 0.;
        qreal fXMax = 0.;
        qreal fZ = 0.;
        bool fIs3D = false;
    };
    // pinhole equivalent of the 2.5D camera transform, projected on
    // the X/Z plane (y is not visible from the top)
    struct CamPose {
        bool fValid = false;
        QPointF fPos;         // equivalent camera position, X/Z plane
        QPointF fDir;         // unit view direction on X/Z
        qreal fHalfFov = 0.;  // horizontal half fov (radians)
        qreal fPanX = 0.;
        qreal fPanY = 0.;
        qreal fZoom = 1.;
        qreal fRotX = 0.;
        qreal fRotY = 0.;
        qreal fRotZ = 0.;
    };
    enum class DragType { none, pan, layer, camera, cameraRot };

    void collectFootprints(QList<Footprint>& out) const;
    QList<Footprint> collectFootprints() const;
    CamPose cameraPose() const;
    void ensureCameraConnections();
    QPointF toDevice(const QPointF& logical) const;
    QPointF mapToTopWorld(const QPointF& device) const;
    QPointF mapToDevice(const QPointF& topWorld) const;
    int hitLayer(const QPointF& device) const;
    bool hitCamera(const QPointF& device) const;
    // ring handle around the camera body: drag to rotate rotZ (the
    // in-plane roll angle)
    bool hitCameraRing(const QPointF& device) const;
    qreal ringAngleAt(const QPointF& device) const;
    void startLayerDrag(const Footprint& fp);
    void finishDrags();
    SkFont hudFont(const qreal size) const;

    Document& mDocument;
    ConnContextPtr<Canvas> mScene;
    ConnContextPtr<CameraLayer> mCamera;  // holds the animator connections

    QMatrix mViewTransform;  // top-view world (x, z) -> device px
    bool mNeedsFit = true;

    DragType mDragType = DragType::none;
    QPointF mPressTopWorld;
    QPointF mLastDevicePos;
    QPointer<BoundingBox> mDragBox;
    qreal mDragStartWorldX = 0.;
    qreal mDragStartZ = 0.;
    QPointF mLocalXAxis{1., 0.};  // world direction of the box local +x
    qreal mDragStartPanX = 0.;
    qreal mDragStartPanY = 0.;
    qreal mDragStartRotZ = 0.;
    qreal mPressRingAngle = 0.;  // device-space handle angle, degrees
    int mHoverIndex = -1;

    // cached for hit-testing while a frame is on screen
    QList<Footprint> mFootprints;
    CamPose mPose;
};

#endif // TOPVIEWWINDOW_H
