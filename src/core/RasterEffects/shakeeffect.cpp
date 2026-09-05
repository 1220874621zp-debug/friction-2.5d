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

#include "shakeeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

ShakeEffect::ShakeEffect() :
    RasterEffect(QObject::tr("Shake"),
                 AppSupport::getRasterEffectHardwareSupport("Shake",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::SHAKE)
{
    mAmplitude = enve::make_shared<QrealAnimator>(20.0, 0.0, 200.0, 1.0, "amplitude");
    ca_addChild(mAmplitude);

    mFrequency = enve::make_shared<QrealAnimator>(15.0, 0.0, 50.0, 0.5, "frequency");
    ca_addChild(mFrequency);

    mRandomness = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "randomness");
    ca_addChild(mRandomness);

    mRotation = enve::make_shared<QrealAnimator>(3.0, -45.0, 45.0, 0.1, "rotation");
    ca_addChild(mRotation);
}

class ShakeEffectCaller : public OpenGLRasterEffectCaller {
public:
    ShakeEffectCaller(const HardwareSupport hwSupport,
                      const QPointF& offset,
                      const qreal rotAngle,
                      const QMargins& margins) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/shakeeffect.frag",
                                 hwSupport,
                                 true,
                                 margins),
        mOffset(offset),
        mRotAngle(rotAngle) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sOffsetU = gl->glGetUniformLocation(sProgramId, "offset");
        sRotAngleU = gl->glGetUniformLocation(sProgramId, "rotAngle");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sOffsetU, toSkScalar(mOffset.x()), toSkScalar(mOffset.y()));
        gl->glUniform1f(sRotAngleU, toSkScalar(mRotAngle));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sOffsetU;
    static GLint sRotAngleU;

    const QPointF mOffset;
    const qreal mRotAngle;
};

bool ShakeEffectCaller::sInitialized = false;
GLuint ShakeEffectCaller::sProgramId = 0;

GLint ShakeEffectCaller::sOffsetU = -1;
GLint ShakeEffectCaller::sRotAngleU = -1;

stdsptr<RasterEffectCaller> ShakeEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal ampPixels = mAmplitude->getEffectiveValue(relFrame) * influence;
    const qreal amp = (ampPixels / 1000.0);
    const qreal freq = mFrequency->getEffectiveValue(relFrame);
    const qreal randFactor = mRandomness->getEffectiveValue(relFrame) / 100.0;
    const qreal rotDeg = mRotation->getEffectiveValue(relFrame) * influence;

    const qreal t = relFrame * freq * 0.1;
    const qreal j1 = std::sin(t * 1.3) * 0.6 + std::sin(t * 2.7) * 0.4;
    const qreal j2 = std::cos(t * 1.7) * 0.6 + std::cos(t * 3.1) * 0.4;
    const qreal randX = std::sin(std::floor(t) * 123.456) * randFactor;
    const qreal randY = std::cos(std::floor(t) * 654.321) * randFactor;

    const QPointF offset((j1 + randX) * amp, (j2 + randY) * amp);
    const qreal rotAngle = qDegreesToRadians((j1 * 0.7 + randX * 0.3) * rotDeg);

    const int m = qCeil(ampPixels + 10);

    return enve::make_shared<ShakeEffectCaller>(
                instanceHwSupport(), offset, rotAngle, QMargins(m, m, m, m));
}

void ShakeEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal cosA = std::cos(mRotAngle);
    const qreal sinA = std::sin(mRotAngle);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const qreal ny = (qreal(yi) / imgHeight) - 0.5;

        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal nx = (qreal(xi) / imgWidth) - 0.5;

            const qreal rx = cosA * nx - sinA * ny + 0.5 - mOffset.x();
            const qreal ry = sinA * nx + cosA * ny + 0.5 - mOffset.y();

            const int sx = std::max(0, std::min(imgWidth - 1, int(std::round(rx * imgWidth))));
            const int sy = std::max(0, std::min(imgHeight - 1, int(std::round(ry * imgHeight))));
            const auto src = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));

            *dst++ = src[0];
            *dst++ = src[1];
            *dst++ = src[2];
            *dst++ = src[3];
        }
    }
}
