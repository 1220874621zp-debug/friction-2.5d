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

#include "tinteffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "colorhelpers.h"
#include "Animators/coloranimator.h"
#include "Animators/qrealanimator.h"
#include "appsupport.h"

TintEffect::TintEffect() :
    RasterEffect("tint",
                 AppSupport::getRasterEffectHardwareSupport("Tint",
                                                            HardwareSupport::cpuOnly),
                 true,
                 RasterEffectType::TINT)
{
    mMapBlack = enve::make_shared<ColorAnimator>("map black");
    mMapBlack->setColor(QColor(0, 0, 0));
    ca_addChild(mMapBlack);

    mMapWhite = enve::make_shared<ColorAnimator>("map white");
    mMapWhite->setColor(QColor(255, 255, 255));
    ca_addChild(mMapWhite);

    mAmount = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0, "amount");
    ca_addChild(mAmount);
}

class TintEffectCaller : public OpenGLRasterEffectCaller {
public:
    TintEffectCaller(const HardwareSupport hwSupport,
                     const QColor& mapBlack,
                     const QColor& mapWhite,
                     const qreal amount) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/tinteffect.frag",
                                 hwSupport),
        mMapBlack(mapBlack),
        mMapWhite(mapWhite),
        mAmount(amount) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sMapBlackU = gl->glGetUniformLocation(sProgramId, "mapBlack");
        sMapWhiteU = gl->glGetUniformLocation(sProgramId, "mapWhite");
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform4f(sMapBlackU,
                        mMapBlack.redF(),
                        mMapBlack.greenF(),
                        mMapBlack.blueF(),
                        mMapBlack.alphaF());
        gl->glUniform4f(sMapWhiteU,
                        mMapWhite.redF(),
                        mMapWhite.greenF(),
                        mMapWhite.blueF(),
                        mMapWhite.alphaF());
        gl->glUniform1f(sAmountU, toSkScalar(mAmount));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sMapBlackU;
    static GLint sMapWhiteU;
    static GLint sAmountU;

    const QColor mMapBlack;
    const QColor mMapWhite;
    const qreal mAmount;
};

bool TintEffectCaller::sInitialized = false;
GLuint TintEffectCaller::sProgramId = 0;

GLint TintEffectCaller::sMapBlackU = -1;
GLint TintEffectCaller::sMapWhiteU = -1;
GLint TintEffectCaller::sAmountU = -1;

stdsptr<RasterEffectCaller> TintEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const QColor mapBlack = mMapBlack->getColor(relFrame);
    const QColor mapWhite = mMapWhite->getColor(relFrame);
    const qreal amount = (mAmount->getEffectiveValue(relFrame) / 100.0) * influence;

    return enve::make_shared<TintEffectCaller>(
                instanceHwSupport(), mapBlack, mapWhite, amount);
}

void TintEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const qreal br = mMapBlack.redF();
    const qreal bg = mMapBlack.greenF();
    const qreal bb = mMapBlack.blueF();

    const qreal wr = mMapWhite.redF();
    const qreal wg = mMapWhite.greenF();
    const qreal wb = mMapWhite.blueF();

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
            const qreal tr = br * (1.0 - luma) + wr * luma;
            const qreal tg = bg * (1.0 - luma) + wg * luma;
            const qreal tb = bb * (1.0 - luma) + wb * luma;

            const qreal rOut = (r / 255.0) * (1.0 - mAmount) + tr * mAmount;
            const qreal gOut = (g / 255.0) * (1.0 - mAmount) + tg * mAmount;
            const qreal bOut = (b / 255.0) * (1.0 - mAmount) + tb * mAmount;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, rOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, gOut * 255.0)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, bOut * 255.0)));
            *dst++ = a;
        }
    }
}
