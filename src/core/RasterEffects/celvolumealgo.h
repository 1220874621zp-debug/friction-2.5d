#ifndef CELVOLUMEALGO_H
#define CELVOLUMEALGO_H

// Flat-color region segmentation + 3-stop gradient re-shading, the
// core of the cel-volume effect. Deliberately free of Qt/Skia so the
// whole pipeline can be exercised from a plain standalone test
// harness.
//
// Pixel format: uint32 packed 0xAARRGGBB (SkColor order), straight
// (non-premultiplied) alpha. The caller converts to/from whatever
// Skia gives it.
//
// The look (reverse-engineered pixel-by-pixel from the user's
// reference pair, a flat-cel portrait and its gradient treatment):
//   - the original hues are fully replaced; the same spot in hair,
//     skin and clothes receives the SAME hue, i.e. the hue comes
//     from a spatial field, not from the source color
//   - the field is one big hue journey along a diagonal: warm end
//     (~350 deg red) at the upper right through bright purple
//     (~295 deg) to a cool end (~250 deg blue-violet) at the lower
//     left - the "3 colors" are three user-picked stops on that
//     journey, interpolated in HSV so the middle stays vivid
//   - saturation stays uniformly high (~0.9), value follows the
//     stops (slight falloff toward the cool end)
//   - thin strokes (lashes, fine lines) pass through untouched;
//     large dark blocks (hair) DO get recolored
//   - the flat backdrop can be darkened toward black
//
// Pipeline:
//   1. dominant flat colors  - greedy frequency clustering with a
//      merge radius (jpeg/anti-aliased jitter collapses)
//   2. region labeling       - 8-connected components per dominant
//      color; the same color in hair and shirt becomes two regions
//   3. inner distance field  - chamfer(1, sqrt2) two passes + masked
//      box blur; used for the thin-sliver test and the optional
//      volumetric shading mode
//   3.5 region merging      - colored specks and strand slivers
//      fold into their largest adjacent region, so one visual block
//      (hair with its inner strands, a dress with folds) is ONE
//      region; only low-saturation line art stays separate
//   4a. radial mode (new)   - every region gets its own CIRCULAR
//      3-stop gradient: t = 1 at the region centroid falling to 0
//      at its outer radius; color = HSV 3-stop interpolation
//   4b. volumetric mode      - distance-field pseudo normal lambert
//      + crease ao produce t, then the same 3-stop interpolation

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace celvolume {

// shading mode
enum ShadeMode { ShadeRadial = 0, ShadeVolumetric = 1 };

struct Params {
    // segmentation
    float colorTol = 32.f;    // merge radius for flat-color clustering (rgb units)
    int minArea = 40;         // regions below this pixel count merge into neighbors
    // radial shading
    int shadeMode = ShadeRadial;
    // stops calibrated against the reference treatment: saturated
    // pure red / electric purple / blue-violet, all s > 0.9
    float colWarm[3] = { 0.80f, 0.10f, 0.04f };  // region-center stop, rgb 0..1
    float colMid[3]  = { 0.58f, 0.03f, 0.72f };  // middle stop
    float colCool[3] = { 0.26f, 0.07f, 0.76f };  // region-edge stop
    float gradGamma = 1.8f;    // t response curve; >1 keeps the center
                               // color wide before falling to the edge
    float mix = 100.f;         // 0 = original .. 100 = fully re-tinted
    float bgDarken = 0.f;      // backdrop (largest border region) darkening, 0..100
    // volumetric shading (shadeMode == ShadeVolumetric)
    float lightAngleDeg = 135.f; // 0 = right, 90 = up (screen y is down)
    float lightElevDeg = 35.f;   // light elevation, z contribution
    float bump = 60.f;           // 0 = flat (ao only) .. 100 = strong ridges
    float ao = 40.f;             // crease darkening strength, 0..100
    float aoWidth = 6.f;         // crease darkening band, px
    int smooth = 3;              // distance-field blur radius, px
};

struct RegionStat {
    int area = 0;
    uint32_t color = 0;   // dominant color of the region
    float maxDist = 1.f;  // deepest inner distance after blur
    bool flat = false;    // passed through untouched (thin sliver / line art)
    bool tiny = false;    // speck/sliver region: folds into its largest neighbor
    bool backdrop = false;// the flat backdrop region (bgDarken target)
};

namespace detail {

inline float clamp01(const float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline float lumaOf(const uint32_t c) {
    return (0.2126f * ((c >> 16) & 255) + 0.7152f * ((c >> 8) & 255)
            + 0.0722f * (c & 255)) / 255.f;
}

inline void rgbToHsv(const float r, const float g, const float b,
                     float& h, float& s, float& v) {
    const float mx = std::max(r, std::max(g, b));
    const float mn = std::min(r, std::min(g, b));
    const float d = mx - mn;
    v = mx;
    s = mx <= 0.f ? 0.f : d / mx;
    if (d <= 1e-6f) { h = 0.f; return; }
    if (mx == r)         h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g)    h = (b - r) / d + 2.f;
    else                 h = (r - g) / d + 4.f;
    h /= 6.f;
}

inline void hsvToRgb(const float h, const float s, const float v,
                     float& r, float& g, float& b) {
    const float hh = h - std::floor(h);
    const int i = int(hh * 6.f);
    const float f = hh * 6.f - i;
    const float p = v * (1.f - s);
    const float q = v * (1.f - f * s);
    const float t = v * (1.f - (1.f - f) * s);
    switch (i % 6) {
    case 0: r = v; g = t; b = p; return;
    case 1: r = q; g = v; b = p; return;
    case 2: r = p; g = v; b = t; return;
    case 3: r = p; g = q; b = v; return;
    case 4: r = t; g = p; b = v; return;
    default: r = v; g = p; b = q; return;
    }
}

// hue delta from a to b on the shortest arc, in turns
inline float hueDelta(const float a, const float b) {
    float d = b - a;
    d -= std::floor(d + 0.5f);
    return d;
}

// the 3-stop journey pre-resolved: hues as accumulated shortest
// arcs, so cool->mid->warm interpolates through vivid middles;
// t = 0 lands on the cool stop, t = 1 on the warm stop
struct Ramp3 {
    float h0, dh1, dh2; // start hue, arc to mid, arc from mid to cool
    float s0, s1, s2;
    float v0, v1, v2;
};

inline Ramp3 makeRamp3(const float c[3], const float m[3], const float w[3]) {
    float hw, mw, cw, sw, sa, sk, vw, va, vk;
    rgbToHsv(w[0], w[1], w[2], hw, sw, vw);
    rgbToHsv(m[0], m[1], m[2], mw, sa, va);
    rgbToHsv(c[0], c[1], c[2], cw, sk, vk);
    Ramp3 r;
    r.h0 = cw;
    r.dh1 = hueDelta(cw, mw);
    r.dh2 = hueDelta(mw, hw);
    r.s0 = sw; r.s1 = sa; r.s2 = sk;
    r.v0 = vw; r.v1 = va; r.v2 = vk;
    return r;
}

inline void evalRamp3(const Ramp3& rp, const float t, const float mixAmt,
                      const uint32_t src, uint32_t& dst) {
    float h, s, v;
    if (t < 0.5f) {
        const float u = t * 2.f;
        h = rp.h0 + rp.dh1 * u;
        s = rp.s0 + (rp.s1 - rp.s0) * u;
        v = rp.v0 + (rp.v1 - rp.v0) * u;
    } else {
        const float u = (t - 0.5f) * 2.f;
        h = rp.h0 + rp.dh1 + rp.dh2 * u;
        s = rp.s1 + (rp.s2 - rp.s1) * u;
        v = rp.v1 + (rp.v2 - rp.v1) * u;
    }
    float r, g, b;
    hsvToRgb(h, s, v, r, g, b);
    const float orr = ((src >> 16) & 255) / 255.f;
    const float og = ((src >> 8) & 255) / 255.f;
    const float ob = (src & 255) / 255.f;
    r = orr + (r - orr) * mixAmt;
    g = og + (g - og) * mixAmt;
    b = ob + (b - ob) * mixAmt;
    const auto q = [](const float f) {
        return (f <= 0.f) ? 0u : (f >= 1.f) ? 255u : uint32_t(f * 255.f + 0.5f);
    };
    dst = (src & 0xFF000000u) | (q(r) << 16) | (q(g) << 8) | q(b);
}

} // namespace detail

// Compute the full pipeline. src and dst may not alias; dst receives
// the same alpha as src (segmentation treats alpha < 128 as
// background). stats, when not null, reports one entry per region.
inline void compute(const uint32_t* const src, const int w, const int h,
                    const Params& p, uint32_t* const dst,
                    std::vector<RegionStat>* const stats = nullptr,
                    std::vector<float>* const tOut = nullptr)
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
            int borderTouch = 0;
            while (!stack.empty()) {
                const size_t i = size_t(stack.back());
                stack.pop_back();
                st.area++;
                const int x = int(i % w);
                const int y = int(i / w);
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) borderTouch++;
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
            st.tiny = st.area < p.minArea;
            // backdrop candidate: a border-touching region big enough
            // to be the ground the character stands on
            st.backdrop = borderTouch > 0 && st.area >= int(0.15 * n);
            regions.push_back(st);
        }
    }
    // only the single largest border region counts as the backdrop
    {
        int best = -1;
        for (size_t k = 0; k < regions.size(); k++) {
            if (!regions[k].backdrop) continue;
            if (best < 0 || regions[k].area > regions[size_t(best)].area) {
                best = int(k);
            }
        }
        for (size_t k = 0; k < regions.size(); k++) {
            if (int(k) != best) regions[k].backdrop = false;
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
    // fragments) never develop a meaningful gradient. Only LOW
    // SATURATION ones pass through untouched - that is line art
    // (gray/black strokes, white glints); colored slivers (backdrop
    // gradient bands, hair strands) tint with the global axis like
    // specks, so real-world sources shade evenly instead of keeping
    // original-color holes
    for (auto& st : regions) {
        if (st.maxDist < 8.f) {
            float hh, ss, vv;
            detail::rgbToHsv(((st.color >> 16) & 255) / 255.f,
                             ((st.color >> 8) & 255) / 255.f,
                             (st.color & 255) / 255.f,
                             hh, ss, vv);
            if (ss < 0.3f) st.flat = true;
            else st.tiny = true;
        }
    }

    // ---- 3.5 fold specks and colored slivers into big neighbors ----
    // one visual block must be ONE region: jpeg crumbs, strand
    // slivers and backdrop gradient bands merge into the largest
    // adjacent region so the radial gradient covers the whole block;
    // low-saturation line art (flat) never merges and never absorbs
    {
        std::vector<int32_t> map(regions.size());
        for (size_t k = 0; k < map.size(); k++) map[k] = int32_t(k);
        auto find = [&](const int32_t a) {
            int32_t r = a;
            while (map[size_t(r)] != r) r = map[size_t(r)];
            int32_t cur = a;
            while (map[size_t(cur)] != r) {
                const int32_t nx = map[size_t(cur)];
                map[size_t(cur)] = r;
                cur = nx;
            }
            return r;
        };
        for (int round = 0; round < 4; round++) {
            std::vector<std::unordered_map<int32_t, int>> adj(regions.size());
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const size_t i = size_t(y) * w + x;
                    const int32_t a = label[i];
                    if (a < 0) continue;
                    const int32_t nbrs[2] = { (x > 0) ? label[i - 1] : -1,
                                              (y > 0) ? label[i - w] : -1 };
                    for (const int32_t o : nbrs) {
                        if (o < 0 || o == a) continue;
                        const int32_t ra = find(a);
                        const int32_t ro = find(o);
                        if (ra == ro) continue;
                        adj[size_t(ra)][ro]++;
                        adj[size_t(ro)][ra]++;
                    }
                }
            }
            bool changed = false;
            for (size_t k = 0; k < regions.size(); k++) {
                if (!regions[k].tiny || map[k] != int32_t(k)) continue;
                int32_t best = -1;
                int bestArea = -1;
                for (const auto& pr : adj[k]) {
                    const RegionStat& st = regions[size_t(pr.first)];
                    if (st.flat || st.tiny) continue;
                    if (st.area > bestArea) { bestArea = st.area; best = pr.first; }
                }
                if (best >= 0) {
                    map[k] = best;
                    regions[size_t(best)].area += regions[k].area;
                    regions[k].tiny = false;
                    changed = true;
                }
            }
            if (!changed) break;
        }
        for (size_t i = 0; i < n; i++) {
            const int32_t L = label[i];
            if (L >= 0) label[i] = find(L);
        }
    }

    if (stats) *stats = regions;
    if (tOut) tOut->assign(n, -1.f);

    // per-region centroid and outer radius for the circular gradient
    std::vector<double> cx(regions.size(), 0.0), cy(regions.size(), 0.0);
    std::vector<int> rcnt(regions.size(), 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int32_t L = label[size_t(y) * w + x];
            if (L < 0) continue;
            cx[size_t(L)] += x + 0.5;
            cy[size_t(L)] += y + 0.5;
            rcnt[size_t(L)]++;
        }
    }
    for (size_t k = 0; k < regions.size(); k++) {
        if (rcnt[k] > 0) { cx[k] /= rcnt[k]; cy[k] /= rcnt[k]; }
    }
    std::vector<float> rad(regions.size(), 1.f);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int32_t L = label[size_t(y) * w + x];
            if (L < 0) continue;
            const float d = float(std::hypot(x + 0.5 - cx[size_t(L)],
                                             y + 0.5 - cy[size_t(L)]));
            if (d > rad[size_t(L)]) rad[size_t(L)] = d;
        }
    }

    // ---- 4. shading ----------------------------------------------------
    const float mixAmt = detail::clamp01(p.mix / 100.f);
    const float gradGamma = std::max(0.05f, p.gradGamma);
    const detail::Ramp3 ramp = detail::makeRamp3(p.colCool, p.colMid, p.colWarm);

    if (p.shadeMode == ShadeVolumetric) {
        // pseudo-normal lambert + crease ao produce t (0 = shadow /
        // cool end, 1 = lit / warm end)
        const float th = p.lightAngleDeg * 3.14159265f / 180.f;
        const float lx = std::cos(th);
        const float ly = -std::sin(th); // screen y is down
        const float lz = std::tan(p.lightElevDeg * 3.14159265f / 180.f);
        const float invLLen = 1.f / std::sqrt(lx * lx + ly * ly + lz * lz);
        const float Lv[3] = { lx * invLLen, ly * invLLen, lz * invLLen };
        const float k = std::pow(p.bump / 100.f, 1.3f) * 5.f;
        const float aoStr = p.ao / 100.f;
        const float aoW = std::max(1.f, p.aoWidth);

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
                const float nDotL = (-gx * Lv[0] - gy * Lv[1] + invN * Lv[2]) * invN;
                float t = 0.5f + 0.5f * nDotL;
                const float depth = detail::clamp01(dist[i] / aoW);
                t -= aoStr * 0.5f * (1.f - depth) * (1.f - depth);
                t = 0.5f + (t - 0.5f) * 1.15f;
                t = detail::clamp01(t);
                if (tOut) (*tOut)[i] = t;
                detail::evalRamp3(ramp, t, mixAmt, s, dst[i]);
            }
        }
        return;
    }

    // radial mode: one CIRCULAR gradient per region - t = 1 at the
    // centroid (center stop) falling to 0 at the region's outer
    // radius (edge stop); the gamma curve sizes the center band
    const float bgK = 1.f - detail::clamp01(p.bgDarken / 100.f);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const size_t i = size_t(y) * w + x;
            const uint32_t s = src[i];
            const int32_t L = label[i];
            if (L < 0) { dst[i] = s; continue; }
            const RegionStat& st = regions[size_t(L)];
            if (st.flat) { dst[i] = s; continue; }
            const float d = float(std::hypot(x + 0.5 - cx[size_t(L)],
                                             y + 0.5 - cy[size_t(L)]));
            // only real blocks (wide AND roomy) run the full journey
            // to the center stop; small leftover regions shade within
            // mid..edge tones so no red cores pop out of folds
            const float tPeak = detail::clamp01(std::min(st.maxDist / 80.f,
                                                          st.area / 8000.f))
                              * 0.7f + 0.3f;
            const float t = std::pow(detail::clamp01(1.f - d / rad[size_t(L)]),
                                     gradGamma) * tPeak;
            if (st.backdrop && bgK < 1.f) {
                // darken the backdrop before tinting, so a full
                // darken swallows the gradient too (reference look)
                const uint32_t a = (s >> 24) & 255;
                const auto q = [bgK](const uint32_t v) -> uint32_t {
                    return uint32_t(v * bgK + 0.5f);
                };
                const uint32_t dark = (a << 24)
                        | (q((s >> 16) & 255) << 16)
                        | (q((s >> 8) & 255) << 8) | q(s & 255);
                if (tOut) (*tOut)[i] = t;
                detail::evalRamp3(ramp, t, mixAmt, dark, dst[i]);
                continue;
            }
            if (tOut) (*tOut)[i] = t;
            detail::evalRamp3(ramp, t, mixAmt, s, dst[i]);
        }
    }
}

} // namespace celvolume

#endif // CELVOLUMEALGO_H
