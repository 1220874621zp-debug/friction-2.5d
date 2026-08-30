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

// Liquid glass (magnifier) distortion ported from a Shadertoy shader
// https://gist.github.com/emmachase/25af1fb66daebf0f9989c93d3c8c5fa6
// The CPU path mirrors liquidglasseffect.frag step by step; colors stay
// premultiplied and the glow result is clamped against the alpha.

#include "liquidglasseffect.h"

#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Boxes/boxrenderdata.h"
#include "appsupport.h"

#include <algorithm>
#include <cmath>

namespace {

const float kM_E = 2.718281828459045f;

float sat1(const float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Mirrors f_func() in liquidglasseffect.frag
float f_func(const float x)
{
    const float u_a = 0.7f;
    const float u_b = 2.3f;
    const float u_c = 5.2f;
    const float u_d = 6.9f;
    return 1.f - u_b * std::pow(u_c * kM_E, -u_d * x - u_a);
}

// Mirrors rand() in liquidglasseffect.frag
float rand2d(const float x, const float y)
{
    const float d = 12.9898f * x + 78.233f * y;
    float v = std::sin(d) * 43758.5453f;
    v = v - std::floor(v);
    return v;
}

// Mirrors sdSuperellipse() in liquidglasseffect.frag
float sdSuperellipse(const float px, const float py,
                     const float n, const float r)
{
    const float ax = std::abs(px);
    const float ay = std::abs(py);
    const float numerator = std::pow(ax, n) + std::pow(ay, n) - std::pow(r, n);
    const float den_x = std::pow(ax, 2.f * n - 2.f);
    const float den_y = std::pow(ay, 2.f * n - 2.f);
    const float denominator = n * std::sqrt(den_x + den_y) + 1e-5f;
    return numerator / denominator;
}

float smoothstepf(const float e0, const float e1, const float x)
{
    const float t = sat1((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

// Mirrors glowPattern() in liquidglasseffect.frag
float glowPattern(const float lu, const float lv)
{
    const float cx = lu * 2.f - 1.f;
    const float cy = lv * 2.f - 1.f;
    const float radius = std::sqrt(cx * cx + cy * cy);
    const float angleFactor = smoothstepf(0.f, 0.9f, radius);
    return std::sin(std::atan2(cy, cx) - 0.5f) * angleFactor;
}

// bilinear sample of a premultiplied src bitmap; uv is in GL space
// (y up), bitmap row 0 is the top; coords clamp to the edge
void sampleBilinear(const SkBitmap& src, const int w, const int h,
                    const float u, const float v,
                    float& r, float& g, float& b, float& a)
{
    float fx = u * float(w) - 0.5f;
    float fy = (1.f - v) * float(h) - 0.5f;
    fx = std::min(std::max(fx, 0.f), float(w - 1));
    fy = std::min(std::max(fy, 0.f), float(h - 1));
    int x0 = int(std::floor(fx));
    int y0 = int(std::floor(fy));
    int x1 = std::min(x0 + 1, w - 1);
    int y1 = std::min(y0 + 1, h - 1);
    const float tx = fx - float(x0);
    const float ty = fy - float(y0);
    const uchar* s00 = static_cast<const uchar*>(src.getAddr(x0, y0));
    const uchar* s10 = static_cast<const uchar*>(src.getAddr(x1, y0));
    const uchar* s01 = static_cast<const uchar*>(src.getAddr(x0, y1));
    const uchar* s11 = static_cast<const uchar*>(src.getAddr(x1, y1));
    const uchar* chans[4] = {s00, s10, s01, s11};
    float out[4];
    for (int c = 0; c < 4; c++) {
        const float top = chans[0][c] / 255.f +
                (chans[1][c] - chans[0][c]) / 255.f * tx;
        const float bot = chans[2][c] / 255.f +
                (chans[3][c] - chans[2][c]) / 255.f * tx;
        out[c] = top + (bot - top) * ty;
    }
    r = out[0]; g = out[1]; b = out[2]; a = out[3];
}

// Mirrors main() in liquidglasseffect.frag step by step
void processPixel(const LiquidGlassEffectData& d,
                  const SkBitmap& src, const int w, const int h,
                  const float u, const float v,
                  const float noiseX, const float noiseY,
                  const float srcR, const float srcG,
                  const float srcB, const float srcA,
                  float& rOut, float& gOut, float& bOut, float& aOut)
{
    const float aspectX = float(w) / float(h);

    // local space centered on the effect, aspect corrected
    const float px = (u - d.mCenterX) * aspectX / d.mSize;
    const float py = (v - d.mCenterY) / d.mSize;

    const float dd = sdSuperellipse(px, py, d.mShapeN, 1.f);

    // outside the shape -> pass the source pixel through untouched
    if (dd > 0.f) {
        rOut = srcR; gOut = srcG; bOut = srcB; aOut = srcA;
        return;
    }

    const float dist = -dd;
    float fval = f_func(dist);
    if (fval < 0.001f) fval = 0.001f;
    const float scale = std::pow(fval, d.mRefraction);
    const float sampleX = px * scale;
    const float sampleY = py * scale;

    // scale back to uv space; +0.1 offset and edge clamping as in the
    // original (out-of-bounds returned debug magenta there)
    float targetU = sampleX * d.mSize / aspectX + d.mCenterX + 0.1f;
    float targetV = sampleY * d.mSize + d.mCenterY + 0.1f;
    targetU = sat1(targetU);
    targetV = sat1(targetV);

    float cr, cg, cb, ca;
    sampleBilinear(src, w, h, targetU, targetV, cr, cg, cb, ca);

    // static grain (screen-space, as in the original)
    const float noise = (rand2d(noiseX, noiseY) - 0.5f) * d.mNoise;

    // glow multiplier: angular pattern near the rim + bias
    const float localU = (u - d.mCenterX) / d.mSize + 0.5f;
    const float localV = (v - d.mCenterY) / d.mSize + 0.5f;
    const float glowFactor = glowPattern(localU, localV) * d.mGlowWeight *
            smoothstepf(0.f, 0.06f, dist) + 1.f + d.mGlowBias;

    // keep the premultiplied invariant (glow can push rgb past alpha)
    const float r = cr * glowFactor + noise;
    const float g = cg * glowFactor + noise;
    const float b = cb * glowFactor + noise;
    rOut = std::min(std::max(r, 0.f), ca);
    gOut = std::min(std::max(g, 0.f), ca);
    bOut = std::min(std::max(b, 0.f), ca);
    aOut = ca;
}

} // namespace

LiquidGlassEffect::LiquidGlassEffect() :
    RasterEffect("liquid-glass",
                 AppSupport::getRasterEffectHardwareSupport("LiquidGlass",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::LIQUID_GLASS)
{
    mCenterX = enve::make_shared<QrealAnimator>(0.5, 0, 1, 0.01,
                                                QObject::tr("center x"));
    ca_addChild(mCenterX);
    // panel-facing y is 0 = top (friction y grows downward)
    mCenterY = enve::make_shared<QrealAnimator>(0.5, 0, 1, 0.01,
                                                QObject::tr("center y"));
    ca_addChild(mCenterY);
    mSize = enve::make_shared<QrealAnimator>(0.15, 0.05, 0.5, 0.01,
                                             QObject::tr("size"));
    ca_addChild(mSize);
    mShapeN = enve::make_shared<QrealAnimator>(3, 2, 8, 0.1,
                                              QObject::tr("shape exponent"));
    ca_addChild(mShapeN);
    mRefraction = enve::make_shared<QrealAnimator>(3, 0, 6, 0.05,
                                                   QObject::tr("refraction"));
    ca_addChild(mRefraction);
    mNoise = enve::make_shared<QrealAnimator>(0.06, 0, 0.2, 0.005,
                                              QObject::tr("noise"));
    ca_addChild(mNoise);
    mGlowWeight = enve::make_shared<QrealAnimator>(0.35, 0, 1, 0.01,
                                                   QObject::tr("glow weight"));
    ca_addChild(mGlowWeight);
    mGlowBias = enve::make_shared<QrealAnimator>(0, -1, 1, 0.01,
                                                 QObject::tr("glow bias"));
    ca_addChild(mGlowBias);
}

// Inherits RasterEffectCaller (not OpenGLRasterEffectCaller) because the
// surface size is only known once the render tools exist, and the base
// class seals processGpu. The GPU flow replicates
// OpenGLRasterEffectCaller::processGpu with the size captured before
// the uniform dispatch.
class LiquidGlassEffectCaller : public RasterEffectCaller {
public:
    LiquidGlassEffectCaller(const HardwareSupport hwSupport,
                            const LiquidGlassEffectData& data) :
        RasterEffectCaller(hwSupport),
        mData(data) {}

    void processGpu(QGL33 * const gl, GpuRenderTools& renderTools) {
        renderTools.switchToOpenGL(gl);

        if (!sInitialized) {
            iniProgram(gl);
            sInitialized = true;
        }

        renderTools.requestTargetFbo().bind(gl);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        gl->glUseProgram(sProgramId);

        // capture the real surface size for uTexSize (BoxRenderData::
        // fGlobalRect is still 0x0 when getEffectCaller runs)
        const auto& srcTex = renderTools.getSrcTexture();
        mRTexW = float(srcTex.fWidth);
        mRTexH = float(srcTex.fHeight);

        setVars(gl);

        gl->glActiveTexture(GL_TEXTURE0);
        renderTools.getSrcTexture().bind(gl);

        gl->glBindVertexArray(renderTools.getSquareVAO());
        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        renderTools.swapTextures();
    }

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
private:
    void iniProgram(QGL33 * const gl) {
        try {
            gIniProgram(gl, sProgramId, GL_TEXTURED_VERT,
                        QStringLiteral(":/shaders/liquidglasseffect.frag"));
        } catch(...) {
            RuntimeThrow("Could not initialize a program for "
                         "'liquidglasseffect.frag'");
        }

        gl->glUseProgram(sProgramId);

        const auto texLocation = gl->glGetUniformLocation(sProgramId, "tex");
        gl->glUniform1i(texLocation, 0);

        iniVars(gl);
    }

    void iniVars(QGL33 * const gl) const {
        sCenterU = gl->glGetUniformLocation(sProgramId, "uCenter");
        sTexSizeU = gl->glGetUniformLocation(sProgramId, "uTexSize");
        sSizeU = gl->glGetUniformLocation(sProgramId, "uSize");
        sShapeNU = gl->glGetUniformLocation(sProgramId, "uShapeN");
        sRefractionU = gl->glGetUniformLocation(sProgramId, "uRefraction");
        sNoiseU = gl->glGetUniformLocation(sProgramId, "uNoise");
        sGlowWeightU = gl->glGetUniformLocation(sProgramId, "uGlowWeight");
        sGlowBiasU = gl->glGetUniformLocation(sProgramId, "uGlowBias");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sCenterU, mData.mCenterX, mData.mCenterY);
        gl->glUniform2f(sTexSizeU, mRTexW, mRTexH);
        gl->glUniform1f(sSizeU, mData.mSize);
        gl->glUniform1f(sShapeNU, mData.mShapeN);
        gl->glUniform1f(sRefractionU, mData.mRefraction);
        gl->glUniform1f(sNoiseU, mData.mNoise);
        gl->glUniform1f(sGlowWeightU, mData.mGlowWeight);
        gl->glUniform1f(sGlowBiasU, mData.mGlowBias);
        // throttled diagnostic for the runtime debug log
        static int sVarLog = 0;
        if (sVarLog++ < 8) {
            qWarning() << "[LG] gpu setVars prog=" << sProgramId
                       << "locs center/texsize/size/shape/refr/noise/gw/gb ="
                       << sCenterU << sTexSizeU << sSizeU << sShapeNU
                       << sRefractionU << sNoiseU << sGlowWeightU << sGlowBiasU
                       << "vals c=" << mData.mCenterX << mData.mCenterY
                       << "tex=" << mRTexW << mRTexH
                       << "size=" << mData.mSize << "n=" << mData.mShapeN
                       << "refr=" << mData.mRefraction;
        }
    }

    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sCenterU;
    static GLint sTexSizeU;
    static GLint sSizeU;
    static GLint sShapeNU;
    static GLint sRefractionU;
    static GLint sNoiseU;
    static GLint sGlowWeightU;
    static GLint sGlowBiasU;

    const LiquidGlassEffectData mData;
    mutable float mRTexW = 1.f;
    mutable float mRTexH = 1.f;
};

bool LiquidGlassEffectCaller::sInitialized = false;
GLuint LiquidGlassEffectCaller::sProgramId = 0;

GLint LiquidGlassEffectCaller::sCenterU = -1;
GLint LiquidGlassEffectCaller::sTexSizeU = -1;
GLint LiquidGlassEffectCaller::sSizeU = -1;
GLint LiquidGlassEffectCaller::sShapeNU = -1;
GLint LiquidGlassEffectCaller::sRefractionU = -1;
GLint LiquidGlassEffectCaller::sNoiseU = -1;
GLint LiquidGlassEffectCaller::sGlowWeightU = -1;
GLint LiquidGlassEffectCaller::sGlowBiasU = -1;

stdsptr<RasterEffectCaller> LiquidGlassEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)

    LiquidGlassEffectData effData;
    // NOTE: data->fGlobalRect is still 0x0 here (updateGlobalRect runs
    // later, in BoxRenderData::process); the surface size is captured
    // from the render tools in processGpu instead
    effData.mCenterX = mCenterX->getEffectiveValue(relFrame);
    // panel 0 = top -> GL uv y up
    effData.mCenterY = 1. - mCenterY->getEffectiveValue(relFrame);
    effData.mSize = mSize->getEffectiveValue(relFrame);
    effData.mShapeN = mShapeN->getEffectiveValue(relFrame);
    effData.mRefraction = mRefraction->getEffectiveValue(relFrame);
    effData.mNoise = mNoise->getEffectiveValue(relFrame);
    effData.mGlowWeight = mGlowWeight->getEffectiveValue(relFrame);
    effData.mGlowBias = mGlowBias->getEffectiveValue(relFrame);

    // throttled diagnostic for the runtime debug log
    static int sCallerLog = 0;
    if (sCallerLog++ < 5) {
        qWarning() << "[LG] getEffectCaller center=" << effData.mCenterX
                   << effData.mCenterY << "size=" << effData.mSize
                   << "refr=" << effData.mRefraction;
    }

    return enve::make_shared<LiquidGlassEffectCaller>(
                instanceHwSupport(), effData);
}

void LiquidGlassEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    // throttled diagnostic for the runtime debug log
    static int sCpuLog = 0;
    if (sCpuLog++ < 5) {
        qWarning() << "[LG] cpu path" << imgWidth << "x" << imgHeight
                   << "tile" << data.fTexTile.left() << data.fTexTile.top()
                   << data.fTexTile.right() << data.fTexTile.bottom();
    }

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right(), imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom(), imgHeight - 1);

    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        for (int xi = xMin; xi <= xMax; xi++) {
            const float r = *src++ / 255.f;
            const float g = *src++ / 255.f;
            const float b = *src++ / 255.f;
            const float a = *src++ / 255.f;

            // texCoord convention: y up, bitmap row 0 is the top
            const float u = (xi + 0.5f) / float(imgWidth);
            const float v = 1.f - (yi + 0.5f) / float(imgHeight);
            // gl_FragCoord origin is the bottom-left
            const float noiseX = float(xi) * 1e-3f;
            const float noiseY = float(imgHeight - yi) * 1e-3f;

            float rOut, gOut, bOut, aOut;
            processPixel(mData, srcBtmp, imgWidth, imgHeight,
                         u, v, noiseX, noiseY,
                         r, g, b, a, rOut, gOut, bOut, aOut);

            *dst++ = static_cast<uchar>(rOut * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(gOut * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(bOut * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(aOut * 255.f + 0.5f);
        }
    }
}
