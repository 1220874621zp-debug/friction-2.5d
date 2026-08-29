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

#include "vignetteeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"

VignetteEffect::VignetteEffect() :
    RasterEffect("vignette",
                 AppSupport::getRasterEffectHardwareSupport("Vignette",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::VIGNETTE)
{
    mRadius = enve::make_shared<QrealAnimator>(0.8, 0.0, 2.0, 0.01, "radius");
    ca_addChild(mRadius);

    mFeather = enve::make_shared<QrealAnimator>(0.4, 0.0, 2.0, 0.01, "feather");
    ca_addChild(mFeather);

    mOpacity = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0, "opacity");
    ca_addChild(mOpacity);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(0, 0, 0));
    ca_addChild(mColor);
}

class VignetteEffectCaller : public OpenGLRasterEffectCaller {
public:
    VignetteEffectCaller(const HardwareSupport hwSupport,
                         const qreal radius,
                         const qreal feather,
                         const qreal opacity,
                         const QColor& color) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/vignetteeffect.frag",
                                 hwSupport),
        mRadius(radius),
        mFeather(feather),
        mOpacity(opacity),
        mColor(color) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sRadiusU = gl->glGetUniformLocation(sProgramId, "radius");
        sFeatherU = gl->glGetUniformLocation(sProgramId, "feather");
        sOpacityU = gl->glGetUniformLocation(sProgramId, "opacity");
        sColorU = gl->glGetUniformLocation(sProgramId, "color");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sRadiusU, mRadius);
        gl->glUniform1f(sFeatherU, mFeather);
        gl->glUniform1f(sOpacityU, mOpacity);
        gl->glUniform4f(sColorU,
                        mColor.redF(),
                        mColor.greenF(),
                        mColor.blueF(),
                        mColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sRadiusU;
    static GLint sFeatherU;
    static GLint sOpacityU;
    static GLint sColorU;

    const qreal mRadius;
    const qreal mFeather;
    const qreal mOpacity;
    const QColor mColor;
};

bool VignetteEffectCaller::sInitialized = false;
GLuint VignetteEffectCaller::sProgramId = 0;

GLint VignetteEffectCaller::sRadiusU = -1;
GLint VignetteEffectCaller::sFeatherU = -1;
GLint VignetteEffectCaller::sOpacityU = -1;
GLint VignetteEffectCaller::sColorU = -1;

stdsptr<RasterEffectCaller> VignetteEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal radius = mRadius->getEffectiveValue(relFrame);
    const qreal feather = mFeather->getEffectiveValue(relFrame);
    const qreal opacity = (mOpacity->getEffectiveValue(relFrame) / 100.0) * influence;
    const QColor color = mColor->getColor(relFrame);

    return enve::make_shared<VignetteEffectCaller>(
                instanceHwSupport(), radius, feather, opacity, color);
}

void VignetteEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal start = std::max(0.0, mRadius - mFeather);
    const qreal end = std::max(start + 0.0001, mRadius + mFeather);
    const qreal invSpan = 1.0 / (end - start);

    const qreal cr = mColor.redF();
    const qreal cg = mColor.greenF();
    const qreal cb = mColor.blueF();

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal ny = (qreal(yi) / imgHeight) - 0.5;

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal nx = (qreal(xi) / imgWidth) - 0.5;
            const qreal dist = std::sqrt(nx * nx + ny * ny) * 2.8284;
            qreal t = (dist - start) * invSpan;
            t = std::max(0.0, std::min(1.0, t));
            const qreal vig = (t * t * (3.0 - 2.0 * t)) * mOpacity;

            const qreal aNorm = a / 255.0;
            const qreal rOut = (r / 255.0) * (1.0 - vig) + cr * aNorm * vig;
            const qreal gOut = (g / 255.0) * (1.0 - vig) + cg * aNorm * vig;
            const qreal bOut = (b / 255.0) * (1.0 - vig) + cb * aNorm * vig;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, rOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, gOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, bOut * 255.0)));
            *dst++ = a;
        }
    }
}
