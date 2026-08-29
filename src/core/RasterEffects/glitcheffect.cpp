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

#include "glitcheffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

GlitchEffect::GlitchEffect() :
    RasterEffect("glitch",
                 AppSupport::getRasterEffectHardwareSupport("Glitch",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::GLITCH)
{
    mIntensity = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "intensity");
    ca_addChild(mIntensity);

    mSpeed = enve::make_shared<QrealAnimator>(1.0, 0.0, 10.0, 0.1, "speed");
    ca_addChild(mSpeed);

    mColorDrift = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "color drift");
    ca_addChild(mColorDrift);
}

class GlitchEffectCaller : public OpenGLRasterEffectCaller {
public:
    GlitchEffectCaller(const HardwareSupport hwSupport,
                       const qreal intensity,
                       const qreal timeSeed,
                       const qreal colorDrift) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/glitcheffect.frag",
                                 hwSupport),
        mIntensity(intensity),
        mTimeSeed(timeSeed),
        mColorDrift(colorDrift) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sIntensityU = gl->glGetUniformLocation(sProgramId, "intensity");
        sTimeSeedU = gl->glGetUniformLocation(sProgramId, "timeSeed");
        sColorDriftU = gl->glGetUniformLocation(sProgramId, "colorDrift");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sIntensityU, toSkScalar(mIntensity));
        gl->glUniform1f(sTimeSeedU, toSkScalar(mTimeSeed));
        gl->glUniform1f(sColorDriftU, toSkScalar(mColorDrift));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sIntensityU;
    static GLint sTimeSeedU;
    static GLint sColorDriftU;

    const qreal mIntensity;
    const qreal mTimeSeed;
    const qreal mColorDrift;
};

bool GlitchEffectCaller::sInitialized = false;
GLuint GlitchEffectCaller::sProgramId = 0;

GLint GlitchEffectCaller::sIntensityU = -1;
GLint GlitchEffectCaller::sTimeSeedU = -1;
GLint GlitchEffectCaller::sColorDriftU = -1;

stdsptr<RasterEffectCaller> GlitchEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal intensity = (mIntensity->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal speed = mSpeed->getEffectiveValue(relFrame);
    const qreal timeSeed = relFrame * speed * 0.2;
    const qreal drift = mColorDrift->getEffectiveValue(relFrame) / 100.0;

    return enve::make_shared<GlitchEffectCaller>(
                instanceHwSupport(), intensity, timeSeed, drift);
}

void GlitchEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int driftPx = qRound(mColorDrift * mIntensity * 15.0);

    const auto hash1 = [](qreal p) -> qreal {
        qreal frac = (p * 0.1031);
        frac = frac - std::floor(frac);
        frac *= (frac + 33.33);
        frac *= (frac + frac);
        return frac - std::floor(frac);
    };

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const qreal block = std::floor(yi * 24.0 / imgHeight + mTimeSeed * 3.7);
        const qreal noise = hash1(block);
        int shiftX = 0;
        if (noise > 0.55) {
            shiftX = qRound((hash1(block + 1.23) - 0.5) * mIntensity * 0.08 * imgWidth);
        }

        for(int xi = xMin; xi <= xMax; xi++) {
            const int baseXi = std::max(0, std::min(imgWidth - 1, xi + shiftX));
            const int rXi = std::max(0, std::min(imgWidth - 1, baseXi + driftPx));
            const int bXi = std::max(0, std::min(imgWidth - 1, baseXi - driftPx));

            const auto srcBase = static_cast<const uchar*>(srcBtmp.getAddr(baseXi, yi));
            const auto srcR = static_cast<const uchar*>(srcBtmp.getAddr(rXi, yi));
            const auto srcB = static_cast<const uchar*>(srcBtmp.getAddr(bXi, yi));

            *dst++ = srcR[0];
            *dst++ = srcBase[1];
            *dst++ = srcB[2];
            *dst++ = srcBase[3];
        }
    }
}
