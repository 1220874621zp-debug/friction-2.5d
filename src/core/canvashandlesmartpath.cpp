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
#include "MovablePoints/smartnodepoint.h"
#include "Boxes/smartvectorpath.h"
#include "Boxes/containerbox.h"
#include "RasterEffects/blureffect.h"
#include "eevent.h"
#include "Private/document.h"

void Canvas::clearCurrentSmartEndPoint() {
    setCurrentSmartEndPoint(nullptr);
}

void Canvas::setCurrentSmartEndPoint(SmartNodePoint * const point) {
    if(mLastEndPoint) mLastEndPoint->setSelected(false);
    if(point) point->setSelected(true);
    mLastEndPoint = point;
}
#include "MovablePoints/pathpointshandler.h"
#include "Animators/SmartPath/smartpathcollection.h"
#include "Animators/transformanimator.h"

namespace {
bool isMaskPathBox(BoundingBox * const box) {
    const auto path = enve_cast<SmartVectorPath*>(box);
    return path && path->getMaskMode();
}

bool isBitmapBox(BoundingBox * const box) {
    // raster images (+PSD) and frame-based media (video/sequences)
    return enve_cast<ImageBox*>(box) ||
           enve_cast<AnimationBox*>(box);
}

bool hostsMaskPaths(ContainerBox * const group) {
    for(const auto& child : group->getContainedBoxes()) {
        if(isMaskPathBox(child)) return true;
    }
    return false;
}

// nearest layer-type ancestor: masks placed there clip that whole
// layer (AE semantics), unwrapped boxes get wrapped on first use.
// the Canvas itself also reports isLayer() (it promotes for render
// purposes) but must NEVER host masks - a mask at scene root erases
// every layer below it
ContainerBox *layerAncestorOrSelf(BoundingBox * const box) {
    auto parent = box->getParentGroup();
    while(parent) {
        if(parent->isLayer() && !enve_cast<Canvas*>(parent)) {
            return parent;
        }
        parent = parent->getParentGroup();
    }
    return nullptr;
}
}

// bitmap auto-detect rule: on a bitmap layer (or over an existing
// mask / mask-hosting group) the pen and rectangle tools draw MASKS
// instead of shapes; everything else keeps drawing shapes
bool Canvas::isMaskIntentTarget(BoundingBox * const target) {
    if(!target) return false;
    if(isBitmapBox(target)) return true;
    if(const auto group = enve_cast<ContainerBox*>(target)) {
        if(group->isLayer() && !enve_cast<Canvas*>(group)) {
            return hostsMaskPaths(group);
        }
    }
    return false;
}

BoundingBox *Canvas::resolveMaskTarget(const eMouseEvent &e) {
    // pure resolution, no tree mutation: the layer under the press
    // point; with no hit, a single selected layer (AE semantics -
    // drawing commonly starts outside the layer bounds)
    BoundingBox *target = getBoxAtFromAllDescendents(e.fPos);
    if(isMaskPathBox(target)) {
        // drawing over an existing mask stacks another mask onto the
        // same layer (multi-mask Add/Subtract composition)
        return target->getParentGroup();
    }
    if(!target) {
        const auto selList = getSelectedBoxesList();
        if(selList.count() == 1) {
            const auto sel = selList.first();
            if(isMaskPathBox(sel)) return sel->getParentGroup();
            const auto selGroup = enve_cast<ContainerBox*>(sel);
            if(selGroup && sel->isLayer() &&
                    !enve_cast<Canvas*>(selGroup)) {
                return selGroup;
            }
            target = sel;
        }
    }
    return target;
}

ContainerBox *Canvas::ensureMaskHost(BoundingBox * const target) {
    if(!target) {
        qWarning() << "蒙版：起笔点未命中图层，且没有单一选中图层，请在要裁剪的图层上起笔（或先选中它）";
        return nullptr;
    }
    // a selected layer-type group hosts the mask itself
    if(const auto group = enve_cast<ContainerBox*>(target)) {
        if(group->isLayer() && !enve_cast<Canvas*>(group)) {
            return group;
        }
    }
    if(const auto host = layerAncestorOrSelf(target)) return host;
    // mask: wrap the target layer into its own layer box and add
    // the mask right above it, so the mask clips ONLY that layer
    // (Friction masks affect their container)
    const auto parentGroup = target->getParentGroup();
    if(!parentGroup) {
        qWarning() << "蒙版：目标图层不在任何容器内，已取消绘制";
        return nullptr;
    }
    const auto& contained = parentGroup->getContained();
    const int tId = contained.indexOf(target->ref<eBoxOrSound>());
    if(tId < 0) {
        qWarning() << "蒙版：目标图层不在任何容器内，已取消绘制";
        return nullptr;
    }
    const auto group = enve::make_shared<ContainerBox>(eBoxType::layer);
    group->setRevealRowsOnce();
    group->prp_setName(target->prp_getName());
    parentGroup->insertContained(tId, group);
    group->addContained(target->ref<eBoxOrSound>());
    return group.get();
}

ContainerBox *Canvas::resolveMaskHost(const eMouseEvent &e) {
    return ensureMaskHost(resolveMaskTarget(e));
}

qsptr<SmartVectorPath> Canvas::createMaskPath(
        BoundingBox * const nameSource) {
    const auto newPath = enve::make_shared<SmartVectorPath>();
    newPath->planCenterPivotPosition();
    // Add mode by default (kDstIn); switch to Subtract (kDstOut) from
    // the mask row context menu - multiple masks on one layer combine
    // Add = union / Subtract = erase, applied topmost-first (AE)
    newPath->setBlendModeSk(SkBlendMode::kDstIn);
    newPath->setMaskMode(true);
    newPath->prp_setName(QStringLiteral("蒙版：%1").arg(
                nameSource ? nameSource->prp_getName() :
                             QStringLiteral("图层")));
    newPath->getFillSettings()->setPaintType(PaintType::FLATPAINT);
    newPath->getStrokeSettings()->setPaintType(PaintType::NOPAINT);
    // feather slot: hard edge by default, animate its radius to feather
    const auto blur = enve::make_shared<BlurEffect>();
    blur->setRadius(0);
    newPath->addRasterEffect(blur);
    return newPath;
}

void Canvas::handleAddSmartPointMousePress(const eMouseEvent &e) {
    if(mLastEndPoint ? mLastEndPoint->isHidden(mCurrentMode) : false) {
        clearCurrentSmartEndPoint();
    }
    qptr<BoundingBox> test;

    auto nodePointUnderMouse = static_cast<SmartNodePoint*>(mPressedPoint.data());
    if(nodePointUnderMouse ? !nodePointUnderMouse->isEndPoint() : false) {
        nodePointUnderMouse = nullptr;
    }
    if(nodePointUnderMouse == mLastEndPoint &&
            nodePointUnderMouse) return;
    if(!mLastEndPoint && !nodePointUnderMouse) {
        // mask session: forced via the mask pen button, or
        // auto-detected - on a bitmap layer (or over an existing
        // mask / mask-hosting group) the plain pen draws a mask too;
        // an unresolved forced target must never fall through to a
        // raw DstIn path in the current container - once closed it
        // would erase every layer below it on the whole canvas
        BoundingBox *maskTarget = nullptr;
        if(mDocument.fMaskPenActive) {
            maskTarget = resolveMaskTarget(e);
        } else {
            const auto autoTarget = resolveMaskTarget(e);
            if(isMaskIntentTarget(autoTarget)) maskTarget = autoTarget;
        }
        if(maskTarget) {
            const auto host = ensureMaskHost(maskTarget);
            if(!host) return;
            const auto maskPath = createMaskPath(host);
            host->setRevealRowsOnce();
            host->addContained(maskPath->ref<eBoxOrSound>());
            clearBoxesSelection();
            addBoxToSelection(maskPath.get());
            const QPointF snappedPos = snapEventPos(e, false);
            const auto relPos = maskPath->mapAbsPosToRel(snappedPos);
            maskPath->getBoxTransformAnimator()->setPosition(relPos.x(), relPos.y());
            const auto newHandler = maskPath->getPathAnimator();
            const auto node = newHandler->createNewSubPathAtRelPos({0, 0});
            setCurrentSmartEndPoint(node);
            return;
        }
        const auto newPath = enve::make_shared<SmartVectorPath>();
        newPath->planCenterPivotPosition();
        if(!newPath->getParentGroup()) {
            mCurrentContainer->addContained(newPath);
        }
        clearBoxesSelection();
        addBoxToSelection(newPath.get());
        const QPointF snappedPos = snapEventPos(e, false);
        const auto relPos = newPath->mapAbsPosToRel(snappedPos);
        newPath->getBoxTransformAnimator()->setPosition(relPos.x(), relPos.y());
        const auto newHandler = newPath->getPathAnimator();
        const auto node = newHandler->createNewSubPathAtRelPos({0, 0});
        setCurrentSmartEndPoint(node);
    } else {
        if(!nodePointUnderMouse) {
            const QPointF snappedPos = snapEventPos(e, false);
            const auto newPoint = mLastEndPoint->actionAddPointAbsPos(snappedPos);
            //newPoint->startTransform();
            setCurrentSmartEndPoint(newPoint);
        } else if(!mLastEndPoint) {
            setCurrentSmartEndPoint(nodePointUnderMouse);
        } else { // mCurrentSmartEndPoint
            const auto targetNode = nodePointUnderMouse->getTargetNode();
            const auto handler = nodePointUnderMouse->getHandler();
            const bool success = nodePointUnderMouse->isEndPoint() &&
                    mLastEndPoint->actionConnectToNormalPoint(
                        nodePointUnderMouse);
            if(success) {
                const int newTargetId = targetNode->getNodeId();
                const auto sel = handler->getPointWithId<SmartNodePoint>(newTargetId);
                setCurrentSmartEndPoint(sel);
            }
        }
    } // pats is not null
}


void Canvas::handleAddSmartPointMouseMove(const eMouseEvent &e) {
    if(!mLastEndPoint) return;
    if(mStartTransform) mLastEndPoint->startTransform();
    const QPointF snappedPos = snapEventPos(e, false);
    if(mLastEndPoint->hasNextNormalPoint() &&
       mLastEndPoint->hasPrevNormalPoint()) {
        mLastEndPoint->setCtrlsMode(CtrlsMode::corner);
        mLastEndPoint->setC0Enabled(true);
        mLastEndPoint->moveC0ToAbsPos(snappedPos);
    } else {
        if(!mLastEndPoint->hasNextNormalPoint() &&
           !mLastEndPoint->hasPrevNormalPoint()) {            
            mLastEndPoint->setCtrlsMode(CtrlsMode::corner);
            mLastEndPoint->setC2Enabled(true);
        } else {
            mLastEndPoint->setCtrlsMode(CtrlsMode::symmetric);
        }
        if(mLastEndPoint->hasNextNormalPoint()) {
            mLastEndPoint->moveC0ToAbsPos(snappedPos);
        } else {
            mLastEndPoint->moveC2ToAbsPos(snappedPos);
        }
    }
}

void Canvas::handleAddSmartPointMouseRelease(const eMouseEvent &e) {
    Q_UNUSED(e)
    if(mLastEndPoint) {
        if(!mStartTransform) mLastEndPoint->finishTransform();
        //mCurrentSmartEndPoint->prp_afterWholeInfluenceRangeChanged();
        if(!mLastEndPoint->isEndPoint())
            clearCurrentSmartEndPoint();
    }
}
