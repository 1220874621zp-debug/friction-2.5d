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

#ifndef PATHBOXRENDERDATA_H
#define PATHBOXRENDERDATA_H

#include "boxrenderdata.h"
#include "Animators/paintsettingsanimator.h"

struct CORE_EXPORT PathBoxRenderData : public BoxRenderData
{
    PathBoxRenderData(BoundingBox * const parentBox);

    SkPath fEditPath;
    SkPath fPath;
    SkPath fFillPath;
    SkPath fOutlineBasePath;
    SkPath fOutlinePath;
    SkStroke fStroker;
    UpdatePaintSettings fPaintSettings;
    UpdateStrokeSettings fStrokeSettings;

    void updateRelBoundingRect();
    QPointF getCenterPosition();

    // a direct-draw path raster is a vector payload, not an image: it can
    // paint without any allocation
    bool hasDrawableContent() const override {
        if(mDirectDraw) return !fFillPath.isEmpty() || !fOutlinePath.isEmpty();
        return BoxRenderData::hasDrawableContent();
    }

protected:
    void setupRenderData();
    void drawSk(SkCanvas * const canvas);
    void drawOnParentLayer(SkCanvas * const canvas,
                           SkPaint &paint);
    void copyFrom(BoxRenderData *src);

private:
    void setupDirectDraw();
    bool mDirectDraw = false;
};

#endif // PATHBOXRENDERDATA_H
