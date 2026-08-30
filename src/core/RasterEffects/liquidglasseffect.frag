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

// Liquid glass (magnifier) distortion ported from a Shadertoy shader
// https://gist.github.com/emmachase/25af1fb66daebf0f9989c93d3c8c5fa6
// The CPU path mirrors this file step by step; the input texture is
// premultiplied alpha, so the glow multiplier is clamped against the
// alpha to keep the premultiplied invariant rgb <= a.
//
// Coordinate convention: texCoord (0,0) is the BOTTOM-left of the
// layer surface (GL). The C++ side flips the panel-facing "center y"
// (0 = top) accordingly before it reaches uCenter.

#version 330 core
layout(location = 0) out vec4 fragColor;

in vec2 texCoord;

uniform sampler2D tex;

uniform vec2 uCenter;      // GL uv space: y up
uniform vec2 uTexSize;     // render surface size in pixels
uniform float uSize;       // effect size (fraction of the height)
uniform float uShapeN;     // superellipse exponent (2 = ellipse .. 8 = squarish)
uniform float uRefraction; // distortion power
uniform float uNoise;
uniform float uGlowWeight;
uniform float uGlowBias;

const float M_E = 2.718281828459045;

// The f() function from the original (constants hardcoded)
float f_func(float x)
{
    float u_a = 0.7;
    float u_b = 2.3;
    float u_c = 5.2;
    float u_d = 6.9;
    return 1.0 - u_b * pow(u_c * M_E, -u_d * x - u_a);
}

float rand(in vec2 co)
{
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// Signed distance function for a superellipse (gradient-normalized,
// exactly as in the original)
float sdSuperellipse(vec2 p, float n, float r)
{
    vec2 p_abs = abs(p);
    float numerator = pow(p_abs.x, n) + pow(p_abs.y, n) - pow(r, n);

    float den_x = pow(p_abs.x, 2.0 * n - 2.0);
    float den_y = pow(p_abs.y, 2.0 * n - 2.0);

    float denominator = n * sqrt(den_x + den_y) + 1e-5;
    return numerator / denominator;
}

// Angular rim pattern; fades out near the center to avoid the
// atan singularity (as in the original Glow())
float glowPattern(vec2 localUV)
{
    vec2 centered = localUV * 2.0 - 1.0;
    float radius = length(centered);
    float angleFactor = smoothstep(0.0, 0.9, radius);
    return sin(atan(centered.y, centered.x) - 0.5) * angleFactor;
}

void main(void)
{
    // aspect correction as in the original: x scaled by width/height
    vec2 aspectRatio = vec2(uTexSize.x / uTexSize.y, 1.0);

    // local space centered on the effect, aspect corrected
    vec2 p = (texCoord - uCenter) * aspectRatio / uSize;

    float r = 1.0;
    float d = sdSuperellipse(p, uShapeN, r);

    // outside the shape -> pass the source pixel through untouched
    if (d > 0.0) {
        fragColor = texture(tex, texCoord);
        return;
    }

    float dist = -d;
    float fval = max(f_func(dist), 0.001);
    vec2 sampleP = p * pow(fval, uRefraction);

    // scale back to uv space
    vec2 targetUV = sampleP * uSize / aspectRatio + uCenter;
    // the original samples at targetUV + 0.1 (author's tuned offset);
    // out-of-bounds samples clamp to the edge instead of the debug
    // magenta the shadertoy version returned
    targetUV = clamp(targetUV + 0.1, 0.0, 1.0);

    vec4 color = texture(tex, targetUV);

    // static grain (screen-space, as in the original)
    vec3 noise = vec3(rand(gl_FragCoord.xy * 1e-3) - 0.5) * uNoise;

    // glow multiplier: angular pattern near the rim + bias
    vec2 localUV = (texCoord - uCenter) / uSize + 0.5;
    float glowFactor = glowPattern(localUV) * uGlowWeight *
            smoothstep(0.0, 0.06, dist) + 1.0 + uGlowBias;

    vec3 rgb = color.rgb * glowFactor + noise;
    // keep the premultiplied invariant (glow can push rgb past alpha)
    rgb = clamp(rgb, 0.0, color.a);
    fragColor = vec4(rgb, color.a);

    // safe fallback
    if (any(isnan(fragColor.rgb))) fragColor = vec4(1.0, 0.0, 1.0, 1.0);
}
