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

#include "gloweffect.h"
#include "gpurendertools.h"
#include "rastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"

GlowEffect::GlowEffect() :
    RasterEffect("glow",
                 AppSupport::getRasterEffectHardwareSupport("Glow",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::GLOW)
{
    mThreshold = enve::make_shared<QrealAnimator>(0.1, 0.0, 1.0, 0.01, "threshold");
    ca_addChild(mThreshold);

    mIntensity = enve::make_shared<QrealAnimator>(2.0, 0.0, 10.0, 0.1, "intensity");
    ca_addChild(mIntensity);

    mRadius = enve::make_shared<QrealAnimator>(20.0, 1.0, 100.0, 0.5, "radius");
    ca_addChild(mRadius);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(255, 255, 255));
    ca_addChild(mColor);
}

class GlowEffectCaller : public RasterEffectCaller {
public:
    GlowEffectCaller(const HardwareSupport hwSupport,
                     const qreal threshold,
                     const qreal intensity,
                     const qreal radius,
                     const QColor& color,
                     const QMargins& margins) :
        RasterEffectCaller(hwSupport, true, margins),
        mThreshold(threshold),
        mIntensity(intensity),
        mRadius(radius),
        mColor(color) {}

    void processGpu(QGL33 * const gl, GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;

private:
    const qreal mThreshold;
    const qreal mIntensity;
    const qreal mRadius;
    const QColor mColor;
};

stdsptr<RasterEffectCaller> GlowEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(data)

    const qreal threshold = mThreshold->getEffectiveValue(relFrame);
    const qreal intensity = mIntensity->getEffectiveValue(relFrame) * influence;
    const qreal radius = mRadius->getEffectiveValue(relFrame) * resolution;
    const QColor color = mColor->getColor(relFrame);

    const int margin = qCeil(radius * 2.5);

    return enve::make_shared<GlowEffectCaller>(
                instanceHwSupport(), threshold, intensity, radius, color,
                QMargins(margin, margin, margin, margin));
}

void GlowEffectCaller::processGpu(QGL33 * const gl, GpuRenderTools &renderTools)
{
    Q_UNUSED(gl)

    renderTools.switchToSkia();
    const auto canvas = renderTools.requestTargetCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const auto srcTex = renderTools.requestSrcTextureImageWrapper();
    if (!srcTex) return;

    // 1. Soft wide bloom pass
    SkPaint bloomPaint;
    const float sigma = std::max(0.5f, static_cast<float>(mRadius * 0.45));
    bloomPaint.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
    const uint8_t a = static_cast<uint8_t>(qBound(0.0, 255.0 * (mColor.alphaF() * std::min(2.0, mIntensity) * 0.6), 255.0));
    bloomPaint.setColorFilter(SkColorFilters::Blend(
        SkColorSetARGB(a, mColor.red(), mColor.green(), mColor.blue()),
        SkBlendMode::kSrcIn));
    canvas->drawImage(srcTex, 0, 0, &bloomPaint);

    // 2. Intense inner glow core
    if (mRadius > 3.0) {
        SkPaint corePaint;
        const float coreSigma = std::max(0.3f, static_cast<float>(mRadius * 0.18));
        corePaint.setImageFilter(SkImageFilters::Blur(coreSigma, coreSigma, nullptr));
        const uint8_t coreA = static_cast<uint8_t>(qBound(0.0, 255.0 * (mColor.alphaF() * std::min(2.0, mIntensity) * 0.5), 255.0));
        corePaint.setColorFilter(SkColorFilters::Blend(
            SkColorSetARGB(coreA, mColor.red(), mColor.green(), mColor.blue()),
            SkBlendMode::kSrcIn));
        canvas->drawImage(srcTex, 0, 0, &corePaint);
    }

    // 3. Crisp source image on top
    canvas->drawImage(srcTex, 0, 0);
    canvas->flush();

    renderTools.swapTextures();
}

void GlowEffectCaller::processCpu(CpuRenderTools& renderTools,
                                  const CpuRenderData& data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;

    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }

    const int imgWidth = srcBtmp.width();
    const int imgHeight = srcBtmp.height();
    if (imgWidth <= 0 || imgHeight <= 0) return;

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right(), imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom(), imgHeight - 1);

    const int rad = std::max(1, qRound(mRadius));
    const qreal cr = mColor.redF();
    const qreal cg = mColor.greenF();
    const qreal cb = mColor.blueF();

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            qreal bloomR = 0, bloomG = 0, bloomB = 0;
            int samples = 0;

            const int sx[8] = {-rad, rad, 0, 0, -rad/2, rad/2, -rad/2, rad/2};
            const int sy[8] = {0, 0, -rad, rad, -rad/2, -rad/2, rad/2, rad/2};

            for(int s = 0; s < 8; s++) {
                const int px = std::max(0, std::min(imgWidth - 1, xi + sx[s]));
                const int py = std::max(0, std::min(imgHeight - 1, yi + sy[s]));
                const auto smp = static_cast<const uchar*>(srcBtmp.getAddr(px, py));
                const qreal sr = smp[0] / 255.0;
                const qreal sg = smp[1] / 255.0;
                const qreal sb = smp[2] / 255.0;
                const qreal luma = 0.2126 * sr + 0.7152 * sg + 0.0722 * sb;
                const qreal pass = std::max(0.0, luma - mThreshold);
                bloomR += sr * pass;
                bloomG += sg * pass;
                bloomB += sb * pass;
                samples++;
            }

            const qreal factor = (samples > 0) ? (mIntensity / samples) : 0;
            bloomR *= factor * cr;
            bloomG *= factor * cg;
            bloomB *= factor * cb;

            const qreal rOut = std::min(255.0, (r + bloomR * 255.0));
            const qreal gOut = std::min(255.0, (g + bloomG * 255.0));
            const qreal bOut = std::min(255.0, (b + bloomB * 255.0));

            *dst++ = static_cast<uchar>(rOut);
            *dst++ = static_cast<uchar>(gOut);
            *dst++ = static_cast<uchar>(bOut);
            *dst++ = a;
        }
    }
}
