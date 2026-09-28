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

#include "thresholdeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

ThresholdEffect::ThresholdEffect() :
    RasterEffect(QObject::tr("阈值 (Threshold)"),
                 AppSupport::getRasterEffectHardwareSupport("Threshold",
                                                             HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::THRESHOLD)
{
    // AE semantics: level is a percent of luminance; pixels at or above
    // it turn white, pixels below turn black (level 0 = all white,
    // level 100 = only pure-white pixels survive white)
    mLevel = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "level");
    ca_addChild(mLevel);
}

class ThresholdEffectCaller : public OpenGLRasterEffectCaller {
public:
    ThresholdEffectCaller(const HardwareSupport hwSupport,
                          const qreal level) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/thresholdeffect.frag",
                                 hwSupport),
        mLevel(level) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sLevelU = gl->glGetUniformLocation(sProgramId, "level");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sLevelU, toSkScalar(mLevel));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sLevelU;

    const qreal mLevel;
};

bool ThresholdEffectCaller::sInitialized = false;
GLuint ThresholdEffectCaller::sProgramId = 0;

GLint ThresholdEffectCaller::sLevelU = -1;

stdsptr<RasterEffectCaller> ThresholdEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(resolution)
    Q_UNUSED(influence)
    Q_UNUSED(data)

    const qreal level = qBound(0.0, mLevel->getEffectiveValue(relFrame), 100.0);

    return enve::make_shared<ThresholdEffectCaller>(
                instanceHwSupport(), level);
}

void ThresholdEffectCaller::processCpu(CpuRenderTools& renderTools,
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
    const int xMax = std::min((int)data.fTexTile.right() - 1, imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom() - 1, imgHeight - 1);

    // Rec.601 luminance, the curve AE's Threshold binarizes against;
    // kN32 is little-endian BGRA in memory: byte 0 = B, 1 = G, 2 = R
    const qreal t = mLevel / 100.0 * 255.0;

    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for (int xi = xMin; xi <= xMax; xi++) {
            const qreal b = *src++;
            const qreal g = *src++;
            const qreal r = *src++;
            const uchar a = *src++;

            const qreal lum = 0.114 * b + 0.587 * g + 0.299 * r;
            const uchar v = lum >= t ? 255 : 0;

            *dst++ = v;
            *dst++ = v;
            *dst++ = v;
            *dst++ = a;
        }
    }
}
