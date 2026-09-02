#include "topviewwindow.h"

#include <QMatrix>
#include <QMouseEvent>
#include <QWheelEvent>

#include "Private/document.h"
#include "Boxes/boundingbox.h"
#include "Boxes/cameralayer.h"
#include "Boxes/containerbox.h"
#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/transformanimator.h"

// The top view maps canvas x to the screen right axis and depth z
// (zPos) to the screen down axis, like AE's Top view: the canvas
// plane is a horizontal line at z = 0, the viewer side is above it
// (negative z) and everything behind the canvas is below it.

namespace {
// Blender-inspired viewport palette (flat dark grey, muted axes,
// grey objects, orange selection)
constexpr SkColor kTopBg        = SkColorSetARGB(255, 41, 41, 41);
constexpr SkColor kGridMinor    = SkColorSetARGB(255, 52, 52, 52);
constexpr SkColor kGridMajor    = SkColorSetARGB(255, 64, 64, 64);
constexpr SkColor kAxisZ        = SkColorSetARGB(255, 80, 118, 70);
constexpr SkColor kDepthLine    = SkColorSetARGB(48, 255, 255, 255);
constexpr SkColor kObjectFill   = SkColorSetARGB(255, 95, 95, 95);
constexpr SkColor kObjectEdge   = SkColorSetARGB(255, 151, 151, 151);
constexpr SkColor kSelectOrange = SkColorSetARGB(255, 245, 160, 58);
constexpr SkColor kCamWire      = SkColorSetARGB(255, 185, 185, 185);
constexpr SkColor kCamFill      = kTopBg;
constexpr SkColor kTextMain     = SkColorSetARGB(255, 205, 205, 205);
constexpr SkColor kTextDim      = SkColorSetARGB(165, 150, 150, 155);
constexpr SkColor kTextHint     = SkColorSetARGB(120, 140, 140, 148);
constexpr SkColor kCenterDot    = SkColorSetARGB(230, 210, 210, 215);
}

TopViewWindow::TopViewWindow(Document& document,
                             QWidget* const parent)
    : GLWindow(parent)
    , mDocument(document)
{
    // pure mouse-driven auxiliary view - never steal the keyboard
    // focus (space playback, tool shortcuts) from the canvas
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setMinimumSize(280, 220);

    connect(&mDocument, &Document::activeSceneSet,
            this, [this](Canvas* const scene) { setScene(scene); });
    setScene(*mDocument.fActiveScene);
}

void TopViewWindow::setScene(Canvas* const scene)
{
    if (mScene == scene) { return; }
    auto& conn = mScene.assign(scene);
    if (mScene) {
        conn << connect(mScene, &Canvas::requestUpdate,
                        this, qOverload<>(&TopViewWindow::update));
        conn << connect(mScene, &Canvas::destroyed,
                        this, [this]() { setScene(nullptr); });
    }
    mNeedsFit = true;
    mDragType = DragType::none;
    mDragBox.clear();
    update();
}

void TopViewWindow::fitToContent()
{
    if (!mScene) { return; }
    const qreal W = mScene->getCanvasWidth();
    const qreal H = mScene->getCanvasHeight();
    qreal x0 = -W * 0.12;
    qreal x1 = W * 1.12;
    qreal z0 = -2. * H;
    qreal z1 = 1.6 * H;
    const auto pose = cameraPose();
    if (pose.fValid) {
        // keep the camera visible, but do not let a huge focal
        // length blow the fit range apart
        z0 = qMin(z0, pose.fPos.y() - H * 0.5);
        x0 = qMin(x0, pose.fPos.x() - W * 0.15);
        x1 = qMax(x1, pose.fPos.x() + W * 0.15);
    }
    const auto fps = collectFootprints();
    for (const auto& fp : fps) {
        x0 = qMin(x0, fp.fXMin - W * 0.02);
        x1 = qMax(x1, fp.fXMax + W * 0.02);
        z1 = qMax(z1, fp.fZ + H * 0.35);
    }
    const qreal w = x1 - x0;
    const qreal h = z1 - z0;
    const qreal pr = devicePixelRatioF();
    const qreal sw = width() * pr;
    const qreal sh = height() * pr;
    const qreal s = qMin(sw / w, sh / h) * 0.88;
    // the screen-down axis is NEGATIVE z (viewer/camera side at the
    // bottom, depth at the top), like looking at the scene from
    // behind the camera: layers above, camera below, view up
    mViewTransform = QMatrix(s, 0., 0., -s,
                             -x0 * s + (sw - w * s) * 0.5,
                             z1 * s + (sh - h * s) * 0.5);
    update();
}

// pinhole equivalent of the 2.5D camera transform, X/Z projection:
//   d = f / zoom     camera-to-canvas distance (zoom in = closer)
//   D = (-cos(rx)sin(ry), sin(rx), -cos(rx)cos(ry))
//                    unit vector canvas center -> camera
//   C = center + pan + d * D
// pan sits inside zoom in the matrix chain, so it maps 1:1 to camera
// units (no zoom factor); the tilt rotation follows the third row of
// CameraLayer's homography (-crx*sry, srx, f)
TopViewWindow::CamPose TopViewWindow::cameraPose() const
{
    CamPose pose;
    if (!mScene) { return pose; }
    const auto cam = mScene->getCameraLayer();
    if (!cam) { return pose; }
    const int absFrame = mScene->getCurrentFrame();
    const auto val = [absFrame](QrealAnimator* const anim) {
        return anim->getEffectiveValue(
                    anim->prp_absFrameToRelFrameF(absFrame));
    };
    const qreal panX = val(cam->panXAnimator());
    const qreal panY = val(cam->panYAnimator());
    const qreal zoom = val(cam->zoomAnimator());
    const qreal rotX = val(cam->rotXAnimator());
    const qreal rotY = val(cam->rotYAnimator());
    const qreal rotZ = val(cam->rotZAnimator());
    const qreal focal = val(cam->focalAnimator());
    const qreal safeZoom = qMax(0.01, zoom);
    const qreal rx = qDegreesToRadians(rotX);
    const qreal ry = qDegreesToRadians(rotY);
    const qreal d = focal / safeZoom;
    const qreal dx = -std::cos(rx) * std::sin(ry);
    const qreal dz = -std::cos(rx) * std::cos(ry);
    const qreal cx = mScene->getCanvasWidth() * 0.5;
    pose.fPos = QPointF(cx + panX + dx * d, dz * d);
    QPointF dir(cx - pose.fPos.x(), -pose.fPos.y());
    const qreal len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
    if (len > 1.) {
        dir /= len;
    } else {
        dir = QPointF(0., 1.);
    }
    pose.fDir = dir;
    pose.fHalfFov = std::atan(mScene->getCanvasWidth() * 0.5 /
                              qMax(1., focal));
    pose.fPanX = panX;
    pose.fPanY = panY;
    pose.fZoom = zoom;
    pose.fRotX = rotX;
    pose.fRotY = rotY;
    pose.fRotZ = rotZ;
    pose.fDz = dz;
    pose.fValid = true;
    return pose;
}

void TopViewWindow::collectFootprints(QList<Footprint>& out) const
{
    if (!mScene) { return; }
    const int absFrame = mScene->getCurrentFrame();
    QList<ContainerBox*> stack;
    stack.append(mScene.data());
    while (!stack.isEmpty()) {
        auto* const group = stack.takeLast();
        const auto& boxes = group->getContainedBoxes();
        for (auto* const box : boxes) {
            const auto adv = dynamic_cast<AdvancedTransformAnimator*>(
                        box->getTransformAnimator());
            const bool is3D = adv && adv->is3DEnabled();
            const auto cont = enve_cast<ContainerBox*>(box);
            if (is3D || !cont) {
                // 3D layers/groups: footprint at their depth; plain
                // 2D layers: a gray marker line on the canvas plane
                const QMatrix T = box->getTotalTransform();
                const QRectF rel = box->getRelBoundingRect();
                QPointF corners[4] = {
                    T.map(rel.topLeft()), T.map(rel.topRight()),
                    T.map(rel.bottomLeft()), T.map(rel.bottomRight())
                };
                qreal xMin = corners[0].x();
                qreal xMax = corners[0].x();
                for (int i = 1; i < 4; i++) {
                    xMin = qMin(xMin, corners[i].x());
                    xMax = qMax(xMax, corners[i].x());
                }
                Footprint fp;
                fp.fBox = box;
                fp.fXMin = xMin;
                fp.fXMax = xMax;
                fp.fIs3D = is3D;
                fp.fZ = is3D ? adv->get3DZPosAtFrame(
                            adv->prp_absFrameToRelFrameF(absFrame)) : 0.;
                out.append(fp);
            }
            if (cont) { stack.append(cont); }
        }
    }
}

QList<TopViewWindow::Footprint> TopViewWindow::collectFootprints() const
{
    QList<Footprint> out;
    collectFootprints(out);
    return out;
}

void TopViewWindow::ensureCameraConnections()
{
    CameraLayer* const cam = mScene ? mScene->getCameraLayer() : nullptr;
    if (mCamera == cam) { return; }
    auto& conn = mCamera.assign(cam);
    if (cam) {
        QrealAnimator* const anims[] = {
            cam->panXAnimator(), cam->panYAnimator(),
            cam->zoomAnimator(), cam->rotZAnimator(),
            cam->rotXAnimator(), cam->rotYAnimator(),
            cam->focalAnimator()
        };
        for (const auto anim : anims) {
            conn << connect(anim, &Property::prp_absFrameRangeChanged,
                            this, qOverload<>(&TopViewWindow::update));
        }
    }
    update();
}

QPointF TopViewWindow::toDevice(const QPointF& logical) const
{
    const qreal pr = devicePixelRatioF();
    return QPointF(logical.x() * pr, logical.y() * pr);
}

QPointF TopViewWindow::mapToTopWorld(const QPointF& device) const
{
    return mViewTransform.inverted().map(device);
}

QPointF TopViewWindow::mapToDevice(const QPointF& topWorld) const
{
    return mViewTransform.map(topWorld);
}

int TopViewWindow::hitLayer(const QPointF& device) const
{
    const qreal pr = devicePixelRatioF();
    const qreal tol = 8. * pr;
    for (int i = mFootprints.count() - 1; i >= 0; i--) {
        const auto& fp = mFootprints.at(i);
        if (!fp.fIs3D) { continue; }
        const QPointF a = mapToDevice(QPointF(fp.fXMin, fp.fZ));
        const QPointF b = mapToDevice(QPointF(fp.fXMax, fp.fZ));
        const QPointF ab = b - a;
        const QPointF ap = device - a;
        const qreal abLen2 = ab.x() * ab.x() + ab.y() * ab.y();
        qreal t = 0.;
        if (abLen2 > 1e-9) {
            t = qBound(0., (ap.x() * ab.x() + ap.y() * ab.y()) / abLen2,
                       1.);
        }
        const QPointF proj = a + t * ab;
        const QPointF d = device - proj;
        if (d.x() * d.x() + d.y() * d.y() <= tol * tol) { return i; }
    }
    return -1;
}

bool TopViewWindow::hitCamera(const QPointF& device) const
{
    if (!mPose.fValid) { return false; }
    const qreal pr = devicePixelRatioF();
    const QPointF c = mapToDevice(mPose.fPos);
    const QPointF d = device - c;
    const qreal r = 12. * pr;
    return d.x() * d.x() + d.y() * d.y() <= r * r;
}

bool TopViewWindow::hitCameraRing(const QPointF& device) const
{
    if (!mPose.fValid) { return false; }
    const qreal pr = devicePixelRatioF();
    const QPointF c = mapToDevice(mPose.fPos);
    const double a = qDegreesToRadians(mPose.fRotZ);
    const QPointF handle(c.x() + std::cos(a) * 14. * pr,
                         c.y() + std::sin(a) * 14. * pr);
    const QPointF d = device - handle;
    const qreal r = 10. * pr;
    return d.x() * d.x() + d.y() * d.y() <= r * r;
}

qreal TopViewWindow::ringAngleAt(const QPointF& device) const
{
    const QPointF c = mapToDevice(mPose.fPos);
    return qRadiansToDegrees(std::atan2(device.y() - c.y(),
                                        device.x() - c.x()));
}

void TopViewWindow::startLayerDrag(const Footprint& fp)
{
    auto* const box = fp.fBox.data();
    if (!box) { return; }
    const auto adv = dynamic_cast<AdvancedTransformAnimator*>(
                box->getTransformAnimator());
    if (!adv) { return; }
    const QMatrix T = box->getTotalTransform();
    QPointF ax = T.map(QPointF(1., 0.)) - T.map(QPointF(0., 0.));
    if (qFuzzyIsNull(ax.manhattanLength())) { ax = QPointF(1., 0.); }
    mLocalXAxis = ax;
    mDragStartWorldX = (fp.fXMin + fp.fXMax) * 0.5;
    mDragStartZ = fp.fZ;
    mDragBox = box;
    mDragType = DragType::layer;
    // the standard pipeline carries undo + auto-key (same pair the
    // canvas gizmo Z drag uses)
    adv->startPosTransform();
    adv->start3DZTransform();
}

void TopViewWindow::finishDrags()
{
    if (mDragType == DragType::layer) {
        auto* const box = mDragBox.data();
        if (box) {
            const auto adv = dynamic_cast<AdvancedTransformAnimator*>(
                        box->getTransformAnimator());
            if (adv) {
                adv->getPosAnimator()->prp_finishTransform();
                adv->getZPosAnimator()->prp_finishTransform();
            }
        }
    } else if (mDragType == DragType::camera) {
        const auto cam = mScene ? mScene->getCameraLayer() : nullptr;
        if (cam) {
            cam->panXAnimator()->prp_finishTransform();
            cam->zoomAnimator()->prp_finishTransform();
        }
    } else if (mDragType == DragType::cameraRot) {
        const auto cam = mScene ? mScene->getCameraLayer() : nullptr;
        if (cam) { cam->rotZAnimator()->prp_finishTransform(); }
    }
    mDragType = DragType::none;
    mDragBox.clear();
    setCursor(Qt::ArrowCursor);
    if (Document::sInstance) { Document::sInstance->actionFinished(); }
    update();
}

SkFont TopViewWindow::hudFont(const qreal size) const
{
    SkFont font;
    // CJK-capable face so layer/scene names render (skia's default
    // typeface has no Chinese glyphs on Windows)
    font.setTypeface(SkTypeface::MakeFromName(
                         "Microsoft YaHei", SkFontStyle::Normal()));
    font.setSize(SkScalar(size * devicePixelRatioF()));
    return font;
}

void TopViewWindow::renderSk(SkCanvas* const canvas)
{
    const qreal pr = devicePixelRatioF();
    const SkScalar W = SkScalar(width() * pr);
    const SkScalar H = SkScalar(height() * pr);

    SkPaint bg;
    bg.setColor(kTopBg);
    canvas->drawRect(SkRect::MakeWH(W, H), bg);

    if (!mScene) {
        const auto font = hudFont(12);
        const auto txt = QString::fromUtf8("无活动场景").toStdString();
        SkPaint tp;
        tp.setColor(kTextDim);
        const auto tw = font.measureText(txt.c_str(), txt.size(),
                                         SkTextEncoding::kUTF8);
        canvas->drawString(txt.c_str(), (W - SkScalar(tw)) * 0.5f,
                           H * 0.5f, font, tp);
        return;
    }

    if (mNeedsFit) {
        mNeedsFit = false;
        fitToContent();
    }
    ensureCameraConnections();
    mFootprints = collectFootprints();
    mPose = cameraPose();

    const QMatrix& T = mViewTransform;
    const auto devX = [&T](const qreal x)
    { return SkScalar(x * T.m11() + T.dx()); };
    const auto devY = [&T](const qreal z)
    { return SkScalar(z * T.m22() + T.dy()); };
    const qreal visZa = (0. - T.dy()) / T.m22();
    const qreal visZb = (qreal(H) - T.dy()) / T.m22();
    const qreal visZMin = qMin(visZa, visZb);
    const qreal visZMax = qMax(visZa, visZb);
    const qreal visXa = (0. - T.dx()) / T.m11();
    const qreal visXb = (qreal(W) - T.dx()) / T.m11();
    const qreal visXMin = qMin(visXa, visXb);
    const qreal visXMax = qMax(visXa, visXb);

    const qreal cw = mScene->getCanvasWidth();
    const qreal ch = mScene->getCanvasHeight();

    // ---- Blender-style grid: sparse adaptive two-level square grid
    // with the muted canvas-plane line (z = 0, green) ----
    const auto labelFont = hudFont(9);
    SkPaint labelP;
    labelP.setColor(kTextDim);
    qreal step = 64. / qMax(1e-9, qAbs(T.m11()));
    const qreal p10 = std::pow(10., std::floor(std::log10(step)));
    step = step / p10 < 1.5 ? p10 :
           step / p10 < 3.5 ? 2. * p10 : 5. * p10;
    const qreal majorStep = step * 5.;
    SkPaint minor;
    minor.setColor(kGridMinor);
    minor.setStrokeWidth(SkScalar(pr));
    SkPaint major;
    major.setColor(kGridMajor);
    major.setStrokeWidth(SkScalar(pr));
    {
        const int i0 = qFloor(visXMin / step);
        const int i1 = qCeil(visXMax / step);
        for (int i = i0; i <= i1; i++) {
            const SkScalar x = devX(i * step);
            canvas->drawLine(x, 0, x, H,
                             (i % 5 + 5) % 5 == 0 ? major : minor);
        }
        const int j0 = qFloor(visZMin / step);
        const int j1 = qCeil(visZMax / step);
        for (int j = j0; j <= j1; j++) {
            const SkScalar y = devY(j * step);
            canvas->drawLine(0, y, W, y,
                             (j % 5 + 5) % 5 == 0 ? major : minor);
        }
    }
    // depth reference labels (one canvas height apart), the grid
    // itself stays Blender-clean without labels
    {
        SkPaint dl;
        dl.setColor(kDepthLine);
        dl.setStrokeWidth(SkScalar(pr));
        const int k0 = qFloor(visZMin / ch) - 1;
        const int k1 = qCeil(visZMax / ch) + 1;
        for (int k = k0; k <= k1; k++) {
            if (k == 0) { continue; }
            const SkScalar y = devY(k * ch);
            canvas->drawLine(0, y, W, y, dl);
            const auto lab = QString::fromUtf8("z=%1%2")
                    .arg(k > 0 ? QStringLiteral("+") : QString())
                    .arg(qRound(k * ch));
            canvas->drawString(lab.toStdString().c_str(),
                               SkScalar(6 * pr), y - SkScalar(3 * pr),
                               labelFont, labelP);
        }
    }
    // canvas-plane line through z = 0 (muted green) + center marker
    {
        SkPaint axZ;
        axZ.setColor(kAxisZ);
        axZ.setStrokeWidth(SkScalar(1.5 * pr));
        canvas->drawLine(0, devY(0), W, devY(0), axZ);

        const SkScalar cxp = devX(cw * 0.5);
        const SkScalar cyp = devY(0);
        SkPaint dot;
        dot.setAntiAlias(true);
        dot.setStyle(SkPaint::kFill_Style);
        dot.setColor(kCenterDot);
        canvas->drawCircle(cxp, cyp, SkScalar(2.2 * pr), dot);
        SkPaint halo;
        halo.setAntiAlias(true);
        halo.setStyle(SkPaint::kStroke_Style);
        halo.setStrokeWidth(SkScalar(1.1 * pr));
        halo.setColor(SkColorSetARGB(90, 210, 210, 215));
        canvas->drawCircle(cxp, cyp, SkScalar(5.5 * pr), halo);
    }

    // ---- canvas plane caption + orientation hint ----
    {
        const auto cLab = QString::fromUtf8("画布 %1×%2").arg(cw).arg(ch);
        const auto s = cLab.toStdString();
        const auto tw = labelFont.measureText(s.c_str(), s.size(),
                                              SkTextEncoding::kUTF8);
        canvas->drawString(s.c_str(),
                           devX(cw * 0.5) - SkScalar(tw * 0.5f),
                           devY(0) + SkScalar(14 * pr), labelFont, labelP);
        // orientation hint: depth is up, the viewer/camera side is
        // down - the camera sits below the canvas line looking up
        const auto orient = QString::fromUtf8("↑ 深处 (z+)　·　↓ 观众侧 / 摄像机 (z−)")
                .toStdString();
        canvas->drawString(orient.c_str(), SkScalar(8 * pr),
                           devY(0) - SkScalar(8 * pr), labelFont, labelP);
    }

    // ---- layer footprints (Blender-style: grey solid bars, orange
    // selection) ----
    const auto nameFont = hudFont(9.5);
    const qreal barTh = qMax(6. * pr,
                             qreal(ch * 0.03 * qAbs(T.m22())));
    for (int i = 0; i < mFootprints.count(); i++) {
        const auto& fp = mFootprints.at(i);
        if (!fp.fIs3D) {
            SkPaint g;
            g.setColor(SkColorSetARGB(140, 120, 120, 124));
            g.setStrokeWidth(SkScalar(1.2 * pr));
            canvas->drawLine(devX(fp.fXMin), devY(fp.fZ),
                             devX(fp.fXMax), devY(fp.fZ), g);
            continue;
        }
        const bool hot = (i == mHoverIndex) ||
                         (mDragBox && mDragBox == fp.fBox.data());
        const bool sel = fp.fBox && fp.fBox->isSelected();
        const SkScalar y = devY(fp.fZ);
        const SkRect bar = SkRect::MakeLTRB(
                    devX(fp.fXMin), y - SkScalar(barTh * 0.5),
                    devX(fp.fXMax), y + SkScalar(barTh * 0.5));
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setStyle(SkPaint::kFill_Style);
        fill.setColor(sel || hot ? SkColorSetARGB(255, 110, 106, 100)
                                 : kObjectFill);
        canvas->drawRect(bar, fill);
        SkPaint edge;
        edge.setAntiAlias(true);
        edge.setStyle(SkPaint::kStroke_Style);
        edge.setStrokeWidth(SkScalar((sel || hot ? 2.2 : 1.3) * pr));
        edge.setColor(sel || hot ? kSelectOrange : kObjectEdge);
        canvas->drawRect(bar, edge);
        if (fp.fBox) {
            SkPaint tp;
            tp.setColor(sel || hot ?
                        SkColorSetARGB(255, 240, 235, 228) : kTextMain);
            const auto name = fp.fBox->prp_getName().toStdString();
            const auto zLab = QString::fromUtf8("z=%1")
                    .arg(fp.fZ, 0, 'f', 1).toStdString();
            canvas->drawString(name.c_str(),
                               devX((fp.fXMin + fp.fXMax) * 0.5),
                               y - SkScalar(barTh * 0.5) - SkScalar(4 * pr),
                               nameFont, tp);
            if (hot) {
                canvas->drawString(zLab.c_str(),
                                   devX((fp.fXMin + fp.fXMax) * 0.5),
                                   y + SkScalar(barTh * 0.5) + SkScalar(12 * pr),
                                   labelFont, tp);
            }
        }
    }

    // ---- scene camera (Blender-style grey wireframe: round body +
    // frustum edges + image-plane bar; turns orange while hovered) ----
    const auto cam = mScene->getCameraLayer();
    if (mPose.fValid && cam) {
        const bool camHot = (mDragType == DragType::camera ||
                             mDragType == DragType::cameraRot ||
                             hitCamera(mLastDevicePos) ||
                             hitCameraRing(mLastDevicePos));
        const SkColor wireCol = camHot ? kSelectOrange : kCamWire;
        const QPointF cDev = mapToDevice(mPose.fPos);
        const QPointF centerDev = mapToDevice(QPointF(cw * 0.5, 0.));
        QPointF cd = centerDev - cDev;
        const qreal cdLen = std::sqrt(cd.x() * cd.x() + cd.y() * cd.y());
        const qreal L = qBound(46., cdLen * 0.94, 100000.);
        // world -> device direction (m22 is negative: depth is up)
        const QPointF dirDev(T.m11() * mPose.fDir.x(),
                             T.m22() * mPose.fDir.y());
        const double base = std::atan2(dirDev.y(), dirDev.x());
        const double a = mPose.fHalfFov;
        SkPaint wp;
        wp.setAntiAlias(true);
        wp.setStyle(SkPaint::kStroke_Style);
        wp.setStrokeWidth(SkScalar(1.4 * pr));
        wp.setColor(wireCol);
        // frustum edges to the image-plane ends
        QPointF ends[2];
        const int sides[2] = { -1, 1 };
        for (int si = 0; si < 2; si++) {
            const double ang = base + sides[si] * a;
            ends[si] = cDev + QPointF(std::cos(ang),
                                      std::sin(ang)) * L;
            canvas->drawLine(cDev.x(), cDev.y(),
                             ends[si].x(), ends[si].y(), wp);
        }
        // image plane bar connecting the frustum ends (sensor)
        canvas->drawLine(ends[0].x(), ends[0].y(),
                         ends[1].x(), ends[1].y(), wp);
        // round body (back-filled so the grid does not show through)
        SkPaint body;
        body.setAntiAlias(true);
        body.setStyle(SkPaint::kFill_Style);
        body.setColor(kCamFill);
        canvas->drawCircle(cDev.x(), cDev.y(), SkScalar(7 * pr), body);
        body.setStyle(SkPaint::kStroke_Style);
        body.setStrokeWidth(SkScalar(1.4 * pr));
        body.setColor(wireCol);
        canvas->drawCircle(cDev.x(), cDev.y(), SkScalar(7 * pr), body);
        SkPaint dot;
        dot.setAntiAlias(true);
        dot.setStyle(SkPaint::kFill_Style);
        dot.setColor(wireCol);
        canvas->drawCircle(cDev.x(), cDev.y(), SkScalar(1.8 * pr), dot);
        // roll ring: drag the orange handle to rotate rotZ (the
        // in-plane xy angle)
        const SkScalar ringR = SkScalar(14 * pr);
        SkPaint ring;
        ring.setAntiAlias(true);
        ring.setStyle(SkPaint::kStroke_Style);
        ring.setStrokeWidth(SkScalar(1.2 * pr));
        ring.setColor(SkColorSetARGB(110, 185, 185, 185));
        canvas->drawCircle(cDev.x(), cDev.y(), ringR, ring);
        const double ringA = qDegreesToRadians(mPose.fRotZ);
        const QPointF handle(cDev.x() + std::cos(ringA) * ringR,
                             cDev.y() + std::sin(ringA) * ringR);
        SkPaint hd;
        hd.setAntiAlias(true);
        hd.setStyle(SkPaint::kFill_Style);
        hd.setColor(kSelectOrange);
        canvas->drawCircle(handle.x(), handle.y(),
                           SkScalar(3.5 * pr), hd);
        const auto camLab = QString::fromUtf8(
                    "%1 · 位移x %2 缩放 %3 · 倾斜(%4°, %5°) 旋转 %6°")
                .arg(cam->prp_getName())
                .arg(mPose.fPanX, 0, 'f', 0)
                .arg(mPose.fZoom, 0, 'f', 2)
                .arg(mPose.fRotX, 0, 'f', 0)
                .arg(mPose.fRotY, 0, 'f', 0)
                .arg(mPose.fRotZ, 0, 'f', 0);
        SkPaint cp;
        cp.setColor(camHot ?
                    SkColorSetARGB(235, 245, 200, 150) : kTextDim);
        canvas->drawString(camLab.toStdString().c_str(),
                           cDev.x() + SkScalar(20 * pr),
                           cDev.y() + SkScalar(4 * pr), nameFont, cp);
    } else {
        const auto hint = QString::fromUtf8(
                    "无摄像机 — 摄像机工具（C 键）首次使用会自动创建").toStdString();
        SkPaint hp;
        hp.setColor(kTextHint);
        canvas->drawString(hint.c_str(), SkScalar(8 * pr),
                           devY(0) - SkScalar(8 * pr), labelFont, hp);
    }

    // ---- HUD ----
    {
        // always-on operation hint
        const auto tip = QString::fromUtf8(
                    "拖摄像机 = 平移/推拉 · 拖环手柄 = 旋转 · 拖线段 = 图层 x/z")
                .toStdString();
        SkPaint tipP;
        tipP.setColor(kTextHint);
        canvas->drawString(tip.c_str(), SkScalar(8 * pr),
                           H - SkScalar(22 * pr), labelFont, tipP);
        const auto hudFontV = hudFont(10);
        SkPaint hp;
        hp.setColor(kTextMain);
        const auto hud = QString::fromUtf8("顶视图 (X/Z) · %1 · 帧 %2")
                .arg(mScene->prp_getName())
                .arg(mScene->getCurrentFrame())
                .toStdString();
        canvas->drawString(hud.c_str(), SkScalar(8 * pr),
                           H - SkScalar(8 * pr), hudFontV, hp);
        QString dragLab;
        if (mDragType == DragType::layer) {
            const QPointF world = mapToTopWorld(mLastDevicePos);
            dragLab = QString::fromUtf8("x=%1  z=%2")
                    .arg(world.x(), 0, 'f', 1)
                    .arg(world.y(), 0, 'f', 1);
        } else if (mDragType == DragType::camera && cam) {
            dragLab = QString::fromUtf8("位移 x=%1 · 缩放 %2")
                    .arg(cam->panXAnimator()->getCurrentBaseValue(), 0, 'f', 1)
                    .arg(cam->zoomAnimator()->getCurrentBaseValue(), 0, 'f', 2);
        } else if (mDragType == DragType::cameraRot && cam) {
            dragLab = QString::fromUtf8("旋转 %1°")
                    .arg(cam->rotZAnimator()->getCurrentBaseValue(), 0, 'f', 1);
        }
        if (!dragLab.isEmpty()) {
            const auto s = dragLab.toStdString();
            const auto tw = hudFontV.measureText(s.c_str(), s.size(),
                                                 SkTextEncoding::kUTF8);
            SkPaint dp;
            dp.setColor(kSelectOrange);
            canvas->drawString(s.c_str(), W - SkScalar(tw) - SkScalar(8 * pr),
                               H - SkScalar(8 * pr), hudFontV, dp);
        }
    }
}

void TopViewWindow::mousePressEvent(QMouseEvent* e)
{
    const QPointF dev = toDevice(e->pos());
    mLastDevicePos = dev;
    if (e->button() == Qt::LeftButton &&
        !(e->modifiers() & Qt::AltModifier)) {
        if (hitCameraRing(dev)) {
            const auto cam = mScene ? mScene->getCameraLayer() : nullptr;
            if (cam) {
                mDragType = DragType::cameraRot;
                mPressRingAngle = ringAngleAt(dev);
                mDragStartRotZ = cam->rotZAnimator()->getCurrentBaseValue();
                cam->rotZAnimator()->prp_startTransform();
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }
        if (hitCamera(dev)) {
            const auto cam = mScene ? mScene->getCameraLayer() : nullptr;
            if (cam) {
                mDragType = DragType::camera;
                mPressTopWorld = mapToTopWorld(dev);
                mDragStartPanX = cam->panXAnimator()->getCurrentBaseValue();
                mDragStartZoom = cam->zoomAnimator()->getCurrentBaseValue();
                mDragStartCamZ = mPose.fValid ? mPose.fPos.y() : 0.;
                mDragDz = mPose.fValid ? mPose.fDz : -1.;
                cam->panXAnimator()->prp_startTransform();
                cam->zoomAnimator()->prp_startTransform();
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }
        const int idx = hitLayer(dev);
        if (idx >= 0) {
            mPressTopWorld = mapToTopWorld(dev);
            startLayerDrag(mFootprints.at(idx));
            if (mDragType == DragType::layer) {
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }
    }
    if (e->button() == Qt::MiddleButton ||
        e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton &&
         (e->modifiers() & Qt::AltModifier))) {
        mDragType = DragType::pan;
        setCursor(Qt::ClosedHandCursor);
    }
}

void TopViewWindow::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF dev = toDevice(e->pos());
    const QPointF prevDev = mLastDevicePos;
    mLastDevicePos = dev;
    if (mDragType == DragType::pan) {
        mViewTransform.translate(dev.x() - prevDev.x(),
                                 dev.y() - prevDev.y());
        update();
        return;
    }
    if (mDragType == DragType::layer) {
        auto* const box = mDragBox.data();
        if (!box) { mDragType = DragType::none; return; }
        const auto adv = dynamic_cast<AdvancedTransformAnimator*>(
                    box->getTransformAnimator());
        if (adv) {
            const QPointF world = mapToTopWorld(dev);
            const QPointF dWorld = world - mPressTopWorld;
            adv->move3DZRelativeToSavedValue(dWorld.y());
            const qreal vv = mLocalXAxis.x() * mLocalXAxis.x() +
                            mLocalXAxis.y() * mLocalXAxis.y();
            const qreal localDx = (dWorld.x() * mLocalXAxis.x()) / vv;
            adv->moveRelativeToSavedValue(localDx, 0.);
        }
        update();
        return;
    }
    if (mDragType == DragType::camera) {
        const auto cam = mScene ? mScene->getCameraLayer() : nullptr;
        if (!cam) { mDragType = DragType::none; return; }
        const QPointF world = mapToTopWorld(dev);
        const QPointF dWorld = world - mPressTopWorld;
        // horizontal drag = camera x position: panX tracks the icon
        // 1:1 (pan sits inside zoom in the matrix chain)
        cam->panXAnimator()->setCurrentBaseValue(
                    mDragStartPanX + dWorld.x());
        // vertical drag = dolly along depth: solve the zoom that puts
        // the icon at the cursor height (C.z = dz * f / zoom), so the
        // camera slides towards/away from the canvas following the
        // mouse exactly
        const qreal f = cam->focalAnimator()->getCurrentBaseValue();
        const qreal dz = mDragDz;
        if (qAbs(dz) > 1e-3 && qAbs(f) > 1e-3) {
            const qreal targetZ = mDragStartCamZ + dWorld.y();
            // keep the camera on the viewer side (z < 0)
            if (targetZ < -1.) {
                cam->zoomAnimator()->setCurrentBaseValue(
                            qBound(0.01, dz * f / targetZ, 100.));
            }
        }
        update();
        return;
    }
    if (mDragType == DragType::cameraRot) {
        const auto cam = mScene ? mScene->getCameraLayer() : nullptr;
        if (!cam) { mDragType = DragType::none; return; }
        qreal d = ringAngleAt(dev) - mPressRingAngle;
        while (d > 180.) { d -= 360.; }
        while (d < -180.) { d += 360.; }
        cam->rotZAnimator()->setCurrentBaseValue(mDragStartRotZ + d);
        update();
        return;
    }
    // idle: hover feedback
    const int idx = hitLayer(dev);
    const bool ringHover = hitCameraRing(dev);
    const bool camHover = hitCamera(dev);
    if (idx != mHoverIndex) {
        mHoverIndex = idx;
        update();
    }
    setCursor(ringHover ? Qt::CrossCursor :
              camHover || idx >= 0 ? Qt::PointingHandCursor
                                   : Qt::ArrowCursor);
}

void TopViewWindow::mouseReleaseEvent(QMouseEvent* e)
{
    Q_UNUSED(e)
    if (mDragType == DragType::none) { return; }
    finishDrags();
}

void TopViewWindow::mouseDoubleClickEvent(QMouseEvent* e)
{
    Q_UNUSED(e)
    fitToContent();
}

void TopViewWindow::wheelEvent(QWheelEvent* e)
{
    const QPointF dev = toDevice(e->position());
    const qreal by = e->angleDelta().y() > 0 ? 1.15 : 1. / 1.15;
    const qreal next = mViewTransform.m11() * by;
    if (next < 0.004 || next > 800.) { return; }
    mViewTransform.translate(dev.x(), dev.y());
    mViewTransform.scale(by, by);
    mViewTransform.translate(-dev.x(), -dev.y());
    update();
}
