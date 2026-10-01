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

#include "levelseffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "Properties/comboboxproperty.h"
#include "appsupport.h"

#include <cmath>
#include <cstdint>

LevelsEffect::LevelsEffect() :
    RasterEffect(QObject::tr("色阶 (Levels)"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "Levels", HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::LEVELS)
{
    const auto channels = QStringList() <<
                QObject::tr("RGB") <<
                QObject::tr("红") <<
                QObject::tr("绿") <<
                QObject::tr("蓝");
    mChannel = enve::make_shared<ComboBoxProperty>(
                QObject::tr("通道"), channels);
    ca_addChild(mChannel);

    mInBlack = enve::make_shared<QrealAnimator>(
                0.0, 0.0, 253.0, 1.0, QObject::tr("输入黑场"));
    mGamma = enve::make_shared<QrealAnimator>(
                1.0, sMinGamma, sMaxGamma, 0.01, QObject::tr("灰度系数"));
    mInWhite = enve::make_shared<QrealAnimator>(
                255.0, 2.0, 255.0, 1.0, QObject::tr("输入白场"));
    mOutBlack = enve::make_shared<QrealAnimator>(
                0.0, 0.0, 254.0, 1.0, QObject::tr("输出黑场"));
    mOutWhite = enve::make_shared<QrealAnimator>(
                255.0, 1.0, 255.0, 1.0, QObject::tr("输出白场"));

    // the wrappers own the value animators; their rows render as the
    // PS gradient sliders, expanding reveals the keyframable rows
    const auto input = enve::make_shared<LevelsInputAnimator>();
    input->ca_addChild(mInBlack);
    input->ca_addChild(mGamma);
    input->ca_addChild(mInWhite);
    ca_addChild(input);

    const auto output = enve::make_shared<LevelsOutputAnimator>();
    output->ca_addChild(mOutBlack);
    output->ca_addChild(mOutWhite);
    ca_addChild(output);
}

LevelsInputAnimator::LevelsInputAnimator() :
    StaticComplexAnimator(QObject::tr("输入色阶")) {}

LevelsOutputAnimator::LevelsOutputAnimator() :
    StaticComplexAnimator(QObject::tr("输出色阶")) {}

class LevelsEffectCaller : public OpenGLRasterEffectCaller {
public:
    LevelsEffectCaller(const HardwareSupport hwSupport,
                       const int channel,
                       const qreal inBlack, const qreal gamma,
                       const qreal inWhite,
                       const qreal outBlack, const qreal outWhite) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/levelseffect.frag",
                                 hwSupport),
        mChannel(channel), mInBlack(inBlack), mGamma(gamma),
        mInWhite(inWhite), mOutBlack(outBlack), mOutWhite(outWhite) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sChannelU = gl->glGetUniformLocation(sProgramId, "channel");
        sInBlackU = gl->glGetUniformLocation(sProgramId, "inBlack");
        sInWhiteU = gl->glGetUniformLocation(sProgramId, "inWhite");
        sGammaU = gl->glGetUniformLocation(sProgramId, "midGamma");
        sOutBlackU = gl->glGetUniformLocation(sProgramId, "outBlack");
        sOutWhiteU = gl->glGetUniformLocation(sProgramId, "outWhite");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1i(sChannelU, mChannel);
        gl->glUniform1f(sInBlackU, toSkScalar(mInBlack / 255.0));
        gl->glUniform1f(sInWhiteU, toSkScalar(mInWhite / 255.0));
        gl->glUniform1f(sGammaU, toSkScalar(mGamma));
        gl->glUniform1f(sOutBlackU, toSkScalar(mOutBlack / 255.0));
        gl->glUniform1f(sOutWhiteU, toSkScalar(mOutWhite / 255.0));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sChannelU;
    static GLint sInBlackU;
    static GLint sInWhiteU;
    static GLint sGammaU;
    static GLint sOutBlackU;
    static GLint sOutWhiteU;

    const int mChannel;
    const qreal mInBlack;
    const qreal mGamma;
    const qreal mInWhite;
    const qreal mOutBlack;
    const qreal mOutWhite;
};

bool LevelsEffectCaller::sInitialized = false;
GLuint LevelsEffectCaller::sProgramId = 0;

GLint LevelsEffectCaller::sChannelU = -1;
GLint LevelsEffectCaller::sInBlackU = -1;
GLint LevelsEffectCaller::sInWhiteU = -1;
GLint LevelsEffectCaller::sGammaU = -1;
GLint LevelsEffectCaller::sOutBlackU = -1;
GLint LevelsEffectCaller::sOutWhiteU = -1;

stdsptr<RasterEffectCaller> LevelsEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(resolution)
    Q_UNUSED(influence)
    Q_UNUSED(data)

    // PS never lets the white point cross below the black point: the
    // white point is kept at least one level above the black point,
    // and the output white at or above the output black
    const qreal inBlack = qBound(0., mInBlack->getEffectiveValue(relFrame), 253.);
    const qreal gamma = qBound(sMinGamma,
            mGamma->getEffectiveValue(relFrame), sMaxGamma);
    const qreal inWhite = qMax(mInWhite->getEffectiveValue(relFrame),
                               inBlack + 1.);
    const qreal outBlack = qBound(0.,
            mOutBlack->getEffectiveValue(relFrame), 255.);
    const qreal outWhite = qMax(mOutWhite->getEffectiveValue(relFrame),
                                outBlack);

    // identity curve = no caller, AE-style passthrough
    if (qFuzzyIsNull(inBlack) && qFuzzyCompare(gamma, 1.) &&
        qFuzzyCompare(inWhite, 255.) && qFuzzyIsNull(outBlack) &&
        qFuzzyCompare(outWhite, 255.)) {
        return nullptr;
    }

    const int channel = qBound(0, mChannel->getCurrentValue(), 3);

    return enve::make_shared<LevelsEffectCaller>(
                instanceHwSupport(), channel,
                inBlack, gamma, inWhite, outBlack, outWhite);
}

void LevelsEffectCaller::processCpu(CpuRenderTools& renderTools,
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
    const int xMax = std::min((int)data.fTexTile.right() - 1, imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom() - 1, imgHeight - 1);

    // one 256-entry LUT per tile: t = (v - inBlack) / (inWhite - inBlack)
    // clamped to [0,1], gamma-curved (PS lightens mids for gamma > 1),
    // then spread between the output points. kN32 is little-endian
    // BGRA in memory: byte 0 = B, 1 = G, 2 = R
    uint8_t lut[256];
    {
        const qreal scale = 1.0 / (mInWhite - mInBlack);
        const qreal invGamma = 1.0 / mGamma;
        const qreal outScale = mOutWhite - mOutBlack;
        for (int v = 0; v < 256; v++) {
            qreal t = (v - mInBlack) * scale;
            if (t < 0.0) t = 0.0;
            else if (t > 1.0) t = 1.0;
            t = std::pow(t, invGamma);
            lut[v] = static_cast<uint8_t>(
                        qBound(0., mOutBlack + t * outScale + 0.5, 255.));
        }
    }

    const int channel = mChannel;
    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for (int xi = xMin; xi <= xMax; xi++) {
            const uint8_t b = *src++;
            const uint8_t g = *src++;
            const uint8_t r = *src++;
            const uint8_t a = *src++;

            switch (channel) {
            case LevelsEffect::Red:
                *dst++ = b;
                *dst++ = g;
                *dst++ = lut[r];
                break;
            case LevelsEffect::Green:
                *dst++ = b;
                *dst++ = lut[g];
                *dst++ = r;
                break;
            case LevelsEffect::Blue:
                *dst++ = lut[b];
                *dst++ = g;
                *dst++ = r;
                break;
            case LevelsEffect::RGB:
            default:
                *dst++ = lut[b];
                *dst++ = lut[g];
                *dst++ = lut[r];
                break;
            }
            *dst++ = a;
        }
    }
}
