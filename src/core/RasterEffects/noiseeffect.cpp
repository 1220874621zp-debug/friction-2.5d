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

#include "noiseeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

NoiseEffect::NoiseEffect() :
    RasterEffect("noise",
                 AppSupport::getRasterEffectHardwareSupport("Noise",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::NOISE)
{
    mAmount = enve::make_shared<QrealAnimator>(15.0, 0.0, 100.0, 0.5, "amount");
    ca_addChild(mAmount);

    mSpeed = enve::make_shared<QrealAnimator>(1.0, 0.0, 10.0, 0.1, "speed");
    ca_addChild(mSpeed);

    mMonochrome = enve::make_shared<QrealAnimator>(1.0, 0.0, 1.0, 1.0, "monochrome");
    ca_addChild(mMonochrome);
}

class NoiseEffectCaller : public OpenGLRasterEffectCaller {
public:
    NoiseEffectCaller(const HardwareSupport hwSupport,
                      const qreal amount,
                      const qreal timeSeed,
                      const qreal monochrome) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/noiseeffect.frag",
                                 hwSupport),
        mAmount(amount),
        mTimeSeed(timeSeed),
        mMonochrome(monochrome) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
        sTimeSeedU = gl->glGetUniformLocation(sProgramId, "timeSeed");
        sMonochromeU = gl->glGetUniformLocation(sProgramId, "monochrome");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sAmountU, toSkScalar(mAmount));
        gl->glUniform1f(sTimeSeedU, toSkScalar(mTimeSeed));
        gl->glUniform1f(sMonochromeU, toSkScalar(mMonochrome));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;
    static GLint sTimeSeedU;
    static GLint sMonochromeU;

    const qreal mAmount;
    const qreal mTimeSeed;
    const qreal mMonochrome;
};

bool NoiseEffectCaller::sInitialized = false;
GLuint NoiseEffectCaller::sProgramId = 0;

GLint NoiseEffectCaller::sAmountU = -1;
GLint NoiseEffectCaller::sTimeSeedU = -1;
GLint NoiseEffectCaller::sMonochromeU = -1;

stdsptr<RasterEffectCaller> NoiseEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amount = (mAmount->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal speed = mSpeed->getEffectiveValue(relFrame);
    const qreal timeSeed = relFrame * speed * 0.17;
    const qreal monochrome = mMonochrome->getEffectiveValue(relFrame);

    return enve::make_shared<NoiseEffectCaller>(
                instanceHwSupport(), amount, timeSeed, monochrome);
}

void NoiseEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const auto hash2 = [](qreal x, qreal y, qreal seed) -> qreal {
        qreal val = std::sin((x + seed) * 12.9898 + (y + seed * 0.7) * 78.233) * 43758.5453;
        return (val - std::floor(val) - 0.5) * 2.0;
    };

    const bool mono = mMonochrome > 0.5;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal n1 = hash2(xi, yi, mTimeSeed) * mAmount * 255.0;
            const qreal n2 = mono ? n1 : hash2(xi + 100, yi, mTimeSeed) * mAmount * 255.0;
            const qreal n3 = mono ? n1 : hash2(xi, yi + 100, mTimeSeed) * mAmount * 255.0;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, r + n1)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, g + n2)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, b + n3)));
            *dst++ = a;
        }
    }
}
