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

#include "huesaturationeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"

#include <algorithm>
#include <cmath>

HueSaturationEffect::HueSaturationEffect() :
    RasterEffect(QObject::tr("色相/饱和度 (Hue-Saturation)"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "HueSaturation", HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::HUE_SATURATION)
{
    mHue = enve::make_shared<QrealAnimator>(
                0.0, -180.0, 180.0, 1.0, "hue");
    ca_addChild(mHue);

    mSaturation = enve::make_shared<QrealAnimator>(
                0.0, -100.0, 100.0, 1.0, "saturation");
    ca_addChild(mSaturation);

    mLightness = enve::make_shared<QrealAnimator>(
                0.0, -100.0, 100.0, 1.0, "lightness");
    ca_addChild(mLightness);
}

class HueSaturationEffectCaller : public OpenGLRasterEffectCaller {
public:
    HueSaturationEffectCaller(const HardwareSupport hwSupport,
                              const qreal hue,
                              const qreal saturation,
                              const qreal lightness) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/huesaturationeffect.frag",
                                 hwSupport),
        mHue(hue), mSaturation(saturation), mLightness(lightness) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sHueU = gl->glGetUniformLocation(sProgramId, "hue");
        sSaturationU = gl->glGetUniformLocation(sProgramId, "saturation");
        sLightnessU = gl->glGetUniformLocation(sProgramId, "lightness");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sHueU, toSkScalar(mHue));
        gl->glUniform1f(sSaturationU, toSkScalar(mSaturation));
        gl->glUniform1f(sLightnessU, toSkScalar(mLightness));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sHueU;
    static GLint sSaturationU;
    static GLint sLightnessU;

    const qreal mHue;
    const qreal mSaturation;
    const qreal mLightness;
};

bool HueSaturationEffectCaller::sInitialized = false;
GLuint HueSaturationEffectCaller::sProgramId = 0;

GLint HueSaturationEffectCaller::sHueU = -1;
GLint HueSaturationEffectCaller::sSaturationU = -1;
GLint HueSaturationEffectCaller::sLightnessU = -1;

stdsptr<RasterEffectCaller> HueSaturationEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(resolution)
    Q_UNUSED(influence)
    Q_UNUSED(data)

    const qreal hue = qBound(-180., mHue->getEffectiveValue(relFrame), 180.);
    const qreal saturation = qBound(-100.,
            mSaturation->getEffectiveValue(relFrame), 100.) / 100.;
    const qreal lightness = qBound(-100.,
            mLightness->getEffectiveValue(relFrame), 100.) / 100.;

    // identity transform = no caller, AE-style passthrough
    if (qFuzzyIsNull(hue) && qFuzzyIsNull(saturation) &&
        qFuzzyIsNull(lightness)) {
        return nullptr;
    }

    return enve::make_shared<HueSaturationEffectCaller>(
                instanceHwSupport(), hue, saturation, lightness);
}

namespace {

// straight-color HSL helpers mirroring the fragment shader, so the
// CPU fallback matches the GPU output pixel for pixel
void rgb2hsl(const qreal r, const qreal g, const qreal b,
             qreal& h, qreal& s, qreal& l)
{
    const qreal maxc = std::max(r, std::max(g, b));
    const qreal minc = std::min(r, std::min(g, b));
    l = (maxc + minc) * 0.5;
    if (maxc == minc) { h = 0.0; s = 0.0; return; }
    const qreal d = maxc - minc;
    s = l > 0.5 ? d / (2.0 - maxc - minc) : d / (maxc + minc);
    if (maxc == r)      { h = (g - b) / d + (g < b ? 6.0 : 0.0); }
    else if (maxc == g) { h = (b - r) / d + 2.0; }
    else                { h = (r - g) / d + 4.0; }
    h /= 6.0;
}

qreal hue2rgb(const qreal p, const qreal q, qreal t)
{
    if (t < 0.0) { t += 1.0; }
    if (t > 1.0) { t -= 1.0; }
    if (t < 1.0 / 6.0) { return p + (q - p) * 6.0 * t; }
    if (t < 1.0 / 2.0) { return q; }
    if (t < 2.0 / 3.0) { return p + (q - p) * (2.0 / 3.0 - t) * 6.0; }
    return p;
}

void hsl2rgb(const qreal h, const qreal s, const qreal l,
             qreal& r, qreal& g, qreal& b)
{
    if (s == 0.0) { r = l; g = l; b = l; return; }
    const qreal q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const qreal p = 2.0 * l - q;
    r = hue2rgb(p, q, h + 1.0 / 3.0);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1.0 / 3.0);
}

} // namespace

void HueSaturationEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for (int xi = xMin; xi <= xMax; xi++) {
            // kN32 is little-endian BGRA in memory, premultiplied
            const qreal b = *src++;
            const qreal g = *src++;
            const qreal r = *src++;
            const qreal a = *src++;

            if (a <= 0.0) {
                *dst++ = 0; *dst++ = 0; *dst++ = 0; *dst++ = 0;
                continue;
            }

            // work on straight colors: premultiplied rgb skews hue/sat
            const qreal ia = 1.0 / a;
            qreal h, s, l;
            rgb2hsl(std::min(r * ia, 1.0), std::min(g * ia, 1.0),
                    std::min(b * ia, 1.0), h, s, l);

            h = std::fmod(h + mHue / 360.0, 1.0);
            if (h < 0.0) { h += 1.0; }
            s = mSaturation < 0.0 ? s * (1.0 + mSaturation)
                                  : s + (1.0 - s) * mSaturation;
            l = mLightness < 0.0 ? l * (1.0 + mLightness)
                                 : l + (1.0 - l) * mLightness;

            qreal nr, ng, nb;
            hsl2rgb(h, s, l, nr, ng, nb);

            *dst++ = static_cast<uchar>(
                        qBound(0., nb * a + 0.5, 255.));
            *dst++ = static_cast<uchar>(
                        qBound(0., ng * a + 0.5, 255.));
            *dst++ = static_cast<uchar>(
                        qBound(0., nr * a + 0.5, 255.));
            *dst++ = static_cast<uchar>(a);
        }
    }
}
