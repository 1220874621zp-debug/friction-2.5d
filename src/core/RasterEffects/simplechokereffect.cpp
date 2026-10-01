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

#include "simplechokereffect.h"
#include "gpurendertools.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

#include <QtMath>

namespace {

// blur the premultiplied input, then re-level the alpha channel:
//   choke  (c > 0): a' = clamp((a - c) / (1 - c))  - black point rises
//   spread (c < 0): a' = clamp(a / (1 - c))        - white point drops
// the 50% edge crossing moves by roughly |u| pixels for remap factor
// c = |u| / (sigma * 1.6), so the slider reads close to pixel units;
// the blurred premul rgb is rescaled onto the new alpha to keep the
// bitmap valid premultiplied (no color fringes where alpha died)
void chokeBlur(SkBitmap& bmp,
               const qreal sigma, const qreal c, const bool choke)
{
    const int w = bmp.width();
    const int h = bmp.height();
    if (w <= 0 || h <= 0) return;

    SkBitmap blurred;
    blurred.allocPixels(bmp.info().makeAlphaType(kPremul_SkAlphaType));
    if (blurred.empty()) return;
    blurred.eraseColor(SK_ColorTRANSPARENT);

    {
        SkCanvas canvas(blurred);
        SkPaint bp;
        if (sigma > 0.01) {
            bp.setImageFilter(SkImageFilters::Blur(
                        static_cast<SkScalar>(sigma),
                        static_cast<SkScalar>(sigma), nullptr));
        }
        canvas.drawBitmap(bmp, 0, 0, sigma > 0.01 ? &bp : nullptr);
    }

    const float fc = static_cast<float>(c);
    const float gain = 1.f / std::max(1e-4f, 1.f - fc);
    const size_t rowBytes = blurred.rowBytes();
    auto addr = static_cast<uchar*>(blurred.getPixels());
    for (int y = 0; y < h; y++) {
        auto px = addr + y * rowBytes;
        for (int x = 0; x < w; x++, px += 4) {
            // kN32 little-endian: b, g, r, a in memory order
            const float a01 = px[3] / 255.f;
            float outA = choke ? (a01 - fc) * gain : a01 * gain;
            outA = qBound(0.f, outA, 1.f);
            const uchar newA = static_cast<uchar>(outA * 255.f + 0.5f);
            if (newA != px[3] && px[3] > 0) {
                const float s = static_cast<float>(newA) / px[3];
                px[0] = static_cast<uchar>(qMin(255.f, px[0] * s + 0.5f));
                px[1] = static_cast<uchar>(qMin(255.f, px[1] * s + 0.5f));
                px[2] = static_cast<uchar>(qMin(255.f, px[2] * s + 0.5f));
            }
            px[3] = newA;
        }
    }

    blurred.swap(bmp);
}

} // namespace

class SimpleChokerEffectCaller : public RasterEffectCaller {
public:
    SimpleChokerEffectCaller(const HardwareSupport hwSupport,
                             const qreal sigma, const qreal c,
                             const bool choke, const QMargins& margin) :
        RasterEffectCaller(hwSupport, true, margin),
        mSigma(sigma), mC(c), mChoke(choke) {}

    void processGpu(QGL33 * const gl,
                    GpuRenderTools& renderTools);
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
private:
    const qreal mSigma;
    const qreal mC;
    const bool mChoke;
};

SimpleChokerEffect::SimpleChokerEffect() :
    RasterEffect(QObject::tr("简单阻塞 (Simple Choker)"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "SimpleChoker", HardwareSupport::gpuPreffered),
                 false,
                 RasterEffectType::SIMPLE_CHOKER)
{
    // AE sign convention: positive chokes the matte inward, negative
    // spreads it outward; ~1 slider unit moves the edge ~1 px
    mChokeMatte = enve::make_shared<QrealAnimator>(
                0.0, -10.0, 10.0, 0.1, QObject::tr("阻塞遮罩"));
    ca_addChild(mChokeMatte);
}

stdsptr<RasterEffectCaller>
SimpleChokerEffect::getEffectCaller(const qreal relFrame,
                                    const qreal resolution,
                                    const qreal influence,
                                    BoxRenderData * const data) const
{
    Q_UNUSED(influence)
    Q_UNUSED(data)

    const qreal chokeRaw = mChokeMatte->getEffectiveValue(relFrame);
    if (std::isnan(chokeRaw) || std::isinf(chokeRaw)) return nullptr;

    // AE: a choke of 0 leaves the matte untouched - skip entirely so
    // the pass-through does not even soften the edge with the blur
    if (qAbs(chokeRaw) < 0.001) return nullptr;

    const qreal u = qBound(-10.0, chokeRaw, 10.0) * resolution;
    const qreal sigma = qBound(0.75, 0.75 + 0.4 * qAbs(u), 24.0);
    const qreal c = qBound(0.0, qAbs(u) / (sigma * 1.6), 0.96);

    const int m = qMin(96, qCeil(sigma * 3.0));

    return enve::make_shared<SimpleChokerEffectCaller>(
                instanceHwSupport(), sigma, c, u > 0,
                QMargins(m, m, m, m));
}

void SimpleChokerEffectCaller::processGpu(QGL33 * const gl,
                                          GpuRenderTools &renderTools)
{
    Q_UNUSED(gl)

    renderTools.switchToSkia();
    const auto canvas = renderTools.requestTargetCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const auto srcTex = renderTools.requestSrcTextureImageWrapper();
    const int w = srcTex->width();
    const int h = srcTex->height();

    SkBitmap bmp;
    bmp.allocN32Pixels(w, h);
    bmp.eraseColor(SK_ColorTRANSPARENT);
    if (w > 0 && h > 0 &&
        srcTex->readPixels(bmp.info(), bmp.getPixels(),
                           bmp.rowBytes(), 0, 0)) {
        chokeBlur(bmp, mSigma, mC, mChoke);
        canvas->drawBitmap(bmp, 0, 0);
    }
    canvas->flush();

    renderTools.swapTextures();
}

void SimpleChokerEffectCaller::processCpu(CpuRenderTools &renderTools,
                                          const CpuRenderData &data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;

    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }

    SkCanvas canvas(dstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);

    const auto& texTile = data.fTexTile;
    const int margin = qCeil(mSigma * 3.0);
    const auto srcRect = texTile.makeOutset(margin, margin);

    auto srcArea = srcRect;
    if (!srcArea.intersect(srcBtmp.bounds())) return;

    SkBitmap packedTile;
    packedTile.allocPixels(srcBtmp.info().makeWH(srcArea.width(),
                                                 srcArea.height()));
    if (!srcBtmp.readPixels(packedTile.info(),
                            packedTile.getPixels(),
                            packedTile.rowBytes(),
                            srcArea.left(),
                            srcArea.top())) return;

    chokeBlur(packedTile, mSigma, mC, mChoke);

    canvas.drawBitmap(packedTile,
                      srcArea.left() - texTile.left(),
                      srcArea.top() - texTile.top());
}
