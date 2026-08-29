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

#include "halftoneeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

HalftoneEffect::HalftoneEffect() :
    RasterEffect("halftone",
                 AppSupport::getRasterEffectHardwareSupport("Halftone",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::HALFTONE)
{
    mDotSize = enve::make_shared<QrealAnimator>(8.0, 2.0, 50.0, 1.0, "dot size");
    ca_addChild(mDotSize);

    mAngle = enve::make_shared<QrealAnimator>(45.0, 0.0, 90.0, 1.0, "angle");
    ca_addChild(mAngle);

    mContrast = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "contrast");
    ca_addChild(mContrast);
}

class HalftoneEffectCaller : public OpenGLRasterEffectCaller {
public:
    HalftoneEffectCaller(const HardwareSupport hwSupport,
                         const qreal dotSize,
                         const qreal angle,
                         const qreal contrast) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/halftoneeffect.frag",
                                 hwSupport),
        mDotSize(dotSize),
        mAngle(angle),
        mContrast(contrast) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sDotSizeU = gl->glGetUniformLocation(sProgramId, "dotSize");
        sAngleU = gl->glGetUniformLocation(sProgramId, "angle");
        sContrastU = gl->glGetUniformLocation(sProgramId, "contrast");
        sResolutionU = gl->glGetUniformLocation(sProgramId, "resolution");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sDotSizeU, toSkScalar(mDotSize));
        gl->glUniform1f(sAngleU, toSkScalar(mAngle));
        gl->glUniform1f(sContrastU, toSkScalar(mContrast));
        gl->glUniform2f(sResolutionU, 1920.0f, 1080.0f);
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sDotSizeU;
    static GLint sAngleU;
    static GLint sContrastU;
    static GLint sResolutionU;

    const qreal mDotSize;
    const qreal mAngle;
    const qreal mContrast;
};

bool HalftoneEffectCaller::sInitialized = false;
GLuint HalftoneEffectCaller::sProgramId = 0;

GLint HalftoneEffectCaller::sDotSizeU = -1;
GLint HalftoneEffectCaller::sAngleU = -1;
GLint HalftoneEffectCaller::sContrastU = -1;
GLint HalftoneEffectCaller::sResolutionU = -1;

stdsptr<RasterEffectCaller> HalftoneEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)
    Q_UNUSED(influence)

    const qreal dotSize = mDotSize->getEffectiveValue(relFrame);
    const qreal angle = mAngle->getEffectiveValue(relFrame);
    const qreal contrast = mContrast->getEffectiveValue(relFrame);

    return enve::make_shared<HalftoneEffectCaller>(
                instanceHwSupport(), dotSize, angle, contrast);
}

void HalftoneEffectCaller::processCpu(CpuRenderTools& renderTools,
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
    const qreal cosA = std::cos(rad);
    const qreal sinA = std::sin(rad);
    const qreal halfSize = std::max(1.0, mDotSize * 0.5);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal px = cosA * xi - sinA * yi;
            const qreal py = sinA * xi + cosA * yi;

            const qreal gx = std::fmod(std::abs(px), mDotSize) - halfSize;
            const qreal gy = std::fmod(std::abs(py), mDotSize) - halfSize;
            const qreal dist = std::sqrt(gx * gx + gy * gy) / halfSize;

            const qreal luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
            const qreal val = (dist > (1.0 - luma)) ? 255.0 : 0.0;

            *dst++ = static_cast<uchar>(val);
            *dst++ = static_cast<uchar>(val);
            *dst++ = static_cast<uchar>(val);
            *dst++ = a;
        }
    }
}
