/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "filmgraineffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "appsupport.h"

FilmGrainEffect::FilmGrainEffect() :
    RasterEffect(QObject::tr("Film Grain"),
                 AppSupport::getRasterEffectHardwareSupport("FilmGrain",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::FILM_GRAIN)
{
    mAmount = enve::make_shared<QrealAnimator>(25.0, 0.0, 100.0, 1.0, "amount");
    ca_addChild(mAmount);

    mSize = enve::make_shared<QrealAnimator>(1.5, 0.5, 5.0, 0.1, "size");
    ca_addChild(mSize);

    mSpeed = enve::make_shared<QrealAnimator>(24.0, 0.0, 60.0, 1.0, "speed");
    ca_addChild(mSpeed);

    mColorGrain = enve::make_shared<QrealAnimator>(0.0, 0.0, 1.0, 1.0, "colorGrain");
    ca_addChild(mColorGrain);
}



class FilmGrainEffectCaller : public OpenGLRasterEffectCaller {
public:
    FilmGrainEffectCaller(const HardwareSupport hwSupport,
                          qreal amount,
                          qreal size,
                          qreal seed,
                          qreal colorGrain) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/filmgraineffect.frag",
                                 hwSupport),
        mAmount(amount),
        mSize(size),
        mSeed(seed),
        mColorGrain(colorGrain) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
        sSizeU = gl->glGetUniformLocation(sProgramId, "size");
        sSeedU = gl->glGetUniformLocation(sProgramId, "seed");
        sColorGrainU = gl->glGetUniformLocation(sProgramId, "colorGrain");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sAmountU, toSkScalar(mAmount));
        gl->glUniform1f(sSizeU, toSkScalar(mSize));
        gl->glUniform1f(sSeedU, toSkScalar(mSeed));
        gl->glUniform1f(sColorGrainU, toSkScalar(mColorGrain));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;
    static GLint sSizeU;
    static GLint sSeedU;
    static GLint sColorGrainU;

    const qreal mAmount;
    const qreal mSize;
    const qreal mSeed;
    const qreal mColorGrain;
};

bool FilmGrainEffectCaller::sInitialized = false;
GLuint FilmGrainEffectCaller::sProgramId = 0;

GLint FilmGrainEffectCaller::sAmountU = -1;
GLint FilmGrainEffectCaller::sSizeU = -1;
GLint FilmGrainEffectCaller::sSeedU = -1;
GLint FilmGrainEffectCaller::sColorGrainU = -1;

stdsptr<RasterEffectCaller> FilmGrainEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amount = mAmount->getEffectiveValue(relFrame) * influence;
    const qreal size = mSize->getEffectiveValue(relFrame);
    const qreal speed = mSpeed->getEffectiveValue(relFrame);
    const qreal seed = relFrame * (speed * 0.1);
    const qreal colorGrain = mColorGrain->getEffectiveValue(relFrame);

    return enve::make_shared<FilmGrainEffectCaller>(
                instanceHwSupport(), amount, size, seed, colorGrain);
}

void FilmGrainEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

            const qreal luma = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
            const qreal grainMask = 1.0 - 2.0 * std::abs(luma - 0.5);

            qreal n = std::abs(std::sin(xi * 12.9898 + yi * 78.233 + mSeed * 37.719) * 43758.5453);
            n = (n - std::floor(n)) * 2.0 - 1.0;

            const qreal noiseVal = n * (mAmount * 0.01) * 255.0 * (0.4 + 0.6 * grainMask);

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, r + noiseVal)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, g + noiseVal)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, b + noiseVal)));
            *dst++ = a;
        }
    }
}
