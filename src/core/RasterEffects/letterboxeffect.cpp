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

#include "letterboxeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "appsupport.h"

LetterboxEffect::LetterboxEffect() :
    RasterEffect("letterbox",
                 AppSupport::getRasterEffectHardwareSupport("Letterbox",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::LETTERBOX)
{
    mSize = enve::make_shared<QrealAnimator>(12.0, 0.0, 50.0, 0.5, "size");
    ca_addChild(mSize);

    mFeather = enve::make_shared<QrealAnimator>(0.0, 0.0, 10.0, 0.1, "feather");
    ca_addChild(mFeather);

    mColor = enve::make_shared<ColorAnimator>("color");
    mColor->setColor(QColor(0, 0, 0));
    ca_addChild(mColor);
}

class LetterboxEffectCaller : public OpenGLRasterEffectCaller {
public:
    LetterboxEffectCaller(const HardwareSupport hwSupport,
                          const qreal barSize,
                          const qreal feather,
                          const QColor& color) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/letterboxeffect.frag",
                                 hwSupport),
        mBarSize(barSize),
        mFeather(feather),
        mColor(color) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sBarSizeU = gl->glGetUniformLocation(sProgramId, "barSize");
        sFeatherU = gl->glGetUniformLocation(sProgramId, "feather");
        sColorU = gl->glGetUniformLocation(sProgramId, "color");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sBarSizeU, mBarSize);
        gl->glUniform1f(sFeatherU, mFeather);
        gl->glUniform4f(sColorU,
                        mColor.redF(),
                        mColor.greenF(),
                        mColor.blueF(),
                        mColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sBarSizeU;
    static GLint sFeatherU;
    static GLint sColorU;

    const qreal mBarSize;
    const qreal mFeather;
    const QColor mColor;
};

bool LetterboxEffectCaller::sInitialized = false;
GLuint LetterboxEffectCaller::sProgramId = 0;

GLint LetterboxEffectCaller::sBarSizeU = -1;
GLint LetterboxEffectCaller::sFeatherU = -1;
GLint LetterboxEffectCaller::sColorU = -1;

stdsptr<RasterEffectCaller> LetterboxEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal barSize = (mSize->getEffectiveValue(relFrame) / 100.0) * influence;
    const qreal feather = (mFeather->getEffectiveValue(relFrame) / 100.0) * influence;
    const QColor color = mColor->getColor(relFrame);

    return enve::make_shared<LetterboxEffectCaller>(
                instanceHwSupport(), barSize, feather, color);
}

void LetterboxEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal cr = mColor.redF();
    const qreal cg = mColor.greenF();
    const qreal cb = mColor.blueF();
    const qreal end = mBarSize + std::max(0.0001, mFeather);
    const qreal invSpan = 1.0 / (end - mBarSize);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal vPos = qreal(yi) / imgHeight;
        const qreal dMin = std::min(vPos, 1.0 - vPos);
        qreal barMask = 0.0;
        if (dMin <= mBarSize) {
            barMask = 1.0;
        } else if (dMin < end) {
            qreal t = (dMin - mBarSize) * invSpan;
            barMask = 1.0 - (t * t * (3.0 - 2.0 * t));
        }

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal aNorm = a / 255.0;
            const qreal rOut = (r / 255.0) * (1.0 - barMask) + cr * aNorm * barMask;
            const qreal gOut = (g / 255.0) * (1.0 - barMask) + cg * aNorm * barMask;
            const qreal bOut = (b / 255.0) * (1.0 - barMask) + cb * aNorm * barMask;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, rOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, gOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, bOut * 255.0)));
            *dst++ = a;
        }
    }
}
