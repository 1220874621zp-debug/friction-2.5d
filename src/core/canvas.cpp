/*
# enve2d - https://github.com/enve2d
#
# Copyright (c) enve2d developers
# Copyright (C) 2016-2020 Maurycy Liebner
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
*/

#include "canvas.h"
#include "Boxes/internallinkgroupbox.h"
#include "Boxes/bone.h"
#include "Boxes/bonelayer.h"
#include "Boxes/adjustmentlayer.h"
#include "Boxes/solidlayer.h"
#include "Boxes/cameralayer.h"
#include <QPainter>
#include <QMouseEvent>
#include <QLineF>
#include <QSet>
#include <QtMath>
#include <cmath>
#include <limits>
#include <QDebug>
#include <QApplication>
#include <QPolygonF>
#include "Boxes/videobox.h"
#include "MovablePoints/pathpivot.h"
#include "Boxes/imagebox.h"
#include "Sound/soundcomposition.h"
#include "Boxes/textbox.h"
#include "GUI/global.h"
#include "appsupport.h"
#include "pointhelpers.h"
#include "Boxes/internallinkbox.h"
#include "clipboardcontainer.h"
//#include "Boxes/paintbox.h"
#include <QFile>
#include "MovablePoints/smartnodepoint.h"
#include "Boxes/internallinkcanvas.h"
#include "pointtypemenu.h"
#include "Animators/transformanimator.h"
#include "glhelpers.h"
#include "Private/document.h"
#include "svgexporter.h"
#include "ReadWrite/evformat.h"
#include "eevent.h"
#include "Boxes/nullobject.h"
#include "simpletask.h"
#include "themesupport.h"
#include "efiltersettings.h"

using namespace Friction::Core;

Canvas::Canvas(Document &document,
               const int canvasWidth,
               const int canvasHeight,
               const int frameCount,
               const qreal fps)
    : mDocument(document)
    //, mPaintTarget(this)
{
    SceneParentSelfAssign(this);
    connect(&mDocument, &Document::canvasModeSet,
            this, &Canvas::setCanvasMode);
    std::function<bool(int)> changeFrameFunc =
    [this](const int undoRedoFrame) {
        if (mDocument.fActiveScene != this) { return false; }
        if (undoRedoFrame != anim_getCurrentAbsFrame()) {
            mDocument.setActiveSceneFrame(undoRedoFrame);
            return true;
        }
        return false;
    };
    mUndoRedoStack = enve::make_shared<UndoRedoStack>(changeFrameFunc);
    mFps = fps;

    mBackgroundColor->setColor(QColor(75, 75, 75));
    mShowSafeFrames = AppSupport::getSettings(
                QStringLiteral("view"), QStringLiteral("safeFrames"),
                false).toBool();
    mTransparencyGrid = AppSupport::getSettings(
                QStringLiteral("view"), QStringLiteral("transparencyGrid"),
                false).toBool();
    ca_addChild(mBackgroundColor);
    mSoundComposition = qsptr<SoundComposition>::create(this);

    mRange = {0, frameCount};

    mWidth = canvasWidth;
    mHeight = canvasHeight;

    mCurrentContainer = this;
    setIsCurrentGroup_k(true);

    mRotPivot = enve::make_shared<PathPivot>(this);

    mTransformAnimator->SWT_hide();

    //anim_setAbsFrame(0);

    //setCanvasMode(MOVE_PATH);

    // update fps for videos if new frame range
    connect(this, &Canvas::newFrameRange, this, [this]() {
        for (const auto &box : getContainedBoxes()) {
            if (const auto &vbox = enve_cast<VideoBox*>(box)) {
                vbox->setCorrectFps();
            }
        }
    });
}

void Canvas::setWorldToScreen(const QTransform& transform,
                              qreal devicePixelRatio)
{
    mWorldToScreen = transform;
    mDevicePixelRatio = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    bool invertible = false;
    mScreenToWorld = transform.inverted(&invertible);
    mHasWorldToScreen = invertible;
}


Canvas::~Canvas()
{
    // from here on the track/selection bookkeeping must stay inert:
    // signals emitted and SWT updates scheduled during the teardown
    // would write into half-dead abstractions (heap corruption when
    // another project is opened afterwards)
    mDestructing = true;
    clearPointsSelection();
    clearBoxesSelection();
}

qreal Canvas::getResolution() const
{
    return mResolution;
}

void Canvas::setSafeFramesVisible(const bool visible) {
    if(mShowSafeFrames == visible) return;
    mShowSafeFrames = visible;
    AppSupport::setSettings(QStringLiteral("view"),
                            QStringLiteral("safeFrames"), visible);
    emit requestUpdate();
}

void Canvas::setTransparencyGrid(const bool grid) {
    if(mTransparencyGrid == grid) return;
    mTransparencyGrid = grid;
    AppSupport::setSettings(QStringLiteral("view"),
                            QStringLiteral("transparencyGrid"), grid);
    emit requestUpdate();
}

void Canvas::setResolution(const qreal percent)
{
    if (isZero6Dec(mResolution - percent)) { return; }
    mResolution = percent;
#ifdef Q_OS_MAC
    invalidateSceneFramesCache();
#endif
    prp_afterWholeInfluenceRangeChanged();
    updateAllBoxes(UpdateReason::userChange);
}

void Canvas::invalidateSceneFramesCache()
{
    mSceneFrame.reset();
    mLoadingSceneFrame.reset();
    mSceneFrameOutdated = true;
    mSceneFramesHandler.clear();
}

// collect the edit generation of this scene plus every scene whose
// content is embedded through (nested) scene/group links
static void walkLinkGen(ContainerBox* const cont, qint64& gen,
                        QSet<Canvas*>& visited) {
    for(const auto& box : cont->getContainedBoxes()) {
        if(const auto group = enve_cast<ContainerBox*>(box)) {
            walkLinkGen(group, gen, visited);
        }
        const auto link = enve_cast<InternalLinkGroupBox*>(box);
        if(!link) continue;
        const auto target = link->linkTargetBox();
        if(!target) continue;
        const auto targetScene = target->getParentScene();
        if(!targetScene || visited.contains(targetScene)) continue;
        visited.insert(targetScene);
        gen += targetScene->contentGen();
        walkLinkGen(targetScene, gen, visited);
    }
}

qint64 Canvas::effectiveContentGen() {
    qint64 gen = mContentGen;
    QSet<Canvas*> visited;
    walkLinkGen(this, gen, visited);
    return gen;
}

void Canvas::setCurrentGroupParentAsCurrentGroup()
{
    setCurrentBoxesGroup(mCurrentContainer->getParentGroup());
}

void Canvas::queTasks()
{
    if (Actions::sInstance->smoothChange() && mCurrentContainer) {
        if (!mDrawnSinceQue) { return; }
        mCurrentContainer->queChildrenTasks();
    } else ContainerBox::queTasks();
    mDrawnSinceQue = false;
}

void Canvas::addSelectedForGraph(const int widgetId,
                                 GraphAnimator* const anim)
{
    const auto it = mSelectedForGraph.find(widgetId);
    if (it == mSelectedForGraph.end()) {
        const auto list = std::make_shared<ConnContextObjList<GraphAnimator*>>();
        mSelectedForGraph.insert({widgetId, list});
    }
    auto &connCtxt = mSelectedForGraph[widgetId]->addObj(anim);
    connCtxt << connect(anim, &QObject::destroyed,
                        this, [this, widgetId, anim]() {
        removeSelectedForGraph(widgetId, anim);
    });
}

bool Canvas::removeSelectedForGraph(const int widgetId,
                                    GraphAnimator* const anim)
{
    return mSelectedForGraph[widgetId]->removeObj(anim);
}

const ConnContextObjList<GraphAnimator*>* Canvas::getSelectedForGraph(const int widgetId) const
{
    const auto it = mSelectedForGraph.find(widgetId);
    if (it == mSelectedForGraph.end()) { return nullptr; }
    return it->second.get();
}

void Canvas::setCurrentBoxesGroup(ContainerBox* const group)
{
    if (mCurrentContainer) {
        mCurrentContainer->setIsCurrentGroup_k(false);
    }
    clearBoxesSelection();
    clearPointsSelection();
    clearCurrentSmartEndPoint();
    clearLastPressedPoint();
    mCurrentContainer = group;
    group->setIsCurrentGroup_k(true);

    emit currentContainerSet(group);
}

void Canvas::updateHoveredBox(const eMouseEvent &e)
{
    mHoveredBox = mCurrentContainer->getBoxAt(e.fPos);
    if(!mHoveredBox && sceneHasActiveCamera()) {
        // 3D layers are displayed through the camera projection: try the
        // un-projected position as well so they highlight where they are
        // actually seen (2D layers keep matching the raw position first)
        mHoveredBox = mCurrentContainer->getBoxAt(mapCameraScreenToWorld(e.fPos));
    }
}

void Canvas::updateHoveredPoint(const eMouseEvent &e)
{
    mHoveredPoint_d = getPointAtAbsPos(e.fPos, mCurrentMode, 1/e.fScale);
}

void Canvas::updateHoveredEdge(const eMouseEvent &e)
{
    if (mCurrentMode != CanvasMode::pointTransform || mHoveredPoint_d) {
        return mHoveredNormalSegment.clear();
    }
    mHoveredNormalSegment = getSegment(e);
    if (mHoveredNormalSegment.isValid()) {
        mHoveredNormalSegment.generateSkPath();
    }
}

void Canvas::clearHovered()
{
    mHoveredBox.clear();
    mHoveredPoint_d.clear();
    mHoveredNormalSegment.clear();
}

bool Canvas::getPivotLocal() const
{
    return mDocument.fLocalPivot;
}

void Canvas::updateHovered(const eMouseEvent &e)
{
    updateHoveredPoint(e);
    updateHoveredEdge(e);
    updateHoveredBox(e);
}

void drawTransparencyMesh(SkCanvas* const canvas,
                          const SkRect &drawRect)
{
    SkPaint paint;
    // the 2x2 checker pattern is static - build the bitmap and base
    // shader once; only the zoom-dependent local matrix changes per call
    static uint8_t pixels[4] = { 0, 255, 255, 0 };
    static const sk_sp<SkShader> baseShader = [] {
        SkBitmap bitmap;
        bitmap.setInfo(SkImageInfo::MakeA8(2, 2), 2);
        bitmap.setPixels(pixels);
        return bitmap.makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat);
    }();

    SkMatrix matr;
    const float scale = canvas->getTotalMatrix().getMinScale();
    const float dim = eSizesUI::widget*0.5f / (scale > 1.f ? 1.f : scale);
    matr.setScale(dim, dim);
    paint.setShader(baseShader->makeWithLocalMatrix(matr));
    paint.setColor(SkColorSetARGB(255, 100, 100, 100));
    canvas->drawRect(drawRect, paint);
}

void Canvas::renderSk(SkCanvas* const canvas,
                      const QRect& drawRect,
                      const QMatrix& viewTrans,
                      const bool mouseGrabbing)
{
    mDrawnSinceQue = true;
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    const qreal pixelRatio = qApp->devicePixelRatio();
    const SkRect canvasRect = SkRect::MakeWH(mWidth, mHeight);
    const qreal zoom = viewTrans.m11();
    const auto filter = eFilterSettings::sDisplay(zoom, mResolution);
    const qreal qInvZoom = 1/viewTrans.m11() * pixelRatio;
    const float invZoom = toSkScalar(qInvZoom);
    const SkMatrix skViewTrans = toSkMatrix(viewTrans);
    const QColor bgColor = mBackgroundColor->getColor();
    const float intervals[2] = {eSizesUI::widget*0.25f*invZoom,
                                eSizesUI::widget*0.25f*invZoom};
    const auto dashPathEffect = SkDashPathEffect::Make(intervals, 2, 0);

    QTransform worldToScreenTransform = mWorldToScreen;
    QTransform screenToWorldTransform = mScreenToWorld;
    bool haveWorldTransform = mHasWorldToScreen;
    if (!haveWorldTransform) {
        bool invertible = false;
        worldToScreenTransform = QTransform(viewTrans.m11(), viewTrans.m12(), 0.0,
                                            viewTrans.m21(), viewTrans.m22(), 0.0,
                                            viewTrans.dx(), viewTrans.dy(), 1.0);
        screenToWorldTransform = worldToScreenTransform.inverted(&invertible);
        haveWorldTransform = invertible;
    }
    const QRectF worldViewport = haveWorldTransform ? screenToWorldTransform.mapRect(QRectF(drawRect)) : QRectF(QPointF(0.0, 0.0),
                                                                                                                QSizeF(drawRect.width(),
                                                                                                                       drawRect.height()));
    QRectF gridViewport = worldViewport.normalized();
    const qreal gridPixelRatio = haveWorldTransform ? mDevicePixelRatio : pixelRatio;
    const auto& gridSettings = mDocument.getGrid()->getSettings();
    const bool gridVisible = gridSettings.show && (!haveWorldTransform || !gridViewport.isEmpty());
    const bool gridOnTop = gridSettings.drawOnTop;
    const bool drawCanvas = mSceneFrame && mSceneFrame->fBoxState == mStateId;

    canvas->concat(skViewTrans);
    if (isPreviewingOrRendering()) {
        if (mSceneFrame) {
            canvas->clear(SK_ColorBLACK);
            canvas->save();
            if (bgColor.alpha() != 255) {
                drawTransparencyMesh(canvas, canvasRect);
            }
            const float reversedRes = toSkScalar(1/mSceneFrame->fResolution);
            canvas->scale(reversedRes, reversedRes);
            mSceneFrame->drawImage(canvas, filter);
            canvas->restore();
        } else {
            // no cached frame to show yet (cache just dropped by an
            // edit, or warm-up right after invalidation): paint the
            // canvas background - returning without drawing anything
            // left the GL FBO contents undefined, shown as a black
            // widget until the first frame landed
            canvas->clear(SK_ColorBLACK);
            if (bgColor.alpha() != 255) {
                drawTransparencyMesh(canvas, canvasRect);
            } else {
                SkPaint bgPaint;
                bgPaint.setStyle(SkPaint::kFill_Style);
                bgPaint.setColor(toSkColor(bgColor));
                canvas->drawRect(canvasRect, bgPaint);
            }
        }
        return;
    }

    canvas->save();

    if (mClipToCanvasSize) {
        canvas->clear(SK_ColorBLACK);
    } else {
        canvas->clear(ThemeSupport::getThemeBaseSkColor());
        paint.setColor(SK_ColorGRAY);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setPathEffect(dashPathEffect);
        canvas->drawRect(toSkRect(getCurrentBounds()), paint);
    }
    if (!mClipToCanvasSize || !drawCanvas) {
        if (bgColor.alpha() == 255 && !mTransparencyGrid &&
                skViewTrans.mapRect(canvasRect).contains(toSkRect(drawRect))) {
            canvas->clear(toSkColor(bgColor));
        } else {
            SkPaint bgPaint;
            bgPaint.setStyle(SkPaint::kFill_Style);
            bgPaint.setColor(toSkColor(bgColor));
            canvas->drawRect(canvasRect, bgPaint);
        }
    }
    if (gridVisible && !gridOnTop) {
        mDocument.getGrid()->drawGrid(canvas,
                                      gridViewport,
                                      worldToScreenTransform,
                                      gridPixelRatio);
    }


    canvas->save();

    if (mClipToCanvasSize) {
        canvas->clipRect(canvasRect);
    }

    if (bgColor.alpha() != 255 || mTransparencyGrid) {
        drawTransparencyMesh(canvas, canvasRect);
    }

    if (!mClipToCanvasSize || !drawCanvas) {
        // NOTE: no camera concat here - the camera affects ONLY 3D
        // layers (AE rule) and that filtering happens per-layer in
        // the render pipeline; a whole-canvas concat would wrongly
        // transform 2D layers in this transient path
        canvas->saveLayer(nullptr, nullptr);
        drawContained(canvas, filter);
        canvas->restore();
    } else if (drawCanvas) {
        canvas->save();
        const float reversedRes = toSkScalar(1/mSceneFrame->fResolution);
        canvas->scale(reversedRes, reversedRes);
        mSceneFrame->drawImage(canvas, filter);
        canvas->restore();
    }

    canvas->restore();
    canvas->restore();

    if (gridVisible && gridOnTop) {
        mDocument.getGrid()->drawGrid(canvas,
                                      gridViewport,
                                      worldToScreenTransform,
                                      gridPixelRatio);
    }

    if (mShowSafeFrames) {
        // AE-style guides: action safe 90%, title safe 80% of the
        // canvas, centered, drawn ON TOP of the content (edit view
        // only - renderSk never feeds the scene frame cache/exports)
        SkPaint sfPaint;
        sfPaint.setStyle(SkPaint::kStroke_Style);
        sfPaint.setColor(SkColorSetARGB(170, 255, 70, 70));
        sfPaint.setStrokeWidth(invZoom);
        sfPaint.setPathEffect(dashPathEffect);
        const auto mkRect = [canvasRect](const qreal f) {
            const SkScalar w = canvasRect.width()*f;
            const SkScalar h = canvasRect.height()*f;
            return SkRect::MakeXYWH(canvasRect.centerX() - w/2,
                                    canvasRect.centerY() - h/2, w, h);
        };
        canvas->drawRect(mkRect(0.9), sfPaint);  // action safe
        sfPaint.setColor(SkColorSetARGB(170, 255, 220, 60));
        canvas->drawRect(mkRect(0.8), sfPaint);  // title safe
        // center marker: small cross at the canvas center
        const SkScalar cx = canvasRect.centerX();
        const SkScalar cy = canvasRect.centerY();
        const SkScalar m = 6*invZoom;
        sfPaint.setStrokeWidth(1.5f*invZoom);
        sfPaint.setPathEffect(nullptr);
        sfPaint.setColor(SkColorSetARGB(200, 255, 255, 255));
        canvas->drawLine(cx - m, cy, cx + m, cy, sfPaint);
        canvas->drawLine(cx, cy - m, cx, cy + m, sfPaint);
    }

    if (!enve_cast<Canvas*>(mCurrentContainer)) {
        mCurrentContainer->drawBoundingRect(canvas, invZoom);
    }

    //if(!mPaintTarget.isValid()) {
        const auto mods = QApplication::queryKeyboardModifiers();
        const bool ctrlPressed = mods & Qt::CTRL && mods & Qt::SHIFT;
        for (int i = mSelectedBoxes.count() - 1; i >= 0; i--) {
            const auto& iBox = mSelectedBoxes.at(i);
            canvas->save();
            iBox->drawBoundingRect(canvas, invZoom);
            iBox->drawAllCanvasControls(canvas, mCurrentMode, invZoom, ctrlPressed);
            canvas->restore();
        }
        for (const auto obj : mNullObjects) {
            canvas->save();
            obj->drawNullObject(canvas, mCurrentMode, invZoom, ctrlPressed);
            canvas->restore();
        }
        for (const auto bone : mBones) {
            canvas->save();
            bone->drawBone(canvas, mCurrentMode, invZoom, ctrlPressed);
            canvas->restore();
        }
    //}

    renderGizmos(canvas, qInvZoom, invZoom);

    // consume pending pivot updates right before the handle is drawn:
    // value changes (dragged motion path keys, timeline edits,
    // playback) schedule a refresh, but repaints driven by the render
    // pipeline never emit requestUpdate, so the handle stuck to a
    // stale spot until the next mode/scene switch
    updatePivotIfNeeded();

    if (mCurrentMode == CanvasMode::boxTransform ||
       mCurrentMode == CanvasMode::pointTransform) {
        if (mTransMode == TransformMode::rotate ||
           mTransMode == TransformMode::rotateX ||
           mTransMode == TransformMode::rotateY ||
           mTransMode == TransformMode::scale ||
           mTransMode == TransformMode::shear) {
            mRotPivot->drawTransforming(canvas, mCurrentMode, invZoom,
                                        eSizesUI::widget*0.25f*invZoom);
        } else if (!mouseGrabbing || mRotPivot->isSelected()) {
            mRotPivot->drawSk(canvas, mCurrentMode, invZoom, false, false);
        }
    } else if (mCurrentMode == CanvasMode::drawPath) {
        const SkScalar nodeSize = 0.15f*eSizesUI::widget*invZoom;
        SkPaint paint;
        paint.setStyle(SkPaint::kFill_Style);
        paint.setAntiAlias(true);

        const auto& pts = mDrawPath.smoothPts();
        const auto drawColor = eSettings::instance().fLastUsedStrokeColor;
        paint.setARGB(255,
                      drawColor.red(),
                      drawColor.green(),
                      drawColor.blue());
        const SkScalar ptSize = 0.25*nodeSize;
        for (const auto& pt : pts) {
            canvas->drawCircle(pt.x(), pt.y(), ptSize, paint);
        }

        const bool drawFitted = mDocument.fDrawPathManual &&
                                mManualDrawPathState == ManualDrawPathState::drawn;
        if (drawFitted) {
            paint.setARGB(255, 255, 0, 0);
            const auto& highlightPts = mDrawPath.forceSplits();
            for (const int ptId : highlightPts) {
                const auto& pt = pts.at(ptId);
                canvas->drawCircle(pt.x(), pt.y(), nodeSize, paint);
            }
            const auto& fitted = mDrawPath.getFitted();
            paint.setARGB(255, 255, 0, 0);
            for (const auto& seg : fitted) {
                const auto path = seg.toSkPath();
                SkiaHelpers::drawOutlineOverlay(canvas,
                                                path,
                                                invZoom,
                                                SK_ColorWHITE);
                const auto& p0 = seg.p0();
                canvas->drawCircle(p0.x(), p0.y(), nodeSize, paint);
            }
            if (!mDrawPathTmp.isEmpty()) {
                SkiaHelpers::drawOutlineOverlay(canvas,
                                                mDrawPathTmp,
                                                invZoom,
                                                SK_ColorWHITE);
            }
        }

        paint.setARGB(255, 0, 75, 155);

        if (mHoveredPoint_d && mHoveredPoint_d->isSmartNodePoint()) {
            const QPointF pos = mHoveredPoint_d->getAbsolutePos();
            const qreal r = 0.5*qInvZoom*mHoveredPoint_d->getRadius();
            canvas->drawCircle(pos.x(), pos.y(), r, paint);
        }

        if (mDrawPathFirst) {
            const QPointF pos = mDrawPathFirst->getAbsolutePos();
            const qreal r = 0.5*qInvZoom*mDrawPathFirst->getRadius();
            canvas->drawCircle(pos.x(), pos.y(), r, paint);
        }
    }

    /*if(mPaintTarget.isValid()) {
        canvas->save();
        mPaintTarget.draw(canvas, viewTrans, invZoom, drawRect,
                          filter, mDocument.fOnionVisible);
        const SkIRect bRect = toSkIRect(mPaintTarget.pixelBoundingRect());
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setColor(SK_ColorRED);
        paint.setPathEffect(dashPathEffect);
        canvas->drawIRect(bRect, paint);
        paint.setPathEffect(nullptr);
        canvas->restore();
    } else {*/
        if (mSelecting) {
            paint.setStyle(SkPaint::kStroke_Style);
            paint.setPathEffect(dashPathEffect);
            paint.setStrokeWidth(2*invZoom);
            paint.setColor(SkColorSetARGB(255, 0, 55, 255));
            canvas->drawRect(toSkRect(mSelectionRect), paint);
            paint.setStrokeWidth(invZoom);
            paint.setColor(SkColorSetARGB(255, 150, 150, 255));
            canvas->drawRect(toSkRect(mSelectionRect), paint);
            //paint.setPathEffect(nullptr);
        }

        if (mHoveredPoint_d) {
            mHoveredPoint_d->drawHovered(canvas, invZoom);
        } else if (mHoveredNormalSegment.isValid()) {
            mHoveredNormalSegment.drawHoveredSk(canvas, invZoom);
        } else if (mHoveredBox) {
            if (!mCurrentNormalSegment.isValid()) {
                mHoveredBox->drawHoveredSk(canvas, invZoom);
            }
        }
    //}

    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(invZoom);
    paint.setColor(SK_ColorGRAY);
    paint.setPathEffect(nullptr);
    canvas->drawRect(canvasRect, paint);

    canvas->resetMatrix();

    if (mTransMode != TransformMode::none || mValueInput.inputEnabled()) {
        mValueInput.draw(canvas, drawRect.height() - eSizesUI::widget);
    }
}

void Canvas::setCanvasSize(const int width,
                           const int height)
{
    if (width == mWidth && height == mHeight) { return; }
    {
        prp_pushUndoRedoName(tr("Scene Dimension Changed"));
        UndoRedo ur;
        const QSize origSize{mWidth, mHeight};
        const QSize newSize{width, height};
        ur.fUndo = [this, origSize]() {
            setCanvasSize(origSize.width(),
                          origSize.height());
        };
        ur.fRedo = [this, newSize]() {
            setCanvasSize(newSize.width(),
                          newSize.height());
        };
        prp_addUndoRedo(ur);
    }
    mWidth = width;
    mHeight = height;
    prp_afterWholeInfluenceRangeChanged();
    emit dimensionsChanged(width, height);
}

void Canvas::setFrameRange(const FrameRange &range,
                           const bool undo)
{
    if (undo) {
        {
            prp_pushUndoRedoName(tr("Frame Range Changed"));
            UndoRedo ur;
            const FrameRange origRange(mRange);
            const FrameRange newRange(range);
            ur.fUndo = [this, origRange]() {
                setFrameRange(origRange);
            };
            ur.fRedo = [this, newRange]() {
                setFrameRange(newRange);
            };
            prp_addUndoRedo(ur);
        }
    }
    mRange = range;
    emit newFrameRange(range);
}

void Canvas::setFrameIn(const bool enabled,
                        const int frameIn)
{
    if (enabled && mOut.enabled && frameIn >= mOut.frame) { return; }
    const auto oIn = mIn;
    mIn.enabled = enabled;
    mIn.frame = frameIn;
    emit requestUpdate();
    {
        prp_pushUndoRedoName(tr("Frame In Changed"));
        UndoRedo ur;
        ur.fUndo = [this, oIn]() { setFrameIn(oIn.enabled, oIn.frame); };
        ur.fRedo = [this, enabled, frameIn]() { setFrameIn(enabled, frameIn); };
        prp_addUndoRedo(ur);
    }
}

void Canvas::setFrameOut(const bool enabled,
                         const int frameOut)
{
    if (enabled && mIn.enabled && frameOut <= mIn.frame) { return; }
    const auto oOut = mOut;
    mOut.enabled = enabled;
    mOut.frame = frameOut;
    emit requestUpdate();
    {
        prp_pushUndoRedoName(tr("Frame Out Changed"));
        UndoRedo ur;
        ur.fUndo = [this, oOut]() { setFrameOut(oOut.enabled, oOut.frame); };
        ur.fRedo = [this, enabled, frameOut]() { setFrameOut(enabled, frameOut); };
        prp_addUndoRedo(ur);
    }
}

const FrameMarker Canvas::getFrameIn() const
{
    return mIn;
}

const FrameMarker Canvas::getFrameOut() const
{
    return mOut;
}

void Canvas::clearFrameInOut()
{
    const auto oIn = mIn;
    const auto oOut = mOut;

    mIn.frame = 0;
    mIn.enabled = false;
    mOut.frame = 0;
    mOut.enabled = false;

    emit requestUpdate();
    {
        prp_pushUndoRedoName(tr("Cleared Frame In/Out"));
        UndoRedo ur;
        ur.fUndo = [this, oIn, oOut]() { restoreFrameInOut(oIn, oOut); };
        ur.fRedo = [this]() { clearFrameInOut(); };
        prp_addUndoRedo(ur);
    }
}

void Canvas::restoreFrameInOut(const FrameMarker &frameIn,
                               const FrameMarker &frameOut)
{
    mIn = frameIn;
    mOut = frameOut;
    emit requestUpdate();
}

void Canvas::setMarker(const QString &title,
                       const int frame)
{
    if (hasMarker(frame)) {
        if (!hasMarkerEnabled(frame)) {
            setMarkerEnabled(frame, true);
        } else { removeMarker(frame); }
        return;
    }
    const QString mark = title.isEmpty() ? QString::number(mMarkers.size()) : title;
    mMarkers.push_back({mark, true, frame});
    emit requestUpdate();
    {
        prp_pushUndoRedoName(tr("Added Marker"));
        UndoRedo ur;;
        ur.fUndo = [this, frame]() { removeMarker(frame); };
        ur.fRedo = [this, mark, frame]() { setMarker(mark, frame); };
        prp_addUndoRedo(ur);
    }
}

void Canvas::setMarker(const int frame)
{
    setMarker(QString::number(mMarkers.size()), frame);
    emit markersChanged();
}

void Canvas::setMarkerEnabled(const int frame,
                              const bool &enabled)
{
    const int index = getMarkerIndex(frame);
    if (index < 0) { return; }
    mMarkers.at(index).enabled = enabled;
    updateMarkers();
    {
        prp_pushUndoRedoName(tr("Changed Marker State"));
        UndoRedo ur;;
        ur.fUndo = [this, frame, enabled]() { setMarkerEnabled(frame, !enabled); };
        ur.fRedo = [this, frame, enabled]() { setMarkerEnabled(frame, enabled); };
        prp_addUndoRedo(ur);
    }
}

bool Canvas::hasMarker(const int frame,
                       const bool removeExists)
{
    int index = 0;
    for (const auto &mark: mMarkers) {
        if (mark.frame == frame) {
            if (removeExists) {
                mMarkers.erase(mMarkers.begin() + index);
                emit newFrameRange(mRange);
                {
                    prp_pushUndoRedoName(tr("Removed Marker"));
                    UndoRedo ur;;
                    ur.fUndo = [this, mark]() { setMarker(mark.title, mark.frame); };
                    ur.fRedo = [this, mark]() { removeMarker(mark.frame); };
                    prp_addUndoRedo(ur);
                }
            }
            return true;
        }
        index++;
    }
    return false;
}

bool Canvas::hasMarkerIn(const int frame)
{
    return mIn.enabled && mIn.frame == frame;
}

bool Canvas::hasMarkerOut(const int frame)
{
    return mOut.enabled && mOut.frame == frame;
}

bool Canvas::hasMarkerEnabled(const int frame)
{
    for (const auto &mark : mMarkers) {
        if (mark.frame == frame) { return mark.enabled; }
    }
    return false;
}

bool Canvas::removeMarker(const int frame)
{
    return hasMarker(frame, true);
}

bool Canvas::editMarker(const int frame,
                        const QString &title,
                        const bool enabled)
{
    int index = getMarkerIndex(frame);
    if (index >= 0) {
        const auto mark = mMarkers.at(index);
        mMarkers.at(index).title = title;
        mMarkers.at(index).enabled = enabled;
        emit newFrameRange(mRange);
        {
            prp_pushUndoRedoName(tr("Changed Marker"));
            UndoRedo ur;;
            ur.fUndo = [this, mark]() { editMarker(mark.frame, mark.title, mark.enabled); };
            ur.fRedo = [this, frame, title, enabled]() { editMarker(frame, title, enabled); };
            prp_addUndoRedo(ur);
        }
        return true;
    }
    return false;
}

void Canvas::moveMarkerFrame(const int markerFrame,
                             const int newFrame)
{
    if (markerFrame == newFrame) { return; }
    int index = getMarkerIndex(markerFrame);
    if (index >= 0) {
        mMarkers.at(index).frame = newFrame;
        emit newFrameRange(mRange);
        emit markersChanged();
        {
            prp_pushUndoRedoName(tr("Moved Marker"));
            UndoRedo ur;
            ur.fUndo = [this, markerFrame, newFrame]() { moveMarkerFrame(newFrame, markerFrame); };
            ur.fRedo = [this, markerFrame, newFrame]() { moveMarkerFrame(markerFrame, newFrame); };
            prp_addUndoRedo(ur);
        }
    }
}

const QString Canvas::getMarkerText(int frame)
{
    for (const auto &mark: mMarkers) {
        if (mark.frame == frame) { return mark.title; }
    }
    return QString();
}

int Canvas::getMarkerIndex(const int frame)
{
    for (size_t i = 0; i < mMarkers.size(); i++) {
        if (mMarkers.at(i).frame == frame) { return i; }
    }
    return -1;
}

const std::vector<FrameMarker> Canvas::getMarkers()
{
    return mMarkers;
}

void Canvas::clearMarkers()
{
    const auto markers = mMarkers;
    mMarkers.clear();
    emit markersChanged();
    emit requestUpdate();
    {
        prp_pushUndoRedoName(tr("Cleared Markers"));
        UndoRedo ur;
        ur.fUndo = [this, markers]() { restoreMarkers(markers); };
        ur.fRedo = [this]() { clearMarkers(); };
        prp_addUndoRedo(ur);
    }
}

void Canvas::updateMarkers()
{
    emit newFrameRange(mRange);
    emit requestUpdate();
}

void Canvas::restoreMarkers(const std::vector<FrameMarker> &markers)
{
    mMarkers = markers;
    updateMarkers();
}

void Canvas::addKeySelectedProperties()
{
    for (const auto &prop : mSelectedProps.getList()) {
        const auto asAnim = enve_cast<Animator*>(prop);
        if (!asAnim) { continue; }
        asAnim->anim_saveCurrentValueAsKey();
    }
    mDocument.actionFinished();
}

stdsptr<BoxRenderData> Canvas::createRenderData() {
    return enve::make_shared<CanvasRenderData>(this);
}

QSize Canvas::getCanvasSize() {
    return QSize(mWidth, mHeight);
}

void Canvas::setPreviewing(const bool bT) {
    mPreviewing = bT;
}

void Canvas::setRenderingPreview(const bool bT) {
    mRenderingPreview = bT;
}

void Canvas::anim_scaleTime(const int pivotAbsFrame, const qreal scale) {
    ContainerBox::anim_scaleTime(pivotAbsFrame, scale);
    //        int newAbsPos = qRound(scale*pivotAbsFrame);
    //        anim_shiftAllKeys(newAbsPos - pivotAbsFrame);
    const int newMin = qRound((mRange.fMin - pivotAbsFrame)*scale);
    const int newMax = qRound((mRange.fMax - pivotAbsFrame)*scale);
    setFrameRange({newMin, newMax});
}

void Canvas::setOutputRendering(const bool bT) {
    mRenderingOutput = bT;
}

// safe fetch: a strong reference taken IMMEDIATELY from the raw
// handler pointer. The handler holds strong refs internally, but the
// memory-checker eviction (freeMemory -> noDataLeft_k -> remove from
// handler -> last ref drops -> object destroyed) can race with queued
// callbacks that cached the raw pointer across event-loop passes.
// ref() on a destroyed object reads freed memory (SelfRef::mThisWeak)
// and crashes with an access violation.
static stdsptr<SceneFrameContainer> safeSceneFrame(
        HddCachableCacheHandler& handler,
        const int relFrame) {
    const auto raw = handler.atFrame<SceneFrameContainer>(relFrame);
    if(!raw) return nullptr;
    try {
        return raw->ref<SceneFrameContainer>();
    } catch(...) { return nullptr; }
}

void Canvas::setSceneFrame(const int relFrame) {
    const auto cont = safeSceneFrame(mSceneFramesHandler, relFrame);
    if(!cont) {
        // no container for this frame (e.g. the cache was dropped by an
        // edit right before playback): keep the current frame on
        // screen instead of assigning null, which made the preview
        // branch draw nothing and the canvas go black
        return;
    }
    if(!cont->storesDataInMemory()) {
        // The frame's pixels were swapped out to a tmp file. Keep the
        // current frame on screen and reload asynchronously instead of
        // assigning a container with a null image, which used to draw
        // a blank transparent canvas (flickering during playback).
        setLoadingSceneFrame(enve::shared<SceneFrameContainer>(cont));
        return;
    }
    setSceneFrame(cont);
}

void Canvas::setSceneFrame(const stdsptr<SceneFrameContainer>& cont) {
    setLoadingSceneFrame(nullptr);
    mSceneFrame = cont;
    emit requestUpdate();
}

void Canvas::scheduleLoadMissingSceneFrames(const int minRelFrame,
                                            const int maxRelFrame) {
    for(int relFrame = minRelFrame; relFrame <= maxRelFrame; relFrame++) {
        const auto cont = safeSceneFrame(mSceneFramesHandler, relFrame);
        if(cont && !cont->storesDataInMemory())
            cont->scheduleLoadFromTmpFile();
    }
}

void Canvas::setLoadingSceneFrame(const stdsptr<SceneFrameContainer>& cont) {
    if(mLoadingSceneFrame == cont) return;
    mLoadingSceneFrame = cont;
    if(cont) {
        Q_ASSERT(!cont->storesDataInMemory());
        cont->scheduleLoadFromTmpFile();
    }
}

FrameRange Canvas::prp_getIdenticalRelRange(const int relFrame) const {
    const auto groupRange = ContainerBox::prp_getIdenticalRelRange(relFrame);
    //FrameRange canvasRange{0, mMaxFrame};
    return groupRange;//*canvasRange;
}

void Canvas::renderDataFinished(BoxRenderData *renderData) {
    const bool currentState = renderData->fBoxStateId == mStateId;
    if(currentState) mRenderDataHandler.removeItemAtRelFrame(renderData->fRelFrame);
    else if(renderData->fBoxStateId < mLastStateId) {
        // stale completion, will never land in the cache - count it so
        // the preview pipeline watchdog can re-feed the frame at once
        mRenderDataDiscardCount++;
        return;
    }
    const int relFrame = qRound(renderData->fRelFrame);
    mLastStateId = renderData->fBoxStateId;

    auto range = prp_getIdenticalRelRange(relFrame);
    if(!range.inRange(relFrame)) {
        // identical-range computation produced a range that does not
        // even cover the frame it was computed for (invalid/shifted
        // intersection) - clamp to the rendered frame so the container
        // always covers it; otherwise the preview pipeline's in-flight
        // retirement check never matches this frame and the warm-up
        // stalls forever. Cache granularity degrades, correctness does
        // not: the frame's pixels are the pixels it rendered.
        qWarning() << "renderDataFinished: identical range"
                   << range.fMin << range.fMax
                   << "does not cover rel frame" << relFrame
                   << "- clamping to single frame";
        range = {relFrame, relFrame};
    }
    const auto cont = enve::make_shared<SceneFrameContainer>(
                this, renderData, range,
                currentState ? &mSceneFramesHandler : nullptr);
    if(currentState) {
        mSceneFramesHandler.add(cont);
        // event-driven pipeline: wakes the preview/output feeder
        // immediately instead of waiting for the next timer tick
        emit sceneFrameCached();
    } else {
        // non-current-state completion gets a null handler and never
        // lands in the cache - count it as a discard as well
        mRenderDataDiscardCount++;
    }

    if(!mPreviewing && !mRenderingOutput){
        bool newerSate = true;
        bool closerFrame = true;
        if(mSceneFrame) {
            newerSate = mSceneFrame->fBoxState < renderData->fBoxStateId;
            const int cRelFrame = anim_getCurrentRelFrame();
            const int finishedFrameDist = qMin(qAbs(cRelFrame - range.fMin),
                                               qAbs(cRelFrame - range.fMax));
            const FrameRange cRange = mSceneFrame->getRange();
            const int oldFrameDist = qMin(qAbs(cRelFrame - cRange.fMin),
                                          qAbs(cRelFrame - cRange.fMax));
            closerFrame = finishedFrameDist < oldFrameDist;
        }
        if(newerSate || closerFrame) {
            mSceneFrameOutdated = !currentState;
            setSceneFrame(cont);
        }
    }
}

void Canvas::prp_afterChangedAbsRange(const FrameRange &range, const bool clip) {
    Property::prp_afterChangedAbsRange(range, clip);
    mSceneFramesHandler.remove(range);
    if(!mSceneFramesHandler.atFrame(anim_getCurrentRelFrame())) {
        mSceneFrameOutdated = true;
        planUpdate(UpdateReason::userChange);
    }
}

void Canvas::saveSceneSVG(SvgExporter& exp) const
{
    auto &svg = exp.svg();
    if (exp.fColors11) {
        svg.setAttribute("version", "1.1");
    }
    svg.setAttribute("xmlns", "http://www.w3.org/2000/svg");
    svg.setAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");

    const auto viewBox = QString("0 0 %1 %2").
                         arg(mWidth).arg(mHeight);
    svg.setAttribute("viewBox", viewBox);

    if (exp.fFixedSize) {
        svg.setAttribute("width", mWidth);
        svg.setAttribute("height", mHeight);
    }

    for (const auto &grad : mGradients) {
        grad->saveSVG(exp);
    }

    if (exp.fBackground) {
        auto bg = exp.createElement("rect");
        bg.setAttribute("width", mWidth);
        bg.setAttribute("height", mHeight);
        mBackgroundColor->saveColorSVG(exp, bg, exp.fAbsRange, "fill");
        svg.appendChild(bg);
    }

    const auto task = enve::make_shared<DomEleTask>(exp, exp.fAbsRange);
    exp.addNextTask(task);
    saveBoxesSVG(exp, task.get(), svg);
    task->queTask();
}

qsptr<BoundingBox> Canvas::createLink(const bool inner)
{
    return enve::make_shared<InternalLinkCanvas>(this, inner);
}

void Canvas::schedulePivotUpdate()
{
    if (mTransMode == TransformMode::rotate ||
        mTransMode == TransformMode::rotateX ||
        mTransMode == TransformMode::rotateY ||
        mTransMode == TransformMode::scale ||
        mRotPivot->isSelected()) { return; }
    mPivotUpdateNeeded = true;
}

void Canvas::updatePivotIfNeeded()
{
    if (mPivotUpdateNeeded) {
        mPivotUpdateNeeded = false;
        updatePivot();
    }
}

void Canvas::makePointCtrlsSymmetric()
{
    prp_pushUndoRedoName(tr("Make Nodes Symmetric"));
    setPointCtrlsMode(CtrlsMode::symmetric);
}

void Canvas::makePointCtrlsSmooth()
{
    prp_pushUndoRedoName(tr("Make Nodes Smooth"));
    setPointCtrlsMode(CtrlsMode::smooth);
}

void Canvas::makePointCtrlsCorner()
{
    prp_pushUndoRedoName(tr("Make Nodes Corner"));
    setPointCtrlsMode(CtrlsMode::corner);
}

void Canvas::newEmptyPaintFrameAction()
{
    //if(mPaintTarget.isValid()) mPaintTarget.newEmptyFrame();
}

void Canvas::moveSecondSelectionPoint(const QPointF &pos)
{
    mSelectionRect.setBottomRight(pos);
}

void Canvas::startSelectionAtPoint(const QPointF &pos)
{
    mSelecting = true;
    mSelectionRect.setTopLeft(pos);
    mSelectionRect.setBottomRight(pos);
}

void Canvas::updatePivot()
{
    const bool havePoints = !mSelectedPoints_d.isEmpty();
    if (mCurrentMode == CanvasMode::pointTransform && havePoints) {
        mRotPivot->setAbsolutePos(getSelectedPointsAbsPivotPos());
        mDocument.fPivotPosForGizmosValid = false;
    } else if (mCurrentMode == CanvasMode::boxTransform ||
               mCurrentMode == CanvasMode::pointTransform) {
        // no point selection in node mode: fall back to the boxes'
        // pivot (an empty point set used to send the handle to the
        // canvas origin, reading as "the pivot does not follow")
        mRotPivot->setAbsolutePos(getSelectedBoxesAbsPivotPos());
        mDocument.fPivotPosForGizmosValid = false;
    }
}

void Canvas::setCanvasMode(const CanvasMode mode)
{
    if (mCurrentMode == CanvasMode::pickFillStroke ||
        mCurrentMode == CanvasMode::pickFillStrokeEvent) {
        emit currentPickedColor(QColor());
        emit currentHoverColor(QColor());
    }
    mCurrentMode = mode;
    mSelecting = false;
    mStylusDrawing = false;
    clearPointsSelection();
    clearCurrentSmartEndPoint();
    clearLastPressedPoint();
    updatePivot();
    //updatePaintBox();
    emit canvasModeSet(mode);
}

/*void Canvas::updatePaintBox()
{
    mPaintTarget.setPaintBox(nullptr);
    if (mCurrentMode != CanvasMode::paint) { return; }
    for (int i = mSelectedBoxes.count() - 1; i >= 0; i--) {
        const auto& iBox = mSelectedBoxes.at(i);
        if (enve_cast<PaintBox*>(iBox)) {
            mPaintTarget.setPaintBox(static_cast<PaintBox*>(iBox));
            break;
        }
    }
}*/

/*bool Canvas::handlePaintModeKeyPress(const eKeyEvent &e)
{
    if (mCurrentMode != CanvasMode::paint) { return false; }
    if (e.fKey == Qt::Key_N && mPaintTarget.isValid()) {
        newEmptyPaintFrameAction();
    } else { return false; }
    return true;
}*/

bool Canvas::handleModifierChange(const eKeyEvent &e)
{
    if (mCurrentMode == CanvasMode::pointTransform) {
        if (e.fKey == Qt::Key_Alt ||
            e.fKey == Qt::Key_Shift ||
            e.fKey == Qt::Key_Meta) {
            handleMovePointMouseMove(e);
            return true;
        } else if (e.fKey == Qt::Key_Control) { return true; }
    }
    return false;
}

bool Canvas::handleTransormationInputKeyEvent(const eKeyEvent &e)
{
    if (mValueInput.handleTransormationInputKeyEvent(e.fKey)) {
        if (mTransMode == TransformMode::rotate) { mValueInput.setupRotate(); }
        else if (mTransMode == TransformMode::rotateX) { mValueInput.setupRotateX(); }
        else if (mTransMode == TransformMode::rotateY) { mValueInput.setupRotateY(); }
        updateTransformation(e);
        mStartTransform = false;
    } else if (e.fKey == Qt::Key_Escape) {
        if (!e.fMouseGrabbing) { return false; }
        cancelCurrentTransform();
        e.fReleaseMouse();
    } else if (e.fKey == Qt::Key_Return ||
               e.fKey == Qt::Key_Enter) {
        handleLeftMouseRelease(e);
    } else if (e.fKey == Qt::Key_X) {
        if (e.fAutorepeat) { return false; }
        mValueInput.switchXOnlyMode();
        const bool linesChanged = updateLineGizmoVisibility();
        updateTransformation(e);
        if (linesChanged) { emit requestUpdate(); }
    } else if (e.fKey == Qt::Key_Y) {
        if (e.fAutorepeat) { return false; }
        mValueInput.switchYOnlyMode();
        const bool linesChanged = updateLineGizmoVisibility();
        updateTransformation(e);
        if (linesChanged) { emit requestUpdate(); }
    } else if (e.fKey == Qt::Key_Z) {
        if (e.fAutorepeat) { return false; }
        if (mCurrentMode != CanvasMode::boxTransform) { return false; }
        mValueInput.switchZOnlyMode();
        const bool linesChanged = updateLineGizmoVisibility();
        updateTransformation(e);
        if (linesChanged) { emit requestUpdate(); }
    } else { return false; }
    return true;
}

void Canvas::deleteAction()
{
    switch(mCurrentMode) {
    case CanvasMode::pointTransform:
        removeSelectedPointsAndClearList();
        break;
    case CanvasMode::boxTransform:
    case CanvasMode::circleCreate:
    case CanvasMode::rectCreate:
    case CanvasMode::textCreate:
    case CanvasMode::nullCreate:
    case CanvasMode::drawPath:
    case CanvasMode::pathCreate:
        removeSelectedBoxesAndClearList();
        break;
    default:;
    }
}

void Canvas::copyAction()
{
    if (mSelectedBoxes.isEmpty()) { return; }
    const auto container = enve::make_shared<BoxesClipboard>(mSelectedBoxes.getList());
    Document::sInstance->replaceClipboard(container);
}

void Canvas::pasteAction()
{
    const auto container = Document::sInstance->getBoxesClipboard();
    if (!container) { return; }
    clearBoxesSelection();
    container->pasteTo(mCurrentContainer);
}

void Canvas::cutAction()
{
    if (mSelectedBoxes.isEmpty()) { return; }
    copyAction();
    deleteAction();
}

void Canvas::splitAction()
{
    if (mSelectedBoxes.isEmpty() || mSelectedBoxes.count() > 1) { return; }

    const auto bBox = enve_cast<BoundingBox*>(mSelectedBoxes.getList().at(0));
    if (!bBox) { return; }

    const auto dRect = bBox->getDurationRectangle();
    if (!dRect) { return; }

    const auto frame = getCurrentFrame();
    const auto range = dRect->getAbsFrameRange();

    if (!range.inRange(frame)) { return; }

    // one named undo set for the whole split: paste + both duration
    // trims; the trims go through the undoable transform path (same
    // as the in/out point actions) - raw setValues would stay out of
    // the undo stack, so undo removed the pasted copy but kept the
    // original trimmed
    pushUndoRedoName(tr("Split Clip"));

    copyAction();
    pasteAction();

    if (mCurrentContainer->getContainedBoxesCount() < 1) { return; }

    const auto box = mCurrentContainer->getContainedBoxes().at(0);
    if (!box) { return; }

    const auto cRect = box->getDurationRectangle();
    if (!cRect) { return; }

    bBox->startMinFramePosTransform();
    dRect->setMinAbsFrame(frame);
    bBox->finishMinFramePosTransform();

    box->startMaxFramePosTransform();
    cRect->setMaxAbsFrame(frame);
    box->finishMaxFramePosTransform();

    for (int i = box->getZIndex(); i < bBox->getZIndex(); i = box->getZIndex()) {
        box->moveDown();
    }

    mSelectedBoxes.removeObj(box);
    box->setSelected(false);
    mSelectedBoxes.addObj(bBox);
    bBox->setSelected(true);

    mDocument.actionFinished();
}

void Canvas::duplicateAction()
{
    if (mSelectedBoxes.isEmpty()) { return; }

    const auto originals = mSelectedBoxes.getList();
    clearBoxesSelection();

    for (auto* box : originals) {
        if (!box) continue;

        ContainerBox* targetContainer = box->getParentGroup();
        if (!targetContainer) { targetContainer = mCurrentContainer; }

        const int originalZIndex = box->getZIndex();
        const QString originalName = box->prp_getName();

        QList<BoundingBox*> singleList;
        singleList.append(box);

        const auto tempClipboard = enve::make_shared<BoxesClipboard>(singleList);
        tempClipboard->pasteTo(targetContainer);

        if (mLastSelectedBox && mLastSelectedBox != box) {
            mLastSelectedBox->moveTo(originalZIndex);
            mLastSelectedBox->prp_setName(originalName + " Copy");
        }
    }
}

void Canvas::selectAllAction()
{
    if (mCurrentMode == CanvasMode::pointTransform) {
        selectAllPointsAction();
    } else {//if(mCurrentMode == MOVE_PATH) {
        selectAllBoxesFromBoxesGroup();
    }
}

void Canvas::invertSelectionAction()
{
    if (mCurrentMode == CanvasMode::pointTransform) {
        QList<MovablePoint*> selectedPts = mSelectedPoints_d;
        selectAllPointsAction();
        for (const auto &pt : selectedPts) { removePointFromSelection(pt); }
    } else {//if(mCurrentMode == MOVE_PATH) {
        QList<BoundingBox*> boxes = mSelectedBoxes.getList();
        selectAllBoxesFromBoxesGroup();
        for (const auto &box : boxes) { removeBoxFromSelection(box); }
    }
}

void Canvas::anim_setAbsFrame(const int frame)
{
    if (frame == anim_getCurrentAbsFrame()) { return; }
    ContainerBox::anim_setAbsFrame(frame);
    const int newRelFrame = anim_getCurrentRelFrame();

    const auto cont = safeSceneFrame(mSceneFramesHandler, newRelFrame);
    if (cont) {
        if (cont->storesDataInMemory()) {
            setSceneFrame(cont);
        } else {
            setLoadingSceneFrame(cont);
        }
        mSceneFrameOutdated = !cont->storesDataInMemory();
    } else {
        mSceneFrameOutdated = true;
        planUpdate(UpdateReason::frameChange);
    }

    mUndoRedoStack->setFrame(frame);

    //if (mCurrentMode == CanvasMode::paint) { mPaintTarget.setupOnionSkin(); }
    emit currentFrameChanged(frame);

    schedulePivotUpdate();
}

void Canvas::clearSelectionAction()
{
    if (mCurrentMode == CanvasMode::pointTransform) {
        clearPointsSelection();
    } else {//if(mCurrentMode == MOVE_PATH) {
        clearPointsSelection();
        clearBoxesSelection();
    }
}

void Canvas::finishedAction()
{
    mDocument.actionFinished();
}

void Canvas::clearParentForSelected()
{
    for (int i = 0; i < mSelectedBoxes.count(); i++) {
        mSelectedBoxes.at(i)->clearParent();
    }
}

void Canvas::setParentToLastSelected()
{
    if (mSelectedBoxes.count() > 1) {
        const auto& lastBox = mSelectedBoxes.last();
        const auto trans = lastBox->getTransformAnimator();
        for (int i = 0; i < mSelectedBoxes.count() - 1; i++) {
            mSelectedBoxes.at(i)->setParentTransform(trans);
        }
    }
}

bool Canvas::startRotatingAction(const eKeyEvent &e)
{
    if (!prepareRotation(e.fPos)) { return false; }
    e.fGrabMouse();
    return true;
}

bool Canvas::startScalingAction(const eKeyEvent &e)
{
    if (mCurrentMode != CanvasMode::boxTransform &&
        mCurrentMode != CanvasMode::pointTransform) { return false; }

    if (mSelectedBoxes.isEmpty()) { return false; }
    if (mCurrentMode == CanvasMode::pointTransform) {
        if (mSelectedPoints_d.isEmpty()) { return false; }
    }
    mValueInput.clearAndDisableInput();
    mValueInput.setupScale();

    mRotPivot->setMousePos(e.fPos);
    mTransMode = TransformMode::scale;
    mDoubleClick = false;
    mStartTransform = true;
    e.fGrabMouse();
    return true;
}

bool Canvas::startMovingAction(const eKeyEvent &e)
{
    if (mCurrentMode != CanvasMode::boxTransform &&
        mCurrentMode != CanvasMode::pointTransform) { return false; }
    mValueInput.clearAndDisableInput();
    mValueInput.setupMove();

    mTransMode = TransformMode::move;
    mDoubleClick = false;
    mStartTransform = true;
    e.fGrabMouse();
    return true;
}

void Canvas::selectAllBoxesAction()
{
    mCurrentContainer->selectAllBoxesFromBoxesGroup();
}

void Canvas::deselectAllBoxesAction()
{
    mCurrentContainer->deselectAllBoxesFromBoxesGroup();
}

void Canvas::selectAllPointsAction()
{
    const auto adder = [this](MovablePoint* const pt) {
        addPointToSelection(pt);
    };
    for (const auto& box : mSelectedBoxes) {
        box->selectAllCanvasPts(adder, mCurrentMode);
    }
}

void Canvas::selectOnlyLastPressedBox()
{
    clearBoxesSelection();
    if (mPressedBox) { addBoxToSelection(mPressedBox); }
}

void Canvas::selectOnlyLastPressedPoint()
{
    clearPointsSelection();
    if (mPressedPoint) { addPointToSelection(mPressedPoint); }
}

bool Canvas::SWT_shouldBeVisible(const SWT_RulesCollection &rules,
                                 const bool parentSatisfies,
                                 const bool parentMainTarget) const
{
    Q_UNUSED(parentSatisfies)
    Q_UNUSED(parentMainTarget)
    const SWT_BoxRule rule = rules.fRule;
    const bool alwaysShowChildren = rules.fAlwaysShowChildren;
    if (alwaysShowChildren) {
        return false;
    } else {
        if (rules.fType == SWT_Type::sound) { return false; }

        if (rule == SWT_BoxRule::all) {
            return true;
        } else if (rule == SWT_BoxRule::selected) {
            return false;
        } else if (rule == SWT_BoxRule::animated) {
            return false;
        } else if (rule == SWT_BoxRule::notAnimated) {
            return false;
        } else if (rule == SWT_BoxRule::visible) {
            return true;
        } else if (rule == SWT_BoxRule::hidden) {
            return false;
        } else if (rule == SWT_BoxRule::locked) {
            return false;
        } else if (rule == SWT_BoxRule::unlocked) {
            return true;
        }
    }
    return false;
}

int Canvas::getCurrentFrame() const
{
    return anim_getCurrentAbsFrame();
}

HddCachableCacheHandler &Canvas::getSoundCacheHandler()
{
    return mSoundComposition->getCacheHandler();
}

// selected sounds live outside the canvas box selection; run func on
// each of them (keeps the "all selected" timeline operations complete)
void Canvas::forEachSelectedSound(
        const std::function<void(eBoxOrSound*)>& func) {
    // inert during the teardown (see Canvas::~Canvas)
    if(mDestructing) return;
    const auto comp = mSoundComposition.data();
    if(!comp) return;
    for(const auto& sound : comp->getSounds()) {
        if(sound->isSelected()) {
            func(static_cast<eBoxOrSound*>(sound.data()));
        }
    }
}

void Canvas::startDurationRectPosTransformForAllSelected()
{
    for (const auto &box : mSelectedBoxes) {
        box->startDurationRectPosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->startDurationRectPosTransform(); });
}

void Canvas::finishDurationRectPosTransformForAllSelected()
{
    for (const auto &box : mSelectedBoxes) {
        box->finishDurationRectPosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->finishDurationRectPosTransform(); });
}

void Canvas::cancelDurationRectPosTransformForAllSelected()
{
    for (const auto &box : mSelectedBoxes) {
        box->cancelDurationRectPosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->cancelDurationRectPosTransform(); });
}

void Canvas::moveDurationRectForAllSelected(const int dFrame)
{
    for (const auto& box : mSelectedBoxes) {
        box->moveDurationRect(dFrame);
    }
    forEachSelectedSound([dFrame](eBoxOrSound* s) { s->moveDurationRect(dFrame); });
}

void Canvas::startMinFramePosTransformForAllSelected()
{
    for (const auto& box : mSelectedBoxes) {
        box->startMinFramePosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->startMinFramePosTransform(); });
}

void Canvas::finishMinFramePosTransformForAllSelected()
{
    for (const auto& box : mSelectedBoxes) {
        box->finishMinFramePosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->finishMinFramePosTransform(); });
}

void Canvas::cancelMinFramePosTransformForAllSelected()
{
    for (const auto& box : mSelectedBoxes) {
        box->cancelMinFramePosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->cancelMinFramePosTransform(); });
}

void Canvas::moveMinFrameForAllSelected(const int dFrame)
{
    for (const auto& box : mSelectedBoxes) {
        box->moveMinFrame(dFrame);
    }
    forEachSelectedSound([dFrame](eBoxOrSound* s) { s->moveMinFrame(dFrame); });
}

void Canvas::startMaxFramePosTransformForAllSelected()
{
    for (const auto& box : mSelectedBoxes) {
        box->startMaxFramePosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->startMaxFramePosTransform(); });
}

void Canvas::finishMaxFramePosTransformForAllSelected()
{
    for (const auto& box : mSelectedBoxes) {
        box->finishMaxFramePosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->finishMaxFramePosTransform(); });
}


void Canvas::cancelMaxFramePosTransformForAllSelected()
{
    for (const auto& box : mSelectedBoxes) {
        box->cancelMaxFramePosTransform();
    }
    forEachSelectedSound([](eBoxOrSound* s) { s->cancelMaxFramePosTransform(); });
}

void Canvas::moveMaxFrameForAllSelected(const int dFrame)
{
    for (const auto& box : mSelectedBoxes) {
        box->moveMaxFrame(dFrame);
    }
    forEachSelectedSound([dFrame](eBoxOrSound* s) { s->moveMaxFrame(dFrame); });
}

bool Canvas::newUndoRedoSet()
{
    return mUndoRedoStack->newCollection();
}

void Canvas::undo()
{
    mUndoRedoStack->undo();
}

void Canvas::redo()
{
    mUndoRedoStack->redo();
}

UndoRedoStack::StackBlock Canvas::blockUndoRedo()
{
    return mUndoRedoStack->blockUndoRedo();
}

void Canvas::addUndoRedo(const QString& name,
                         const stdfunc<void()>& undo,
                         const stdfunc<void()>& redo)
{
    qDebug() << "addUndoRedo" << name;
    mUndoRedoStack->addUndoRedo(name, undo, redo);
}

void Canvas::pushUndoRedoName(const QString& name) const
{
    qDebug() << "pushUndoRedoName" << name;
    mUndoRedoStack->pushName(name);
}

SoundComposition *Canvas::getSoundComposition()
{
    return mSoundComposition.get();
}

void Canvas::writeSettings(eWriteStream& dst) const
{
    dst << getCurrentFrame();
    dst << mClipToCanvasSize;
    dst << mWidth;
    dst << mHeight;
    dst << mFps;
    dst << mRange;

    writeMarkers(dst);

    // ruler guides (unconditional write, version-gated read)
    dst << int(mHGuides.count());
    for (const qreal y : mHGuides) { dst << y; }
    dst << int(mVGuides.count());
    for (const qreal x : mVGuides) { dst << x; }
}

void Canvas::readSettings(eReadStream& src)
{
    int currFrame;
    src >> currFrame;
    src >> mClipToCanvasSize;
    src >> mWidth;
    src >> mHeight;
    src >> mFps;
    FrameRange range;
    src >> range;
    if (src.evFileVersion() >= EvFormat::markers) {
        readMarkers(src);
    }
    if (src.evFileVersion() >= EvFormat::canvasGuides) {
        int count = 0;
        src >> count;
        mHGuides.clear();
        for (int i = 0; i < count; i++) {
            qreal y; src >> y; mHGuides << y;
        }
        src >> count;
        mVGuides.clear();
        for (int i = 0; i < count; i++) {
            qreal x; src >> x; mVGuides << x;
        }
    }
    setFrameRange(range, false);
    anim_setAbsFrame(currFrame);
}

void Canvas::writeBoundingBox(eWriteStream& dst) const
{
    writeGradients(dst);
    ContainerBox::writeBoundingBox(dst);
    clearGradientRWIds();
}

void Canvas::readBoundingBox(eReadStream& src)
{
    if (src.evFileVersion() > 5) { readGradients(src); }
    ContainerBox::readBoundingBox(src);
    if (src.evFileVersion() < EvFormat::readSceneSettingsBeforeContent) {
        readSettings(src);
    }
    clearGradientRWIds();
}

void Canvas::writeMarkers(eWriteStream &dst) const
{
    dst << mIn.enabled;
    dst << mIn.frame;
    dst << mOut.enabled;
    dst << mOut.frame;
    QStringList markers;
    for (auto &marker: mMarkers) {
        QString title = marker.title.isEmpty() ? tr("Marker") : marker.title;
        markers << QString("%1:%2:%3").arg(title,
                                           QString::number(marker.frame),
                                           QString::number(marker.enabled ? 1 : 0));
    }
    dst << markers.join(",").toUtf8();
}

void Canvas::readMarkers(eReadStream &src)
{
    src >> mIn.enabled;
    src >> mIn.frame;
    src >> mOut.enabled;
    src >> mOut.frame;
    QByteArray markerData;
    src >> markerData;
    mMarkers.clear();
    const auto markers = QString::fromUtf8(markerData).split(",");
    for (auto &marker: markers) {
        const auto content = marker.split(":");
        if (content.size() >= 2) {
            const QString title = content.at(0).isEmpty() ? tr("Marker") : content.at(0);
            const bool enabled = content.size() > 2 ? content.at(2).toInt() : true;
            const int frame = content.at(1).toInt();
            if (hasMarker(frame)) { continue; }
            mMarkers.push_back({title.simplified(), enabled, frame});
        }
    }
}

void Canvas::writeBoxOrSoundXEV(const stdsptr<XevZipFileSaver>& xevFileSaver,
                                const RuntimeIdToWriteId& objListIdConv,
                                const QString& path) const
{
    ContainerBox::writeBoxOrSoundXEV(xevFileSaver, objListIdConv, path);
    auto& fileSaver = xevFileSaver->fileSaver();
    fileSaver.processText(path + "gradients.xml",
                          [&](QTextStream& stream) {
        QDomDocument doc;
        auto gradients = doc.createElement("Gradients");
        int id = 0;
        const auto exp = enve::make_shared<XevExporter>(
                    doc, xevFileSaver, objListIdConv, path);
        for (const auto &grad : mGradients) {
            auto gradient = grad->prp_writePropertyXEV(*exp);
            gradient.setAttribute("id", id++);
            gradients.appendChild(gradient);
        }
        doc.appendChild(gradients);

        stream << doc.toString();
    });
}

void Canvas::readBoxOrSoundXEV(XevReadBoxesHandler& boxReadHandler,
                               ZipFileLoader &fileLoader,
                               const QString &path,
                               const RuntimeIdToWriteId& objListIdConv)
{
    ContainerBox::readBoxOrSoundXEV(boxReadHandler, fileLoader, path, objListIdConv);
    fileLoader.process(path + "gradients.xml",
                       [&](QIODevice* const src) {
        QDomDocument doc;
        doc.setContent(src);
        const auto root = doc.firstChildElement("Gradients");
        const auto gradients = root.elementsByTagName("Gradient");
        for (int i = 0; i < gradients.count(); i++) {
            const auto node = gradients.at(i);
            const auto ele = node.toElement();
            const XevImporter imp(boxReadHandler, fileLoader, objListIdConv, path);
            createNewGradient()->prp_readPropertyXEV(ele, imp);
        }
    });
}

int Canvas::getByteCountPerFrame()
{
    return qCeil(mWidth*mResolution)*qCeil(mHeight*mResolution)*4;
}

void Canvas::readGradients(eReadStream& src)
{
    int nGrads; src >> nGrads;
    for (int i = 0; i < nGrads; i++) {
        createNewGradient()->read(src);
    }
}

void Canvas::writeGradients(eWriteStream &dst) const
{
    dst << mGradients.count();
    int id = 0;
    for (const auto &grad : mGradients) {
        grad->write(id++, dst);
    }
}

SceneBoundGradient *Canvas::createNewGradient()
{
    prp_pushUndoRedoName(tr("Create Gradient"));
    const auto grad = enve::make_shared<SceneBoundGradient>(this);
    addGradient(grad);
    return grad.get();
}

void Canvas::addGradient(const qsptr<SceneBoundGradient>& grad)
{
    prp_pushUndoRedoName(tr("Add Gradient"));
    mGradients.append(grad);
    emit gradientCreated(grad.get());
    {
        UndoRedo ur;
        ur.fUndo = [this, grad]() {
            removeGradient(grad);
        };
        ur.fRedo = [this, grad]() {
            addGradient(grad);
        };
        prp_addUndoRedo(ur);
    }
}

bool Canvas::removeGradient(const qsptr<SceneBoundGradient> &gradient)
{
    const auto guard = gradient;
    if (mGradients.removeOne(gradient)) {
        prp_pushUndoRedoName(tr("Remove Gradient"));
        {
            UndoRedo ur;
            ur.fUndo = [this, guard]() {
                addGradient(guard);
            };
            ur.fRedo = [this, guard]() {
                removeGradient(guard);
            };
            prp_addUndoRedo(ur);
        }
        emit gradient->removed();
        emit gradientRemoved(gradient.data());
        return true;
    }
    return false;
}

SceneBoundGradient *Canvas::getGradientWithRWId(const int rwId) const
{
    for (const auto &grad : mGradients) {
        if (grad->getReadWriteId() == rwId) { return grad.get(); }
    }
    return nullptr;
}

SceneBoundGradient *Canvas::getGradientWithDocumentId(const int id) const
{
    for (const auto &grad : mGradients) {
        if (grad->getDocumentId() == id) { return grad.get(); }
    }
    return nullptr;
}

SceneBoundGradient *Canvas::getGradientWithDocumentSceneId(const int id) const
{
    for (const auto &scene : mDocument.fScenes) {
        for (const auto &grad : scene->mGradients) {
            if (grad->getDocumentId() == id) { return grad.get(); }
        }
    }
    return nullptr;
}

void Canvas::addNullObject(NullObject* const obj)
{
    mNullObjects.append(obj);
}

void Canvas::removeNullObject(NullObject* const obj)
{
    mNullObjects.removeOne(obj);
}

void Canvas::addBone(Bone* const bone)
{
    Bone::diag(QStringLiteral("+bone %1 count=%2")
               .arg(bone ? bone->prp_getName() : QStringLiteral("?"))
               .arg(mBones.count() + 1));
    mBones.append(bone);
}

void Canvas::removeBone(Bone* const bone)
{
    Bone::diag(QStringLiteral("-bone %1 remaining=%2")
               .arg(bone ? bone->prp_getName() : QStringLiteral("?"))
               .arg(mBones.count() - 1));
    mBones.removeOne(bone);
    if(mDraftBone == bone) mDraftBone = nullptr;
    if(mChainTail == bone) mChainTail = nullptr;
}

// begin a bone chain at the given scene position: new bones land in the
// current bone layer (one is created at the top when none exists yet)
void Canvas::addBoneLayerAction() {
    const auto layer = enve::make_shared<BoneLayer>();
    mCurrentContainer ? mCurrentContainer->addContained(layer) :
                        addContained(layer);
    if(Document::sInstance) Document::sInstance->actionFinished();
}

void Canvas::addAdjustmentLayerAction() {
    const auto adj = enve::make_shared<AdjustmentLayer>();
    mCurrentContainer ? mCurrentContainer->addContained(adj) :
                        addContained(adj);
    adj->planUpdate(UpdateReason::userChange);
    if(Document::sInstance) Document::sInstance->actionFinished();
}

// Moho-style switch group: an empty group flagged as switch layer, the
// user then drops the alternative layers into it
void Canvas::addSwitchGroupAction() {
    const auto group = enve::make_shared<ContainerBox>(
                QObject::tr("切换组"), eBoxType::group);
    mCurrentContainer ? mCurrentContainer->addContained(group) :
                        addContained(group);
    group->enableSwitchLayer();
    group->planUpdate(UpdateReason::userChange);
    clearBoxesSelection();
    addBoxToSelection(group.get());
    if(Document::sInstance) Document::sInstance->actionFinished();
}

// AE solid layer: flat-color plane the size of the canvas
void Canvas::addSolidLayerAction() {
    const auto solid = enve::make_shared<SolidLayer>();
    solid->setTopLeftPos(QPointF(0, 0));
    solid->setBottomRightPos(QPointF(getCanvasWidth(), getCanvasHeight()));
    mCurrentContainer ? mCurrentContainer->addContained(solid) :
                        addContained(solid);
    solid->planUpdate(UpdateReason::userChange);
    if(Document::sInstance) Document::sInstance->actionFinished();
}

// ---- scene camera (AE-like, driven by a CameraLayer box) ----

// freeze pose across the whole rig: key every channel of every bone
// at the current frame - a pose is only pinned when no bone anywhere
// keeps interpolating through this frame (staggered keys = drift)
void Canvas::freezeAllBones() {
    const QList<Bone*> bones = mBones;
    for(const auto bone : bones) {
        if(bone) bone->freezeChannels();
    }
}

CameraLayer* Canvas::getCameraLayer() const {
    for(const auto& c : getContained()) {
        if(const auto cam = enve_cast<CameraLayer*>(c.data())) {
            return cam;
        }
    }
    return nullptr;
}

void Canvas::addCameraLayerAction() {
    if(getCameraLayer()) return;
    const auto cam = enve::make_shared<CameraLayer>();
    addContained(cam);
    if(Document::sInstance) Document::sInstance->actionFinished();
}

SkMatrix Canvas::getCameraTransformAtFrame(const qreal relFrame) const {
    const auto cam = getCameraLayer();
    if(!cam) return SkMatrix();
    return cam->getCameraTransformAtFrame(relFrame, mWidth, mHeight);
}

bool Canvas::cameraHasPerspectiveAtFrame(const qreal relFrame) const {
    const auto cam = getCameraLayer();
    if(!cam) return false;
    return cam->hasPerspectiveAtFrame(relFrame);
}

bool Canvas::sceneHasActiveCamera() const {
    if(!getCameraLayer()) return false;
    return !getCameraTransformAtFrame(anim_getCurrentRelFrame()).isIdentity();
}

bool Canvas::selectionNeedsCameraMapping() const {
    if(!sceneHasActiveCamera()) return false;
    for(const auto& box : mSelectedBoxes) {
        const auto ta = box ? box->getBoxTransformAnimator() : nullptr;
        if(ta && ta->is3DEnabled()) return true;
    }
    return false;
}

QPointF Canvas::mapCameraScreenToWorld(const QPointF &pos) const {
    const SkMatrix cam = getCameraTransformAtFrame(anim_getCurrentRelFrame());
    if(cam.isIdentity()) return pos;
    SkMatrix inv;
    if(!cam.invert(&inv)) return pos;
    SkPoint pt = toSkPoint(pos);
    inv.mapPoints(&pt, &pt, 1);
    return toQPointF(pt);
}

// camera values changed: drop the cached scene frames AND every 3D
// layer's render data - the layers themselves believe nothing of
// their own changed and would otherwise keep serving cached data
// carrying the OLD camera matrix (the original "camera tool has no
// effect" bug).
// Coalesced through the event loop: an orbit drag sets rotX and rotY
// every mouse move, and each setter fires this - without coalescing
// the scene is walked and invalidated twice per move.
void Canvas::sceneCameraChanged(const FrameRange& range) {
    mCameraChangePendingRange = mCameraChangePendingRange + range;
    if(mCameraChangeQueued) return;
    mCameraChangeQueued = true;
    QMetaObject::invokeMethod(this, [this]() {
        mCameraChangeQueued = false;
        const auto range = mCameraChangePendingRange;
        mCameraChangePendingRange = FrameRange::INVALID;
        mSceneFramesHandler.remove(range);
        if(!mSceneFramesHandler.atFrame(anim_getCurrentRelFrame())) {
            mSceneFrameOutdated = true;
        }
        // link canvases (InternalLinkCanvas) can introduce cycles into
        // the box hierarchy - guard the recursion with a visited set
        // or a cyclic link means unbounded recursion (stack overflow)
        QSet<ContainerBox*> visited;
        std::function<void(ContainerBox*)> walk =
                [&](ContainerBox* const cont) {
            if(!cont || visited.contains(cont)) return;
            visited.insert(cont);
            for(const auto& c : cont->getContained()) {
                const auto box = enve_cast<BoundingBox*>(c.data());
                if(const auto group = enve_cast<ContainerBox*>(c.data())) {
                    walk(group);
                }
                if(box && box->getBoxTransformAnimator() &&
                   box->getBoxTransformAnimator()->is3DEnabled()) {
                    box->planUpdate(UpdateReason::userChange);
                }
            }
        };
        walk(const_cast<Canvas*>(this));
        planUpdate(UpdateReason::userChange);
    }, Qt::QueuedConnection);
}

// depth-first search for the first bone layer anywhere in the scene
// hierarchy (bone layers may be nested inside groups - e.g. a PSD
// import root converted into one); the visited set guards against
// cycles introduced by link canvases (stack overflow otherwise)
static BoneLayer* findBoneLayerDeep(ContainerBox* const cont,
                                    QSet<ContainerBox*>& visited) {
    if(!cont || visited.contains(cont)) return nullptr;
    visited.insert(cont);
    for(const auto& c : cont->getContained()) {
        if(const auto bl = enve_cast<BoneLayer*>(c.data())) return bl;
        if(const auto group = enve_cast<ContainerBox*>(c.data())) {
            if(const auto bl = findBoneLayerDeep(group, visited)) return bl;
        }
    }
    return nullptr;
}

static BoneLayer* findBoneLayerDeep(ContainerBox* const cont) {
    QSet<ContainerBox*> visited;
    return findBoneLayerDeep(cont, visited);
}

Bone* Canvas::startBoneChain(const QPointF& absPos) {
    ContainerBox* parent = enve_cast<BoneLayer*>(mCurrentContainer.data());
    if(!parent && mCurrentContainer) {
        // walk up: a bone layer nested above the current container is
        // the natural home for new bones (converted PSD group etc.)
        for(auto p = mCurrentContainer->getParentGroup(); p;
            p = p->getParentGroup()) {
            if(const auto bl = enve_cast<BoneLayer*>(p)) { parent = bl; break; }
        }
    }
    if(!parent) parent = findBoneLayerDeep(this);
    if(!parent) {
        const auto layer = enve::make_shared<BoneLayer>();
        addContained(layer);
        parent = layer.get();
    }
    const auto bone = enve::make_shared<Bone>();
    parent->addContained(bone);
    bone->getBoxTransformAnimator()->setPivot(0, 0);
    const QPointF rel = parent->mapAbsPosToRel(absPos);
    bone->getBoxTransformAnimator()->setPosition(rel.x(), rel.y());
    mDraftBone = bone.get();
    return mDraftBone;
}

void Canvas::clearGradientRWIds() const
{
    SimpleTask::sScheduleContexted(this, [this]() {
        for (const auto &grad : mGradients) {
            grad->clearReadWriteId();
        }
    });
}
