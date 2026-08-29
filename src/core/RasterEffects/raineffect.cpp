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
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::RAIN)
{
    mDensity = enve::make_shared<QrealAnimator>(50.0, 1.0, 200.0, 1.0, "density");
    ca_addChild(mDensity);

    mSpeed = enve::make_shared<QrealAnimator>(15.0, 0.0, 100.0, 0.5, "speed");
    ca_addChild(mSpeed);

    mAngle = enve::make_shared<QrealAnimator>(12.0, -60.0, 60.0, 1.0, "angle");
    ca_addChild(mAngle);

    mOpacity = enve::make_shared<QrealAnimator>(70.0, 0.0, 100.0, 1.0, "opacity");
    ca_addChild(mOpacity);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(200, 230, 255));
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
    const qreal timeOffset = relFrame * speed * 0.05;
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

    const qreal cr = mColor.redF();
    const qreal cg = mColor.greenF();
    const qreal cb = mColor.blueF();

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
            const qreal py = ny * mDensity + mTimeOffset;
            const qreal px = nx * mDensity + py * mSlant;
            const qreal cellY = std::floor(py);
            const qreal cellX = std::floor(px);
            const qreal fracY = py - cellY - 0.5;
            const qreal fracX = px - cellX - 0.5;

            const qreal n = std::abs(std::sin(cellX * 127.1 + cellY * 311.7) * 43758.5453);
            const qreal hashVal = n - std::floor(n);
            qreal rain = 0.0;
            if (hashVal > 0.75) {
                const qreal lineX = (hashVal - 0.75) * 4.0 - 0.5;
                const qreal d = std::abs(fracX - lineX * 0.4);
                if (d < 0.06 && std::abs(fracY) < 0.5) {
                    rain = (1.0 - d / 0.06) * (1.0 - std::abs(fracY) / 0.5) * mOpacity;
                }
            }

            const qreal aNorm = a / 255.0;
            const qreal rOut = (r / 255.0) * (1.0 - rain) + cr * aNorm * rain;
            const qreal gOut = (g / 255.0) * (1.0 - rain) + cg * aNorm * rain;
            const qreal bOut = (b / 255.0) * (1.0 - rain) + cb * aNorm * rain;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, rOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, gOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, bOut * 255.0)));
            *dst++ = a;
        }
    }
}
