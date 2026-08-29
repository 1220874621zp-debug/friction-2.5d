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

#include "mirroreffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

MirrorEffect::MirrorEffect() :
    RasterEffect("mirror",
                 AppSupport::getRasterEffectHardwareSupport("Mirror",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::MIRROR)
{
    mCenterX = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "center X");
    ca_addChild(mCenterX);

    mCenterY = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "center Y");
    ca_addChild(mCenterY);

    mMirrorX = enve::make_shared<QrealAnimator>(1.0, 0.0, 1.0, 1.0, "mirror horizontal");
    ca_addChild(mMirrorX);

    mMirrorY = enve::make_shared<QrealAnimator>(0.0, 0.0, 1.0, 1.0, "mirror vertical");
    ca_addChild(mMirrorY);
}

class MirrorEffectCaller : public OpenGLRasterEffectCaller {
public:
    MirrorEffectCaller(const HardwareSupport hwSupport,
                       const qreal centerX,
                       const qreal centerY,
                       const qreal mirrorX,
                       const qreal mirrorY) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/mirroreffect.frag",
                                 hwSupport),
        mCenterX(centerX),
        mCenterY(centerY),
        mMirrorX(mirrorX),
        mMirrorY(mirrorY) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sCenterU = gl->glGetUniformLocation(sProgramId, "center");
        sMirrorXU = gl->glGetUniformLocation(sProgramId, "mirrorX");
        sMirrorYU = gl->glGetUniformLocation(sProgramId, "mirrorY");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sCenterU, toSkScalar(mCenterX), toSkScalar(mCenterY));
        gl->glUniform1f(sMirrorXU, toSkScalar(mMirrorX));
        gl->glUniform1f(sMirrorYU, toSkScalar(mMirrorY));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sCenterU;
    static GLint sMirrorXU;
    static GLint sMirrorYU;

    const qreal mCenterX;
    const qreal mCenterY;
    const qreal mMirrorX;
    const qreal mMirrorY;
};

bool MirrorEffectCaller::sInitialized = false;
GLuint MirrorEffectCaller::sProgramId = 0;

GLint MirrorEffectCaller::sCenterU = -1;
GLint MirrorEffectCaller::sMirrorXU = -1;
GLint MirrorEffectCaller::sMirrorYU = -1;

stdsptr<RasterEffectCaller> MirrorEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)
    Q_UNUSED(influence)

    const qreal cx = mCenterX->getEffectiveValue(relFrame);
    const qreal cy = mCenterY->getEffectiveValue(relFrame);
    const qreal mx = mMirrorX->getEffectiveValue(relFrame);
    const qreal my = mMirrorY->getEffectiveValue(relFrame);

    return enve::make_shared<MirrorEffectCaller>(
                instanceHwSupport(), cx, cy, mx, my);
}

void MirrorEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int cx = qRound(mCenterX * imgWidth);
    const int cy = qRound(mCenterY * imgHeight);
    const bool mx = mMirrorX > 0.5;
    const bool my = mMirrorY > 0.5;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const int sy = my ? std::max(0, std::min(imgHeight - 1, cy - std::abs(yi - cy))) : yi;

        for(int xi = xMin; xi <= xMax; xi++) {
            const int sx = mx ? std::max(0, std::min(imgWidth - 1, cx - std::abs(xi - cx))) : xi;
            const auto src = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));

            *dst++ = src[0];
            *dst++ = src[1];
            *dst++ = src[2];
            *dst++ = src[3];
        }
    }
}
