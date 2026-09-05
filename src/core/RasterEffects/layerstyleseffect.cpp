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

#include "layerstyleseffect.h"

#include "gpurendertools.h"
#include "rastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "Animators/boolanimator.h"
#include "Animators/staticcomplexanimator.h"
#include "Properties/comboboxproperty.h"

#include "appsupport.h"
#include "include/effects/SkColorMatrix.h"
#include <QtMath>
#include <QMargins>
#include <QAtomicInt>

struct LayerStylesData {
    bool mShadow = false;
    qreal mShadowAngle = 120;   // PS light angle, degrees CCW from east
    qreal mShadowDist = 0;      // px, resolution-scaled
    qreal mShadowSpread = 0;    // 0..1 (PS "spread"/choke)
    qreal mShadowSize = 0;      // px, resolution-scaled (blur extent)
    qreal mShadowOpacity = 1;   // 0..1
    QColor mShadowColor = Qt::black;

    bool mGlow = false;
    qreal mGlowSpread = 0;      // 0..1
    qreal mGlowSize = 0;        // px, resolution-scaled
    qreal mGlowOpacity = 1;
    QColor mGlowColor = QColor(255, 255, 190);

    bool mStroke = false;
    int mStrokePos = 0;         // 0 outside, 1 center, 2 inside
    qreal mStrokeSize = 0;      // px, resolution-scaled
    qreal mStrokeOpacity = 1;
    QColor mStrokeColor = Qt::red;
};

// Choke/spread tightens the blurred alpha: a' = clamp(a/(1-k) - k/(1-k)),
// linear in alpha. Expressed as one color matrix together with the
// tint: rgb := color, a := opacity * gain * clamp(s*a - k*s). Done in
// a SECOND pass over a blurred snapshot - the nested filter chain
// SkImageFilters::ColorFilter(choke, SkImageFilters::Blur(...))
// renders nothing in this skia build (verified by bisect).
// gain lifts the rim (outer glow): PS glows are near-solid at the
// edge fading outward, while a bare gaussian rim sits at 0.5 alpha
// (and this skia's box-blur approximation lands ~0.6 of the ideal
// curve); x3 matches PS visibility for typical PSD values.
static void paintShape(SkCanvas * const canvas, const SkImage * const src,
                       const qreal dx, const qreal dy,
                       const QColor& color, const qreal opacity,
                       const qreal sigma, const qreal spread01,
                       const qreal gain = 1.0)
{
    // 1. blur the silhouette onto a private surface (single filter
    //    per draw, the pattern proven by the stroke path)
    const auto surf = SkSurface::MakeRaster(src->imageInfo());
    if (!surf) return;
    const auto sc = surf->getCanvas();
    sc->clear(SK_ColorTRANSPARENT);
    if (sigma > 0.01) {
        SkPaint bp;
        bp.setImageFilter(SkImageFilters::Blur(
                    static_cast<SkScalar>(sigma),
                    static_cast<SkScalar>(sigma), nullptr));
        sc->drawImage(src, 0, 0, &bp);
    } else {
        sc->drawImage(src, 0, 0);
    }
    sc->flush();
    const auto blurred = surf->makeImageSnapshot();
    if (!blurred) return;

    // 2. tint + choke as one color matrix (values in 0..1 space)
    const qreal alphaF = qBound(0.0, color.alphaF() * opacity * gain, 1.0);
    const qreal k = qBound(0.0, spread01, 0.98);
    const qreal s = k > 0.001 ? 1.0 / (1.0 - k) : 1.0;
    const float m[20] = {
        0, 0, 0, 0, static_cast<float>(color.redF()),
        0, 0, 0, 0, static_cast<float>(color.greenF()),
        0, 0, 0, 0, static_cast<float>(color.blueF()),
        0, 0, 0, static_cast<float>(alphaF * s),
                           static_cast<float>(-alphaF * k * s)
    };
    SkColorMatrix cm;
    cm.setRowMajor(m);
    SkPaint paint;
    paint.setColorFilter(SkColorFilters::Matrix(cm));
    canvas->drawImage(blurred.get(), static_cast<SkScalar>(dx),
                      static_cast<SkScalar>(dy), &paint);
}

// Stroke ring on its own surface: dilated (or plain) tinted
// silhouette minus the interior cut out with kDstOut. Drawn OVER
// everything afterwards, matching Photoshop's stroke-on-top order.
static sk_sp<SkImage> makeStrokeRing(const SkImage * const src,
                                     const LayerStylesData& d)
{
    const int dilateR = d.mStrokePos == 2 ? 0 :
                        qMax(0, qRound(d.mStrokeSize * (d.mStrokePos == 1 ? 0.5 : 1.0)));
    const int erodeR = d.mStrokePos == 0 ? 0 :
                       qMax(0, qRound(d.mStrokeSize * (d.mStrokePos == 1 ? 0.5 : 1.0)));
    if (dilateR <= 0 && erodeR <= 0) return nullptr;

    const auto surf = SkSurface::MakeRaster(src->imageInfo());
    if (!surf) return nullptr;
    const auto canvas = surf->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    SkPaint dp;
    const qreal alphaF = qBound(0.0, d.mStrokeColor.alphaF() * d.mStrokeOpacity, 1.0);
    const SkColor tint = SkColorSetARGB(static_cast<U8CPU>(qRound(alphaF * 255)),
                                        d.mStrokeColor.red(),
                                        d.mStrokeColor.green(),
                                        d.mStrokeColor.blue());
    dp.setColorFilter(SkColorFilters::Blend(tint, SkBlendMode::kSrcIn));
    if (dilateR > 0) {
        dp.setImageFilter(SkImageFilters::Dilate(dilateR, dilateR, nullptr));
    }
    // inside strokes start from the plain silhouette
    canvas->drawImage(src, 0, 0, &dp);

    SkPaint cp;
    cp.setBlendMode(SkBlendMode::kDstOut);
    if (erodeR > 0) {
        cp.setImageFilter(SkImageFilters::Erode(erodeR, erodeR, nullptr));
    }
    canvas->drawImage(src, 0, 0, &cp);
    canvas->flush();
    return surf->makeImageSnapshot();
}

// Composite in the fixed Photoshop stacking order, every layer
// sampled from the pristine source alpha. (ox, oy) translates the
// whole stack: 0 on the GPU path, the tile origin on the CPU path
// (dst is a tile subset while src stays the full padded image).
static void composeStyles(SkCanvas * const canvas, const SkImage * const src,
                          const LayerStylesData& d, const qreal ox, const qreal oy)
{
    const SkScalar sx = static_cast<SkScalar>(ox);
    const SkScalar sy = static_cast<SkScalar>(oy);
    if (d.mShadow) {
        // PS angle: 0 = light from the right, shadow falls LEFT and
        // the dial is mirrored vs. screen y-down; user-calibrated
        // against Photoshop: offY is negated
        const qreal rad = qDegreesToRadians(d.mShadowAngle);
        const qreal dx = -qCos(rad) * d.mShadowDist;
        const qreal dy = -qSin(rad) * d.mShadowDist;
        const qreal sigma = d.mShadowSize / 3.0;
        paintShape(canvas, src, sx + dx, sy + dy, d.mShadowColor,
                   d.mShadowOpacity, sigma, d.mShadowSpread);
    }
    if (d.mGlow) {
        const qreal sigma = d.mGlowSize / 3.0;
        // PS glow "spread" reads as a subtle tightness there, but the
        // linear alpha remap erases the long gaussian tail entirely
        // (a 42% value nukes everything past half the radius) - v1
        // ignores it and lifts the rim instead (gain 2) so typical
        // PSD opacity values (20-30%) stay visible
        paintShape(canvas, src, sx, sy, d.mGlowColor,
                   d.mGlowOpacity, sigma, 0.0, 3.0);
    }
    canvas->drawImage(src, sx, sy);
    if (d.mStroke) {
        const auto ring = makeStrokeRing(src, d);
        if (ring) canvas->drawImage(ring.get(), sx, sy);
    }
}

class LayerStylesEffectCaller : public RasterEffectCaller {
public:
    LayerStylesEffectCaller(const HardwareSupport hwSupport,
                            const LayerStylesData& data,
                            const QMargins& margins) :
        RasterEffectCaller(hwSupport, true, margins), mData(data) {}

    void processGpu(QGL33 * const gl, GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
private:
    const LayerStylesData mData;
};

LayerStylesEffect::LayerStylesEffect() :
    RasterEffect(QObject::tr("图层样式"),
                 AppSupport::getRasterEffectHardwareSupport("Layer Styles",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::LAYER_STYLES)
{
    const auto shadowGroup = enve::make_shared<StaticComplexAnimator>(QObject::tr("投影"));
    mShadowEnabled = enve::make_shared<BoolAnimator>(QObject::tr("启用"));
    mShadowEnabled->setCurrentBoolValue(false);
    shadowGroup->ca_addChild(mShadowEnabled);
    mShadowAngle = enve::make_shared<QrealAnimator>(120.0, 0.0, 360.0, 1.0,
                                                    QObject::tr("角度"));
    shadowGroup->ca_addChild(mShadowAngle);
    mShadowDistance = enve::make_shared<QrealAnimator>(5.0, 0.0, 999.0, 1.0,
                                                       QObject::tr("距离"));
    shadowGroup->ca_addChild(mShadowDistance);
    mShadowSpread = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0,
                                                     QObject::tr("扩展"));
    shadowGroup->ca_addChild(mShadowSpread);
    mShadowSize = enve::make_shared<QrealAnimator>(5.0, 0.0, 999.0, 0.5,
                                                   QObject::tr("大小"));
    shadowGroup->ca_addChild(mShadowSize);
    mShadowOpacity = enve::make_shared<QrealAnimator>(75.0, 0.0, 100.0, 1.0,
                                                      QObject::tr("不透明度"));
    shadowGroup->ca_addChild(mShadowOpacity);
    mShadowColor = enve::make_shared<ColorAnimator>(QObject::tr("颜色"));
    mShadowColor->setColor(QColor(0, 0, 0, 255));
    shadowGroup->ca_addChild(mShadowColor);
    ca_addChild(shadowGroup);

    const auto glowGroup = enve::make_shared<StaticComplexAnimator>(QObject::tr("外发光"));
    mGlowEnabled = enve::make_shared<BoolAnimator>(QObject::tr("启用"));
    mGlowEnabled->setCurrentBoolValue(false);
    glowGroup->ca_addChild(mGlowEnabled);
    mGlowSpread = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0,
                                                   QObject::tr("扩展"));
    glowGroup->ca_addChild(mGlowSpread);
    mGlowSize = enve::make_shared<QrealAnimator>(10.0, 0.0, 999.0, 0.5,
                                                 QObject::tr("大小"));
    glowGroup->ca_addChild(mGlowSize);
    mGlowOpacity = enve::make_shared<QrealAnimator>(75.0, 0.0, 100.0, 1.0,
                                                    QObject::tr("不透明度"));
    glowGroup->ca_addChild(mGlowOpacity);
    mGlowColor = enve::make_shared<ColorAnimator>(QObject::tr("颜色"));
    mGlowColor->setColor(QColor(255, 255, 190, 255));
    glowGroup->ca_addChild(mGlowColor);
    ca_addChild(glowGroup);

    const auto strokeGroup = enve::make_shared<StaticComplexAnimator>(QObject::tr("描边"));
    mStrokeEnabled = enve::make_shared<BoolAnimator>(QObject::tr("启用"));
    mStrokeEnabled->setCurrentBoolValue(false);
    strokeGroup->ca_addChild(mStrokeEnabled);
    mStrokePosition = enve::make_shared<ComboBoxProperty>(
                QObject::tr("位置"), QStringList()
                << QObject::tr("外部") << QObject::tr("居中") << QObject::tr("内部"));
    strokeGroup->ca_addChild(mStrokePosition);
    mStrokeSize = enve::make_shared<QrealAnimator>(3.0, 0.0, 999.0, 0.5,
                                                   QObject::tr("大小"));
    strokeGroup->ca_addChild(mStrokeSize);
    mStrokeOpacity = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0,
                                                      QObject::tr("不透明度"));
    strokeGroup->ca_addChild(mStrokeOpacity);
    mStrokeColor = enve::make_shared<ColorAnimator>(QObject::tr("颜色"));
    mStrokeColor->setColor(QColor(255, 0, 0, 255));
    strokeGroup->ca_addChild(mStrokeColor);
    ca_addChild(strokeGroup);

    // margin-affecting parameters must retrigger the forced-margin
    // machinery so the allocation bounds follow (blur pattern)
    const auto wireMargin = [this](QrealAnimator * const a) {
        connect(a, &QrealAnimator::effectiveValueChanged,
                this, &RasterEffect::forcedMarginChanged);
    };
    wireMargin(mShadowAngle.get());
    wireMargin(mShadowDistance.get());
    wireMargin(mShadowSize.get());
    wireMargin(mGlowSize.get());
    wireMargin(mStrokeSize.get());
    connect(mShadowEnabled.get(), &QrealAnimator::effectiveValueChanged,
            this, &RasterEffect::forcedMarginChanged);
    connect(mGlowEnabled.get(), &QrealAnimator::effectiveValueChanged,
            this, &RasterEffect::forcedMarginChanged);
    connect(mStrokeEnabled.get(), &QrealAnimator::effectiveValueChanged,
            this, &RasterEffect::forcedMarginChanged);
    connect(mStrokePosition.get(), &ComboBoxProperty::valueChanged,
            this, &RasterEffect::forcedMarginChanged);
}

void LayerStylesEffect::setShadow(const bool enabled, const qreal angle,
                                  const qreal distance, const qreal spread,
                                  const qreal size, const qreal opacity,
                                  const QColor& color)
{
    mShadowEnabled->setCurrentBoolValue(enabled);
    mShadowAngle->setCurrentBaseValue(angle);
    mShadowDistance->setCurrentBaseValue(distance);
    mShadowSpread->setCurrentBaseValue(spread);
    mShadowSize->setCurrentBaseValue(size);
    mShadowOpacity->setCurrentBaseValue(opacity);
    mShadowColor->setColor(color);
}

void LayerStylesEffect::setGlow(const bool enabled, const qreal spread,
                                const qreal size, const qreal opacity,
                                const QColor& color)
{
    mGlowEnabled->setCurrentBoolValue(enabled);
    mGlowSpread->setCurrentBaseValue(spread);
    mGlowSize->setCurrentBaseValue(size);
    mGlowOpacity->setCurrentBaseValue(opacity);
    mGlowColor->setColor(color);
}

void LayerStylesEffect::setStroke(const bool enabled, const int position,
                                  const qreal size, const qreal opacity,
                                  const QColor& color)
{
    mStrokeEnabled->setCurrentBoolValue(enabled);
    mStrokePosition->setCurrentValue(qBound(0, position, 2));
    mStrokeSize->setCurrentBaseValue(size);
    mStrokeOpacity->setCurrentBaseValue(opacity);
    mStrokeColor->setColor(color);
}

stdsptr<RasterEffectCaller> LayerStylesEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(data)

    LayerStylesData d;
    d.mShadow = mShadowEnabled->getBoolValue(relFrame);
    d.mGlow = mGlowEnabled->getBoolValue(relFrame);
    d.mStroke = mStrokeEnabled->getBoolValue(relFrame);
    if (!d.mShadow && !d.mGlow && !d.mStroke) return nullptr;

    int mL = 0, mT = 0, mR = 0, mB = 0;
    if (d.mShadow) {
        d.mShadowAngle = mShadowAngle->getEffectiveValue(relFrame);
        d.mShadowDist = mShadowDistance->getEffectiveValue(relFrame)
                        * resolution * influence;
        d.mShadowSpread = mShadowSpread->getEffectiveValue(relFrame) / 100.0;
        d.mShadowSize = mShadowSize->getEffectiveValue(relFrame) * resolution;
        d.mShadowOpacity = (mShadowOpacity->getEffectiveValue(relFrame) / 100.0)
                           * influence;
        d.mShadowColor = mShadowColor->getColor(relFrame);

        const qreal rad = qDegreesToRadians(d.mShadowAngle);
        const int pad = qCeil(d.mShadowSize) + qCeil(qAbs(qCos(rad)) * d.mShadowDist) + 2;
        const int padV = qCeil(d.mShadowSize) + qCeil(qAbs(qSin(rad)) * d.mShadowDist) + 2;
        mL = qMax(mL, pad); mR = qMax(mR, pad);
        mT = qMax(mT, padV); mB = qMax(mB, padV);
    }
    if (d.mGlow) {
        d.mGlowSpread = mGlowSpread->getEffectiveValue(relFrame) / 100.0;
        d.mGlowSize = mGlowSize->getEffectiveValue(relFrame) * resolution;
        d.mGlowOpacity = (mGlowOpacity->getEffectiveValue(relFrame) / 100.0)
                         * influence;
        d.mGlowColor = mGlowColor->getColor(relFrame);

        const int pad = qCeil(d.mGlowSize) + 2;
        mL = qMax(mL, pad); mT = qMax(mT, pad);
        mR = qMax(mR, pad); mB = qMax(mB, pad);
    }
    if (d.mStroke) {
        d.mStrokePos = mStrokePosition->getCurrentValue();
        d.mStrokeSize = mStrokeSize->getEffectiveValue(relFrame) * resolution;
        d.mStrokeOpacity = (mStrokeOpacity->getEffectiveValue(relFrame) / 100.0)
                           * influence;
        d.mStrokeColor = mStrokeColor->getColor(relFrame);

        const int r = d.mStrokePos == 0 ? qCeil(d.mStrokeSize) :
                      d.mStrokePos == 1 ? qCeil(d.mStrokeSize * 0.5) : 0;
        mL = qMax(mL, r); mT = qMax(mT, r);
        mR = qMax(mR, r); mB = qMax(mB, r);
    }

    return enve::make_shared<LayerStylesEffectCaller>(
                instanceHwSupport(), d, QMargins(mL, mT, mR, mB));
}

// allocation-time margin: computed from the CURRENT parameter values
// (no frame context here); must stay in sync with the per-frame
// margins in getEffectCaller
QMargins LayerStylesEffect::getMargin() const
{
    int mL = 0, mT = 0, mR = 0, mB = 0;
    if (mShadowEnabled->getBoolValue()) {
        const qreal rad = qDegreesToRadians(
                    mShadowAngle->getEffectiveValue());
        const int pad = qCeil(mShadowSize->getEffectiveValue())
                        + qCeil(qAbs(qCos(rad))
                                * mShadowDistance->getEffectiveValue()) + 2;
        const int padV = qCeil(mShadowSize->getEffectiveValue())
                         + qCeil(qAbs(qSin(rad))
                                 * mShadowDistance->getEffectiveValue()) + 2;
        mL = qMax(mL, pad); mR = qMax(mR, pad);
        mT = qMax(mT, padV); mB = qMax(mB, padV);
    }
    if (mGlowEnabled->getBoolValue()) {
        const int pad = qCeil(mGlowSize->getEffectiveValue()) + 2;
        mL = qMax(mL, pad); mT = qMax(mT, pad);
        mR = qMax(mR, pad); mB = qMax(mB, pad);
    }
    if (mStrokeEnabled->getBoolValue()) {
        const int pos = mStrokePosition->getCurrentValue();
        const qreal size = mStrokeSize->getEffectiveValue();
        const int r = pos == 0 ? qCeil(size) :
                      pos == 1 ? qCeil(size * 0.5) : 0;
        mL = qMax(mL, r); mT = qMax(mT, r);
        mR = qMax(mR, r); mB = qMax(mB, r);
    }
    return QMargins(mL, mT, mR, mB);
}

static QAtomicInt g_lsGpuLogs = { 0 };
static QAtomicInt g_lsCpuLogs = { 0 };

void LayerStylesEffectCaller::processGpu(QGL33 * const gl,
                                         GpuRenderTools &renderTools)
{
    Q_UNUSED(gl)

    renderTools.switchToSkia();
    const auto canvas = renderTools.requestTargetCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const auto srcTex = renderTools.requestSrcTextureImageWrapper();
    if (!srcTex) return;

    if (g_lsGpuLogs.fetchAndAddRelaxed(1) < 3) {
        qWarning() << "[LayerStyles] GPU compose src"
                   << srcTex->width() << "x" << srcTex->height()
                   << "shadow" << mData.mShadow << "glow" << mData.mGlow
                   << "stroke" << mData.mStroke;
    }
    composeStyles(canvas, srcTex.get(), mData, 0, 0);
    canvas->flush();

    renderTools.swapTextures();
}

void LayerStylesEffectCaller::processCpu(CpuRenderTools& renderTools,
                                         const CpuRenderData& data)
{
    if (renderTools.fSrcBtmp.empty() || renderTools.fSrcBtmp.getPixels() == nullptr ||
        renderTools.fDstBtmp.empty() || renderTools.fDstBtmp.getPixels() == nullptr) {
        return;
    }

    const auto srcImg = SkImage::MakeFromBitmap(renderTools.fSrcBtmp);
    if (!srcImg) return;

    if (g_lsCpuLogs.fetchAndAddRelaxed(1) < 3) {
        qWarning() << "[LayerStyles] CPU compose tile"
                   << data.fTexTile.left() << data.fTexTile.top()
                   << data.fTexTile.width() << "x" << data.fTexTile.height()
                   << "src" << renderTools.fSrcBtmp.width()
                   << "x" << renderTools.fSrcBtmp.height()
                   << "shadow" << mData.mShadow << "glow" << mData.mGlow
                   << "stroke" << mData.mStroke;
    }

    SkCanvas canvas(renderTools.fDstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);
    composeStyles(&canvas, srcImg.get(), mData,
                  -data.fTexTile.left(), -data.fTexTile.top());
    canvas.flush();
}
