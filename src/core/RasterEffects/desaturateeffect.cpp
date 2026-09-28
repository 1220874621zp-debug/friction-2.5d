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

#include "desaturateeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "Animators/boolanimator.h"
#include "appsupport.h"

DesaturateEffect::DesaturateEffect() :
    RasterEffect(QObject::tr("去色 (Desaturate)"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "Desaturate", HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::DESATURATE)
{
    mAmount = enve::make_shared<QrealAnimator>(
                100.0, 0.0, 100.0, 1.0, "amount");
    ca_addChild(mAmount);

    mInvert = enve::make_shared<BoolAnimator>("invert");
    mInvert->setCurrentBoolValue(false);
    ca_addChild(mInvert);
}

class DesaturateEffectCaller : public OpenGLRasterEffectCaller {
public:
    DesaturateEffectCaller(const HardwareSupport hwSupport,
                           const qreal amount, const bool invert) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/desaturateeffect.frag",
                                 hwSupport),
        mAmount(amount), mInvert(invert) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
        sInvertU = gl->glGetUniformLocation(sProgramId, "invert");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sAmountU, toSkScalar(mAmount / 100.0));
        gl->glUniform1i(sInvertU, mInvert ? 1 : 0);
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;
    static GLint sInvertU;

    const qreal mAmount;
    const bool mInvert;
};

bool DesaturateEffectCaller::sInitialized = false;
GLuint DesaturateEffectCaller::sProgramId = 0;

GLint DesaturateEffectCaller::sAmountU = -1;
GLint DesaturateEffectCaller::sInvertU = -1;

stdsptr<RasterEffectCaller> DesaturateEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(resolution)
    Q_UNUSED(influence)
    Q_UNUSED(data)

    const qreal amount = qBound(0.0,
            mAmount->getEffectiveValue(relFrame), 100.0);
    const bool invert = mInvert->getBoolValue(relFrame);

    return enve::make_shared<DesaturateEffectCaller>(
                instanceHwSupport(), amount, invert);
}

void DesaturateEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    // Rec.601 luminance, the same curve as Threshold; kN32 is
    // little-endian BGRA in memory: byte 0 = B, 1 = G, 2 = R.
    // Normal: blend toward the shared luminance (colors die, values
    // survive). Invert: blend toward channel-minus-minimum, the pure
    // chroma component (values die, colors survive)
    const qreal t = mAmount / 100.0;
    const qreal it = 1.0 - t;

    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for (int xi = xMin; xi <= xMax; xi++) {
            const qreal b = *src++;
            const qreal g = *src++;
            const qreal r = *src++;
            const uchar a = *src++;

            qreal tb, tg, tr;
            if (mInvert) {
                const qreal m = std::min(b, std::min(g, r));
                tb = b - m; tg = g - m; tr = r - m;
            } else {
                const qreal gray = 0.299 * r + 0.587 * g + 0.114 * b;
                tb = gray; tg = gray; tr = gray;
            }

            *dst++ = static_cast<uchar>(qBound(0.0, b * it + tb * t + 0.5, 255.0));
            *dst++ = static_cast<uchar>(qBound(0.0, g * it + tg * t + 0.5, 255.0));
            *dst++ = static_cast<uchar>(qBound(0.0, r * it + tr * t + 0.5, 255.0));
            *dst++ = a;
        }
    }
}
