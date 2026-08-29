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

#include "dropshadoweffect.h"
#include "gpurendertools.h"
#include "rastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"
#include <QtMath>

DropShadowEffect::DropShadowEffect() :
    RasterEffect("drop shadow",
                 AppSupport::getRasterEffectHardwareSupport("Drop Shadow",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::DROP_SHADOW)
{
    mDistance = enve::make_shared<QrealAnimator>(15.0, 0.0, 500.0, 1.0, "distance");
    ca_addChild(mDistance);

    mAngle = enve::make_shared<QrealAnimator>(135.0, 0.0, 360.0, 1.0, "angle");
    ca_addChild(mAngle);

    mSoftness = enve::make_shared<QrealAnimator>(10.0, 0.0, 200.0, 0.5, "softness");
    ca_addChild(mSoftness);

    mOpacity = enve::make_shared<QrealAnimator>(75.0, 0.0, 100.0, 1.0, "opacity");
    ca_addChild(mOpacity);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(0, 0, 0, 255));
    ca_addChild(mColor);
}

class DropShadowEffectCaller : public RasterEffectCaller {
public:
    DropShadowEffectCaller(const HardwareSupport hwSupport,
                           const QPointF& offset,
                           const qreal softness,
                           const qreal opacity,
                           const QColor& color,
                           const QMargins& margins) :
        RasterEffectCaller(hwSupport, true, margins),
        mOffset(offset),
        mSoftness(softness),
        mOpacity(opacity),
        mColor(color) {}

    void processGpu(QGL33 * const gl, GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;

private:
    const QPointF mOffset;
    const qreal mSoftness;
    const qreal mOpacity;
    const QColor mColor;
};

stdsptr<RasterEffectCaller> DropShadowEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(data)

    const qreal dist = mDistance->getEffectiveValue(relFrame) * resolution * influence;
    const qreal ang = qDegreesToRadians(mAngle->getEffectiveValue(relFrame));
    const QPointF offset(std::cos(ang) * dist, std::sin(ang) * dist);
    const qreal softness = mSoftness->getEffectiveValue(relFrame) * resolution;
    const qreal opacity = (mOpacity->getEffectiveValue(relFrame) / 100.0) * influence;
    const QColor color = mColor->getColor(relFrame);

    const int iL = qMax(0, qCeil(softness * 2.0 - offset.x() + 5));
    const int iT = qMax(0, qCeil(softness * 2.0 - offset.y() + 5));
    const int iR = qMax(0, qCeil(softness * 2.0 + offset.x() + 5));
    const int iB = qMax(0, qCeil(softness * 2.0 + offset.y() + 5));

    return enve::make_shared<DropShadowEffectCaller>(
                instanceHwSupport(), offset, softness, opacity, color,
                QMargins(iL, iT, iR, iB));
}

void DropShadowEffectCaller::processGpu(QGL33 * const gl, GpuRenderTools &renderTools)
{
    Q_UNUSED(gl)

    renderTools.switchToSkia();
    const auto canvas = renderTools.requestTargetCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const auto srcTex = renderTools.requestSrcTextureImageWrapper();
    if (!srcTex) return;

    // 1. Draw shadow with offset and blur
    SkPaint shadowPaint;
    const float sigma = std::max(0.1f, static_cast<float>(mSoftness * 0.45));
    shadowPaint.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
    const uint8_t alpha = static_cast<uint8_t>(qBound(0.0, 255.0 * (mColor.alphaF() * mOpacity), 255.0));
    shadowPaint.setColorFilter(SkColorFilters::Blend(
        SkColorSetARGB(alpha, mColor.red(), mColor.green(), mColor.blue()),
        SkBlendMode::kSrcIn));

    canvas->drawImage(srcTex, mOffset.x(), mOffset.y(), &shadowPaint);

    // 2. Draw original layer at origin
    canvas->drawImage(srcTex, 0, 0);
    canvas->flush();

    renderTools.swapTextures();
}

void DropShadowEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int offX = qRound(mOffset.x());
    const int offY = qRound(mOffset.y());
    const qreal cr = mColor.redF();
    const qreal cg = mColor.greenF();
    const qreal cb = mColor.blueF();
    const qreal ca = mColor.alphaF() * mOpacity;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const int sx = std::max(0, std::min(imgWidth - 1, xi - offX));
            const int sy = std::max(0, std::min(imgHeight - 1, yi - offY));
            const auto shadowSmp = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));
            const qreal sAlpha = (shadowSmp[3] / 255.0) * ca;

            const qreal srcA = a / 255.0;
            const qreal outA = srcA + sAlpha * (1.0 - srcA);
            qreal outR = (r / 255.0) * srcA + cr * sAlpha * (1.0 - srcA);
            qreal outG = (g / 255.0) * srcA + cg * sAlpha * (1.0 - srcA);
            qreal outB = (b / 255.0) * srcA + cb * sAlpha * (1.0 - srcA);

            if (outA > 0.001) {
                outR /= outA;
                outG /= outA;
                outB /= outA;
            }

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outR * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outG * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outB * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outA * 255.0)));
        }
    }
}
