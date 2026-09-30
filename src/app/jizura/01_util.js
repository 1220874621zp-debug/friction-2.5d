"use strict";
const J = window.J = window.J || {};
J.clamp = (x, a = 0, b = 1) => x < a ? a : x > b ? b : x;
J.lerp = (a, b, t) => a + (b - a) * t;
J.inv = (a, b, x) => b === a ? 0 : (x - a) / (b - a);
J.smooth = (a, b, x) => {
  const t = J.clamp(J.inv(a, b, x));
  return t * t * (3 - 2 * t);
};
J.TAU = Math.PI * 2;
J.DEG = Math.PI / 180;
J.E = {
  lin: (x) => J.clamp(x),
  inQuad: (x) => {
    x = J.clamp(x);
    return x * x;
  },
  outQuad: (x) => {
    x = J.clamp(x);
    return 1 - (1 - x) * (1 - x);
  },
  inCubic: (x) => {
    x = J.clamp(x);
    return x * x * x;
  },
  outCubic: (x) => {
    x = J.clamp(x);
    return 1 - Math.pow(1 - x, 3);
  },
  inOutCubic: (x) => {
    x = J.clamp(x);
    return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2;
  },
  outExpo: (x) => {
    x = J.clamp(x);
    return x >= 1 ? 1 : 1 - Math.pow(2, -10 * x);
  },
  inExpo: (x) => {
    x = J.clamp(x);
    return x <= 0 ? 0 : Math.pow(2, 10 * x - 10);
  },
  inOutExpo: (x) => {
    x = J.clamp(x);
    if (x <= 0 || x >= 1) return x;
    return x < 0.5 ? Math.pow(2, 20 * x - 10) / 2 : (2 - Math.pow(2, -20 * x + 10)) / 2;
  },
  outBack: (x, s = 1.9) => {
    x = J.clamp(x);
    const c = s + 1;
    return 1 + c * Math.pow(x - 1, 3) + s * Math.pow(x - 1, 2);
  },
  outElastic: (x) => {
    x = J.clamp(x);
    if (x === 0 || x === 1) return x;
    return Math.pow(2, -10 * x) * Math.sin((x * 10 - 0.75) * (J.TAU / 3)) + 1;
  },
  inOutSine: (x) => {
    x = J.clamp(x);
    return -(Math.cos(Math.PI * x) - 1) / 2;
  }
};
const _sidCache = /* @__PURE__ */ new Map();
J.sid = (s) => {
  let v = _sidCache.get(s);
  if (v !== void 0) return v;
  let h = 2166136261 >>> 0;
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  v = h >>> 0;
  _sidCache.set(s, v);
  return v;
};
J.h = function(a, b, c, d, e) {
  let h = 2654435769 ^ (a | 0);
  h = Math.imul(h ^ h >>> 16, 2246822507);
  h = h + Math.imul((b | 0) + 1663821227, 3266489909) | 0;
  h = Math.imul(h ^ h >>> 13, 3266489909);
  h = h + Math.imul((c | 0) + 1540483477, 668265263) | 0;
  h = Math.imul(h ^ h >>> 15, 374761393);
  h = h + Math.imul((d | 0) + 461845907, 2246822507) | 0;
  h = Math.imul(h ^ h >>> 16, 668265263);
  h = h + Math.imul((e | 0) + 1759714724, 2654435761) | 0;
  h ^= h >>> 15;
  h = Math.imul(h, 739982445);
  h ^= h >>> 12;
  h = Math.imul(h, 695872825);
  h ^= h >>> 15;
  return h >>> 0;
};
J.r = (a, b, c, d, e) => J.h(a, b, c, d, e) / 4294967296;
J.rs = (a, b, c, d, e) => J.r(a, b, c, d, e) * 2 - 1;
J.rr = (lo, hi, a, b, c, d, e) => lo + (hi - lo) * J.r(a, b, c, d, e);
J.pick = (arr, a, b, c, d) => arr[Math.floor(J.r(a, b, c, d) * arr.length) % arr.length];
J.rng = (seed) => {
  let s = seed >>> 0;
  const f = () => {
    s = s + 1831565813 >>> 0;
    let t = s;
    t = Math.imul(t ^ t >>> 15, t | 1);
    t ^= t + Math.imul(t ^ t >>> 7, t | 61);
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
  f.range = (lo, hi) => lo + (hi - lo) * f();
  f.int = (lo, hi) => Math.floor(lo + (hi - lo + 1) * f());
  f.pick = (arr) => arr[Math.floor(f() * arr.length) % arr.length];
  f.chance = (p) => f() < p;
  f.wpick = (list) => {
    let tot = 0;
    for (const it of list) tot += Array.isArray(it) ? it[1] : it.w;
    let x = f() * tot;
    for (const it of list) {
      const w = Array.isArray(it) ? it[1] : it.w;
      if ((x -= w) <= 0) return Array.isArray(it) ? it[0] : it.v;
    }
    const last = list[list.length - 1];
    return Array.isArray(last) ? last[0] : last.v;
  };
  return f;
};
J.noise1 = (x, seed = 0) => {
  const i = Math.floor(x), f = x - i, u = f * f * (3 - 2 * f);
  return J.lerp(J.rs(seed, i), J.rs(seed, i + 1), u);
};
J.hex = (h) => {
  h = String(h || "#000").replace("#", "");
  if (h.length === 3) h = h.split("").map((c) => c + c).join("");
  const n = parseInt(h.slice(0, 6), 16);
  return [n >> 16 & 255, n >> 8 & 255, n & 255];
};
J.rgba = (h, a = 1) => {
  const [r, g, b] = J.hex(h);
  return `rgba(${r},${g},${b},${a})`;
};
J.mix = (h1, h2, t) => {
  const a = J.hex(h1), b = J.hex(h2);
  const c = a.map((v, i) => Math.round(J.lerp(v, b[i], t)));
  return "#" + c.map((v) => v.toString(16).padStart(2, "0")).join("");
};
J.lum = (h) => {
  const [r, g, b] = J.hex(h);
  return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
};
J.toHex = (r, g, b) => "#" + [r, g, b].map((v) => Math.round(J.clamp(v, 0, 255)).toString(16).padStart(2, "0")).join("").toUpperCase();
J.hsl = (h, s, l) => {
  h = (h % 360 + 360) % 360 / 360;
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  const f = (t) => {
    t = (t + 1) % 1;
    return t < 1 / 6 ? p + (q - p) * 6 * t : t < 1 / 2 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p;
  };
  return J.toHex(f(h + 1 / 3) * 255, f(h) * 255, f(h - 1 / 3) * 255);
};
J.toHsl = (hex) => {
  const [r, g, b] = J.hex(hex).map((v) => v / 255);
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2;
  if (mx === mn) return [0, 0, l];
  const d = mx - mn, s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
  const h = mx === r ? (g - b) / d + (g < b ? 6 : 0) : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return [h * 60, s, l];
};
J.contrast = (a, b) => {
  const L = (h) => {
    const c = J.hex(h).map((v) => {
      v /= 255;
      return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
    });
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  };
  const x = L(a), y = L(b);
  return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
};
J.fitContrast = (hex, bg, min = 3) => {
  if (J.contrast(hex, bg) >= min) return hex.toUpperCase();
  let [h, s, l] = J.toHsl(hex);
  const dark = J.lum(bg) < 0.5;
  for (let i = 0; i < 24; i++) {
    l = dark ? Math.min(0.96, l + 0.035) : Math.max(0.04, l - 0.035);
    const c = J.hsl(h, s, l);
    if (J.contrast(c, bg) >= min) return c;
  }
  return dark ? "#FFFFFF" : "#111111";
};
J.GHOST_PAIRS = [
  ["#16F4D4", "#F5A50C"],
  ["#FF2A2A", "#2AA8FF"],
  ["#FF2BD6", "#2BFF88"],
  ["#FFE600", "#7B2BFF"],
  ["#FF6A00", "#00C2B8"],
  ["#FF6FAE", "#B6FF3B"],
  ["#00E0FF", "#FF3D6E"],
  ["#C8FF00", "#FF00A8"],
  ["#4D6BFF", "#FFB000"],
  ["#FF4B2B", "#2BD9FF"]
];
J.randomPalette = (bg, rnd = Math.random) => {
  const dark = J.lum(bg) < 0.5;
  let a, b, mode;
  if (rnd() < 0.4) {
    mode = "curated";
    [a, b] = J.GHOST_PAIRS[Math.floor(rnd() * J.GHOST_PAIRS.length)];
    if (rnd() < 0.5) [a, b] = [b, a];
    if (!dark) {
      a = J.hsl(J.toHsl(a)[0], 0.95, 0.47);
      b = J.hsl(J.toHsl(b)[0], 0.95, 0.47);
    }
  } else {
    mode = "harmony";
    const h = rnd() * 360, gap = [180, 165, 150, 135][Math.floor(rnd() * 4)] * (rnd() < 0.5 ? 1 : -1);
    const s = 0.82 + rnd() * 0.18, l = dark ? 0.52 + rnd() * 0.1 : 0.44 + rnd() * 0.08;
    a = J.hsl(h, s, l);
    b = J.hsl(h + gap, s, l);
  }
  const r = rnd();
  const ha = J.toHsl(a)[0], hb = J.toHsl(b)[0];
  let acc = r < 0.35 ? a : r < 0.6 ? b : J.hsl((ha + hb) / 2 + (rnd() < 0.5 ? 0 : 180), 0.9, dark ? 0.6 : 0.45);
  acc = J.fitContrast(acc, bg, 3);
  return { accent: acc, ghostA: a, ghostB: b, mode };
};
J.isKanji = (c) => /[㐀-鿿豈-﫿々〆ヶ]/.test(c);
J.isHira = (c) => /[ぁ-ゟ]/.test(c);
J.isKata = (c) => /[゠-ヿㇰ-ㇿｦ-ﾟ]/.test(c);
J.isSmallKana = (c) => "\u3041\u3043\u3045\u3047\u3049\u3063\u3083\u3085\u3087\u308E\u30A1\u30A3\u30A5\u30A7\u30A9\u30C3\u30E3\u30E5\u30E7\u30EE\u30F5\u30F6".includes(c);
J.isPunct = (c) => /[、。，．,.!?！？…‥・「」『』（）()【】〈〉《》〔〕［］\[\]'"“”‘’ー〜～:：;；\-—―]/.test(c);
J.isLatin = (c) => /[A-Za-z0-9]/.test(c);
J.VERT_ROTATE = "\u30FC\u301C\uFF5E\u2026\u2025\u2015\u2014-()\uFF08\uFF09\u300C\u300D\u300E\u300F\u3010\u3011\u3008\u3009\u300A\u300B\u3014\u3015[]\uFF3B\uFF3D\u2192\u2190:\uFF1A;\uFF1B=\uFF1D";
(() => {
  const base = { \u3042: "a", \u3044: "i", \u3046: "u", \u3048: "e", \u304A: "o", \u304B: "ka", \u304D: "ki", \u304F: "ku", \u3051: "ke", \u3053: "ko", \u3055: "sa", \u3057: "shi", \u3059: "su", \u305B: "se", \u305D: "so", \u305F: "ta", \u3061: "chi", \u3064: "tsu", \u3066: "te", \u3068: "to", \u306A: "na", \u306B: "ni", \u306C: "nu", \u306D: "ne", \u306E: "no", \u306F: "ha", \u3072: "hi", \u3075: "fu", \u3078: "he", \u307B: "ho", \u307E: "ma", \u307F: "mi", \u3080: "mu", \u3081: "me", \u3082: "mo", \u3084: "ya", \u3086: "yu", \u3088: "yo", \u3089: "ra", \u308A: "ri", \u308B: "ru", \u308C: "re", \u308D: "ro", \u308F: "wa", \u3092: "wo", \u3093: "n", \u304C: "ga", \u304E: "gi", \u3050: "gu", \u3052: "ge", \u3054: "go", \u3056: "za", \u3058: "ji", \u305A: "zu", \u305C: "ze", \u305E: "zo", \u3060: "da", \u3062: "ji", \u3065: "zu", \u3067: "de", \u3069: "do", \u3070: "ba", \u3073: "bi", \u3076: "bu", \u3079: "be", \u307C: "bo", \u3071: "pa", \u3074: "pi", \u3077: "pu", \u307A: "pe", \u307D: "po", \u3041: "a", \u3043: "i", \u3045: "u", \u3047: "e", \u3049: "o", \u3094: "vu" };
  const yo = { \u3083: "ya", \u3085: "yu", \u3087: "yo" };
  J.romaji = (s) => {
    let out = "", i = 0;
    const toH = (c) => J.isKata(c) && c !== "\u30FC" ? String.fromCharCode(c.charCodeAt(0) - 96) : c;
    const arr = [...s].map(toH);
    while (i < arr.length) {
      const c = arr[i], n = arr[i + 1];
      if (c === "\u3063") {
        const nx = base[n] || "";
        out += nx ? nx[0] : "";
        i++;
        continue;
      }
      if (c === "\u30FC") {
        out += out.slice(-1);
        i++;
        continue;
      }
      if (n && yo[n] && base[c]) {
        const b = base[c];
        out += (b.length > 1 && b.endsWith("i") ? b.slice(0, -1) : b) + (b === "shi" || b === "chi" || b === "ji" ? yo[n].slice(1) : yo[n]);
        i += 2;
        continue;
      }
      if (base[c]) out += base[c];
      else if (/[A-Za-z0-9 ]/.test(c)) out += c;
      else return null;
      i++;
    }
    return out;
  };
})();
J.fmtTime = (t, fps) => {
  t = Math.max(0, t);
  const m = Math.floor(t / 60), s = Math.floor(t % 60), f = Math.floor(t % 1 * (fps || 100));
  return `${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}${fps ? ":" + String(f).padStart(2, "0") : "." + String(f).padStart(2, "0")}`;
};
