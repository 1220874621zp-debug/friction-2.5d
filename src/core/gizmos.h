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
# See 'README.md' for more information.
#
*/

#ifndef FRICTION_GIZMOS
#define FRICTION_GIZMOS

#include "core_global.h"

#include <QObject>
#include <QColor>
#include <QPointF>
#include <QSizeF>
#include <QVector>

namespace Friction
{
    namespace Core
    {
        class CORE_EXPORT Gizmos : public QObject
        {
        public:
            enum class Interact
            {
                Position,
                Rotate,
                Scale,
                Shear,
                All
            };
            enum class AxisConstraint
            {
                None,
                X,
                Y,
                Z,
                Uniform
            };

            enum class ScaleHandle
            {
                None,
                X,
                Y,
                Uniform
            };

            enum class ShearHandle
            {
                None,
                X,
                Y
            };

            enum class Rot3DHandle
            {
                None,
                X,
                Y
            };

            struct AxisGeometry
            {
                QPointF center;
                QSizeF size;
                qreal angleDeg = 0.0;
                bool visible = false;
                bool usePolygon = false;
                QVector<QPointF> polygonPoints;
            };

            struct ScaleGeometry
            {
                QPointF center;
                qreal halfExtent = 0.0;
                bool visible = false;
                bool usePolygon = false;
                QVector<QPointF> polygonPoints;
            };

            struct ShearGeometry
            {
                QPointF center;
                qreal radius = 0.0;
                bool visible = false;
                bool usePolygon = false;
                QVector<QPointF> polygonPoints;
            };

            struct LineGeometry
            {
                QPointF start;
                QPointF end;
                qreal strokeWidth = 0.0;
                bool visible = false;
            };

            struct Config
            {
                qreal rotateSweepDeg = 90.0; // default sweep of gizmo arc
                qreal rotateBaseOffsetDeg = 270.0; // default angular offset for gizmo arc
                qreal rotateRadiusPx = 45.0; // gizmo radius in screen pixels
                qreal rotateStrokePx = 4.0; // arc stroke thickness in screen pixels
                qreal rotateHitWidthPx = rotateStrokePx + 1.0; // hit area thickness in screen pixels
                qreal rot3DXRadiusXPx = 10.0; // RotX ring horizontal semi-axis in screen pixels
                qreal rot3DXRadiusYPx = 30.0; // RotX ring vertical semi-axis in screen pixels
                qreal rot3DYRadiusXPx = 30.0; // RotY ring horizontal semi-axis in screen pixels
                qreal rot3DYRadiusYPx = 10.0; // RotY ring vertical semi-axis in screen pixels
                qreal axisWidthPx = 5.0; // axis gizmo rectangle width in screen pixels
                qreal axisHeightPx = 60.0; // axis gizmo rectangle height in screen pixels
                qreal axisYOffsetPx = 40.0; // vertical distance of Y gizmo from pivot in pixels
                qreal axisUniformOffsetPx = 7.0; // XY offset from pivot for Uniform position gizmo in pixels
                qreal axisUniformWidthPx = 24.0; // XY square width for Uniform position gizmo in pixels
                qreal axisUniformChamferPx = 1.0; // XY square chamfer for Uniform position gizmo in pixels
                qreal axisXOffsetPx = 40.0; // horizontal distance of X gizmo from pivot in pixels
                qreal axisZAngleDeg = 45.0; // screen angle of the Z gizmo (down-right diagonal)
                qreal axisZLengthPx = 70.0; // length of the Z gizmo arrow in screen pixels
                qreal scaleSizePx = 8.0; // scale gizmo square size in screen pixels
                qreal scaleGapPx = 2.0; // gap between position gizmos and scale gizmos in screen pixels
                qreal shearRadiusPx = 4.0; // shear gizmo circle radius in screen pixels
                // TODO? this is not used
                qreal shearGapPx = 2.0; // gap between scale and shear gizmos in screen pixels
                qreal xLineLengthPx = 100.0; // length of the XLine gizmo in screen pixels
                qreal xLineStrokePx = 2.0; // stroke thickness for the XLine gizmo in screen pixels
                qreal yLineLengthPx = 100.0; // length of the YLine gizmo in screen pixels
                qreal yLineStrokePx = 2.0; // stroke thickness for the YLine gizmo in screen pixels
            };

            struct Theme
            {
                // TODO: get colors from theme
                // wait until we merge the 'theme-changes' branch
                QColor colorX = QColor(232, 32, 45);
                QColor colorY = QColor(134, 232, 32);
                QColor colorZ = QColor(32, 139, 232);
                QColor colorUniform = QColor(232, 215, 32);
                qreal colorAlphaFillNormal = 210.0;
                qreal colorAlphaFillHover = 255.0;
                qreal colorAlphaStrokeNormal = 0.0;
                qreal colorAlphaStrokeHover = 210.0;
                int colorLightenNormal = 100;
                int colorLightenHover = 150;
            };

            struct State
            {
                bool rotateHandleVisible = false;
                QPointF rotateHandlePos;
                QPointF rotateHandleAnchor;
                qreal rotateHandleRadius = 0;
                qreal rotateHandleAngleDeg = 0; // cached visual rotation of the gizmo
                qreal rotateHandleSweepDeg = 90.0; // cached arc span used for draw + hit-test
                qreal mRotateHandleStartOffsetDeg = 45.0; // cached base offset applied before box rotation
                bool rotateHandleHovered = false; // true when pointer hovers the gizmo
                QVector<QPointF> rotateHandlePolygon;
                QVector<QPointF> rotateHandleHitPolygon;
                QVector<QPointF> rot3DXHandlePolygon;
                QVector<QPointF> rot3DXHandleHitPolygon;
                QVector<QPointF> rot3DYHandlePolygon;
                QVector<QPointF> rot3DYHandleHitPolygon;
                bool rot3DXHandleHovered = false;
                bool rot3DYHandleHovered = false;
                AxisGeometry axisXGeom;
                AxisGeometry axisYGeom;
                AxisGeometry axisZGeom;
                AxisGeometry axisUniformGeom;
                ScaleGeometry scaleXGeom;
                ScaleGeometry scaleYGeom;
                ScaleGeometry scaleUniformGeom;
                ShearGeometry shearXGeom;
                ShearGeometry shearYGeom;
                LineGeometry xLineGeom;
                LineGeometry yLineGeom;
                LineGeometry zLineGeom;
                bool axisXHovered = false;
                bool axisYHovered = false;
                bool axisZHovered = false;
                bool axisUniformHovered = false;
                bool scaleXHovered = false;
                bool scaleYHovered = false;
                bool scaleUniformHovered = false;
                bool shearXHovered = false;
                bool shearYHovered = false;
                AxisConstraint axisConstraint = AxisConstraint::None;
                ScaleHandle scaleConstraint = ScaleHandle::None;
                ShearHandle shearConstraint = ShearHandle::None;
                bool axisHandleActive = false;
                bool scaleHandleActive = false;
                bool shearHandleActive = false;
                bool gizmosSuppressed = false;
                bool showRotate = true;
                bool showPosition = true;
                bool showScale = false;
                bool showShear = false;
                bool rotatingFromHandle = false;
                bool visible = true;
            };

            Config fConfig;
            Theme fTheme;
            State fState;
        };
    }
}

#endif // FRICTION_GIZMOS
