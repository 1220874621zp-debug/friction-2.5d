/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "blackwhiteflasheffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"

BlackWhiteFlashEffect::BlackWhiteFlashEffect() :
    RasterEffect("blackWhiteFlash",
                 AppSupport::getRasterEffectHardwareSupport("BlackWhiteFlash",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::BLACK_WHITE_FLASH)
{
    mThreshold = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "threshold");
    ca_addChild(mThreshold);

    mContrast = enve::make_shared<QrealAnimator>(2.0, 0.1, 10.0, 0.1, "contrast");
    ca_addChild(mContrast);

    mLightIntensity = enve::make_shared<QrealAnimator>(80.0, 0.0, 200.0, 1.0, "intensity");
    ca_addChild(mLightIntensity);

    mLightLength = enve::make_shared<QrealAnimator>(25.0, 0.0, 100.0, 1.0, "distance");
    ca_addChild(mLightLength);

    mEdgeIntensity = enve::make_shared<QrealAnimator>(1.5, 0.0, 5.0, 0.1, "edge");
    ca_addChild(mEdgeIntensity);

    mInvert = enve::make_shared<QrealAnimator>(0.0, 0.0, 1.0, 1.0, "invert");
    ca_addChild(mInvert);

    mFlashColor = enve::make_shared<ColorAnimator>("color");
    mFlashColor->setColor(QColor(255, 255, 255, 255));
    ca_addChild(mFlashColor);

    mBgColor = enve::make_shared<ColorAnimator>("bgcolor");
    mBgColor->setColor(QColor(0, 0, 0, 255));
    ca_addChild(mBgColor);
}

class BlackWhiteFlashEffectCaller : public OpenGLRasterEffectCaller {
public:
    BlackWhiteFlashEffectCaller(const HardwareSupport hwSupport,
                                qreal threshold,
                                qreal contrast,
                                qreal lightIntensity,
                                qreal lightLength,
                                qreal edgeIntensity,
                                qreal invert,
                                const QColor& flashColor,
                                const QColor& bgColor) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/blackwhiteflasheffect.frag",
                                 hwSupport),
        mThreshold(threshold),
        mContrast(contrast),
        mLightIntensity(lightIntensity),
        mLightLength(lightLength),
        mEdgeIntensity(edgeIntensity),
        mInvert(invert),
        mFlashColor(flashColor),
        mBgColor(bgColor) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sThresholdU = gl->glGetUniformLocation(sProgramId, "threshold");
        sContrastU = gl->glGetUniformLocation(sProgramId, "contrast");
        sLightIntensityU = gl->glGetUniformLocation(sProgramId, "lightIntensity");
        sLightLengthU = gl->glGetUniformLocation(sProgramId, "lightLength");
        sEdgeIntensityU = gl->glGetUniformLocation(sProgramId, "edgeIntensity");
        sInvertU = gl->glGetUniformLocation(sProgramId, "invert");
        sFlashColorU = gl->glGetUniformLocation(sProgramId, "flashColor");
        sBgColorU = gl->glGetUniformLocation(sProgramId, "bgColor");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sThresholdU, toSkScalar(mThreshold));
        gl->glUniform1f(sContrastU, toSkScalar(mContrast));
        gl->glUniform1f(sLightIntensityU, toSkScalar(mLightIntensity));
        gl->glUniform1f(sLightLengthU, toSkScalar(mLightLength));
        gl->glUniform1f(sEdgeIntensityU, toSkScalar(mEdgeIntensity));
        gl->glUniform1f(sInvertU, toSkScalar(mInvert));
        gl->glUniform4f(sFlashColorU, mFlashColor.redF(), mFlashColor.greenF(), mFlashColor.blueF(), mFlashColor.alphaF());
        gl->glUniform4f(sBgColorU, mBgColor.redF(), mBgColor.greenF(), mBgColor.blueF(), mBgColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sThresholdU;
    static GLint sContrastU;
    static GLint sLightIntensityU;
    static GLint sLightLengthU;
    static GLint sEdgeIntensityU;
    static GLint sInvertU;
    static GLint sFlashColorU;
    static GLint sBgColorU;

    const qreal mThreshold;
    const qreal mContrast;
    const qreal mLightIntensity;
    const qreal mLightLength;
    const qreal mEdgeIntensity;
    const qreal mInvert;
    const QColor mFlashColor;
    const QColor mBgColor;
};

bool BlackWhiteFlashEffectCaller::sInitialized = false;
GLuint BlackWhiteFlashEffectCaller::sProgramId = 0;

GLint BlackWhiteFlashEffectCaller::sThresholdU = -1;
GLint BlackWhiteFlashEffectCaller::sContrastU = -1;
GLint BlackWhiteFlashEffectCaller::sLightIntensityU = -1;
GLint BlackWhiteFlashEffectCaller::sLightLengthU = -1;
GLint BlackWhiteFlashEffectCaller::sEdgeIntensityU = -1;
GLint BlackWhiteFlashEffectCaller::sInvertU = -1;
GLint BlackWhiteFlashEffectCaller::sFlashColorU = -1;
GLint BlackWhiteFlashEffectCaller::sBgColorU = -1;

stdsptr<RasterEffectCaller> BlackWhiteFlashEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal threshold = mThreshold->getEffectiveValue(relFrame);
    const qreal contrast = mContrast->getEffectiveValue(relFrame);
    const qreal lightIntensity = mLightIntensity->getEffectiveValue(relFrame) * influence;
    const qreal lightLength = mLightLength->getEffectiveValue(relFrame);
    const qreal edgeIntensity = mEdgeIntensity->getEffectiveValue(relFrame);
    const qreal invert = mInvert->getEffectiveValue(relFrame);
    const QColor flashColor = mFlashColor->getColor(relFrame);
    const QColor bgColor = mBgColor->getColor(relFrame);

    return enve::make_shared<BlackWhiteFlashEffectCaller>(
                instanceHwSupport(), threshold, contrast, lightIntensity,
                lightLength, edgeIntensity, invert, flashColor, bgColor);
}

void BlackWhiteFlashEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            if (a < 2) {
                *dst++ = 0; *dst++ = 0; *dst++ = 0; *dst++ = 0;
                continue;
            }

            const qreal lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
            qreal cl = (lum - 0.5) * mContrast + 0.5;
            qreal val = (cl > mThreshold * 0.01) ? 1.0 : 0.0;
            if (mInvert > 0.5) val = 1.0 - val;

            const qreal outR = mBgColor.redF() * (1.0 - val) + mFlashColor.redF() * val;
            const qreal outG = mBgColor.greenF() * (1.0 - val) + mFlashColor.greenF() * val;
            const qreal outB = mBgColor.blueF() * (1.0 - val) + mFlashColor.blueF() * val;

            *dst++ = static_cast<uchar>(outR * 255.0);
            *dst++ = static_cast<uchar>(outG * 255.0);
            *dst++ = static_cast<uchar>(outB * 255.0);
            *dst++ = a;
        }
    }
}
