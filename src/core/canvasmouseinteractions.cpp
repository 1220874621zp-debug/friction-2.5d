/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "canvas.h"
#include "Boxes/adjustmentlayer.h"
#include "Boxes/bone.h"
#include "Boxes/bonelayer.h"
#include "Boxes/cameralayer.h"

#include "eevent.h"

#include "Private/document.h"
#include "GUI/dialogsinterface.h"

#include "Boxes/boundingbox.h"
#include "Boxes/circle.h"
#include "Boxes/rectangle.h"
#include "Boxes/imagebox.h"
#include "Boxes/textbox.h"
#include "Boxes/internallinkbox.h"
#include "Boxes/containerbox.h"
#include "MovablePoints/smartctrlpoint.h"
#include "MovablePoints/pathpointshandler.h"
#include "Boxes/smartvectorpath.h"
//#include "Boxes/paintbox.h"
#include "Boxes/nullobject.h"

#include "pointtypemenu.h"
#include "pointhelpers.h"
#include "clipboardcontainer.h"

#include "PathEffects/patheffect.h"
#include "PathEffects/patheffectsinclude.h"
#include "RasterEffects/rastereffect.h"

#include "MovablePoints/smartnodepoint.h"
#include "MovablePoints/pathpivot.h"

#include <QDesktopWidget>
#include <QScreen>
#include <QMouseEvent>
#include <QMenu>
#include <QInputDialog>
#include <QApplication>

using namespace Friction::Core;

void Canvas::handleMovePathMousePressEvent(const eMouseEvent& e)
{
    mPressedBox = mCurrentContainer->getBoxAt(e.fPos);
    if(!mPressedBox && sceneHasActiveCamera()) {
        // 3D layers are hit-tested where they are SEEN (through the camera
        // projection), 2D layers keep the raw canvas position first
        mPressedBox = mCurrentContainer->getBoxAt(mapCameraScreenToWorld(e.fPos));
    }
    if (e.shiftMod()) { return; }
    if (mPressedBox ? !mPressedBox->isSelected() : true) {
        clearBoxesSelection();
    }
}

void Canvas::addActionsToMenu(QMenu *const menu)
{
    // new adjustment layer (applies its raster effects to the layers
    // below inside the same parent)
    menu->addAction(QObject::tr("New Adjustment Layer"), [this]() {
        const auto adj = enve::make_shared<AdjustmentLayer>();
        mCurrentContainer->addContained(adj);
        adj->planUpdate(UpdateReason::userChange);
        Document::sInstance->actionFinished();
    });

    // new bone layer (FK rig container; use the bone tool inside it)
    menu->addAction(QObject::tr("New Bone Layer"), [this]() {
        const auto layer = enve::make_shared<BoneLayer>();
        mCurrentContainer->addContained(layer);
        Document::sInstance->actionFinished();
    });

    // new solid layer (AE-style flat-color plane, canvas-sized)
    menu->addAction(QObject::tr("New Solid Layer"), [this]() {
        addSolidLayerAction();
    });

    const auto clipboard = mDocument.getBoxesClipboard();
    if (clipboard) {
        QAction * const pasteAct = menu->addAction(tr("Paste"), this,
                                                   &Canvas::pasteAction);
        pasteAct->setShortcut(Qt::CTRL + Qt::Key_V);
    }

    const auto sceneIcon = QIcon::fromTheme("sequence");
    QMenu * const linkCanvasMenu = menu->addMenu(sceneIcon,
                                                 tr("Link Scene"));
    for (const auto& canvas : mDocument.fScenes) {
        const auto slot = [this, canvas]() {
            auto newLink = canvas->createLink(false);
            mCurrentContainer->addContained(newLink);
            newLink->centerPivotPosition();
        };
        QAction * const action = linkCanvasMenu->addAction(sceneIcon,
                                                           canvas->prp_getName(),
                                                           this,
                                                           slot);
        if (canvas == this) {
            action->setEnabled(false);
            action->setVisible(false);
        }
    }

    menu->addAction(QIcon::fromTheme("duplicate"),
                    tr("Duplicate Scene"), [this]() {
        const auto newScene = Document::sInstance->createNewScene();
        newScene->setCanvasSize(mWidth, mHeight);
        newScene->setFps(mFps);
        newScene->setFrameRange(mRange, false);
        BoxClipboard::sCopyAndPaste(this, newScene);
        newScene->prp_setNameAction(newScene->prp_getName() + " copy");
    });

    const auto parentWidget = menu->parentWidget();
    menu->addAction(QIcon::fromTheme("file_movie"),
                    tr("Map to Different Fps"), [this, parentWidget]() {
        bool ok;
        const qreal newFps = QInputDialog::getDouble(
                    parentWidget, "Map to Different Fps",
                    "New Fps:", mFps, 1, 999, 2, &ok);
        if (ok) { changeFpsTo(newFps); }
    });

    menu->addAction(QIcon::fromTheme("sequence"),
                    tr("Scene Properties"), [this]() {
        const auto& dialogs = DialogsInterface::instance();
        dialogs.showSceneSettingsDialog(this);
    });
}

void Canvas::handleRightButtonMouseRelease(const eMouseEvent& e)
{
    if (e.fMouseGrabbing) {
        cancelCurrentTransform();
        e.fReleaseMouse();
        mValueInput.clearAndDisableInput();
    } else {
        mPressedBox = mHoveredBox;
        mPressedPoint = mHoveredPoint_d;
        if (mPressedPoint) {
            QMenu qMenu;
            PointTypeMenu menu(&qMenu, this, e.fWidget);
            if (mPressedPoint->selectionEnabled()) {
                if (!mPressedPoint->isSelected()) {
                    if (!e.shiftMod()) { clearPointsSelection(); }
                    addPointToSelection(mPressedPoint);
                }
                for (const auto& pt : mSelectedPoints_d) { pt->canvasContextMenu(&menu); }
            } else { mPressedPoint->canvasContextMenu(&menu); }
            qMenu.exec(e.fGlobalPos);
        } else if (mPressedBox) {
            if (!mPressedBox->isSelected()) {
                if (!e.shiftMod()) { clearBoxesSelection(); }
                addBoxToSelection(mPressedBox);
            }

            QMenu qMenu(e.fWidget);
            PropertyMenu menu(&qMenu, this, e.fWidget);
            for (const auto& box : mSelectedBoxes) { box->setupCanvasMenu(&menu); }
            qMenu.exec(e.fGlobalPos);
        } else {
            clearPointsSelection();
            clearBoxesSelection();
            QMenu menu(e.fWidget);
            addActionsToMenu(&menu);
            menu.exec(e.fGlobalPos);
        }
    }
    mDocument.actionFinished();
}

void Canvas::clearHoveredEdge()
{
    mHoveredNormalSegment.reset();
}

void Canvas::handleMovePointMousePressEvent(const eMouseEvent& e)
{
    if (mHoveredNormalSegment.isValid()) {
        if (e.ctrlMod()) {
            clearPointsSelection();
            mPressedPoint = mHoveredNormalSegment.divideAtAbsPos(e.fPos);
        } else {
            mCurrentNormalSegment = mHoveredNormalSegment;
            mCurrentNormalSegmentT = mCurrentNormalSegment.closestAbsT(e.fPos);
            clearPointsSelection();
            clearCurrentSmartEndPoint();
            clearLastPressedPoint();
        }
        clearHovered();
    } else if (mPressedPoint) {
        if (mPressedPoint->isSelected()) { return; }
        if (!e.shiftMod() && mPressedPoint->selectionEnabled()) {
            clearPointsSelection();
        }
        if (!mPressedPoint->selectionEnabled()) {
            addPointToSelection(mPressedPoint);
        }
    }
}


// bone tool: pressing near the tail of the last (or any) bone continues
// the chain with a child bone; pressing farther away starts a new,
// independent bone at the click point. The draft bone's length/rotation
// then follow the cursor until the next click (see updateDraftBone in
// the mouse-move path)
void Canvas::boneCreatePress(const eMouseEvent& e) {
    clearBoxesSelection();
    // screen-pixel pick radius so the connect feel stays the same at
    // every zoom level (same style as pickBoneAt's 12*e.fScale)
    const qreal pickDist = 20*e.fScale;
    // branching: pressing near ANOTHER bone's tail re-targets the
    // chain there (fork a new branch from that bone)
    for(const auto bone : mBones) {
        if(!bone || bone == mDraftBone) continue;
        if(QLineF(e.fPos, bone->getTailAbsPos()).length() < pickDist) {
            mDraftBone = bone->addChildBone();
            mChainTail = mDraftBone; // continue the fork from the new bone
            addBoxToSelection(bone);
            // orient the fresh bone towards the press point RIGHT AWAY:
            // without this it briefly renders at the parent's ORIGIN
            // with the default length/rotation (visible jump; and it
            // stays there on a click without any drag)
            updateDraftBone(e.fPos);
            return;
        }
    }
    // continue the chain ONLY when the press hugs the chain tail - a
    // distant press means the user is drawing a separate bone and must
    // not be yanked back onto the chain; Ctrl forces a new chain too
    if(mChainTail && !e.ctrlMod()
            && QLineF(e.fPos, mChainTail->getTailAbsPos()).length() < pickDist) {
        mDraftBone = mChainTail->addChildBone();
        // advance the chain tail to the fresh bone, otherwise every
        // subsequent bone keeps branching off the FIRST one
        mChainTail = mDraftBone;
        addBoxToSelection(mChainTail);
        updateDraftBone(e.fPos);
    } else {
        startBoneChain(e.fPos);
        mChainTail = mDraftBone;
        updateDraftBone(e.fPos);
    }
}

void Canvas::updateDraftBone(const QPointF& absPos) {
    if(!mDraftBone) return;
    const auto parent = mDraftBone->getParentGroup();
    const QPointF pRel = parent ? parent->mapAbsPosToRel(absPos) : absPos;
    const QPointF bonePos = mDraftBone->getBoxTransformAnimator()->
            getPosAnimator()->getEffectiveValue();
    const QPointF rel = pRel - bonePos;
    const qreal len = qBound(10., pointToLen(rel), 2000.);
    mDraftBone->lengthAnimator()->setCurrentBaseValue(len);
    if(len > 10) {
        const qreal deg = qRadiansToDegrees(qAtan2(rel.y(), rel.x()));
        mDraftBone->getBoxTransformAnimator()->getRotAnimator()->
                setCurrentBaseValue(deg);
    }
    mDraftBone->planUpdate(UpdateReason::userChange);
}

// nearest visible bone whose head-tail segment passes within maxDist
// of the given scene position (shared by the pose/bind tools)
Bone* Canvas::pickBoneAt(const QPointF& absPos, const qreal maxDist) {
    Bone* best = nullptr;
    qreal bestDist = maxDist;
    for(const auto bone : mBones) {
        if(!bone || !bone->isVisible()) continue;
        const QPointF head = bone->getHeadAbsPos();
        const QPointF tail = bone->getTailAbsPos();
        const QPointF d = tail - head;
        const qreal denom = d.x()*d.x() + d.y()*d.y();
        const qreal u = denom > 0 ? qBound(0., ((absPos.x()-head.x())*d.x() +
                                                (absPos.y()-head.y())*d.y()) /
                                      denom, 1.) : 0.;
        const QPointF proj(head.x() + u*d.x(), head.y() + u*d.y());
        const qreal dist = QLineF(absPos, proj).length();
        if(dist < bestDist) { bestDist = dist; best = bone; }
    }
    return best;
}

// bone parent-link tool: with a bone selected (object tool), clicking
// another bone makes that bone its parent - the hierarchy re-links in
// place, world positions of the whole sub-chain are preserved
void Canvas::boneParentPress(const eMouseEvent& e) {
    const auto target = pickBoneAt(e.fPos, 12*e.fScale);
    if(!target) { clearBoxesSelection(); return; }
    // NOTE: Canvas IS the scene - getParentScene() returns null here
    // (an early bug that made the tool silently do nothing)
    const auto& sel = getSelectedBoxesList();
    if(sel.isEmpty()) return;
    const auto selected = enve_cast<Bone*>(sel.last());
    if(!selected || selected == target) return;
    Bone::diag(QStringLiteral("parentLink %1 under %2")
         .arg(selected->prp_getName(), target->prp_getName()));
    selected->setParentBone(target);
}

// bone bind tool (Moho flow): with one or more layers selected (done in
// the object tool), clicking a bone re-parents them into it; clicking
// empty space just clears the selection
void Canvas::boneBindPress(const eMouseEvent& e) {
    const auto bone = pickBoneAt(e.fPos, 12*e.fScale);
    if(!bone) { clearBoxesSelection(); return; }
    bone->bindSelectedLayers();
    clearBoxesSelection();
    addBoxToSelection(bone);
}

// bone select tool: bones only - handy in dense scenes where clicking
// through stacked artwork would otherwise pick graphics
void Canvas::boneSelectPress(const eMouseEvent& e) {
    const auto bone = pickBoneAt(e.fPos, 12*e.fScale);
    if(bone) {
        if(e.shiftMod()) {
            // shift-click toggles membership like the object tool
            if(bone->isSelected()) removeBoxFromSelection(bone);
            else addBoxToSelection(bone);
        } else {
            clearBoxesSelection();
            addBoxToSelection(bone);
        }
    } else {
        // empty space: begin a marquee (shift keeps the current pick)
        if(!e.shiftMod()) clearBoxesSelection();
        startSelectionAtPoint(e.fPos);
    }
}

// scene camera tool (AE-like, Blender-flavoured): LMB drag orbits the
// composition (tilt X/Y), Shift+LMB pans, Ctrl+LMB drags zoom. AE
// flow: a camera LAYER must exist - auto-created on first use; the
// camera only affects layers with their 3D switch enabled. All values
// live as plain animators on the camera layer - the standard
// prp_start/finishTransform pipeline carries undo and auto-keying
void Canvas::cameraPress(const eMouseEvent& e) {
    const bool autoCreated = !getCameraLayer();
    if(autoCreated) addCameraLayerAction();
    const auto cam = getCameraLayer();
    if(!cam) return;
    if(e.ctrlMod()) mCamDragMode = CamDragMode::zoom;
    else if(e.shiftMod()) mCamDragMode = CamDragMode::pan;
    else mCamDragMode = CamDragMode::orbit;
    qWarning() << "CAMERA: press, drag mode" << int(mCamDragMode)
               << (autoCreated ? "(camera layer auto-created)"
                               : "(camera layer present)");
    mCamPressPos = e.fPos;
    mCamStartPanX = cam->panXAnimator()->getCurrentBaseValue();
    mCamStartPanY = cam->panYAnimator()->getCurrentBaseValue();
    mCamStartZoom = cam->zoomAnimator()->getCurrentBaseValue();
    mCamStartRotX = cam->rotXAnimator()->getCurrentBaseValue();
    mCamStartRotY = cam->rotYAnimator()->getCurrentBaseValue();
    if(mCamDragMode == CamDragMode::orbit) {
        cam->rotXAnimator()->prp_startTransform();
        cam->rotYAnimator()->prp_startTransform();
    } else if(mCamDragMode == CamDragMode::pan) {
        cam->panXAnimator()->prp_startTransform();
        cam->panYAnimator()->prp_startTransform();
    } else {
        cam->zoomAnimator()->prp_startTransform();
    }
}

void Canvas::cameraMove(const eMouseEvent& e) {
    if(mCamDragMode == CamDragMode::none) return;
    const auto cam = getCameraLayer();
    if(!cam) { mCamDragMode = CamDragMode::none; return; }
    const QPointF d = e.fPos - mCamPressPos;
    if(mCamDragMode == CamDragMode::orbit) {
        cam->rotYAnimator()->setCurrentBaseValue(
                    qBound(-89., mCamStartRotY + d.x()*0.3, 89.));
        cam->rotXAnimator()->setCurrentBaseValue(
                    qBound(-89., mCamStartRotX + d.y()*0.3, 89.));
    } else if(mCamDragMode == CamDragMode::pan) {
        // content follows the cursor 1:1: canvas units = view pixels
        // divided by view scale and camera zoom
        const qreal k = 1./(e.fScale*qMax(0.01, mCamStartZoom));
        cam->panXAnimator()->setCurrentBaseValue(
                    mCamStartPanX - d.x()*k);
        cam->panYAnimator()->setCurrentBaseValue(
                    mCamStartPanY - d.y()*k);
    } else {
        // drag up = zoom in
        cam->zoomAnimator()->setCurrentBaseValue(
                    qBound(0.01, mCamStartZoom*std::exp(-d.y()*0.005),
                           100.));
    }
}

void Canvas::cameraRelease() {
    if(mCamDragMode == CamDragMode::none) return;
    const auto cam = getCameraLayer();
    if(cam) {
        if(mCamDragMode == CamDragMode::orbit) {
            cam->rotXAnimator()->prp_finishTransform();
            cam->rotYAnimator()->prp_finishTransform();
        } else if(mCamDragMode == CamDragMode::pan) {
            cam->panXAnimator()->prp_finishTransform();
            cam->panYAnimator()->prp_finishTransform();
        } else {
            cam->zoomAnimator()->prp_finishTransform();
        }
    }
    mCamDragMode = CamDragMode::none;
    if(Document::sInstance) Document::sInstance->actionFinished();
}

void Canvas::cameraCancel() {
    if(mCamDragMode == CamDragMode::none) return;
    const auto cam = getCameraLayer();
    if(cam) {
        // write the press-time values back before finishing so the
        // undo entry becomes a no-op
        if(mCamDragMode == CamDragMode::orbit) {
            cam->rotXAnimator()->setCurrentBaseValue(mCamStartRotX);
            cam->rotYAnimator()->setCurrentBaseValue(mCamStartRotY);
        } else if(mCamDragMode == CamDragMode::pan) {
            cam->panXAnimator()->setCurrentBaseValue(mCamStartPanX);
            cam->panYAnimator()->setCurrentBaseValue(mCamStartPanY);
        } else {
            cam->zoomAnimator()->setCurrentBaseValue(mCamStartZoom);
        }
    }
    cameraRelease();
}

// bone pose tool (Moho-style): pick the nearest bone under the cursor.
// Pressing near the head joint grabs a MOVE (translate the bone, the
// chain follows); pressing anywhere else on the body grabs a ROTATION
// around the head joint. Recording auto-keys the touched animator.
void Canvas::bonePosePress(const eMouseEvent& e) {
    const qreal pickPx = 12*e.fScale;
    Bone* best = pickBoneAt(e.fPos, pickPx);
    if(!best) { clearBoxesSelection(); return; }

    clearBoxesSelection();
    addBoxToSelection(best);
    mPoseBone = best;
    mPoseMoved = false;
    const auto transform = best->getBoxTransformAnimator();
    const QPointF head = best->getHeadAbsPos();
    if(QLineF(e.fPos, head).length() < 10*e.fScale) {
        mPoseMode = PoseDragMode::move;
        mPoseMoveLast = e.fPos;
        transform->getPosAnimator()->prp_startTransform();
    } else {
        mPoseMode = PoseDragMode::rotate;
        mPoseStartAngle = qAtan2(e.fPos.y() - head.y(),
                                 e.fPos.x() - head.x());
        mPoseLastAngle = mPoseStartAngle;
        mPoseAccumDeg = 0;
        const auto rot = transform->getRotAnimator();
        mPoseStartRot = rot->getEffectiveValue();
        rot->prp_startTransform();
    }
}

void Canvas::bonePoseMove(const eMouseEvent& e) {
    if(!mPoseBone || mPoseMode == PoseDragMode::none) return;
    const auto transform = mPoseBone->getBoxTransformAnimator();
    if(mPoseMode == PoseDragMode::rotate) {
        const QPointF head = mPoseBone->getHeadAbsPos();
        const qreal cur = qAtan2(e.fPos.y() - head.y(),
                                 e.fPos.x() - head.x());
        // wrap each INCREMENT to (-180, 180]: dragging across the
        // atan2 +/-pi boundary must accumulate smoothly instead of
        // jumping ~360 (keys interpolating the long way = reversed
        // rotation direction)
        qreal incr = qRadiansToDegrees(cur - mPoseLastAngle);
        while(incr > 180.) incr -= 360.;
        while(incr < -180.) incr += 360.;
        mPoseAccumDeg += incr;
        mPoseLastAngle = cur;
        transform->getRotAnimator()->setCurrentBaseValue(
                    mPoseStartRot + mPoseAccumDeg);
    } else {
        const QPointF delta = e.fPos - mPoseMoveLast;
        mPoseMoveLast = e.fPos;
        transform->translate(delta.x(), delta.y());
    }
    mPoseMoved = true;
    mPoseBone->planUpdate(UpdateReason::userChange);
}

void Canvas::bonePoseRelease() {
    if(!mPoseBone) return;
    const auto transform = mPoseBone->getBoxTransformAnimator();
    if(mPoseMode == PoseDragMode::rotate) {
        transform->getRotAnimator()->prp_finishTransform();
        // Moho-style auto-keyframing: posing a bone records a key at
        // the current frame; with auto-freeze ON a plain CLICK (no
        // drag) also freezes - the first pose frame usually needs no
        // movement
        if(mPoseMoved || Bone::sAutoFreezePose) {
            if(Bone::sAutoFreezePose) freezeAllBones();
            else if(mPoseMoved) {
                transform->getRotAnimator()->anim_saveCurrentValueAsKey();
            }
        }
    } else if(mPoseMode == PoseDragMode::move) {
        transform->getPosAnimator()->prp_finishTransform();
        if(mPoseMoved || Bone::sAutoFreezePose) {
            if(Bone::sAutoFreezePose) freezeAllBones();
            else if(mPoseMoved) {
                transform->getPosAnimator()->anim_saveCurrentValueAsKey();
            }
        }
    }
    if(Document::sInstance) Document::sInstance->actionFinished();
    mPoseMode = PoseDragMode::none;
    mPoseBone = nullptr;
    mPoseMoved = false;
}

void Canvas::bonePoseCancel() {
    if(!mPoseBone) return;
    const auto transform = mPoseBone->getBoxTransformAnimator();
    if(mPoseMode == PoseDragMode::rotate) {
        transform->getRotAnimator()->prp_cancelTransform();
    } else if(mPoseMode == PoseDragMode::move) {
        transform->getPosAnimator()->prp_cancelTransform();
    }
    mPoseMode = PoseDragMode::none;
    mPoseBone = nullptr;
}

void Canvas::handleLeftButtonMousePress(const eMouseEvent& e)
{
    if (e.fMouseGrabbing) {
        //handleMouseRelease(event->pos());
        //releaseMouseAndDontTrack();
        return;
    }

    mDoubleClick = false;
    //mMovesToSkip = 2;
    mStartTransform = true;
    mHasCreationPressPos = false;

    const qreal invScale = 1/e.fScale;
    const qreal invScaleUi = (qApp ? qApp->devicePixelRatio() : 1.0) * invScale;

    if (tryStartShearGizmo(e, invScaleUi)) {
        mPressedPoint = nullptr;
        return;
    }
    if (tryStartScaleGizmo(e, invScaleUi)) {
        mPressedPoint = nullptr;
        return;
    }
    if (tryStartAxisGizmo(e, invScaleUi)) {
        mPressedPoint = nullptr;
        return;
    }
    if (tryStartRotateWithGizmo(e, invScaleUi)) {
        mPressedPoint = nullptr;
        return;
    }

    mPressedPoint = getPointAtAbsPos(e.fPos, mCurrentMode, invScale);

    if (mRotPivot->isPointAtAbsPos(e.fPos, mCurrentMode, invScale)) {
        return mRotPivot->setSelected(true);
    }
    if (mCurrentMode == CanvasMode::boxTransform) {
        if (mHoveredPoint_d) { handleMovePointMousePressEvent(e); }
        else { handleMovePathMousePressEvent(e); }
    } else if (mCurrentMode == CanvasMode::pathCreate) {
        handleAddSmartPointMousePress(e);
    } else if (mCurrentMode == CanvasMode::pointTransform) {
        handleMovePointMousePressEvent(e);
    } else if (mCurrentMode == CanvasMode::drawPath) {
        const bool manual = mDocument.fDrawPathManual;
        bool start;
        if (manual) {
            start = mManualDrawPathState == ManualDrawPathState::none;
            if (mManualDrawPathState == ManualDrawPathState::drawn) {
                qreal dist;
                const int forceSplit = mDrawPath.nearestForceSplit(e.fPos, &dist);
                const int maxDist = 10;
                if (dist < maxDist) { mDrawPath.removeForceSplit(forceSplit); }
                else {
                    const int smoothPt = mDrawPath.nearestSmoothPt(e.fPos, &dist);
                    if (dist < maxDist) { mDrawPath.addForceSplit(smoothPt); }
                }
                mDrawPath.fit(DBL_MAX/5, false);
            }
        } else { start = true; }
        if (start) {
            mDrawPathFirst = getPointAtAbsPos(e.fPos, mCurrentMode, invScale);
            mDrawPathFit = 0;
            drawPathClear();
            mDrawPath.lineTo(e.fPos);
        }
    } else if (mCurrentMode == CanvasMode::pickFillStroke ||
               mCurrentMode == CanvasMode::pickFillStrokeEvent) {
        //mPressedBox = getBoxAtFromAllDescendents(e.fPos);
    } else if (mCurrentMode == CanvasMode::boneCreate) {
        boneCreatePress(e);
    } else if (mCurrentMode == CanvasMode::bonePose) {
        bonePosePress(e);
    } else if (mCurrentMode == CanvasMode::boneBind) {
        boneBindPress(e);
    } else if (mCurrentMode == CanvasMode::boneParent) {
        boneParentPress(e);
    } else if (mCurrentMode == CanvasMode::boneSelect) {
        boneSelectPress(e);
    } else if (mCurrentMode == CanvasMode::camera) {
        cameraPress(e);
    } else if (mCurrentMode == CanvasMode::circleCreate) {
        const auto newPath = enve::make_shared<Circle>();
        newPath->planCenterPivotPosition();
        mCurrentContainer->addContained(newPath);
        const QPointF snappedPos = snapEventPos(e, false);
        newPath->setAbsolutePos(snappedPos);
        clearBoxesSelection();
        addBoxToSelection(newPath.get());
        mCurrentCircle = newPath.get();
        mCreationPressPos = snappedPos;
        mHasCreationPressPos = true;
    } else if (mCurrentMode == CanvasMode::nullCreate) {
        const auto newPath = enve::make_shared<NullObject>();
        newPath->planCenterPivotPosition();
        mCurrentContainer->addContained(newPath);
        newPath->setAbsolutePos(e.fPos);
        clearBoxesSelection();
        addBoxToSelection(newPath.get());
    } else if (mCurrentMode == CanvasMode::rectCreate) {
        if (mDocument.fMaskRectActive) {
            // AE-style rect mask: drag a rectangle onto a layer
            if (!startMaskRectDrag(e)) { return; }
        } else {
            const auto newPath = enve::make_shared<RectangleBox>();
            newPath->planCenterPivotPosition();
            mCurrentContainer->addContained(newPath);
            const QPointF snappedPos = snapEventPos(e, false);
            newPath->setAbsolutePos(snappedPos);
            clearBoxesSelection();
            addBoxToSelection(newPath.get());
            mCurrentRectangle = newPath.get();
            mCreationPressPos = snappedPos;
            mHasCreationPressPos = true;
        }
    } else if (mCurrentMode == CanvasMode::textCreate) {
        if (enve_cast<TextBox*>(mHoveredBox)) {
            setCurrentBox(mHoveredBox);
            emit openTextEditor();
        } else {
            const auto newPath = enve::make_shared<TextBox>();
            newPath->planCenterPivotPosition();
            newPath->setFontFamilyAndStyle(mDocument.fFontFamily,
                                           mDocument.fFontStyle);
            newPath->setFontSize(mDocument.fFontSize);
            mCurrentContainer->addContained(newPath);
            newPath->setAbsolutePos(e.fPos);
            mCurrentTextBox = newPath.get();
            clearBoxesSelection();
            addBoxToSelection(newPath.get());
            // creating text means the user is about to type: focus
            // the text input explicitly (selection alone no longer
            // does, so Space playback keeps working)
            emit openTextEditor();
        }
    }
}

void Canvas::cancelCurrentTransform()
{
    mGizmos.fState.rotatingFromHandle = false;

    if(mCurrentMode == CanvasMode::bonePose) { bonePoseCancel(); }
    if(mCurrentMode == CanvasMode::camera) { cameraCancel(); }

    if (mCurrentMode == CanvasMode::pointTransform) {
        if (mCurrentNormalSegment.isValid()) {
            mCurrentNormalSegment.cancelPassThroughTransform();
        } else { cancelSelectedPointsTransform(); }
    } else if (mCurrentMode == CanvasMode::boxTransform) {
        if (mRotPivot->isSelected()) { mRotPivot->cancelTransform(); }
        else { cancelSelectedBoxesTransform(); }
    } else if (mCurrentMode == CanvasMode::pathCreate) {
        //
    } else if (mCurrentMode == CanvasMode::pickFillStroke ||
               mCurrentMode == CanvasMode::pickFillStrokeEvent) {
        //mCanvasWindow->setCanvasMode(MOVE_PATH);
    }
    mValueInput.clearAndDisableInput();
    mTransMode = TransformMode::none;
    cancelCurrentTransformGimzos();
}

void Canvas::handleMovePointMouseRelease(const eMouseEvent &e)
{
    if (mRotPivot->isSelected()) {
        mRotPivot->setSelected(false);
    } else if (mTransMode == TransformMode::rotate ||
               mTransMode == TransformMode::scale ||
               mTransMode == TransformMode::shear) {
        finishSelectedPointsTransform();
        mTransMode = TransformMode::none;
    } else if (mSelecting) {
        mSelecting = false;
        if (!e.shiftMod()) { clearPointsSelection(); }
        moveSecondSelectionPoint(e.fPos);
        selectAndAddContainedPointsToSelection(mSelectionRect);
    } else if (mStartTransform) {
        if (mPressedPoint) {
            if (mPressedPoint->isCtrlPoint()) { removePointFromSelection(mPressedPoint); }
            else if (e.shiftMod()) {
                if (mPressedPoint->isSelected()) { removePointFromSelection(mPressedPoint); }
                else { addPointToSelection(mPressedPoint); }
            } else { selectOnlyLastPressedPoint(); }
        } else {
            mPressedBox = mCurrentContainer->getBoxAt(e.fPos);
            if (mPressedBox ? !!enve_cast<ContainerBox*>(mPressedBox) : true) {
                const auto pressedBox = getBoxAtFromAllDescendents(e.fPos);
                if (!pressedBox) {
                    if (!e.shiftMod()) { clearPointsSelectionOrDeselect(); }
                } else {
                    clearPointsSelection();
                    clearCurrentSmartEndPoint();
                    clearLastPressedPoint();
                    setCurrentBoxesGroup(pressedBox->getParentGroup());
                    addBoxToSelection(pressedBox);
                    mPressedBox = pressedBox;
                }
            }
            if (mPressedBox) {
                if (e.shiftMod()) {
                    if (mPressedBox->isSelected()) { removeBoxFromSelection(mPressedBox); }
                    else { addBoxToSelection(mPressedBox); }
                } else {
                    clearPointsSelection();
                    clearCurrentSmartEndPoint();
                    clearLastPressedPoint();
                    selectOnlyLastPressedBox();
                }
            }
        }
    } else {
        finishSelectedPointsTransform();
        if (mPressedPoint) {
            if (!mPressedPoint->selectionEnabled()) { removePointFromSelection(mPressedPoint); }
        }
    }
}

void Canvas::handleMovePathMouseRelease(const eMouseEvent &e)
{
    if (mRotPivot->isSelected()) {
        if (!mStartTransform) { mRotPivot->finishTransform(); }
        mRotPivot->setSelected(false);
    } else if (mTransMode == TransformMode::rotate) {
        pushUndoRedoName("Rotate Objects");
        finishSelectedBoxesTransform();
    } else if (mTransMode == TransformMode::scale) {
        pushUndoRedoName("Scale Objects");
        finishSelectedBoxesTransform();
    } else if (mTransMode == TransformMode::shear) {
        pushUndoRedoName("Shear Objects");
        finishSelectedBoxesTransform();
    } else if (mStartTransform) {
        mSelecting = false;
        if (e.shiftMod() && mPressedBox) {
            if (mPressedBox->isSelected()) { removeBoxFromSelection(mPressedBox); }
            else { addBoxToSelection(mPressedBox); }
        } else { selectOnlyLastPressedBox(); }
    } else if (mSelecting) {
        moveSecondSelectionPoint(e.fPos);
        mCurrentContainer->addContainedBoxesToSelection(mSelectionRect);
        mSelecting = false;
    } else {
        pushUndoRedoName("Move Objects");
        finishSelectedBoxesTransform();
    }
}

SmartNodePoint* drawPathAppend(const QList<qCubicSegment2D>& fitted,
                               SmartNodePoint* endPoint)
{
    for (int i = 0; i < fitted.count(); i++) {
        const auto& seg = fitted.at(i);
        endPoint->moveC2ToAbsPos(seg.c1());
        endPoint = endPoint->actionAddPointAbsPos(seg.p3());
        endPoint->moveC0ToAbsPos(seg.c2());
    }
    return endPoint;
}

qsptr<SmartVectorPath> drawPathNew(QList<qCubicSegment2D>& fitted)
{
    const QPointF& begin = fitted.first().p0();
    const QPointF& end = fitted.last().p3();
    const qreal beginEndDist = pointToLen(end - begin);
    const bool close = beginEndDist < 7 && fitted.count() > 1;
    if (close) { fitted.last().setP3(begin); }
    const auto newPath = enve::make_shared<SmartVectorPath>();
    CubicList fittedList(fitted);
    newPath->loadSkPath(fittedList.toSkPath());
    newPath->planCenterPivotPosition();
    return newPath;
}

void Canvas::drawPathClear()
{
    mManualDrawPathState = ManualDrawPathState::none;
    mDrawPathFirst.clear();
    mDrawPath.clear();
    mDrawPathTmp.reset();
}

void Canvas::drawPathFinish(const qreal invScale)
{
    mDrawPath.smooth(mDocument.fDrawPathSmooth);
    const bool manual = mDocument.fDrawPathManual;
    const qreal error = manual ? DBL_MAX/5 :
                                 mDocument.fDrawPathMaxError;
    mDrawPath.fit(error, !manual);

    auto& fitted = mDrawPath.getFitted();
    if (!fitted.isEmpty()) {
        const QPointF& begin = fitted.first().p0();
        const QPointF& end = fitted.last().p3();
        const auto beginHover = getPointAtAbsPos(begin, mCurrentMode, invScale);
        const auto beginNode = enve_cast<SmartNodePoint*>(beginHover);
        const auto endHover = getPointAtAbsPos(end, mCurrentMode, invScale);
        const auto endNode = enve_cast<SmartNodePoint*>(endHover);
        const bool beginEndPoint = beginNode ? beginNode->isEndPoint() : false;
        const bool endEndPoint = endNode ? endNode->isEndPoint() : false;
        bool createNew = false;

        if (beginNode && endNode && beginNode != endNode) {
            const auto beginParent = beginNode->getTargetAnimator();
            const auto endParent = endNode->getTargetAnimator();
            const bool sampeParent = beginParent == endParent;

            if (sampeParent) {
                const auto transform = beginNode->getTransform();
                const auto matrix = transform->getTotalTransform();
                const auto invMatrix = matrix.inverted();
                std::for_each(fitted.begin(), fitted.end(),
                              [&invMatrix](qCubicSegment2D& seg) {
                    seg.transform(invMatrix);
                });
                const int beginId = beginNode->getNodeId();
                const int endId = endNode->getNodeId();
                beginParent->actionReplaceSegments(beginId, endId, fitted);
            } else if (beginEndPoint && endEndPoint) {
                const bool reverse = endNode->hasNextPoint();

                const auto orderedBegin = reverse ? endNode : beginNode;
                const auto orderedEnd = reverse ? beginNode : endNode;

                if (orderedEnd->hasNextPoint() || !endNode->hasNextPoint()) {
                    std::reverse(fitted.begin(), fitted.end());
                    std::for_each(fitted.begin(), fitted.end(),
                                  [](qCubicSegment2D& seg) { seg.reverse(); });
                }

                const auto& lastSeg = fitted.last();
                const auto mid = fitted.mid(0, fitted.count() - 1);
                const auto last = drawPathAppend(mid, orderedEnd);
                last->moveC2ToAbsPos(lastSeg.c1());
                orderedBegin->moveC0ToAbsPos(lastSeg.c2());
                last->actionConnectToNormalPoint(orderedBegin);
            } else { createNew = true; }
        } else if (beginNode && beginEndPoint) {
            drawPathAppend(fitted, beginNode);
        } else if (endNode && endEndPoint) {
            drawPathAppend(fitted, endNode);
        } else { createNew = true; }
        if (createNew) {
            const auto matrix = mCurrentContainer->getTotalTransform();
            const auto invMatrix = matrix.inverted();
            std::for_each(fitted.begin(), fitted.end(),
                          [&invMatrix](qCubicSegment2D& seg) {
                seg.transform(invMatrix);
            });
            const auto newPath = drawPathNew(fitted);
            mCurrentContainer->addContained(newPath);
            clearBoxesSelection();
            addBoxToSelection(newPath.get());
        }
    }

    drawPathClear();
}

const QColor Canvas::pickPixelColor(const QPoint &pos)
{
    // try the "safe" option first
    if (QApplication::activeWindow()) {
        const auto nPos = QApplication::activeWindow()->mapFromGlobal(pos);
        return QApplication::activeWindow()->grab(QRect(QPoint(nPos.x(), nPos.y()),
                                                        QSize(1, 1))).toImage().pixel(0, 0);
    }

    // "insecure" fallback (will not work in a sandbox or wayland)
    // will prompt for permissions on macOS
    // Windows and X11 don't care
    QScreen *screen = QApplication::screenAt(pos);
    if (!screen) { return QColor(); }
    WId wid = QApplication::desktop()->winId();
    const auto pix = screen->grabWindow(wid, pos.x(), pos.y(), 1, 1);
    return QColor(pix.toImage().pixel(0, 0));
}

void Canvas::applyPixelColor(const QColor &color,
                             const bool &fill)
{
    if (!color.isValid()) { return; }
    for (const auto& box : mSelectedBoxes) {
        if (fill) {
            auto settings = box->getFillSettings();
            if (settings) {
                if (settings->getPaintType() == PaintType::NOPAINT) {
                    settings->setPaintType(PaintType::FLATPAINT);
                }
                settings->setCurrentColor(color, true);
                box->fillStrokeSettingsChanged();
            }
        } else {
            auto settings = box->getStrokeSettings();
            if (settings) {
                if (settings->getPaintType() == PaintType::NOPAINT) {
                    settings->setPaintType(PaintType::FLATPAINT);
                }
                settings->setCurrentColor(color, true);
                box->fillStrokeSettingsChanged();
            }
        }
    }
}

bool Canvas::startMaskRectDrag(const eMouseEvent &e)
{
    const auto host = resolveMaskHost(e);
    if (!host) { return false; }
    const auto maskPath = createMaskPath(host);
    host->setRevealRowsOnce();
    host->addContained(maskPath->ref<eBoxOrSound>());
    clearBoxesSelection();
    addBoxToSelection(maskPath.get());

    const QPointF snappedPos = snapEventPos(e, false);
    mMaskRectAnchor = snappedPos;
    mCreationPressPos = snappedPos;
    mHasCreationPressPos = true;
    const auto relPos = maskPath->mapAbsPosToRel(snappedPos);
    maskPath->getBoxTransformAnimator()->setPosition(relPos.x(), relPos.y());
    mCurrentMaskRectPath = maskPath.get();

    // closed 4-corner rectangle (TL-TR-BR-BL), corner nodes so the
    // rect stays editable with the point/pen tools like any path
    mCurrentMaskRectNodes.clear();
    const auto handler = maskPath->getPathAnimator();
    auto node = handler->createNewSubPathAtRelPos({0, 0});
    mCurrentMaskRectNodes << node;
    for (int i = 0; i < 3; i++) {
        node = node->actionAddPointAbsPos(snappedPos);
        mCurrentMaskRectNodes << node;
    }
    if (mCurrentMaskRectNodes.count() == 4) {
        mCurrentMaskRectNodes.last()->actionConnectToNormalPoint(
                    mCurrentMaskRectNodes.first().data());
    }
    return true;
}

void Canvas::updateMaskRectDrag(const eMouseEvent &e)
{
    if (mCurrentMaskRectNodes.count() != 4) { return; }
    const QPointF anchor = mHasCreationPressPos ? mCreationPressPos
                                                : mMaskRectAnchor;
    const QPointF current = snapEventPos(e, false);
    qreal dx = current.x() - anchor.x();
    qreal dy = current.y() - anchor.y();
    if (e.shiftMod()) { // square, like the rectangle tool
        const qreal len = qMax(qAbs(dx), qAbs(dy));
        dx = dx < 0 ? -len : len;
        dy = dy < 0 ? -len : len;
    }
    const QPointF tl(qMin(anchor.x(), anchor.x() + dx),
                     qMin(anchor.y(), anchor.y() + dy));
    const QPointF br(qMax(anchor.x(), anchor.x() + dx),
                     qMax(anchor.y(), anchor.y() + dy));
    if (mStartTransform) {
        for (const auto& node : mCurrentMaskRectNodes) {
            if (node) { node->startTransform(); }
        }
    }
    const QPointF corners[4] = {
        tl, QPointF(br.x(), tl.y()), br, QPointF(tl.x(), br.y()) };
    for (int i = 0; i < 4; i++) {
        const auto& node = mCurrentMaskRectNodes.at(i);
        if (node) { node->setAbsolutePos(corners[i]); }
    }
}

void Canvas::finishMaskRectDrag(const eMouseEvent &e)
{
    Q_UNUSED(e)
    if (mCurrentMaskRectNodes.isEmpty()) { return; }
    if (!mStartTransform) {
        for (const auto& node : mCurrentMaskRectNodes) {
            if (node) { node->finishTransform(); }
        }
    }
    // a click without a drag leaves a zero-area mask - under AE
    // semantics that hides the whole layer, so drop it instead
    bool degenerate = true;
    if (mCurrentMaskRectNodes.count() == 4 &&
            mCurrentMaskRectNodes.at(0) && mCurrentMaskRectNodes.at(2)) {
        const QPointF p0 = mCurrentMaskRectNodes.at(0)->getAbsolutePos();
        const QPointF p2 = mCurrentMaskRectNodes.at(2)->getAbsolutePos();
        degenerate = qAbs(p2.x() - p0.x()) < 1. ||
                     qAbs(p2.y() - p0.y()) < 1.;
    }
    if (degenerate && mCurrentMaskRectPath) {
        mCurrentMaskRectPath->removeFromParent_k();
        clearBoxesSelection();
    }
    mCurrentMaskRectNodes.clear();
    mCurrentMaskRectPath.clear();
    mHasCreationPressPos = false;
    Document::sInstance->actionFinished();
}

void Canvas::handleLeftMouseRelease(const eMouseEvent &e)
{
    if (e.fMouseGrabbing) { e.fReleaseMouse(); }

    handleLeftMouseGizmos();

    // bone select marquee: collect only BONES whose head-tail segment
    // intersects the selection rectangle
    if(mCurrentMode == CanvasMode::boneSelect && mSelecting) {
        mSelecting = false;
        moveSecondSelectionPoint(e.fPos);
        if(!e.shiftMod()) clearBoxesSelection();
        const QRectF rect = mSelectionRect.normalized();
        for(const auto bone : mBones) {
            if(!bone || !bone->isVisible()) continue;
            const QLineF seg(bone->getHeadAbsPos(), bone->getTailAbsPos());
            const QRectF segBounds = QRectF(seg.p1(), seg.p2()).normalized();
            if(rect.intersects(segBounds) &&
               (rect.contains(seg.p1()) || rect.contains(seg.p2()) ||
                segBounds.intersected(rect).width() > 0)) {
                addBoxToSelection(bone);
            }
        }
        return;
    }

    if (mCurrentNormalSegment.isValid()) {
        if (!mStartTransform) { mCurrentNormalSegment.finishPassThroughTransform(); }
        mHoveredNormalSegment = mCurrentNormalSegment;
        mHoveredNormalSegment.generateSkPath();
        mCurrentNormalSegment.reset();
        return;
    }
    if (mDoubleClick) { return; }
    if (mCurrentMode == CanvasMode::pointTransform) {
        handleMovePointMouseRelease(e);
    } else if (mCurrentMode == CanvasMode::boxTransform) {
        if (!mPressedPoint) { handleMovePathMouseRelease(e); }
        else {
            handleMovePointMouseRelease(e);
            clearPointsSelection();
        }
    } else if (mCurrentMode == CanvasMode::rectCreate &&
               !mCurrentMaskRectNodes.isEmpty()) {
        finishMaskRectDrag(e);
    } else if (mCurrentMode == CanvasMode::pathCreate) {
        handleAddSmartPointMouseRelease(e);
    } else if (mCurrentMode == CanvasMode::drawPath) {
        const bool manual = mDocument.fDrawPathManual;
        if (manual) { mManualDrawPathState = ManualDrawPathState::drawn; }
        else { drawPathFinish(1/e.fScale); }
    } else if (mCurrentMode == CanvasMode::pickFillStrokeEvent) {
        emit currentPickedColor(pickPixelColor(e.fGlobalPos));
    }
    mValueInput.clearAndDisableInput();
    mTransMode = TransformMode::none;
}

QPointF Canvas::getMoveByValueForEvent(const eMouseEvent &e)
{
    if (mValueInput.inputEnabled()) {
        return mValueInput.getPtValue();
    }
    const QPointF moveByPoint = e.fPos - e.fLastPressPos;
    mValueInput.setDisplayedValue(moveByPoint);
    if (mValueInput.yOnlyMode()) { return {0, moveByPoint.y()}; }
    else if (mValueInput.xOnlyMode()) { return {moveByPoint.x(), 0}; }
    return moveByPoint;
}

void Canvas::handleMovePointMouseMove(const eMouseEvent &e)
{
    if (mRotPivot->isSelected()) {
        if (mStartTransform) { mRotPivot->startTransform(); }
        mRotPivot->moveByAbs(getMoveByValueForEvent(e));
    } else if (mTransMode == TransformMode::scale) {
        scaleSelected(e);
    } else if (mTransMode == TransformMode::shear) {
        shearSelected(e);
    } else if (mTransMode == TransformMode::rotate) {
        rotateSelected(e);
    } else if (mCurrentNormalSegment.isValid()) {
        if (mStartTransform) { mCurrentNormalSegment.startPassThroughTransform(); }
        mCurrentNormalSegment.makePassThroughAbs(e.fPos, mCurrentNormalSegmentT);
    } else {
        const auto& gridSettings = mDocument.getGrid()->getSettings();
        const bool snappingActive = gridSettings.snapEnabled;
        const bool boxesSnapEnabled = snappingActive && gridSettings.snapToBoxes;
        const bool includeSelectedBounds = boxesSnapEnabled && mPressedPoint && mPressedPoint->isPivotPoint();

        if (mPressedPoint) {
            addPointToSelection(mPressedPoint);
            const auto mods = QGuiApplication::queryKeyboardModifiers();
            if (mPressedPoint->isSmartNodePoint()) {
                if (mods & Qt::ControlModifier) {
                    const auto nodePt = static_cast<SmartNodePoint*>(mPressedPoint.data());
                    if (nodePt->isDissolved()) {
                        const int selId = nodePt->moveToClosestSegment(e.fPos);
                        const auto handler = nodePt->getHandler();
                        const auto dissPt = handler->getPointWithId<SmartNodePoint>(selId);
                        if (nodePt->getNodeId() != selId) {
                            removePointFromSelection(nodePt);
                            addPointToSelection(dissPt);
                        }
                        mPressedPoint = dissPt;
                        return;
                    }
                } else if (mods & Qt::ShiftModifier) {
                    const auto nodePt = static_cast<SmartNodePoint*>(mPressedPoint.data());
                    const auto nodePtAnim = nodePt->getTargetAnimator();
                    if (nodePt->isNormal()) {
                        SmartNodePoint* closestNode = nullptr;
                        qreal minDist = 10/e.fScale;
                        for (const auto& sBox : mSelectedBoxes) {
                            if (!enve_cast<SmartVectorPath*>(sBox)) { continue; }
                            const auto sPatBox = static_cast<SmartVectorPath*>(sBox);
                            const auto sAnim = sPatBox->getPathAnimator();
                            for (int i = 0; i < sAnim->ca_getNumberOfChildren(); i++) {
                                const auto sPath = sAnim->getChild(i);
                                if (sPath == nodePtAnim) { continue; }
                                const auto sHandler = static_cast<PathPointsHandler*>(sPath->getPointsHandler());
                                const auto node = sHandler->getClosestNode(e.fPos, minDist);
                                if (node) {
                                    closestNode = node;
                                    minDist = pointToLen(closestNode->getAbsolutePos() - e.fPos);
                                }
                            }
                        }
                        if (closestNode) {
                            const bool reverse = mods & Qt::ALT;

                            const auto sC0 = reverse ? closestNode->getC2Pt() : closestNode->getC0Pt();
                            const auto sC2 = reverse ? closestNode->getC0Pt() : closestNode->getC2Pt();

                            nodePt->setCtrlsMode(closestNode->getCtrlsMode());
                            nodePt->setC0Enabled(sC0->enabled());
                            nodePt->setC2Enabled(sC2->enabled());
                            nodePt->setAbsolutePos(closestNode->getAbsolutePos());
                            nodePt->getC0Pt()->setAbsolutePos(sC0->getAbsolutePos());
                            nodePt->getC2Pt()->setAbsolutePos(sC2->getAbsolutePos());
                        } else {
                            if (mStartTransform) { mPressedPoint->startTransform(); }
                            mPressedPoint->moveByAbs(getMoveByValueForEvent(e));
                        }
                        return;
                    }
                }
            }

            if (!mPressedPoint->selectionEnabled()) {
                if (mStartTransform) {
                    mPressedPoint->startTransform();
                    mGridMoveStartPivot = mPressedPoint->getAbsolutePos();
                }

                auto moveBy = getMoveByValueForEvent(e);
                if (snappingActive) {
                    const auto snapped = moveBySnapTargets(e.fModifiers,
                                                           moveBy,
                                                           gridSettings,
                                                           includeSelectedBounds,
                                                           false,
                                                           false);
                    if (snapped.first) { moveBy = snapped.second; }
                }

                mPressedPoint->moveByAbs(moveBy);
                return;
            }
        }

        if (mStartTransform && !mSelectedPoints_d.isEmpty()) {
            mGridMoveStartPivot = getSelectedPointsAbsPivotPos();
        }

        auto moveBy = getMoveByValueForEvent(e);
        if (snappingActive && !mSelectedPoints_d.isEmpty()) {
            const auto snapped = moveBySnapTargets(e.fModifiers,
                                                   moveBy,
                                                   gridSettings,
                                                   includeSelectedBounds,
                                                   false,
                                                   false);
            if (snapped.first) { moveBy = snapped.second; }
        }

        moveSelectedPointsByAbs(moveBy, mStartTransform);
    }
}

void Canvas::scaleSelected(const eMouseEvent& e)
{
    const QPointF absPos = mRotPivot->getAbsolutePos();
    const QPointF distMoved = e.fPos - e.fLastPressPos;

    qreal scaleBy;
    if (mValueInput.inputEnabled()) { scaleBy = mValueInput.getValue(); }
    else {
        scaleBy = 1 + distSign({distMoved.x(), -distMoved.y()})*0.003;
    }

    qreal scaleX;
    qreal scaleY;
    if (mValueInput.xOnlyMode()) {
        scaleX = scaleBy;
        scaleY = 1;
    } else if (mValueInput.yOnlyMode()) {
        scaleX = 1;
        scaleY = scaleBy;
    } else {
        scaleX = scaleBy;
        scaleY = scaleBy;
    }

    if (mCurrentMode == CanvasMode::boxTransform) {
        scaleSelectedBy(scaleX,
                        scaleY,
                        absPos,
                        mStartTransform);
    } else {
        scaleSelectedPointsBy(scaleX,
                              scaleY,
                              absPos,
                              mStartTransform);
    }

    if (!mValueInput.inputEnabled()) {
        mValueInput.setDisplayedValue({scaleX, scaleY});
    }
    mRotPivot->setMousePos(e.fPos);
}

void Canvas::shearSelected(const eMouseEvent& e)
{
    const QPointF absPos = mRotPivot->getAbsolutePos();
    const QPointF distMoved = e.fPos - e.fLastPressPos;

    qreal shearBy;
    if (mValueInput.inputEnabled()) {
        shearBy = mValueInput.getValue();
    } else {
        qreal axisDelta;
        if (mValueInput.xOnlyMode()) { axisDelta = -distMoved.x(); }
        else { axisDelta = distMoved.y(); }
        shearBy = axisDelta * 0.01;
    }

    qreal shearX = 0;
    qreal shearY = 0;
    if (mValueInput.xOnlyMode()) { shearX = shearBy; }
    else if (mValueInput.yOnlyMode()) { shearY = shearBy; }
    else {
        shearX = shearBy;
        shearY = shearBy;
    }

    if (mCurrentMode == CanvasMode::boxTransform) {
        shearSelectedBy(shearX,
                        shearY,
                        absPos,
                        mStartTransform);
    } else {
        shearSelectedPointsBy(shearX,
                              shearY,
                              absPos,
                              mStartTransform);
    }

    if (!mValueInput.inputEnabled()) {
        mValueInput.setDisplayedValue({shearX, shearY});
    }
    mRotPivot->setMousePos(e.fPos);
}

void Canvas::rotateSelected(const eMouseEvent& e)
{
    const QPointF absPos = mRotPivot->getAbsolutePos();
    qreal rot;
    if (mValueInput.inputEnabled()) {
        rot = mValueInput.getValue();
    } else {
        const QLineF dest_line(absPos, e.fPos);
        const QLineF prev_line(absPos, e.fLastPressPos);
        qreal d_rot = dest_line.angleTo(prev_line);
        if (d_rot > 180) { d_rot -= 360; }
        if (mLastDRot - d_rot > 90) { mRotHalfCycles += 2; }
        else if (mLastDRot - d_rot < -90) { mRotHalfCycles -= 2; }
        mLastDRot = d_rot;
        rot = d_rot + mRotHalfCycles*180;
    }

    if (!mValueInput.inputEnabled()) {
        const auto grid = eSettings::instance().fGrid;
        if (e.ctrlMod() || e.shiftMod()) {
            const qreal step = e.ctrlMod() ? grid.stepRotCtrl : grid.stepRotShift;
            qDebug() << step;
            rot = qRound(rot / step) * step;
        }
    }

    if (mCurrentMode == CanvasMode::boxTransform) {
        rotateSelectedBy(rot, absPos, mStartTransform);
    } else {
        rotateSelectedPointsBy(rot, absPos, mStartTransform);
    }

    if (!mValueInput.inputEnabled()) {
        mValueInput.setDisplayedValue(rot);
    }
    mRotPivot->setMousePos(e.fPos);
}

bool Canvas::prepareRotation(const QPointF &startPos,
                             bool fromHandle)
{
    if (mCurrentMode != CanvasMode::boxTransform &&
        mCurrentMode != CanvasMode::pointTransform) { return false; }
    if (mSelectedBoxes.isEmpty()) { return false; }
    if (mCurrentMode == CanvasMode::pointTransform) {
        if (mSelectedPoints_d.isEmpty()) { return false; }
    }

    mGizmos.fState.rotatingFromHandle = fromHandle;
    mValueInput.clearAndDisableInput();
    mValueInput.setupRotate();

    if (fromHandle) { setGizmosSuppressed(true); }

    mRotPivot->setMousePos(startPos);
    mTransMode = TransformMode::rotate;
    mRotHalfCycles = 0;
    mLastDRot = 0;

    mDoubleClick = false;
    mStartTransform = true;
    return true;
}

void Canvas::handleMovePathMouseMove(const eMouseEvent& e)
{
    if (mRotPivot->isSelected()) {
        if (mStartTransform) { mRotPivot->startTransform(); }
        mRotPivot->moveByAbs(getMoveByValueForEvent(e));
    } else if (mTransMode == TransformMode::scale) {
        scaleSelected(e);
    } else if (mTransMode == TransformMode::shear) {
        shearSelected(e);
    } else if (mTransMode == TransformMode::rotate) {
        rotateSelected(e);
    } else {
        if (mPressedBox) {
            addBoxToSelection(mPressedBox);
            mPressedBox = nullptr;
        }

        const auto& gridSettings = mDocument.getGrid()->getSettings();

        if (mStartTransform && !mSelectedBoxes.isEmpty()) {
            collectAnchorOffsets(gridSettings);
        }

        auto moveBy = getMoveByValueForEvent(e);
        if(selectionNeedsCameraMapping()) {
            // the selection is displayed through the camera projection:
            // un-project both positions so the layer follows the cursor
            // instead of racing off when the camera is rotated/tilted
            moveBy = mapCameraScreenToWorld(e.fPos) -
                     mapCameraScreenToWorld(e.fLastPressPos);
        }
        if (gridSettings.snapEnabled && !mSelectedBoxes.isEmpty()) {
            const auto snapped = moveBySnapTargets(e.fModifiers,
                                                   moveBy,
                                                   gridSettings,
                                                   false,
                                                   true,
                                                   true);
            if (snapped.first) { moveBy = snapped.second; }
        }

        moveSelectedBoxesByAbs(moveBy, mStartTransform);
    }
}

void Canvas::updateTransformation(const eKeyEvent &e)
{
    if (mSelecting) {
        moveSecondSelectionPoint(e.fPos);
    } else if (mCurrentMode == CanvasMode::pointTransform) {
        handleMovePointMouseMove(e);
    } else if (mCurrentMode == CanvasMode::boxTransform) {
        if (!mPressedPoint) { handleMovePathMouseMove(e); }
        else { handleMovePointMouseMove(e); }
    } else if (mCurrentMode == CanvasMode::pathCreate) {
        handleAddSmartPointMouseMove(e);
    }
}
