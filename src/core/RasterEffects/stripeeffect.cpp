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

#include "stripeeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>

StripeEffect::StripeEffect() :
    RasterEffect(QObject::tr("Stripe"),
                 AppSupport::getRasterEffectHardwareSupport("Stripe",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::STRIPE)
{
    mTransition = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "transition");
    ca_addChild(mTransition);

    mWidth = enve::make_shared<QrealAnimator>(20.0, 1.0, 200.0, 1.0, "width");
    ca_addChild(mWidth);

    mAngle = enve::make_shared<QrealAnimator>(45.0, -180.0, 180.0, 1.0, "angle");
    ca_addChild(mAngle);

    mFeather = enve::make_shared<QrealAnimator>(10.0, 0.0, 100.0, 1.0, "feather");
    ca_addChild(mFeather);
}

class StripeEffectCaller : public OpenGLRasterEffectCaller {
public:
    StripeEffectCaller(const HardwareSupport hwSupport,
                       const qreal transition,
                       const qreal width,
                       const qreal angle,
                       const qreal feather) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/stripeeffect.frag",
                                 hwSupport),
        mTransition(transition),
        mWidth(width),
        mAngle(angle),
        mFeather(feather) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sTransitionU = gl->glGetUniformLocation(sProgramId, "transition");
        sWidthU = gl->glGetUniformLocation(sProgramId, "width");
        sAngleU = gl->glGetUniformLocation(sProgramId, "angle");
        sFeatherU = gl->glGetUniformLocation(sProgramId, "feather");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sTransitionU, toSkScalar(mTransition));
        gl->glUniform1f(sWidthU, toSkScalar(mWidth));
        gl->glUniform1f(sAngleU, toSkScalar(mAngle));
        gl->glUniform1f(sFeatherU, toSkScalar(mFeather));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sTransitionU;
    static GLint sWidthU;
    static GLint sAngleU;
    static GLint sFeatherU;

    const qreal mTransition;
    const qreal mWidth;
    const qreal mAngle;
    const qreal mFeather;
};

bool StripeEffectCaller::sInitialized = false;
GLuint StripeEffectCaller::sProgramId = 0;

GLint StripeEffectCaller::sTransitionU = -1;
GLint StripeEffectCaller::sWidthU = -1;
GLint StripeEffectCaller::sAngleU = -1;
GLint StripeEffectCaller::sFeatherU = -1;

stdsptr<RasterEffectCaller> StripeEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal transition = (mTransition->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal width = mWidth->getEffectiveValue(relFrame);
    const qreal angle = mAngle->getEffectiveValue(relFrame);
    const qreal feather = (mFeather->getEffectiveValue(relFrame) / 100.0);

    return enve::make_shared<StripeEffectCaller>(
                instanceHwSupport(), transition, width, angle, feather);
}

void StripeEffectCaller::processCpu(CpuRenderTools& renderTools,
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
    const qreal dirX = std::cos(rad);
    const qreal dirY = std::sin(rad);
    const qreal edge = 1.0 - mTransition;

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal ny = qreal(yi) / imgHeight;

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal nx = qreal(xi) / imgWidth;
            const qreal proj = nx * dirX + ny * dirY;
            const qreal stripeVal = proj * mWidth;
            const qreal stripePos = stripeVal - std::floor(stripeVal);

            const qreal mult = (stripePos > edge) ? 0.0 : 1.0;

            *dst++ = r;
            *dst++ = g;
            *dst++ = b;
            *dst++ = static_cast<uchar>(a * mult);
        }
    }
}
