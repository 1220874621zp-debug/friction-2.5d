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

#ifndef EBOXORSOUND_H
#define EBOXORSOUND_H

#include "staticcomplexanimator.h"
#include "boolanimator.h"

#include "../zipfilesaver.h"
#include "../zipfileloader.h"
#include "../XML/xevzipfilesaver.h"

class ContainerBox;
class Canvas;
class DurationRectangle;
class QMimeData;

class CORE_EXPORT eBoxOrSound : public StaticComplexAnimator {
    Q_OBJECT
    e_OBJECT
    e_DECLARE_TYPE(eBoxOrSound)
protected:
    eBoxOrSound(const QString& name);
public:
    virtual bool isLink() const { return false; }

    virtual bool isFrameInDurationRect(const int relFrame) const;
    virtual bool isFrameFInDurationRect(const qreal relFrame) const;
    virtual void shiftAll(const int shift);

    QMimeData *SWT_createMimeData();

    FrameRange prp_relInfluenceRange() const;
    FrameRange prp_getIdenticalRelRange(const int relFrame) const;
    int prp_getRelFrameShift() const;
    void prp_afterChangedAbsRange(const FrameRange &range,
                                  const bool clip = true);

    void prp_writeProperty_impl(eWriteStream& dst) const;
    void prp_readProperty_impl(eReadStream& src);

    virtual void writeBoxOrSoundXEV(const std::shared_ptr<XevZipFileSaver>& xevFileSaver,
                                    const RuntimeIdToWriteId& objListIdConv,
                                    const QString& path) const;
    virtual void readBoxOrSoundXEV(XevReadBoxesHandler& boxReadHandler,
                                   ZipFileLoader& fileLoader, const QString& path,
                                   const RuntimeIdToWriteId& objListIdConv);

    TimelineMovable *anim_getTimelineMovable(
            const int relX, const int minViewedFrame,
            const qreal pixelsPerFrame);
    void prp_drawTimelineControls(
            QPainter * const p, const qreal pixelsPerFrame,
            const FrameRange &absFrameRange, const int rowHeight);

    void setParentGroup(ContainerBox * const parent);
    ContainerBox *getParentGroup() const { return mParentGroup; }
    void removeFromParent_k();
    bool isAncestor(const BoundingBox * const box) const;

    void drawDurationRectangle(
            QPainter * const p, const qreal pixelsPerFrame,
            const FrameRange &absFrameRange, const int rowHeight) const;

    void setDurationRectangle(const qsptr<DurationRectangle> &durationRect,
                              const bool lock = false);
    bool durationRectangleLocked() const;
    bool hasDurationRectangle() const;
    void createDurationRectangle();
    bool isVisibleAndInVisibleDurationRect() const;

    void startDurationRectPosTransform();
    void finishDurationRectPosTransform();
    void cancelDurationRectPosTransform();
    void moveDurationRect(const int dFrame);

    void startMinFramePosTransform();
    void finishMinFramePosTransform();
    void cancelMinFramePosTransform();
    void moveMinFrame(const int dFrame);

    void startMaxFramePosTransform();
    void finishMaxFramePosTransform();
    void cancelMaxFramePosTransform();
    void moveMaxFrame(const int dFrame);

    DurationRectangle *getDurationRectangle() const;
    bool isVisibleAndInDurationRect(const int relFrame) const;
    bool isFrameFVisibleAndInDurationRect(const qreal relFrame) const;

    void setSelected(const bool select);
    void select();
    void deselect();
    bool isSelected() const { return mSelected; }
    void selectionChangeTriggered(const bool shiftPressed);

    void hide();
    void show();
    bool isVisible() const;
    void setVisible(const bool visible);
    void switchVisible();

    void lock();
    void unlock();
    void setLocked(const bool locked);
    void switchLocked();
    bool isLocked() const;

    // AE-style layer switches
    void setSolo(const bool solo);
    void switchSolo();
    bool isSolo() const { return mSolo; }

    void setShy(const bool shy);
    void switchShy();
    bool isShy() const { return mShy; }

    // per-layer motion blur switch: gates the MotionBlur raster effect
    // sampling for this layer (scene-wide master in the View menu)
    void setMbEnabled(const bool enabled);
    void switchMbEnabled();
    bool isMbEnabled() const { return mMbEnabled; }
    // update timeline row visibility from shy state + track membership
    void updateRowVisibility();

    // timeline tracks: sibling boxes sharing a non-negative trackId
    // collapse into a single timeline row (UI-level grouping only,
    // the scene graph and rendering are not affected)
    int trackId() const { return mTrackId; }
    bool isInTrack() const { return mTrackId >= 0; }
    void setTrackId(const int id);
    // audio layers may only share a track with other audio layers,
    // visual layers (BoundingBox subclasses) only with visual ones
    bool isAudioKind() const;
    // row hidden because another member of the track is active
    void setHiddenByTrack(const bool hidden);
    bool isHiddenByTrack() const { return mHiddenByTrack; }
    // all sibling members of this box's track (incl. itself, z-order)
    QList<eBoxOrSound*> trackMembers() const;
    // the track sibling whose duration rect interior contains the x
    // position (pixels), excluding this box and the resize handles
    eBoxOrSound *trackMemberAtX(const int pressX, const int minViewedFrame,
                                const qreal pixelsPerFrame) const;
    // draw duration rectangles of all track members except 'active'
    void drawTrackClips(QPainter * const p, const qreal pixelsPerFrame,
                        const FrameRange &absFrameRange, const int rowHeight,
                        eBoxOrSound* const active) const;
    // draw this box's name over its own duration rectangle
    void drawClipLabel(QPainter * const p, const qreal pixelsPerFrame,
                       const FrameRange &absFrameRange,
                       const int rowHeight) const;

    // AE-style layer label color (shown instead of the type icon)
    void setLabelColor(const QColor& color);
    QColor getLabelColor() const { return mLabelColor; }

    bool isVisibleAndUnlocked() const;

    void moveUp();
    void moveDown();
    void moveTo(const int index);
    void bringToFront();
    void bringToEnd();
    void setZListIndex(const int z) { mZListIndex = z; }
    int getZIndex() const { return mZListIndex; }

    void rename(const QString& newName);
signals:
    void parentChanged(ContainerBox*);
    void aboutToChangeAncestor();
    void selectionChanged(bool);
    void visibilityChanged(bool);
    void lockedChanged(bool);
    void soloChanged(bool);
    void shyChanged(bool);
    void labelColorChanged(const QColor&);
private:
    void applyTrackId(const int id);
    void scheduleTrackEnforce() const;

    bool mSelected = false;
    bool mVisible = true;
    // keyframable visibility channel ("可见" property row): an extra
    // gate AND-ed with the row eye (mVisible) - base value true keeps
    // the legacy behavior until the user keys it
    qsptr<BoolAnimator> mVisibleAnim;
    bool mLocked = false;
    bool mSolo = false;
    bool mShy = false;
    bool mMbEnabled = true;
    QColor mLabelColor; // invalid = no label color (type icon shown)
    int mZListIndex = 0;
    int mTrackId = -1; // -1 = own timeline row, >= 0 shared with siblings
    bool mHiddenByTrack = false;

    bool mDurationRectangleLocked = false;
    ConnContextQSPtr<DurationRectangle> mDurationRectangle;

    ConnContextPtr<ContainerBox> mParentGroup;
};

#endif // EBOXORSOUND_H
