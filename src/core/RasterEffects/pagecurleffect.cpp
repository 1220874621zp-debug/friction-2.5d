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

// Page curl / wave / page turn, rendered the lattice-warp way: a
// forward-warped mesh redrawn with SkCanvas::drawVertices (skin-mesh
// style). The source bitmap is the texture, N.L lighting and the cast
// shadow are baked into vertex colors, and the mesh is drawn twice
// (textured front pass with a cos(phi)>=0 alpha mask, solid back pass
// with the complementary mask). CPU-only like LatticeWarpEffect.
//
// Modes:
//  curl (0) - the page rolls up around a cylinder of radius R; the
//             radius can grow per turn (spiral); the contact line can
//             be slanted (corner-curl look)
//  wave (1) - no displacement, a traveling N.L ripple in the vertex
//             colors (optionally crossed with a second wave)
//  turn (2) - like curl, but wrapping stops at half a turn and the
//             rest of the page lies flat on top, flipped - the page
//             turn transition end state

#include "pagecurleffect.h"

#include "cpurendertools.h"
#include "skia/skiaincludes.h"
#include "include/core/SkVertices.h"
#include "Boxes/boxrenderdata.h"
#include "MovablePoints/pointshandler.h"
#include "RasterEffects/effectcanvaspoint.h"

#include "Animators/coloranimator.h"
#include "Animators/qpointfanimator.h"
#include "Properties/comboboxproperty.h"

#include "appsupport.h"

#include <algorithm>
#include <cmath>

namespace {

float sat1(const float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

float smooth01(const float t) {
    const float c = sat1(t);
    return c * c * (3.f - 2.f * c);
}

// light basis shared by all modes
struct LightBasis {
    float lX, lY, Lz;   // light direction (x,y in pixel space, z up)
    float hX, hY, Hz;   // half vector with the view direction (0,0,1)
};

LightBasis makeLight(const PageCurlEffectData& d)
{
    const float kPi = 3.14159265359f;
    const float la = d.mLightAngle * kPi / 180.f;
    const float el = d.mLightElev * kPi / 180.f;
    const float cel = std::cos(el);
    LightBasis lb;
    lb.lX = std::cos(la) * cel;
    lb.lY = std::sin(la) * cel;
    lb.Lz = std::sin(el);
    float hx = lb.lX, hy = lb.lY;
    float hz = lb.Lz + 1.f;
    const float hl = std::max(std::sqrt(hx * hx + hy * hy + hz * hz), 0.0001f);
    lb.hX = hx / hl; lb.hY = hy / hl; lb.Hz = hz / hl;
    return lb;
}

// spiral wrap: dR/dphi = R0*spiral/(2*pi), so the arc length obeys
// t(phi) = R0*(phi + a*phi^2) with a = spiral/(4*pi) - invertible in
// closed form
float spiralRadius(const float R0, const float spiral, const float phi)
{
    return R0 * (1.f + spiral * phi / 6.28318530718f);
}

float spiralPhi(const float R0, const float spiral, const float t)
{
    if (spiral <= 0.0001f) return t / R0;
    const float a = spiral / 12.56637061436f; // spiral/(4*pi)
    const float arg = 1.f + 4.f * a * t / R0;
    if (arg <= 0.f) return 0.f;
    return (-1.f + std::sqrt(arg)) / (2.f * a);
}

} // namespace

PageCurlEffect::PageCurlEffect() :
    RasterEffect(QObject::tr("卷页 (Page Curl)"),
                 AppSupport::getRasterEffectHardwareSupport("PageCurl",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::PAGE_CURL)
{
    const auto modes = QStringList() <<
            QObject::tr("卷页") <<
            QObject::tr("波浪") <<
            QObject::tr("翻页");
    mMode = enve::make_shared<ComboBoxProperty>(QObject::tr("模式"), modes);
    // default to the wave: it keeps the whole image visible, so a fresh
    // effect never looks like it destroyed the layer
    mMode->setCurrentValue(1);
    ca_addChild(mMode);

    mProgress = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                                 QObject::tr("卷曲进度"));
    ca_addChild(mProgress);

    mDirection = enve::make_shared<QrealAnimator>(0, 0, 360, 1,
                                                  QObject::tr("卷曲方向"));
    ca_addChild(mDirection);

    mRadius = enve::make_shared<QrealAnimator>(8, 1, 50, 1,
                                               QObject::tr("卷曲半径"));
    ca_addChild(mRadius);

    mBackColor = enve::make_shared<ColorAnimator>(QObject::tr("背面颜色"));
    mBackColor->setColor(QColor(216, 216, 216));
    ca_addChild(mBackColor);

    mLightAngle = enve::make_shared<QrealAnimator>(225, 0, 360, 1,
                                                   QObject::tr("光源角度"));
    ca_addChild(mLightAngle);

    mLightElev = enve::make_shared<QrealAnimator>(50, 5, 90, 1,
                                                  QObject::tr("光源高度"));
    ca_addChild(mLightElev);

    mAmbient = enve::make_shared<QrealAnimator>(55, 0, 100, 1,
                                                QObject::tr("环境光"));
    ca_addChild(mAmbient);

    mShadow = enve::make_shared<QrealAnimator>(45, 0, 100, 1,
                                               QObject::tr("阴影强度"));
    ca_addChild(mShadow);

    mSpecular = enve::make_shared<QrealAnimator>(25, 0, 100, 1,
                                                 QObject::tr("高光强度"));
    ca_addChild(mSpecular);

    mWaveAmp = enve::make_shared<QrealAnimator>(12, 0, 30, 1,
                                                QObject::tr("波浪幅度"));
    ca_addChild(mWaveAmp);

    mWaveLen = enve::make_shared<QrealAnimator>(35, 5, 100, 1,
                                                QObject::tr("波浪长度"));
    ca_addChild(mWaveLen);

    mWaveSpeed = enve::make_shared<QrealAnimator>(15, -100, 100, 1,
                                                  QObject::tr("波浪速度"));
    ca_addChild(mWaveSpeed);

    mSlant = enve::make_shared<QrealAnimator>(0, -100, 100, 1,
                                              QObject::tr("前沿斜度"));
    ca_addChild(mSlant);

    mPerspective = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                                    QObject::tr("透视强度"));
    ca_addChild(mPerspective);

    mSpiral = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                               QObject::tr("螺旋递增"));
    ca_addChild(mSpiral);

    mCrossWave = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                                  QObject::tr("交叉波浪"));
    ca_addChild(mCrossWave);

    // canvas-handle control: the point's angle from the image center
    // drives the roll direction, its distance drives the progress
    const auto ctrlSrcs = QStringList() <<
            QObject::tr("滑块") <<
            QObject::tr("画布点");
    mCtrlSrc = enve::make_shared<ComboBoxProperty>(QObject::tr("控制来源"), ctrlSrcs);
    ca_addChild(mCtrlSrc);

    mCtrlPoint = enve::make_shared<QPointFAnimator>(QObject::tr("卷曲控制点"));
    mCtrlPoint->setBaseValue(0.5, 0.5);
    ca_addChild(mCtrlPoint);

    setPointsHandler(enve::make_shared<PointsHandler>());
    getPointsHandler()->appendPt(enve::make_shared<EffectCanvasPoint>(
                mCtrlPoint.get(), this, EffectCanvasPoint::Space::Normalized));
}

class PageCurlEffectCaller : public RasterEffectCaller {
public:
    PageCurlEffectCaller(const PageCurlEffectData& data) :
        RasterEffectCaller(HardwareSupport::cpuOnly), mData(data) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
private:
    const PageCurlEffectData mData;
};

stdsptr<RasterEffectCaller> PageCurlEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)

    PageCurlEffectData effData;
    effData.mMode = mMode->getCurrentValue();
    effData.mProgress = static_cast<float>(
                qBound(0.0, mProgress->getEffectiveValue(relFrame) * 0.01 * influence, 1.0));
    effData.mDirection = static_cast<float>(mDirection->getEffectiveValue(relFrame));
    effData.mRadius = static_cast<float>(mRadius->getEffectiveValue(relFrame) * 0.01);
    const QColor back = mBackColor->getColor(relFrame);
    effData.mBackR = static_cast<float>(back.redF());
    effData.mBackG = static_cast<float>(back.greenF());
    effData.mBackB = static_cast<float>(back.blueF());
    effData.mLightAngle = static_cast<float>(mLightAngle->getEffectiveValue(relFrame));
    effData.mLightElev = static_cast<float>(mLightElev->getEffectiveValue(relFrame));
    effData.mAmbient = static_cast<float>(mAmbient->getEffectiveValue(relFrame) * 0.01);
    effData.mShadow = static_cast<float>(mShadow->getEffectiveValue(relFrame) * 0.01);
    effData.mSpecular = static_cast<float>(mSpecular->getEffectiveValue(relFrame) * 0.01);
    effData.mWaveAmp = static_cast<float>(mWaveAmp->getEffectiveValue(relFrame) * 0.01);
    effData.mWaveLen = static_cast<float>(mWaveLen->getEffectiveValue(relFrame));
    effData.mWavePhase = static_cast<float>(
                relFrame * mWaveSpeed->getEffectiveValue(relFrame) * 0.05);
    effData.mSlant = static_cast<float>(mSlant->getEffectiveValue(relFrame) * 0.01);
    effData.mPerspective = static_cast<float>(mPerspective->getEffectiveValue(relFrame) * 0.01);
    effData.mSpiral = static_cast<float>(mSpiral->getEffectiveValue(relFrame) * 0.01);
    effData.mCrossWave = static_cast<float>(mCrossWave->getEffectiveValue(relFrame) * 0.01);

    if (mCtrlSrc->getCurrentValue() == 1) {
        // canvas-handle drive: angle from the image center = direction,
        // distance (aspect-corrected, image-height units) = progress
        const QPointF p = mCtrlPoint->getEffectiveValue(relFrame);
        double aspect = 1.0;
        if (data && data->fGlobalRect.height() > 0) {
            aspect = double(data->fGlobalRect.width()) /
                     double(data->fGlobalRect.height());
        }
        const double vx = (p.x() - 0.5) * aspect;
        const double vy = p.y() - 0.5;
        const double len = std::sqrt(vx * vx + vy * vy);
        double ang = std::atan2(vy, vx) * 180.0 / M_PI;
        if (ang < 0.0) ang += 360.0;
        effData.mDirection = static_cast<float>(ang);
        effData.mProgress = static_cast<float>(qBound(0.0, len / 0.55, 1.0));
    }

    if (data) {
        effData.mTexW = data->fGlobalRect.width();
        effData.mTexH = data->fGlobalRect.height();
    }

    return enve::make_shared<PageCurlEffectCaller>(effData);
}

void PageCurlEffectCaller::processCpu(CpuRenderTools& renderTools,
                                      const CpuRenderData& data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    auto& dstBtmp = renderTools.fDstBtmp;
    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }
    const int w = srcBtmp.width();
    const int h = srcBtmp.height();
    if (w <= 0 || h <= 0) return;

    const float kPi = 3.14159265359f;
    const float kTwoPi = 6.28318530718f;

    const float radDir = mData.mDirection * kPi / 180.f;
    const float dirX = std::cos(radDir);
    const float dirY = std::sin(radDir);
    const float perpX = -dirY;
    const float perpY = dirX;

    // material range in (s, m) pixel coordinates (s along dir)
    const float cornersX[4] = {0.f, float(w), 0.f, float(w)};
    const float cornersY[4] = {0.f, 0.f, float(h), float(h)};
    float sMin = 1e30f, sMax = -1e30f, mMin = 1e30f, mMax = -1e30f;
    for (int i = 0; i < 4; i++) {
        const float s = cornersX[i] * dirX + cornersY[i] * dirY;
        const float m = cornersX[i] * perpX + cornersY[i] * perpY;
        sMin = std::min(sMin, s); sMax = std::max(sMax, s);
        mMin = std::min(mMin, m); mMax = std::max(mMax, m);
    }
    const float mMid = 0.5f * (mMin + mMax);

    const LightBasis lb = makeLight(mData);
    const float Ls = dirX * lb.lX + dirY * lb.lY;
    const float Hs = dirX * lb.hX + dirY * lb.hY;

    // mesh density: fine along s (the curl is curved), coarser across
    int nS = std::max(64, std::min(220, int((sMax - sMin) / 4.f) + 1));
    int nM = std::max(8, std::min(120, int((mMax - mMin) / 8.f) + 1));
    while ((nS + 1) * (nM + 1) > 65000) { nS = nS * 3 / 4; nM = nM * 3 / 4; }

    const int nVx = (nS + 1) * (nM + 1);
    QVector<SkPoint> pos(nVx);
    QVector<SkPoint> tex(nVx);
    QVector<SkColor> colFront(nVx);
    QVector<SkColor> colBack(nVx);

    const float c = (1.f - mData.mProgress) * sMax;
    const float R = std::max(mData.mRadius * float(h), 1.f);
    const float edgeFade = std::max(4.f, 0.02f * float(std::max(w, h)));

    // perspective: f in image-height units; 0 strength ~ orthographic
    const float fPersp = mData.mPerspective > 0.001f
            ? float(h) * (0.3f + 20.f * (1.f - mData.mPerspective))
            : -1.f;
    const float cx0 = 0.5f * float(w);
    const float cy0 = 0.5f * float(h);

    // flat-part lighting fades in with the wrapped arc so progress 0 is
    // an exact passthrough (vertex colors = opaque white)
    const float arcAvail = (sMax - c) / R;
    const float active = smooth01(arcAvail / kPi);
    const float litFlat = mData.mAmbient +
            (1.f - mData.mAmbient) * std::max(0.f, lb.Lz);
    const float flatShade0 = 1.f + (litFlat - 1.f) * active;

    const bool isWave = mData.mMode == 1;
    const bool isTurn = mData.mMode == 2;
    const float tCapHalfTurn = R * kPi; // turn mode: wrap stops at half a turn

    for (int b = 0; b <= nM; b++) {
        const float m = mMin + (mMax - mMin) * float(b) / float(nM);
        // slanted contact line: per-row offset (corner-curl look)
        const float cLocal = isWave ? c : c + mData.mSlant * (m - mMid);
        for (int a = 0; a <= nS; a++) {
            const float s0 = sMin + (sMax - sMin) * float(a) / float(nS);
            const int id = b * (nS + 1) + a;

            const float px = dirX * s0 + perpX * m;
            const float py = dirY * s0 + perpY * m;
            tex[id] = SkPoint::Make(px, py);

            float sx = px;
            float sy = py;
            float z = 0.f;
            float frontA = 1.f;
            float frontShade = flatShade0;
            float backA = 0.f;
            float backShade = 0.f;

            const float t = s0 - cLocal;

            if (isWave) {
                // wave: no in-plane displacement, a traveling N.L ripple
                // in the vertex colors (optionally crossed across m);
                // fade the whole effect in with amplitude
                const float A = mData.mWaveAmp * float(h);
                const float rep = 100.f / std::max(mData.mWaveLen, 1.f);
                const float omegaS = kTwoPi * rep / std::max(sMax - sMin, 1.f);
                const float omegaM = kTwoPi * rep / std::max(mMax - mMin, 1.f);
                const float zslope = A * omegaS *
                        std::cos(omegaS * s0 + mData.mWavePhase);
                const float mslope = A * mData.mCrossWave * omegaM *
                        std::cos(omegaM * m + mData.mWavePhase + 1.57f);
                z = A * std::sin(omegaS * s0 + mData.mWavePhase) +
                    A * mData.mCrossWave *
                        std::sin(omegaM * m + mData.mWavePhase + 1.57f);
                float nX = -zslope * dirX - mslope * perpX;
                float nY = -zslope * dirY - mslope * perpY;
                const float nLen = std::max(std::sqrt(nX * nX + nY * nY + 1.f),
                                            0.0001f);
                nX /= nLen; nY /= nLen;
                const float nZ = 1.f / nLen;
                float nDotL = nX * lb.lX + nY * lb.lY + nZ * lb.Lz;
                float nDotH = nX * lb.hX + nY * lb.hY + nZ * lb.Hz;
                float shade = mData.mAmbient +
                        (1.f - mData.mAmbient) * std::max(0.f, nDotL);
                float spec = mData.mSpecular *
                        std::pow(std::max(0.f, nDotH), 32.f);
                const float tFade = smooth01(mData.mWaveAmp / 0.03f);
                shade = 1.f + (shade - 1.f) * tFade;
                spec *= tFade;
                frontShade = sat1(shade + spec * 0.5f);
            } else if (t > 0.f) {
                if (isTurn && t > tCapHalfTurn) {
                    // page turn: past half a turn the rest lies flat on
                    // top, flipped - the transition end state
                    const float L = t - tCapHalfTurn;
                    const float screenS = cLocal + L;
                    sx = dirX * screenS + perpX * m;
                    sy = dirY * screenS + perpY * m;
                    z = 2.f * R;
                    const float nDotL = -lb.Lz;
                    const float nDotH = -lb.Hz;
                    frontA = 0.f;
                    backA = 1.f;
                    backShade = mData.mAmbient +
                            (1.f - mData.mAmbient) * std::max(0.f, nDotL);
                } else {
                    // wrapped around the (possibly growing) cylinder
                    const float phi = isTurn ? t / R :
                            spiralPhi(R, mData.mSpiral, t);
                    const float Rw = isTurn ? R :
                            spiralRadius(R, mData.mSpiral, phi);
                    const float sinPhi = std::sin(phi);
                    const float cosPhi = std::cos(phi);
                    const float screenS = cLocal - Rw * sinPhi;
                    sx = dirX * screenS + perpX * m;
                    sy = dirY * screenS + perpY * m;
                    z = Rw * (1.f - cosPhi);
                    float nDotL = sinPhi * Ls + cosPhi * lb.Lz;
                    float nDotH = sinPhi * Hs + cosPhi * lb.Hz;
                    if (cosPhi >= 0.f) {
                        const float shade = mData.mAmbient +
                                (1.f - mData.mAmbient) * std::max(0.f, nDotL);
                        const float spec = mData.mSpecular *
                                std::pow(std::max(0.f, nDotH), 32.f);
                        frontShade = sat1(shade + spec * 0.5f);
                    } else {
                        // the flipped side faces the camera: back pass
                        frontA = 0.f;
                        backA = 1.f;
                        backShade = mData.mAmbient +
                                (1.f - mData.mAmbient) * std::max(0.f, -nDotL);
                    }
                }
            } else if (active > 0.f) {
                // flat part under the raised tube: cast-shadow test
                // (ray from the pixel toward the light vs the cylinder)
                const float d = t;
                const float A2 = Ls * d - lb.Lz * R;
                const float disc = A2 * A2 - d * d;
                if (disc > 0.f) {
                    const float sd = std::sqrt(disc);
                    if (sd > A2) {
                        const float pen = std::min(std::max(0.2f * R, 1.f),
                                                    float(h) * 0.05f + 1.f);
                        const float occ = 1.f - smooth01(sd / pen);
                        frontShade *= 1.f - mData.mShadow * active * occ;
                    }
                }
            }

            // fade alpha where the vertex samples outside the bitmap
            const float dxo = std::max(0.f, std::max(-px, px - float(w)));
            const float dyo = std::max(0.f, std::max(-py, py - float(h)));
            const float dOut = std::max(dxo, dyo);
            float edgeA = 1.f;
            if (dOut > 0.f) {
                edgeA = 1.f - sat1(dOut / edgeFade);
            }

            // perspective divide (orthographic when disabled): pulls
            // raised geometry toward the viewer, opening up the tube
            if (fPersp > 0.f && z > 0.001f) {
                const float scale = fPersp / (fPersp + z);
                sx = cx0 + (sx - cx0) * scale;
                sy = cy0 + (sy - cy0) * scale;
            }

            pos[id] = SkPoint::Make(sx, sy);
            const int shadeByte = qRound(sat1(frontShade) * 255.f);
            colFront[id] = SkColorSetARGB(
                        qRound(sat1(frontA * edgeA) * 255.f),
                        shadeByte, shadeByte, shadeByte);
            const int backByte = qRound(sat1(backShade) * 255.f);
            colBack[id] = SkColorSetARGB(
                        qRound(sat1(backA * edgeA) * 255.f),
                        backByte, backByte, backByte);
        }
    }

    // grid indices; later s (outer wraps) draw over earlier ones, which
    // approximates the outer turn occluding the inner turns
    QVector<uint16_t> idx;
    idx.reserve(nS * nM * 6);
    for (int b = 0; b < nM; b++) {
        for (int a = 0; a < nS; a++) {
            const int i0 = b * (nS + 1) + a;
            const int i1 = i0 + 1;
            const int i2 = i0 + (nS + 1);
            const int i3 = i2 + 1;
            idx << uint16_t(i0) << uint16_t(i2) << uint16_t(i1)
                << uint16_t(i1) << uint16_t(i2) << uint16_t(i3);
        }
    }

    const sk_sp<SkImage> img = SkImage::MakeFromBitmap(srcBtmp);
    if (!img) return;

    SkCanvas canvas(dstBtmp);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.translate(-data.fTexTile.left(), -data.fTexTile.top());

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setFilterQuality(kMedium_SkFilterQuality);

    if (isWave) {
        // wave: one textured pass, all front
        paint.setShader(img->makeShader(SkTileMode::kClamp,
                                        SkTileMode::kClamp,
                                        nullptr));
        const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
                    SkVertices::kTriangles_VertexMode,
                    pos.count(), pos.constData(), tex.constData(),
                    colFront.constData(),
                    idx.count(), idx.constData());
        canvas.drawVertices(vertices.get(), SkBlendMode::kModulate, paint);
        return;
    }

    // curl / turn: textured front pass masked by cos(phi)>=0, then a
    // solid back pass with the complementary mask
    paint.setShader(img->makeShader(SkTileMode::kClamp,
                                    SkTileMode::kClamp,
                                    nullptr));
    sk_sp<SkVertices> front = SkVertices::MakeCopy(
                SkVertices::kTriangles_VertexMode,
                pos.count(), pos.constData(), tex.constData(),
                colFront.constData(),
                idx.count(), idx.constData());
    canvas.drawVertices(front.get(), SkBlendMode::kModulate, paint);

    if (mData.mProgress > 0.f) {
        SkPaint backPaint;
        backPaint.setAntiAlias(true);
        backPaint.setColor(SkColorSetRGB(
                    qRound(mData.mBackR * 255.f),
                    qRound(mData.mBackG * 255.f),
                    qRound(mData.mBackB * 255.f)));
        const sk_sp<SkVertices> back = SkVertices::MakeCopy(
                    SkVertices::kTriangles_VertexMode,
                    pos.count(), pos.constData(), tex.constData(),
                    colBack.constData(),
                    idx.count(), idx.constData());
        canvas.drawVertices(back.get(), SkBlendMode::kModulate, backPaint);
    }
}
