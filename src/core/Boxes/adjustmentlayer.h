#ifndef ADJUSTMENTLAYER_H
#define ADJUSTMENTLAYER_H

#include "boundingbox.h"

#include <QList>

class RasterEffectCaller;
class SkCanvas;
struct CpuRenderData;

// AE-style adjustment layer: carries raster effects and applies them to
// the composite of everything drawn BENEATH it inside the same parent
// (effects chain taken from its own raster effect collection). It never
// draws content of its own. GPU-only callers (custom shaders) are
// skipped on this layer for now.
class CORE_EXPORT AdjustmentLayer : public BoundingBox {
    e_OBJECT
protected:
    AdjustmentLayer();
public:
    stdsptr<BoxRenderData> createRenderData() override;
    void setupRenderData(const qreal relFrame, const QMatrix& parentM,
                         BoxRenderData* const data,
                         Canvas* const scene) override;
    void drawPixmapSk(SkCanvas * const canvas,
                      const SkFilterQuality filter, int &drawId,
                      QList<BlendEffect::Delayed> &delayed) const override;
};

// snapshot the canvas, run the callers on it (CPU) and draw the result
// back over the same device region
void CORE_EXPORT adjustmentApplyBackdrop(
        SkCanvas* const canvas,
        const QList<stdsptr<RasterEffectCaller>>& callers);

// render data for the adjustment layer: collects the callers during
// setup and applies them against the parent canvas at draw time
class CORE_EXPORT AdjustmentRenderData : public BoxRenderData {
    e_OBJECT
public:
    QList<stdsptr<RasterEffectCaller>> fCallers;

    void updateRelBoundingRect() override;
    void drawSk(SkCanvas* const canvas) override;
    void drawOnParentLayer(SkCanvas * const canvas,
                           SkPaint &paint) override;
protected:
    AdjustmentRenderData(BoundingBox * const parent);
};

#endif // ADJUSTMENTLAYER_H
