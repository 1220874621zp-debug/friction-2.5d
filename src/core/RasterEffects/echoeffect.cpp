/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-Andre Rodlie and contributors
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

#include "echoeffect.h"

#include "Boxes/boxrenderdata.h"
#include "Boxes/boundingbox.h"
#include "canvas.h"
#include "appsupport.h"

class EchoCaller : public RasterEffectCaller {
    e_OBJECT
    friend class StdSelfRef;
    EchoCaller(const HardwareSupport hwSupport,
               const qreal startIntensity,
               const qreal decay,
               const QList<stdsptr<BoxRenderData>>& samples) :
        RasterEffectCaller(hwSupport),
        mStartIntensity(startIntensity),
        mDecay(decay),
        mSamples(samples) {}
public:
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData &data);

    bool srcDstSeparation() const { return false; }
private:
    static void sCompositeSample(const stdsptr<BoxRenderData>& sample,
                                 const qreal sampleOpacity,
                                 SkPixmap& dstPixmap,
                                 const CpuRenderData& data);

    const qreal mStartIntensity;
    const qreal mDecay;
    const QList<stdsptr<BoxRenderData>> mSamples;
};

EchoEffect::EchoEffect() :
    RasterEffect(QObject::tr("Echo"),
                 AppSupport::getRasterEffectHardwareSupport("Echo",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::ECHO)
{
    mEchoTime = enve::make_shared<QrealAnimator>(
                -0.033, -999.0, 999.0, 0.001,
                QObject::tr("echo time"));
    ca_addChild(mEchoTime);

    mEchoCount = enve::make_shared<QrealAnimator>(
                3.0, 0.0, 50.0, 1.0,
                QObject::tr("echo count"));
    ca_addChild(mEchoCount);

    mStartIntensity = enve::make_shared<QrealAnimator>(
                50.0, 0.0, 100.0, 1.0,
                QObject::tr("start intensity"));
    ca_addChild(mStartIntensity);

    mDecay = enve::make_shared<QrealAnimator>(
                0.5, 0.0, 1.0, 0.01,
                QObject::tr("decay"));
    ca_addChild(mDecay);

    connect(this, &Property::prp_parentChanged,
            this, [this]() {
        mParentBox = getFirstAncestor<BoundingBox>();
    });
}

class EchoEffectBlock {
public:
    EchoEffectBlock(bool& block) : mBlock(block) { mBlock = true; }
    ~EchoEffectBlock() { mBlock = false; }
private:
    bool& mBlock;
};

stdsptr<RasterEffectCaller> EchoEffect::getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    if(mBlocked) return nullptr;
    const EchoEffectBlock block(mBlocked);
    if(!mParentBox) return nullptr;

    const auto scene = mParentBox->getParentScene();
    const qreal fps = scene ? scene->getFps() : 30.0;
    if(qFuzzyIsNull(fps)) return nullptr;

    const qreal echoTime = mEchoTime->getEffectiveValue(relFrame);
    const qreal echoCount = mEchoCount->getEffectiveValue(relFrame)*influence;
    const qreal startIntensity = mStartIntensity->getEffectiveValue(relFrame)*0.01*influence;
    const qreal decay = mDecay->getEffectiveValue(relFrame);

    const int nSamples = qCeil(echoCount);
    if(nSamples <= 0) return nullptr;
    if(qFuzzyIsNull(echoTime)) return nullptr;

    const qreal frameStep = echoTime * fps;
    const auto idRange = mParentBox->prp_getIdenticalRelRange(relFrame);

    QList<stdsptr<BoxRenderData>> samples;
    qreal sampleRelFrame = relFrame;
    for(int i = 0; i < nSamples; i++) {
        sampleRelFrame += frameStep;
        if(!idRange.inRange(sampleRelFrame)) {
            const auto sample = mParentBox->queExternalRender(sampleRelFrame, true);
            if(sample) {
                if(sample->finished()) {
                    data->fOtherGlobalRects << sample->fGlobalRect;
                } else {
                    sample->addDependent(data);
                }
                samples << sample;
            }
        }
    }
    if(samples.isEmpty()) return nullptr;
    return enve::make_shared<EchoCaller>(
                instanceHwSupport(), startIntensity, decay, samples);
}

FrameRange EchoEffect::getEchoPropsIdenticalRange(const int relFrame) const
{
    auto range = mParentBox ? mParentBox->getMotionBlurIdenticalRange(relFrame, true)
                            : FrameRange::EMINMAX;
    if(range == FrameRange::EMINMAX) return range;

    const auto scene = mParentBox ? mParentBox->getParentScene() : nullptr;
    const qreal fps = scene ? scene->getFps() : 30.0;
    const qreal echoTime = mEchoTime->getEffectiveValue(relFrame);
    const qreal echoCount = mEchoCount->getEffectiveValue(relFrame);
    const qreal frameStep = echoTime * fps;
    const qreal marginF = echoCount * qAbs(frameStep);
    const int margin = qCeil(marginF);
    if(margin == 0) return range;
    const int positive = frameStep > 0 ? margin : 0;
    const int negative = frameStep < 0 ? margin : 0;
    if(relFrame - range.fMin < positive ||
       range.fMax - relFrame < negative) return {relFrame, relFrame};
    if(range.fMin == FrameRange::EMIN) {
        range.fMax -= negative;
    } else if(range.fMax == FrameRange::EMAX) {
        range.fMin += positive;
    } else {
        range = {qMin(relFrame, range.fMin + positive),
                 qMax(relFrame, range.fMax - negative)};
    }
    return range;
}

FrameRange EchoEffect::prp_getIdenticalRelRange(const int relFrame) const {
    const auto propsRange = getEchoPropsIdenticalRange(relFrame);
    const auto effectRange = RasterEffect::prp_getIdenticalRelRange(relFrame);
    return propsRange*effectRange;
}

static void echoCompositeBehind(const int x0, const int y0,
                                SkPixmap& dst,
                                const SkPixmap& src,
                                const qreal opacity) {
    // "behind" operator: sample slides under whatever is already in dst
    // dst' = dst + src*opacity*(1 - dstA), applied per pixel, premultiplied
    uint8_t *dstD = static_cast<uint8_t*>(dst.writable_addr());
    const uint8_t *srcD = static_cast<const uint8_t*>(src.addr());
    const int dstRowWidth = dst.rowBytesAsPixels();
    const int srcRowWidth = src.rowBytesAsPixels();

    int dstId = (y0 * dstRowWidth + x0)*4;
    int srcId = 0;
    const int yMax = qMin(src.height(), dst.height() - y0);
    const int xMax = qMin(src.width(), dst.width() - x0);
    const int iDstYInc = qMax(0, dstRowWidth - xMax)*4;
    const int iSrcYInc = qMax(0, srcRowWidth - xMax)*4;
    for(int y = 0; y < yMax; y++) {
        for(int x = 0; x < xMax; x++) {
            const int maxSrcId = srcRowWidth*src.height()*4;
            Q_ASSERT(srcId + 3 < maxSrcId);
            const int maxDstId = dstRowWidth*dst.height()*4;
            Q_ASSERT(dstId + 3 < maxDstId);

            const qreal dstA = dstD[dstId + 3] / 255.0;
            const qreal srcA = srcD[srcId + 3] / 255.0 * opacity;
            const qreal gap = 1.0 - dstA;
            if(gap > 0.0001 && srcA > 0.0001) {
                dstD[dstId]     = static_cast<uint8_t>(qBound(0, qRound(dstD[dstId]     + srcD[srcId]     * opacity * gap), 255));
                dstD[dstId + 1] = static_cast<uint8_t>(qBound(0, qRound(dstD[dstId + 1] + srcD[srcId + 1] * opacity * gap), 255));
                dstD[dstId + 2] = static_cast<uint8_t>(qBound(0, qRound(dstD[dstId + 2] + srcD[srcId + 2] * opacity * gap), 255));
                dstD[dstId + 3] = static_cast<uint8_t>(qBound(0, qRound(255.0 * (dstA + srcA * gap)), 255));
            }

            dstId += 4;
            srcId += 4;
        }
        dstId += iDstYInc;
        srcId += iSrcYInc;
    }
}

void EchoCaller::sCompositeSample(const stdsptr<BoxRenderData>& sample,
                                  const qreal sampleOpacity,
                                  SkPixmap& dstPixmap,
                                  const CpuRenderData& data) {
    const auto& srcImg = sample->fRenderedImage;
    if(!srcImg) return;
    const auto rasterImg = srcImg->makeRasterImage();
    if(!rasterImg) return;
    SkPixmap samplePixmap;
    if(!rasterImg->peekPixels(&samplePixmap)) return;
    QPoint offset = sample->fGlobalRect.topLeft() - data.fPos;
    SkPixmap samplePart;
    const auto sampleTile = data.fTexTile.makeOffset(-offset.x(), -offset.y());
    if(!samplePixmap.extractSubset(&samplePart, sampleTile)) return;
    const int drawX = sampleTile.x() < 0 ? offset.x() - data.fTexTile.x() : 0;
    const int drawY = sampleTile.y() < 0 ? offset.y() - data.fTexTile.y() : 0;
    echoCompositeBehind(drawX, drawY, dstPixmap, samplePart, sampleOpacity);
}

void EchoCaller::processCpu(CpuRenderTools& renderTools,
                            const CpuRenderData &data) {
    auto& dstBtmp = renderTools.fDstBtmp;
    if(dstBtmp.empty()) return;

    SkPixmap dstPixmap;
    if(!dstBtmp.peekPixels(&dstPixmap)) return;

    // newest echo slides in first (immediately under the current frame),
    // each older echo stacks further behind - "behind" compositing
    for(int i = 0; i < mSamples.count(); i++) {
        const qreal sampleOpacity = mStartIntensity * qPow(mDecay, i);
        if(sampleOpacity <= 0.001) continue;
        sCompositeSample(mSamples.at(i), sampleOpacity, dstPixmap, data);
    }
}
