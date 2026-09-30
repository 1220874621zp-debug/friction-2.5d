/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "fractalnoiseeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"

FractalNoiseEffect::FractalNoiseEffect() :
    RasterEffect(QObject::tr("Fractal Noise"),
                 AppSupport::getRasterEffectHardwareSupport("FractalNoise",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::FRACTAL_NOISE)
{
    mNoiseType = enve::make_shared<QrealAnimator>(0.0, 0.0, 3.0, 1.0, "type");
    ca_addChild(mNoiseType);

    mScale = enve::make_shared<QrealAnimator>(50.0, 1.0, 500.0, 1.0, "scale");
    ca_addChild(mScale);

    mComplexity = enve::make_shared<QrealAnimator>(5.0, 1.0, 8.0, 1.0, "complexity");
    ca_addChild(mComplexity);

    mEvolution = enve::make_shared<QrealAnimator>(0.0, 0.0, 3600.0, 1.0, "evolution");
    ca_addChild(mEvolution);

    mBrightness = enve::make_shared<QrealAnimator>(0.0, -100.0, 100.0, 1.0, "brightness");
    ca_addChild(mBrightness);

    mContrast = enve::make_shared<QrealAnimator>(100.0, 0.0, 500.0, 1.0, "contrast");
    ca_addChild(mContrast);

    mOpacity = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0, "opacity");
    ca_addChild(mOpacity);

    mInvert = enve::make_shared<QrealAnimator>(0.0, 0.0, 1.0, 1.0, "invert");
    ca_addChild(mInvert);

    mColor1 = enve::make_shared<ColorAnimator>("color1");
    mColor1->setColor(QColor(0, 0, 0, 255));
    ca_addChild(mColor1);

    mColor2 = enve::make_shared<ColorAnimator>("color2");
    mColor2->setColor(QColor(255, 255, 255, 255));
    ca_addChild(mColor2);
}



class FractalNoiseEffectCaller : public OpenGLRasterEffectCaller {
public:
    FractalNoiseEffectCaller(const HardwareSupport hwSupport,
                             int noiseType,
                             qreal scale,
                             qreal complexity,
                             qreal evolution,
                             qreal brightness,
                             qreal contrast,
                             qreal opacity,
                             qreal invert,
                             const QColor& c1,
                             const QColor& c2) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/fractalnoiseeffect.frag",
                                 hwSupport),
        mNoiseType(noiseType),
        mScale(scale),
        mComplexity(complexity),
        mEvolution(evolution),
        mBrightness(brightness),
        mContrast(contrast),
        mOpacity(opacity),
        mInvert(invert),
        mColor1(c1),
        mColor2(c2) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sNoiseTypeU = gl->glGetUniformLocation(sProgramId, "noiseType");
        sScaleU = gl->glGetUniformLocation(sProgramId, "scale");
        sComplexityU = gl->glGetUniformLocation(sProgramId, "complexity");
        sEvolutionU = gl->glGetUniformLocation(sProgramId, "evolution");
        sBrightnessU = gl->glGetUniformLocation(sProgramId, "brightness");
        sContrastU = gl->glGetUniformLocation(sProgramId, "contrast");
        sOpacityU = gl->glGetUniformLocation(sProgramId, "opacity");
        sInvertU = gl->glGetUniformLocation(sProgramId, "invert");
        sColor1U = gl->glGetUniformLocation(sProgramId, "color1");
        sColor2U = gl->glGetUniformLocation(sProgramId, "color2");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1i(sNoiseTypeU, mNoiseType);
        gl->glUniform1f(sScaleU, toSkScalar(mScale));
        gl->glUniform1f(sComplexityU, toSkScalar(mComplexity));
        gl->glUniform1f(sEvolutionU, toSkScalar(mEvolution));
        gl->glUniform1f(sBrightnessU, toSkScalar(mBrightness));
        gl->glUniform1f(sContrastU, toSkScalar(mContrast));
        gl->glUniform1f(sOpacityU, toSkScalar(mOpacity));
        gl->glUniform1f(sInvertU, toSkScalar(mInvert));
        gl->glUniform4f(sColor1U, mColor1.redF(), mColor1.greenF(), mColor1.blueF(), mColor1.alphaF());
        gl->glUniform4f(sColor2U, mColor2.redF(), mColor2.greenF(), mColor2.blueF(), mColor2.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sNoiseTypeU;
    static GLint sScaleU;
    static GLint sComplexityU;
    static GLint sEvolutionU;
    static GLint sBrightnessU;
    static GLint sContrastU;
    static GLint sOpacityU;
    static GLint sInvertU;
    static GLint sColor1U;
    static GLint sColor2U;

    const int mNoiseType;
    const qreal mScale;
    const qreal mComplexity;
    const qreal mEvolution;
    const qreal mBrightness;
    const qreal mContrast;
    const qreal mOpacity;
    const qreal mInvert;
    const QColor mColor1;
    const QColor mColor2;
};

bool FractalNoiseEffectCaller::sInitialized = false;
GLuint FractalNoiseEffectCaller::sProgramId = 0;

GLint FractalNoiseEffectCaller::sNoiseTypeU = -1;
GLint FractalNoiseEffectCaller::sScaleU = -1;
GLint FractalNoiseEffectCaller::sComplexityU = -1;
GLint FractalNoiseEffectCaller::sEvolutionU = -1;
GLint FractalNoiseEffectCaller::sBrightnessU = -1;
GLint FractalNoiseEffectCaller::sContrastU = -1;
GLint FractalNoiseEffectCaller::sOpacityU = -1;
GLint FractalNoiseEffectCaller::sInvertU = -1;
GLint FractalNoiseEffectCaller::sColor1U = -1;
GLint FractalNoiseEffectCaller::sColor2U = -1;

stdsptr<RasterEffectCaller> FractalNoiseEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const int nType = qRound(mNoiseType->getEffectiveValue(relFrame));
    const qreal scale = mScale->getEffectiveValue(relFrame);
    const qreal complexity = mComplexity->getEffectiveValue(relFrame);
    const qreal evolution = mEvolution->getEffectiveValue(relFrame);
    const qreal brightness = mBrightness->getEffectiveValue(relFrame);
    const qreal contrast = mContrast->getEffectiveValue(relFrame);
    const qreal opacity = (mOpacity->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal invert = mInvert->getEffectiveValue(relFrame);
    const QColor c1 = mColor1->getColor(relFrame);
    const QColor c2 = mColor2->getColor(relFrame);

    return enve::make_shared<FractalNoiseEffectCaller>(
                instanceHwSupport(), nType, scale, complexity, evolution,
                brightness, contrast, opacity, invert, c1, c2);
}

void FractalNoiseEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

    const qreal sc = std::max(mScale, 1.0) * 0.05;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal ny = (qreal(yi) / imgHeight) * sc;

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            if (a < 2) {
                *dst++ = 0; *dst++ = 0; *dst++ = 0; *dst++ = 0;
                continue;
            }

            const qreal nx = (qreal(xi) / imgWidth) * sc;
            qreal val = std::sin(nx * 10.0 + mEvolution * 0.02) * std::cos(ny * 10.0 + mEvolution * 0.02) * 0.5 + 0.5;
            if (mInvert > 0.5) val = 1.0 - val;
            val = std::max(0.0, std::min(1.0, (val - 0.5) * (mContrast * 0.01) + 0.5 + mBrightness * 0.01));

            const qreal nr = mColor1.redF() * (1.0 - val) + mColor2.redF() * val;
            const qreal ng = mColor1.greenF() * (1.0 - val) + mColor2.greenF() * val;
            const qreal nb = mColor1.blueF() * (1.0 - val) + mColor2.blueF() * val;
            const qreal na = (mColor1.alphaF() * (1.0 - val) + mColor2.alphaF() * val) * (mOpacity * 0.01);

            qreal outR = r * (1.0 - na) + nr * 255.0 * na;
            qreal outG = g * (1.0 - na) + ng * 255.0 * na;
            qreal outB = b * (1.0 - na) + nb * 255.0 * na;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outR)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outG)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outB)));
            *dst++ = a;
        }
    }
}
