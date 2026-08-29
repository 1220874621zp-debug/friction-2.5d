/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "liquidglasseffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"
#include <cmath>

LiquidGlassEffect::LiquidGlassEffect() :
    RasterEffect("liquidGlass",
                 AppSupport::getRasterEffectHardwareSupport("LiquidGlass",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::LIQUID_GLASS)
{
    mBlurRadius = enve::make_shared<QrealAnimator>(15.0, 0.0, 100.0, 1.0, "blur radius");
    ca_addChild(mBlurRadius);

    mRefraction = enve::make_shared<QrealAnimator>(20.0, 0.0, 100.0, 1.0, "displacement");
    ca_addChild(mRefraction);

    mSurfaceNoise = enve::make_shared<QrealAnimator>(20.0, 0.0, 100.0, 1.0, "roughness");
    ca_addChild(mSurfaceNoise);

    mThickness = enve::make_shared<QrealAnimator>(10.0, 0.0, 50.0, 1.0, "thickness");
    ca_addChild(mThickness);

    mHighlightIntensity = enve::make_shared<QrealAnimator>(60.0, 0.0, 200.0, 1.0, "intensity");
    ca_addChild(mHighlightIntensity);

    mLightAngle = enve::make_shared<QrealAnimator>(45.0, -180.0, 180.0, 1.0, "angle");
    ca_addChild(mLightAngle);

    mHighlightSize = enve::make_shared<QrealAnimator>(25.0, 1.0, 100.0, 1.0, "size");
    ca_addChild(mHighlightSize);

    mEdgeSoftness = enve::make_shared<QrealAnimator>(10.0, 0.0, 100.0, 1.0, "feather");
    ca_addChild(mEdgeSoftness);

    mMagnification = enve::make_shared<QrealAnimator>(1.0, 0.2, 3.0, 0.05, "scale");
    ca_addChild(mMagnification);

    mGlassTint = enve::make_shared<ColorAnimator>("color");
    mGlassTint->setColor(QColor(230, 245, 255, 255));
    ca_addChild(mGlassTint);

    mTintOpacity = enve::make_shared<QrealAnimator>(10.0, 0.0, 100.0, 1.0, "opacity");
    ca_addChild(mTintOpacity);
}

class LiquidGlassEffectCaller : public OpenGLRasterEffectCaller {
public:
    LiquidGlassEffectCaller(const HardwareSupport hwSupport,
                            qreal blurRadius,
                            qreal refraction,
                            qreal surfaceNoise,
                            qreal thickness,
                            qreal highlightIntensity,
                            qreal lightAngle,
                            qreal highlightSize,
                            qreal edgeSoftness,
                            qreal magnification,
                            const QColor& glassTint,
                            qreal tintOpacity) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/liquidglasseffect.frag",
                                 hwSupport),
        mBlurRadius(blurRadius),
        mRefraction(refraction),
        mSurfaceNoise(surfaceNoise),
        mThickness(thickness),
        mHighlightIntensity(highlightIntensity),
        mLightAngle(lightAngle),
        mHighlightSize(highlightSize),
        mEdgeSoftness(edgeSoftness),
        mMagnification(magnification),
        mGlassTint(glassTint),
        mTintOpacity(tintOpacity) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sBlurRadiusU = gl->glGetUniformLocation(sProgramId, "blurRadius");
        sRefractionU = gl->glGetUniformLocation(sProgramId, "refraction");
        sSurfaceNoiseU = gl->glGetUniformLocation(sProgramId, "surfaceNoise");
        sThicknessU = gl->glGetUniformLocation(sProgramId, "thickness");
        sHighlightIntensityU = gl->glGetUniformLocation(sProgramId, "highlightIntensity");
        sLightAngleU = gl->glGetUniformLocation(sProgramId, "lightAngle");
        sHighlightSizeU = gl->glGetUniformLocation(sProgramId, "highlightSize");
        sEdgeSoftnessU = gl->glGetUniformLocation(sProgramId, "edgeSoftness");
        sMagnificationU = gl->glGetUniformLocation(sProgramId, "magnification");
        sGlassTintU = gl->glGetUniformLocation(sProgramId, "glassTint");
        sTintOpacityU = gl->glGetUniformLocation(sProgramId, "tintOpacity");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sBlurRadiusU, toSkScalar(mBlurRadius));
        gl->glUniform1f(sRefractionU, toSkScalar(mRefraction));
        gl->glUniform1f(sSurfaceNoiseU, toSkScalar(mSurfaceNoise));
        gl->glUniform1f(sThicknessU, toSkScalar(mThickness));
        gl->glUniform1f(sHighlightIntensityU, toSkScalar(mHighlightIntensity));
        gl->glUniform1f(sLightAngleU, toSkScalar(mLightAngle));
        gl->glUniform1f(sHighlightSizeU, toSkScalar(mHighlightSize));
        gl->glUniform1f(sEdgeSoftnessU, toSkScalar(mEdgeSoftness));
        gl->glUniform1f(sMagnificationU, toSkScalar(mMagnification));
        gl->glUniform4f(sGlassTintU, mGlassTint.redF(), mGlassTint.greenF(), mGlassTint.blueF(), mGlassTint.alphaF());
        gl->glUniform1f(sTintOpacityU, toSkScalar(mTintOpacity));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sBlurRadiusU;
    static GLint sRefractionU;
    static GLint sSurfaceNoiseU;
    static GLint sThicknessU;
    static GLint sHighlightIntensityU;
    static GLint sLightAngleU;
    static GLint sHighlightSizeU;
    static GLint sEdgeSoftnessU;
    static GLint sMagnificationU;
    static GLint sGlassTintU;
    static GLint sTintOpacityU;

    const qreal mBlurRadius;
    const qreal mRefraction;
    const qreal mSurfaceNoise;
    const qreal mThickness;
    const qreal mHighlightIntensity;
    const qreal mLightAngle;
    const qreal mHighlightSize;
    const qreal mEdgeSoftness;
    const qreal mMagnification;
    const QColor mGlassTint;
    const qreal mTintOpacity;
};

bool LiquidGlassEffectCaller::sInitialized = false;
GLuint LiquidGlassEffectCaller::sProgramId = 0;

GLint LiquidGlassEffectCaller::sBlurRadiusU = -1;
GLint LiquidGlassEffectCaller::sRefractionU = -1;
GLint LiquidGlassEffectCaller::sSurfaceNoiseU = -1;
GLint LiquidGlassEffectCaller::sThicknessU = -1;
GLint LiquidGlassEffectCaller::sHighlightIntensityU = -1;
GLint LiquidGlassEffectCaller::sLightAngleU = -1;
GLint LiquidGlassEffectCaller::sHighlightSizeU = -1;
GLint LiquidGlassEffectCaller::sEdgeSoftnessU = -1;
GLint LiquidGlassEffectCaller::sMagnificationU = -1;
GLint LiquidGlassEffectCaller::sGlassTintU = -1;
GLint LiquidGlassEffectCaller::sTintOpacityU = -1;

stdsptr<RasterEffectCaller> LiquidGlassEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal blurRadius = mBlurRadius->getEffectiveValue(relFrame);
    const qreal refraction = mRefraction->getEffectiveValue(relFrame) * influence;
    const qreal surfaceNoise = mSurfaceNoise->getEffectiveValue(relFrame);
    const qreal thickness = mThickness->getEffectiveValue(relFrame);
    const qreal highlightIntensity = mHighlightIntensity->getEffectiveValue(relFrame);
    const qreal lightAngle = mLightAngle->getEffectiveValue(relFrame);
    const qreal highlightSize = mHighlightSize->getEffectiveValue(relFrame);
    const qreal edgeSoftness = mEdgeSoftness->getEffectiveValue(relFrame);
    const qreal magnification = mMagnification->getEffectiveValue(relFrame);
    const QColor glassTint = mGlassTint->getColor(relFrame);
    const qreal tintOpacity = mTintOpacity->getEffectiveValue(relFrame);

    return enve::make_shared<LiquidGlassEffectCaller>(
                instanceHwSupport(), blurRadius, refraction, surfaceNoise,
                thickness, highlightIntensity, lightAngle, highlightSize,
                edgeSoftness, magnification, glassTint, tintOpacity);
}

void LiquidGlassEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;
    if (srcBtmp.empty() || dstBtmp.empty()) return;

    const int imgWidth = srcBtmp.width();
    const int imgHeight = srcBtmp.height();
    if (imgWidth <= 0 || imgHeight <= 0) return;

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right(), imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom(), imgHeight - 1);

    const auto sampleSrc = [&srcBtmp, imgWidth, imgHeight](int x, int y, int ch) -> uchar {
        x = std::max(0, std::min(imgWidth - 1, x));
        y = std::max(0, std::min(imgHeight - 1, y));
        const auto p = static_cast<const uchar*>(srcBtmp.getAddr(x, y));
        return p[ch];
    };

    const qreal tr = mGlassTint.redF() * 255.0;
    const qreal tg = mGlassTint.greenF() * 255.0;
    const qreal tb = mGlassTint.blueF() * 255.0;
    const qreal topac = (mTintOpacity * 0.01) * mGlassTint.alphaF();

    const qreal bRad = std::max(0.0, mBlurRadius) * 0.15;
    const int bRadius = qRound(bRad);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const qreal uvy = (qreal(yi) / imgHeight) - 0.5;

        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal uvx = (qreal(xi) / imgWidth) - 0.5;

            const qreal freq = 8.0 + mSurfaceNoise * 0.15;
            const qreal waveX = std::sin(yi * 0.03 * freq + xi * 0.01) * (mSurfaceNoise * 0.1);
            const qreal waveY = std::cos(xi * 0.03 * freq - yi * 0.01) * (mSurfaceNoise * 0.1);

            const int dispX = qRound((uvx * (mThickness * 0.2) + waveX) * (mRefraction * 0.2));
            const int dispY = qRound((uvy * (mThickness * 0.2) + waveY) * (mRefraction * 0.2));

            const int sampleX = xi + dispX;
            const int sampleY = yi + dispY;

            qreal sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            if (bRadius > 0) {
                for(int dy = -bRadius; dy <= bRadius; dy += std::max(1, bRadius / 2)) {
                    for(int dx = -bRadius; dx <= bRadius; dx += std::max(1, bRadius / 2)) {
                        sumR += sampleSrc(sampleX + dx, sampleY + dy, 0);
                        sumG += sampleSrc(sampleX + dx, sampleY + dy, 1);
                        sumB += sampleSrc(sampleX + dx, sampleY + dy, 2);
                        sumA += sampleSrc(sampleX + dx, sampleY + dy, 3);
                        count++;
                    }
                }
            } else {
                sumR = sampleSrc(sampleX, sampleY, 0);
                sumG = sampleSrc(sampleX, sampleY, 1);
                sumB = sampleSrc(sampleX, sampleY, 2);
                sumA = sampleSrc(sampleX, sampleY, 3);
                count = 1;
            }

            const qreal r = sumR / count;
            const qreal g = sumG / count;
            const qreal b = sumB / count;
            const qreal a = sumA / count;

            if (a < 1.0) {
                *dst++ = 0; *dst++ = 0; *dst++ = 0; *dst++ = 0;
                continue;
            }

            const qreal outR = r * (1.0 - topac) + tr * topac;
            const qreal outG = g * (1.0 - topac) + tg * topac;
            const qreal outB = b * (1.0 - topac) + tb * topac;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outR)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outG)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outB)));
            *dst++ = static_cast<uchar>(a);
        }
    }
}
