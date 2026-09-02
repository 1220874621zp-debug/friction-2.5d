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

#include "layerboxrenderdata.h"
#include "skia/skqtconversions.h"

ContainerBoxRenderData::ContainerBoxRenderData(BoundingBox * const parentBox) :
    BoxRenderData(parentBox) {
    mDelayDataSet = true;
}

void ContainerBoxRenderData::transformRenderCanvas(SkCanvas &canvas) const {
    canvas.translate(toSkScalar(-fGlobalRect.x()),
                     toSkScalar(-fGlobalRect.y()));
}
#include "pointhelpers.h"
void ContainerBoxRenderData::updateRelBoundingRect() {
    fRelBoundingRect = QRectF();
    const auto invTrans = fTotalTransform.inverted();
    for(const auto &child : fChildrenRenderData) {
        if(child->fRelBoundingRect.isEmpty()) continue;
        QPointF tl = child->fRelBoundingRect.topLeft();
        QPointF tr = child->fRelBoundingRect.topRight();
        QPointF br = child->fRelBoundingRect.bottomRight();
        QPointF bl = child->fRelBoundingRect.bottomLeft();

        const auto trans = child->fTotalTransform*invTrans;

        tl = trans.map(tl);
        tr = trans.map(tr);
        br = trans.map(br);
        bl = trans.map(bl);

        if(fRelBoundingRect.isNull()) {
            fRelBoundingRect.setTop(qMin4(tl.y(), tr.y(), br.y(), bl.y()));
            fRelBoundingRect.setLeft(qMin4(tl.x(), tr.x(), br.x(), bl.x()));
            fRelBoundingRect.setBottom(qMax4(tl.y(), tr.y(), br.y(), bl.y()));
            fRelBoundingRect.setRight(qMax4(tl.x(), tr.x(), br.x(), bl.x()));
        } else {
            fRelBoundingRect.setTop(qMin(fRelBoundingRect.top(),
                                         qMin4(tl.y(), tr.y(), br.y(), bl.y())));
            fRelBoundingRect.setLeft(qMin(fRelBoundingRect.left(),
                                          qMin4(tl.x(), tr.x(), br.x(), bl.x())));
            fRelBoundingRect.setBottom(qMax(fRelBoundingRect.bottom(),
                                            qMax4(tl.y(), tr.y(), br.y(), bl.y())));
            fRelBoundingRect.setRight(qMax(fRelBoundingRect.right(),
                                           qMax4(tl.x(), tr.x(), br.x(), bl.x())));
        }

        fOtherGlobalRects << child->fGlobalRect;
    }
}

bool ContainerBoxRenderData::isMaskDraw(const ChildRenderData &child) {
    // an open mask path temporarily falls back to SrcOver (drawing
    // state) - it must keep the legacy direct-draw path, pulling it
    // into a mask run would clear everything below it
    return child->fIsMask &&
           (child->fBlendMode == SkBlendMode::kDstIn ||
            child->fBlendMode == SkBlendMode::kDstOut);
}

void ContainerBoxRenderData::drawChild(SkCanvas * const canvas,
                                       const int index) {
    const auto& child = fChildrenRenderData.at(index);
    canvas->save();
    if(!child.fClip.fClipOps.isEmpty()) {
        const SkMatrix transform = canvas->getTotalMatrix();
        canvas->concat(toSkMatrix(fResolutionScale));
        child.fClip.clip(canvas);
        canvas->setMatrix(transform);
    }
    child->drawOnParentLayer(canvas);
    canvas->restore();
}

void ContainerBoxRenderData::drawMaskRun(SkCanvas * const canvas,
                                         const int from, const int to) {
    // accumulate the run into a matte surface the size of this
    // container's own render surface (children rects are part of it),
    // then multiply the destination by the combined coverage once
    const int w = qMax(1, fGlobalRect.width());
    const int h = qMax(1, fGlobalRect.height());
    const auto surface = SkSurface::MakeRaster(
                SkImageInfo::MakeN32Premul(w, h));
    if(!surface) {
        for(int i = from; i < to; i++) drawChild(canvas, i);
        return;
    }
    const auto mCanvas = surface->getCanvas();
    transformRenderCanvas(*mCanvas);
    // children composite bottom-to-top; AE applies masks topmost
    // first, so accumulate the run in reverse
    for(int k = to - 1; k >= from; k--) {
        const auto& child = fChildrenRenderData.at(k);
        if(!child->fRenderedImage) continue;
        mCanvas->save();
        if(!child.fClip.fClipOps.isEmpty()) {
            const SkMatrix transform = mCanvas->getTotalMatrix();
            mCanvas->concat(toSkMatrix(fResolutionScale));
            child.fClip.clip(mCanvas);
            mCanvas->setMatrix(transform);
        }
        SkPaint paint;
        if(child->fUseRenderTransform) {
            mCanvas->concat(toSkMatrix(child->fRenderTransform));
        }
        paint.setAlpha(static_cast<U8CPU>(qRound(child->fOpacity*2.55)));
        paint.setBlendMode(child->fBlendMode == SkBlendMode::kDstOut ?
                           SkBlendMode::kDstOut : SkBlendMode::kSrcOver);
        paint.setAntiAlias(child->fAntiAlias);
        mCanvas->drawImage(child->fRenderedImage,
                           child->fGlobalRect.x(),
                           child->fGlobalRect.y(), &paint);
        mCanvas->restore();
    }
    const auto maskImage = surface->makeImageSnapshot();
    // DstIn only affects the drawn image rect - clear everything
    // outside it first (same anti-bleed guard as the direct DstIn
    // draw in BoxRenderData::drawOnParentLayer, including its 1px
    // inset against filter edge bleed)
    canvas->save();
    auto rect = SkRect::MakeXYWH(fGlobalRect.x(), fGlobalRect.y(), w, h);
    rect.inset(1, 1);
    canvas->clipRect(rect, SkClipOp::kDifference, false);
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->restore();
    SkPaint maskPaint;
    maskPaint.setBlendMode(SkBlendMode::kDstIn);
    canvas->drawImage(maskImage, fGlobalRect.x(), fGlobalRect.y(),
                      &maskPaint);
}

void ContainerBoxRenderData::drawSk(SkCanvas * const canvas) {
    const int n = fChildrenRenderData.count();
    int i = 0;
    while(i < n) {
        if(isMaskDraw(fChildrenRenderData.at(i))) {
            int j = i + 1;
            while(j < n && isMaskDraw(fChildrenRenderData.at(j))) j++;
            const int runLen = j - i;
            if(runLen == 1 && fChildrenRenderData.at(i)->fBlendMode ==
                              SkBlendMode::kDstIn) {
                // single Add mask: the legacy direct draw is identical
                drawChild(canvas, i);
            } else {
                drawMaskRun(canvas, i, j);
            }
            i = j;
        } else {
            drawChild(canvas, i);
            i++;
        }
    }
}
