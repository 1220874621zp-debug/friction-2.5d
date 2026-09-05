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

#include "colorgradingeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

ColorGradingEffect::ColorGradingEffect() :
    RasterEffect(QObject::tr("Color Grading"),
                 AppSupport::getRasterEffectHardwareSupport("Color Grading",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::COLOR_GRADING)
{
    mExposure = enve::make_shared<QrealAnimator>(0.0, -5.0, 5.0, 0.1, "exposure");
    ca_addChild(mExposure);

    mContrast = enve::make_shared<QrealAnimator>(0.0, -100.0, 100.0, 1.0, "contrast");
    ca_addChild(mContrast);

    mSaturation = enve::make_shared<QrealAnimator>(0.0, -100.0, 100.0, 1.0, "saturation");
    ca_addChild(mSaturation);

    mTemperature = enve::make_shared<QrealAnimator>(0.0, -100.0, 100.0, 1.0, "temperature");
    ca_addChild(mTemperature);

    mTint = enve::make_shared<QrealAnimator>(0.0, -100.0, 100.0, 1.0, "tint");
    ca_addChild(mTint);
}

class ColorGradingEffectCaller : public OpenGLRasterEffectCaller {
public:
    ColorGradingEffectCaller(const HardwareSupport hwSupport,
                             const qreal exposure,
                             const qreal contrast,
                             const qreal saturation,
                             const qreal temperature,
                             const qreal tint) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/colorgradingeffect.frag",
                                 hwSupport),
        mExposure(exposure),
        mContrast(contrast),
        mSaturation(saturation),
        mTemperature(temperature),
        mTint(tint) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sExposureU = gl->glGetUniformLocation(sProgramId, "exposure");
        sContrastU = gl->glGetUniformLocation(sProgramId, "contrast");
        sSaturationU = gl->glGetUniformLocation(sProgramId, "saturation");
        sTemperatureU = gl->glGetUniformLocation(sProgramId, "temperature");
        sTintU = gl->glGetUniformLocation(sProgramId, "tint");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sExposureU, toSkScalar(mExposure));
        gl->glUniform1f(sContrastU, toSkScalar(mContrast));
        gl->glUniform1f(sSaturationU, toSkScalar(mSaturation));
        gl->glUniform1f(sTemperatureU, toSkScalar(mTemperature));
        gl->glUniform1f(sTintU, toSkScalar(mTint));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sExposureU;
    static GLint sContrastU;
    static GLint sSaturationU;
    static GLint sTemperatureU;
    static GLint sTintU;

    const qreal mExposure;
    const qreal mContrast;
    const qreal mSaturation;
    const qreal mTemperature;
    const qreal mTint;
};

bool ColorGradingEffectCaller::sInitialized = false;
GLuint ColorGradingEffectCaller::sProgramId = 0;

GLint ColorGradingEffectCaller::sExposureU = -1;
GLint ColorGradingEffectCaller::sContrastU = -1;
GLint ColorGradingEffectCaller::sSaturationU = -1;
GLint ColorGradingEffectCaller::sTemperatureU = -1;
GLint ColorGradingEffectCaller::sTintU = -1;

stdsptr<RasterEffectCaller> ColorGradingEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal exposure = mExposure->getEffectiveValue(relFrame) * influence;
    const qreal contrast = (mContrast->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal saturation = (mSaturation->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal temperature = (mTemperature->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal tint = (mTint->getEffectiveValue(relFrame) / 100.0) * influence;

    return enve::make_shared<ColorGradingEffectCaller>(
                instanceHwSupport(), exposure, contrast, saturation, temperature, tint);
}

void ColorGradingEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal expMult = std::pow(2.0, mExposure);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            qreal r = (*src++ / 255.0) * expMult;
            qreal g = (*src++ / 255.0) * expMult;
            qreal b = (*src++ / 255.0) * expMult;
            const uchar a = *src++;

            r += mTemperature * 0.1 + mTint * 0.05;
            b -= mTemperature * 0.1 - mTint * 0.05;
            g -= mTint * 0.1;

            r = (r - 0.5) * (1.0 + mContrast) + 0.5;
            g = (g - 0.5) * (1.0 + mContrast) + 0.5;
            b = (b - 0.5) * (1.0 + mContrast) + 0.5;

            const qreal luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const qreal satMult = std::max(0.0, 1.0 + mSaturation);
            r = luma + (r - luma) * satMult;
            g = luma + (g - luma) * satMult;
            b = luma + (b - luma) * satMult;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, r * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, g * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, b * 255.0)));
            *dst++ = a;
        }
    }
}
