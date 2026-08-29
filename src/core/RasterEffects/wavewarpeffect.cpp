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

#include "wavewarpeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

WaveWarpEffect::WaveWarpEffect() :
    RasterEffect("wave warp",
                 AppSupport::getRasterEffectHardwareSupport("WaveWarp",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::WAVE_WARP)
{
    mAmplitude = enve::make_shared<QrealAnimator>(10.0, 0.0, 100.0, 0.5, "amplitude");
    ca_addChild(mAmplitude);

    mWavelength = enve::make_shared<QrealAnimator>(50.0, 1.0, 500.0, 1.0, "wavelength");
    ca_addChild(mWavelength);

    mSpeed = enve::make_shared<QrealAnimator>(2.0, -50.0, 50.0, 0.1, "speed");
    ca_addChild(mSpeed);

    mDirection = enve::make_shared<QrealAnimator>(0.0, -360.0, 360.0, 1.0, "direction");
    ca_addChild(mDirection);
}

class WaveWarpEffectCaller : public OpenGLRasterEffectCaller {
public:
    WaveWarpEffectCaller(const HardwareSupport hwSupport,
                        const qreal amplitude,
                        const qreal wavelength,
                        const qreal phase,
                        const qreal direction) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/wavewarpeffect.frag",
                                 hwSupport),
        mAmplitude(amplitude),
        mWavelength(wavelength),
        mPhase(phase),
        mDirection(direction) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sAmplitudeU = gl->glGetUniformLocation(sProgramId, "amplitude");
        sFrequencyU = gl->glGetUniformLocation(sProgramId, "frequency");
        sPhaseU = gl->glGetUniformLocation(sProgramId, "phase");
        sWaveDirU = gl->glGetUniformLocation(sProgramId, "waveDir");
        sPerpDirU = gl->glGetUniformLocation(sProgramId, "perpDir");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        const qreal rad = qDegreesToRadians(mDirection);
        const qreal wx = std::cos(rad);
        const qreal wy = std::sin(rad);
        const qreal px = -wy;
        const qreal py = wx;

        const qreal freq = (mWavelength > 0.0) ? (6.2831853 / (mWavelength / 100.0)) : 1.0;

        gl->glUniform1f(sAmplitudeU, toSkScalar(mAmplitude / 1000.0));
        gl->glUniform1f(sFrequencyU, toSkScalar(freq));
        gl->glUniform1f(sPhaseU, toSkScalar(mPhase));
        gl->glUniform2f(sWaveDirU, toSkScalar(wx), toSkScalar(wy));
        gl->glUniform2f(sPerpDirU, toSkScalar(px), toSkScalar(py));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmplitudeU;
    static GLint sFrequencyU;
    static GLint sPhaseU;
    static GLint sWaveDirU;
    static GLint sPerpDirU;

    const qreal mAmplitude;
    const qreal mWavelength;
    const qreal mPhase;
    const qreal mDirection;
};

bool WaveWarpEffectCaller::sInitialized = false;
GLuint WaveWarpEffectCaller::sProgramId = 0;

GLint WaveWarpEffectCaller::sAmplitudeU = -1;
GLint WaveWarpEffectCaller::sFrequencyU = -1;
GLint WaveWarpEffectCaller::sPhaseU = -1;
GLint WaveWarpEffectCaller::sWaveDirU = -1;
GLint WaveWarpEffectCaller::sPerpDirU = -1;

stdsptr<RasterEffectCaller> WaveWarpEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amplitude = mAmplitude->getEffectiveValue(relFrame) * influence;
    const qreal wavelength = mWavelength->getEffectiveValue(relFrame);
    const qreal speed = mSpeed->getEffectiveValue(relFrame);
    const qreal phase = relFrame * speed * 0.1;
    const qreal direction = mDirection->getEffectiveValue(relFrame);

    return enve::make_shared<WaveWarpEffectCaller>(
                instanceHwSupport(), amplitude, wavelength, phase, direction);
}

void WaveWarpEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal rad = qDegreesToRadians(mDirection);
    const qreal wx = std::cos(rad);
    const qreal wy = std::sin(rad);
    const qreal px = -wy;
    const qreal py = wx;
    const qreal freq = (mWavelength > 0.0) ? (6.283185307 / (mWavelength / 100.0)) : 1.0;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const qreal ny = qreal(yi) / imgHeight;

        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal nx = qreal(xi) / imgWidth;
            const qreal pos = nx * wx + ny * wy;
            const qreal wave = std::sin(pos * freq + mPhase) * (mAmplitude / 1000.0);

            const int sx = std::max(0, std::min(imgWidth - 1, qRound((nx + px * wave) * imgWidth)));
            const int sy = std::max(0, std::min(imgHeight - 1, qRound((ny + py * wave) * imgHeight)));

            const auto src = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));
            *dst++ = src[0];
            *dst++ = src[1];
            *dst++ = src[2];
            *dst++ = src[3];
        }
    }
}
