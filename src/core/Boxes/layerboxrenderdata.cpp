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
    // a mask with a missing image (evicted / still loading) must not
    // collapse the matte: fall back to legacy per-child draws - a
    // skipped Add would otherwise erase the whole layer for a frame
    for(int i = from; i < to; i++) {
        if(!fChildrenRenderData.at(i)->fRenderedImage) {
            for(int k = from; k < to; k++) drawChild(canvas, k);
            return;
        }
    }
    // AE applies masks top-down against the layer's own alpha: a run
    // whose topmost mask is a Subtract starts from FULL coverage, so
    // the matte must span the whole container; a run starting with an
    // Add builds coverage from empty and only needs the union of the
    // masks' own rects (much smaller on large canvases)
    const bool topmostSubtract =
            fChildrenRenderData.at(to - 1)->fBlendMode ==
            SkBlendMode::kDstOut;
    QRect matteRect = fGlobalRect;
    if(!topmostSubtract) {
        QRect rectUnion;
        for(int i = from; i < to; i++)
            rectUnion = rectUnion.united(
                        fChildrenRenderData.at(i)->fGlobalRect);
        matteRect = rectUnion.intersected(fGlobalRect);
    }
    if(matteRect.isEmpty()) {
        // no mask covers any content: an Add-run hides everything,
        // a Subtract-first run erases nothing
        if(!topmostSubtract) canvas->clear(SK_ColorTRANSPARENT);
        return;
    }
    // accumulate the run into a matte surface, then multiply the
    // destination by the combined coverage once
    const int w = matteRect.width();
    const int h = matteRect.height();
    const auto surface = SkSurface::MakeRaster(
                SkImageInfo::MakeN32Premul(w, h));
    if(!surface) {
        for(int i = from; i < to; i++) drawChild(canvas, i);
        return;
    }
    const auto mCanvas = surface->getCanvas();
    mCanvas->translate(toSkScalar(-matteRect.x()),
                       toSkScalar(-matteRect.y()));
    // Subtract-first runs begin from full coverage (AE); the empty
    // matte default only fits Add-first runs
    if(topmostSubtract) mCanvas->clear(SK_ColorWHITE);
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
        // drawOnParentLayer sets the filter quality for transformed
        // draws - without it a scaled/rotated mask samples
        // nearest-neighbour and its edges turn jagged
        paint.setFilterQuality(child->fFilterQuality);
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
    // inset against filter edge bleed); for Add-runs the matte rect
    // is the mask union, so this also hides content outside all masks
    canvas->save();
    auto rect = SkRect::MakeXYWH(matteRect.x(), matteRect.y(), w, h);
    // insetting a tiny rect would empty it, turning the difference
    // clip into a full-canvas clear
    if(rect.width() > 2 && rect.height() > 2) rect.inset(1, 1);
    canvas->clipRect(rect, SkClipOp::kDifference, false);
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->restore();
    SkPaint maskPaint;
    maskPaint.setBlendMode(SkBlendMode::kDstIn);
    canvas->drawImage(maskImage, matteRect.x(), matteRect.y(),
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
            bool allSubtract = true;
            for(int k = i; k < j; k++) {
                if(fChildrenRenderData.at(k)->fBlendMode !=
                        SkBlendMode::kDstOut) {
                    allSubtract = false;
                    break;
                }
            }
            // single masks and pure Subtract runs: the legacy direct
            // draws are identical (sequential DstOut IS matte algebra:
            // (1-a)(1-b) == 1-(a union b)), and a lone Subtract must
            // cut a hole - NOT erase the layer as an empty-matte
            // DstIn run would
            if(runLen == 1 || allSubtract) {
                for(int k = i; k < j; k++) drawChild(canvas, k);
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
