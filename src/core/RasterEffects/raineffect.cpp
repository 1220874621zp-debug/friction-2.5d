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

#include "raineffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"
#include <QtMath>

RainEffect::RainEffect() :
    RasterEffect("rain",
                 AppSupport::getRasterEffectHardwareSupport("Rain",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::RAIN)
{
    mDensity = enve::make_shared<QrealAnimator>(60.0, 1.0, 300.0, 1.0, "density");
    ca_addChild(mDensity);

    mSpeed = enve::make_shared<QrealAnimator>(30.0, 0.0, 100.0, 0.5, "speed");
    ca_addChild(mSpeed);

    mAngle = enve::make_shared<QrealAnimator>(12.0, -60.0, 60.0, 1.0, "angle");
    ca_addChild(mAngle);

    mOpacity = enve::make_shared<QrealAnimator>(80.0, 0.0, 100.0, 1.0, "opacity");
    ca_addChild(mOpacity);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(210, 235, 255, 230));
    ca_addChild(mColor);
}

class RainEffectCaller : public OpenGLRasterEffectCaller {
public:
    RainEffectCaller(const HardwareSupport hwSupport,
                     const qreal density,
                     const qreal timeOffset,
                     const qreal slant,
                     const qreal opacity,
                     const QColor& color) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/raineffect.frag",
                                 hwSupport),
        mDensity(density),
        mTimeOffset(timeOffset),
        mSlant(slant),
        mOpacity(opacity),
        mColor(color) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sDensityU = gl->glGetUniformLocation(sProgramId, "density");
        sTimeOffsetU = gl->glGetUniformLocation(sProgramId, "timeOffset");
        sSlantU = gl->glGetUniformLocation(sProgramId, "slant");
        sOpacityU = gl->glGetUniformLocation(sProgramId, "opacity");
        sRainColorU = gl->glGetUniformLocation(sProgramId, "rainColor");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sDensityU, toSkScalar(mDensity));
        gl->glUniform1f(sTimeOffsetU, toSkScalar(mTimeOffset));
        gl->glUniform1f(sSlantU, toSkScalar(mSlant));
        gl->glUniform1f(sOpacityU, toSkScalar(mOpacity));
        gl->glUniform4f(sRainColorU,
                        mColor.redF(),
                        mColor.greenF(),
                        mColor.blueF(),
                        mColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sDensityU;
    static GLint sTimeOffsetU;
    static GLint sSlantU;
    static GLint sOpacityU;
    static GLint sRainColorU;

    const qreal mDensity;
    const qreal mTimeOffset;
    const qreal mSlant;
    const qreal mOpacity;
    const QColor mColor;
};

bool RainEffectCaller::sInitialized = false;
GLuint RainEffectCaller::sProgramId = 0;

GLint RainEffectCaller::sDensityU = -1;
GLint RainEffectCaller::sTimeOffsetU = -1;
GLint RainEffectCaller::sSlantU = -1;
GLint RainEffectCaller::sOpacityU = -1;
GLint RainEffectCaller::sRainColorU = -1;

stdsptr<RasterEffectCaller> RainEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal density = mDensity->getEffectiveValue(relFrame);
    const qreal speed = mSpeed->getEffectiveValue(relFrame);
    const qreal timeOffset = relFrame * (speed * 0.02) + 0.1;
    const qreal angle = mAngle->getEffectiveValue(relFrame);
    const qreal slant = std::tan(qDegreesToRadians(angle));
    const qreal opacity = (mOpacity->getEffectiveValue(relFrame) / 100.0) * influence;
    const QColor color = mColor->getColor(relFrame);

    return enve::make_shared<RainEffectCaller>(
                instanceHwSupport(), density, timeOffset, slant, opacity, color);
}

void RainEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal cr = mColor.redF() * 255.0;
    const qreal cg = mColor.greenF() * 255.0;
    const qreal cb = mColor.blueF() * 255.0;

    const auto streakAt = [this](qreal nx, qreal ny, qreal scale, qreal speed, qreal seed) -> qreal {
        qreal px = (nx + ny * mSlant + seed) * scale * 1.5;
        qreal py = (ny + mTimeOffset * speed) * scale * 0.1;
        qreal cellX = std::floor(px);
        qreal cellY = std::floor(py);
        qreal fracX = px - cellX - 0.5;
        qreal fracY = py - cellY - 0.5;

        qreal n = std::abs(std::sin(cellX * 127.1 + cellY * 311.7 + seed * 17.13) * 43758.5453);
        qreal h = n - std::floor(n);
        if (h < 0.5) return 0.0;

        qreal dropX = (h - 0.5) * 1.6 - 0.8;
        qreal dist = std::abs(fracX - dropX * 0.35);
        if (dist < 0.06 && std::abs(fracY) < 0.5) {
            qreal sx = 1.0 - dist / 0.06;
            qreal sy = 1.0 - std::abs(fracY) / 0.5;
            return sx * sy * (0.5 + 0.5 * h);
        }
        return 0.0;
    };

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal ny = qreal(yi) / imgHeight;

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal nx = qreal(xi) / imgWidth;
            qreal r1 = streakAt(nx, ny, mDensity * 0.5, 1.8, 1.0) * 0.7;
            qreal r2 = streakAt(nx + 0.23, ny + 0.41, mDensity * 1.0, 1.4, 2.5) * 0.5;
            qreal r3 = streakAt(nx + 0.67, ny + 0.83, mDensity * 2.0, 1.0, 5.7) * 0.3;
            qreal rain = std::min(1.0, (r1 + r2 + r3) * mOpacity);

            const qreal outR = std::min(255.0, r + cr * rain * 1.5);
            const qreal outG = std::min(255.0, g + cg * rain * 1.5);
            const qreal outB = std::min(255.0, b + cb * rain * 1.5);
            const qreal outA = std::min(255.0, a + rain * 255.0 * mColor.alphaF());

            *dst++ = static_cast<uchar>(outR);
            *dst++ = static_cast<uchar>(outG);
            *dst++ = static_cast<uchar>(outB);
            *dst++ = static_cast<uchar>(outA);
        }
    }
}
