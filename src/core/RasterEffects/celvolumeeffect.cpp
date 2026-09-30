#include "celvolumeeffect.h"

#include "rastereffectcaller.h"
#include "celvolumealgo.h"

#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "Animators/staticcomplexanimator.h"
#include "Properties/comboboxproperty.h"
#include "Animators/qpointfanimator.h"
#include "MovablePoints/pointshandler.h"
#include "RasterEffects/effectcanvaspoint.h"

#include "appsupport.h"

#include <QMutex>
#include <QMutexLocker>
#include <vector>

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
    const celvolume::Params mP;
    QMutex mMutex;
    bool mComputed = false;
    SkBitmap mResult;
};

CelVolumeEffect::CelVolumeEffect() :
    RasterEffect(QObject::tr("\u4e09\u8272\u6e10\u53d8"),
                 AppSupport::getRasterEffectHardwareSupport("CelVolume",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::CEL_VOLUME)
{
    const auto segGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("\u533a\u57df\u5206\u5272"));
    mColorTol = enve::make_shared<QrealAnimator>(96.0, 0.0, 128.0, 0.5,
                                                 QObject::tr("\u989c\u8272\u5bb9\u5dee"));
    segGroup->ca_addChild(mColorTol);
    mMinArea = enve::make_shared<QrealAnimator>(725.7, 1.0, 20000.0, 0.1,
                                                QObject::tr("\u6700\u5c0f\u533a\u57df"));
    segGroup->ca_addChild(mMinArea);
    ca_addChild(segGroup);

    const auto gradGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("\u4e09\u8272\u6e10\u53d8"));
    mShadeMode = enve::make_shared<ComboBoxProperty>(
                QObject::tr("\u7740\u8272\u6a21\u5f0f"), QStringList()
                << QObject::tr("\u5f84\u5411\u6e10\u53d8") << QObject::tr("\u4f53\u79ef\u5149\u5f71"));
    gradGroup->ca_addChild(mShadeMode);
    mGradGamma = enve::make_shared<QrealAnimator>(1.8, 0.2, 5.0, 0.1,
                                                  QObject::tr("\u4e2d\u5fc3\u8272\u8303\u56f4"));
    gradGroup->ca_addChild(mGradGamma);
    mLightPos = enve::make_shared<QPointFAnimator>(
                QObject::tr("\u5149\u6e90\u4f4d\u7f6e"));
    mLightPos->setBaseValue(0.2, 0.2);
    gradGroup->ca_addChild(mLightPos);
    // AE-style draggable crosshair aiming every region's center
    // color band at once (0..1 uv over the host box content rect)
    setPointsHandler(enve::make_shared<PointsHandler>());
    getPointsHandler()->appendPt(enve::make_shared<EffectCanvasPoint>(
                mLightPos.get(), this, EffectCanvasPoint::Space::Normalized));
    mColWarm = enve::make_shared<ColorAnimator>(QObject::tr("\u4e2d\u5fc3\u8272"));
    mColWarm->setColor(QColor(212, 0, 202, 255));
    gradGroup->ca_addChild(mColWarm);
    mColMid = enve::make_shared<ColorAnimator>(QObject::tr("\u4e2d\u95f4\u8272"));
    mColMid->setColor(QColor(148, 7, 184, 255));
    gradGroup->ca_addChild(mColMid);
    mColCool = enve::make_shared<ColorAnimator>(QObject::tr("\u8fb9\u7f18\u8272"));
    mColCool->setColor(QColor(66, 17, 194, 255));
    gradGroup->ca_addChild(mColCool);
    mBgDarken = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0,
                                                 QObject::tr("\u80cc\u666f\u6697\u5316"));
    gradGroup->ca_addChild(mBgDarken);
    mMix = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0,
                                            QObject::tr("\u4e0a\u8272\u5f3a\u5ea6"));
    gradGroup->ca_addChild(mMix);
    ca_addChild(gradGroup);

    const auto volGroup =
            enve::make_shared<StaticComplexAnimator>(QObject::tr("\u4f53\u79ef\u5149\u5f71"));
    mLightAngle = enve::make_shared<QrealAnimator>(135.0, -360.0, 360.0, 1.0,
                                                   QObject::tr("\u5149\u7167\u89d2\u5ea6"));
    volGroup->ca_addChild(mLightAngle);
    mLightElev = enve::make_shared<QrealAnimator>(35.0, 0.0, 90.0, 1.0,
                                                  QObject::tr("\u5149\u6e90\u9ad8\u5ea6"));
    volGroup->ca_addChild(mLightElev);
    mBump = enve::make_shared<QrealAnimator>(60.0, 0.0, 100.0, 1.0,
                                             QObject::tr("\u4f53\u79ef\u5f3a\u5ea6"));
    volGroup->ca_addChild(mBump);
    mAo = enve::make_shared<QrealAnimator>(40.0, 0.0, 100.0, 1.0,
                                          QObject::tr("\u8fb9\u7f18\u6697\u90e8"));
    volGroup->ca_addChild(mAo);
    mAoWidth = enve::make_shared<QrealAnimator>(6.0, 1.0, 30.0, 0.5,
                                                QObject::tr("\u6697\u90e8\u5bbd\u5ea6"));
    volGroup->ca_addChild(mAoWidth);
    mSmooth = enve::make_shared<QrealAnimator>(3.0, 0.0, 16.0, 1.0,
                                               QObject::tr("\u5e73\u6ed1\u534a\u5f84"));
    volGroup->ca_addChild(mSmooth);
    ca_addChild(volGroup);
}

stdsptr<RasterEffectCaller> CelVolumeEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(data)
    celvolume::Params p;
    p.colorTol = float(mColorTol->getEffectiveValue(relFrame));
    p.minArea = qMax(1, qRound(mMinArea->getEffectiveValue(relFrame)));
    p.shadeMode = mShadeMode->getCurrentValue();
    p.gradGamma = float(mGradGamma->getEffectiveValue(relFrame));
    const QPointF lp = mLightPos->getEffectiveValue(relFrame);
    p.lightPos[0] = float(lp.x());
    p.lightPos[1] = float(lp.y());
    const QColor warm = mColWarm->getColor(relFrame);
    p.colWarm[0] = float(warm.redF());
    p.colWarm[1] = float(warm.greenF());
    p.colWarm[2] = float(warm.blueF());
    const QColor mid = mColMid->getColor(relFrame);
    p.colMid[0] = float(mid.redF());
    p.colMid[1] = float(mid.greenF());
    p.colMid[2] = float(mid.blueF());
    const QColor cool = mColCool->getColor(relFrame);
    p.colCool[0] = float(cool.redF());
    p.colCool[1] = float(cool.greenF());
    p.colCool[2] = float(cool.blueF());
    p.bgDarken = float(mBgDarken->getEffectiveValue(relFrame));
    p.mix = float(mMix->getEffectiveValue(relFrame) * influence);
    p.lightAngleDeg = float(mLightAngle->getEffectiveValue(relFrame));
    p.lightElevDeg = float(mLightElev->getEffectiveValue(relFrame));
    p.bump = float(mBump->getEffectiveValue(relFrame));
    p.ao = float(mAo->getEffectiveValue(relFrame));
    p.aoWidth = float(mAoWidth->getEffectiveValue(relFrame) * resolution);
    p.smooth = qMax(0, qRound(mSmooth->getEffectiveValue(relFrame) * resolution));
    return enve::make_shared<CelVolumeEffectCaller>(p);
}
