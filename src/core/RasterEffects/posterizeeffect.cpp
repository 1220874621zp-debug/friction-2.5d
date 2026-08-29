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

#include "posterizeeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

PosterizeEffect::PosterizeEffect() :
    RasterEffect("posterize",
                 AppSupport::getRasterEffectHardwareSupport("Posterize",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::POSTERIZE)
{
    mLevels = enve::make_shared<QrealAnimator>(4.0, 2.0, 32.0, 1.0, "levels");
    ca_addChild(mLevels);
}

class PosterizeEffectCaller : public OpenGLRasterEffectCaller {
public:
    PosterizeEffectCaller(const HardwareSupport hwSupport,
                          const qreal levels) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/posterizeeffect.frag",
                                 hwSupport),
        mLevels(levels) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sLevelsU = gl->glGetUniformLocation(sProgramId, "levels");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sLevelsU, toSkScalar(mLevels));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sLevelsU;

    const qreal mLevels;
};

bool PosterizeEffectCaller::sInitialized = false;
GLuint PosterizeEffectCaller::sProgramId = 0;

GLint PosterizeEffectCaller::sLevelsU = -1;

stdsptr<RasterEffectCaller> PosterizeEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)
    Q_UNUSED(influence)

    const qreal levels = std::max(2.0, mLevels->getEffectiveValue(relFrame));

    return enve::make_shared<PosterizeEffectCaller>(
                instanceHwSupport(), levels);
}

void PosterizeEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal n = std::max(2.0, mLevels);
    const qreal step = 255.0 / (n - 1.0);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            *dst++ = static_cast<uchar>(std::min(255.0, std::round(r / step) * step));
            *dst++ = static_cast<uchar>(std::min(255.0, std::round(g / step) * step));
            *dst++ = static_cast<uchar>(std::min(255.0, std::round(b / step) * step));
            *dst++ = a;
        }
    }
}
