#ifndef AUTOLIGHTALGO_H
#define AUTOLIGHTALGO_H

// Auto-lighting: depth/normal driven cel shading with a levels
// section that decides whether the shadow edge is hard or soft,
// optional halftone shadows and a rim light. Deliberately free of
// Qt/Skia like celvolumealgo.h, so the whole pipeline can run from a
// plain standalone harness.
//
// Pixel format: uint32 packed 0xAARRGGBB (SkColor order), straight
// (non-premultiplied) alpha; the caller converts to/from Skia.
//
// The lighting field t (0..1; -1 = unlit passthrough, e.g. line art
// or transparent) comes from one of three sources:
//   0 depthField - normals from the AI depth map gradient, lit by a
//                  positional light (canvas crosshair + height)
//   1 celField   - the cel-volume distance-field pseudo normal
//                  (celvolumealgo.h volumetric tOut): the 三色渐变
//                  effect's shading brain reused as the "normal map"
//   2 lumaField  - the input image's own luminance, for when a
//                  shading effect (e.g. 三色渐变 radial) is stacked
//                  below this one and its output IS the light field
// The field then runs through: depth falloff -> posterize (cel bands)
// -> levels (threshold + edge hardness = the shadow mask) -> light
// color add -> shadow tint / halftone -> rim light (alpha distance
// field, lit on the side away from the light).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "celvolumealgo.h"

namespace autolight {

struct Params {
    int fieldSrc = 0;              // 0 depth, 1 celvolume, 2 luma
    // light
    float lightPos[2] = {0.3f, 0.3f}; // uv over the host content rect
    float lightZ = 800.f;          // positional light height, px (depth mode)
    float lightElevDeg = 35.f;     // light elevation (celvolume mode)
    float lightIntensity = 75.f;   // %
    float lightCol[3] = {1.f, 1.f, 1.f};
    int smooth = 3;                // field blur radius, px
    float bump = 60.f;             // depth-gradient normal strength 0..100
    bool posterize = false;
    int posterizeLevels = 8;
    // shadow
    float threshold = 50.f;        // % - the levels black point on t
    float hardness = 100.f;        // % 100 = hard binary edge, 0 = widest
    float shadowStrength = 100.f;  // %
    float shadowCol[3] = {0.40f, 0.44f, 0.78f}; // periwinkle
    bool halftone = false;
    float halftoneScale = 8.f;     // px per dot cell
    float halftoneVar = 25.f;      // % dot position jitter
    float mixOriginal = 0.f;       // %
    bool shadowOnly = false;
    // rim
    bool rim = true;
    float rimCol[3] = {1.f, 1.f, 1.f};
    float rimIntensity = 200.f;    // %
    float rimWidth = 14.f;         // px from the silhouette edge
    float rimSoftness = 4.f;       // px transition at the band limit
    float rimOffsetDeg = 0.f;      // rotate away-from-light direction
    float rimOpacity = 50.f;       // %
    bool rimOnly = false;
    // depth
    float depthFalloff = 40.f;     // % darkening toward far
    bool depthInvert = false;      // input bright = far
};

inline float clamp01(const float v) {
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

inline float smoothstepf(const float a, const float b, const float x) {
    if (std::abs(b - a) < 1e-6f) return x < a ? 0.f : 1.f;
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.f - 2.f * t);
}

inline float lumaOf(const uint32_t c) {
    return (0.2126f * ((c >> 16) & 255) + 0.7152f * ((c >> 8) & 255)
            + 0.0722f * (c & 255)) / 255.f;
}

inline float hash2(const float a, const float b) {
    const float h = std::sin(a * 12.9898f + b * 78.233f) * 43758.5453f;
    return h - std::floor(h);
}

// invert the JET colormap of the AI depth panel's false-color output
// (r = clamp01(1.5-|4v-3|), g = clamp01(1.5-|4v-2|), b = clamp01(1.5-|4v-1|));
// gray pixels never reach this (the caller short-circuits r==g==b)
inline float jetDecode(const float r, const float g, const float b) {
    if (g > 0.002f && g < 0.998f) {
        // two roots on the g ramp; b >= r picks the low half
        return (b >= r) ? (0.5f + g) * 0.25f : (3.5f - g) * 0.25f;
    }
    if (g >= 0.998f) return clamp01(0.5f + (r - b) * 0.125f);
    // g ~ 0: v in [0,0.125] (b ramp) or [0.875,1] (r ramp)
    return (r > 0.002f) ? clamp01((4.5f - r) * 0.25f)
                        : clamp01((b - 0.5f) * 0.25f);
}

inline void packStraight(const float r, const float g, const float b,
                         const uint32_t a, uint32_t& out) {
    const auto q = [](const float f) {
        return (f <= 0.f) ? 0u : (f >= 1.f) ? 255u : uint32_t(f * 255.f + 0.5f);
    };
    out = (a << 24) | (q(r) << 16) | (q(g) << 8) | q(b);
}

// src: packed straight-alpha image; depth: per-pixel 0..1 aligned to
// src (1 = near) or null; lumaField: per-pixel 0..1 lighting field
// from an external layer aligned to src (-1 = no data) or null; dst
// receives the composited result with the same alpha as src (except
// the "only" modes, which carry their mask in the alpha)
inline void compute(const uint32_t* const src, const float* const depth,
                    const float* const lumaField,
                    const int w, const int h, const Params& p,
                    uint32_t* const dst)
{
    const size_t n = size_t(w) * size_t(h);
    if (n == 0) return;

    // ---- 0. normalized depth (1 = near) --------------------------------
    std::vector<float> dn;
    const bool haveDepth = depth != nullptr;
    if (haveDepth) {
        dn.resize(n);
        for (size_t i = 0; i < n; i++) {
            dn[i] = p.depthInvert ? 1.f - clamp01(depth[i]) : clamp01(depth[i]);
        }
    }

    // ---- 1. lighting field t -------------------------------------------
    std::vector<float> t(n, -1.f);
    if (p.fieldSrc == 1) {
        // the 三色渐变 volumetric field: segmentation + distance-field
        // pseudo normal lambert; the colors are discarded, only the
        // lambert term survives as the shadow/lighting driver
        celvolume::Params cp;
        cp.shadeMode = celvolume::ShadeVolumetric;
        cp.smooth = std::max(0, p.smooth);
        cp.lightElevDeg = p.lightElevDeg;
        cp.bump = p.bump;
        cp.ao = 20.f;
        cp.aoWidth = 6.f;
        // crosshair azimuth: direction from the image center toward
        // the light (0 = right, 90 = up, screen y is down)
        const float acx = (p.lightPos[0] - 0.5f) * float(w);
        const float acy = (p.lightPos[1] - 0.5f) * float(h);
        cp.lightAngleDeg = std::atan2(-acy, acx) * 57.2957795f;
        std::vector<uint32_t> scratch(n);
        celvolume::compute(src, w, h, cp, scratch.data(), nullptr, &t);
    } else if (p.fieldSrc == 0 && haveDepth) {
        // depth-gradient pseudo normals + positional light; the depth
        // is box-blurred first so pixel staircases do not spike the
        // gradient
        std::vector<float> db(dn);
        const int r = std::max(0, p.smooth);
        if (r > 0) {
            std::vector<float> tmp(n);
            for (int pass = 0; pass < 2; pass++) {
                const std::vector<float>& from = (pass == 0) ? db : tmp;
                std::vector<float>& to = (pass == 0) ? tmp : db;
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        const size_t i = size_t(y) * w + x;
                        const int c = (pass == 0) ? x : y;
                        const int cN = (pass == 0) ? w : h;
                        const int ca = std::max(0, c - r);
                        const int cb = std::min(cN - 1, c + r);
                        float sum = 0.f;
                        for (int cc = ca; cc <= cb; cc++) {
                            const size_t j = (pass == 0)
                                        ? size_t(y) * w + cc
                                        : size_t(cc) * w + x;
                            sum += from[j];
                        }
                        to[i] = sum / float(cb - ca + 1);
                    }
                }
            }
        }
        const float lx = p.lightPos[0] * float(w);
        const float ly = p.lightPos[1] * float(h);
        const float lz = std::max(1.f, p.lightZ);
        const float k = std::pow(std::max(0.f, p.bump) / 100.f, 1.3f) * 5.f;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const size_t i = size_t(y) * w + x;
                if (((src[i] >> 24) & 255) < 128) continue;
                const size_t il = (x > 0) ? i - 1 : i;
                const size_t ir = (x < w - 1) ? i + 1 : i;
                const size_t iu = (y > 0) ? i - w : i;
                const size_t idn = (y < h - 1) ? i + w : i;
                const float gx = (db[ir] - db[il]) * k;
                const float gy = (db[idn] - db[iu]) * k;
                const float invN = 1.f / std::sqrt(gx * gx + gy * gy + 1.f);
                float dx = lx - (float(x) + 0.5f);
                float dy = ly - (float(y) + 0.5f);
                const float dl = 1.f / std::sqrt(dx * dx + dy * dy + lz * lz);
                dx *= dl; dy *= dl;
                const float dz = lz * dl;
                // n = (-gx, -gy, 1) * invN
                const float nDotL = (-gx * dx - gy * dy + dz) * invN;
                t[i] = 0.5f + 0.5f * nDotL;
            }
        }
    } else if (p.fieldSrc == 2 && lumaField) {
        // a luminance layer was picked: its (resampled) luminance IS
        // the light field; -1 cells (transparent there) pass through
        for (size_t i = 0; i < n; i++) {
            if (((src[i] >> 24) & 255) < 128) continue;
            t[i] = lumaField[i];
        }
    } else {
        // no depth picked, or luma mode: the input's own luminance
        // (a shading effect stacked below becomes the light field)
        for (size_t i = 0; i < n; i++) {
            if (((src[i] >> 24) & 255) < 128) continue;
            t[i] = lumaOf(src[i]);
        }
    }

    // ---- 2. depth falloff ------------------------------------------------
    if (haveDepth && p.depthFalloff > 0.f) {
        const float f = clamp01(p.depthFalloff / 100.f);
        for (size_t i = 0; i < n; i++) {
            if (t[i] < 0.f) continue;
            t[i] *= 1.f - f * (1.f - dn[i]);
        }
    }

    // ---- 3. posterize (cel bands) ----------------------------------------
    if (p.posterize && p.posterizeLevels >= 2) {
        const float L = float(p.posterizeLevels - 1);
        for (size_t i = 0; i < n; i++) {
            if (t[i] < 0.f) continue;
            t[i] = std::round(clamp01(t[i]) * L) / L;
        }
    }

    // ---- 4. rim distance field (chamfer over the alpha silhouette) -------
    const bool wantRim = p.rimOnly ||
            (p.rim && p.rimIntensity > 0.f && p.rimWidth >= 1.f
             && p.rimOpacity > 0.f);
    std::vector<float> edist;
    if (wantRim) {
        edist.assign(n, 0.f);
        const float BIG = 1e9f;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const size_t i = size_t(y) * w + x;
                if (((src[i] >> 24) & 255) < 128) continue;
                bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
                if (!edge) {
                    edge = ((src[i - 1] >> 24) & 255) < 128
                        || ((src[i + 1] >> 24) & 255) < 128
                        || ((src[i - w] >> 24) & 255) < 128
                        || ((src[i + w] >> 24) & 255) < 128;
                }
                edist[i] = edge ? 0.5f : BIG;
            }
        }
        const float D1 = 1.f;
        const float D2 = 1.41421356f;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const size_t i = size_t(y) * w + x;
                if (edist[i] <= 0.5f) continue;
                float d = edist[i];
                if (x > 0)     d = std::min(d, edist[i - 1] + D1);
                if (y > 0)     d = std::min(d, edist[i - w] + D1);
                if (x > 0 && y > 0)
                    d = std::min(d, edist[i - w - 1] + D2);
                if (x < w - 1 && y > 0)
                    d = std::min(d, edist[i - w + 1] + D2);
                edist[i] = d;
            }
        }
        for (int y = h - 1; y >= 0; y--) {
            for (int x = w - 1; x >= 0; x--) {
                const size_t i = size_t(y) * w + x;
                if (edist[i] <= 0.5f) continue;
                float d = edist[i];
                if (x < w - 1) d = std::min(d, edist[i + 1] + D1);
                if (y < h - 1) d = std::min(d, edist[i + w] + D1);
                if (x < w - 1 && y < h - 1)
                    d = std::min(d, edist[i + w + 1] + D2);
                if (x > 0 && y < h - 1)
                    d = std::min(d, edist[i + w - 1] + D2);
                edist[i] = d;
            }
        }
    }

    // ---- 5. composite ------------------------------------------------------
    const float thr = clamp01(p.threshold / 100.f);
    const float sw = (1.f - clamp01(p.hardness / 100.f)) * 0.5f;
    const float litK = p.lightIntensity / 100.f;
    const float mixO = clamp01(p.mixOriginal / 100.f);
    const float shStr = clamp01(p.shadowStrength / 100.f);
    const float rimInt = clamp01(p.rimIntensity / 100.f);
    const float rimOp = clamp01(p.rimOpacity / 100.f);
    const float cell = std::max(2.f, p.halftoneScale);
    const float jitter = clamp01(p.halftoneVar / 100.f) * 0.8f;
    const float rimRad = p.rimOffsetDeg * 0.01745329f;
    const float rimCa = std::cos(rimRad);
    const float rimSa = std::sin(rimRad);
    const float lx = p.lightPos[0] * float(w);
    const float ly = p.lightPos[1] * float(h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const size_t i = size_t(y) * w + x;
            const uint32_t sC = src[i];
            const uint32_t alpha = (sC >> 24) & 255;
            uint32_t o = sC;
            if (alpha >= 128 && t[i] >= 0.f) {
                const float tv = clamp01(t[i]);
                // levels: t below the threshold is shadow; hardness
                // sizes the transition band (100% = binary hard edge)
                const float mask = 1.f - smoothstepf(thr - sw, thr + sw, tv);
                const float m2 = mask * shStr;

                const float sr = float((sC >> 16) & 255) / 255.f;
                const float sg = float((sC >> 8) & 255) / 255.f;
                const float sb = float(sC & 255) / 255.f;

                // lit side: add the light color, only outside the shadow
                const float lit = std::max(0.f, tv - 0.5f) * 2.f
                                  * litK * (1.f - mask);
                const float litR = clamp01(sr + p.lightCol[0] * lit);
                const float litG = clamp01(sg + p.lightCol[1] * lit);
                const float litB = clamp01(sb + p.lightCol[2] * lit);
                // shadow side: multiply by the shadow color
                const float shR = litR * p.shadowCol[0];
                const float shG = litG * p.shadowCol[1];
                const float shB = litB * p.shadowCol[2];

                bool halftoneOff = false;
                if (p.halftone && m2 > 0.002f) {
                    const float fx = float(x) / cell;
                    const float fy = float(y) / cell;
                    const float cx = std::floor(fx);
                    const float cy = std::floor(fy);
                    const float jx = (hash2(cx, cy) - 0.5f) * jitter;
                    const float jy = (hash2(cx + 7.3f, cy + 3.1f) - 0.5f) * jitter;
                    const float dx = fx - cx - 0.5f + jx;
                    const float dy = fy - cy - 0.5f + jy;
                    const float d = std::sqrt(dx * dx + dy * dy) * 2.f;
                    halftoneOff = m2 <= d;
                }

                float oR = halftoneOff ? litR : litR + (shR - litR) * m2;
                float oG = halftoneOff ? litG : litG + (shG - litG) * m2;
                float oB = halftoneOff ? litB : litB + (shB - litB) * m2;
                // blend the untouched original back in
                oR += (sr - oR) * mixO;
                oG += (sg - oG) * mixO;
                oB += (sb - oB) * mixO;

                if (p.shadowOnly) {
                    // isolate the shadow: the mask rides the alpha so
                    // the layer composites as a shadow-only pass
                    packStraight(p.shadowCol[0], p.shadowCol[1],
                                 p.shadowCol[2],
                                 uint32_t(std::round(float(alpha) * m2)), o);
                    dst[i] = o;
                    continue;
                }

                if (wantRim) {
                    const float width = std::max(1.f, p.rimWidth);
                    const float soft = std::max(0.f, p.rimSoftness);
                    const float band = 1.f - smoothstepf(
                                width - soft, width + soft, edist[i]);
                    if (band > 0.001f) {
                        // inward normal from the distance gradient
                        const size_t il = (x > 0) ? i - 1 : i;
                        const size_t ir = (x < w - 1) ? i + 1 : i;
                        const size_t iu = (y > 0) ? i - w : i;
                        const size_t idn = (y < h - 1) ? i + w : i;
                        float gxn = edist[ir] - edist[il];
                        float gyn = edist[idn] - edist[iu];
                        const float gl = std::sqrt(gxn * gxn + gyn * gyn);
                        // away-from-light direction, rotated by the
                        // rim offset
                        float ax = float(x) + 0.5f - lx;
                        float ay = float(y) + 0.5f - ly;
                        const float al = std::sqrt(ax * ax + ay * ay);
                        if (gl > 1e-4f && al > 1e-3f) {
                            gxn /= gl; gyn /= gl;
                            ax /= al; ay /= al;
                            const float rx = ax * rimCa - ay * rimSa;
                            const float ry = ax * rimSa + ay * rimCa;
                            // the rim sits where the surface points
                            // AWAY from the light
                            const float facing =
                                    std::max(0.f, -(gxn * rx + gyn * ry));
                            const float rimA = band * facing;
                            if (p.rimOnly) {
                                packStraight(p.rimCol[0], p.rimCol[1],
                                             p.rimCol[2],
                                             uint32_t(std::round(
                                                 float(alpha)
                                                 * clamp01(rimA * rimInt)
                                                 * rimOp)), o);
                                dst[i] = o;
                                continue;
                            }
                            if (rimA > 0.001f) {
                                // additive glow, clamped
                                const float add = rimA * rimInt * rimOp;
                                oR = clamp01(oR + p.rimCol[0] * add);
                                oG = clamp01(oG + p.rimCol[1] * add);
                                oB = clamp01(oB + p.rimCol[2] * add);
                            }
                        }
                    }
                }

                packStraight(oR, oG, oB, alpha, o);
            }
            dst[i] = o;
        }
    }
}

} // namespace autolight

#endif // AUTOLIGHTALGO_H
