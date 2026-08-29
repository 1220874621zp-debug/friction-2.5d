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

#include "radialblureffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

RadialBlurEffect::RadialBlurEffect() :
    RasterEffect("radial blur",
                 AppSupport::getRasterEffectHardwareSupport("RadialBlur",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::RADIAL_BLUR)
{
    mAmount = enve::make_shared<QrealAnimator>(15.0, 0.0, 100.0, 0.5, "amount");
    ca_addChild(mAmount);

    mCenterX = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "center X");
    ca_addChild(mCenterX);

    mCenterY = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "center Y");
    ca_addChild(mCenterY);
}

class RadialBlurEffectCaller : public OpenGLRasterEffectCaller {
public:
    RadialBlurEffectCaller(const HardwareSupport hwSupport,
                          const qreal amount,
                          const qreal centerX,
                          const qreal centerY) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/radialblureffect.frag",
                                 hwSupport),
        mAmount(amount),
        mCenterX(centerX),
        mCenterY(centerY) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
        sCenterU = gl->glGetUniformLocation(sProgramId, "center");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sAmountU, toSkScalar(mAmount / 100.0));
        gl->glUniform2f(sCenterU, toSkScalar(mCenterX), toSkScalar(mCenterY));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;
    static GLint sCenterU;

    const qreal mAmount;
    const qreal mCenterX;
    const qreal mCenterY;
};

bool RadialBlurEffectCaller::sInitialized = false;
GLuint RadialBlurEffectCaller::sProgramId = 0;

GLint RadialBlurEffectCaller::sAmountU = -1;
GLint RadialBlurEffectCaller::sCenterU = -1;

stdsptr<RasterEffectCaller> RadialBlurEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amount = mAmount->getEffectiveValue(relFrame) * influence;
    const qreal cx = mCenterX->getEffectiveValue(relFrame);
    const qreal cy = mCenterY->getEffectiveValue(relFrame);

    return enve::make_shared<RadialBlurEffectCaller>(
                instanceHwSupport(), amount, cx, cy);
}

void RadialBlurEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal cx = mCenterX * imgWidth;
    const qreal cy = mCenterY * imgHeight;
    const qreal amt = mAmount / 100.0;
    const int SAMPLES = 8;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal dx = xi - cx;
            const qreal dy = yi - cy;
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            for(int s = 0; s < SAMPLES; s++) {
                const qreal scale = 1.0 - amt * (qreal(s) / (SAMPLES - 1));
                const int sx = std::max(0, std::min(imgWidth - 1, qRound(cx + dx * scale)));
                const int sy = std::max(0, std::min(imgHeight - 1, qRound(cy + dy * scale)));
                const auto src = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));
                sumR += src[0];
                sumG += src[1];
                sumB += src[2];
                sumA += src[3];
            }
            *dst++ = static_cast<uchar>(sumR / SAMPLES);
            *dst++ = static_cast<uchar>(sumG / SAMPLES);
            *dst++ = static_cast<uchar>(sumB / SAMPLES);
            *dst++ = static_cast<uchar>(sumA / SAMPLES);
        }
    }
}
