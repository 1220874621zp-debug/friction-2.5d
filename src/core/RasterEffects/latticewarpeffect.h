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

#ifndef LATTICEWARPEFFECT_H
#define LATTICEWARPEFFECT_H

#include "rastereffect.h"
#include "skia/skiaincludes.h"
#include "Animators/complexanimator.h"
#include "Animators/staticcomplexanimator.h"
#include "Animators/qrealanimator.h"

class ComboBoxProperty;
class LatticePoint;

// One control point: a u/v pair of keyframable QrealAnimators with
// public accessors (ComplexAnimator keeps its child API protected).
class LatticePointValues : public StaticComplexAnimator {
public:
    LatticePointValues(const QString &name,
                       qsptr<QrealAnimator> u,
                       qsptr<QrealAnimator> v) :
        StaticComplexAnimator(name) {
        ca_addChild(u);
        ca_addChild(v);
    }

    QrealAnimator *uAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(0).get()); }
    QrealAnimator *vAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(1).get()); }
};

// The dynamic control-point grid: ComplexAnimator (not Static -
// StaticComplexAnimator privatizes child removal) with the same
// child-sequential serialization StaticComplexAnimator provides.
class LatticePointsGroup : public ComplexAnimator {
public:
    LatticePointsGroup();

    const QList<qsptr<Property>>& pts() const
    { return ca_getChildren(); }
    void addPt(const qsptr<Property>& child)
    { ca_addChild(child); }
    void removePt(const qsptr<Property>& child)
    { ca_removeChild(child); }

    void prp_writeProperty_impl(eWriteStream &dst) const;
    void prp_readProperty_impl(eReadStream &src);
    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp);
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;
};

// Lattice (FFD) deformation in the spirit of the AE "Putty" plugin:
// a rows x cols grid of keyframable control points stored in
// normalized [0,1] space over the rest lattice (content bounds plus a
// configurable margin), so the lattice auto-fits whatever the host
// layer renders. Rendering tessellates the lattice into a dense
// triangle mesh, evaluates the tensor-product surface (bilinear or
// clamped cubic B-spline) at mesh vertices only, then redraws the
// source bitmap through the mesh with SkCanvas::drawVertices
// (skin-mesh style) - no per-pixel inversion.
class CORE_EXPORT LatticeWarpEffect : public RasterEffect {
    e_OBJECT
    Q_OBJECT
public:
    LatticeWarpEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

    QMargins getMargin() const override;

    void prp_drawCanvasControls(SkCanvas * const canvas,
                                const CanvasMode mode,
                                const float invScale,
                                const bool ctrlPressed) override;

    // rest lattice rect in box-local coordinates: content bounds
    // outset by the margin percentage
    QRectF calcRestRectLocal() const;

    // control-point accessors used by the canvas handle; ids are
    // row-major over the (rows+1)x(cols+1) control grid
    int pointCount() const;
    QPointF pointLocalPos(const int id) const;
    void setPointLocalPos(const int id, const QPointF &pos);
    void startPointTransform(const int id);
    void finishPointTransform(const int id);
    void cancelPointTransform(const int id);
private:
    friend class LatticePoint;

    void rebuildPoints();

    QrealAnimator *pointU(const int id) const;
    QrealAnimator *pointV(const int id) const;
    QMargins calcMargin(const qreal relFrame, const qreal resolution) const;

    qsptr<ComboBoxProperty> mRows;    // cells vertically, 1..8
    qsptr<ComboBoxProperty> mCols;    // cells horizontally, 1..8
    qsptr<ComboBoxProperty> mSmooth;  // 0 = bilinear, 1 = cubic b-spline
    qsptr<QrealAnimator> mDensity;    // render tessellation per cell edge
    qsptr<QrealAnimator> mMarginPct;  // lattice outset, % of content bounds
    qsptr<LatticePointsGroup> mPoints; // (rows+1)*(cols+1) u/v pairs
};

#endif // LATTICEWARPEFFECT_H
