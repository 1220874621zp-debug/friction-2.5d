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

#ifndef CORNERPINEFFECT_H
#define CORNERPINEFFECT_H

#include "rastereffect.h"
#include "Animators/staticcomplexanimator.h"
#include "Animators/qrealanimator.h"

// One pin corner: a u/v pair in normalized content space
// (0..1 = the layer's content bounds, values outside move the
// corner beyond them). Children layout is positional: X, Y.
class CornerPinPointValues : public StaticComplexAnimator {
public:
    CornerPinPointValues(const QString &name,
                         qsptr<QrealAnimator> u,
                         qsptr<QrealAnimator> v);

    QrealAnimator *uAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(0).get()); }
    QrealAnimator *vAnim() const
    { return enve_cast<QrealAnimator*>(ca_getChildren().at(1).get()); }
};

// AE-style Corner Pin: maps the layer's content rectangle onto an
// arbitrary quadrilateral through a true perspective (homography)
// transform - the screen-replacement staple. The four pin points are
// stored normalized over the content bounds, so the default values
// (0/1 corners) are an exact passthrough regardless of layer size or
// resolution, and the mapping follows layer resizes proportionally.
// Rendering tessellates the destination quad into a triangle mesh
// (the homography evaluated at the vertices only) and redraws the
// source bitmap through it with SkCanvas::drawVertices - no
// per-pixel inversion, Skia handles the sampling. A quad that folds
// through infinity (a bowtie or a corner dragged across the opposite
// side) has no valid perspective map and renders empty.
class CORE_EXPORT CornerPinEffect : public RasterEffect {
    e_OBJECT
public:
    CornerPinEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const override;

    QMargins getMargin() const override;

    // pins can drag content outside the layer's own bounds; without this
    // the effect's margin is ignored by the collection, the scene bounds
    // never grow and the dragged content is clipped away (same fix as
    // TurbulentDisplace)
    bool forceMargin() const override { return true; }

    void prp_drawCanvasControls(SkCanvas * const canvas,
                                const CanvasMode mode,
                                const float invScale,
                                const bool ctrlPressed) override;

    // pin-point accessors used by the canvas handles; ids 0..3 are
    // property order: 0 UL, 1 UR, 2 LL, 3 LR
    int pointCount() const;
    CornerPinPointValues *point(const int id) const;
    QPointF pointLocalPos(const int id) const;
    void setPointLocalPos(const int id, const QPointF &pos);
    void startPointTransform(const int id);
    void finishPointTransform(const int id);
    void cancelPointTransform(const int id);
    void resetPoint(const int id);
    void resetAllPoints();
private:
    // content bounds in box-local coordinates (the pin rest domain)
    QRectF calcRestRectLocal() const;
    QMargins calcMargin(const qreal relFrame, const qreal resolution) const;
    void syncHandler();

    qsptr<CornerPinPointValues> mPts[4]; // UL, UR, LL, LR
};

#endif // CORNERPINEFFECT_H
