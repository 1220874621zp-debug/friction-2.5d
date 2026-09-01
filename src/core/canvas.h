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

#ifndef CANVAS_H
#define CANVAS_H

#include "Boxes/containerbox.h"
#include "colorhelpers.h"
#include <QThread>
#include <functional>
#include "CacheHandlers/hddcachablecachehandler.h"
#include "skia/skiaincludes.h"
#include "GUI/valueinput.h"
#include "Animators/coloranimator.h"
#include "MovablePoints/segment.h"
#include "MovablePoints/movablepoint.h"
#include "Boxes/canvasrenderdata.h"
//#include "Paint/drawableautotiledsurface.h"
#include "canvasbase.h"
//#include "Paint/animatedsurface.h"
#include <QAction>
#include "Animators/outlinesettingsanimator.h"
//#include "Paint/painttarget.h"
#include "CacheHandlers/usepointer.h"
#include "CacheHandlers/sceneframecontainer.h"
#include "undoredo.h"
#include "drawpath.h"
#include <QMouseEvent>
#include <QTabletEvent>
#include <QSizeF>
#include <QVector>
#include <QTransform>
#include <vector>

#include "gizmos.h"

class AnimatedSurface;
//class PaintBox;
class TextBox;
class Circle;
class RectangleBox;
class PathPivot;
class SoundComposition;
class SkCanvas;
class ImageSequenceBox;
class Brush;
class UndoRedoStack;
class ExternalLinkBox;
struct ShaderEffectCreator;
class VideoBox;
class ImageBox;
class Document;
class NullObject;
class Bone;

class eMouseEvent;
class eKeyEvent;

enum class CtrlsMode : short;

enum class AlignPivot {
    geometry, pivot, pivotItself
};

enum class AlignRelativeTo {
    scene, lastSelected, lastSelectedPivot, boundingBox
};

class CORE_EXPORT Canvas : public CanvasBase
{
    friend class CanvasWindow;
    typedef qCubicSegment1DAnimator::Action SegAction;
    Q_OBJECT
    e_OBJECT
    e_DECLARE_TYPE(Canvas)

protected:
    explicit Canvas(Document& document,
                    const int canvasWidth = 1920,
                    const int canvasHeight = 1080,
                    const int frameCount = 200,
                    const qreal fps = 24);
public:
    ~Canvas();

    void prp_afterChangedAbsRange(const FrameRange &range,
                                  const bool clip = true);

    void saveSceneSVG(SvgExporter& exp) const;

    void selectOnlyLastPressedBox();
    void selectOnlyLastPressedPoint();

    void repaintIfNeeded();
    void setCanvasMode(const CanvasMode mode);
    void startSelectionAtPoint(const QPointF &pos);
    void moveSecondSelectionPoint(const QPointF &pos);
    void setPointCtrlsMode(const CtrlsMode mode);
    void setCurrentBoxesGroup(ContainerBox * const group);

    void updatePivot();

    void updatePivotIfNeeded();

    //void updateAfterFrameChanged(const int currentFrame);

    QSize getCanvasSize();

    //
    void finishSelectedPointsTransform();
    void finishSelectedBoxesTransform();
    void setSelectedBoxesInPoint();
    void setSelectedBoxesOutPoint();
    void trimSelectedSounds(const bool inPoint, const int absFrame);
    void moveSelectedPointsByAbs(const QPointF &by,
                                 const bool startTransform);
    void moveSelectedBoxesByAbs(const QPointF &by,
                                const bool startTransform);
    void moveSelectedBoxes3DZ(const qreal by,
                              const bool startTransform);
    void rotateSelectedBoxes3DX(const qreal by,
                                const bool startTransform);
    void rotateSelectedBoxes3DY(const qreal by,
                                const bool startTransform);

    // node-link parenting: reuse or create a ParentEffect on the child
    // and bind it to the given parent (nullptr clears); returns success
    bool linkParentLevel(BoundingBox* const child,
                         BoundingBox* const parent);

    // drop one or more sibling rows onto a plain layer/sound row: move
    // the dragged rows next to the anchor and put them all on the same
    // timeline track (UI-level grouping, see eBoxOrSound::setTrackId);
    // kinds must match - audio rows only join audio tracks, visual rows
    // only visual tracks; returns false when combining is not allowed
    bool combineIntoTrack(eBoxOrSound* const anchor,
                          const QList<eBoxOrSound*>& layers);
    ContainerBox* groupSelectedBoxes();

    //void selectAllBoxes();
    void deselectAllBoxes();

    void applyShadowToSelected();

    void selectedPathsUnion();
    void selectedPathsDifference();
    void selectedPathsIntersection();
    void selectedPathsDivision();
    void selectedPathsExclusion();

    void centerPivotForSelected();
    void resetSelectedScale();
    void resetSelectedTranslation();
    void resetSelectedRotation();
    void convertSelectedBoxesToPath();
    void convertSelectedPathStrokesToPath();

    void rotateSelectedBy(const qreal rotBy,
                          const QPointF &absOrigin,
                          const bool startTrans);

    QPointF getSelectedBoxesAbsPivotPos();
    int getSelectedBoxesCount();
    bool isBoxSelectionEmpty() const;
    // authoritative selection list (kept in sync with box isSelected)
    QList<BoundingBox*> getSelectedBoxesList() const
    { return mSelectedBoxes.getList(); }
    // selected property rows (e.g. Position clicked in the timeline)
    QList<Property*> getSelectedPropsList() const
    { return mSelectedProps.getList(); }

    void ungroupSelectedBoxes();
    void scaleSelectedBy(const qreal scaleBy,
                         const QPointF &absOrigin,
                         const bool startTrans);
    void cancelSelectedBoxesTransform();
    void shearSelectedBy(const qreal shearXBy,
                         const qreal shearYBy,
                         const QPointF &absOrigin,
                         const bool startTrans);

    void cancelSelectedPointsTransform();

    void setSelectedCapStyle(const SkPaint::Cap capStyle);
    void setSelectedJoinStyle(const SkPaint::Join joinStyle);
    void setSelectedStrokeBrush(SimpleBrushWrapper * const brush);

    void applyStrokeBrushWidthActionToSelected(const SegAction &action);
    void applyStrokeBrushPressureActionToSelected(const SegAction &action);
    void applyStrokeBrushSpacingActionToSelected(const SegAction &action);
    void applyStrokeBrushTimeActionToSelected(const SegAction &action);

    void strokeWidthAction(const QrealAction &action);

    void startSelectedStrokeColorTransform();
    void startSelectedFillColorTransform();

    void scaleSelectedBy(const qreal scaleXBy,
                         const qreal scaleYBy,
                         const QPointF &absOrigin,
                         const bool startTrans);

    qreal getResolution() const;
    void setSafeFramesVisible(const bool visible);
    bool safeFramesVisible() const { return mShowSafeFrames; }
    // PS-style ruler guides (canvas coordinates; y for horizontal
    // lines, x for vertical ones)
    const QList<qreal>& hGuides() const { return mHGuides; }
    const QList<qreal>& vGuides() const { return mVGuides; }
    int addHGuide(const qreal y) { mHGuides << y; return mHGuides.count()-1; }
    int addVGuide(const qreal x) { mVGuides << x; return mVGuides.count()-1; }
    void setHGuide(const int id, const qreal y)
    { if (id >= 0 && id < mHGuides.count()) mHGuides[id] = y; }
    void setVGuide(const int id, const qreal x)
    { if (id >= 0 && id < mVGuides.count()) mVGuides[id] = x; }
    void removeHGuide(const int id)
    { if (id >= 0 && id < mHGuides.count()) mHGuides.removeAt(id); }
    void removeVGuide(const int id)
    { if (id >= 0 && id < mVGuides.count()) mVGuides.removeAt(id); }
    void setTransparencyGrid(const bool grid);
    bool transparencyGrid() const { return mTransparencyGrid; }
    void setWorldToScreen(const QTransform& transform,
                          qreal devicePixelRatio);
    void setResolution(const qreal percent);
    void invalidateSceneFramesCache();

    void applyCurrentTransformToSelected();
    QPointF getSelectedPointsAbsPivotPos();
    bool isPointSelectionEmpty() const;
    void scaleSelectedPointsBy(const qreal scaleXBy,
                               const qreal scaleYBy,
                               const QPointF &absOrigin,
                               const bool startTrans);
    void shearSelectedPointsBy(const qreal shearXBy,
                               const qreal shearYBy,
                               const QPointF &absOrigin,
                               const bool startTrans);

    void rotateSelectedPointsBy(const qreal rotBy,
                                const QPointF &absOrigin,
                                const bool startTrans);
    int getPointsSelectionCount() const;

    void clearPointsSelectionOrDeselect();
    NormalSegment getSegment(const eMouseEvent &e) const;

    void createLinkBoxForSelected();
    void startSelectedPointsTransform();

    void mergePoints();
    void splitPoints();
    void disconnectPoints();
    bool connectPoints();
    void subdivideSegments();
    void makeSelectedNodeFirst();
    void reverseSelectedNodesOrder();

    void setSelectedTextAlignment(const Qt::Alignment alignment) const;
    void setSelectedTextVAlignment(const Qt::Alignment alignment) const;
    void setSelectedFontFamilyAndStyle(const QString &family,
                                       const SkFontStyle &style);
    void setSelectedFontSize(const qreal size);
    void setSelectedFontText(const QString &text);
    void removeSelectedPointsAndClearList();
    void removeSelectedPointsApprox();
    void removeSelectedBoxesAndClearList();

    BoundingBox* getCurrentBox() const { return mCurrentBox; }
    void setCurrentBox(BoundingBox* const box);
    void addBoxToSelection(BoundingBox* const box);
    void removeBoxFromSelection(BoundingBox* const box);
    void clearBoxesSelection();
    void clearBoxesSelectionList();

    // timeline tracks (UI-level grouping of sibling boxes sharing
    // one timeline row, see eBoxOrSound::setTrackId):
    // re-resolve which member of the track is the active (visible) one
    void enforceTrack(ContainerBox* const parent, const int trackId);
    // allocate a track id unused among the parent's direct children
    int newTrackId(ContainerBox* const parent) const;
    // stack the other members of the anchor's track directly below
    // it, keeping the whole track contiguous after a reorder drop
    void gatherTrack(eBoxOrSound* const anchor);
    const QString checkForUnsupportedBoxSVG(BoundingBox* const box);
    const QString checkForUnsupportedBoxesSVG(const QList<BoundingBox*> boxes);
    const QString checkForUnsupportedSVG();

    void addPointToSelection(MovablePoint * const point);
    void removePointFromSelection(MovablePoint * const point);

    void clearPointsSelection();
    void raiseSelectedBoxesToTop();
    void lowerSelectedBoxesToBottom();
    void raiseSelectedBoxes();
    void lowerSelectedBoxes();

    void alignSelectedBoxes(const Qt::Alignment align,
                            const AlignPivot pivot,
                            const AlignRelativeTo relativeTo);

    // uniformly scale each selected box so its world-space width/height
    // matches the canvas, then center it on the canvas (one undo step)
    void scaleSelectedBoxesToCanvas(const bool byWidth);

    void selectAndAddContainedPointsToSelection(const QRectF &absRect);
//
    //void newPaintBox(const QPointF &pos);

    void mousePressEvent(const eMouseEvent &e);
    void mouseReleaseEvent(const eMouseEvent &e);
    void mouseMoveEvent(const eMouseEvent &e);
    void mouseDoubleClickEvent(const eMouseEvent &e);

    struct TabletEvent
    {
        TabletEvent(const QPointF &pos,
                    QTabletEvent* const e)
            : fPos(pos)
            , fType(e->type())
            , fButton(e->button())
            , fButtons(e->buttons())
            , fModifiers(e->modifiers())
            , fTimestamp(e->timestamp())
        {

        }

        QPointF fPos;
        QEvent::Type fType;
        Qt::MouseButton fButton;
        Qt::MouseButtons fButtons;
        Qt::KeyboardModifiers fModifiers;
        ulong fTimestamp;
        qreal fPressure;
        int fXTilt;
        int fYTilt;
    };

    void tabletEvent(const QTabletEvent* const e,
                     const QPointF &pos);

    bool keyPressEvent(QKeyEvent *event);

    qsptr<BoundingBox> createLink(const bool inner);

    void setPreviewing(const bool bT);
    void setOutputRendering(const bool bT);

    bool SWT_shouldBeVisible(const SWT_RulesCollection &rules,
                             const bool parentSatisfies,
                             const bool parentMainTarget) const;

    ContainerBox *getCurrentGroup()
    {
        return mCurrentContainer;
    }

    void updateTotalTransform() {}

    QMatrix getTotalTransform() const
    {
        return QMatrix();
    }

    QMatrix getRelativeTransformAtCurrentFrame() const
    {
        return QMatrix();
    }

    QPointF mapAbsPosToRel(const QPointF &absPos)
    {
        return absPos;
    }

    void scheduleEffectsMarginUpdate()
    {

    }

    void renderSk(SkCanvas* const canvas,
                  const QRect &drawRect,
                  const QMatrix &viewTrans,
                  const bool mouseGrabbing);
    void renderGizmos(SkCanvas* const canvas,
                      const qreal qInvZoom,
                      const float invZoom);

    void setCanvasSize(const int width,
                       const int height);

    int getCanvasWidth() const
    {
        return mWidth;
    }

    QRect getCanvasBounds() const
    {
        return QRect(0, 0, mWidth, mHeight);
    }

    QRect getMaxBounds() const
    {
        return QRect(-mWidth/2, - mHeight/2, 2*mWidth, 2*mHeight);
    }

    QRect getCurrentBounds() const
    {
        //if(mClipToCanvasSize) return getCanvasBounds();
        //else return getMaxBounds();
        return getMaxBounds();
    }

    int getCanvasHeight() const
    {
        return mHeight;
    }

    void setFrameRange(const FrameRange& range,
                       const bool undo = true);

    void setFrameIn(const bool enabled,
                    const int frameIn);
    void setFrameOut(const bool enabled,
                     const int frameOut);
    const FrameMarker getFrameIn() const;
    const FrameMarker getFrameOut() const;
    void clearFrameInOut();
    void restoreFrameInOut(const FrameMarker &frameIn,
                           const FrameMarker &frameOut);

    void setMarker(const QString &title,
                   const int frame);
    void setMarker(const int frame);
    void setMarkerEnabled(const int frame, const bool &enabled);
    bool hasMarker(const int frame,
                   const bool removeExists = false);
    bool hasMarkerIn(const int frame);
    bool hasMarkerOut(const int frame);
    bool hasMarkerEnabled(const int frame);
    bool removeMarker(const int frame);
    bool editMarker(const int frame,
                    const QString &title,
                    const bool enabled);
    void moveMarkerFrame(const int markerFrame,
                         const int newFrame);
    const QString getMarkerText(int frame);
    int getMarkerIndex(const int frame);
    const std::vector<FrameMarker> getMarkers();
    void clearMarkers();
    void updateMarkers();
    void restoreMarkers(const std::vector<FrameMarker> &markers);

    void addKeySelectedProperties();

    ColorAnimator *getBgColorAnimator()
    {
        return mBackgroundColor.get();
    }

    stdsptr<BoxRenderData> createRenderData();

    void setupRenderData(const qreal relFrame,
                         const QMatrix &parentM,
                         BoxRenderData* const data,
                         Canvas* const scene)
    {
        ContainerBox::setupRenderData(relFrame, parentM, data, scene);
        auto canvasData = static_cast<CanvasRenderData*>(data);
        canvasData->fBgColor = toSkColor(mBackgroundColor->getColor());
        canvasData->fCanvasHeight = mHeight;
        canvasData->fCanvasWidth = mWidth;
    }

    bool clipToCanvas()
    {
        return mClipToCanvasSize;
    }

    void schedulePivotUpdate();
    void setClipToCanvas(const bool bT)
    {
        mClipToCanvasSize = bT;
        // the clip only affects the transient canvas paint - without a
        // repaint request the change sits dormant until something else
        // (e.g. playback) refreshes the viewport
        emit requestUpdate();
    }
    void setRasterEffectsVisible(const bool bT)
    {
        mRasterEffectsVisible = bT;
    }
    // scene-wide motion blur master (View menu); per-layer switches in
    // the timeline gate on top of this
    void setMotionBlurEnabled(const bool bT)
    {
        mMotionBlurEnabled = bT;
    }
    bool getMotionBlurEnabled() const
    {
        return mMotionBlurEnabled;
    }
    void setPathEffectsVisible(const bool bT)
    {
        mPathEffectsVisible = bT;
    }

    void setGizmoVisibility(const Friction::Core::Gizmos::Interact &ti,
                            const bool visibility);
    bool getGizmoVisibility(const Friction::Core::Gizmos::Interact &ti);

    void setEasingAction(const QString &easing)
    {
        emit requestEasingAction(easing);
    }

protected:
    void setCurrentSmartEndPoint(SmartNodePoint* const point);

    void handleMovePathMouseRelease(const eMouseEvent &e);
    void handleMovePointMouseRelease(const eMouseEvent &e);

    void handleRightButtonMouseRelease(const eMouseEvent &e);
    void handleLeftButtonMousePress(const eMouseEvent &e);

signals:
    void requestUpdate();
    void newFrameRange(FrameRange);
    void currentBoxChanged(BoundingBox*);
    void selectedPaintSettingsChanged();
    void objectSelectionChanged();
    void pointSelectionChanged();
    void currentFrameChanged(int);
    void currentContainerSet(ContainerBox*);
    void dimensionsChanged(int, int);
    void fpsChanged(qreal);
    void displayTimeCodeChanged(bool);
    void gradientCreated(SceneBoundGradient*);
    void gradientRemoved(SceneBoundGradient*);
    void openTextEditor();
    void requestEasingAction(const QString &easing);
    void openMarkerEditor();
    void openExpressionDialog(QrealAnimator* const target);
    void openApplyExpressionDialog(QrealAnimator* const target);
    void currentPickedColor(const QColor &color);
    void currentHoverColor(const QColor &color);
    void markersChanged();
    void canvasModeSet(const CanvasMode &mode);

public:
    void makePointCtrlsSymmetric();
    void makePointCtrlsSmooth();
    void makePointCtrlsCorner();

    void makeSegmentLine();
    void makeSegmentCurve();

    void newEmptyPaintFrameAction();

    MovablePoint *getPointAtAbsPos(const QPointF &absPos,
                                   const CanvasMode mode,
                                   const qreal invScale);
    void clearLastPressedPoint();
    void clearCurrentSmartEndPoint();
    void applyPaintSettingToSelected(const PaintSettingsApplier &setting);

    int getCurrentFrame() const;
    FrameRange getFrameRange() const
    {
        return mRange;
    }

    SoundComposition *getSoundComposition();

    void updateHoveredBox(const eMouseEvent &e);
    void updateHoveredPoint(const eMouseEvent &e);
    void updateHoveredEdge(const eMouseEvent &e);
    void updateHovered(const eMouseEvent &e);
    void clearHoveredEdge();
    void clearHovered();

    bool getPivotLocal() const;

    int getMinFrame() const
    {
        return mRange.fMin;
    }
    int getMaxFrame() const
    {
        return mRange.fMax;
    }

    //void updatePixmaps();
    HddCachableCacheHandler &getSceneFramesHandler()
    {
        return mSceneFramesHandler;
    }

    HddCachableCacheHandler &getSoundCacheHandler();

    void setSceneFrame(const int relFrame);
    void setSceneFrame(const stdsptr<SceneFrameContainer> &cont);
    void setLoadingSceneFrame(const stdsptr<SceneFrameContainer> &cont);
    // Schedule tmp-file reloads for cached frames in [minRelFrame, maxRelFrame]
    // whose pixels are currently swapped out of memory. Called when preview
    // playback starts so frames are back in memory by the time the playhead
    // reaches them.
    void scheduleLoadMissingSceneFrames(const int minRelFrame, const int maxRelFrame);

    void setRenderingPreview(const bool bT);

    bool isPreviewingOrRendering() const
    {
        return mPreviewing || mRenderingPreview || mRenderingOutput;
    }

    qreal getFps() const
    {
        return mFps;
    }
    void setFps(const qreal fps)
    {
        mFps = fps;
        emit fpsChanged(fps);
    }

    bool getDisplayTimecode()
    {
        return mDisplayTimeCode;
    }
    void setDisplayTimecode(bool timecode)
    {
        mDisplayTimeCode = timecode;
        emit displayTimeCodeChanged(timecode);
    }

    BoundingBox *getBoxAt(const QPointF &absPos)
    {
        if (mClipToCanvasSize) {
            const auto bRect = Canvas::getCurrentBounds();
            if (!QRectF(bRect).contains(absPos)) { return nullptr; }
        }
        return ContainerBox::getBoxAt(absPos);
    }

    void anim_scaleTime(const int pivotAbsFrame,
                        const qreal scale);

    void changeFpsTo(const qreal fps)
    {
        anim_scaleTime(0, fps/mFps);
        setFps(fps);
    }

    void addActionsToMenu(QMenu* const menu);

    void deleteAction();
    void copyAction();
    void pasteAction();
    void cutAction();
    void splitAction();
    void duplicateAction();
    void selectAllAction();
    void clearSelectionAction();
    void finishedAction();
    void rotateSelectedBoxesStartAndFinish(const qreal rotBy,
                                           bool inc = true);
    void scaleSelectedBoxesStartAndFinish(const qreal scaleBy);
    void moveSelectedBoxesStartAndFinish(const QPointF moveBy);

    bool shouldScheduleUpdate()
    {
        return mSceneFrameOutdated;
    }

    void renderDataFinished(BoxRenderData *renderData);
    FrameRange prp_getIdenticalRelRange(const int relFrame) const;

    void writeSettings(eWriteStream &dst) const;
    void readSettings(eReadStream &src);
    void writeBoundingBox(eWriteStream& dst) const;
    void readBoundingBox(eReadStream& src);
    void writeMarkers(eWriteStream &dst) const;
    void readMarkers(eReadStream &src);

    void writeBoxOrSoundXEV(const stdsptr<XevZipFileSaver> &xevFileSaver,
                            const RuntimeIdToWriteId &objListIdConv,
                            const QString &path) const;
    void readBoxOrSoundXEV(XevReadBoxesHandler &boxReadHandler,
                           ZipFileLoader &fileLoader,
                           const QString &path,
                           const RuntimeIdToWriteId &objListIdConv);

    bool anim_prevRelFrameWithKey(const int relFrame,
                                  int &prevRelFrame);
    bool anim_nextRelFrameWithKey(const int relFrame,
                                  int &nextRelFrame);

    void shiftAllPointsForAllKeys(const int by);
    void revertAllPointsForAllKeys();
    void shiftAllPoints(const int by);
    void revertAllPoints();
    void flipSelectedBoxesHorizontally();
    void flipSelectedBoxesVertically();
    int getByteCountPerFrame();
    int getMaxPreviewFrame(const int minFrame,
                           const int maxFrame);
    void selectedPathsCombine();
    void selectedPathsBreakApart();
    void invertSelectionAction();

    bool getRasterEffectsVisible() const
    {
        return mRasterEffectsVisible;
    }

    bool getPathEffectsVisible() const
    {
        return mPathEffectsVisible;
    }

    void anim_setAbsFrame(const int frame);

    // run func on every selected sound (sounds live outside the canvas
    // box selection but join the "all selected" timeline operations)
    void forEachSelectedSound(const std::function<void(eBoxOrSound*)>& func);

    void moveDurationRectForAllSelected(const int dFrame);
    void startDurationRectPosTransformForAllSelected();
    void finishDurationRectPosTransformForAllSelected();
    void cancelDurationRectPosTransformForAllSelected();

    void startMinFramePosTransformForAllSelected();
    void finishMinFramePosTransformForAllSelected();
    void cancelMinFramePosTransformForAllSelected();
    void moveMinFrameForAllSelected(const int dFrame);

    void startMaxFramePosTransformForAllSelected();
    void finishMaxFramePosTransformForAllSelected();
    void cancelMaxFramePosTransformForAllSelected();
    void moveMaxFrameForAllSelected(const int dFrame);

    bool newUndoRedoSet();

    void undo();
    void redo();

    UndoRedoStack::StackBlock blockUndoRedo();
    void unblockUndoRedo();

    void setParentToLastSelected();
    void clearParentForSelected();

    bool startRotatingAction(const eKeyEvent &e);
    bool startScalingAction(const eKeyEvent &e);
    bool startMovingAction(const eKeyEvent &e);

    void deselectAllBoxesAction();
    void selectAllBoxesAction();
    void selectAllPointsAction();
    //bool handlePaintModeKeyPress(const eKeyEvent &e);
    bool handleModifierChange(const eKeyEvent &e);
    bool handleTransormationInputKeyEvent(const eKeyEvent &e);

    void setCurrentGroupParentAsCurrentGroup();

    /*bool hasValidPaintTarget() const
    {
        return mPaintTarget.isValid();
    }*/

    void queTasks();

    void setMinFrameUseRange(const int min)
    {
        mSceneFramesHandler.setMinUseRange(min);
    }

    void setMaxFrameUseRange(const int max)
    {
        mSceneFramesHandler.setMaxUseRange(max);
    }

    void clearUseRange()
    {
        mSceneFramesHandler.clearUseRange();
    }

    void setGizmosSuppressed(bool suppressed);

    //! Used for clip to canvas, when frames are not really changed.
    void sceneFramesUpToDate() const
    {
        for (const auto &cont : mSceneFramesHandler) {
            const auto sceneCont = static_cast<SceneFrameContainer*>(cont.second.get());
            sceneCont->fBoxState = mStateId;
        }
    }

    void addSelectedForGraph(const int widgetId,
                             GraphAnimator* const anim);
    bool removeSelectedForGraph(const int widgetId,
                                GraphAnimator* const anim);
    const ConnContextObjList<GraphAnimator*>* getSelectedForGraph(const int widgetId) const;
    void addUndoRedo(const QString &name,
                     const stdfunc<void ()> &undo,
                     const stdfunc<void ()> &redo);
    void pushUndoRedoName(const QString &name) const;

    UndoRedoStack* undoRedoStack() const
    {
        return mUndoRedoStack.get();
    }

    const QList<qsptr<SceneBoundGradient>> &gradients() const
    {
        return mGradients;
    }
    SceneBoundGradient * createNewGradient();
    bool removeGradient(const qsptr<SceneBoundGradient> &gradient);

    SceneBoundGradient * getGradientWithRWId(const int rwId) const;
    SceneBoundGradient * getGradientWithDocumentId(const int id) const;
    SceneBoundGradient * getGradientWithDocumentSceneId(const int id) const;

    void addNullObject(NullObject* const obj);
    void removeNullObject(NullObject* const obj);

    // FK bones: editing-time visuals + the bone-in-progress chain
    void addBone(Bone* const bone);

    // read access for diagnostics / tools
    const QList<Bone*>& getBones() const { return mBones; }
    // freeze-pose helper: key every channel of every bone in the scene
    // at the current frame (the auto-freeze toolbar toggle uses this)
    void freezeAllBones();
    void removeBone(Bone* const bone);
    // bone currently being placed by the bone tool (length/rotation
    // follow the cursor until the next click grows a child bone)
    Bone* draftBone() const { return mDraftBone; }
    void setDraftBone(Bone* const bone) { mDraftBone = bone; }
    // target container for new bones: the current BoneLayer if any
    Bone* startBoneChain(const QPointF& absPos);
    // UI helpers: create rig/special layers into the current container
    void addBoneLayerAction();
    void addAdjustmentLayerAction();
    void addSolidLayerAction();
    // empty group flagged as a switch group, ready to receive layers
    void addSwitchGroupAction();

    // ---- scene camera (AE-like): driven by a CameraLayer box (the
    // camera tool auto-creates one on first use). Affects ONLY layers
    // with their 3D switch enabled (AE rule - plain 2D layers live in
    // screen space) ----
    class CameraLayer* getCameraLayer() const;
    SkMatrix getCameraTransformAtFrame(const qreal relFrame) const;
    bool cameraHasPerspectiveAtFrame(const qreal relFrame) const;
    // invalidate every 3D layer's render data + the scene frame cache
    // (wired to the CameraLayer animators - without this the layers
    // keep serving cached render data with the OLD camera matrix)
    // Coalesced through the event loop: one orbit drag updates rotX AND
    // rotY, which must not walk the whole scene twice per mouse move
    void sceneCameraChanged(const FrameRange& range);
    void addCameraLayerAction();
    // true when a camera layer exists and its transform is not the identity
    bool sceneHasActiveCamera() const;
    // true when the current box selection contains 3D layers shown through
    // an active camera (their move deltas must be un-projected)
    bool selectionNeedsCameraMapping() const;
    // un-project a canvas position through the scene camera so screen-space
    // mouse deltas become world-space deltas (identity camera: unchanged)
    QPointF mapCameraScreenToWorld(const QPointF &pos) const;
    // bone tool interaction helpers (canvasmouseinteractions.cpp)
    void boneCreatePress(const class eMouseEvent& e);
    void updateDraftBone(const QPointF& absPos);
    // bone pose tool (Moho-style): drag a bone body to rotate it around
    // its head, drag the head joint to move the whole chain segment
    void bonePosePress(const class eMouseEvent& e);
    void bonePoseMove(const class eMouseEvent& e);
    void bonePoseRelease();
    // bone bind tool: click a bone to bind the selected layers into it
    void boneBindPress(const class eMouseEvent& e);
    // bone parent-link tool: click a bone to make it the parent of
    // the currently selected bone (world positions preserved)
    void boneParentPress(const class eMouseEvent& e);
    // bone select tool: clicking picks ONLY bones (graphics are
    // transparent to the pick)
    void boneSelectPress(const class eMouseEvent& e);
    // scene camera tool (Blender-flavoured): LMB drag orbits (tilt),
    // Shift+LMB pans, Ctrl+LMB drags zoom
    void cameraPress(const class eMouseEvent& e);
    void cameraMove(const class eMouseEvent& e);
    void cameraRelease();
    void cameraCancel();
    Bone* pickBoneAt(const QPointF& absPos, const qreal maxDist);
    void bonePoseCancel();

private:
    // set first thing in the destructor: while true the track/selection
    // bookkeeping (enforceTrack, forEachSelectedSound) stays inert so
    // the teardown never performs active cross-object updates
    bool mDestructing = false;

    void addGradient(const qsptr<SceneBoundGradient> &grad);

    void readGradients(eReadStream &src);
    void writeGradients(eWriteStream &dst) const;

    void clearGradientRWIds() const;
    QList<SmartNodePoint*> getSortedSelectedNodes();
    //void openTextEditorForTextBox(TextBox *textBox);

    void scaleSelected(const eMouseEvent &e);
    void shearSelected(const eMouseEvent &e);
    void rotateSelected(const eMouseEvent &e);
    void rotate3DXSelected(const eMouseEvent &e);
    void rotate3DYSelected(const eMouseEvent &e);

    bool prepareRotation(const QPointF &startPos,
                         bool fromHandle = false);

    void updateRotateHandleHover(const QPointF &pos,
                                 qreal invScale);
    bool pointOnRotateGizmo(const QPointF &pos,
                            qreal invScale) const;
    void setRotateHandleHover(bool hovered);
    bool pointOnRot3DXGizmo(const QPointF &pos,
                            qreal invScale) const;
    bool pointOnRot3DYGizmo(const QPointF &pos,
                            qreal invScale) const;
    void setRot3DXGizmoHover(const bool hovered);
    void setRot3DYGizmoHover(const bool hovered);
    bool shouldShowXLineGizmo() const;
    bool shouldShowYLineGizmo() const;
    bool shouldShowZLineGizmo() const;
    bool updateLineGizmoVisibility();

    void updateRotateHandleGeometry(qreal invScale);

    bool tryStartRotateWithGizmo(const eMouseEvent &e,
                                 qreal invScale);
    bool startRot3DConstrainedMove(const eMouseEvent &e,
                                   Friction::Core::Gizmos::Rot3DHandle handle);
    bool tryStartScaleGizmo(const eMouseEvent &e,
                            qreal invScale);
    bool tryStartShearGizmo(const eMouseEvent &e,
                            qreal invScale);
    bool tryStartAxisGizmo(const eMouseEvent &e,
                           qreal invScale);

    bool startScaleConstrainedMove(const eMouseEvent &e,
                                   Friction::Core::Gizmos::ScaleHandle handle);
    bool startShearConstrainedMove(const eMouseEvent &e,
                                   Friction::Core::Gizmos::ShearHandle handle);
    bool startAxisConstrainedMove(const eMouseEvent &e,
                                  Friction::Core::Gizmos::AxisConstraint axis);

    bool pointOnScaleGizmo(Friction::Core::Gizmos::ScaleHandle handle,
                           const QPointF &pos, qreal invScale) const;
    bool pointOnShearGizmo(Friction::Core::Gizmos::ShearHandle handle,
                           const QPointF &pos, qreal invScale) const;
    bool pointOnAxisGizmo(Friction::Core::Gizmos::AxisConstraint axis,
                          const QPointF &pos, qreal invScale) const;

    void setScaleGizmoHover(Friction::Core::Gizmos::ScaleHandle handle,
                            bool hovered);
    void setShearGizmoHover(Friction::Core::Gizmos::ShearHandle handle,
                            bool hovered);
    void setAxisGizmoHover(Friction::Core::Gizmos::AxisConstraint axis,
                           bool hovered);

    QPointF snapPosToGrid(const QPointF& pos,
                          Qt::KeyboardModifiers modifiers,
                          bool forceSnap) const;
    QPointF snapEventPos(const eMouseEvent& e,
                         bool forceSnap) const;
    void collectAnchorOffsets(const Friction::Core::Grid::Settings &settings);
    const QPair<bool, QPointF> moveBySnapTargets(const Qt::KeyboardModifiers &modifiers,
                                                 const QPointF &moveBy,
                                                 const Friction::Core::Grid::Settings &settings,
                                                 const bool &includeSelectedBounds = false,
                                                 const bool &useAnchorOffsets = true,
                                                 const bool &mustHaveSelected = true);

    void drawPathClear();
    void drawPathFinish(const qreal invScale);

    const QColor pickPixelColor(const QPoint &pos);
    void applyPixelColor(const QColor &color,
                         const bool &fill);

    qreal mLastDRot = 0;
    int mRotHalfCycles = 0;
    TransformMode mTransMode = TransformMode::none;

    QList<qsptr<SceneBoundGradient>> mGradients;
    QList<NullObject*> mNullObjects;
    QList<Bone*> mBones;

    Bone* mDraftBone = nullptr;
    // tail of the chain being built: a press WITHIN the pick radius of
    // this bone's tail grows a CHILD bone from here (Spine-style
    // auto-chaining); a distant press, Ctrl+press or right-click ends
    // the chain (a distant press immediately starts a new one)
    Bone* mChainTail = nullptr;

    // bone pose tool drag state
    enum class PoseDragMode { none, rotate, move };
    PoseDragMode mPoseMode = PoseDragMode::none;

    // camera tool drag state
    enum class CamDragMode { none, orbit, pan, zoom };
    CamDragMode mCamDragMode = CamDragMode::none;
    QPointF mCamPressPos;
    qreal mCamStartPanX = 0;
    qreal mCamStartPanY = 0;
    qreal mCamStartZoom = 1;
    qreal mCamStartRotX = 0;
    qreal mCamStartRotY = 0;
    Bone* mPoseBone = nullptr;
    bool mPoseMoved = false;   // any value written this drag
    qreal mPoseStartAngle = 0;   // world angle of the cursor at press
    qreal mPoseStartRot = 0;     // bone rotation value at press
    // incremental rotation accumulation: each move wraps its delta to
    // (-180, 180] so dragging across the atan2 +/-pi boundary never
    // jumps the stored value by ~360 (which made keys interpolate the
    // long way around - the wrong rotation direction)
    qreal mPoseLastAngle = 0;    // cursor angle at the previous move
    qreal mPoseAccumDeg = 0;     // total wrapped degrees since press
    QPointF mPoseMoveLast;       // last cursor pos while moving

protected:
    Document& mDocument;

    QTransform mWorldToScreen;
    QTransform mScreenToWorld;
    bool mHasWorldToScreen = false;
    qreal mDevicePixelRatio = 1.0;
    QPointF mGridMoveStartPivot;
    std::vector<QPointF> mGridSnapAnchorOffsets;
    bool mHasCreationPressPos = false;
    QPointF mCreationPressPos;

    bool mDrawnSinceQue = true;

    qsptr<UndoRedoStack> mUndoRedoStack;

    //void updatePaintBox();

    //PaintTarget mPaintTarget;
    bool mStylusDrawing = false;

    uint mLastStateId = 0;
    HddCachableCacheHandler mSceneFramesHandler;

    qsptr<ColorAnimator> mBackgroundColor = enve::make_shared<ColorAnimator>();

    SmartVectorPath *getPathResultingFromOperation(const SkPathOp &pathOp);
    SmartVectorPath *getPathResultingFromCombine();

//    void sortSelectedBoxesAsc();
    void sortSelectedBoxesDesc();

    qsptr<SoundComposition> mSoundComposition;

    bool mLocalPivot = false;
    FrameRange mRange{0, 200};

    qreal mResolution = 0.5;

    // view-only toggles (edit canvas, never part of renders/exports):
    // AE-style action/title safe guides + transparency checkerboard
    bool mShowSafeFrames = false;
    // PS-style ruler guides
    QList<qreal> mHGuides;
    QList<qreal> mVGuides;
    bool mTransparencyGrid = false;

    qptr<BoundingBox> mCurrentBox;
    qptr<Circle> mCurrentCircle;
    qptr<RectangleBox> mCurrentRectangle;
    qptr<TextBox> mCurrentTextBox;
    qptr<ContainerBox> mCurrentContainer;

    stdptr<MovablePoint> mHoveredPoint_d;
    qptr<BoundingBox> mHoveredBox;

    qptr<BoundingBox> mPressedBox;
    stdsptr<PathPivot> mRotPivot;

    stdptr<SmartNodePoint> mLastEndPoint;

    stdptr<MovablePoint> mDrawPathFirst;
    ManualDrawPathState mManualDrawPathState = ManualDrawPathState::none;
    int mDrawPathFit = 0;
    SkPath mDrawPathTmp;
    DrawPath mDrawPath;

    NormalSegment mHoveredNormalSegment;
    NormalSegment mCurrentNormalSegment;
    qreal mCurrentNormalSegmentT;

    ValueInput mValueInput;

    Friction::Core::Gizmos mGizmos;

    bool mPreviewing = false;
    bool mRenderingPreview = false;
    bool mRenderingOutput = false;

    bool mSceneFrameOutdated = false;
    // camera-change coalescing (see sceneCameraChanged)
    bool mCameraChangeQueued = false;
    FrameRange mCameraChangePendingRange = FrameRange::INVALID;
    UseSharedPointer<SceneFrameContainer> mSceneFrame;
    UseSharedPointer<SceneFrameContainer> mLoadingSceneFrame;

    bool mClipToCanvasSize = false;
    bool mRasterEffectsVisible = true;
    bool mMotionBlurEnabled = true;
    bool mPathEffectsVisible = true;

    bool mDoubleClick = false;
    int mMovesToSkip = 0;

    int mWidth;
    int mHeight;
    qreal mFps;

    bool mDisplayTimeCode = false;

    bool mPivotUpdateNeeded = false;

    bool mStartTransform = false;
    bool mSelecting = false;
//    bool mMoving = false;

    QRectF mSelectionRect;
    CanvasMode mCurrentMode = CanvasMode::boxTransform;

    std::map<int, stdsptr<ConnContextObjList<GraphAnimator*>>> mSelectedForGraph;

    FrameMarker mIn{tr("In"), false, 0};
    FrameMarker mOut{tr("Out"), false, 0};
    std::vector<FrameMarker> mMarkers;

    void handleMovePointMousePressEvent(const eMouseEvent &e);
    void handleMovePointMouseMove(const eMouseEvent &e);

    void handleMovePathMousePressEvent(const eMouseEvent &e);
    void handleMovePathMouseMove(const eMouseEvent &e);

    void handleLeftMouseRelease(const eMouseEvent &e);
    void handleLeftMouseGizmos();

    void handleAddSmartPointMousePress(const eMouseEvent &e);
    void handleAddSmartPointMouseMove(const eMouseEvent &e);
    void handleAddSmartPointMouseRelease(const eMouseEvent &e);

    void updateTransformation(const eKeyEvent &e);
    QPointF getMoveByValueForEvent(const eMouseEvent &e);
    qreal getMoveZValueForEvent(const eMouseEvent &e);
    void cancelCurrentTransform();
    void cancelCurrentTransformGimzos();

    void collectSnapTargets(bool includePivots,
                            bool includeBounds,
                            bool includeNodes,
                            std::vector<QPointF>& pivotTargets,
                            std::vector<QPointF>& boxTargets,
                            std::vector<QPointF>& nodeTargets,
                            bool includeSelectedBounds = false) const;
};

#endif // CANVAS_H
