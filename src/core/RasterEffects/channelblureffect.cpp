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

#include "channelblureffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

ChannelBlurEffect::ChannelBlurEffect() :
    RasterEffect("channel blur",
                 AppSupport::getRasterEffectHardwareSupport("Channel Blur",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::CHANNEL_BLUR)
{
    mRedRadius = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 0.5, "red radius");
    ca_addChild(mRedRadius);

    mGreenRadius = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 0.5, "green radius");
    ca_addChild(mGreenRadius);

    mBlueRadius = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 0.5, "blue radius");
    ca_addChild(mBlueRadius);
}

class ChannelBlurEffectCaller : public OpenGLRasterEffectCaller {
public:
    ChannelBlurEffectCaller(const HardwareSupport hwSupport,
                            const qreal rRad,
                            const qreal gRad,
                            const qreal bRad) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/channelblureffect.frag",
                                 hwSupport),
        mRRad(rRad),
        mGRad(gRad),
        mBRad(bRad) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sRRadU = gl->glGetUniformLocation(sProgramId, "rRad");
        sGRadU = gl->glGetUniformLocation(sProgramId, "gRad");
        sBRadU = gl->glGetUniformLocation(sProgramId, "bRad");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sRRadU, toSkScalar(mRRad));
        gl->glUniform1f(sGRadU, toSkScalar(mGRad));
        gl->glUniform1f(sBRadU, toSkScalar(mBRad));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sRRadU;
    static GLint sGRadU;
    static GLint sBRadU;

    const qreal mRRad;
    const qreal mGRad;
    const qreal mBRad;
};

bool ChannelBlurEffectCaller::sInitialized = false;
GLuint ChannelBlurEffectCaller::sProgramId = 0;

GLint ChannelBlurEffectCaller::sRRadU = -1;
GLint ChannelBlurEffectCaller::sGRadU = -1;
GLint ChannelBlurEffectCaller::sBRadU = -1;

stdsptr<RasterEffectCaller> ChannelBlurEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal rRad = mRedRadius->getEffectiveValue(relFrame) * influence;
    const qreal gRad = mGreenRadius->getEffectiveValue(relFrame) * influence;
    const qreal bRad = mBlueRadius->getEffectiveValue(relFrame) * influence;

    return enve::make_shared<ChannelBlurEffectCaller>(
                instanceHwSupport(), rRad, gRad, bRad);
}

void ChannelBlurEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int rRadius = qRound(mRRad);
    const int gRadius = qRound(mGRad);
    const int bRadius = qRound(mBRad);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));

        for(int xi = xMin; xi <= xMax; xi++) {
            const auto srcCenter = static_cast<const uchar*>(srcBtmp.getAddr(xi, yi));

            // R channel blur
            int rSum = 0, rCount = 0;
            if (rRadius > 0) {
                for (int ky = -rRadius; ky <= rRadius; ky += std::max(1, rRadius / 3)) {
                    const int sy = std::max(0, std::min(imgHeight - 1, yi + ky));
                    for (int kx = -rRadius; kx <= rRadius; kx += std::max(1, rRadius / 3)) {
                        const int sx = std::max(0, std::min(imgWidth - 1, xi + kx));
                        const auto s = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));
                        rSum += s[0];
                        rCount++;
                    }
                }
            } else {
                rSum = srcCenter[0];
                rCount = 1;
            }

            // G channel blur
            int gSum = 0, gCount = 0;
            if (gRadius > 0) {
                for (int ky = -gRadius; ky <= gRadius; ky += std::max(1, gRadius / 3)) {
                    const int sy = std::max(0, std::min(imgHeight - 1, yi + ky));
                    for (int kx = -gRadius; kx <= gRadius; kx += std::max(1, gRadius / 3)) {
                        const int sx = std::max(0, std::min(imgWidth - 1, xi + kx));
                        const auto s = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));
                        gSum += s[1];
                        gCount++;
                    }
                }
            } else {
                gSum = srcCenter[1];
                gCount = 1;
            }

            // B channel blur
            int bSum = 0, bCount = 0;
            if (bRadius > 0) {
                for (int ky = -bRadius; ky <= bRadius; ky += std::max(1, bRadius / 3)) {
                    const int sy = std::max(0, std::min(imgHeight - 1, yi + ky));
                    for (int kx = -bRadius; kx <= bRadius; kx += std::max(1, bRadius / 3)) {
                        const int sx = std::max(0, std::min(imgWidth - 1, xi + kx));
                        const auto s = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));
                        bSum += s[2];
                        bCount++;
                    }
                }
            } else {
                bSum = srcCenter[2];
                bCount = 1;
            }

            *dst++ = static_cast<uchar>(rSum / std::max(1, rCount));
            *dst++ = static_cast<uchar>(gSum / std::max(1, gCount));
            *dst++ = static_cast<uchar>(bSum / std::max(1, bCount));
            *dst++ = srcCenter[3];
        }
    }
}
