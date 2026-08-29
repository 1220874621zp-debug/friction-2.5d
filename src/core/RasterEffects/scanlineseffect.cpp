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

#include "scanlineseffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

ScanlinesEffect::ScanlinesEffect() :
    RasterEffect("scanlines",
                 AppSupport::getRasterEffectHardwareSupport("Scanlines",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::SCANLINES)
{
    mDensity = enve::make_shared<QrealAnimator>(150.0, 1.0, 1000.0, 5.0, "density");
    ca_addChild(mDensity);

    mOpacity = enve::make_shared<QrealAnimator>(35.0, 0.0, 100.0, 1.0, "opacity");
    ca_addChild(mOpacity);

    mSpeed = enve::make_shared<QrealAnimator>(0.0, -100.0, 100.0, 0.5, "speed");
    ca_addChild(mSpeed);
}

class ScanlinesEffectCaller : public OpenGLRasterEffectCaller {
public:
    ScanlinesEffectCaller(const HardwareSupport hwSupport,
                          const qreal density,
                          const qreal opacity,
                          const qreal timeOffset) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/scanlineseffect.frag",
                                 hwSupport),
        mDensity(density),
        mOpacity(opacity),
        mTimeOffset(timeOffset) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sDensityU = gl->glGetUniformLocation(sProgramId, "density");
        sOpacityU = gl->glGetUniformLocation(sProgramId, "opacity");
        sTimeOffsetU = gl->glGetUniformLocation(sProgramId, "timeOffset");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sDensityU, mDensity);
        gl->glUniform1f(sOpacityU, mOpacity);
        gl->glUniform1f(sTimeOffsetU, mTimeOffset);
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sDensityU;
    static GLint sOpacityU;
    static GLint sTimeOffsetU;

    const qreal mDensity;
    const qreal mOpacity;
    const qreal mTimeOffset;
};

bool ScanlinesEffectCaller::sInitialized = false;
GLuint ScanlinesEffectCaller::sProgramId = 0;

GLint ScanlinesEffectCaller::sDensityU = -1;
GLint ScanlinesEffectCaller::sOpacityU = -1;
GLint ScanlinesEffectCaller::sTimeOffsetU = -1;

stdsptr<RasterEffectCaller> ScanlinesEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal density = mDensity->getEffectiveValue(relFrame);
    const qreal opacity = (mOpacity->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal speed = mSpeed->getEffectiveValue(relFrame);
    const qreal timeOffset = (speed != 0.0) ? (relFrame * speed * 0.005) : 0.0;

    return enve::make_shared<ScanlinesEffectCaller>(
                instanceHwSupport(), density, opacity, timeOffset);
}

void ScanlinesEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal twoPi = 6.283185307179586;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal vPos = qreal(yi) / imgHeight;
        const qreal line = std::sin((vPos + mTimeOffset) * mDensity * twoPi);
        const qreal scan = 0.5 + 0.5 * line;
        const qreal factor = 1.0 - (scan * mOpacity);

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, r * factor)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, g * factor)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, b * factor)));
            *dst++ = a;
        }
    }
}
