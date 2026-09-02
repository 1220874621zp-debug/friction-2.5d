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

    void setupRenderData(const qreal relFrame, const QMatrix& parentM,
                         BoxRenderData * const data,
                         Canvas* const scene) override;

    // AE-style mask mode (Add/Subtract) lives in the blend mode:
    // kDstIn = Add, kDstOut = Subtract; the timeline row context menu
    // exposes it as 蒙版模式
    void prp_setupTreeViewMenu(PropertyMenu * const menu) override;
    void setupMaskModeMenu(PropertyMenu * const menu);

    void writeBoundingBox(eWriteStream& dst) const override;
    void readBoundingBox(eReadStream& src) override;

    bool differenceInEditPathBetweenFrames(const int frame1,
                                           const int frame2) const;

    void saveSVG(SvgExporter& exp, DomEleTask* const task) const;

    void applyCurrentTransform();

    void loadSkPath(const SkPath& path);

    SmartPathCollection *getPathAnimator();

    QList<qsptr<SmartVectorPath>> breakPathsApart_k();
protected:
    void getMotionBlurProperties(QList<Property*> &list) const;
    qsptr<SmartPathCollection> mPathAnimator;
    bool mMaskMode = false;
};

#endif // SMARTSmartVectorPath_H
