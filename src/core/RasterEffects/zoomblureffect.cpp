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

#include "zoomblureffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "Animators/qpointfanimator.h"
#include "appsupport.h"

ZoomBlurEffect::ZoomBlurEffect() :
    RasterEffect("zoom blur",
                 AppSupport::getRasterEffectHardwareSupport("Zoom Blur",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::ZOOM_BLUR)
{
    mAmount = enve::make_shared<QrealAnimator>(25.0, 0.0, 100.0, 0.5, "amount");
    ca_addChild(mAmount);

    mCenter = enve::make_shared<QPointFAnimator>("center");
    mCenter->setBaseValue(0.5, 0.5);
    ca_addChild(mCenter);
}

class ZoomBlurEffectCaller : public OpenGLRasterEffectCaller {
public:
    ZoomBlurEffectCaller(const HardwareSupport hwSupport,
                         const qreal amount,
                         const QPointF& center,
                         const QMargins& margins) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/zoomblureffect.frag",
                                 hwSupport,
                                 true,
                                 margins),
        mAmount(amount),
        mCenter(center) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
        sCenterU = gl->glGetUniformLocation(sProgramId, "center");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sAmountU, toSkScalar(mAmount));
        gl->glUniform2f(sCenterU, toSkScalar(mCenter.x()), toSkScalar(mCenter.y()));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;
    static GLint sCenterU;

    const qreal mAmount;
    const QPointF mCenter;
};

bool ZoomBlurEffectCaller::sInitialized = false;
GLuint ZoomBlurEffectCaller::sProgramId = 0;

GLint ZoomBlurEffectCaller::sAmountU = -1;
GLint ZoomBlurEffectCaller::sCenterU = -1;

stdsptr<RasterEffectCaller> ZoomBlurEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amount = (mAmount->getEffectiveValue(relFrame) / 100.0) * influence;
    const QPointF center = mCenter->getEffectiveValue(relFrame);
    const int m = qCeil(amount * 50.0);

    return enve::make_shared<ZoomBlurEffectCaller>(
                instanceHwSupport(), amount, center, QMargins(m, m, m, m));
}

void ZoomBlurEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int SAMPLES = 20;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const qreal ny = qreal(yi) / imgHeight;
        const qreal toCenterY = mCenter.y() - ny;

        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal nx = qreal(xi) / imgWidth;
            const qreal toCenterX = mCenter.x() - nx;

            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            for(int s = 0; s < SAMPLES; s++) {
                const qreal scale = 1.0 + mAmount * (qreal(s) / (SAMPLES - 1));
                const qreal sxNorm = mCenter.x() - toCenterX * scale;
                const qreal syNorm = mCenter.y() - toCenterY * scale;

                const int sx = std::max(0, std::min(imgWidth - 1, int(std::round(sxNorm * imgWidth))));
                const int sy = std::max(0, std::min(imgHeight - 1, int(std::round(syNorm * imgHeight))));
                const auto smp = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));

                sumR += smp[0];
                sumG += smp[1];
                sumB += smp[2];
                sumA += smp[3];
            }

            *dst++ = static_cast<uchar>(sumR / SAMPLES);
            *dst++ = static_cast<uchar>(sumG / SAMPLES);
            *dst++ = static_cast<uchar>(sumB / SAMPLES);
            *dst++ = static_cast<uchar>(sumA / SAMPLES);
        }
    }
}
