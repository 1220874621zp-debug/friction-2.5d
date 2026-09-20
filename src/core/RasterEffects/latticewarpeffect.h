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

// One control point: a u/v pair plus the P1 degrees of freedom
// (rotation in degrees, scale in percent), all keyframable, with
// public accessors (ComplexAnimator keeps its child API protected).
// Children layout is positional: X, Y, 角度, 缩放 - the binary reader
// gates the last two on EvFormat::latticeWarpP1 for old files.
class LatticePointValues : public StaticComplexAnimator {
public:
    LatticePointValues(const QString &name,
                       qsptr<QrealAnimator> u,
                       qsptr<QrealAnimator> v,
                       qsptr<QrealAnimator> rot = nullptr,
                       qsptr<QrealAnimator> scale = nullptr);

    void prp_readProperty_impl(eReadStream &src) override;

    QrealAnimator *uAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(0).get()); }
    QrealAnimator *vAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(1).get()); }
    QrealAnimator *rotAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(2).get()); }
    QrealAnimator *scaleAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(3).get()); }
};

// The dynamic control-point grid/cage: ComplexAnimator (not Static -
// StaticComplexAnimator privatizes child removal) with the same
// child-sequential serialization StaticComplexAnimator provides.
class LatticePointsGroup : public ComplexAnimator {
public:
    LatticePointsGroup();

    const QList<qsptr<Property>>& pts() const
    { return ca_getChildren(); }
    void addPt(const qsptr<Property>& child)
    { ca_addChild(child); }
    void insertPt(const qsptr<Property>& child, const int id)
    { ca_insertChild(child, id); }
    void removePt(const qsptr<Property>& child)
    { ca_removeChild(child); }

    void prp_writeProperty_impl(eWriteStream &dst) const;
    void prp_readProperty_impl(eReadStream &src);
    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp);
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;
};

// Lattice (FFD) / cage (mean-value coordinates) deformation in the
// spirit of the AE "Putty" plugin: control points stored in
// normalized [0,1] space over the rest domain (content bounds plus a
// configurable margin, or a scene-anchored fixed field), so the
// lattice auto-fits whatever the host layer renders. Per-point DOF
// (rotation/scale) act as local affine handles blended by the basis
// weights; a falloff ring fades the displacement smoothly outside
// the domain. Rendering tessellates the domain into a dense triangle
// mesh, evaluates the surface at mesh vertices only, then redraws
// the source bitmap through the mesh with SkCanvas::drawVertices
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

    // rest domain rect in box-local coordinates: content bounds
    // outset by the margin percentage, or the fixed field mapped
    // back through the layer transform
    QRectF calcRestRectLocal() const;

    // control-point accessors used by the canvas handle; ids are
    // row-major over the control grid (lattice) or polygon order (cage)
    int pointCount() const;
    QPointF pointLocalPos(const int id) const;
    void setPointLocalPos(const int id, const QPointF &pos);
    void startPointTransform(const int id);
    void finishPointTransform(const int id);
    void cancelPointTransform(const int id);

    // 0 = lattice FFD, 1 = cage mean-value coordinates
    int latticeMode() const;
    bool isFixedField() const;
    QRectF fieldRectScene() const;

    // cage editing (context menu); structure changes, not undoable
    void insertPointAfter(const int id);
    void removePoint(const int id);
private:
    friend class LatticePoint;

    void rebuildPoints();
    void syncHandler();
    void captureFieldRect();

    QrealAnimator *pointU(const int id) const;
    QrealAnimator *pointV(const int id) const;
    QrealAnimator *pointRot(const int id) const;
    QrealAnimator *pointScale(const int id) const;
    QMargins calcMargin(const qreal relFrame, const qreal resolution) const;

    qsptr<ComboBoxProperty> mMode;      // 0 lattice, 1 cage
    qsptr<ComboBoxProperty> mRows;      // cells vertically, 1..8
    qsptr<ComboBoxProperty> mCols;      // cells horizontally, 1..8
    qsptr<ComboBoxProperty> mSmooth;    // 0 = bilinear, 1 = b-spline
    qsptr<ComboBoxProperty> mFieldMode; // 0 follow layer, 1 fixed scene field
    qsptr<QrealAnimator> mDensity;      // render tessellation per cell edge
    qsptr<QrealAnimator> mMarginPct;    // domain outset, % of content bounds
    qsptr<QrealAnimator> mFalloff;      // displacement fade, % of domain min dim
    qsptr<ComboBoxProperty> mFalloffType; // 0 linear, 1 smooth
    qsptr<StaticComplexAnimator> mFieldGroup; // fixed field (scene units)
    qsptr<QrealAnimator> mFieldX;
    qsptr<QrealAnimator> mFieldY;
    qsptr<QrealAnimator> mFieldW;
    qsptr<QrealAnimator> mFieldH;
    qsptr<LatticePointsGroup> mPoints;  // grid or polygon u/v + DOF
};

#endif // LATTICEWARPEFFECT_H
