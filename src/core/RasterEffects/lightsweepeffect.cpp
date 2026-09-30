/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "lightsweepeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"
#include <QtMath>

LightSweepEffect::LightSweepEffect() :
    RasterEffect(QObject::tr("Light Sweep"),
                 AppSupport::getRasterEffectHardwareSupport("LightSweep",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::LIGHT_SWEEP)
{
    mCenter = enve::make_shared<QrealAnimator>(50.0, -50.0, 150.0, 1.0, "center");
    ca_addChild(mCenter);

    mAngle = enve::make_shared<QrealAnimator>(35.0, -180.0, 180.0, 1.0, "angle");
    ca_addChild(mAngle);

    mWidth = enve::make_shared<QrealAnimator>(40.0, 1.0, 200.0, 1.0, "width");
    ca_addChild(mWidth);

    mIntensity = enve::make_shared<QrealAnimator>(100.0, 0.0, 500.0, 1.0, "intensity");
    ca_addChild(mIntensity);

    mFeather = enve::make_shared<QrealAnimator>(50.0, 1.0, 100.0, 1.0, "feather");
    ca_addChild(mFeather);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(255, 255, 255, 255));
    ca_addChild(mColor);
}



class LightSweepEffectCaller : public OpenGLRasterEffectCaller {
public:
    LightSweepEffectCaller(const HardwareSupport hwSupport,
                           qreal center,
                           qreal angle,
                           qreal width,
                           qreal intensity,
                           qreal feather,
                           const QColor& color) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/lightsweepeffect.frag",
                                 hwSupport),
        mCenter(center),
        mAngle(angle),
        mWidth(width),
        mIntensity(intensity),
        mFeather(feather),
        mColor(color) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sCenterU = gl->glGetUniformLocation(sProgramId, "center");
        sAngleU = gl->glGetUniformLocation(sProgramId, "angle");
        sWidthU = gl->glGetUniformLocation(sProgramId, "width");
        sIntensityU = gl->glGetUniformLocation(sProgramId, "intensity");
        sFeatherU = gl->glGetUniformLocation(sProgramId, "feather");
        sLightColorU = gl->glGetUniformLocation(sProgramId, "lightColor");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sCenterU, toSkScalar(mCenter));
        gl->glUniform1f(sAngleU, toSkScalar(mAngle));
        gl->glUniform1f(sWidthU, toSkScalar(mWidth));
        gl->glUniform1f(sIntensityU, toSkScalar(mIntensity));
        gl->glUniform1f(sFeatherU, toSkScalar(mFeather));
        gl->glUniform4f(sLightColorU, mColor.redF(), mColor.greenF(), mColor.blueF(), mColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sCenterU;
    static GLint sAngleU;
    static GLint sWidthU;
    static GLint sIntensityU;
    static GLint sFeatherU;
    static GLint sLightColorU;

    const qreal mCenter;
    const qreal mAngle;
    const qreal mWidth;
    const qreal mIntensity;
    const qreal mFeather;
    const QColor mColor;
};

bool LightSweepEffectCaller::sInitialized = false;
GLuint LightSweepEffectCaller::sProgramId = 0;

GLint LightSweepEffectCaller::sCenterU = -1;
GLint LightSweepEffectCaller::sAngleU = -1;
GLint LightSweepEffectCaller::sWidthU = -1;
GLint LightSweepEffectCaller::sIntensityU = -1;
GLint LightSweepEffectCaller::sFeatherU = -1;
GLint LightSweepEffectCaller::sLightColorU = -1;

stdsptr<RasterEffectCaller> LightSweepEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal center = mCenter->getEffectiveValue(relFrame);
    const qreal angle = mAngle->getEffectiveValue(relFrame);
    const qreal width = mWidth->getEffectiveValue(relFrame);
    const qreal intensity = mIntensity->getEffectiveValue(relFrame) * influence;
    const qreal feather = mFeather->getEffectiveValue(relFrame);
    const QColor color = mColor->getColor(relFrame);

    return enve::make_shared<LightSweepEffectCaller>(
                instanceHwSupport(), center, angle, width, intensity, feather, color);
}

void LightSweepEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

    const qreal rad = qDegreesToRadians(mAngle);
    const qreal nx = -std::sin(rad);
    const qreal ny = std::cos(rad);
    const qreal sweepPos = (mCenter - 50.0) * 0.02;
    const qreal halfW = std::max(mWidth * 0.005, 0.001);
    const qreal fth = std::max(mFeather * 0.01 * halfW, 0.0001);
    const qreal lr = mColor.redF() * 255.0;
    const qreal lg = mColor.greenF() * 255.0;
    const qreal lb = mColor.blueF() * 255.0;
    const qreal la = mColor.alphaF();

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal uvy = (qreal(yi) / imgHeight) - 0.5;

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            if (a < 2) {
                *dst++ = 0; *dst++ = 0; *dst++ = 0; *dst++ = 0;
                continue;
            }

            const qreal uvx = (qreal(xi) / imgWidth) - 0.5;
            const qreal proj = uvx * nx + uvy * ny;
            const qreal dist = std::abs(proj - sweepPos);

            qreal beam = 0.0;
            if (dist < halfW + fth) {
                beam = (dist < halfW - fth) ? 1.0 : (1.0 - (dist - (halfW - fth)) / (2.0 * fth));
            }
            qreal core = (dist < halfW * 0.4) ? (1.0 - dist / (halfW * 0.4)) * 0.5 : 0.0;
            qreal lightVal = (beam + core) * (mIntensity * 0.01) * la;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, r + lr * lightVal)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, g + lg * lightVal)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, b + lb * lightVal)));
            *dst++ = a;
        }
    }
}
