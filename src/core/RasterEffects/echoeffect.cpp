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
               const QVector<qreal>& sampleOpacities,
               const QList<stdsptr<BoxRenderData>>& samples) :
        RasterEffectCaller(hwSupport),
        mSampleOpacities(sampleOpacities),
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

    // per-sample opacity: with motion blur one echo contributes several
    // sub-frame samples that each carry a fraction of the echo's intensity
    const QVector<qreal> mSampleOpacities;
    const QList<stdsptr<BoxRenderData>> mSamples;
};

EchoEffect::EchoEffect() :
    RasterEffect(QObject::tr("残影 (Echo)"),
                 AppSupport::getRasterEffectHardwareSupport("Echo",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::ECHO)
{
    mEchoTime = enve::make_shared<QrealAnimator>(
                -0.033, -999.0, 999.0, 0.001,
                QObject::tr("回声时间"));
    ca_addChild(mEchoTime);

    mEchoCount = enve::make_shared<QrealAnimator>(
                3.0, 0.0, 50.0, 1.0,
                QObject::tr("回声数量"));
    ca_addChild(mEchoCount);

    mStartIntensity = enve::make_shared<QrealAnimator>(
                50.0, 0.0, 100.0, 1.0,
                QObject::tr("起始强度"));
    ca_addChild(mStartIntensity);

    mDecay = enve::make_shared<QrealAnimator>(
                0.5, 0.0, 1.0, 0.01,
                QObject::tr("衰减"));
    ca_addChild(mDecay);

    // 0 = sharp echo copies (the effect's original look); raising it
    // averages each echo over a shutter window so the trails smear
    mMotionBlur = enve::make_shared<QrealAnimator>(
                0.0, 0.0, 100.0, 1.0,
                QObject::tr("动态模糊"));
    ca_addChild(mMotionBlur);

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

// per-thread reentrancy guard: getEffectCaller runs on multiple
// scheduler threads at the same time for the same effect instance; a
// plain member bool lets one thread's assembly pass skip another's
// caller and the echo intermittently vanishes
thread_local bool sAssemblyGuard = false;

stdsptr<RasterEffectCaller> EchoEffect::getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    if(sAssemblyGuard) return nullptr;
    const EchoEffectBlock block(sAssemblyGuard);
    if(!mParentBox) return nullptr;

    const auto scene = mParentBox->getParentScene();
    const qreal fps = scene ? scene->getFps() : 30.0;
    if(qFuzzyIsNull(fps)) return nullptr;

    const qreal echoTime = mEchoTime->getEffectiveValue(relFrame);
    const qreal echoCount = mEchoCount->getEffectiveValue(relFrame)*influence;
    const qreal startIntensity = mStartIntensity->getEffectiveValue(relFrame)*0.01*influence;
    const qreal decay = mDecay->getEffectiveValue(relFrame);
    // 0..1 shutter fraction of one echo step (100% = the echo is smeared
    // over the whole step, i.e. the trail becomes continuous)
    const qreal motionBlur = qBound(0.0,
                mMotionBlur->getEffectiveValue(relFrame)*0.01*influence, 1.0);

    const int nSamples = qCeil(echoCount);
    if(nSamples <= 0) return nullptr;
    if(qFuzzyIsNull(echoTime)) return nullptr;

    const qreal frameStep = echoTime * fps;
    const auto idRange = mParentBox->prp_getIdenticalRelRange(relFrame);

    // how many sub-frame samples each echo averages (0 blur -> a single
    // sample, exactly the previous behaviour)
    const int nSub = motionBlur > 0.001 ?
                qBound(2, int(qCeil(motionBlur*4.0)), 4) : 1;

    QList<stdsptr<BoxRenderData>> samples;
    QVector<qreal> sampleOpacities;
    qreal sampleRelFrame = relFrame;
    for(int i = 0; i < nSamples; i++) {
        sampleRelFrame += frameStep;
        const qreal echoOpacity = startIntensity * qPow(decay, i);
        // the composite skips echoes below this threshold, so sampling
        // them would only cost time
        if(echoOpacity <= 0.001) continue;
        for(int k = 0; k < nSub; k++) {
            // shutter window centred on the echo's own frame
            const qreal t = nSub == 1 ? 0.0 :
                        ((k + 0.5)/nSub - 0.5)*motionBlur;
            const qreal f = sampleRelFrame + t*frameStep;
            // a sub-frame inside the identical range is the current
            // frame's own image (already drawn, opaquely, in front)
            if(idRange.inRange(f)) continue;
            const auto sample = mParentBox->queExternalRender(f, true);
            if(!sample) continue;
            if(sample->finished()) {
                data->fOtherGlobalRects << sample->fGlobalRect;
            } else {
                sample->addDependent(data);
            }
            // the motion-blur hook: the sample's afterProcessing folds its
            // global rect into this data's fOtherGlobalRects, so the host
            // render rect covers the echo. Without it echoes landing
            // outside the current content rect were clipped away - the
            // faster the motion (or the larger the echo time), the more
            // the echo silently disappeared.
            sample->fMotionBlurTarget = data;
            samples << sample;
            sampleOpacities << echoOpacity/nSub;
        }
    }
    if(samples.isEmpty()) return nullptr;
    return enve::make_shared<EchoCaller>(
                instanceHwSupport(), sampleOpacities, samples);
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
    // the shutter window of every echo also reaches outside the echo
    // steps themselves (half of motionBlur*step on each side), so it
    // widens the range the effect depends on
    const qreal motionBlur = qBound(0.0,
                mMotionBlur->getEffectiveValue(relFrame)*0.01, 1.0);
    const qreal marginF = echoCount*qAbs(frameStep) +
                          0.5*motionBlur*qAbs(frameStep);
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
    const int n = qMin(mSamples.count(), mSampleOpacities.count());
    for(int i = 0; i < n; i++) {
        if(mSampleOpacities.at(i) <= 0.001) continue;
        sCompositeSample(mSamples.at(i), mSampleOpacities.at(i),
                         dstPixmap, data);
    }
}
