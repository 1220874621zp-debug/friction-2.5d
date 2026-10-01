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
#include "Animators/boolanimator.h"
#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "ReadWrite/evformat.h"
#include "gpurendertools.h"
#include "appsupport.h"

#include <QScopeGuard>
#include <cmath>
#include <vector>

// boxes whose depth-map sampling is currently on the call stack:
// queExternalRender runs the target's setup SYNCHRONOUSLY, so a depth-map
// cycle (A samples B, B samples A) would recurse forever - a revisit of a
// chained box degrades to the uniform blur instead
namespace {
thread_local QVector<const BoundingBox*> sSampleChain;
}

CameraLensBlurEffect::CameraLensBlurEffect() :
    RasterEffect(QObject::tr("镜头模糊 (Camera Lens Blur)"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "CameraLensBlur", HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::CAMERA_LENS_BLUR)
{
    mRadius = enve::make_shared<QrealAnimator>(15, 0, 250, 0.5,
                                               QObject::tr("半径"));
    ca_addChild(mRadius);
    connect(mRadius.get(), &QrealAnimator::effectiveValueChanged,
            this, &RasterEffect::forcedMarginChanged);
    ca_setGUIProperty(mRadius.get());

    mThreshold = enve::make_shared<QrealAnimator>(0.7, 0.0, 1.0, 0.01,
                                                  QObject::tr("阈值"));
    ca_addChild(mThreshold);

    mGain = enve::make_shared<QrealAnimator>(2.0, 0.0, 10.0, 0.05,
                                             QObject::tr("增益"));
    ca_addChild(mGain);

    // --- depth map driven defocus (AE: Blur Map Layer / Blur Focal
    // Distance / Invert Blur Map), appended in format 53 ---
    mDepthTarget = enve::make_shared<BoxTargetProperty>(
                QObject::tr("深度图"));
    mDepthTarget->setComboPicker(true);
    connect(mDepthTarget.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const box) {
        // live follow: the depth layer animating/changing must invalidate
        // the HOST's render cache (mirrors SetMatteEffect)
        auto& conn = mFollowConn.assign(box);
        if(box) {
            conn << connect(box, &BoundingBox::prp_absFrameRangeChanged,
                            this, [this](const FrameRange&, const bool) {
                prp_afterWholeInfluenceRangeChanged();
            });
        }
        prp_afterWholeInfluenceRangeChanged();
    });
    ca_addChild(mDepthTarget);

    // depth (luminance) that stays sharp - AE "Blur Focal Distance"
    mFocal = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01,
                                              QObject::tr("对焦距离"));
    ca_addChild(mFocal);

    // how fast the blur ramps away from the focal depth (small = deep
    // focus / everything sharp, large = shallow focus)
    mDof = enve::make_shared<QrealAnimator>(2.0, 0.01, 10.0, 0.05,
                                            QObject::tr("景深强度"));
    ca_addChild(mDof);

    mInvert = enve::make_shared<BoolAnimator>(QObject::tr("反转深度"));
    mInvert->setCurrentBoolValue(false);
    ca_addChild(mInvert);
}

int CameraLensBlurEffect::ca_readChildCount(const int evFileVersion) const
{
    // radius / threshold / gain only in older files: the depth map block
    // was appended later (positional child layout)
    return evFileVersion < EvFormat::cameraLensBlurDepth ?
                qMin(3, ca_getNumberOfChildren()) :
                ca_getNumberOfChildren();
}

namespace {

// Pass 1 - defocused base: plain gaussian over the whole frame.
SkPaint lensBasePaint(const float sigma) {
    SkPaint paint;
    if(sigma > 0.01f) {
        paint.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
    }
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
    if(sigma > 0.01f) {
        paint.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
    }
    paint.setBlendMode(SkBlendMode::kScreen);
    return paint;
}

// per-channel "screen" on premultiplied 8-bit values (s + d - s*d/255),
// matching SkBlendMode::kScreen used by the uniform path
inline uint32_t screenPremul(const uint32_t s, const uint32_t d) {
    const auto ch = [](const int a, const int b) {
        return a + b - (a * b + 127) / 255;
    };
    return uint32_t(ch(int((s >> 24) & 0xff), int((d >> 24) & 0xff)) << 24) |
           uint32_t(ch(int((s >> 16) & 0xff), int((d >> 16) & 0xff)) << 16) |
           uint32_t(ch(int((s >> 8) & 0xff), int((d >> 8) & 0xff)) << 8) |
           uint32_t(ch(int(s & 0xff), int(d & 0xff)));
}

inline uint32_t lerpPremul(const uint32_t a, const uint32_t b, const float t) {
    const auto ch = [t](const uint32_t x, const uint32_t y) {
        const float v = float(x & 0xff) * (1.f - t) + float(y & 0xff) * t;
        return uint32_t(qBound(0, int(v + 0.5f), 255));
    };
    return (ch(a >> 24, b >> 24) << 24) | (ch(a >> 16, b >> 16) << 16) |
           (ch(a >> 8, b >> 8) << 8) | ch(a, b);
}

} // namespace

class CameraLensBlurEffectCaller : public RasterEffectCaller {
public:
    CameraLensBlurEffectCaller(const HardwareSupport hwSupport,
                               const qreal radius,
                               const qreal threshold,
                               const qreal gain,
                               stdsptr<BoxRenderData> depth,
                               const qreal focal,
                               const qreal dof,
                               const bool invert);

    void processGpu(QGL33 * const gl, GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;

private:
    void process(SkCanvas * const canvas,
                 const sk_sp<SkImage>& srcImg,
                 const float drawX, const float drawY) const;
    // depth-map mode: per-pixel radius from the depth layer; returns false
    // when the depth pixels are unusable (caller falls back to uniform)
    bool processDepth(CpuRenderTools& renderTools,
                      const CpuRenderData& data) const;

    const float mRadius;
    const float mThreshold;
    const float mGain;
    const stdsptr<BoxRenderData> mDepth;
    const float mFocal;
    const float mDof;
    const bool mInvert;
};

CameraLensBlurEffectCaller::CameraLensBlurEffectCaller(
        const HardwareSupport hwSupport,
        const qreal radius,
        const qreal threshold,
        const qreal gain,
        stdsptr<BoxRenderData> depth,
        const qreal focal,
        const qreal dof,
        const bool invert) :
    RasterEffectCaller(hwSupport, true, QMargins() + qCeil(radius)),
    mRadius(static_cast<float>(radius)),
    mThreshold(static_cast<float>(threshold)),
    mGain(static_cast<float>(gain)),
    mDepth(std::move(depth)),
    mFocal(static_cast<float>(focal)),
    mDof(static_cast<float>(dof)),
    mInvert(invert) {}

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

bool CameraLensBlurEffectCaller::processDepth(
        CpuRenderTools& renderTools, const CpuRenderData& data) const {
    if(!mDepth) return false;
    const auto& depthImg = mDepth->fRenderedImage;
    if(!depthImg) return false;
    const auto depthRaster = depthImg->makeRasterImage();
    SkPixmap depthPix;
    if(!depthRaster || !depthRaster->peekPixels(&depthPix)) return false;

    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;
    if(srcBtmp.empty() || dstBtmp.empty()) return false;

    const int radCeil = std::max(1, static_cast<int>(ceil(mRadius)));
    const auto& tile = data.fTexTile;
    auto srcRect = tile.makeOutset(radCeil, radCeil);
    if(!srcRect.intersect(srcRect, srcBtmp.bounds())) return false;

    // padded source tile (the blur needs the halo outside the tile)
    SkBitmap packedTile;
    if(!packedTile.tryAllocPixels(
                srcBtmp.info().makeWH(srcRect.width(), srcRect.height()))) {
        return false;
    }
    srcBtmp.readPixels(packedTile.info(), packedTile.getPixels(),
                       packedTile.rowBytes(), srcRect.left(), srcRect.top());
    const auto srcImg = SkImage::MakeFromBitmap(packedTile);
    if(!srcImg) return false;

    const float drawX = static_cast<float>(srcRect.left() - tile.left());
    const float drawY = static_cast<float>(srcRect.top() - tile.top());

    const int w = tile.width();
    const int h = tile.height();
    if(w <= 0 || h <= 0) return false;

    // sigma levels 0 .. sigmaMax; the per-pixel depth picks a fraction and
    // the two neighbouring levels are blended (compound-blur style)
    const int nLevels = 4;
    const float sigmaMax = std::max(0.05f, mRadius * 0.3333333f);
    const auto info = dstBtmp.info().makeWH(w, h);

    const bool highlights = mGain > 0.001f && mThreshold < 0.999f;
    const float thr = qBound(0.0f, mThreshold, 0.999f);
    const float slope = std::min(100.0f, mGain / (1.0f - thr));
    const float offset = -slope * thr;

    std::vector<SkBitmap> baseLevels(static_cast<size_t>(nLevels));
    std::vector<SkBitmap> hiLevels(highlights ?
                                       static_cast<size_t>(nLevels) :
                                       static_cast<size_t>(0));
    for(int i = 0; i < nLevels; i++) {
        const float sigma = sigmaMax * float(i) / float(nLevels - 1);
        if(!baseLevels[size_t(i)].tryAllocPixels(info)) return false;
        baseLevels[size_t(i)].eraseColor(SK_ColorTRANSPARENT);
        {
            SkCanvas c(baseLevels[size_t(i)]);
            auto paint = lensBasePaint(sigma);
            c.drawImage(srcImg, drawX, drawY, &paint);
        }
        if(!highlights) continue;
        if(!hiLevels[size_t(i)].tryAllocPixels(info)) return false;
        hiLevels[size_t(i)].eraseColor(SK_ColorTRANSPARENT);
        {
            SkCanvas c(hiLevels[size_t(i)]);
            auto paint = lensHighlightPaint(sigma, slope, offset);
            // the level bitmap is screened onto the base later; drawing it
            // here must stay plain source-over
            paint.setBlendMode(SkBlendMode::kSrcOver);
            c.drawImage(srcImg, drawX, drawY, &paint);
        }
    }

    // depth origin in THIS layer's image coordinates (same conversion the
    // track matte / motion blur callers use for their samples)
    const QPoint depthOff = mDepth->fGlobalRect.topLeft() - data.fPos;
    const int depthW = depthPix.width();
    const int depthH = depthPix.height();
    const auto* depthPix0 = static_cast<const uint32_t*>(depthPix.addr32());
    const int depthStride = int(depthPix.rowBytes() / 4);

    SkBitmap outBmp;
    if(!outBmp.tryAllocPixels(info)) return false;
    for(int y = 0; y < h; y++) {
        const int dy = tile.top() + y - depthOff.y();
        const uint32_t* depthRow = (dy >= 0 && dy < depthH) ?
                    depthPix0 + qint64(dy) * depthStride : nullptr;
        auto* outRow = outBmp.getAddr32(0, y);
        for(int x = 0; x < w; x++) {
            float d = 0.f;
            const int dx = tile.left() + x - depthOff.x();
            if(depthRow && dx >= 0 && dx < depthW) {
                const SkColor c = depthRow[dx];
                d = (0.2126f * SkColorGetR(c) +
                     0.7152f * SkColorGetG(c) +
                     0.0722f * SkColorGetB(c)) / 255.f;
            }
            if(mInvert) d = 1.f - d;
            const float weight = qBound(0.f,
                                        std::fabs(d - mFocal) * mDof,
                                        1.f);
            const float f = weight * float(nLevels - 1);
            int i0 = int(f);
            if(i0 > nLevels - 2) i0 = nLevels - 2;
            const float k = f - float(i0);
            const uint32_t b0 = *baseLevels[size_t(i0)].getAddr32(x, y);
            const uint32_t b1 = *baseLevels[size_t(i0 + 1)].getAddr32(x, y);
            uint32_t outC = lerpPremul(b0, b1, k);
            if(highlights) {
                const uint32_t h0 = *hiLevels[size_t(i0)].getAddr32(x, y);
                const uint32_t h1 = *hiLevels[size_t(i0 + 1)].getAddr32(x, y);
                outC = screenPremul(lerpPremul(h0, h1, k), outC);
            }
            outRow[x] = outC;
        }
    }

    SkCanvas canvas(renderTools.fDstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.drawBitmap(outBmp, 0, 0);
    return true;
}

void CameraLensBlurEffectCaller::processGpu(QGL33 * const gl,
                                            GpuRenderTools& renderTools) {
    Q_UNUSED(gl)

    renderTools.switchToSkia();
    const auto canvas = renderTools.requestTargetCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const auto srcTex = renderTools.requestSrcTextureImageWrapper();
    if (!srcTex) return;

    // the depth-map mode is CPU only (getEffectCaller reports cpuOnly when
    // a depth layer is attached), so this path stays the uniform blur
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

    // depth map attached: the blur radius follows the depth layer's
    // luminance per pixel; falls back to the uniform blur when its pixels
    // cannot be read (evicted / still loading) - an empty layer would be
    // worse than an unfocused one
    if (mDepth && processDepth(renderTools, data)) { return; }

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
    const qreal radius = mRadius->getEffectiveValue(relFrame)*resolution*influence;
    if (radius < 0.001) return nullptr;

    const qreal threshold = mThreshold->getEffectiveValue(relFrame);
    const qreal gain = mGain->getEffectiveValue(relFrame)*influence;

    // depth map mode: queue the depth layer for an independent render and
    // let its pixels drive the per-pixel blur radius. The dependency
    // delays this box's effects phase until the sample finished (the
    // SetMatte / track-matte queExternalRender pattern). CPU only: the
    // level pyramid blend needs the depth pixels on the CPU.
    stdsptr<BoxRenderData> depthSample;
    auto hwSupport = instanceHwSupport();
    const auto target = mDepthTarget ? mDepthTarget->getTarget() : nullptr;
    if (target && data) {
        const auto parentBox = data->fParentBox.data();
        // self, own subtree and own ancestors would recurse through the
        // group's synchronous child setup - degrade to the uniform blur
        const bool usable = parentBox && target != parentBox &&
                !parentBox->isAncestor(target) &&
                !target->isAncestor(parentBox) &&
                !sSampleChain.contains(target);
        if (usable) {
            sSampleChain.append(parentBox);
            const auto guard = qScopeGuard([]() { sSampleChain.removeLast(); });
            // relFrame is the HOST's relative frame; the depth layer has
            // its own trim/start - convert through absolute scene frames
            const qreal absFrame = parentBox->prp_relFrameToAbsFrameF(relFrame);
            const qreal tRel = target->prp_absFrameToRelFrameF(absFrame);
            depthSample = target->queExternalRender(tRel, true);
            if (depthSample) depthSample->addDependent(data);
            hwSupport = HardwareSupport::cpuOnly;
        }
    }

    return enve::make_shared<CameraLensBlurEffectCaller>(
                hwSupport, radius, threshold, gain, std::move(depthSample),
                mFocal->getEffectiveValue(relFrame),
                mDof->getEffectiveValue(relFrame)*influence,
                mInvert->getBoolValue(relFrame));
}

FrameRange CameraLensBlurEffect::prp_getIdenticalRelRange(
        const int relFrame) const {
    const auto thisIdent = ComplexAnimator::prp_getIdenticalRelRange(relFrame);
    const auto target = mDepthTarget ? mDepthTarget->getTarget() : nullptr;
    if(!target) return thisIdent;
    // the host's rendered pixels depend on the depth layer's content: a
    // static host under an animated depth map is NOT frame-identical
    // (same pattern as SetMatteEffect); the chain guard breaks cycles
    static thread_local QVector<const BoundingBox*> sIdentChain;
    if(sIdentChain.contains(target)) return thisIdent;
    sIdentChain.append(target);
    const auto guard = qScopeGuard([]() { sIdentChain.removeLast(); });
    const int absFrame = prp_relFrameToAbsFrame(relFrame);
    const int tRelFrame = target->prp_absFrameToRelFrame(absFrame);
    const auto targetIdent = target->prp_getIdenticalRelRange(tRelFrame);
    const auto absTargetIdent = target->prp_relRangeToAbsRange(targetIdent);
    return thisIdent*prp_absRangeToRelRange(absTargetIdent);
}

QMargins CameraLensBlurEffect::getMargin() const {
    return QMargins() + qCeil(mRadius->getEffectiveValue());
}
