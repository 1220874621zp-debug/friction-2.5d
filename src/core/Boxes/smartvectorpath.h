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

#ifndef SMARTSmartVectorPath_H
#define SMARTSmartVectorPath_H
#include <QPainterPath>
#include <QLinearGradient>
#include "pathbox.h"
#include "Animators/SmartPath/smartpathcollection.h"

class NodePoint;
class ContainerBox;
class PathAnimator;

enum class CanvasMode : short;

class SmartVectorPathEdge;

class CORE_EXPORT SmartVectorPath : public PathBox {
    e_OBJECT
    e_DECLARE_TYPE(SmartVectorPath)
protected:
    SmartVectorPath();
public:
    void setupCanvasMenu(PropertyMenu * const menu);

    SkPath getRelativePath(const qreal relFrame) const;

    // mask pen shapes: an open path does not clip (DstIn applies only
    // once every sub-path is closed); evaluated live at render time
    void setMaskMode(const bool mask) { mMaskMode = mask; }
    bool getMaskMode() const { return mMaskMode; }
    // relFrame-parameterized: render-time callers must pass the frame
    // being rendered - reading the global current frame breaks inside
    // scene links (the linked scene's boxes keep a stale current frame)
    bool allSubPathsClosed(const qreal relFrame) const;
    SkBlendMode getPaintBlendMode(const qreal relFrame) const override;
    bool isMaskBox() const override { return mMaskMode; }

    void setupRenderData(const qreal relFrame, const QTransform& parentM,
                         BoxRenderData * const data,
                         Canvas* const scene) override;

    // AE-style mask mode (Add/Subtract) lives in the blend mode:
    // kDstIn = Add, kDstOut = Subtract; the timeline mask row shows
    // a dedicated dropdown (BoxSingleWidget)

    void writeBoundingBox(eWriteStream& dst) const override;
    void readBoundingBox(eReadStream& src) override;

    bool differenceInEditPathBetweenFrames(const int frame1,
                                           const int frame2) const;
    // path-source aware cache compatibility (path effects included)
    bool localDifferenceInPathBetweenFrames(const int frame1,
                                            const int frame2) const;

    void saveSVG(SvgExporter& exp, DomEleTask* const task) const;

    void applyCurrentTransform();

    void loadSkPath(const SkPath& path);

    SmartPathCollection *getPathAnimator();

    // path source link (AE "shared path" equivalent): this layer's
    // geometry is redirected to the source layer's path, so editing
    // the source's nodes updates every linked layer live. An optional
    // outline offset renders the normal-offset closed outline (road
    // frame) instead of the plain path. Cycles are rejected.
    void setPathSource(SmartVectorPath * const source);
    SmartVectorPath *getPathSource() const { return mPathSource.data(); }
    QrealAnimator *getPathSourceOffset() const
    { return mPathSourceOffset.get(); }

    QList<qsptr<SmartVectorPath>> breakPathsApart_k();
protected:
    void getMotionBlurProperties(QList<Property*> &list) const;
    qsptr<SmartPathCollection> mPathAnimator;
    bool mMaskMode = false;
private:
    SkPath outlineOffsetPath(const SkPath &src, const qreal offset) const;
    void connectPathSource();
    void disconnectPathSource();
    qptr<SmartVectorPath> mPathSource;
    qsptr<QrealAnimator> mPathSourceOffset;
    qsptr<class BoxTargetProperty> mPathTarget;
    QList<QMetaObject::Connection> mPathSourceConns;
};

#endif // SMARTSmartVectorPath_H
