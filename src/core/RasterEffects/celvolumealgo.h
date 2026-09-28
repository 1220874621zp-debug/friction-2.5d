#ifndef CELVOLUMEALGO_H
#define CELVOLUMEALGO_H

// Flat-color region segmentation + volumetric 4-stop ramp, the core
// of the cel-volume effect. Deliberately free of Qt/Skia so the whole
// pipeline can be exercised from a plain standalone test harness.
//
// Pixel format: uint32 packed 0xAARRGGBB (SkColor order), straight
// (non-premultiplied) alpha. The caller converts to/from whatever
// Skia gives it.
//
// Pipeline:
//   1. dominant flat colors  - greedy frequency clustering with a
//      merge radius, so jpeg/anti-aliased jitter around each flat
//      fill collapses onto one dominant color
//   2. region labeling       - 8-connected components per dominant
//      color; the same color reappearing in hair and shirt becomes
//      two regions with independent shading
//   3. inner distance field  - chamfer(1, sqrt2) two-pass inside each
//      region, then masked box blur so gradients flow instead of
//      following pixel-level staircases
//   4. volume shading        - the blurred distance field is read as
//      a height field; its central-difference gradient gives a
//      pseudo surface normal, lambert against the user light, plus a
//      crease-style ambient term from the distance itself
//   5. 4-stop ramp           - shade / base / bright / highlight with
//      soft transition bands (hard cel steps .. full airbrush blend)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace celvolume {

struct Params {
    // segmentation
    float colorTol = 14.f;    // merge radius for flat-color clustering (rgb units)
    int minArea = 40;         // regions below this pixel count pass through
    bool protectDark = true;  // keep line-art-ish dark regions flat
    float darkLuma = 0.30f;   // luma below which a region reads as line art
    // volume
    float lightAngleDeg = 135.f; // 0 = right, 90 = up (screen y is down)
    float lightElevDeg = 35.f;   // light elevation, z contribution
    float bump = 60.f;           // 0 = flat (ao only) .. 100 = strong ridges
    float ao = 40.f;             // crease darkening strength, 0..100
    float aoWidth = 6.f;         // crease darkening band, px
    int smooth = 3;              // distance-field blur radius, px
    // 4-stop ramp
    float hiStrength = 62.f;     // highlight lift toward warm white, 0..100
    float hiWarm = 40.f;         // warmness of the highlight tint, 0..100
    float brightStrength = 30.f; // midtone lift, 0..100
    float shadeStrength = 38.f;  // shadow drop, 0..100
    float shadeHue = -18.f;      // shadow hue shift, degrees (negative = cooler)
    float softness = 45.f;       // ramp transition band width, 0..100
    float mix = 100.f;           // 0 = original .. 100 = fully ramped
};

struct RegionStat {
    int area = 0;
    uint32_t color = 0;   // dominant color of the region
    float maxDist = 1.f;  // deepest inner distance after blur
    bool flat = false;    // passed through untouched (tiny / line art)
};

namespace detail {

inline float clamp01(const float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline float smoothStep(const float e0, const float e1, const float x) {
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

struct rgbF { float r, g, b; };

inline rgbF unpack(const uint32_t c) {
    return { ((c >> 16) & 255) / 255.f,
             ((c >> 8) & 255) / 255.f,
             (c & 255) / 255.f };
}

inline uint32_t pack(const rgbF& v) {
    const auto q = [](const float f) {
        return (f <= 0.f) ? 0u : (f >= 1.f) ? 255u : uint32_t(f * 255.f + 0.5f);
    };
    return 0xFF000000u | (q(v.r) << 16) | (q(v.g) << 8) | q(v.b);
}

inline float lumaOf(const uint32_t c) {
    const rgbF v = unpack(c);
    return 0.2126f * v.r + 0.7152f * v.g + 0.0722f * v.b;
}

// rgb <-> hsv (h in [0,1)); used for the shadow hue shift only
inline void rgbToHsv(const rgbF& in, float& h, float& s, float& v) {
    const float mx = std::max(in.r, std::max(in.g, in.b));
    const float mn = std::min(in.r, std::min(in.g, in.b));
    const float d = mx - mn;
    v = mx;
    s = mx <= 0.f ? 0.f : d / mx;
    if (d <= 1e-6f) { h = 0.f; return; }
    if (mx == in.r)      h = (in.g - in.b) / d + (in.g < in.b ? 6.f : 0.f);
    else if (mx == in.g) h = (in.b - in.r) / d + 2.f;
    else                 h = (in.r - in.g) / d + 4.f;
    h /= 6.f;
}

inline rgbF hsvToRgb(float h, float s, float v) {
    h = h - std::floor(h);
    const int i = int(h * 6.f);
    const float f = h * 6.f - i;
    const float p = v * (1.f - s);
    const float q = v * (1.f - f * s);
    const float t = v * (1.f - (1.f - f) * s);
    switch (i % 6) {
    case 0: return { v, t, p };
    case 1: return { q, v, p };
    case 2: return { p, v, t };
    case 3: return { p, q, v };
    case 4: return { t, p, v };
    default: return { v, p, q };
    }
}

inline rgbF mix(const rgbF& a, const rgbF& b, const float t) {
    return { a.r + (b.r - a.r) * t,
             a.g + (b.g - a.g) * t,
             a.b + (b.b - a.b) * t };
}

// the four ramp colors derived from one region's dominant color
struct Ramp {
    rgbF shade, base, bright, hi;
};

inline Ramp makeRamp(const uint32_t color, const Params& p) {
    const rgbF base = unpack(color);

    // shadow: hue-shifted, darker, slightly more saturated so it reads
    // "thicker" than a plain multiply
    float h, s, v;
    rgbToHsv(base, h, s, v);
    const float ss = clamp01(s + (1.f - s) * 0.25f * (p.shadeStrength / 100.f));
    const float vs = clamp01(v * (1.f - 0.9f * p.shadeStrength / 100.f));
    const rgbF shade = hsvToRgb(h + p.shadeHue / 360.f, ss, vs);

    // bright: gentle lift toward white, slightly warm
    const rgbF bright = mix(base, rgbF{ 1.f, 0.995f, 0.97f },
                            0.85f * p.brightStrength / 100.f);

    // highlight: strong lift toward a warm-white whose temperature the
    // user controls
    const float w = p.hiWarm / 100.f;
    const rgbF warm = { 1.f, 1.f - 0.02f * w, 1.f - 0.10f * w };
    const rgbF hi = mix(base, warm, p.hiStrength / 100.f);

    return { shade, base, bright, hi };
}

} // namespace detail

// Compute the full pipeline. src and dst may not alias; dst receives
// the same alpha as src (segmentation treats alpha < 128 as
// background). stats, when not null, reports one entry per region.
// sdOut, when not null, receives the raw ramp coordinate per pixel
// (test/debug hook; -1 for background).
inline void compute(const uint32_t* const src, const int w, const int h,
                    const Params& p, uint32_t* const dst,
                    std::vector<RegionStat>* const stats = nullptr,
                    std::vector<float>* const sdOut = nullptr,
                    std::vector<float>* const distOut = nullptr)
{
    const size_t n = size_t(w) * size_t(h);
    if (n == 0) return;

    // ---- 1. dominant flat colors --------------------------------------
    std::unordered_map<uint32_t, int> hist;
    hist.reserve(4096);
    for (size_t i = 0; i < n; i++) {
        if (((src[i] >> 24) & 255) < 128) continue;
        ++hist[src[i] & 0x00FFFFFFu];
    }
    std::vector<std::pair<uint32_t, int>> items(hist.begin(), hist.end());
    std::sort(items.begin(), items.end(),
              [](const std::pair<uint32_t, int>& a,
                 const std::pair<uint32_t, int>& b) {
                  return a.second > b.second;
              });

    std::vector<uint32_t> doms;
    const float tol2 = 3.f * p.colorTol * p.colorTol;
    {
        long total = 0;
        for (const auto& it : items) total += it.second;
        long covered = 0;
        for (const auto& it : items) {
            bool merged = false;
            for (const uint32_t d : doms) {
                const int dr = int((it.first >> 16) & 255) - int((d >> 16) & 255);
                const int dg = int((it.first >> 8) & 255) - int((d >> 8) & 255);
                const int db = int(it.first & 255) - int(d & 255);
                if (float(dr * dr + dg * dg + db * db) <= tol2) { merged = true; break; }
            }
            if (merged) { covered += it.second; continue; }
            if (int(doms.size()) >= 48) break; // rest falls back to nearest
            doms.push_back(it.first);
            covered += it.second;
            if (total > 0 && covered >= long(0.998 * total)) break;
        }
    }
    if (doms.empty()) { // nothing opaque to segment
        for (size_t i = 0; i < n; i++) dst[i] = src[i];
        return;
    }

    // color id per pixel (cached; most pixels hit the same few colors)
    std::vector<int32_t> colorId(n, -1);
    std::unordered_map<uint32_t, int32_t> idCache;
    idCache.reserve(1024);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const size_t i = size_t(y) * w + x;
            const uint32_t c = src[i];
            if (((c >> 24) & 255) < 128) continue;
            const uint32_t rgb = c & 0x00FFFFFFu;
            const auto it = idCache.find(rgb);
            if (it != idCache.end()) { colorId[i] = it->second; continue; }
            int32_t best = 0;
            float bestD = 1e30f;
            for (size_t k = 0; k < doms.size(); k++) {
                const int dr = int((rgb >> 16) & 255) - int((doms[k] >> 16) & 255);
                const int dg = int((rgb >> 8) & 255) - int((doms[k] >> 8) & 255);
                const int db = int(rgb & 255) - int(doms[k] & 255);
                const float d = float(dr * dr + dg * dg + db * db);
                if (d < bestD) { bestD = d; best = int32_t(k); }
            }
            idCache[rgb] = best;
            colorId[i] = best;
        }
    }

    // ---- 2. region labeling (8-connected per dominant color) ---------
    std::vector<int32_t> label(n, -1);
    std::vector<RegionStat> regions;
    std::vector<int32_t> stack; // flood fill worklist (indices)
    for (int y0 = 0; y0 < h; y0++) {
        for (int x0 = 0; x0 < w; x0++) {
            const size_t i0 = size_t(y0) * w + x0;
            if (label[i0] >= 0 || colorId[i0] < 0) continue;
            const int id = int(regions.size());
            const int32_t cid = colorId[i0];
            RegionStat st;
            st.color = doms[size_t(cid)];
            label[i0] = id;
            stack.clear();
            stack.push_back(int32_t(i0));
            while (!stack.empty()) {
                const size_t i = size_t(stack.back());
                stack.pop_back();
                st.area++;
                const int x = int(i % w);
                const int y = int(i / w);
                for (int dy = -1; dy <= 1; dy++) {
                    const int ny = y + dy;
                    if (ny < 0 || ny >= h) continue;
                    for (int dx = -1; dx <= 1; dx++) {
                        const int nx = x + dx;
                        if (nx < 0 || nx >= w) continue;
                        const size_t j = size_t(ny) * w + nx;
                        if (label[j] >= 0 || colorId[j] != cid) continue;
                        label[j] = id;
                        stack.push_back(int32_t(j));
                    }
                }
            }
            st.flat = st.area < p.minArea
                      || (p.protectDark && detail::lumaOf(st.color) < p.darkLuma);
            regions.push_back(st);
        }
    }

    // ---- 3. inner distance field --------------------------------------
    std::vector<float> dist(n, 0.f);
    {
        const float BIG = 1e9f;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const size_t i = size_t(y) * w + x;
                const int32_t L = label[i];
                if (L < 0) { dist[i] = 0.f; continue; }
                bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
                if (!edge) {
                    edge = label[i - 1] != L || label[i + 1] != L
                        || label[i - w] != L || label[i + w] != L;
                }
                dist[i] = edge ? 0.5f : BIG;
            }
        }
        const float D1 = 1.f;
        const float D2 = 1.41421356f;
        auto same = [&](const size_t a, const size_t b) {
            return label[a] == label[b] && label[a] >= 0;
        };
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const size_t i = size_t(y) * w + x;
                if (label[i] < 0) continue;
                float d = dist[i];
                if (x > 0 && same(i, i - 1))     d = std::min(d, dist[i - 1] + D1);
                if (y > 0 && same(i, i - w))     d = std::min(d, dist[i - w] + D1);
                if (x > 0 && y > 0 && same(i, i - w - 1))
                    d = std::min(d, dist[i - w - 1] + D2);
                if (x < w - 1 && y > 0 && same(i, i - w + 1))
                    d = std::min(d, dist[i - w + 1] + D2);
                dist[i] = d;
            }
        }
        for (int y = h - 1; y >= 0; y--) {
            for (int x = w - 1; x >= 0; x--) {
                const size_t i = size_t(y) * w + x;
                if (label[i] < 0) continue;
                float d = dist[i];
                if (x < w - 1 && same(i, i + 1)) d = std::min(d, dist[i + 1] + D1);
                if (y < h - 1 && same(i, i + w)) d = std::min(d, dist[i + w] + D1);
                if (x < w - 1 && y < h - 1 && same(i, i + w + 1))
                    d = std::min(d, dist[i + w + 1] + D2);
                if (x > 0 && y < h - 1 && same(i, i + w - 1))
                    d = std::min(d, dist[i + w - 1] + D2);
                dist[i] = d;
            }
        }
    }

    // masked box blur so gradients ignore pixel staircases; averaging
    // only same-label pixels keeps neighboring regions from bleeding
    if (p.smooth > 0) {
        const int r = p.smooth;
        std::vector<float> tmp(n);
        for (int pass = 0; pass < 2; pass++) { // horizontal then vertical
            std::vector<float>& from = (pass == 0) ? dist : tmp;
            std::vector<float>& to = (pass == 0) ? tmp : dist;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const size_t i = size_t(y) * w + x;
                    if (label[i] < 0) { to[i] = 0.f; continue; }
                    const int32_t L = label[i];
                    // window walks the blur axis: x on the horizontal
                    // pass, y on the vertical one
                    const int c = (pass == 0) ? x : y;
                    const int cN = (pass == 0) ? w : h;
                    float sum = 0.f;
                    int cnt = 0;
                    const int ca = std::max(0, c - r);
                    const int cb = std::min(cN - 1, c + r);
                    for (int cc = ca; cc <= cb; cc++) {
                        const size_t j = (pass == 0) ? size_t(y) * w + cc
                                                     : size_t(cc) * w + x;
                        if (label[j] != L) continue;
                        sum += from[j];
                        cnt++;
                    }
                    to[i] = cnt > 0 ? sum / cnt : from[i];
                }
            }
        }
    }

    // per-region depth (post-blur) for the ao term
    for (size_t i = 0; i < n; i++) {
        const int32_t L = label[i];
        if (L < 0) continue;
        if (dist[i] > regions[size_t(L)].maxDist) {
            regions[size_t(L)].maxDist = dist[i];
        }
    }
    for (auto& st : regions) st.maxDist = std::max(st.maxDist, 1.f);

    // thin slivers (anti-aliased rims around shapes, stray outline
    // fragments) never develop a meaningful gradient - their shading
    // is pure noise, so they pass through like flat regions
    for (auto& st : regions) {
        if (st.maxDist < 4.5f) st.flat = true;
    }

    if (stats) *stats = regions;
    if (sdOut) sdOut->assign(n, -1.f);
    if (distOut) *distOut = dist;

    // ---- 4/5. shading + ramp -----------------------------------------
    using detail::rgbF;
    // one ramp per dominant color, not per region - same-fill regions
    // shift identically, which is how flat art recolors read
    std::vector<detail::Ramp> ramps(doms.size());
    for (size_t k = 0; k < doms.size(); k++) ramps[k] = detail::makeRamp(doms[k], p);

    const float th = p.lightAngleDeg * 3.14159265f / 180.f;
    const float lx = std::cos(th);
    const float ly = -std::sin(th); // screen y is down
    const float lz = std::tan(p.lightElevDeg * 3.14159265f / 180.f);
    const float invLLen = 1.f / std::sqrt(lx * lx + ly * ly + lz * lz);
    const rgbF L3 = { lx * invLLen, ly * invLLen, lz * invLLen };

    // height-field steepness; 0 maps to a flat sheet (normal = +z)
    const float k = std::pow(p.bump / 100.f, 1.3f) * 5.f;
    const float aoStr = p.ao / 100.f;
    const float aoW = std::max(1.f, p.aoWidth);
    const float halfBand = 0.5f * (0.06f + p.softness / 100.f * 0.94f);
    const float mixAmt = detail::clamp01(p.mix / 100.f);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const size_t i = size_t(y) * w + x;
            const uint32_t s = src[i];
            const int32_t L = label[i];
            if (L < 0 || regions[size_t(L)].flat) { dst[i] = s; continue; }

            const size_t il = (x > 0) ? i - 1 : i;
            const size_t ir = (x < w - 1) ? i + 1 : i;
            const size_t iu = (y > 0) ? i - w : i;
            const size_t idn = (y < h - 1) ? i + w : i;
            const float gx = (dist[ir] - dist[il]) * k;
            const float gy = (dist[idn] - dist[iu]) * k;
            const float invN = 1.f / std::sqrt(gx * gx + gy * gy + 1.f);
            const float nx = -gx * invN;
            const float ny = -gy * invN;
            const float nz = invN;
            const float nDotL = nx * L3.r + ny * L3.g + nz * L3.b;

            float sd = 0.5f + 0.5f * nDotL;

            // crease shading: a fixed pixel band around any region
            // edge darkens, regardless of region size, so both a huge
            // dress and a tiny eye-white get the same contact shadow
            const float depth = detail::clamp01(dist[i] / aoW);
            sd -= aoStr * 0.5f * (1.f - depth) * (1.f - depth);
            // spread the lambert term so lit slopes reach the
            // highlight stop and unlit slopes the shadow stop
            sd = 0.5f + (sd - 0.5f) * 1.15f;
            sd = detail::clamp01(sd);
            if (sdOut) (*sdOut)[i] = sd;

            // 4-stop gradient: shade @0 .. base @0.36 .. bright @0.70 .. hi @1
            const detail::Ramp& rp = ramps[size_t(colorId[i])];
            static const float stops[3] = { 0.36f, 0.70f, 1.f };
            const rgbF* cols[4] = { &rp.shade, &rp.base, &rp.bright, &rp.hi };
            rgbF out = *cols[0];
            float prev = 0.f;
            for (int t = 0; t < 3; t++) {
                const float tt = detail::clamp01((sd - prev) / (stops[t] - prev));
                const float m = detail::smoothStep(0.5f - halfBand,
                                                   0.5f + halfBand, tt);
                out = detail::mix(out, *cols[t + 1], m);
                prev = stops[t];
            }

            const rgbF orig = detail::unpack(s);
            const rgbF final = detail::mix(orig, out, mixAmt);
            dst[i] = (s & 0xFF000000u) | (detail::pack(final) & 0x00FFFFFFu);
        }
    }
}

} // namespace celvolume

#endif // CELVOLUMEALGO_H
