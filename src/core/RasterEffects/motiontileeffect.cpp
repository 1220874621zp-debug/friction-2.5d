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

#include "motiontileeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "Animators/boolanimator.h"
#include "appsupport.h"
#include <QtMath>

MotionTileEffect::MotionTileEffect() :
    RasterEffect(QObject::tr("Motion Tile"),
                 AppSupport::getRasterEffectHardwareSupport("Motion Tile",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::MOTION_TILE)
{
    mTileCountX = enve::make_shared<QrealAnimator>(3.0, 1.0, 50.0, 1.0, "tileCountX");
    ca_addChild(mTileCountX);

    mTileCountY = enve::make_shared<QrealAnimator>(3.0, 1.0, 50.0, 1.0, "tileCountY");
    ca_addChild(mTileCountY);

    mOffsetX = enve::make_shared<QrealAnimator>(0.0, -500.0, 500.0, 1.0, "offsetX");
    ca_addChild(mOffsetX);

    mOffsetY = enve::make_shared<QrealAnimator>(0.0, -500.0, 500.0, 1.0, "offsetY");
    ca_addChild(mOffsetY);

    mMirrorEdges = enve::make_shared<BoolAnimator>("mirrorEdges");
    mMirrorEdges->setCurrentBoolValue(true);
    ca_addChild(mMirrorEdges);
}

class MotionTileEffectCaller : public OpenGLRasterEffectCaller {
public:
    MotionTileEffectCaller(const HardwareSupport hwSupport,
                           const qreal tileCountX,
                           const qreal tileCountY,
                           const QPointF& offset,
                           const bool mirrorEdges) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/motiontileeffect.frag",
                                 hwSupport),
        mTileCountX(tileCountX),
        mTileCountY(tileCountY),
        mOffset(offset),
        mMirrorEdges(mirrorEdges) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sTileCountXU = gl->glGetUniformLocation(sProgramId, "tileCountX");
        sTileCountYU = gl->glGetUniformLocation(sProgramId, "tileCountY");
        sOffsetU = gl->glGetUniformLocation(sProgramId, "offset");
        sMirrorEdgesU = gl->glGetUniformLocation(sProgramId, "mirrorEdges");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sTileCountXU, toSkScalar(mTileCountX));
        gl->glUniform1f(sTileCountYU, toSkScalar(mTileCountY));
        gl->glUniform2f(sOffsetU, toSkScalar(mOffset.x()), toSkScalar(mOffset.y()));
        gl->glUniform1i(sMirrorEdgesU, mMirrorEdges ? 1 : 0);
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sTileCountXU;
    static GLint sTileCountYU;
    static GLint sOffsetU;
    static GLint sMirrorEdgesU;

    const qreal mTileCountX;
    const qreal mTileCountY;
    const QPointF mOffset;
    const bool mMirrorEdges;
};

bool MotionTileEffectCaller::sInitialized = false;
GLuint MotionTileEffectCaller::sProgramId = 0;

GLint MotionTileEffectCaller::sTileCountXU = -1;
GLint MotionTileEffectCaller::sTileCountYU = -1;
GLint MotionTileEffectCaller::sOffsetU = -1;
GLint MotionTileEffectCaller::sMirrorEdgesU = -1;

stdsptr<RasterEffectCaller> MotionTileEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal tileCountX = mTileCountX->getEffectiveValue(relFrame);
    const qreal tileCountY = mTileCountY->getEffectiveValue(relFrame);
    const qreal offX = (mOffsetX->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal offY = (mOffsetY->getEffectiveValue(relFrame) / 100.0) * influence;
    const bool mirror = mMirrorEdges->getBoolValue(relFrame);

    return enve::make_shared<MotionTileEffectCaller>(
                instanceHwSupport(), tileCountX, tileCountY, QPointF(offX, offY), mirror);
}

void MotionTileEffectCaller::processCpu(CpuRenderTools& renderTools,
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
        const qreal ny = (qreal(yi) / imgHeight - mOffset.y()) * mTileCountY;
        const qreal cellY = std::floor(ny);
        qreal fracY = ny - cellY;
        if (mMirrorEdges && int(std::abs(cellY)) % 2 == 1) {
            fracY = 1.0 - fracY;
        }
        const int sy = std::max(0, std::min(imgHeight - 1, int(std::round(fracY * (imgHeight - 1)))));

        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal nx = (qreal(xi) / imgWidth - mOffset.x()) * mTileCountX;
            const qreal cellX = std::floor(nx);
            qreal fracX = nx - cellX;
            if (mMirrorEdges && int(std::abs(cellX)) % 2 == 1) {
                fracX = 1.0 - fracX;
            }
            const int sx = std::max(0, std::min(imgWidth - 1, int(std::round(fracX * (imgWidth - 1)))));

            const auto src = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));
            *dst++ = src[0];
            *dst++ = src[1];
            *dst++ = src[2];
            *dst++ = src[3];
        }
    }
}
