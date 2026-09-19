/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "adjustmentlayer.h"

#include "Boxes/boxrenderdata.h"
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffect.h"
#include "canvas.h"
#include "Private/document.h"

// ---------------------------------------------------------------------------
// AdjustmentRenderData

AdjustmentRenderData::AdjustmentRenderData(BoundingBox * const parent) :
    BoxRenderData(parent) {}

void AdjustmentRenderData::updateRelBoundingRect() {
    // never draws own content; keep a tiny non-degenerate bitmap so the
    // base pipeline stays happy
    fRelBoundingRect = QRectF(0, 0, 1, 1);
}

void AdjustmentRenderData::drawSk(SkCanvas* const canvas) {
    Q_UNUSED(canvas)
}

void AdjustmentRenderData::drawOnParentLayer(SkCanvas * const canvas,
                                             SkPaint &paint) {
    Q_UNUSED(paint)
    adjustmentApplyBackdrop(canvas, fCallers,
                            fScaledTransform.map(QPointF(0, 0)));
}

// ---------------------------------------------------------------------------
// backdrop application

void adjustmentApplyBackdrop(
        SkCanvas* const canvas,
        const QList<stdsptr<RasterEffectCaller>>& callers,
        const QPointF& worldAnchor) {
    if(callers.isEmpty() || !canvas) return;
    const SkIRect dev = canvas->getDeviceClipBounds();
    if(dev.isEmpty()) return;
    auto* surf = canvas->getSurface();
    if(!surf) return;
    canvas->flush();
    sk_sp<SkImage> snap = surf->makeImageSnapshot(dev);
    if(!snap) return;

    const SkImageInfo info =
            SkImageInfo::MakeN32Premul(dev.width(), dev.height());
    SkBitmap srcB;
    srcB.allocPixels(info);
    if(!snap->readPixels(srcB.pixmap(), 0, 0)) {
        if(!surf->readPixels(srcB, dev.left(), dev.top())) return;
    }
    SkBitmap dstB;
    dstB.allocPixels(info);
    dstB.eraseColor(SK_ColorTRANSPARENT);

    CpuRenderTools tools{srcB, dstB};
    CpuRenderData cdata;
    cdata.fPos = QPoint(dev.left(), dev.top());
    cdata.fTexTile = SkIRect::MakeSize(info.dimensions());
    cdata.fWidth = static_cast<uint>(dev.width());
    cdata.fHeight = static_cast<uint>(dev.height());
    // the tile is a device-space viewport snapshot; hand callers the
    // view matrix and a world anchor so content-placing effects (e.g.
    // particles) stay anchored to the scene instead of the viewport
    cdata.fDeviceSpace = true;
    cdata.fDevMatrix = canvas->getTotalMatrix();
    cdata.fWorldAnchor = worldAnchor;

    bool hasProcessed = false;
    for(const auto& caller : callers) {
        if(!caller) continue;
        // MVP: gpu-only callers (custom shaders) are skipped here
        if(caller->hardwareSupport() == HardwareSupport::gpuOnly) continue;
        caller->processCpu(tools, cdata);
        hasProcessed = true;
        // Feed dstB back into srcB for the next chained effect
        if(srcB.getPixels() && dstB.getPixels()) {
            memcpy(srcB.getPixels(), dstB.getPixels(), srcB.computeByteSize());
        }
    }
    if(!hasProcessed) return;

    canvas->save();
    canvas->resetMatrix(); // the snapshot is in device space
    canvas->drawImage(SkImage::MakeFromBitmap(dstB), dev.left(), dev.top());
    canvas->restore();
}

// ---------------------------------------------------------------------------
// AdjustmentLayer

AdjustmentLayer::AdjustmentLayer() :
    BoundingBox(QObject::tr("Adjustment Layer"),
                eBoxType::adjustmentLayer) {}

stdsptr<BoxRenderData> AdjustmentLayer::createRenderData() {
    return enve::make_shared<AdjustmentRenderData>(this);
}

void AdjustmentLayer::setupRenderData(const qreal relFrame,
                                      const QTransform& parentM,
                                      BoxRenderData* const data,
                                      Canvas* const scene) {
    // like the base class, except the raster effect callers are NOT left
    // in the box-image effects phase - they are moved out and applied
    // manually against the backdrop snapshot in drawOnParentLayer
    setupWithoutRasterEffects(relFrame, parentM, data, scene);
    const auto adData = static_cast<AdjustmentRenderData*>(data);
    if(!scene || !scene->getRasterEffectsVisible() || !mEffectsEnabled) {
        adData->fCallers.clear();
        return;
    }
    mRasterEffectsAnimators->addEffects(relFrame, data);
    adData->fCallers = data->fEffectCallers;
    data->fEffectCallers.clear();
}

void AdjustmentLayer::drawPixmapSk(SkCanvas * const canvas,
                                   const SkFilterQuality filter,
                                   int &drawId,
                                   QList<BlendEffect::Delayed> &delayed) const {
    Q_UNUSED(filter)
    Q_UNUSED(drawId)
    Q_UNUSED(delayed)
    // interactive (view) path: same backdrop treatment using the cached
    // render data's callers
    const auto data = drawRenderContainer().getSrcRenderData();
    const auto adData = data ?
                static_cast<const AdjustmentRenderData*>(data) : nullptr;
    if(!adData) return;
    adjustmentApplyBackdrop(canvas, adData->fCallers,
                            adData->fScaledTransform.map(QPointF(0, 0)));
}
