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

#include "eboxorsound.h"

#include "canvas.h"
#include "Boxes/containerbox.h"
#include "Timeline/durationrectangle.h"
#include "Properties/emimedata.h"
#include "Sound/esound.h"
#include "Sound/soundcomposition.h"
#include "ReadWrite/evformat.h"
#include <QPointer>
#include "Boxes/bone.h"

eBoxOrSound::eBoxOrSound(const QString &name) :
    StaticComplexAnimator(name) {
    ca_setDisabledWhenEmpty(false);
    // keyframable "可见" row: an extra visibility gate AND-ed with the
    // row eye; base value true = legacy behavior until keyed
    mVisibleAnim = enve::make_shared<BoolAnimator>(
                QStringLiteral("\u53EF\u89C1"));
    mVisibleAnim->setCurrentBoolValue(true);
    ca_addChild(mVisibleAnim);
    connect(this, &Property::prp_nameChanged, this,
            &SingleWidgetTarget::SWT_scheduleSearchContentUpdate);
}

void eBoxOrSound::setParentGroup(ContainerBox * const parent) {
    if(parent == mParentGroup) return;
    // capture before reassignment: needed for the track enforce below
    // (after a removal the parent chain no longer resolves the scene)
    const QPointer<Canvas> scene = getParentScene();
    const QPointer<ContainerBox> oldParent = mParentGroup.data();
    emit aboutToChangeAncestor();

    prp_afterWholeInfluenceRangeChanged();
    auto& conn = mParentGroup.assign(parent);
    if(mParentGroup) {
        anim_setAbsFrame(mParentGroup->anim_getCurrentAbsFrame());
        conn << connect(mParentGroup, &eBoxOrSound::aboutToChangeAncestor,
                        this, &eBoxOrSound::aboutToChangeAncestor);
    }

    setParent(mParentGroup);
    emit parentChanged(parent);

    // track maintenance: re-resolve the active member / hidden rows for
    // both the old and the new sibling group (queued: we may be inside
    // an insert/remove that must finish before rows are rebuilt)
    if(mTrackId >= 0 && scene) {
        const QPointer<ContainerBox> newParent = mParentGroup.data();
        const int tid = mTrackId;
        QMetaObject::invokeMethod(this, [scene, oldParent, newParent, tid]() {
            if(!scene) return;
            if(oldParent) scene->enforceTrack(oldParent, tid);
            if(newParent && newParent != oldParent) {
                scene->enforceTrack(newParent, tid);
            }
        }, Qt::QueuedConnection);
    }
}

void eBoxOrSound::removeFromParent_k() {
    if(!mParentGroup) return;
    mParentGroup->removeContained_k(ref<eBoxOrSound>());
}

bool eBoxOrSound::isAncestor(const BoundingBox * const box) const {
    if(!mParentGroup) return false;
    if(mParentGroup == box) return true;
    if(enve_cast<const ContainerBox*>(box))
        return mParentGroup->isAncestor(box);
    return false;
}

bool eBoxOrSound::isFrameInDurationRect(const int relFrame) const {
    if(!mDurationRectangle) return true;
    return relFrame <= mDurationRectangle->getMaxRelFrame() &&
            relFrame >= mDurationRectangle->getMinRelFrame();
}

bool eBoxOrSound::isFrameFInDurationRect(const qreal relFrame) const {
    if(!mDurationRectangle) return true;
    return qCeil(relFrame) <= mDurationRectangle->getMaxRelFrame() &&
           qFloor(relFrame) >= mDurationRectangle->getMinRelFrame();
}

void eBoxOrSound::shiftAll(const int shift) {
    if(hasDurationRectangle()) mDurationRectangle->changeFramePosBy(shift);
    else anim_shiftAllKeys(shift);
}

QMimeData *eBoxOrSound::SWT_createMimeData() {
    return new eMimeData(QList<eBoxOrSound*>() << this);
}

FrameRange eBoxOrSound::prp_relInfluenceRange() const {
    if(mDurationRectangle) return mDurationRectangle->getRelFrameRange();
    return ComplexAnimator::prp_relInfluenceRange();
}

FrameRange eBoxOrSound::prp_getIdenticalRelRange(const int relFrame) const {
    if(mVisible) {
        const auto cRange = ComplexAnimator::prp_getIdenticalRelRange(relFrame);
        if(mDurationRectangle) {
            const auto dRange = mDurationRectangle->getRelFrameRange();
            if(relFrame > dRange.fMax) {
                return mDurationRectangle->getRelFrameRangeToTheRight();
            } else if(relFrame < dRange.fMin) {
                return mDurationRectangle->getRelFrameRangeToTheLeft();
            } else return cRange*dRange;
        }
        return cRange;
    }
    return {FrameRange::EMIN, FrameRange::EMAX};
}

int eBoxOrSound::prp_getRelFrameShift() const {
    if(!mDurationRectangle) return 0;
    return mDurationRectangle->getRelShift();
}

void eBoxOrSound::prp_afterChangedAbsRange(const FrameRange &range,
                                           const bool clip) {
    const auto croppedRange = clip ? prp_absInfluenceRange()*range : range;
    StaticComplexAnimator::prp_afterChangedAbsRange(croppedRange);
}

void eBoxOrSound::prp_writeProperty_impl(eWriteStream& dst) const {
    StaticComplexAnimator::prp_writeProperty_impl(dst);
    dst << mVisible;
    dst << mLocked;
    dst << mSolo;
    dst << mShy;
    dst << mLabelColor;
    dst << mTrackId;
    dst << mTrackId;
    // unconditional write, gated read (files written today are v39+)
    dst << mMbEnabled;

    const bool hasDurRect = mDurationRectangle;
    dst << hasDurRect;
    if(hasDurRect) mDurationRectangle->writeDurationRectangle(dst);

    dst << prp_getName();
}

void eBoxOrSound::prp_readProperty_impl(eReadStream& src) {
    StaticComplexAnimator::prp_readProperty_impl(src);
    src >> mVisible;
    src >> mLocked;
    if(src.evFileVersion() >= EvFormat::boxLayerSwitches) {
        src >> mSolo;
        src >> mShy;
        if(mShy) updateRowVisibility();
    }
    if(src.evFileVersion() >= EvFormat::layerLabelColor) {
        src >> mLabelColor;
    }
    if(src.evFileVersion() >= EvFormat::trackIds) {
        src >> mTrackId;
    }
    if(src.evFileVersion() >= EvFormat::trackRows) {
        src >> mTrackId;
        if(mTrackId >= 0) scheduleTrackEnforce();
    }
    if(src.evFileVersion() >= EvFormat::layerFxColumns) {
        src >> mMbEnabled;
    }

    bool hasDurRect;
    src >> hasDurRect;
    if(hasDurRect) {
        if(!mDurationRectangle) createDurationRectangle();
        mDurationRectangle->readDurationRectangle(src);
    }
    if(src.evFileVersion() >= 10) {
        QString name; src >> name;
        prp_setName(name);
    }
}

void eBoxOrSound::writeBoxOrSoundXEV(const std::shared_ptr<XevZipFileSaver>& xevFileSaver,
                                     const RuntimeIdToWriteId& objListIdConv,
                                     const QString& path) const {
    QDomDocument doc;
    const auto exp = enve::make_shared<XevExporter>(
                         doc, xevFileSaver, objListIdConv, path);
    auto obj = prp_writeNamedPropertyXEV("Object", *exp);
    if(mDurationRectangle) mDurationRectangle->writeDurationRectangleXEV(obj);
    if(mTrackId >= 0) obj.setAttribute("trackId", mTrackId);

    doc.appendChild(obj);
    auto& fileSaver = xevFileSaver->fileSaver();
    fileSaver.processText(path + "properties.xml",
                          [&](QTextStream& stream) {
        stream << doc.toString();
    });
}

void eBoxOrSound::readBoxOrSoundXEV(XevReadBoxesHandler& boxReadHandler,
                                    ZipFileLoader& fileLoader, const QString& path,
                                    const RuntimeIdToWriteId& objListIdConv) {
    QDomDocument doc;
    fileLoader.process(path + "properties.xml",
                       [&](QIODevice* const src) {
        doc.setContent(src);
    });
    const auto obj = doc.firstChildElement("Object");
    const bool hasDurRect = obj.hasAttribute("visRange");
    if(hasDurRect) {
        if(!mDurationRectangle) createDurationRectangle();
        mDurationRectangle->readDurationRectangleXEV(obj);
    }
    if(obj.hasAttribute("trackId")) {
        mTrackId = obj.attribute("trackId", "-1").toInt();
        if(mTrackId >= 0) scheduleTrackEnforce();
    }
    const XevImporter imp(boxReadHandler, fileLoader, objListIdConv, path);
    prp_readPropertyXEV(obj, imp);
}

TimelineMovable *eBoxOrSound::anim_getTimelineMovable(
        const int relX, const int minViewedFrame,
        const qreal pixelsPerFrame) {
    if(!mDurationRectangle) return nullptr;
    return mDurationRectangle->getMovableAt(relX, pixelsPerFrame,
                                            minViewedFrame);
}

void eBoxOrSound::drawDurationRectangle(
        QPainter * const p, const qreal pixelsPerFrame,
        const FrameRange &absFrameRange, const int rowHeight) const {
    if(mDurationRectangle) {
        p->save();
        const int width = qCeil(absFrameRange.span()*pixelsPerFrame);
        const QRect drawRect(0, 0, width, rowHeight);
        const auto pScene = getParentScene();
        const qreal fps = pScene ? pScene->getFps() : 1;
        mDurationRectangle->draw(p, drawRect, fps,
                                 pixelsPerFrame, absFrameRange);
        p->restore();
    }
}

void eBoxOrSound::prp_drawTimelineControls(
        QPainter * const p, const qreal pixelsPerFrame,
        const FrameRange &absFrameRange, const int rowHeight) {
    // a track row shows the clips of all its members; the inactive
    // members are drawn dimmed underneath the active member's controls
    if(mTrackId >= 0) {
        drawTrackClips(p, pixelsPerFrame, absFrameRange, rowHeight, this);
    }
    drawDurationRectangle(p, pixelsPerFrame, absFrameRange, rowHeight);
    ComplexAnimator::prp_drawTimelineControls(
                p, pixelsPerFrame, absFrameRange, rowHeight);
}

void eBoxOrSound::setDurationRectangle(
        const qsptr<DurationRectangle>& durationRect,
        const bool lock) {
    Q_ASSERT(!mDurationRectangleLocked);
    if(mDurationRectangle == durationRect) return;
    if(mDurationRectangleLocked) return;
    if(lock) mDurationRectangleLocked = true;
    const FrameRange oldRange = mDurationRectangle ?
                mDurationRectangle->getAbsFrameRange() :
                FrameRange{FrameRange::EMIN, FrameRange::EMAX};
    const FrameRange newRange = durationRect ?
                durationRect->getAbsFrameRange() :
                FrameRange{FrameRange::EMIN, FrameRange::EMAX};
    const auto oldDurRect = mDurationRectangle.sptr();
    auto& conn = mDurationRectangle.assign(durationRect);
    prp_afterFrameShiftChanged(oldRange, newRange);

    {
        UndoRedo ur;
        ur.fUndo = [this, oldDurRect]() {
            setDurationRectangle(oldDurRect);
        };
        ur.fRedo = [this, durationRect]() {
            setDurationRectangle(durationRect);
        };
        prp_addUndoRedo(ur);
    }

    if(!durationRect) return anim_shiftAllKeys(oldDurRect->getRelShift());
    if(durationRect->getRelShift() != 0)
        anim_shiftAllKeys(-durationRect->getRelShift());

    conn << connect(durationRect.data(), &DurationRectangle::shiftChanged,
            this, [this](const int oldShift, const int newShift) {
        const auto newRange = prp_absInfluenceRange();
        const auto oldRange = newRange.shifted(oldShift - newShift);
        prp_afterFrameShiftChanged(oldRange, newRange);
    });

    conn << connect(durationRect.data(), &DurationRectangle::minRelFrameChanged,
            this, [this](const int oldMin, const int newMin) {
        const int min = qMin(newMin, oldMin);
        const int max = qMax(newMin, oldMin);
        prp_afterChangedRelRange(FrameRange{min, max}.adjusted(-1, 1), false);
    });
    conn << connect(durationRect.data(), &DurationRectangle::maxRelFrameChanged,
            this, [this](const int oldMax, const int newMax) {
        const int min = qMin(newMax, oldMax);
        const int max = qMax(newMax, oldMax);
        prp_afterChangedRelRange(FrameRange{min, max}.adjusted(-1, 1), false);
    });
}

bool eBoxOrSound::durationRectangleLocked() const {
    return mDurationRectangleLocked;
}

bool eBoxOrSound::isVisibleAndInVisibleDurationRect() const {
    return isFrameInDurationRect(anim_getCurrentRelFrame()) &&
            mVisible && mVisibleAnim->getBoolValue();
}

bool eBoxOrSound::isVisibleAndInDurationRect(
        const int relFrame) const {
    return isFrameInDurationRect(relFrame) && mVisible &&
            mVisibleAnim->getEffectiveIntValue(relFrame) == 1;
}

bool eBoxOrSound::isFrameFVisibleAndInDurationRect(
        const qreal relFrame) const {
    return isFrameFInDurationRect(relFrame) && mVisible &&
            mVisibleAnim->getEffectiveIntValue(relFrame) == 1;
}

bool eBoxOrSound::hasDurationRectangle() const {
    return mDurationRectangle;
}

void eBoxOrSound::startDurationRectPosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->startPosTransform();
    }
}

void eBoxOrSound::finishDurationRectPosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->finishPosTransform();
    }
}

void eBoxOrSound::cancelDurationRectPosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->cancelPosTransform();
    }
}

void eBoxOrSound::moveDurationRect(const int dFrame) {
    if(hasDurationRectangle()) {
        mDurationRectangle->changeFramePosBy(dFrame);
    }
}

void eBoxOrSound::startMinFramePosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->startMinFramePosTransform();
    }
}

void eBoxOrSound::finishMinFramePosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->finishMinFramePosTransform();
    }
}

void eBoxOrSound::cancelMinFramePosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->cancelMinFramePosTransform();
    }
}

void eBoxOrSound::moveMinFrame(const int dFrame) {
    if(hasDurationRectangle()) {
        mDurationRectangle->moveMinFrame(dFrame);
    }
}

void eBoxOrSound::startMaxFramePosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->startMaxFramePosTransform();
    }
}

void eBoxOrSound::finishMaxFramePosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->finishMaxFramePosTransform();
    }
}

void eBoxOrSound::cancelMaxFramePosTransform() {
    if(hasDurationRectangle()) {
        mDurationRectangle->cancelMaxFramePosTransform();
    }
}

void eBoxOrSound::moveMaxFrame(const int dFrame) {
    if(hasDurationRectangle()) {
        mDurationRectangle->moveMaxFrame(dFrame);
    }
}

DurationRectangle *eBoxOrSound::getDurationRectangle() const {
    return mDurationRectangle.get();
}

void eBoxOrSound::createDurationRectangle() {
    const auto durRect = enve::make_shared<DurationRectangle>(*this);
//    durRect->setMinFrame(0);
//    if(mParentScene) durRect->setFramesDuration(mParentScene->getFrameCount());
    durRect->setMinRelFrame(anim_getCurrentRelFrame() - 5);
    durRect->setFramesDuration(10);
    setDurationRectangle(durRect);
}

void eBoxOrSound::setSelected(const bool select) {
    if(mSelected == select) return;
    mSelected = select;
    SWT_scheduleContentUpdate(SWT_BoxRule::selected);
    // a track row belongs to its selected member; re-resolve after the
    // selection settles (queued: selection changes arrive in batches)
    if(mTrackId >= 0) scheduleTrackEnforce();
    emit selectionChanged(select);
}

void eBoxOrSound::select() {
    setSelected(true);
}

void eBoxOrSound::deselect() {
    setSelected(false);
}

void eBoxOrSound::selectionChangeTriggered(const bool shiftPressed) {
    const auto pScene = getParentScene();
    const auto bb = enve_cast<BoundingBox*>(this);
    if(!bb) {
        // sounds cannot enter the canvas box selection; they toggle their
        // own row selection state instead (click again to deselect)
        if(!shiftPressed && pScene) {
            pScene->clearBoxesSelection();
            const auto comp = pScene->getSoundComposition();
            if(comp) {
                for(const auto& sound : comp->getSounds()) {
                    const auto sPtr = static_cast<eBoxOrSound*>(sound.data());
                    if(sPtr != this && sPtr->isSelected()) {
                        sPtr->setSelected(false);
                    }
                }
            }
        }
        setSelected(!mSelected);
        // the row widget clears its highlight on release before this
        // runs; re-assert so it repaints even without a state change
        emit selectionChanged(mSelected);
        return;
    }
    if(!pScene) return;
    if(shiftPressed) {
        if(mSelected) {
            pScene->removeBoxFromSelection(bb);
        } else {
            pScene->addBoxToSelection(bb);
        }
    } else {
        pScene->clearBoxesSelection();
        pScene->addBoxToSelection(bb);
    }
}

void eBoxOrSound::setVisible(const bool visible)
{
    if (mVisible == visible) { return; }
    if(const auto bone = enve_cast<Bone*>(this)) {
        Bone::diag(QStringLiteral("visibility %1 -> %2")
                   .arg(prp_getName()).arg(visible));
    }
    if (!isLink()) {
        if (enve_cast<eSound*>(this)) {
            prp_pushUndoRedoName(visible ? tr("Mute") : tr("Unmute"));
        } else { prp_pushUndoRedoName(visible ? tr("Hide") : tr("Show")); }
        UndoRedo ur;
        const auto oldValue = mVisible;
        const auto newValue = visible;
        ur.fUndo = [this, oldValue]() { setVisible(oldValue); };
        ur.fRedo = [this, newValue]() { setVisible(newValue); };
        prp_addUndoRedo(ur);
    }
    mVisible = visible;

    if (hasDurationRectangle() && enve_cast<BoundingBox*>(this)) {
        const auto updateRange = prp_absInfluenceRange().adjusted(-1, 1);
        prp_afterChangedAbsRange(updateRange, false);
    } else { prp_afterWholeInfluenceRangeChanged(); }

    SWT_scheduleContentUpdate(SWT_BoxRule::visible);
    SWT_scheduleContentUpdate(SWT_BoxRule::hidden);

    emit visibilityChanged(visible);
}

void eBoxOrSound::switchVisible() {
    setVisible(!mVisible);
}

void eBoxOrSound::switchLocked() {
    setLocked(!mLocked);
}

void eBoxOrSound::hide() {
    setVisible(false);
}

void eBoxOrSound::show() {
    setVisible(true);
}

bool eBoxOrSound::isVisible() const {
    return mVisible && mVisibleAnim->getBoolValue();
}

bool eBoxOrSound::isVisibleAndUnlocked() const {
    return isVisible() && !mLocked;
}

bool eBoxOrSound::isLocked() const {
    return mLocked;
}

void eBoxOrSound::lock() {
    setLocked(true);
}

void eBoxOrSound::unlock() {
    setLocked(false);
}

void eBoxOrSound::setLocked(const bool locked) {
    if(locked == mLocked) return;
    const auto pScene = getParentScene();
    if(pScene && mSelected) {
        if(const auto bb = enve_cast<BoundingBox*>(this)) {
            pScene->removeBoxFromSelection(bb);
        }
    }
    mLocked = locked;
    SWT_scheduleContentUpdate(SWT_BoxRule::locked);
    SWT_scheduleContentUpdate(SWT_BoxRule::unlocked);
    emit lockedChanged(locked);
}

void eBoxOrSound::setSolo(const bool solo) {
    if(mSolo == solo) return;
    if(const auto bone = enve_cast<Bone*>(this)) {
        Bone::diag(QStringLiteral("solo %1 -> %2")
                   .arg(prp_getName()).arg(solo));
    }
    if(!isLink()) {
        prp_pushUndoRedoName(solo ? tr("Solo") : tr("Unsolo"));
        UndoRedo ur;
        const auto oldValue = mSolo;
        const auto newValue = solo;
        ur.fUndo = [this, oldValue]() { setSolo(oldValue); };
        ur.fRedo = [this, newValue]() { setSolo(newValue); };
        prp_addUndoRedo(ur);
    }
    mSolo = solo;
    // solo changes the draw list of the whole parent container
    prp_afterWholeInfluenceRangeChanged();
    emit soloChanged(solo);
}

void eBoxOrSound::switchSolo() {
    setSolo(!mSolo);
}

void eBoxOrSound::setShy(const bool shy) {
    if(mShy == shy) return;
    if(const auto bone = enve_cast<Bone*>(this)) {
        Bone::diag(QStringLiteral("shy %1 -> %2")
                   .arg(prp_getName()).arg(shy));
    }
    mShy = shy;
    // defer the row visibility update: SWT_setVisible immediately
    // rebuilds the visible timeline rows and must not run inside the
    // click handler of a button that lives in one of those rows
    QMetaObject::invokeMethod(this, [this]() {
        updateRowVisibility();
    }, Qt::QueuedConnection);
    emit shyChanged(shy);
}

void eBoxOrSound::switchShy() {
    setShy(!mShy);
}

void eBoxOrSound::setMbEnabled(const bool enabled) {
    if(mMbEnabled == enabled) return;
    mMbEnabled = enabled;
    prp_afterWholeInfluenceRangeChanged();
}

void eBoxOrSound::switchMbEnabled() {
    setMbEnabled(!mMbEnabled);
}

void eBoxOrSound::updateRowVisibility() {
    SWT_setVisible(!(mShy && SingleWidgetTarget::sHideShyLayers) &&
                   !mHiddenByTrack);
}

// audio layers may only share a track with other audio layers,
// visual layers (BoundingBox subclasses) only with visual ones
bool eBoxOrSound::isAudioKind() const {
    return enve_cast<const eSound*>(this) != nullptr;
}

void eBoxOrSound::applyTrackId(const int id) {
    const int oldId = mTrackId;
    mTrackId = id;
    if(id < 0) setHiddenByTrack(false);
    // re-resolve both the abandoned and the joined track (queued: this
    // may run inside an undo/redo or a drag&drop reparent)
    if(oldId >= 0 || id >= 0) {
        const QPointer<Canvas> scene = getParentScene();
        const QPointer<ContainerBox> parent = mParentGroup.data();
        QMetaObject::invokeMethod(this, [scene, parent, oldId, id]() {
            if(!scene || !parent) return;
            if(oldId >= 0) scene->enforceTrack(parent, oldId);
            if(id >= 0 && id != oldId) scene->enforceTrack(parent, id);
        }, Qt::QueuedConnection);
    }
}

void eBoxOrSound::setTrackId(const int id) {
    if(mTrackId == id) return;
    const int oldId = mTrackId;
    applyTrackId(id);
    const QPointer<eBoxOrSound> thisQPtr = this;
    UndoRedo ur;
    ur.fUndo = [thisQPtr, oldId]() {
        if(thisQPtr) thisQPtr->applyTrackId(oldId);
    };
    ur.fRedo = [thisQPtr, id]() {
        if(thisQPtr) thisQPtr->applyTrackId(id);
    };
    prp_addUndoRedo(ur);
}

void eBoxOrSound::setHiddenByTrack(const bool hidden) {
    if(mHiddenByTrack == hidden) return;
    mHiddenByTrack = hidden;
    updateRowVisibility();
}

QList<eBoxOrSound*> eBoxOrSound::trackMembers() const {
    QList<eBoxOrSound*> result;
    if(mTrackId < 0 || !mParentGroup) {
        result << const_cast<eBoxOrSound*>(this);
        return result;
    }
    const auto& contained = mParentGroup->getContained();
    for(const auto& c : contained) {
        if(c && c->trackId() == mTrackId) result << c.data();
    }
    return result;
}

eBoxOrSound *eBoxOrSound::trackMemberAtX(
        const int pressX, const int minViewedFrame,
        const qreal pixelsPerFrame) const {
    if(mTrackId < 0 || !mParentGroup) return nullptr;
    const auto& contained = mParentGroup->getContained();
    // topmost-first: the visually topmost sibling wins the click
    for(const auto& c : contained) {
        const auto sibling = c.data();
        if(!sibling || sibling == this) continue;
        if(sibling->trackId() != mTrackId) continue;
        const auto dur = sibling->getDurationRectangle();
        if(!dur) continue;
        const qreal startX = (dur->getMinAbsFrame() - minViewedFrame + 0.5)*
                             pixelsPerFrame;
        const qreal endX = (dur->getMaxAbsFrame() - minViewedFrame + 0.5)*
                           pixelsPerFrame;
        if(pressX > startX && pressX < endX) return sibling;
    }
    return nullptr;
}

void eBoxOrSound::scheduleTrackEnforce() const {
    const QPointer<Canvas> scene = getParentScene();
    const QPointer<ContainerBox> parent = mParentGroup.data();
    const int tid = mTrackId;
    // const_cast: invokeMethod needs a non-const context object;
    // the lambda only reschedules a UI refresh, it does not mutate
    QMetaObject::invokeMethod(const_cast<eBoxOrSound*>(this),
                              [scene, parent, tid]() {
        if(scene && parent) scene->enforceTrack(parent, tid);
    }, Qt::QueuedConnection);
}

static void drawClipNameOnRect(QPainter * const p,
                               const eBoxOrSound* const box,
                               const qreal pixelsPerFrame,
                               const FrameRange &absFrameRange,
                               const int rowHeight) {
    const auto dur = box->getDurationRectangle();
    if(!dur) return;
    const qreal x0 = (dur->getMinAbsFrame() - absFrameRange.fMin + 0.5)*
                     pixelsPerFrame;
    const qreal x1 = (dur->getMaxAbsFrame() - absFrameRange.fMin + 1.5)*
                     pixelsPerFrame;
    const int w = qFloor(x1 - x0) - 6;
    if(w < 20) return;
    p->save();
    p->setPen(QColor(255, 255, 255, 200));
    p->setBrush(Qt::NoBrush);
    const QRect rect(qFloor(x0) + 3, 0, w, rowHeight);
    const QString name = p->fontMetrics().elidedText(
                box->prp_getName(), Qt::ElideRight, w);
    p->drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, name);
    p->restore();
}

void eBoxOrSound::drawTrackClips(QPainter * const p,
                                 const qreal pixelsPerFrame,
                                 const FrameRange &absFrameRange,
                                 const int rowHeight,
                                 eBoxOrSound* const active) const {
    if(mTrackId < 0 || !mParentGroup) return;
    const auto& contained = mParentGroup->getContained();
    for(const auto& c : contained) {
        const auto sibling = c.data();
        if(!sibling || sibling == active) continue;
        if(sibling->trackId() != mTrackId) continue;
        p->save();
        p->setOpacity(0.45);
        sibling->drawDurationRectangle(p, pixelsPerFrame,
                                       absFrameRange, rowHeight);
        p->restore();
        drawClipNameOnRect(p, sibling, pixelsPerFrame,
                           absFrameRange, rowHeight);
    }
}

void eBoxOrSound::drawClipLabel(QPainter * const p,
                                const qreal pixelsPerFrame,
                                const FrameRange &absFrameRange,
                                const int rowHeight) const {
    drawClipNameOnRect(p, this, pixelsPerFrame, absFrameRange, rowHeight);
}

void eBoxOrSound::setLabelColor(const QColor& color) {
    if(mLabelColor == color) return;
    mLabelColor = color;
    emit labelColorChanged(mLabelColor);
}

void eBoxOrSound::moveUp() {
    mParentGroup->decreaseContainedZInList(this);
}

void eBoxOrSound::moveDown() {
    mParentGroup->increaseContainedZInList(this);
}

void eBoxOrSound::moveTo(const int index)
{
    mParentGroup->moveContainedInList(this,
                                      getZIndex(),
                                      index);
}

void eBoxOrSound::bringToFront() {
    mParentGroup->bringContainedToFrontList(this);
}

void eBoxOrSound::bringToEnd() {
    mParentGroup->bringContainedToEndList(this);
}

void eBoxOrSound::rename(const QString &newName) {
    if(newName == prp_getName()) return;
    const auto fixedName = Property::prp_sFixName(newName);
    const auto parentScene = getParentScene();
    if(parentScene) {
        const QString uniqueName = parentScene->
                makeNameUniqueForDescendants(fixedName, this);
        return prp_setNameAction(uniqueName);
    }
    prp_setNameAction(fixedName);
}
