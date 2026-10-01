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

#include "cameralensblureffect.h"

#include "include/effects/SkColorMatrix.h"
#include "Animators/qrealanimator.h"
#include "gpurendertools.h"
#include "appsupport.h"

CameraLensBlurEffect::CameraLensBlurEffect() :
    RasterEffect(QObject::tr("Camera Lens Blur"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "CameraLensBlur", HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::CAMERA_LENS_BLUR)
{
    mRadius = enve::make_shared<QrealAnimator>(15, 0, 250, 0.5, "radius");
    ca_addChild(mRadius);
    connect(mRadius.get(), &QrealAnimator::effectiveValueChanged,
            this, &RasterEffect::forcedMarginChanged);
    ca_setGUIProperty(mRadius.get());

    mThreshold = enve::make_shared<QrealAnimator>(0.7, 0.0, 1.0, 0.01,
                                                  "threshold");
    ca_addChild(mThreshold);

    mGain = enve::make_shared<QrealAnimator>(2.0, 0.0, 10.0, 0.05, "gain");
    ca_addChild(mGain);
}

namespace {

// Pass 1 - defocused base: plain gaussian over the whole frame.
SkPaint lensBasePaint(const float sigma) {
    SkPaint paint;
    paint.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
    return paint;
}

// Pass 2 - bokeh highlights: per-channel soft threshold
//   out.c = clamp(slope * (c - threshold))     (slope folds in the gain)
// then re-blurred so the surviving bright spots spread into discs,
// screen-composited over the defocused base.
SkPaint lensHighlightPaint(const float sigma,
                           const float slope,
                           const float offset) {
    const float m[20] = {
        slope, 0,     0,     0, offset,
        0,     slope, 0,     0, offset,
        0,     0,     slope, 0, offset,
        0,     0,     0,     1, 0
    };
    SkColorMatrix cm;
    cm.setRowMajor(m);

    SkPaint paint;
    paint.setColorFilter(SkColorFilters::Matrix(cm));
    paint.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
    paint.setBlendMode(SkBlendMode::kScreen);
    return paint;
}

} // namespace

class CameraLensBlurEffectCaller : public RasterEffectCaller {
public:
    CameraLensBlurEffectCaller(const HardwareSupport hwSupport,
                               const qreal radius,
                               const qreal threshold,
                               const qreal gain);

    void processGpu(QGL33 * const gl, GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;

private:
    void process(SkCanvas * const canvas,
                 const sk_sp<SkImage>& srcImg,
                 const float drawX, const float drawY) const;

    const float mRadius;
    const float mThreshold;
    const float mGain;
};

CameraLensBlurEffectCaller::CameraLensBlurEffectCaller(
        const HardwareSupport hwSupport,
        const qreal radius,
        const qreal threshold,
        const qreal gain) :
    RasterEffectCaller(hwSupport, true, QMargins() + qCeil(radius)),
    mRadius(static_cast<float>(radius)),
    mThreshold(static_cast<float>(threshold)),
    mGain(static_cast<float>(gain)) {}

void CameraLensBlurEffectCaller::process(SkCanvas * const canvas,
                                         const sk_sp<SkImage>& srcImg,
                                         const float drawX,
                                         const float drawY) const {
    const float sigma = std::max(0.05f, mRadius * 0.3333333f);

    // 1. defocused base
    auto basePaint = lensBasePaint(sigma);
    canvas->drawImage(srcImg, drawX, drawY, &basePaint);

    // 2. bokeh highlights on top (screen blend)
    if (mGain > 0.001f && mThreshold < 0.999f) {
        const float t = qBound(0.0f, mThreshold, 0.999f);
        const float slope = std::min(100.0f, mGain / (1.0f - t));
        const float offset = -slope * t;

        auto hiPaint = lensHighlightPaint(sigma, slope, offset);
        canvas->drawImage(srcImg, drawX, drawY, &hiPaint);

        // tighter second highlight pass: flattens the disc interiors,
        // closer to the flat bokeh discs of a real lens
        if (mRadius > 4.0f) {
            auto corePaint = lensHighlightPaint(sigma * 0.45f,
                                                slope * 0.6f, offset * 0.6f);
            canvas->drawImage(srcImg, drawX, drawY, &corePaint);
        }
    }
}

void CameraLensBlurEffectCaller::processGpu(QGL33 * const gl,
                                            GpuRenderTools& renderTools) {
    Q_UNUSED(gl)

    renderTools.switchToSkia();
    const auto canvas = renderTools.requestTargetCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const auto srcTex = renderTools.requestSrcTextureImageWrapper();
    if (!srcTex) return;

    process(canvas, srcTex, 0, 0);
    canvas->flush();

    renderTools.swapTextures();
}

void CameraLensBlurEffectCaller::processCpu(CpuRenderTools& renderTools,
                                            const CpuRenderData& data) {
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;

    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }

    SkCanvas canvas(dstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);

    const int radCeil = std::max(1, static_cast<int>(ceil(mRadius)));
    const auto& texTile = data.fTexTile;
    auto srcRect = texTile.makeOutset(radCeil, radCeil);

    if (srcRect.intersect(srcRect, srcBtmp.bounds())) {
        SkBitmap packedTile;
        packedTile.allocPixels(srcBtmp.info().makeWH(srcRect.width(),
                                                     srcRect.height()));

        srcBtmp.readPixels(packedTile.info(),
                           packedTile.getPixels(),
                           packedTile.rowBytes(),
                           srcRect.left(),
                           srcRect.top());

        const float drawX = static_cast<float>(srcRect.left() - texTile.left());
        const float drawY = static_cast<float>(srcRect.top() - texTile.top());

        const auto srcImg = SkImage::MakeFromBitmap(packedTile);
        if (srcImg) process(&canvas, srcImg, drawX, drawY);
    }
}

stdsptr<RasterEffectCaller> CameraLensBlurEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(data)

    const qreal radius = mRadius->getEffectiveValue(relFrame)*resolution*influence;
    if (radius < 0.001) return nullptr;

    const qreal threshold = mThreshold->getEffectiveValue(relFrame);
    const qreal gain = mGain->getEffectiveValue(relFrame)*influence;

    return enve::make_shared<CameraLensBlurEffectCaller>(
                instanceHwSupport(), radius, threshold, gain);
}

QMargins CameraLensBlurEffect::getMargin() const {
    return QMargins() + qCeil(mRadius->getEffectiveValue());
}
