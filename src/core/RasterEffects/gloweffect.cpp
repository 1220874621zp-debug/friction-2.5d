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

#include "gloweffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"

GlowEffect::GlowEffect() :
    RasterEffect("glow",
                 AppSupport::getRasterEffectHardwareSupport("Glow",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::GLOW)
{
    mThreshold = enve::make_shared<QrealAnimator>(0.6, 0.0, 1.0, 0.01, "threshold");
    ca_addChild(mThreshold);

    mIntensity = enve::make_shared<QrealAnimator>(1.5, 0.0, 10.0, 0.1, "intensity");
    ca_addChild(mIntensity);

    mRadius = enve::make_shared<QrealAnimator>(10.0, 1.0, 50.0, 0.5, "radius");
    ca_addChild(mRadius);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(255, 255, 255));
    ca_addChild(mColor);
}

class GlowEffectCaller : public OpenGLRasterEffectCaller {
public:
    GlowEffectCaller(const HardwareSupport hwSupport,
                     const qreal threshold,
                     const qreal intensity,
                     const qreal radius,
                     const QColor& color) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/gloweffect.frag",
                                 hwSupport),
        mThreshold(threshold),
        mIntensity(intensity),
        mRadius(radius),
        mColor(color) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sThresholdU = gl->glGetUniformLocation(sProgramId, "threshold");
        sIntensityU = gl->glGetUniformLocation(sProgramId, "intensity");
        sRadiusU = gl->glGetUniformLocation(sProgramId, "radius");
        sGlowColorU = gl->glGetUniformLocation(sProgramId, "glowColor");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sThresholdU, mThreshold);
        gl->glUniform1f(sIntensityU, mIntensity);
        gl->glUniform1f(sRadiusU, toSkScalar(mRadius / 1000.0));
        gl->glUniform4f(sGlowColorU,
                        mColor.redF(),
                        mColor.greenF(),
                        mColor.blueF(),
                        mColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sThresholdU;
    static GLint sIntensityU;
    static GLint sRadiusU;
    static GLint sGlowColorU;

    const qreal mThreshold;
    const qreal mIntensity;
    const qreal mRadius;
    const QColor mColor;
};

bool GlowEffectCaller::sInitialized = false;
GLuint GlowEffectCaller::sProgramId = 0;

GLint GlowEffectCaller::sThresholdU = -1;
GLint GlowEffectCaller::sIntensityU = -1;
GLint GlowEffectCaller::sRadiusU = -1;
GLint GlowEffectCaller::sGlowColorU = -1;

stdsptr<RasterEffectCaller> GlowEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal threshold = mThreshold->getEffectiveValue(relFrame);
    const qreal intensity = mIntensity->getEffectiveValue(relFrame) * influence;
    const qreal radius = mRadius->getEffectiveValue(relFrame);
    const QColor color = mColor->getColor(relFrame);

    return enve::make_shared<GlowEffectCaller>(
                instanceHwSupport(), threshold, intensity, radius, color);
}

void GlowEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int rad = std::max(1, qRound(mRadius));
    const qreal cr = mColor.redF();
    const qreal cg = mColor.greenF();
    const qreal cb = mColor.blueF();

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            qreal bloomR = 0;
            qreal bloomG = 0;
            qreal bloomB = 0;
            int samples = 0;

            const int sx[4] = {-rad, rad, 0, 0};
            const int sy[4] = {0, 0, -rad, rad};

            for(int s = 0; s < 4; s++) {
                const int px = std::max(0, std::min(imgWidth - 1, xi + sx[s]));
                const int py = std::max(0, std::min(imgHeight - 1, yi + sy[s]));
                const auto smp = static_cast<const uchar*>(srcBtmp.getAddr(px, py));
                const qreal sr = smp[0] / 255.0;
                const qreal sg = smp[1] / 255.0;
                const qreal sb = smp[2] / 255.0;
                const qreal luma = 0.2126 * sr + 0.7152 * sg + 0.0722 * sb;
                const qreal pass = std::max(0.0, luma - mThreshold);
                bloomR += sr * pass;
                bloomG += sg * pass;
                bloomB += sb * pass;
                samples++;
            }

            const qreal factor = (samples > 0) ? (mIntensity / samples) : 0;
            bloomR *= factor * cr;
            bloomG *= factor * cg;
            bloomB *= factor * cb;

            const qreal rOut = std::min(255.0, (r + bloomR * 255.0));
            const qreal gOut = std::min(255.0, (g + bloomG * 255.0));
            const qreal bOut = std::min(255.0, (b + bloomB * 255.0));

            *dst++ = static_cast<uchar>(rOut);
            *dst++ = static_cast<uchar>(gOut);
            *dst++ = static_cast<uchar>(bOut);
            *dst++ = a;
        }
    }
}
