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

// Page curl: analytic cylindrical inverse mapping of the layer image.
// The CPU path mirrors pagecurleffect.frag step by step; the source is
// premultiplied N32 (little-endian BGRA byte order), colors stay
// premultiplied end to end (a scalar shade multiplies cleanly).

#include "pagecurleffect.h"

#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Boxes/boxrenderdata.h"

#include "Animators/coloranimator.h"

#include "appsupport.h"

#include <algorithm>
#include <cmath>

namespace {

float sat1(const float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

float smooth01(const float t) {
    const float c = sat1(t);
    return c * c * (3.f - 2.f * c);
}

struct Vec4 { float r, g, b, a; };

// manual bilinear over the premultiplied N32 source (bytes: B, G, R, A)
Vec4 sampleBilinear(const SkBitmap& btmp, const int w, const int h,
                    const float u, const float v)
{
    float fx = u * float(w) - 0.5f;
    float fy = v * float(h) - 0.5f;
    fx = std::min(std::max(fx, 0.f), float(w - 1));
    fy = std::min(std::max(fy, 0.f), float(h - 1));
    const int x0 = int(fx);
    const int y0 = int(fy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float tx = fx - float(x0);
    const float ty = fy - float(y0);
    const auto px = [&](const int x, const int y) {
        const uchar* q = static_cast<const uchar*>(btmp.getAddr(x, y));
        return Vec4 { q[2] / 255.f, q[1] / 255.f, q[0] / 255.f, q[3] / 255.f };
    };
    const auto mixv = [](const Vec4& lo, const Vec4& hi, const float t) {
        return Vec4 { lo.r + (hi.r - lo.r) * t,
                      lo.g + (hi.g - lo.g) * t,
                      lo.b + (hi.b - lo.b) * t,
                      lo.a + (hi.a - lo.a) * t };
    };
    const Vec4 a = px(x0, y0);
    const Vec4 b = px(x1, y0);
    const Vec4 c = px(x0, y1);
    const Vec4 e = px(x1, y1);
    return mixv(mixv(a, b, tx), mixv(c, e, tx), ty);
}

// mirrors main() in pagecurleffect.frag step by step
Vec4 evalPageCurl(const PageCurlEffectData& d,
                  const SkBitmap& srcBtmp,
                  const float u, const float v)
{
    const float kPi = 3.14159265359f;
    const float kTwoPi = 6.28318530718f;

    int w = d.mTexW;
    int h = d.mTexH;
    if (w <= 0) w = srcBtmp.width();
    if (h <= 0) h = srcBtmp.height();
    if (w <= 0 || h <= 0) return Vec4 { 0.f, 0.f, 0.f, 0.f };
    const float aspect = float(w) / float(h);

    const float px = u * aspect;
    const float py = v;
    const float radDir = d.mDirection * kPi / 180.f;
    const float dirX = std::cos(radDir);
    const float dirY = std::sin(radDir);
    const float perpX = -dirY;
    const float perpY = dirX;

    // page extent along dir (contact line travel range)
    const float c01 = aspect * dirX;
    const float c10 = dirY;
    const float c11 = aspect * dirX + dirY;
    const float cMax = std::max(std::max(0.f, c01), std::max(c10, c11));
    const float c = (1.f - d.mProgress) * cMax;
    const float R = std::max(d.mRadius, 0.002f);

    const float dd = (px * dirX + py * dirY) - c; // signed distance from contact line
    const float m = px * perpX + py * perpY;      // position along the curl axis

    // wrapped page lands at -R*sin(phi): solve for every turn, keep the
    // frontmost (largest height R*(1-cos(phi))); a solution counts only
    // if its source point stays on the page - checking the UV after
    // picking the max lets an off-page solution shadow a valid one
    // (leaves holes and 1px stripes across the tube). Wrapped material
    // only ever lands within +-R of the contact line; outside that band
    // there is no solution (clamping -dd/R would fabricate a phantom
    // phi=pi/2 one that overwrites the flat page above the tube)
    float bestZ = -1.f;
    float bestPhi = -1.f;
    float bestU = 0.f;
    float bestV = 0.f;
    if (std::abs(dd) <= R) {
        const float q = -dd / R;
        const float phi0 = std::asin(q);
        const float phiCap = 11.5f * kPi;
        const auto consider = [&](const float phi) {
            if (phi < 0.f || phi > phiCap) return;
            const float z = R * (1.f - std::cos(phi));
            if (z <= bestZ) return;
            const float s0 = c + R * phi;
            const float p0x = dirX * s0 + perpX * m;
            const float p0y = dirY * s0 + perpY * m;
            const float u0 = p0x / aspect;
            const float v0 = p0y;
            if (u0 < 0.f || u0 > 1.f || v0 < 0.f || v0 > 1.f) return;
            bestZ = z; bestPhi = phi; bestU = u0; bestV = v0;
        };
        for (int k = 0; k < 8; k++) {
            const float base = kTwoPi * float(k);
            consider(phi0 + base);
            consider(kPi - phi0 + base);
            if (phi0 + base > phiCap) break;
        }
    }

    const bool useWrap = bestPhi >= 0.f;
    float uvX = u, uvY = v;
    if (useWrap) { uvX = bestU; uvY = bestV; }

    // no tube above and not on the flat side: the page has left
    if (!useWrap && dd > 0.f) {
        return Vec4 { 0.f, 0.f, 0.f, 0.f };
    }

    // light (image-space y points down; 225 deg = from the upper left)
    const float la = d.mLightAngle * kPi / 180.f;
    const float el = d.mLightElev * kPi / 180.f;
    const float cel = std::cos(el);
    const float lX = std::cos(la) * cel;
    const float lY = std::sin(la) * cel;
    const float Lz = std::sin(el);
    const float Ls = lX * dirX + lY * dirY;
    // half vector with the orthographic view direction (0,0,1)
    float hX = lX, hY = lY;
    float Hz = Lz + 1.f;
    const float hLen = std::max(std::sqrt(hX * hX + hY * hY + Hz * Hz), 0.0001f);
    hX /= hLen; hY /= hLen; Hz /= hLen;
    const float Hs = hX * dirX + hY * dirY;

    const Vec4 src = sampleBilinear(srcBtmp, w, h, uvX, uvY);

    float shade;
    float spec = 0.f;
    bool backFace = false;
    if (useWrap) {
        const float sinPhi = std::sin(bestPhi);
        const float cosPhi = std::cos(bestPhi);
        float nDotL = sinPhi * Ls + cosPhi * Lz;
        float nDotH = sinPhi * Hs + cosPhi * Hz;
        backFace = cosPhi < 0.f; // the flipped side faces the camera
        if (backFace) { nDotL = -nDotL; nDotH = -nDotH; }
        shade = d.mAmbient + (1.f - d.mAmbient) * std::max(0.f, nDotL);
        spec = d.mSpecular * std::pow(std::max(0.f, nDotH), 32.f);
    } else {
        // flat part: fade the lighting in with the amount of wrapped
        // material so progress 0 is an exact passthrough
        const float arcAvail = (cMax - c) / R;
        const float active = smooth01(arcAvail / kPi);
        const float litFlat = d.mAmbient + (1.f - d.mAmbient) * std::max(0.f, Lz);
        shade = 1.f + (litFlat - 1.f) * active;
        // the raised tube shades the flat part behind the contact line
        if (active > 0.f) {
            const float A = Ls * dd - Lz * R;
            const float disc = A * A - dd * dd;
            if (disc > 0.f) {
                const float sd = std::sqrt(disc);
                if (sd > A) { // a positive ray parameter exists
                    const float pen = std::min(std::max(0.2f * R, 0.004f), 0.05f);
                    const float occ = 1.f - smooth01(sd / pen);
                    shade *= 1.f - d.mShadow * active * occ;
                }
            }
        }
    }

    Vec4 out;
    if (backFace) {
        const float a = src.a;
        out.r = sat1(d.mBackR * (a * shade) + spec * a);
        out.g = sat1(d.mBackG * (a * shade) + spec * a);
        out.b = sat1(d.mBackB * (a * shade) + spec * a);
        out.a = sat1(a);
    } else {
        out.r = sat1(src.r * shade + spec * src.a);
        out.g = sat1(src.g * shade + spec * src.a);
        out.b = sat1(src.b * shade + spec * src.a);
        out.a = sat1(src.a);
    }
    return out;
}

} // namespace

PageCurlEffect::PageCurlEffect() :
    RasterEffect(QObject::tr("卷页 (Page Curl)"),
                 AppSupport::getRasterEffectHardwareSupport("PageCurl",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::PAGE_CURL)
{
    mProgress = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                                 QObject::tr("卷曲进度"));
    ca_addChild(mProgress);

    mDirection = enve::make_shared<QrealAnimator>(0, 0, 360, 1,
                                                  QObject::tr("卷曲方向"));
    ca_addChild(mDirection);

    mRadius = enve::make_shared<QrealAnimator>(12, 1, 50, 1,
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
}

class PageCurlEffectCaller : public OpenGLRasterEffectCaller {
public:
    PageCurlEffectCaller(const HardwareSupport hwSupport,
                         const PageCurlEffectData& data) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/pagecurleffect.frag",
                                 hwSupport),
        mData(data) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sProgressU = gl->glGetUniformLocation(sProgramId, "uProgress");
        sDirectionU = gl->glGetUniformLocation(sProgramId, "uDirection");
        sRadiusU = gl->glGetUniformLocation(sProgramId, "uRadius");
        sBackColorU = gl->glGetUniformLocation(sProgramId, "uBackColor");
        sLightAngleU = gl->glGetUniformLocation(sProgramId, "uLightAngle");
        sLightElevU = gl->glGetUniformLocation(sProgramId, "uLightElev");
        sAmbientU = gl->glGetUniformLocation(sProgramId, "uAmbient");
        sShadowU = gl->glGetUniformLocation(sProgramId, "uShadow");
        sSpecularU = gl->glGetUniformLocation(sProgramId, "uSpecular");
        sTexSizeU = gl->glGetUniformLocation(sProgramId, "uTexSize");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sProgressU, mData.mProgress);
        gl->glUniform1f(sDirectionU, mData.mDirection);
        gl->glUniform1f(sRadiusU, mData.mRadius);
        gl->glUniform3f(sBackColorU, mData.mBackR, mData.mBackG, mData.mBackB);
        gl->glUniform1f(sLightAngleU, mData.mLightAngle);
        gl->glUniform1f(sLightElevU, mData.mLightElev);
        gl->glUniform1f(sAmbientU, mData.mAmbient);
        gl->glUniform1f(sShadowU, mData.mShadow);
        gl->glUniform1f(sSpecularU, mData.mSpecular);
        gl->glUniform2f(sTexSizeU,
                        static_cast<GLfloat>(std::max(mData.mTexW, 1)),
                        static_cast<GLfloat>(std::max(mData.mTexH, 1)));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sProgressU;
    static GLint sDirectionU;
    static GLint sRadiusU;
    static GLint sBackColorU;
    static GLint sLightAngleU;
    static GLint sLightElevU;
    static GLint sAmbientU;
    static GLint sShadowU;
    static GLint sSpecularU;
    static GLint sTexSizeU;

    const PageCurlEffectData mData;
};

bool PageCurlEffectCaller::sInitialized = false;
GLuint PageCurlEffectCaller::sProgramId = 0;

GLint PageCurlEffectCaller::sProgressU = -1;
GLint PageCurlEffectCaller::sDirectionU = -1;
GLint PageCurlEffectCaller::sRadiusU = -1;
GLint PageCurlEffectCaller::sBackColorU = -1;
GLint PageCurlEffectCaller::sLightAngleU = -1;
GLint PageCurlEffectCaller::sLightElevU = -1;
GLint PageCurlEffectCaller::sAmbientU = -1;
GLint PageCurlEffectCaller::sShadowU = -1;
GLint PageCurlEffectCaller::sSpecularU = -1;
GLint PageCurlEffectCaller::sTexSizeU = -1;

stdsptr<RasterEffectCaller> PageCurlEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)

    PageCurlEffectData effData;
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
    if (data) {
        effData.mTexW = data->fGlobalRect.width();
        effData.mTexH = data->fGlobalRect.height();
    }

    return enve::make_shared<PageCurlEffectCaller>(
                instanceHwSupport(), effData);
}

void PageCurlEffectCaller::processCpu(CpuRenderTools& renderTools,
                                      const CpuRenderData& data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;

    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }

    const int imgWidth = srcBtmp.width();
    const int imgHeight = srcBtmp.height();
    if (imgWidth <= 0 || imgHeight <= 0) return;

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right(), imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom(), imgHeight - 1);

    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const float v = (float(yi) + 0.5f) / float(imgHeight);

        for (int xi = xMin; xi <= xMax; xi++) {
            const float u = (float(xi) + 0.5f) / float(imgWidth);
            const Vec4 out = evalPageCurl(mData, srcBtmp, u, v);

            // N32 on little-endian is BGRA in memory
            *dst++ = static_cast<uchar>(out.b * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(out.g * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(out.r * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(out.a * 255.f + 0.5f);
        }
    }
}
