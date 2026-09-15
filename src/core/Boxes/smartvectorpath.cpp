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

#include "smartvectorpath.h"
#include <QPainter>
#include "canvas.h"
#include <QDebug>
#include "undoredo.h"
#include "MovablePoints/pathpivot.h"
#include "pointhelpers.h"
#include "Animators/SmartPath/smartpathcollection.h"
#include "Animators/gradientpoints.h"
#include "Animators/transformanimator.h"
#include "MovablePoints/segment.h"
#include "Animators/SmartPath/smartpathanimator.h"
#include "PathEffects/patheffectcollection.h"
#include "Animators/qpointfanimator.h"
#include "svgexporter.h"
#include "Properties/boxtargetproperty.h"
#include "Animators/qrealanimator.h"
#include "include/core/SkPathMeasure.h"

SmartVectorPath::SmartVectorPath() :
    PathBox("Path", eBoxType::vectorPath) {
    // AE-style label color default: shape layers are blue
    setLabelColor(QColor(32, 100, 230));
    mPathAnimator = enve::make_shared<SmartPathCollection>();
    connect(mPathAnimator.get(), &Property::prp_currentFrameChanged,
            this, [this](const UpdateReason reason) {
        setPathsOutdated(reason);
    });
    ca_prependChild(mPathEffectsAnimators.data(), mPathAnimator);

    // path source link: holds the target for serialization + the
    // timeline picker; offset drives the road-frame outline
    mPathTarget = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("path source"));
    mPathTarget->setValidator<SmartVectorPath>();
    ca_prependChild(mPathEffectsAnimators.data(), mPathTarget);
    connect(mPathTarget.data(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox * const box) {
        const auto src = enve_cast<SmartVectorPath*>(box);
        if (src == mPathSource.data()) { return; }
        // picker/undo path: re-run the full link setup
        disconnectPathSource();
        mPathSource = src;
        connectPathSource();
        setPathsOutdated(UpdateReason::userChange);
    });
    mPathSourceOffset = enve::make_shared<QrealAnimator>(
                QStringLiteral("path offset"));
    mPathSourceOffset->setValueRange(0., 9999.);
    mPathSourceOffset->setCurrentBaseValue(0.);
    connect(mPathSourceOffset.get(), &Property::prp_currentFrameChanged,
            this, [this](const UpdateReason reason) {
        if (mPathSource) { setPathsOutdated(reason); }
    });
    ca_prependChild(mPathEffectsAnimators.data(), mPathSourceOffset);
}

void SmartVectorPath::disconnectPathSource() {
    for (const auto &conn : mPathSourceConns) { disconnect(conn); }
    mPathSourceConns.clear();
}

void SmartVectorPath::connectPathSource() {
    const auto src = mPathSource.data();
    if (!src) { return; }
    const auto anim = src->getPathAnimator();
    if (anim) {
        mPathSourceConns << connect(anim, &Property::prp_currentFrameChanged,
                this, [this](const UpdateReason reason) {
            setPathsOutdated(reason);
        });
    }
    mPathSourceConns << connect(src, &QObject::destroyed,
            this, [this]() {
        disconnectPathSource();
        mPathSource.clear();
        setPathsOutdated(UpdateReason::userChange);
    });
}

void SmartVectorPath::setPathSource(SmartVectorPath * const source) {
    // reject cycles: walk the chain up from the candidate source
    auto check = source;
    while (check) {
        if (check == this) { return; }
        check = check->getPathSource();
    }
    disconnectPathSource();
    mPathSource = source;
    // keep the serialized target in sync (targetSet handler sees the
    // pointer already matches and skips the re-entry)
    if (mPathTarget) { mPathTarget->setTargetAction(source); }
    connectPathSource();
    if (source) {
        // empty the local path (animator kept: removing it would
        // keep it alive through undo closures, leaving dead anchors;
        // an empty path shows no anchors and renders nothing local)
        const int nPathKids = mPathAnimator->ca_getNumberOfChildren();
        for (int i = 0; i < nPathKids; i++) {
            const auto anim = mPathAnimator->ca_getChildAt<SmartPathAnimator>(i);
            if (anim) { anim->setPathToEmpty(); }
        }
    }
    setPathsOutdated(UpdateReason::userChange);
}

// normal-offset closed outline along the path (road frame geometry,
// the CEP buildRoadRect expression ported to C++)
SkPath SmartVectorPath::outlineOffsetPath(const SkPath &src,
                                          const qreal offset) const {
    SkPathMeasure measure(src, false);
    SkPath result;
    do {
        const SkScalar len = measure.getLength();
        if (len < 0.001f) { continue; }
        const int samples = qBound(4, int(len / 8) + 2, 60);
        QVector<QPointF> top;
        QVector<QPointF> bottom;
        for (int i = 0; i < samples; i++) {
            const SkScalar d = len * SkScalar(i) / SkScalar(samples - 1);
            SkPoint pos;
            SkVector tan;
            if (!measure.getPosTan(d, &pos, &tan)) { continue; }
            const qreal nx = -qreal(tan.y());
            const qreal ny = qreal(tan.x());
            top << QPointF(qreal(pos.x()) + nx * offset,
                          qreal(pos.y()) + ny * offset);
            bottom << QPointF(qreal(pos.x()) - nx * offset,
                              qreal(pos.y()) - ny * offset);
        }
        if (top.count() < 2) { continue; }
        result.moveTo(toSkScalar(top.first().x()),
                      toSkScalar(top.first().y()));
        for (int i = 1; i < top.count(); i++) {
            result.lineTo(toSkScalar(top.at(i).x()),
                          toSkScalar(top.at(i).y()));
        }
        for (int i = bottom.count() - 1; i >= 0; i--) {
            result.lineTo(toSkScalar(bottom.at(i).x()),
                          toSkScalar(bottom.at(i).y()));
        }
        result.close();
    } while (measure.nextContour());
    return result;
}

bool SmartVectorPath::differenceInEditPathBetweenFrames(
        const int frame1, const int frame2) const {
    if (mPathSource) {
        const auto src = mPathSource.data();
        if (mPathSourceOffset->prp_differencesBetweenRelFrames(
                    frame1, frame2)) { return true; }
        return src->differenceInEditPathBetweenFrames(frame1, frame2);
    }
    return mPathAnimator->prp_differencesBetweenRelFrames(frame1, frame2);
}

bool SmartVectorPath::localDifferenceInPathBetweenFrames(
        const int frame1, const int frame2) const {
    if (mPathSource) {
        // linked layers share the parent group, so rel frames match
        if (mPathSource->differenceInPathBetweenFrames(frame1, frame2)) {
            return true;
        }
        return differenceInEditPathBetweenFrames(frame1, frame2);
    }
    return BoxWithPathEffects::localDifferenceInPathBetweenFrames(
                frame1, frame2);
}

SkBlendMode SmartVectorPath::getPaintBlendMode(const qreal relFrame) const {
    if(mMaskMode) {
        // a mask only clips its owner layer - creation always wraps a
        // host layer, but the timeline lets the user drag a mask to
        // the scene root, where a DstIn/DstOut blend would erase every
        // layer below it; disable clipping there
        const auto parent = getParentGroup();
        if(!parent || enve_cast<Canvas*>(parent)) {
            return SkBlendMode::kSrcOver;
        }
        // while any sub-path is open the mask shape draws normally
        // (SrcOver) instead of clipping the layers below with DstIn;
        // evaluate at the frame being rendered, never the global current
        // frame (stale inside scene links)
        if(!allSubPathsClosed(relFrame)) {
            return SkBlendMode::kSrcOver;
        }
    }
    return PathBox::getPaintBlendMode(relFrame);
}

bool SmartVectorPath::allSubPathsClosed(const qreal relFrame) const {
    const int n = mPathAnimator->ca_getNumberOfChildren();
    if(n == 0) return false;
    for(int i = 0; i < n; i++) {
        const auto asAnim = enve_cast<SmartPathAnimator*>(
                    mPathAnimator->getChild(i));
        if(!asAnim) continue;
        SmartPath sp;
        asAnim->deepCopyValue(relFrame, sp);
        if(!sp.isClosed()) return false;
    }
    return true;
}

void SmartVectorPath::setupRenderData(const qreal relFrame,
                                      const QTransform& parentM,
                                      BoxRenderData * const data,
                                      Canvas* const scene) {
    PathBox::setupRenderData(relFrame, parentM, data, scene);
    // mask pen: an open shape must not show its fill - Skia would fill
    // the implicitly closed outline and paint it over the canvas while
    // the user is still drawing
    if(mMaskMode && !allSubPathsClosed(relFrame)) {
        static_cast<PathBoxRenderData*>(data)->fFillPath.reset();
    }
}

void SmartVectorPath::saveSVG(SvgExporter& exp, DomEleTask* const task) const {
    const bool baseEffects = hasBasePathEffects();
    const bool outlineBaseEffects = hasOutlineBaseEffects();
    const bool outlineEffects = hasOutlineEffects();
    const bool fillEffects = hasFillEffects();
    const bool splitFillStroke = fillEffects ||
                                 outlineBaseEffects ||
                                 outlineEffects;
    if(splitFillStroke) {
        auto& ele = task->initialize("g");

        auto fill = exp.createElement("path");
        SmartPathCollection::EffectApplier fillApplier;
        if(baseEffects || fillEffects) {
            fillApplier = [this](const int relFrame, SkPath& path) {
                applyBasePathEffects(relFrame, path);
                applyFillEffects(relFrame, path);
            };
        };
        QList<Animator*> fillExtInfl;
        if(baseEffects) fillExtInfl << mPathEffectsAnimators.get();
        if(fillEffects) fillExtInfl << mFillPathEffectsAnimators.get();
        mPathAnimator->savePathsSVG(exp, fill, fillApplier,
                                    baseEffects || fillEffects,
                                    task->visRange(), fillExtInfl);
        saveFillSettingsSVG(exp, fill, task->visRange());
        fill.setAttribute("stroke", "none");
        ele.appendChild(fill);
        switch(mPathAnimator->getFillType()) {
        case SkPathFillType::kEvenOdd:
            fill.setAttribute("fill-rule", "evenodd");
            break;
        default:
            fill.setAttribute("fill-rule", "nonzero");
        }

        auto stroke = exp.createElement("path");
        SmartPathCollection::EffectApplier strokeApplier;
        if(baseEffects || outlineBaseEffects || outlineEffects) {
            strokeApplier = [this, outlineEffects](const int relFrame, SkPath& path) {
                applyBasePathEffects(relFrame, path);
                applyOutlineBaseEffects(relFrame, path);
                if(!outlineEffects) return;
                const auto strokeSettings = getStrokeSettings();
                SkStroke stroker;
                strokeSettings->setStrokerSettingsForRelFrameSk(relFrame, &stroker);
                stroker.strokePath(path, &path);
                applyOutlineEffects(relFrame, path);
            };
        };
        QList<Animator*> strokeExtInfl;
        if(baseEffects) fillExtInfl << mPathEffectsAnimators.get();
        if(outlineBaseEffects) fillExtInfl << mOutlineBasePathEffectsAnimators.get();
        if(outlineEffects) fillExtInfl << mOutlinePathEffectsAnimators.get();
        const bool forceDumb = baseEffects || outlineBaseEffects || outlineEffects;
        mPathAnimator->savePathsSVG(exp, stroke, strokeApplier,
                                    forceDumb, task->visRange(),
                                    strokeExtInfl);
        saveStrokeSettingsSVG(exp, stroke, task->visRange(), outlineEffects);
        stroke.setAttribute(outlineEffects ? "stroke" : "fill", "none");
        if(outlineEffects) stroke.setAttribute("fill-rule", "nonzero");

        ele.appendChild(stroke);
    } else {
        auto& ele = task->initialize("path");
        SmartPathCollection::EffectApplier applier;
        if(baseEffects) {
            applier = [this](const int relFrame, SkPath& path) {
                applyBasePathEffects(relFrame, path);
            };
        };
        QList<Animator*> extInfl;
        if(baseEffects) extInfl << mPathEffectsAnimators.get();
        mPathAnimator->savePathsSVG(exp, ele, applier, baseEffects,
                                    task->visRange(), extInfl);
        savePathBoxSVG(exp, ele, task->visRange());
    }
}

void SmartVectorPath::loadSkPath(const SkPath &path) {
    mPathAnimator->loadSkPath(path);
}

SmartPathCollection *SmartVectorPath::getPathAnimator() {
    return mPathAnimator.data();
}

#include "typemenu.h"
#include "ReadWrite/evformat.h"
void SmartVectorPath::setupCanvasMenu(PropertyMenu * const menu)
{
    if (menu->hasActionsForType<SmartVectorPath>()) { return; }
    menu->addedActionsForType<SmartVectorPath>();
    PathBox::setupCanvasMenu(menu);
    PropertyMenu::PlainSelectedOp<SmartVectorPath> op = [](SmartVectorPath * box) {
        box->applyCurrentTransform();
    };
    menu->addSeparator();
    menu->addPlainAction(QIcon::fromTheme("loop2"), tr("Apply Transformation"), op);
}

void SmartVectorPath::writeBoundingBox(eWriteStream& dst) const
{
    PathBox::writeBoundingBox(dst);
    dst << mMaskMode;
}

void SmartVectorPath::readBoundingBox(eReadStream& src)
{
    PathBox::readBoundingBox(src);
    if (src.evFileVersion() >= EvFormat::maskPathMode) {
        src >> mMaskMode;
    }
}

void SmartVectorPath::applyCurrentTransform()
{
    prp_pushUndoRedoName(tr("Apply Transform"));
    mNReasonsNotToApplyUglyTransform++;
    const auto transform = mTransformAnimator->getRotScaleShearTransform();
    mPathAnimator->applyTransform(transform);
    getFillSettings()->applyTransform(transform);
    getStrokeSettings()->applyTransform(transform);
    mTransformAnimator->startRotScaleShearTransform();
    mTransformAnimator->resetRotScaleShear();
    mTransformAnimator->prp_finishTransform();
    mNReasonsNotToApplyUglyTransform--;
}

SkPath SmartVectorPath::getRelativePath(const qreal relFrame) const {
    if (mPathSource) {
        // linked geometry: the source's local coordinates are used
        // as-is (linked layers share the parent group/transform by
        // convention of the map-line generator)
        const auto srcPath = mPathSource->getRelativePath(relFrame);
        const qreal offset = mPathSourceOffset ?
                    mPathSourceOffset->getEffectiveValue(relFrame) : 0.;
        if (offset > 0.01) { return outlineOffsetPath(srcPath, offset); }
        return srcPath;
    }
    return mPathAnimator->getPathAtRelFrame(relFrame);
}

void SmartVectorPath::getMotionBlurProperties(QList<Property*> &list) const {
    PathBox::getMotionBlurProperties(list);
    list.append(mPathAnimator.get());
}

QList<qsptr<SmartVectorPath>> SmartVectorPath::breakPathsApart_k() {
    QList<qsptr<SmartVectorPath>> result;
    const int iMax = mPathAnimator->ca_getNumberOfChildren() - 1;
    if(iMax < 1) return result;
    for(int i = iMax; i >= 0; i--) {
        const auto srcPath = mPathAnimator->takeChildAt(i);
        const auto newPath = enve::make_shared<SmartVectorPath>();
        copyPathBoxDataTo(newPath.get());
        newPath->getPathAnimator()->addChild(srcPath);
        result.append(newPath);
    }
    removeFromParent_k();
    return result;
}
