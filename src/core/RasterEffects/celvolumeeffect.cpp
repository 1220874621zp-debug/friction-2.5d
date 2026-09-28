#include "celvolumeeffect.h"

#include "rastereffectcaller.h"
#include "celvolumealgo.h"

#include "Animators/qrealanimator.h"
#include "Animators/boolanimator.h"
#include "Animators/staticcomplexanimator.h"

#include "appsupport.h"

#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <cstring>

namespace {

// convert skia's premultiplied N32 bytes to the straight-alpha
// packed 0xAARRGGBB colors the algorithm expects; byte order
// matches the other CPU effects in this tree (pixelate/chromakey)
inline uint32_t unpackPixel(const uchar* const p)
{
    const uint32_t a = p[3];
    if (a == 255 || a == 0) {
        return (a << 24) | (uint32_t(p[0]) << 16)
                | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
    }
    const uint32_t r = uint32_t(p[0]) * 255u / a;
    const uint32_t g = uint32_t(p[1]) * 255u / a;
    const uint32_t b = uint32_t(p[2]) * 255u / a;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline void packPixel(const uint32_t c, uchar* const p)
{
    const uint32_t a = (c >> 24) & 255u;
    p[0] = uchar((((c >> 16) & 255u) * a + 127u) / 255u);
    p[1] = uchar((((c >> 8) & 255u) * a + 127u) / 255u);
    p[2] = uchar(((c & 255u) * a + 127u) / 255u);
    p[3] = uchar(a);
}

} // namespace

class CelVolumeEffectCaller : public RasterEffectCaller {
public:
    CelVolumeEffectCaller(const celvolume::Params& p) :
        RasterEffectCaller(AppSupport::getRasterEffectHardwareSupport(
                               "CelVolume", HardwareSupport::cpuOnly),
                           false, QMargins()),
        mP(p) {}

    // the pipeline is a whole-image pass (segmentation has no tile
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
            mResult.allocN32Pixels(w, h);
            std::vector<uint32_t> src(size_t(w) * h);
            std::vector<uint32_t> dst(size_t(w) * h);
            for (int y = 0; y < h; y++) {
                const auto row = static_cast<const uchar*>(
                            srcBtmp.getAddr(0, y));
                for (int x = 0; x < w; x++) {
                    src[size_t(y) * w + x] = unpackPixel(row + x * 4);
                }
            }
            celvolume::compute(src.data(), w, h, mP, dst.data());
            for (int y = 0; y < h; y++) {
                const auto row = static_cast<uchar*>(
                            mResult.getAddr(0, y));
                for (int x = 0; x < w; x++) {
                    packPixel(dst[size_t(y) * w + x], row + x * 4);
                }
            }
            mComputed = true;
        }
        lock.unlock();

        // copy this tile's slice of the full-image result; same pixel
        // format, so whole rows move with memcpy
        const auto& tile = data.fTexTile;
        const int xMin = std::max(0, tile.left());
        const int xMax = std::min(tile.right(), mResult.width() - 1);
        const int yMin = std::max(0, tile.top());
        const int yMax = std::min(tile.bottom(), mResult.height() - 1);
        for (int yi = yMin; yi <= yMax; yi++) {
            memcpy(dstBtmp.getAddr(0, yi - yMin),
                   mResult.getAddr(xMin, yi),
                   static_cast<size_t>(xMax - xMin + 1) * 4);
        }

    }
private:
    const celvolume::Params mP;
    QMutex mMutex;
    bool mComputed = false;
    SkBitmap mResult;
};

CelVolumeEffect::CelVolumeEffect() :
    RasterEffect(QObject::tr("体积渐变"),
                 AppSupport::getRasterEffectHardwareSupport("CelVolume",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::CEL_VOLUME)
{
    const auto segGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("区域分割"));
    mColorTol = enve::make_shared<QrealAnimator>(14.0, 0.0, 64.0, 0.5,
                                                 QObject::tr("颜色容差"));
    segGroup->ca_addChild(mColorTol);
    mMinArea = enve::make_shared<QrealAnimator>(40.0, 1.0, 5000.0, 1.0,
                                                QObject::tr("最小区域"));
    segGroup->ca_addChild(mMinArea);
    mProtectDark = enve::make_shared<BoolAnimator>(
                QObject::tr("保护线稿"));
    mProtectDark->setCurrentBoolValue(true);
    segGroup->ca_addChild(mProtectDark);
    mDarkLuma = enve::make_shared<QrealAnimator>(30.0, 0.0, 100.0, 1.0,
                                                 QObject::tr("线稿亮度阈值"));
    segGroup->ca_addChild(mDarkLuma);
    ca_addChild(segGroup);

    const auto volGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("体积光照"));
    mLightAngle = enve::make_shared<QrealAnimator>(135.0, -360.0, 360.0, 1.0,
                                                   QObject::tr("光照角度"));
    volGroup->ca_addChild(mLightAngle);
    mLightElev = enve::make_shared<QrealAnimator>(35.0, 0.0, 90.0, 1.0,
                                                  QObject::tr("光源高度"));
    volGroup->ca_addChild(mLightElev);
    mBump = enve::make_shared<QrealAnimator>(60.0, 0.0, 100.0, 1.0,
                                             QObject::tr("体积强度"));
    volGroup->ca_addChild(mBump);
    mAo = enve::make_shared<QrealAnimator>(40.0, 0.0, 100.0, 1.0,
                                          QObject::tr("边缘暗部"));
    volGroup->ca_addChild(mAo);
    mAoWidth = enve::make_shared<QrealAnimator>(6.0, 1.0, 30.0, 0.5,
                                                QObject::tr("暗部宽度"));
    volGroup->ca_addChild(mAoWidth);
    mSmooth = enve::make_shared<QrealAnimator>(3.0, 0.0, 16.0, 1.0,
                                               QObject::tr("平滑半径"));
    volGroup->ca_addChild(mSmooth);
    ca_addChild(volGroup);

    const auto rampGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("四色渐变"));
    mHiStrength = enve::make_shared<QrealAnimator>(62.0, 0.0, 100.0, 1.0,
                                                   QObject::tr("高光强度"));
    rampGroup->ca_addChild(mHiStrength);
    mHiWarm = enve::make_shared<QrealAnimator>(40.0, 0.0, 100.0, 1.0,
                                               QObject::tr("高光暖度"));
    rampGroup->ca_addChild(mHiWarm);
    mBrightStrength = enve::make_shared<QrealAnimator>(30.0, 0.0, 100.0, 1.0,
                                                       QObject::tr("亮部强度"));
    rampGroup->ca_addChild(mBrightStrength);
    mShadeStrength = enve::make_shared<QrealAnimator>(38.0, 0.0, 100.0, 1.0,
                                                     QObject::tr("暗部强度"));
    rampGroup->ca_addChild(mShadeStrength);
    mShadeHue = enve::make_shared<QrealAnimator>(-18.0, -180.0, 180.0, 1.0,
                                                 QObject::tr("暗部偏色"));
    rampGroup->ca_addChild(mShadeHue);
    mSoftness = enve::make_shared<QrealAnimator>(45.0, 0.0, 100.0, 1.0,
                                                 QObject::tr("渐变柔和度"));
    rampGroup->ca_addChild(mSoftness);
    mMix = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0,
                                            QObject::tr("效果强度"));
    rampGroup->ca_addChild(mMix);
    ca_addChild(rampGroup);
}

stdsptr<RasterEffectCaller> CelVolumeEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(data)
    celvolume::Params p;
    p.colorTol = float(mColorTol->getEffectiveValue(relFrame));
    p.minArea = qMax(1, qRound(mMinArea->getEffectiveValue(relFrame)));
    p.protectDark = mProtectDark->getBoolValue(relFrame);
    p.darkLuma = float(mDarkLuma->getEffectiveValue(relFrame) / 100.0);
    p.lightAngleDeg = float(mLightAngle->getEffectiveValue(relFrame));
    p.lightElevDeg = float(mLightElev->getEffectiveValue(relFrame));
    p.bump = float(mBump->getEffectiveValue(relFrame));
    p.ao = float(mAo->getEffectiveValue(relFrame));
    p.aoWidth = float(mAoWidth->getEffectiveValue(relFrame) * resolution);
    p.smooth = qMax(0, qRound(mSmooth->getEffectiveValue(relFrame) * resolution));
    p.hiStrength = float(mHiStrength->getEffectiveValue(relFrame));
    p.hiWarm = float(mHiWarm->getEffectiveValue(relFrame));
    p.brightStrength = float(mBrightStrength->getEffectiveValue(relFrame));
    p.shadeStrength = float(mShadeStrength->getEffectiveValue(relFrame));
    p.shadeHue = float(mShadeHue->getEffectiveValue(relFrame));
    p.softness = float(mSoftness->getEffectiveValue(relFrame));
    p.mix = float(mMix->getEffectiveValue(relFrame) * influence);
    return enve::make_shared<CelVolumeEffectCaller>(p);
}
