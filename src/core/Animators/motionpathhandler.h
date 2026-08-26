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

#ifndef MOTIONPATHHANDLER_H
#define MOTIONPATHHANDLER_H

#include "Animators/transformanimator.h"
#include "MovablePoints/movablepoint.h"

class GraphKey;

// AE-style motion path: position keyframes of the parent
// BoxTransformAnimator shown on the canvas with draggable key points
// and bezier handle points.
// Coordinates: the position animator works in the PARENT coordinate
// system; points reuse the parent transform so everything matches.
class CORE_EXPORT MotionPathHandler final : public Property {
    Q_OBJECT
public:
    MotionPathHandler(BoxTransformAnimator * const target);

    void prp_drawCanvasControls(
            SkCanvas * const canvas, const CanvasMode mode,
            const float invScale, const bool ctrlPressed);

    // not serialized: the handler is an overlay owned by the box
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;
    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp);
private:
    void syncPoints();
    bool shouldDraw() const;

    BoxTransformAnimator * const mTarget;
    stdsptr<PointsHandler> mPoints;
    int mSyncedKeyCount = -1;
};

#endif // MOTIONPATHHANDLER_H
