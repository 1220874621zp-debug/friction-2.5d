/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "displacementwarpeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

DisplacementWarpEffect::DisplacementWarpEffect() :
    RasterEffect(QObject::tr("Displacement Warp"),
                 AppSupport::getRasterEffectHardwareSupport("DisplacementWarp",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::DISPLACEMENT_WARP)
{
    mAmount = enve::make_shared<QrealAnimator>(20.0, 0.0, 200.0, 1.0, "amount");
    ca_addChild(mAmount);

    mFrequency = enve::make_shared<QrealAnimator>(10.0, 0.1, 100.0, 0.5, "frequency");
    ca_addChild(mFrequency);

    mSpeed = enve::make_shared<QrealAnimator>(15.0, 0.0, 100.0, 0.5, "speed");
    ca_addChild(mSpeed);

    mDispType = enve::make_shared<QrealAnimator>(0.0, 0.0, 2.0, 1.0, "type");
    ca_addChild(mDispType);

    mChromatic = enve::make_shared<QrealAnimator>(5.0, 0.0, 50.0, 0.5, "chromatic");
    ca_addChild(mChromatic);
}



class DisplacementWarpEffectCaller : public OpenGLRasterEffectCaller {
public:
    DisplacementWarpEffectCaller(const HardwareSupport hwSupport,
                                 qreal amount,
                                 qreal frequency,
                                 qreal timeOffset,
                                 int dispType,
                                 qreal chromatic) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/displacementwarpeffect.frag",
                                 hwSupport),
        mAmount(amount),
        mFrequency(frequency),
        mTimeOffset(timeOffset),
        mDispType(dispType),
        mChromatic(chromatic) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
        sFrequencyU = gl->glGetUniformLocation(sProgramId, "frequency");
        sTimeOffsetU = gl->glGetUniformLocation(sProgramId, "timeOffset");
        sDispTypeU = gl->glGetUniformLocation(sProgramId, "dispType");
        sChromaticU = gl->glGetUniformLocation(sProgramId, "chromatic");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sAmountU, toSkScalar(mAmount));
        gl->glUniform1f(sFrequencyU, toSkScalar(mFrequency));
        gl->glUniform1f(sTimeOffsetU, toSkScalar(mTimeOffset));
        gl->glUniform1i(sDispTypeU, mDispType);
        gl->glUniform1f(sChromaticU, toSkScalar(mChromatic));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;
    static GLint sFrequencyU;
    static GLint sTimeOffsetU;
    static GLint sDispTypeU;
    static GLint sChromaticU;

    const qreal mAmount;
    const qreal mFrequency;
    const qreal mTimeOffset;
    const int mDispType;
    const qreal mChromatic;
};

bool DisplacementWarpEffectCaller::sInitialized = false;
GLuint DisplacementWarpEffectCaller::sProgramId = 0;

GLint DisplacementWarpEffectCaller::sAmountU = -1;
GLint DisplacementWarpEffectCaller::sFrequencyU = -1;
GLint DisplacementWarpEffectCaller::sTimeOffsetU = -1;
GLint DisplacementWarpEffectCaller::sDispTypeU = -1;
GLint DisplacementWarpEffectCaller::sChromaticU = -1;

stdsptr<RasterEffectCaller> DisplacementWarpEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amount = mAmount->getEffectiveValue(relFrame) * influence;
    const qreal frequency = mFrequency->getEffectiveValue(relFrame);
    const qreal speed = mSpeed->getEffectiveValue(relFrame);
    const qreal timeOffset = relFrame * (speed * 0.01);
    const int dType = qRound(mDispType->getEffectiveValue(relFrame));
    const qreal chromatic = mChromatic->getEffectiveValue(relFrame);

    return enve::make_shared<DisplacementWarpEffectCaller>(
                instanceHwSupport(), amount, frequency, timeOffset, dType, chromatic);
}

void DisplacementWarpEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const qreal ny = qreal(yi) / imgHeight;

        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal nx = qreal(xi) / imgWidth;
            qreal dx = std::sin(ny * mFrequency * 20.0 + mTimeOffset * 5.0) * (mAmount * 0.2);
            qreal dy = std::cos(nx * mFrequency * 20.0 + mTimeOffset * 5.0) * (mAmount * 0.2);

            const int sx = qRound(xi + dx);
            const int sy = qRound(yi + dy);
            const int chrOffset = qRound(mChromatic * 0.5);

            *dst++ = sampleSrc(sx + chrOffset, sy, 0);
            *dst++ = sampleSrc(sx, sy, 1);
            *dst++ = sampleSrc(sx - chrOffset, sy, 2);
            *dst++ = sampleSrc(sx, sy, 3);
        }
    }
}
