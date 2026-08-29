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
// The input texture is premultiplied alpha, the original shader assumes
// straight alpha, so colors are un-premultiplied on entry and
// re-premultiplied on exit.

#version 330 core
layout(location = 0) out vec4 fragColor;

in vec2 texCoord;

uniform sampler2D tex;

uniform vec3 uKeyColor;
uniform int uKeyMethod;
uniform float uTolerance;
uniform float uMatteBlack;
uniform float uMatteWhite;
uniform float uMatteHighlights;
uniform float uMatteShadows;
uniform float uEdgeSoftness;
uniform float uHairDetail;
uniform float uDefringe;
uniform float uSpillReduction;
uniform float uSpillBalance;
uniform int uPreviewMode;

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main(void) {
    // Convert slider values to factors (as in the original shader)
    float key_tolerance_factor = uTolerance * 0.01;
    float matte_white_factor = 1.0 + (uMatteWhite * 0.01);
    float matte_black_factor = 1.0 + (uMatteBlack * 0.01);
    float matte_highlights_factor = uMatteHighlights * 0.01;
    float matte_shadows_factor = uMatteShadows * 0.01;
    float spill_reduction_factor = uSpillReduction * 0.01;
    float spill_balance_factor = 0.5 + (uSpillBalance * 0.005);
    float edge_softness_factor = uEdgeSoftness * 0.01;
    float hair_detail_value = uHairDetail * 0.01;
    float defringe_value = uDefringe * 0.01;

    // Un-premultiply the input color
    vec4 texColor = texture(tex, texCoord);
    float srcAlpha = texColor.a;
    vec3 rawColor = srcAlpha > 0.0 ? texColor.rgb / srcAlpha : vec3(0.0);

    vec3 color = rawColor;
    vec3 keyColorWorking = uKeyColor;
    vec3 spillMatte = vec3(0.0);
    float edgeMatte = 0.0;
    float hueOffset = 0.0;
    float alpha = 0.0;

    // Align the key hue with pure green/blue/red for better keying
    vec3 hsvColor = rgb2hsv(color);
    vec3 hsvKey = rgb2hsv(keyColorWorking);
    vec3 hsvRaw = rgb2hsv(rawColor);

    if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
        if (hsvKey.x < 0.33333) { hueOffset = 0.33333 - hsvKey.x; }
        else                    { hueOffset = -(hsvKey.x - 0.33333); }
    } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
        if (hsvKey.x < 0.66667) { hueOffset = 0.66667 - hsvKey.x; }
        else                    { hueOffset = -(hsvKey.x - 0.66667); }
    } else {
        if (hsvKey.x > 0.5) { hueOffset = 1.0 - hsvKey.x; }
        else                { hueOffset = -hsvKey.x; }
    }

    hsvColor.x = hsvColor.x + hueOffset;
    hsvKey.x = hsvKey.x + hueOffset;
    hsvRaw.x = hsvRaw.x + hueOffset;

    color.rgb = hsv2rgb(hsvColor);
    keyColorWorking.rgb = hsv2rgb(hsvKey);
    rawColor.rgb = hsv2rgb(hsvRaw);

    // Calculate alpha matte based on keying method
    if (uKeyMethod == 0) { // RGB Difference (Vlahos method)
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            alpha = color.g - max(color.r, color.b);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            alpha = color.b - max(color.r, color.g);
        } else {
            alpha = color.r - max(color.g, color.b);
        }
        alpha = alpha * (1.0 + key_tolerance_factor);
    } else if (uKeyMethod == 1) { // YUV Color Space
        float Y = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
        float U = ((color.b - Y) * 0.565) + 0.5;
        float V = ((color.r - Y) * 0.713) + 0.5;
        float Y2 = 0.299 * keyColorWorking.r + 0.587 * keyColorWorking.g + 0.114 * keyColorWorking.b;
        float U2 = ((keyColorWorking.b - Y2) * 0.565) + 0.5;
        float V2 = ((keyColorWorking.r - Y2) * 0.713) + 0.5;
        vec3 yuv_diff = abs(vec3(Y, U, V) / vec3(max(Y2, 0.001), max(U2, 0.001), max(V2, 0.001)) - 1.0);
        alpha = 1.0 - (key_tolerance_factor + 1.0) * max(yuv_diff.y, yuv_diff.z);
    } else if (uKeyMethod == 2) { // Hybrid
        float alpha_rgb = 0.0;
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            alpha_rgb = color.g - max(color.r, color.b);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            alpha_rgb = color.b - max(color.r, color.g);
        } else {
            alpha_rgb = color.r - max(color.g, color.b);
        }
        float Y = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
        float U = ((color.b - Y) * 0.565) + 0.5;
        float V = ((color.r - Y) * 0.713) + 0.5;
        float Y2 = 0.299 * keyColorWorking.r + 0.587 * keyColorWorking.g + 0.114 * keyColorWorking.b;
        float U2 = ((keyColorWorking.b - Y2) * 0.565) + 0.5;
        float V2 = ((keyColorWorking.r - Y2) * 0.713) + 0.5;
        vec3 yuv_diff = abs(vec3(Y, U, V) / vec3(max(Y2, 0.001), max(U2, 0.001), max(V2, 0.001)) - 1.0);
        float alpha_yuv = 1.0 - 2.0 * max(yuv_diff.y, yuv_diff.z);
        alpha = mix(alpha_rgb, alpha_yuv, key_tolerance_factor);
    } else { // Multi-Layer
        float baseAlpha = 0.0;
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            baseAlpha = color.g - max(color.r, color.b);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            baseAlpha = color.b - max(color.r, color.g);
        } else {
            baseAlpha = color.r - max(color.g, color.b);
        }
        baseAlpha = baseAlpha * (1.0 + key_tolerance_factor);
        float colorLuma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        float keyLuma = dot(keyColorWorking.rgb, vec3(0.2126, 0.7152, 0.0722));
        float shadowThreshold = keyLuma * 0.7;
        float highlightThreshold = keyLuma * 1.3;
        float shadowAlpha = 0.0;
        float midtoneAlpha = 0.0;
        float highlightAlpha = 0.0;
        if (colorLuma < shadowThreshold) {
            float shadowFactor = 1.0 - (colorLuma / max(shadowThreshold, 0.001));
            shadowAlpha = baseAlpha * (1.0 - (shadowFactor * 0.5));
        }
        if (colorLuma >= shadowThreshold && colorLuma <= highlightThreshold) {
            float midtoneFactor = 1.0 - (2.0 * abs(colorLuma - keyLuma) / max(highlightThreshold - shadowThreshold, 0.001));
            midtoneAlpha = baseAlpha * midtoneFactor;
        }
        if (colorLuma > highlightThreshold) {
            float highlightFactor = (colorLuma - highlightThreshold) / max(1.0 - highlightThreshold, 0.001);
            highlightAlpha = baseAlpha * (1.0 - (highlightFactor * 0.3));
        }
        alpha = max(shadowAlpha, max(midtoneAlpha, highlightAlpha));
    }

    edgeMatte = alpha;

    // Handle bright areas that should be keyed (like reflections)
    if (matte_highlights_factor > 0.0) {
        float highlightDiff = dot(color.rgb - keyColorWorking.rgb, vec3(1.0, 1.0, 1.0));
        alpha -= matte_highlights_factor * clamp(highlightDiff, 0.0, 1.0);
    }

    // Handle dark areas that should be keyed (like shadows)
    if (matte_shadows_factor > 0.0) {
        float shadowDiff = dot((1.0 - color.rgb) - (1.0 - keyColorWorking.rgb), vec3(1.0, 1.0, 1.0));
        alpha -= matte_shadows_factor * clamp(shadowDiff, 0.0, 1.0);
    }

    // Matte controls: black point, invert, white point
    alpha = alpha * matte_black_factor;
    alpha = 1.0 - alpha;
    alpha = clamp(alpha * matte_white_factor, 0.0, 1.0);

    // Hair detail enhancement
    if (hair_detail_value > 0.0) {
        float rawLuma = dot(rawColor.rgb, vec3(0.2126, 0.7152, 0.0722));
        float keyLuma = dot(keyColorWorking.rgb, vec3(0.2126, 0.7152, 0.0722));
        float edgeMask = 1.0 - abs((alpha * 2.0) - 1.0);
        edgeMask = pow(max(edgeMask, 0.0), 0.5);
        float lumaDiff = clamp(abs(rawLuma - keyLuma) * 2.0, 0.0, 1.0);
        float enhancementMask = edgeMask * lumaDiff;
        alpha = clamp(alpha + (enhancementMask * hair_detail_value), 0.0, 1.0);
    }

    // Edge softness
    if (edge_softness_factor > 0.0) {
        edgeMatte = 1.0 - abs(alpha * 2.0 - 1.0);
        edgeMatte = pow(max(edgeMatte, 0.0), 2.0 - edge_softness_factor);
        alpha = mix(alpha, 0.5, edgeMatte * edge_softness_factor);
    }

    // Spill reduction
    if (spill_reduction_factor > 0.0) {
        float spillRB = mix(color.r, color.b, spill_balance_factor);
        if (keyColorWorking.g > keyColorWorking.r && keyColorWorking.g > keyColorWorking.b) {
            if (color.g > spillRB) {
                color.g = mix(color.g, spillRB, spill_reduction_factor);
            }
            spillMatte.g = max(0.0, color.g - spillRB);
        } else if (keyColorWorking.b > keyColorWorking.r && keyColorWorking.b > keyColorWorking.g) {
            if (color.b > spillRB) {
                color.b = mix(color.b, spillRB, spill_reduction_factor);
            }
            spillMatte.b = max(0.0, color.b - spillRB);
        } else {
            float spillGB = max(color.g, color.b);
            if (color.r > spillGB) {
                color.r = mix(color.r, spillGB, spill_reduction_factor);
            }
            spillMatte.r = max(0.0, color.r - spillGB);
        }
    }

    // Restore the raw foreground color (premultiply at default 1.0)
    if (alpha > 0.0) {
        color.rgb = rawColor.rgb;
    }

    // Edge defringing
    if (defringe_value > 0.0 && alpha < 1.0) {
        float edgeDefringeValue = (1.0 - alpha) * defringe_value;
        float keyMax = max(keyColorWorking.r, max(keyColorWorking.g, keyColorWorking.b));
        if (keyColorWorking.g > 0.4 && keyColorWorking.g >= keyColorWorking.r && keyColorWorking.g >= keyColorWorking.b) {
            float adjust = keyColorWorking.g / max(0.001, keyMax);
            color.g *= (1.0 - (edgeDefringeValue * adjust));
            color.r *= (1.0 + (edgeDefringeValue * 0.2));
            color.b *= (1.0 + (edgeDefringeValue * 0.2));
        } else if (keyColorWorking.b > 0.4 && keyColorWorking.b >= keyColorWorking.g && keyColorWorking.b >= keyColorWorking.r) {
            float adjust = keyColorWorking.b / max(0.001, keyMax);
            color.b *= (1.0 - (edgeDefringeValue * adjust));
            color.r *= (1.0 + (edgeDefringeValue * 0.2));
            color.g *= (1.0 + (edgeDefringeValue * 0.2));
        } else if (keyColorWorking.r > 0.4 && keyColorWorking.r >= keyColorWorking.g && keyColorWorking.r >= keyColorWorking.b) {
            float adjust = keyColorWorking.r / max(0.001, keyMax);
            color.r *= (1.0 - (edgeDefringeValue * adjust));
            color.g *= (1.0 + (edgeDefringeValue * 0.2));
            color.b *= (1.0 + (edgeDefringeValue * 0.2));
        }
    }

    // Undo the keying hue alignment
    vec3 hsvFinal = rgb2hsv(color);
    hsvFinal.x = hsvFinal.x - hueOffset;
    color.rgb = hsv2rgb(hsvFinal);

    // Re-premultiply for the straight-to-premultiplied output
    color.rgb *= alpha;

    // Preview modes
    if (uPreviewMode == 1) {
        color.rgb = vec3(alpha);
        alpha = 1.0;
        color.rgb *= alpha;
    } else if (uPreviewMode == 2) {
        color.rgb = vec3(edgeMatte);
        alpha = 1.0;
        color.rgb *= alpha;
    } else if (uPreviewMode == 3) {
        color.rgb = spillMatte;
        alpha = 1.0;
        color.rgb *= alpha;
    }

    fragColor = vec4(color.rgb, alpha);
}
