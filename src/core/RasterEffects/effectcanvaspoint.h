/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
*/

#ifndef EFFECTCANVASPOINT_H
#define EFFECTCANVASPOINT_H

#include "MovablePoints/movablepoint.h"

class QPointFAnimator;
class RasterEffect;

// AE-style effect point control: a draggable crosshair on the canvas
// for a raster-effect point parameter (zoom-blur center, shadow
// translation, ...). The canvas draws canvas controls and hit-tests
// dragging only for selected boxes, so the handle shows up exactly
// like AE's effect point (layer selected + view toggle on).
//
// The animator is driven through the standard transform protocol
// (prp_startTransform / setBaseValue / prp_finishTransform), so drags
// are single-step undoable and keyframable.
//
// Space selects how the animator value maps to the host box's local
// coordinates:
//   Normalized - 0..1 over the content bounding rect (UV centers)
//   Offset     - pixels relative to the content bounding-rect center
class CORE_EXPORT EffectCanvasPoint : public MovablePoint {
    e_OBJECT
public:
    enum class Space { Normalized, Offset };

    EffectCanvasPoint(QPointFAnimator * const animator,
                      RasterEffect * const effect,
                      const Space space);

    QPointF getRelativePos() const;
    void setRelativePos(const QPointF &relPos);

    void startTransform();
    void finishTransform();
    void cancelTransform();

    bool isVisible(const CanvasMode mode) const;

    void drawSk(SkCanvas * const canvas,
                const CanvasMode mode,
                const float invScale,
                const bool keyOnCurrent,
                const bool ctrlPressed);

    static bool pointsVisible();
    static void setPointsVisible(const bool visible);
private:
    BoundingBox *resolveHostBox() const;
    QRectF hostRect() const;

    const QPointer<RasterEffect> mEffect;
    const QPointer<QPointFAnimator> mAnimator;
    const Space mSpace;
};

#endif // EFFECTCANVASPOINT_H
