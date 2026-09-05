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

#include "pixelateeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

PixelateEffect::PixelateEffect() :
    RasterEffect(QObject::tr("Pixelate"),
                 AppSupport::getRasterEffectHardwareSupport("Pixelate",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::PIXELATE)
{
    mBlockSize = enve::make_shared<QrealAnimator>(10.0, 1.0, 100.0, 1.0, "block size");
    ca_addChild(mBlockSize);
}

class PixelateEffectCaller : public OpenGLRasterEffectCaller {
public:
    PixelateEffectCaller(const HardwareSupport hwSupport,
                         const qreal blockSize) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/pixelateeffect.frag",
                                 hwSupport),
        mBlockSize(blockSize) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sPixelStepU = gl->glGetUniformLocation(sProgramId, "pixelStep");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sPixelStepU,
                        toSkScalar(mBlockSize / 1000.0),
                        toSkScalar(mBlockSize / 1000.0));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sPixelStepU;

    const qreal mBlockSize;
};

bool PixelateEffectCaller::sInitialized = false;
GLuint PixelateEffectCaller::sProgramId = 0;

GLint PixelateEffectCaller::sPixelStepU = -1;

stdsptr<RasterEffectCaller> PixelateEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal rawSize = mBlockSize->getEffectiveValue(relFrame);
    const qreal blockSize = 1.0 + (rawSize - 1.0) * influence;

    return enve::make_shared<PixelateEffectCaller>(
                instanceHwSupport(), blockSize);
}

void PixelateEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int block = std::max(1, qRound(mBlockSize));

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const int by = (yi / block) * block + block / 2;
        const int sy = std::max(0, std::min(imgHeight - 1, by));

        for(int xi = xMin; xi <= xMax; xi++) {
            const int bx = (xi / block) * block + block / 2;
            const int sx = std::max(0, std::min(imgWidth - 1, bx));
            const auto src = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));

            *dst++ = src[0];
            *dst++ = src[1];
            *dst++ = src[2];
            *dst++ = src[3];
        }
    }
}
