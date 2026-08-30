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

#include "twirleffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

TwirlEffect::TwirlEffect() :
    RasterEffect("twirl",
                 AppSupport::getRasterEffectHardwareSupport("Twirl",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::TWIRL)
{
    mCenterX = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "center X");
    ca_addChild(mCenterX);

    mCenterY = enve::make_shared<QrealAnimator>(0.5, 0.0, 1.0, 0.01, "center Y");
    ca_addChild(mCenterY);

    mRadius = enve::make_shared<QrealAnimator>(0.5, 0.01, 1.0, 0.01, "radius");
    ca_addChild(mRadius);

    mAngle = enve::make_shared<QrealAnimator>(180.0, -720.0, 720.0, 1.0, "angle");
    ca_addChild(mAngle);
}

class TwirlEffectCaller : public OpenGLRasterEffectCaller {
public:
    TwirlEffectCaller(const HardwareSupport hwSupport,
                      const qreal centerX,
                      const qreal centerY,
                      const qreal radius,
                      const qreal angleRad) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/twirleffect.frag",
                                 hwSupport),
        mCenterX(centerX),
        mCenterY(centerY),
        mRadius(radius),
        mAngleRad(angleRad) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sCenterU = gl->glGetUniformLocation(sProgramId, "center");
        sRadiusU = gl->glGetUniformLocation(sProgramId, "radius");
        sAngleU = gl->glGetUniformLocation(sProgramId, "angle");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sCenterU, toSkScalar(mCenterX), toSkScalar(mCenterY));
        gl->glUniform1f(sRadiusU, toSkScalar(mRadius));
        gl->glUniform1f(sAngleU, toSkScalar(mAngleRad));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sCenterU;
    static GLint sRadiusU;
    static GLint sAngleU;

    const qreal mCenterX;
    const qreal mCenterY;
    const qreal mRadius;
    const qreal mAngleRad;
};

bool TwirlEffectCaller::sInitialized = false;
GLuint TwirlEffectCaller::sProgramId = 0;

GLint TwirlEffectCaller::sCenterU = -1;
GLint TwirlEffectCaller::sRadiusU = -1;
GLint TwirlEffectCaller::sAngleU = -1;

stdsptr<RasterEffectCaller> TwirlEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal cx = mCenterX->getEffectiveValue(relFrame);
    const qreal cy = mCenterY->getEffectiveValue(relFrame);
    const qreal rad = mRadius->getEffectiveValue(relFrame);
    const qreal deg = mAngle->getEffectiveValue(relFrame) * influence;
    const qreal radAngle = qDegreesToRadians(deg);

    return enve::make_shared<TwirlEffectCaller>(
                instanceHwSupport(), cx, cy, rad, radAngle);
}

void TwirlEffectCaller::processCpu(CpuRenderTools& renderTools,
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
    const qreal rPx = mRadius * std::min(imgWidth, imgHeight);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));

        for(int xi = xMin; xi <= xMax; xi++) {
            qreal dx = xi - cx;
            qreal dy = yi - cy;
            const qreal dist = std::sqrt(dx * dx + dy * dy);

            if (dist < rPx && rPx > 0.0001) {
                const qreal percent = (rPx - dist) / rPx;
                const qreal theta = percent * percent * mAngleRad;
                const qreal s = std::sin(theta);
                const qreal c = std::cos(theta);
                const qreal rx = dx * c - dy * s;
                const qreal ry = dx * s + dy * c;
                dx = rx;
                dy = ry;
            }

            const int sx = std::max(0, std::min(imgWidth - 1, qRound(cx + dx)));
            const int sy = std::max(0, std::min(imgHeight - 1, qRound(cy + dy)));
            const auto src = static_cast<const uchar*>(srcBtmp.getAddr(sx, sy));

            *dst++ = src[0];
            *dst++ = src[1];
            *dst++ = src[2];
            *dst++ = src[3];
        }
    }
}
