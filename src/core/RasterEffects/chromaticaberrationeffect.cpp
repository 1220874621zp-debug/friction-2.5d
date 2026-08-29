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

#include "chromaticaberrationeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

ChromaticAberrationEffect::ChromaticAberrationEffect() :
    RasterEffect("chromatic aberration",
                 AppSupport::getRasterEffectHardwareSupport("ChromaticAberration",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::CHROMATIC_ABERRATION)
{
    mAmount = enve::make_shared<QrealAnimator>(5.0, 0.0, 100.0, 0.5, "amount");
    ca_addChild(mAmount);

    mAngle = enve::make_shared<QrealAnimator>(0.0, -360.0, 360.0, 1.0, "angle");
    ca_addChild(mAngle);
}

class ChromaticAberrationEffectCaller : public OpenGLRasterEffectCaller {
public:
    ChromaticAberrationEffectCaller(const HardwareSupport hwSupport,
                                   const qreal amount,
                                   const qreal angle) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/chromaticaberrationeffect.frag",
                                 hwSupport),
        mAmount(amount),
        mAngle(angle) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sOffsetU = gl->glGetUniformLocation(sProgramId, "offset");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        const qreal rad = qDegreesToRadians(mAngle);
        const qreal dx = std::cos(rad) * mAmount;
        const qreal dy = std::sin(rad) * mAmount;
        gl->glUniform2f(sOffsetU,
                        toSkScalar(dx / 1000.0),
                        toSkScalar(dy / 1000.0));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sOffsetU;

    const qreal mAmount;
    const qreal mAngle;
};

bool ChromaticAberrationEffectCaller::sInitialized = false;
GLuint ChromaticAberrationEffectCaller::sProgramId = 0;

GLint ChromaticAberrationEffectCaller::sOffsetU = -1;

stdsptr<RasterEffectCaller> ChromaticAberrationEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal amount = mAmount->getEffectiveValue(relFrame) * influence;
    const qreal angle = mAngle->getEffectiveValue(relFrame);

    return enve::make_shared<ChromaticAberrationEffectCaller>(
                instanceHwSupport(), amount, angle);
}

void ChromaticAberrationEffectCaller::processCpu(CpuRenderTools& renderTools,
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
    const int dx = qRound(std::cos(rad) * mAmount);
    const int dy = qRound(std::sin(rad) * mAmount);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        for(int xi = xMin; xi <= xMax; xi++) {
            const int rx = std::max(0, std::min(imgWidth - 1, xi - dx));
            const int ry = std::max(0, std::min(imgHeight - 1, yi - dy));
            const auto srcR = static_cast<const uchar*>(srcBtmp.getAddr(rx, ry));

            const auto srcG = static_cast<const uchar*>(srcBtmp.getAddr(xi, yi));

            const int bx = std::max(0, std::min(imgWidth - 1, xi + dx));
            const int by = std::max(0, std::min(imgHeight - 1, yi + dy));
            const auto srcB = static_cast<const uchar*>(srcBtmp.getAddr(bx, by));

            const uchar r = srcR[0];
            const uchar g = srcG[1];
            const uchar b = srcB[2];
            const uchar a = std::max(srcG[3], std::max(srcR[3], srcB[3]));

            *dst++ = r;
            *dst++ = g;
            *dst++ = b;
            *dst++ = a;
        }
    }
}
