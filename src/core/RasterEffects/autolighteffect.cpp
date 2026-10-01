#include "autolighteffect.h"

#include "rastereffectcaller.h"
#include "autolightalgo.h"

#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "Animators/staticcomplexanimator.h"
#include "Animators/qpointfanimator.h"
#include "Properties/comboboxproperty.h"
#include "Properties/boolproperty.h"
#include "MovablePoints/pointshandler.h"
#include "RasterEffects/effectcanvaspoint.h"
#include "skia/skqtconversions.h"
#include "skia/skiaincludes.h"

#include "appsupport.h"

#include <QMutex>
#include <QMutexLocker>
#include <QScopeGuard>
#include <algorithm>
#include <vector>

namespace {

// boxes whose depth sampling is currently on the call stack: a cycle
// (A lights with B, B lights with A) would recurse forever through
// the synchronous external render setup
thread_local QVector<const BoundingBox*> sSampleChain;

// convert skia's premultiplied N32 bytes to the straight-alpha packed
// 0xAARRGGBB colors the algorithm expects. Byte order: this Skia is
// built for Windows (SK_BUILD_FOR_WIN -> SK_R32_SHIFT = 16,
// SkTypes.h), so N32 memory order is [B, G, R, A]. celvolume and
// colorize read byte0 as red - a latent R/B swap on their injected
// colors (masked by their purple-ish palettes / green-centric
// keying); do NOT "fix" this file to match them.
inline uint32_t unpackPixel(const uchar* const p)
{
    const uint32_t a = p[3];
    if (a == 255 || a == 0) {
        return (a << 24) | (uint32_t(p[2]) << 16)
                | (uint32_t(p[1]) << 8) | uint32_t(p[0]);
    }
    const uint32_t r = uint32_t(p[2]) * 255u / a;
    const uint32_t g = uint32_t(p[1]) * 255u / a;
    const uint32_t b = uint32_t(p[0]) * 255u / a;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline void packPixel(const uint32_t c, uchar* const p)
{
    const uint32_t a = (c >> 24) & 255u;
    p[0] = uchar((((c & 255u)) * a + 127u) / 255u);
    p[1] = uchar(((((c >> 8) & 255u)) * a + 127u) / 255u);
    p[2] = uchar(((((c >> 16) & 255u)) * a + 127u) / 255u);
    p[3] = uchar(a);
}

} // namespace

class AutoLightEffectCaller : public RasterEffectCaller {
public:
    AutoLightEffectCaller(const HardwareSupport hwSupport,
                          const autolight::Params& p,
                          stdsptr<BoxRenderData> depthSample,
                          const qreal hostRes) :
        RasterEffectCaller(hwSupport, false, QMargins()),
        mP(p), mDepthSample(std::move(depthSample)), mHostRes(hostRes) {}

    // whole-image pass (segmentation / distance fields have no tile
    // locality), computed once per caller and shared by every tile
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override {
        const auto& srcBtmp = renderTools.fSrcBtmp;
        const auto& dstBtmp = renderTools.fDstBtmp;
        if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
            dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
            return;
        }

        QMutexLocker lock(&mMutex);
        if (!mComputed) {
            const int w = srcBtmp.width();
            const int h = srcBtmp.height();
            const size_t n = size_t(w) * h;
            mResult.allocN32Pixels(w, h);
            std::vector<uint32_t> src(n);
            for (int y = 0; y < h; y++) {
                const auto row = static_cast<const uchar*>(
                            srcBtmp.getAddr(0, y));
                for (int x = 0; x < w; x++) {
                    src[size_t(y) * w + x] = unpackPixel(row + x * 4);
                }
            }

            // rasterize the depth sample into the host image frame so
            // both grids share one coordinate system (the track-matte
            // placement math plus the liquid-glass resolution bridge)
            std::vector<float> depth;
            if (mDepthSample && mDepthSample->fRenderedImage) {
                const auto& s = *mDepthSample;
                const auto img = s.fRenderedImage->makeRasterImage();
                SkPixmap dPix;
                if (img && img->peekPixels(&dPix) && dPix.width() > 0
                    && dPix.height() > 0) {
                    SkBitmap dBmp;
                    dBmp.allocPixels(SkImageInfo::MakeN32Premul(w, h));
                    // neutral gray where the depth layer has no pixels:
                    // zero gradient, mid falloff
                    dBmp.eraseColor(0xFF808080);
                    SkCanvas dc(dBmp);
                    SkMatrix m = SkMatrix::MakeTrans(
                                -qreal(data.fPos.x()), -qreal(data.fPos.y()));
                    const qreal sRes = s.fResolution > 0. ? s.fResolution : 1.;
                    const qreal hRes = mHostRes > 0. ? mHostRes : 1.;
                    m.postScale(qreal(hRes / sRes), qreal(hRes / sRes));
                    dc.setMatrix(m);
                    SkPaint dp;
                    dp.setFilterQuality(kLow_SkFilterQuality);
                    if (s.fUseRenderTransform) {
                        dc.concat(toSkMatrix(s.fRenderTransform));
                        dc.drawImageRect(s.fRenderedImage,
                                         toSkRect(s.fRelBoundingRect), &dp);
                    } else {
                        dc.drawImage(s.fRenderedImage,
                                     s.fGlobalRect.x(),
                                     s.fGlobalRect.y(), &dp);
                    }
                    dc.flush();

                    depth.resize(n);
                    for (int y = 0; y < h; y++) {
                        const auto row = static_cast<const uchar*>(
                                    dBmp.getAddr(0, y));
                        for (int x = 0; x < w; x++) {
                            // N32 memory order is [B, G, R, A] on
                            // Windows (see unpackPixel above); R
                            // carries gray depth
                            const float r = float(row[x * 4 + 2]) / 255.f;
                            const float g = float(row[x * 4 + 1]) / 255.f;
                            const float b = float(row[x * 4 + 0]) / 255.f;
                            depth[size_t(y) * w + x] =
                                    (std::abs(r - g) < 0.008f
                                     && std::abs(g - b) < 0.008f)
                                        ? r : autolight::jetDecode(r, g, b);
                        }
                    }
                }
            }

            std::vector<uint32_t> dst(n);
            autolight::compute(src.data(), depth.empty() ? nullptr
                                                         : depth.data(),
                               w, h, mP, dst.data());
            for (int y = 0; y < h; y++) {
                const auto row = static_cast<uchar*>(mResult.getAddr(0, y));
                for (int x = 0; x < w; x++) {
                    packPixel(dst[size_t(y) * w + x], row + x * 4);
                }
            }
            mComputed = true;
        }
        lock.unlock();

        // copy this tile's slice of the full-image result
        const auto& tile = data.fTexTile;
        const int xMin = std::max(0, tile.left());
        const int xMax = std::min(tile.right() - 1, mResult.width() - 1);
        const int yMin = std::max(0, tile.top());
        const int yMax = std::min(tile.bottom() - 1, mResult.height() - 1);
        for (int yi = yMin; yi <= yMax; yi++) {
            memcpy(dstBtmp.getAddr(0, yi - yMin),
                   mResult.getAddr(xMin, yi),
                   static_cast<size_t>(xMax - xMin + 1) * 4);
        }
    }
private:
    const autolight::Params mP;
    const stdsptr<BoxRenderData> mDepthSample;
    const qreal mHostRes;
    QMutex mMutex;
    bool mComputed = false;
    SkBitmap mResult;
};

AutoLightEffect::AutoLightEffect() :
    RasterEffect(QStringLiteral("自动打光 (Auto Light)"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "AutoLight", HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::AUTO_LIGHT)
{
    // NOTE: Chinese names use QStringLiteral (u16 literal) - plain
    // tr() with \u escapes mangles under MSVC (set-matte precedent)
    const auto lightGroup =
            enve::make_shared<StaticComplexAnimator>(QStringLiteral("灯光"));
    mFieldSrc = enve::make_shared<ComboBoxProperty>(
                QStringLiteral("光照场"), QStringList()
                << QStringLiteral("深度图法线")
                << QStringLiteral("三色渐变体积")
                << QStringLiteral("输入图亮度"));
    lightGroup->ca_addChild(mFieldSrc);
    mLightPos = enve::make_shared<QPointFAnimator>(
                QStringLiteral("灯光位置"));
    mLightPos->setBaseValue(0.3, 0.3);
    lightGroup->ca_addChild(mLightPos);
    // AE-style draggable crosshair over the host content rect
    setPointsHandler(enve::make_shared<PointsHandler>());
    getPointsHandler()->appendPt(enve::make_shared<EffectCanvasPoint>(
                mLightPos.get(), this, EffectCanvasPoint::Space::Normalized));
    mLightZ = enve::make_shared<QrealAnimator>(800.0, 0.0, 2000.0, 1.0,
                                               QStringLiteral("灯光高度"));
    lightGroup->ca_addChild(mLightZ);
    mLightElev = enve::make_shared<QrealAnimator>(35.0, 0.0, 90.0, 1.0,
                                                  QStringLiteral("光源仰角"));
    lightGroup->ca_addChild(mLightElev);
    mLightIntensity = enve::make_shared<QrealAnimator>(75.0, 0.0, 300.0, 0.5,
                                               QStringLiteral("灯光强度 %"));
    lightGroup->ca_addChild(mLightIntensity);
    mLightColor = enve::make_shared<ColorAnimator>(QStringLiteral("灯光颜色"));
    mLightColor->setColor(QColor(255, 255, 255, 255));
    lightGroup->ca_addChild(mLightColor);
    mSmooth = enve::make_shared<QrealAnimator>(3.0, 0.0, 16.0, 1.0,
                                               QStringLiteral("平滑半径"));
    lightGroup->ca_addChild(mSmooth);
    mBump = enve::make_shared<QrealAnimator>(60.0, 0.0, 100.0, 1.0,
                                             QStringLiteral("深度凹凸"));
    lightGroup->ca_addChild(mBump);
    mPosterize = enve::make_shared<BoolProperty>(QStringLiteral("色阶化光照"));
    lightGroup->ca_addChild(mPosterize);
    mPosterizeLevels = enve::make_shared<QrealAnimator>(8.0, 2.0, 16.0, 1.0,
                                               QStringLiteral("色阶数"));
    lightGroup->ca_addChild(mPosterizeLevels);
    ca_addChild(lightGroup);

    const auto shadowGroup =
            enve::make_shared<StaticComplexAnimator>(QStringLiteral("阴影"));
    mThreshold = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 0.5,
                                                  QStringLiteral("阴影阈值 %"));
    shadowGroup->ca_addChild(mThreshold);
    mHardness = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 0.5,
                                             QStringLiteral("阴影边缘硬度 %"));
    shadowGroup->ca_addChild(mHardness);
    mShadowStrength = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 0.5,
                                               QStringLiteral("阴影强度 %"));
    shadowGroup->ca_addChild(mShadowStrength);
    mShadowColor = enve::make_shared<ColorAnimator>(QStringLiteral("阴影颜色"));
    mShadowColor->setColor(QColor(102, 112, 199, 255));
    shadowGroup->ca_addChild(mShadowColor);
    mHalftone = enve::make_shared<BoolProperty>(QStringLiteral("半调阴影"));
    shadowGroup->ca_addChild(mHalftone);
    mHalftoneScale = enve::make_shared<QrealAnimator>(8.0, 2.0, 32.0, 0.5,
                                              QStringLiteral("半调缩放"));
    shadowGroup->ca_addChild(mHalftoneScale);
    mHalftoneVar = enve::make_shared<QrealAnimator>(25.0, 0.0, 100.0, 0.5,
                                              QStringLiteral("半调变化 %"));
    shadowGroup->ca_addChild(mHalftoneVar);
    mMixOriginal = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 0.5,
                                              QStringLiteral("混合原始 %"));
    shadowGroup->ca_addChild(mMixOriginal);
    mShadowOnly = enve::make_shared<BoolProperty>(QStringLiteral("仅输出阴影"));
    shadowGroup->ca_addChild(mShadowOnly);
    ca_addChild(shadowGroup);

    const auto rimGroup =
            enve::make_shared<StaticComplexAnimator>(QStringLiteral("边缘光"));
    mRimOn = enve::make_shared<BoolProperty>(QStringLiteral("启用边缘光"));
    rimGroup->ca_addChild(mRimOn);
    mRimColor = enve::make_shared<ColorAnimator>(QStringLiteral("边缘光颜色"));
    mRimColor->setColor(QColor(255, 255, 255, 255));
    rimGroup->ca_addChild(mRimColor);
    mRimIntensity = enve::make_shared<QrealAnimator>(200.0, 0.0, 400.0, 1.0,
                                              QStringLiteral("边缘光强度 %"));
    rimGroup->ca_addChild(mRimIntensity);
    mRimWidth = enve::make_shared<QrealAnimator>(14.0, 1.0, 60.0, 0.5,
                                              QStringLiteral("边缘光宽度"));
    rimGroup->ca_addChild(mRimWidth);
    mRimSoftness = enve::make_shared<QrealAnimator>(4.0, 0.0, 30.0, 0.5,
                                              QStringLiteral("边缘光柔化"));
    rimGroup->ca_addChild(mRimSoftness);
    mRimOffset = enve::make_shared<QrealAnimator>(0.0, -180.0, 180.0, 1.0,
                                              QStringLiteral("边缘光方向偏移"));
    rimGroup->ca_addChild(mRimOffset);
    mRimOpacity = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 0.5,
                                              QStringLiteral("边缘光不透明度 %"));
    rimGroup->ca_addChild(mRimOpacity);
    mRimOnly = enve::make_shared<BoolProperty>(QStringLiteral("仅边缘光"));
    rimGroup->ca_addChild(mRimOnly);
    ca_addChild(rimGroup);

    const auto depthGroup =
            enve::make_shared<StaticComplexAnimator>(QStringLiteral("深度图"));
    mDepthTarget = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("深度图层"));
    mDepthTarget->setComboPicker(true);
    connect(mDepthTarget.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const box) {
        // live follow: the depth layer animating must invalidate the
        // HOST's render cache (set-matte precedent)
        auto& conn = mFollowConn.assign(box);
        if(box) {
            conn << connect(box, &BoundingBox::prp_absFrameRangeChanged,
                            this, [this](const FrameRange&, const bool) {
                prp_afterWholeInfluenceRangeChanged();
            });
        }
        prp_afterWholeInfluenceRangeChanged();
    });
    depthGroup->ca_addChild(mDepthTarget);
    mDepthInvert = enve::make_shared<BoolProperty>(QStringLiteral("深度反转(亮=远)"));
    depthGroup->ca_addChild(mDepthInvert);
    mDepthFalloff = enve::make_shared<QrealAnimator>(40.0, 0.0, 100.0, 0.5,
                                              QStringLiteral("深度衰减 %"));
    depthGroup->ca_addChild(mDepthFalloff);
    ca_addChild(depthGroup);
}

stdsptr<RasterEffectCaller> AutoLightEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    autolight::Params p;
    p.fieldSrc = mFieldSrc ? qBound(0, mFieldSrc->getCurrentValue(), 2) : 0;
    const QPointF lp = mLightPos->getEffectiveValue(relFrame);
    p.lightPos[0] = float(lp.x());
    p.lightPos[1] = float(lp.y());
    p.lightZ = float(mLightZ->getEffectiveValue(relFrame));
    p.lightElevDeg = float(mLightElev->getEffectiveValue(relFrame));
    p.lightIntensity = float(mLightIntensity->getEffectiveValue(relFrame)
                             * influence);
    const QColor lc = mLightColor->getColor(relFrame);
    p.lightCol[0] = float(lc.redF());
    p.lightCol[1] = float(lc.greenF());
    p.lightCol[2] = float(lc.blueF());
    p.smooth = qMax(0, qRound(mSmooth->getEffectiveValue(relFrame)
                              * resolution));
    p.bump = float(mBump->getEffectiveValue(relFrame));
    p.posterize = mPosterize->getValue();
    p.posterizeLevels = qMax(2, qRound(mPosterizeLevels->getEffectiveValue(
                             relFrame)));
    p.threshold = float(mThreshold->getEffectiveValue(relFrame));
    p.hardness = float(mHardness->getEffectiveValue(relFrame));
    p.shadowStrength = float(mShadowStrength->getEffectiveValue(relFrame)
                             * influence);
    const QColor sc = mShadowColor->getColor(relFrame);
    p.shadowCol[0] = float(sc.redF());
    p.shadowCol[1] = float(sc.greenF());
    p.shadowCol[2] = float(sc.blueF());
    p.halftone = mHalftone->getValue();
    p.halftoneScale = float(mHalftoneScale->getEffectiveValue(relFrame));
    p.halftoneVar = float(mHalftoneVar->getEffectiveValue(relFrame));
    p.mixOriginal = float(mMixOriginal->getEffectiveValue(relFrame));
    p.shadowOnly = mShadowOnly->getValue();
    p.rim = mRimOn->getValue();
    const QColor rc = mRimColor->getColor(relFrame);
    p.rimCol[0] = float(rc.redF());
    p.rimCol[1] = float(rc.greenF());
    p.rimCol[2] = float(rc.blueF());
    p.rimIntensity = float(mRimIntensity->getEffectiveValue(relFrame));
    p.rimWidth = float(mRimWidth->getEffectiveValue(relFrame)
                       * std::max(1e-4, resolution));
    p.rimSoftness = float(mRimSoftness->getEffectiveValue(relFrame)
                          * std::max(1e-4, resolution));
    p.rimOffsetDeg = float(mRimOffset->getEffectiveValue(relFrame));
    p.rimOpacity = float(mRimOpacity->getEffectiveValue(relFrame)
                         * influence);
    p.rimOnly = mRimOnly->getValue();
    p.depthInvert = mDepthInvert->getValue();
    p.depthFalloff = float(mDepthFalloff->getEffectiveValue(relFrame));

    // queue the picked depth layer for an independent render; the
    // dependency delays this box's effects phase until the sample
    // finishes (the set-matte queExternalRender pattern)
    stdsptr<BoxRenderData> sample;
    const auto target = mDepthTarget ? mDepthTarget->getTarget() : nullptr;
    const auto parentBox = data ? data->fParentBox.data() : nullptr;
    if(data && target && target != parentBox
       && !(parentBox && parentBox->isAncestor(target))
       && !target->isAncestor(parentBox)
       && !sSampleChain.contains(target)
       && target->isVisibleAndInVisibleDurationRect()) {
        sSampleChain.append(parentBox ? parentBox : target);
        const auto guard = qScopeGuard([]() { sSampleChain.removeLast(); });
        // relFrame is the HOST's relative frame; the depth layer has
        // its own trim/start - convert through the absolute frame
        const qreal absFrame =
                parentBox ? parentBox->prp_relFrameToAbsFrameF(relFrame)
                          : relFrame;
        const qreal tRel = target->prp_absFrameToRelFrameF(absFrame);
        sample = target->queExternalRender(tRel, true);
        if(sample) sample->addDependent(data);
    }

    return enve::make_shared<AutoLightEffectCaller>(
                instanceHwSupport(), p, std::move(sample), resolution);
}

FrameRange AutoLightEffect::prp_getIdenticalRelRange(
        const int relFrame) const {
    const auto thisIdent = ComplexAnimator::prp_getIdenticalRelRange(relFrame);
    const auto target = mDepthTarget ? mDepthTarget->getTarget() : nullptr;
    if(!target) return thisIdent;
    // the host's rendered pixels depend on the depth layer's content:
    // a static host under an animated depth map is NOT frame-identical
    // (set-matte pattern)
    if(sSampleChain.contains(target)) return thisIdent;
    sSampleChain.append(target);
    const auto guard = qScopeGuard([]() { sSampleChain.removeLast(); });
    const int absFrame = prp_relFrameToAbsFrame(relFrame);
    const int tRelFrame = target->prp_absFrameToRelFrame(absFrame);
    const auto targetIdent = target->prp_getIdenticalRelRange(tRelFrame);
    const auto absTargetIdent = target->prp_relRangeToAbsRange(targetIdent);
    return thisIdent*prp_absRangeToRelRange(absTargetIdent);
}
