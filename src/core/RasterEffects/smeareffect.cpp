/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "smeareffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>
#include <algorithm>

SmearEffect::SmearEffect() :
    RasterEffect(QObject::tr("CC Smear"),
                 AppSupport::getRasterEffectHardwareSupport("Smear",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::SMEAR)
{
    mFromX = enve::make_shared<QrealAnimator>(0.45, 0.0, 1.0, 0.01, "from X");
    ca_addChild(mFromX);

    mFromY = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "from Y");
    ca_addChild(mFromY);

    mToX = enve::make_shared<QrealAnimator>(0.55, 0.0, 1.0, 0.01, "to X");
    ca_addChild(mToX);

    mToY = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "to Y");
    ca_addChild(mToY);

    mRadius = enve::make_shared<QrealAnimator>(0.25, 0.01, 1.0, 0.01, "radius");
    ca_addChild(mRadius);

    mElasticity = enve::make_shared<QrealAnimator>(1.5, 0.1, 5.0, 0.1, "elasticity");
    ca_addChild(mElasticity);
}

class SmearEffectCaller : public OpenGLRasterEffectCaller {
public:
    SmearEffectCaller(const HardwareSupport hwSupport,
                      const qreal fromX, const qreal fromY,
                      const qreal toX, const qreal toY,
                      const qreal radius, const qreal elasticity) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/smeareffect.frag",
                                 hwSupport),
        mFromX(fromX), mFromY(fromY),
        mToX(toX), mToY(toY),
        mRadius(radius), mElasticity(elasticity) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sFromPtU = gl->glGetUniformLocation(sProgramId, "fromPt");
        sToPtU = gl->glGetUniformLocation(sProgramId, "toPt");
        sRadiusU = gl->glGetUniformLocation(sProgramId, "radius");
        sElasticityU = gl->glGetUniformLocation(sProgramId, "elasticity");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sFromPtU, toSkScalar(mFromX), toSkScalar(mFromY));
        gl->glUniform2f(sToPtU, toSkScalar(mToX), toSkScalar(mToY));
        gl->glUniform1f(sRadiusU, toSkScalar(mRadius));
        gl->glUniform1f(sElasticityU, toSkScalar(mElasticity));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sFromPtU;
    static GLint sToPtU;
    static GLint sRadiusU;
    static GLint sElasticityU;

    const qreal mFromX;
    const qreal mFromY;
    const qreal mToX;
    const qreal mToY;
    const qreal mRadius;
    const qreal mElasticity;
};

bool SmearEffectCaller::sInitialized = false;
GLuint SmearEffectCaller::sProgramId = 0;

GLint SmearEffectCaller::sFromPtU = -1;
GLint SmearEffectCaller::sToPtU = -1;
GLint SmearEffectCaller::sRadiusU = -1;
GLint SmearEffectCaller::sElasticityU = -1;

stdsptr<RasterEffectCaller> SmearEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal fx = mFromX->getEffectiveValue(relFrame);
    const qreal fy = mFromY->getEffectiveValue(relFrame);
    const qreal rawTx = mToX->getEffectiveValue(relFrame);
    const qreal rawTy = mToY->getEffectiveValue(relFrame);
    // modulate smear displacement amount by influence
    const qreal tx = fx + (rawTx - fx) * influence;
    const qreal ty = fy + (rawTy - fy) * influence;
    const qreal rad = mRadius->getEffectiveValue(relFrame);
    const qreal ela = mElasticity->getEffectiveValue(relFrame);

    return enve::make_shared<SmearEffectCaller>(
                instanceHwSupport(), fx, fy, tx, ty, rad, ela);
}

void SmearEffectCaller::processCpu(CpuRenderTools& renderTools,
                                   const CpuRenderData& data) {
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

    const qreal offsetX = mToX - mFromX;
    const qreal offsetY = mToY - mFromY;
    const qreal r = std::max(0.001, mRadius);
    const qreal ela = std::max(0.01, mElasticity);

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

            const qreal dx = nx - mToX;
            const qreal dy = ny - mToY;
            const qreal dist = std::sqrt(dx * dx + dy * dy);

            qreal sampleX = nx;
            qreal sampleY = ny;

            if (dist < r) {
                const qreal w = std::pow(std::max(0.0, std::min(1.0, 1.0 - dist / r)), ela);
                sampleX = nx - offsetX * w;
                sampleY = ny - offsetY * w;
            }

            const int sx = qRound(sampleX * imgWidth);
            const int sy = qRound(sampleY * imgHeight);

            *dst++ = sampleSrc(sx, sy, 0);
            *dst++ = sampleSrc(sx, sy, 1);
            *dst++ = sampleSrc(sx, sy, 2);
            *dst++ = sampleSrc(sx, sy, 3);
        }
    }
}
