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
    mViewTransform = QMatrix(s, 0., 0., s,
                             -x0 * s + (sw - w * s) * 0.5,
                             -z0 * s + (sh - h * s) * 0.5);
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
    const qreal r = 16. * pr;
    return d.x() * d.x() + d.y() * d.y() <= r * r;
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
            cam->panYAnimator()->prp_finishTransform();
        }
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
    bg.setColor(SkColorSetARGB(255, 24, 24, 28));
    canvas->drawRect(SkRect::MakeWH(W, H), bg);

    if (!mScene) {
        const auto font = hudFont(12);
        const auto txt = QString::fromUtf8("无活动场景").toStdString();
        SkPaint tp;
        tp.setColor(SkColorSetARGB(200, 150, 150, 158));
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
    const qreal visZ0 = (0. - T.dy()) / T.m22();
    const qreal visZ1 = (qreal(H) - T.dy()) / T.m22();

    const qreal cw = mScene->getCanvasWidth();
    const qreal ch = mScene->getCanvasHeight();

    // ---- depth grid (one canvas height per line) ----
    const auto labelFont = hudFont(9);
    SkPaint labelP;
    labelP.setColor(SkColorSetARGB(150, 110, 110, 120));
    SkPaint grid;
    grid.setColor(SkColorSetARGB(80, 90, 90, 100));
    grid.setStrokeWidth(SkScalar(pr));
    const int k0 = qFloor(visZ0 / ch) - 1;
    const int k1 = qCeil(visZ1 / ch) + 1;
    for (int k = k0; k <= k1; k++) {
        if (k == 0) { continue; }
        const SkScalar y = devY(k * ch);
        canvas->drawLine(0, y, W, y, grid);
        const auto lab = QString::fromUtf8("z=%1%2")
                .arg(k > 0 ? QStringLiteral("+") : QString())
                .arg(qRound(k * ch));
        canvas->drawString(lab.toStdString().c_str(),
                           SkScalar(6 * pr), y - SkScalar(3 * pr),
                           labelFont, labelP);
    }

    // ---- canvas plane line (z = 0) ----
    SkPaint pline;
    pline.setColor(SkColorSetARGB(235, 225, 225, 232));
    pline.setStrokeWidth(SkScalar(1.5 * pr));
    pline.setStrokeCap(SkPaint::kRound_Cap);
    canvas->drawLine(devX(0), devY(0), devX(cw), devY(0), pline);
    canvas->drawLine(devX(0), devY(0) - SkScalar(5 * pr),
                     devX(0), devY(0) + SkScalar(5 * pr), pline);
    canvas->drawLine(devX(cw), devY(0) - SkScalar(5 * pr),
                     devX(cw), devY(0) + SkScalar(5 * pr), pline);
    {
        const auto cLab = QString::fromUtf8("画布 %1×%2").arg(cw).arg(ch);
        const auto s = cLab.toStdString();
        const auto tw = labelFont.measureText(s.c_str(), s.size(),
                                              SkTextEncoding::kUTF8);
        canvas->drawString(s.c_str(),
                           devX(cw * 0.5) - SkScalar(tw * 0.5f),
                           devY(0) + SkScalar(14 * pr), labelFont, labelP);
    }

    // ---- layer footprints ----
    const auto nameFont = hudFont(9.5);
    for (int i = 0; i < mFootprints.count(); i++) {
        const auto& fp = mFootprints.at(i);
        if (!fp.fIs3D) {
            SkPaint g;
            g.setColor(SkColorSetARGB(160, 96, 96, 104));
            g.setStrokeWidth(SkScalar(1.2 * pr));
            canvas->drawLine(devX(fp.fXMin), devY(fp.fZ),
                             devX(fp.fXMax), devY(fp.fZ), g);
            continue;
        }
        const bool hot = (i == mHoverIndex) ||
                         (mDragBox && mDragBox == fp.fBox.data());
        const bool sel = fp.fBox && fp.fBox->isSelected();
        SkPaint p;
        p.setColor(sel ? SkColorSetARGB(255, 255, 255, 255) :
                   hot ? SkColorSetARGB(255, 130, 240, 220) :
                         SkColorSetARGB(230, 78, 201, 176));
        p.setStrokeWidth(SkScalar((hot || sel ? 2.6 : 2.) * pr));
        p.setStrokeCap(SkPaint::kRound_Cap);
        const SkScalar y = devY(fp.fZ);
        canvas->drawLine(devX(fp.fXMin), y, devX(fp.fXMax), y, p);
        // end whiskers towards the viewer side
        SkPaint w = p;
        w.setStrokeWidth(SkScalar(1.2 * pr));
        canvas->drawLine(devX(fp.fXMin), y,
                         devX(fp.fXMin), y - SkScalar(6 * pr), w);
        canvas->drawLine(devX(fp.fXMax), y,
                         devX(fp.fXMax), y - SkScalar(6 * pr), w);
        if (fp.fBox) {
            SkPaint tp;
            tp.setColor(hot || sel ?
                        SkColorSetARGB(235, 220, 250, 244) :
                        SkColorSetARGB(190, 120, 200, 185));
            const auto name = fp.fBox->prp_getName().toStdString();
            const auto zLab = QString::fromUtf8("z=%1")
                    .arg(fp.fZ, 0, 'f', 1).toStdString();
            canvas->drawString(name.c_str(),
                               devX((fp.fXMin + fp.fXMax) * 0.5),
                               y - SkScalar(10 * pr), nameFont, tp);
            if (hot) {
                canvas->drawString(zLab.c_str(),
                                   devX((fp.fXMin + fp.fXMax) * 0.5),
                                   y + SkScalar(14 * pr),
                                   labelFont, tp);
            }
        }
    }

    // ---- scene camera ----
    const auto cam = mScene->getCameraLayer();
    if (mPose.fValid && cam) {
        const QPointF cDev = mapToDevice(mPose.fPos);
        const QPointF centerDev = mapToDevice(QPointF(cw * 0.5, 0.));
        QPointF cd = centerDev - cDev;
        const qreal cdLen = std::sqrt(cd.x() * cd.x() + cd.y() * cd.y());
        const qreal L = qBound(40., cdLen * 0.92, 100000.);
        SkPaint cone;
        cone.setColor(SkColorSetARGB(160, 176, 106, 30));
        cone.setStrokeWidth(SkScalar(1.2 * pr));
        const double base = std::atan2(mPose.fDir.y(), mPose.fDir.x());
        const double a = mPose.fHalfFov;
        const int sides[2] = { -1, 1 };
        for (const int side : sides) {
            const double ang = base + side * a;
            const QPointF end = cDev + QPointF(std::cos(ang),
                                               std::sin(ang)) * L;
            canvas->drawLine(cDev.x(), cDev.y(), end.x(), end.y(), cone);
        }
        // body: dot + view direction stub
        SkPaint body;
        body.setAntiAlias(true);
        body.setStyle(SkPaint::kFill_Style);
        body.setColor(SkColorSetARGB(255, 255, 158, 44));
        canvas->drawCircle(cDev.x(), cDev.y(), SkScalar(5 * pr), body);
        SkPaint stub;
        stub.setStyle(SkPaint::kStroke_Style);
        stub.setStrokeWidth(SkScalar(2.4 * pr));
        stub.setStrokeCap(SkPaint::kRound_Cap);
        stub.setColor(SkColorSetARGB(255, 255, 158, 44));
        const QPointF stubEnd = cDev + QPointF(mPose.fDir.x(),
                                               mPose.fDir.y()) * 13. * pr;
        canvas->drawLine(cDev.x(), cDev.y(), stubEnd.x(), stubEnd.y(),
                         stub);
        const auto camLab = QString::fromUtf8(
                    "%1 · pan(%2, %3) 缩放 %4 · 倾斜(%5°, %6°)")
                .arg(cam->prp_getName())
                .arg(mPose.fPanX, 0, 'f', 0)
                .arg(mPose.fPanY, 0, 'f', 0)
                .arg(mPose.fZoom, 0, 'f', 2)
                .arg(mPose.fRotX, 0, 'f', 0)
                .arg(mPose.fRotY, 0, 'f', 0);
        SkPaint cp;
        cp.setColor(SkColorSetARGB(220, 255, 200, 140));
        canvas->drawString(camLab.toStdString().c_str(),
                           cDev.x() + SkScalar(10 * pr),
                           cDev.y() - SkScalar(8 * pr), nameFont, cp);
    } else {
        const auto hint = QString::fromUtf8(
                    "无摄像机 — 摄像机工具（C 键）首次使用会自动创建").toStdString();
        SkPaint hp;
        hp.setColor(SkColorSetARGB(150, 130, 130, 140));
        canvas->drawString(hint.c_str(), SkScalar(8 * pr),
                           devY(0) - SkScalar(8 * pr), labelFont, hp);
    }

    // ---- HUD ----
    {
        const auto hudFontV = hudFont(10);
        SkPaint hp;
        hp.setColor(SkColorSetARGB(210, 190, 190, 200));
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
            dragLab = QString::fromUtf8("pan(%1, %2)")
                    .arg(cam->panXAnimator()->getCurrentBaseValue(), 0, 'f', 1)
                    .arg(cam->panYAnimator()->getCurrentBaseValue(), 0, 'f', 1);
        }
        if (!dragLab.isEmpty()) {
            const auto s = dragLab.toStdString();
            const auto tw = hudFontV.measureText(s.c_str(), s.size(),
                                                 SkTextEncoding::kUTF8);
            SkPaint dp;
            dp.setColor(SkColorSetARGB(230, 130, 240, 220));
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
        if (hitCamera(dev)) {
            const auto cam = mScene ? mScene->getCameraLayer() : nullptr;
            if (cam) {
                mDragType = DragType::camera;
                mPressTopWorld = mapToTopWorld(dev);
                mDragStartPanX = cam->panXAnimator()->getCurrentBaseValue();
                mDragStartPanY = cam->panYAnimator()->getCurrentBaseValue();
                cam->panXAnimator()->prp_startTransform();
                cam->panYAnimator()->prp_startTransform();
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
        // the pose position follows panX 1:1, so the icon tracks the
        // cursor exactly; panY is not visible from the top but edits
        // the same animator pair the camera tool uses
        cam->panXAnimator()->setCurrentBaseValue(
                    mDragStartPanX + dWorld.x());
        cam->panYAnimator()->setCurrentBaseValue(
                    mDragStartPanY + dWorld.y());
        update();
        return;
    }
    // idle: hover feedback
    const int idx = hitLayer(dev);
    const bool camHover = hitCamera(dev);
    if (idx != mHoverIndex) {
        mHoverIndex = idx;
        update();
    }
    setCursor(camHover || idx >= 0 ? Qt::PointingHandCursor
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
