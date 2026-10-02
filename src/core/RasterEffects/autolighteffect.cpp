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

// rasterize an external layer's rendered image into the host image
// frame (track-matte placement math + the liquid-glass resolution
// bridge) and read it back as floats. mode 0 (depth): 0..1 per pixel
// (gray R, JET false-color decoded); mode 1 (luma): 0..1 luminance,
// -1 where the sample is transparent (no lighting data there);
// mode 2 (normal map): 3 floats per pixel, tangent-space normal
// decoded from RGB (OpenGL G-up screen-space convention by default,
// flipG switches to DirectX), (0,0,0) = no data
std::vector<float> rasterizeSample(const BoxRenderData& s,
                                   const int mode,
                                   const bool flipG,
                                   const int w, const int h,
                                   const QPoint& imgPos,
                                   const qreal hostRes)
{
    std::vector<float> out;
    if (!s.fRenderedImage) return out;
    const auto img = s.fRenderedImage->makeRasterImage();
    SkPixmap pix;
    if (!img || !img->peekPixels(&pix) || pix.width() <= 0
        || pix.height() <= 0) {
        return out;
    }
    SkBitmap bmp;
    bmp.allocPixels(SkImageInfo::MakeN32Premul(w, h));
    bmp.eraseColor(mode == 1 ? 0x00000000 : 0xFF808080);
    SkCanvas dc(bmp);
    SkMatrix m = SkMatrix::MakeTrans(-qreal(imgPos.x()),
                                     -qreal(imgPos.y()));
    const qreal sRes = s.fResolution > 0. ? s.fResolution : 1.;
    const qreal hRes = hostRes > 0. ? hostRes : 1.;
    m.postScale(hRes / sRes, hRes / sRes);
    dc.setMatrix(m);
    SkPaint dp;
    dp.setFilterQuality(kLow_SkFilterQuality);
    if (s.fUseRenderTransform) {
        dc.concat(toSkMatrix(s.fRenderTransform));
        dc.drawImageRect(s.fRenderedImage,
                         toSkRect(s.fRelBoundingRect), &dp);
    } else {
        dc.drawImage(s.fRenderedImage,
                     s.fGlobalRect.x(), s.fGlobalRect.y(), &dp);
    }
    dc.flush();

    out.resize(size_t(w) * h * (mode == 2 ? 3 : 1));
    for (int y = 0; y < h; y++) {
        const auto row = static_cast<const uchar*>(bmp.getAddr(0, y));
        for (int x = 0; x < w; x++) {
            // N32 memory order is [B, G, R, A] on Windows (see
            // unpackPixel above)
            const float b = float(row[x * 4 + 0]) / 255.f;
            const float g = float(row[x * 4 + 1]) / 255.f;
            const float r = float(row[x * 4 + 2]) / 255.f;
            const float a = float(row[x * 4 + 3]) / 255.f;
            if (mode == 2) {
                float* const o = &out[(size_t(y) * w + x) * 3];
                if (a < 0.5f) { o[0] = 0.f; o[1] = 0.f; o[2] = 0.f; continue; }
                const float ur = a < 0.999f ? r / a : r;
                const float ug = a < 0.999f ? g / a : g;
                const float ub = a < 0.999f ? b / a : b;
                float nx = ur * 2.f - 1.f;
                float ny = 1.f - ug * 2.f; // OpenGL G-up (screen y down)
                float nz = ub * 2.f - 1.f;
                if (flipG) ny = -ny;      // DirectX convention
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nl > 1e-4f) { nx /= nl; ny /= nl; nz /= nl; }
                o[0] = nx; o[1] = ny; o[2] = nz;
                continue;
            }
            float& o = out[size_t(y) * w + x];
            if (mode == 1) {
                if (a < 0.5f) { o = -1.f; continue; }
                if (a < 0.999f) {
                    const float ir = r / a, ig = g / a, ib = b / a;
                    o = 0.2126f * ir + 0.7152f * ig + 0.0722f * ib;
                } else {
                    o = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                }
            } else {
                o = (std::abs(r - g) < 0.008f && std::abs(g - b) < 0.008f)
                        ? r : autolight::jetDecode(r, g, b);
            }
        }
    }
    return out;
}

} // namespace

class AutoLightEffectCaller : public RasterEffectCaller {
public:
    AutoLightEffectCaller(const HardwareSupport hwSupport,
                          const autolight::Params& p,
                          stdsptr<BoxRenderData> depthSample,
                          stdsptr<BoxRenderData> lumaSample,
                          stdsptr<BoxRenderData> normalSample,
                          const bool normalFlipG,
                          const qreal hostRes) :
        RasterEffectCaller(hwSupport, false, QMargins()),
        mP(p), mDepthSample(std::move(depthSample)),
        mLumaSample(std::move(lumaSample)),
        mNormalSample(std::move(normalSample)),
        mNormalFlipG(normalFlipG), mHostRes(hostRes) {}

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

            // rasterize the depth / luminance / normal samples into
            // the host image frame so all grids share one coordinate
            // system
            std::vector<float> depth, luma, nrm;
            if (mDepthSample) {
                depth = rasterizeSample(*mDepthSample, 0, false, w, h,
                                        data.fPos, mHostRes);
            }
            if (mLumaSample) {
                luma = rasterizeSample(*mLumaSample, 1, false, w, h,
                                       data.fPos, mHostRes);
            }
            if (mNormalSample) {
                nrm = rasterizeSample(*mNormalSample, 2, mNormalFlipG,
                                      w, h, data.fPos, mHostRes);
            }

            std::vector<uint32_t> dst(n);
            autolight::compute(src.data(),
                               depth.empty() ? nullptr : depth.data(),
                               luma.empty() ? nullptr : luma.data(),
                               nrm.empty() ? nullptr : nrm.data(),
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
    const stdsptr<BoxRenderData> mLumaSample;
    const stdsptr<BoxRenderData> mNormalSample;
    const bool mNormalFlipG;
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
                << QStringLiteral("输入图亮度")
                << QStringLiteral("法向贴图"));
    lightGroup->ca_addChild(mFieldSrc);
    // luminance-field layer picker: when a layer is picked (输入图亮度
    // mode), its rendered luminance becomes the lighting field; empty
    // keeps the input image's own luminance (the effect stack below)
    mLumaTarget = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("亮度图层"));
    mLumaTarget->setComboPicker(true);
    connect(mLumaTarget.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const box) {
        auto& conn = mLumaFollowConn.assign(box);
        if(box) {
            conn << connect(box, &BoundingBox::prp_absFrameRangeChanged,
                            this, [this](const FrameRange&, const bool) {
                prp_afterWholeInfluenceRangeChanged();
            });
        }
        prp_afterWholeInfluenceRangeChanged();
    });
    lightGroup->ca_addChild(mLumaTarget);
    // real tangent-space normal map picker (法向贴图 mode): RGB is
    // decoded back to a per-pixel XYZ normal; flipG switches between
    // the OpenGL (default) and DirectX green-channel conventions
    mNormalTarget = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("法向图层"));
    mNormalTarget->setComboPicker(true);
    connect(mNormalTarget.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const box) {
        auto& conn = mNormalFollowConn.assign(box);
        if(box) {
            conn << connect(box, &BoundingBox::prp_absFrameRangeChanged,
                            this, [this](const FrameRange&, const bool) {
                prp_afterWholeInfluenceRangeChanged();
            });
        }
        prp_afterWholeInfluenceRangeChanged();
    });
    lightGroup->ca_addChild(mNormalTarget);
    mNormalFlipG = enve::make_shared<BoolProperty>(
                QStringLiteral("翻转G(DirectX法线)"));
    lightGroup->ca_addChild(mNormalFlipG);
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
    p.fieldSrc = mFieldSrc ? qBound(0, mFieldSrc->getCurrentValue(), 3) : 0;
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

    // queue an external layer for an independent render; the
    // dependency delays this box's effects phase until the sample
    // finishes (the set-matte queExternalRender pattern)
    const auto parentBox = data ? data->fParentBox.data() : nullptr;
    const auto queueSample = [this, &data, &parentBox, &relFrame](
                BoundingBox* const target) -> stdsptr<BoxRenderData> {
        if(!data || !target || target == parentBox) return nullptr;
        if(parentBox) {
            // self / own subtree / own ancestors would recurse
            if(parentBox->isAncestor(target)) return nullptr;
            if(target->isAncestor(parentBox)) return nullptr;
        }
        if(sSampleChain.contains(target)) return nullptr;
        if(!target->isVisibleAndInVisibleDurationRect()) return nullptr;
        sSampleChain.append(parentBox ? parentBox : target);
        const auto guard = qScopeGuard([]() { sSampleChain.removeLast(); });
        // relFrame is the HOST's relative frame; the sampled layer has
        // its own trim/start - convert through the absolute frame
        const qreal absFrame =
                parentBox ? parentBox->prp_relFrameToAbsFrameF(relFrame)
                          : relFrame;
        const qreal tRel = target->prp_absFrameToRelFrameF(absFrame);
        auto sample = target->queExternalRender(tRel, true);
        if(sample) sample->addDependent(data);
        return sample;
    };

    stdsptr<BoxRenderData> depthSample, lumaSample, normalSample;
    const auto depthTarget =
            mDepthTarget ? mDepthTarget->getTarget() : nullptr;
    if(depthTarget) {
        depthSample = queueSample(depthTarget);
    }
    if(p.fieldSrc == 2 && mLumaTarget) {
        lumaSample = queueSample(mLumaTarget->getTarget());
    }
    if(p.fieldSrc == 3 && mNormalTarget) {
        normalSample = queueSample(mNormalTarget->getTarget());
    }

    return enve::make_shared<AutoLightEffectCaller>(
                instanceHwSupport(), p, std::move(depthSample),
                std::move(lumaSample), std::move(normalSample),
                mNormalFlipG ? mNormalFlipG->getValue() : false,
                resolution);
}

FrameRange AutoLightEffect::prp_getIdenticalRelRange(
        const int relFrame) const {
    auto thisIdent = ComplexAnimator::prp_getIdenticalRelRange(relFrame);
    // the host's rendered pixels depend on the sampled layers'
    // content: a static host under an animated depth/luminance layer
    // is NOT frame-identical (set-matte pattern)
    BoundingBox* const targets[3] = {
        mDepthTarget ? mDepthTarget->getTarget() : nullptr,
        mLumaTarget ? mLumaTarget->getTarget() : nullptr,
        mNormalTarget ? mNormalTarget->getTarget() : nullptr
    };
    for(const auto target : targets) {
        if(!target || sSampleChain.contains(target)) continue;
        sSampleChain.append(target);
        const auto guard = qScopeGuard([]() { sSampleChain.removeLast(); });
        const int absFrame = prp_relFrameToAbsFrame(relFrame);
        const int tRelFrame = target->prp_absFrameToRelFrame(absFrame);
        const auto targetIdent = target->prp_getIdenticalRelRange(tRelFrame);
        const auto absTargetIdent =
                target->prp_relRangeToAbsRange(targetIdent);
        thisIdent = thisIdent*prp_absRangeToRelRange(absTargetIdent);
    }
    return thisIdent;
}
