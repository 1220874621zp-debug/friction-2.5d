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

// Chroma key algorithm ported from Enhanced Hybrid Keyer 3.1
// https://github.com/RazvanO2/Enhanced-Hybrid-Keyer
// Copyright (c) Eki "Halsu" Halkka and Razvan "zvix" Olariu (MIT License)
// The CPU path mirrors chromakeyeffect.frag step by step, colors are
// un-premultiplied on entry and re-premultiplied on exit.

#include "chromakeyeffect.h"

#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/coloranimator.h"
#include "Properties/comboboxproperty.h"

#include "appsupport.h"

#include <algorithm>
#include <cmath>

namespace {

struct Vec3 { float r, g, b; };

float sat1(const float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Mirrors the rgb2hsv in chromakeyeeffect.frag
Vec3 rgb2hsv(const Vec3& c) {
    const float Kx = 0.f, Ky = -1.f / 3.f, Kz = 2.f / 3.f, Kw = -1.f;
    // p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g))
    const float s1 = c.g >= c.b ? 1.f : 0.f;
    const float px = c.b + (c.g - c.b) * s1;
    const float py = c.g + (c.b - c.g) * s1;
    const float pz = Kw + (Kx - Kw) * s1;
    const float pw = Kz + (Ky - Kz) * s1;
    // q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r))
    const float s2 = c.r >= px ? 1.f : 0.f;
    const float qx = px + (c.r - px) * s2;
    const float qy = py;
    const float qz = pw + (pz - pw) * s2;
    const float qw = c.r + (px - c.r) * s2;
    const float d = qx - std::min(qw, qy);
    const float e = 1.0e-10f;
    Vec3 hsv;
    hsv.r = std::abs(qz + (qw - qy) / (6.f * d + e));
    hsv.g = d / (qx + e);
    hsv.b = qx;
    return hsv;
}

// Mirrors the hsv2rgb in chromakeyeeffect.frag
Vec3 hsv2rgb(const Vec3& c) {
    const float Kx = 1.f, Ky = 2.f / 3.f, Kz = 1.f / 3.f, Kw = 3.f;
    const float channels[3] = { c.r + Kx, c.r + Ky, c.r + Kz };
    Vec3 rgb;
    float* out[3] = { &rgb.r, &rgb.g, &rgb.b };
    for (int i = 0; i < 3; i++) {
        const float fr = channels[i] - std::floor(channels[i]);
        const float p = std::abs(fr * 6.f - Kw);
        const float cl = std::min(std::max(p - Kx, 0.f), 1.f);
        *out[i] = c.b * (Kx + (cl - Kx) * c.g);
    }
    return rgb;
}

// Mirrors main() in chromakeyeeffect.frag step by step
void processPixel(const ChromaKeyEffectData& d,
                  const float rIn, const float gIn, const float bIn, const float aIn,
                  float& rOut, float& gOut, float& bOut, float& aOut)
{
    const float key_tolerance_factor = d.mTolerance * 0.01f;
    const float matte_white_factor = 1.f + (d.mMatteWhite * 0.01f);
    const float matte_black_factor = 1.f + (d.mMatteBlack * 0.01f);
    const float matte_highlights_factor = d.mMatteHighlights * 0.01f;
    const float matte_shadows_factor = d.mMatteShadows * 0.01f;
    const float spill_reduction_factor = d.mSpillReduction * 0.01f;
    const float spill_balance_factor = 0.5f + (d.mSpillBalance * 0.005f);
    const float edge_softness_factor = d.mEdgeSoftness * 0.01f;
    const float hair_detail_value = d.mHairDetail * 0.01f;
    const float defringe_value = d.mDefringe * 0.01f;

    // Un-premultiply the input color
    Vec3 rawColor = { 0.f, 0.f, 0.f };
    if (aIn > 0.f) {
        rawColor.r = rIn / aIn;
        rawColor.g = gIn / aIn;
        rawColor.b = bIn / aIn;
    }

    Vec3 color = rawColor;
    Vec3 keyColorWorking = { d.mKeyR, d.mKeyG, d.mKeyB };
    Vec3 spillMatte = { 0.f, 0.f, 0.f };
    float edgeMatte = 0.f;
    float hueOffset = 0.f;
    float alpha = 0.f;

    // Align the key hue with pure green/blue/red for better keying
    Vec3 hsvColor = rgb2hsv(color);
    Vec3 hsvKey = rgb2hsv(keyColorWorking);
    Vec3 hsvRaw = rgb2hsv(rawColor);

    if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
        if (hsvKey.r < 0.33333f) { hueOffset = 0.33333f - hsvKey.r; }
        else                     { hueOffset = -(hsvKey.r - 0.33333f); }
    } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
        if (hsvKey.r < 0.66667f) { hueOffset = 0.66667f - hsvKey.r; }
        else                     { hueOffset = -(hsvKey.r - 0.66667f); }
    } else {
        if (hsvKey.r > 0.5f) { hueOffset = 1.f - hsvKey.r; }
        else                 { hueOffset = -hsvKey.r; }
    }

    hsvColor.r += hueOffset;
    hsvKey.r += hueOffset;
    hsvRaw.r += hueOffset;

    color = hsv2rgb(hsvColor);
    keyColorWorking = hsv2rgb(hsvKey);
    rawColor = hsv2rgb(hsvRaw);

    // Calculate alpha matte based on keying method
    if (d.mKeyMethod == 0) { // RGB Difference (Vlahos method)
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            alpha = color.g - std::max(color.r, color.b);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            alpha = color.b - std::max(color.r, color.g);
        } else {
            alpha = color.r - std::max(color.g, color.b);
        }
        alpha = alpha * (1.f + key_tolerance_factor);
    } else if (d.mKeyMethod == 1) { // YUV Color Space
        const float Y = 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
        const float U = ((color.b - Y) * 0.565f) + 0.5f;
        const float V = ((color.r - Y) * 0.713f) + 0.5f;
        const float Y2 = 0.299f * keyColorWorking.r + 0.587f * keyColorWorking.g + 0.114f * keyColorWorking.b;
        const float U2 = ((keyColorWorking.b - Y2) * 0.565f) + 0.5f;
        const float V2 = ((keyColorWorking.r - Y2) * 0.713f) + 0.5f;
        const float dUc = std::abs(U / std::max(U2, 0.001f) - 1.f);
        const float dVc = std::abs(V / std::max(V2, 0.001f) - 1.f);
        const float yuvDiff = std::max(dUc, dVc);
        alpha = 1.f - (key_tolerance_factor + 1.f) * yuvDiff;
    } else if (d.mKeyMethod == 2) { // Hybrid
        float alpha_rgb = 0.f;
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            alpha_rgb = color.g - std::max(color.r, color.b);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            alpha_rgb = color.b - std::max(color.r, color.g);
        } else {
            alpha_rgb = color.r - std::max(color.g, color.b);
        }
        const float Y = 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
        const float U = ((color.b - Y) * 0.565f) + 0.5f;
        const float V = ((color.r - Y) * 0.713f) + 0.5f;
        const float Y2 = 0.299f * keyColorWorking.r + 0.587f * keyColorWorking.g + 0.114f * keyColorWorking.b;
        const float U2 = ((keyColorWorking.b - Y2) * 0.565f) + 0.5f;
        const float V2 = ((keyColorWorking.r - Y2) * 0.713f) + 0.5f;
        const float dUc = std::abs(U / std::max(U2, 0.001f) - 1.f);
        const float dVc = std::abs(V / std::max(V2, 0.001f) - 1.f);
        const float alpha_yuv = 1.f - 2.f * std::max(dUc, dVc);
        alpha = alpha_rgb + (alpha_yuv - alpha_rgb) * key_tolerance_factor;
    } else { // Multi-Layer
        float baseAlpha = 0.f;
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            baseAlpha = color.g - std::max(color.r, color.b);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            baseAlpha = color.b - std::max(color.r, color.g);
        } else {
            baseAlpha = color.r - std::max(color.g, color.b);
        }
        baseAlpha = baseAlpha * (1.f + key_tolerance_factor);
        const float colorLuma = 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
        const float keyLuma = 0.2126f * keyColorWorking.r + 0.7152f * keyColorWorking.g + 0.0722f * keyColorWorking.b;
        const float shadowThreshold = keyLuma * 0.7f;
        const float highlightThreshold = keyLuma * 1.3f;
        float shadowAlpha = 0.f;
        float midtoneAlpha = 0.f;
        float highlightAlpha = 0.f;
        if (colorLuma < shadowThreshold) {
            const float shadowFactor = 1.f - (colorLuma / std::max(shadowThreshold, 0.001f));
            shadowAlpha = baseAlpha * (1.f - (shadowFactor * 0.5f));
        }
        if (colorLuma >= shadowThreshold && colorLuma <= highlightThreshold) {
            const float midtoneFactor = 1.f - (2.f * std::abs(colorLuma - keyLuma) / std::max(highlightThreshold - shadowThreshold, 0.001f));
            midtoneAlpha = baseAlpha * midtoneFactor;
        }
        if (colorLuma > highlightThreshold) {
            const float highlightFactor = (colorLuma - highlightThreshold) / std::max(1.f - highlightThreshold, 0.001f);
            highlightAlpha = baseAlpha * (1.f - (highlightFactor * 0.3f));
        }
        alpha = std::max(shadowAlpha, std::max(midtoneAlpha, highlightAlpha));
    }

    edgeMatte = alpha;

    // Handle bright areas that should be keyed (like reflections)
    if (matte_highlights_factor > 0.f) {
        const float highlightDiff = (color.r - keyColorWorking.r) +
                                    (color.g - keyColorWorking.g) +
                                    (color.b - keyColorWorking.b);
        alpha -= matte_highlights_factor * sat1(highlightDiff);
    }

    // Handle dark areas that should be keyed (like shadows)
    if (matte_shadows_factor > 0.f) {
        const float shadowDiff = (keyColorWorking.r - color.r) +
                                 (keyColorWorking.g - color.g) +
                                 (keyColorWorking.b - color.b);
        alpha -= matte_shadows_factor * sat1(shadowDiff);
    }

    // Matte controls: black point, invert, white point
    alpha = alpha * matte_black_factor;
    alpha = 1.f - alpha;
    alpha = sat1(alpha * matte_white_factor);

    // Hair detail enhancement
    if (hair_detail_value > 0.f) {
        const float rawLuma = 0.2126f * rawColor.r + 0.7152f * rawColor.g + 0.0722f * rawColor.b;
        const float keyLuma = 0.2126f * keyColorWorking.r + 0.7152f * keyColorWorking.g + 0.0722f * keyColorWorking.b;
        float edgeMask = 1.f - std::abs((alpha * 2.f) - 1.f);
        edgeMask = std::pow(std::max(edgeMask, 0.f), 0.5f);
        const float lumaDiff = sat1(std::abs(rawLuma - keyLuma) * 2.f);
        const float enhancementMask = edgeMask * lumaDiff;
        alpha = sat1(alpha + (enhancementMask * hair_detail_value));
    }

    // Edge softness
    if (edge_softness_factor > 0.f) {
        edgeMatte = 1.f - std::abs(alpha * 2.f - 1.f);
        edgeMatte = std::pow(std::max(edgeMatte, 0.f), 2.f - edge_softness_factor);
        alpha = alpha + (0.5f - alpha) * edgeMatte * edge_softness_factor;
    }

    // Spill reduction
    if (spill_reduction_factor > 0.f) {
        const float spillRB = color.r + (color.b - color.r) * spill_balance_factor;
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            if (color.g > spillRB) {
                color.g = color.g + (spillRB - color.g) * spill_reduction_factor;
            }
            spillMatte.g = std::max(0.f, color.g - spillRB);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            if (color.b > spillRB) {
                color.b = color.b + (spillRB - color.b) * spill_reduction_factor;
            }
            spillMatte.b = std::max(0.f, color.b - spillRB);
        } else {
            const float spillGB = std::max(color.g, color.b);
            if (color.r > spillGB) {
                color.r = color.r + (spillGB - color.r) * spill_reduction_factor;
            }
            spillMatte.r = std::max(0.f, color.r - spillGB);
        }
    }

    // Restore the raw foreground color (premultiply at default 1.0)
    if (alpha > 0.f) {
        color = rawColor;
    }

    // Edge defringing
    if (defringe_value > 0.f && alpha < 1.f) {
        const float edgeDefringeValue = (1.f - alpha) * defringe_value;
        const float keyMax = std::max(keyColorWorking.r, std::max(keyColorWorking.g, keyColorWorking.b));
        if (keyColorWorking.g > 0.4f && keyColorWorking.g >= keyColorWorking.r && keyColorWorking.g >= keyColorWorking.b) {
            const float adjust = keyColorWorking.g / std::max(0.001f, keyMax);
            color.g *= (1.f - (edgeDefringeValue * adjust));
            color.r *= (1.f + (edgeDefringeValue * 0.2f));
            color.b *= (1.f + (edgeDefringeValue * 0.2f));
        } else if (keyColorWorking.b > 0.4f && keyColorWorking.b >= keyColorWorking.g && keyColorWorking.b >= keyColorWorking.r) {
            const float adjust = keyColorWorking.b / std::max(0.001f, keyMax);
            color.b *= (1.f - (edgeDefringeValue * adjust));
            color.r *= (1.f + (edgeDefringeValue * 0.2f));
            color.g *= (1.f + (edgeDefringeValue * 0.2f));
        } else if (keyColorWorking.r > 0.4f && keyColorWorking.r >= keyColorWorking.g && keyColorWorking.r >= keyColorWorking.b) {
            const float adjust = keyColorWorking.r / std::max(0.001f, keyMax);
            color.r *= (1.f - (edgeDefringeValue * adjust));
            color.g *= (1.f + (edgeDefringeValue * 0.2f));
            color.b *= (1.f + (edgeDefringeValue * 0.2f));
        }
    }

    // Undo the keying hue alignment
    Vec3 hsvFinal = rgb2hsv(color);
    hsvFinal.r -= hueOffset;
    color = hsv2rgb(hsvFinal);

    // Re-premultiply for the straight-to-premultiplied output
    color.r *= alpha;
    color.g *= alpha;
    color.b *= alpha;

    // Preview modes
    if (d.mPreviewMode == 1) {
        color.r = alpha; color.g = alpha; color.b = alpha;
        alpha = 1.f;
    } else if (d.mPreviewMode == 2) {
        color.r = edgeMatte; color.g = edgeMatte; color.b = edgeMatte;
        alpha = 1.f;
    } else if (d.mPreviewMode == 3) {
        color.r = spillMatte.r; color.g = spillMatte.g; color.b = spillMatte.b;
        alpha = 1.f;
    }

    rOut = sat1(color.r);
    gOut = sat1(color.g);
    bOut = sat1(color.b);
    aOut = sat1(alpha);
}

} // namespace

ChromaKeyEffect::ChromaKeyEffect() :
    RasterEffect("chroma-key",
                 AppSupport::getRasterEffectHardwareSupport("ChromaKey",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::CHROMA_KEY)
{
    mKeyColor = enve::make_shared<ColorAnimator>(QObject::tr("key color"));
    mKeyColor->setColor(QColor(0, 255, 0));
    ca_addChild(mKeyColor);

    const auto methods = QStringList() <<
            QObject::tr("RGB Difference") <<
            QObject::tr("YUV Color Space") <<
            QObject::tr("Hybrid") <<
            QObject::tr("Multi-Layer");
    mKeyMethod = enve::make_shared<ComboBoxProperty>(QObject::tr("keying method"), methods);
    ca_addChild(mKeyMethod);

    mTolerance = enve::make_shared<QrealAnimator>(50, 0, 100, 1,
                                                  QObject::tr("tolerance"));
    ca_addChild(mTolerance);

    mMatteBlack = enve::make_shared<QrealAnimator>(0, -100, 100, 1,
                                                   QObject::tr("matte black"));
    ca_addChild(mMatteBlack);

    mMatteWhite = enve::make_shared<QrealAnimator>(0, -100, 100, 1,
                                                   QObject::tr("matte white"));
    ca_addChild(mMatteWhite);

    mMatteHighlights = enve::make_shared<QrealAnimator>(0, -100, 100, 1,
                                                        QObject::tr("matte highlights"));
    ca_addChild(mMatteHighlights);

    mMatteShadows = enve::make_shared<QrealAnimator>(0, -100, 100, 1,
                                                     QObject::tr("matte shadows"));
    ca_addChild(mMatteShadows);

    mEdgeSoftness = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                                     QObject::tr("edge softness"));
    ca_addChild(mEdgeSoftness);

    mHairDetail = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                                   QObject::tr("hair detail"));
    ca_addChild(mHairDetail);

    mDefringe = enve::make_shared<QrealAnimator>(0, 0, 100, 1,
                                                 QObject::tr("edge defringe"));
    ca_addChild(mDefringe);

    mSpillReduction = enve::make_shared<QrealAnimator>(50, 0, 100, 1,
                                                       QObject::tr("spill reduction"));
    ca_addChild(mSpillReduction);

    mSpillBalance = enve::make_shared<QrealAnimator>(0, -100, 100, 1,
                                                     QObject::tr("spill balance"));
    ca_addChild(mSpillBalance);

    const auto previews = QStringList() <<
            QObject::tr("Final Result") <<
            QObject::tr("Alpha Channel") <<
            QObject::tr("Edge Matte") <<
            QObject::tr("Spill Matte");
    mPreviewMode = enve::make_shared<ComboBoxProperty>(QObject::tr("preview mode"), previews);
    ca_addChild(mPreviewMode);
}

class ChromaKeyEffectCaller : public OpenGLRasterEffectCaller {
public:
    ChromaKeyEffectCaller(const HardwareSupport hwSupport,
                          const ChromaKeyEffectData& data) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/chromakeyeffect.frag",
                                 hwSupport),
        mData(data) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sKeyColorU = gl->glGetUniformLocation(sProgramId, "uKeyColor");
        sKeyMethodU = gl->glGetUniformLocation(sProgramId, "uKeyMethod");
        sToleranceU = gl->glGetUniformLocation(sProgramId, "uTolerance");
        sMatteBlackU = gl->glGetUniformLocation(sProgramId, "uMatteBlack");
        sMatteWhiteU = gl->glGetUniformLocation(sProgramId, "uMatteWhite");
        sMatteHighlightsU = gl->glGetUniformLocation(sProgramId, "uMatteHighlights");
        sMatteShadowsU = gl->glGetUniformLocation(sProgramId, "uMatteShadows");
        sEdgeSoftnessU = gl->glGetUniformLocation(sProgramId, "uEdgeSoftness");
        sHairDetailU = gl->glGetUniformLocation(sProgramId, "uHairDetail");
        sDefringeU = gl->glGetUniformLocation(sProgramId, "uDefringe");
        sSpillReductionU = gl->glGetUniformLocation(sProgramId, "uSpillReduction");
        sSpillBalanceU = gl->glGetUniformLocation(sProgramId, "uSpillBalance");
        sPreviewModeU = gl->glGetUniformLocation(sProgramId, "uPreviewMode");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform3f(sKeyColorU, mData.mKeyR, mData.mKeyG, mData.mKeyB);
        gl->glUniform1i(sKeyMethodU, mData.mKeyMethod);
        gl->glUniform1f(sToleranceU, mData.mTolerance);
        gl->glUniform1f(sMatteBlackU, mData.mMatteBlack);
        gl->glUniform1f(sMatteWhiteU, mData.mMatteWhite);
        gl->glUniform1f(sMatteHighlightsU, mData.mMatteHighlights);
        gl->glUniform1f(sMatteShadowsU, mData.mMatteShadows);
        gl->glUniform1f(sEdgeSoftnessU, mData.mEdgeSoftness);
        gl->glUniform1f(sHairDetailU, mData.mHairDetail);
        gl->glUniform1f(sDefringeU, mData.mDefringe);
        gl->glUniform1f(sSpillReductionU, mData.mSpillReduction);
        gl->glUniform1f(sSpillBalanceU, mData.mSpillBalance);
        gl->glUniform1i(sPreviewModeU, mData.mPreviewMode);
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sKeyColorU;
    static GLint sKeyMethodU;
    static GLint sToleranceU;
    static GLint sMatteBlackU;
    static GLint sMatteWhiteU;
    static GLint sMatteHighlightsU;
    static GLint sMatteShadowsU;
    static GLint sEdgeSoftnessU;
    static GLint sHairDetailU;
    static GLint sDefringeU;
    static GLint sSpillReductionU;
    static GLint sSpillBalanceU;
    static GLint sPreviewModeU;

    const ChromaKeyEffectData mData;
};

bool ChromaKeyEffectCaller::sInitialized = false;
GLuint ChromaKeyEffectCaller::sProgramId = 0;

GLint ChromaKeyEffectCaller::sKeyColorU = -1;
GLint ChromaKeyEffectCaller::sKeyMethodU = -1;
GLint ChromaKeyEffectCaller::sToleranceU = -1;
GLint ChromaKeyEffectCaller::sMatteBlackU = -1;
GLint ChromaKeyEffectCaller::sMatteWhiteU = -1;
GLint ChromaKeyEffectCaller::sMatteHighlightsU = -1;
GLint ChromaKeyEffectCaller::sMatteShadowsU = -1;
GLint ChromaKeyEffectCaller::sEdgeSoftnessU = -1;
GLint ChromaKeyEffectCaller::sHairDetailU = -1;
GLint ChromaKeyEffectCaller::sDefringeU = -1;
GLint ChromaKeyEffectCaller::sSpillReductionU = -1;
GLint ChromaKeyEffectCaller::sSpillBalanceU = -1;
GLint ChromaKeyEffectCaller::sPreviewModeU = -1;

stdsptr<RasterEffectCaller> ChromaKeyEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    ChromaKeyEffectData effData;
    const QColor keyCol = mKeyColor->getColor(relFrame);
    effData.mKeyR = keyCol.redF();
    effData.mKeyG = keyCol.greenF();
    effData.mKeyB = keyCol.blueF();
    effData.mKeyMethod = mKeyMethod->getCurrentValue();
    effData.mTolerance = mTolerance->getEffectiveValue(relFrame) * influence;
    effData.mMatteBlack = mMatteBlack->getEffectiveValue(relFrame);
    effData.mMatteWhite = mMatteWhite->getEffectiveValue(relFrame);
    effData.mMatteHighlights = mMatteHighlights->getEffectiveValue(relFrame);
    effData.mMatteShadows = mMatteShadows->getEffectiveValue(relFrame);
    effData.mEdgeSoftness = mEdgeSoftness->getEffectiveValue(relFrame);
    effData.mHairDetail = mHairDetail->getEffectiveValue(relFrame);
    effData.mDefringe = mDefringe->getEffectiveValue(relFrame);
    effData.mSpillReduction = mSpillReduction->getEffectiveValue(relFrame);
    effData.mSpillBalance = mSpillBalance->getEffectiveValue(relFrame);
    effData.mPreviewMode = mPreviewMode->getCurrentValue();

    return enve::make_shared<ChromaKeyEffectCaller>(
                instanceHwSupport(), effData);
}

void ChromaKeyEffectCaller::processCpu(CpuRenderTools& renderTools,
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

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right(), imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom(), imgHeight - 1);

    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        for (int xi = xMin; xi <= xMax; xi++) {
            const float r = *src++ / 255.f;
            const float g = *src++ / 255.f;
            const float b = *src++ / 255.f;
            const float a = *src++ / 255.f;

            float rOut, gOut, bOut, aOut;
            processPixel(mData, r, g, b, a, rOut, gOut, bOut, aOut);

            *dst++ = static_cast<uchar>(rOut * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(gOut * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(bOut * 255.f + 0.5f);
            *dst++ = static_cast<uchar>(aOut * 255.f + 0.5f);
        }
    }
}
