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

#include "inverteffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

InvertEffect::InvertEffect() :
    RasterEffect("invert",
                 AppSupport::getRasterEffectHardwareSupport("Invert",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::INVERT)
{
    mAmount = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0, "amount");
    ca_addChild(mAmount);
}

class InvertEffectCaller : public OpenGLRasterEffectCaller {
public:
    InvertEffectCaller(const HardwareSupport hwSupport,
                       const qreal amount) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/inverteffect.frag",
                                 hwSupport),
        mAmount(amount) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sAmountU, toSkScalar(mAmount));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;

    const qreal mAmount;
};

bool InvertEffectCaller::sInitialized = false;
GLuint InvertEffectCaller::sProgramId = 0;

GLint InvertEffectCaller::sAmountU = -1;

stdsptr<RasterEffectCaller> InvertEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amount = (mAmount->getEffectiveValue(relFrame) / 100.0) * influence;

    return enve::make_shared<InvertEffectCaller>(
                instanceHwSupport(), amount);
}

void InvertEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal rInv = 255 - r;
            const qreal gInv = 255 - g;
            const qreal bInv = 255 - b;

            *dst++ = static_cast<uchar>(r * (1.0 - mAmount) + rInv * mAmount);
            *dst++ = static_cast<uchar>(g * (1.0 - mAmount) + gInv * mAmount);
            *dst++ = static_cast<uchar>(b * (1.0 - mAmount) + bInv * mAmount);
            *dst++ = a;
        }
    }
}
