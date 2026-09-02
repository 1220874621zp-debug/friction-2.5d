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

#include "directionalblureffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

DirectionalBlurEffect::DirectionalBlurEffect() :
    RasterEffect(QObject::tr("Directional Blur"),
                 AppSupport::getRasterEffectHardwareSupport("DirectionalBlur",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::DIRECTIONAL_BLUR)
{
    mLength = enve::make_shared<QrealAnimator>(15.0, 0.0, 300.0, 0.5, "length");
    ca_addChild(mLength);

    mAngle = enve::make_shared<QrealAnimator>(0.0, 0.0, 360.0, 1.0, "angle");
    ca_addChild(mAngle);
}

class DirectionalBlurEffectCaller : public OpenGLRasterEffectCaller {
public:
    DirectionalBlurEffectCaller(const HardwareSupport hwSupport,
                                const qreal length,
                                const qreal angle,
                                const QMargins& margins) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/directionalblureffect.frag",
                                 hwSupport,
                                 true,
                                 margins),
        mLength(length),
        mAngle(angle) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sDirStepU = gl->glGetUniformLocation(sProgramId, "dirStep");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        const qreal rad = qDegreesToRadians(mAngle);
        const qreal dx = std::cos(rad) * (mLength / 500.0);
        const qreal dy = std::sin(rad) * (mLength / 500.0);
        gl->glUniform2f(sDirStepU, toSkScalar(dx), toSkScalar(dy));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sDirStepU;

    const qreal mLength;
    const qreal mAngle;
};

bool DirectionalBlurEffectCaller::sInitialized = false;
GLuint DirectionalBlurEffectCaller::sProgramId = 0;

GLint DirectionalBlurEffectCaller::sDirStepU = -1;

stdsptr<RasterEffectCaller> DirectionalBlurEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal length = mLength->getEffectiveValue(relFrame) * influence;
    const qreal angle = mAngle->getEffectiveValue(relFrame);

    const int m = qCeil(length);

    return enve::make_shared<DirectionalBlurEffectCaller>(
                instanceHwSupport(), length, angle, QMargins(m, m, m, m));
}

void DirectionalBlurEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal rad = qDegreesToRadians(mAngle);
    const qreal dirX = std::cos(rad) * (mLength / 2.0);
    const qreal dirY = std::sin(rad) * (mLength / 2.0);

    const int SAMPLES = 24;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));

        for(int xi = xMin; xi <= xMax; xi++) {
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            for(int s = 0; s < SAMPLES; s++) {
                const qreal t = (qreal(s) / (SAMPLES - 1)) - 0.5;
                const int sx = std::max(0, std::min(imgWidth - 1, int(std::round(xi + dirX * t))));
                const int sy = std::max(0, std::min(imgHeight - 1, int(std::round(yi + dirY * t))));
                const auto smp = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));

                sumR += smp[0];
                sumG += smp[1];
                sumB += smp[2];
                sumA += smp[3];
            }

            *dst++ = static_cast<uchar>(sumR / SAMPLES);
            *dst++ = static_cast<uchar>(sumG / SAMPLES);
            *dst++ = static_cast<uchar>(sumB / SAMPLES);
            *dst++ = static_cast<uchar>(sumA / SAMPLES);
        }
    }
}
