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

#include "edgedetecteffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"

EdgeDetectEffect::EdgeDetectEffect() :
    RasterEffect("edge detect",
                 AppSupport::getRasterEffectHardwareSupport("EdgeDetect",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::EDGE_DETECT)
{
    mThickness = enve::make_shared<QrealAnimator>(1.5, 0.5, 10.0, 0.1, "thickness");
    ca_addChild(mThickness);

    mSensitivity = enve::make_shared<QrealAnimator>(1.0, 0.1, 10.0, 0.1, "sensitivity");
    ca_addChild(mSensitivity);

    mInvert = enve::make_shared<QrealAnimator>(0.0, 0.0, 1.0, 1.0, "invert");
    ca_addChild(mInvert);

    mEdgeColor = enve::make_shared<ColorAnimator>("edge color");
    mEdgeColor->setColor(QColor(0, 0, 0));
    ca_addChild(mEdgeColor);

    mBgColor = enve::make_shared<ColorAnimator>("background");
    mBgColor->setColor(QColor(255, 255, 255, 0));
    ca_addChild(mBgColor);
}

class EdgeDetectEffectCaller : public OpenGLRasterEffectCaller {
public:
    EdgeDetectEffectCaller(const HardwareSupport hwSupport,
                          const qreal thickness,
                          const qreal sensitivity,
                          const qreal invert,
                          const QColor& edgeColor,
                          const QColor& bgColor) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/edgedetecteffect.frag",
                                 hwSupport),
        mThickness(thickness),
        mSensitivity(sensitivity),
        mInvert(invert),
        mEdgeColor(edgeColor),
        mBgColor(bgColor) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sStepSizeU = gl->glGetUniformLocation(sProgramId, "stepSize");
        sSensitivityU = gl->glGetUniformLocation(sProgramId, "sensitivity");
        sInvertU = gl->glGetUniformLocation(sProgramId, "invert");
        sEdgeColorU = gl->glGetUniformLocation(sProgramId, "edgeColor");
        sBgColorU = gl->glGetUniformLocation(sProgramId, "bgColor");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sStepSizeU,
                        toSkScalar(mThickness / 1000.0),
                        toSkScalar(mThickness / 1000.0));
        gl->glUniform1f(sSensitivityU, toSkScalar(mSensitivity));
        gl->glUniform1f(sInvertU, toSkScalar(mInvert));
        gl->glUniform4f(sEdgeColorU,
                        mEdgeColor.redF(),
                        mEdgeColor.greenF(),
                        mEdgeColor.blueF(),
                        mEdgeColor.alphaF());
        gl->glUniform4f(sBgColorU,
                        mBgColor.redF(),
                        mBgColor.greenF(),
                        mBgColor.blueF(),
                        mBgColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sStepSizeU;
    static GLint sSensitivityU;
    static GLint sInvertU;
    static GLint sEdgeColorU;
    static GLint sBgColorU;

    const qreal mThickness;
    const qreal mSensitivity;
    const qreal mInvert;
    const QColor mEdgeColor;
    const QColor mBgColor;
};

bool EdgeDetectEffectCaller::sInitialized = false;
GLuint EdgeDetectEffectCaller::sProgramId = 0;

GLint EdgeDetectEffectCaller::sStepSizeU = -1;
GLint EdgeDetectEffectCaller::sSensitivityU = -1;
GLint EdgeDetectEffectCaller::sInvertU = -1;
GLint EdgeDetectEffectCaller::sEdgeColorU = -1;
GLint EdgeDetectEffectCaller::sBgColorU = -1;

stdsptr<RasterEffectCaller> EdgeDetectEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal thickness = mThickness->getEffectiveValue(relFrame);
    const qreal sensitivity = mSensitivity->getEffectiveValue(relFrame) * influence;
    const qreal invert = mInvert->getEffectiveValue(relFrame);
    const QColor edgeColor = mEdgeColor->getColor(relFrame);
    const QColor bgColor = mBgColor->getColor(relFrame);

    return enve::make_shared<EdgeDetectEffectCaller>(
                instanceHwSupport(), thickness, sensitivity, invert, edgeColor, bgColor);
}

void EdgeDetectEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int step = std::max(1, qRound(mThickness));
    const auto getLuma = [&](int x, int y) -> qreal {
        const int cx = std::max(0, std::min(imgWidth - 1, x));
        const int cy = std::max(0, std::min(imgHeight - 1, y));
        const auto p = static_cast<const uchar*>(srcBtmp.getAddr(cx, cy));
        return (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) * (p[3] / 255.0);
    };

    const qreal er = mEdgeColor.redF();
    const qreal eg = mEdgeColor.greenF();
    const qreal eb = mEdgeColor.blueF();
    const qreal ea = mEdgeColor.alphaF();

    const qreal bgr = mBgColor.redF();
    const qreal bgg = mBgColor.greenF();
    const qreal bgb = mBgColor.blueF();
    const qreal bga = mBgColor.alphaF();

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal gx = -getLuma(xi - step, yi - step) + getLuma(xi + step, yi - step)
                             -2.0 * getLuma(xi - step, yi) + 2.0 * getLuma(xi + step, yi)
                             -getLuma(xi - step, yi + step) + getLuma(xi + step, yi + step);

            const qreal gy = -getLuma(xi - step, yi - step) - 2.0 * getLuma(xi, yi - step) - getLuma(xi + step, yi - step)
                             +getLuma(xi - step, yi + step) + 2.0 * getLuma(xi, yi + step) + getLuma(xi + step, yi + step);

            qreal edge = std::min(1.0, std::sqrt(gx * gx + gy * gy) * (mSensitivity / 255.0));
            if (mInvert > 0.5) edge = 1.0 - edge;

            const qreal rOut = bgr * (1.0 - edge) + er * edge;
            const qreal gOut = bgg * (1.0 - edge) + eg * edge;
            const qreal bOut = bgb * (1.0 - edge) + eb * edge;
            const qreal aOut = std::max(edge * ea, bga);

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, rOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, gOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, bOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, aOut * 255.0)));
        }
    }
}
