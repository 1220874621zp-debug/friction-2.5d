(() => {
  "use strict";
  const E = J.E;
  const P = "typo";
  const SET = "typo";
  const reg = (g, key, def) => J.register(g, key, Object.assign({ set: SET }, def), P);
  const HIDE = Object.freeze({ hide: true });
  const clamp = J.clamp, lerp = J.lerp, DEG = J.DEG;
  const isSp = (c) => c === " " || c === "\u3000";
  const layOf = (it) => (it._m || (it._m = J.measure(it))).lay;
  const motionK = (env) => J.clamp((env.fx && env.fx.motion != null ? env.fx.motion : 0.7) / 0.7, 0, 1.6);
  const cutN = (env) => Math.max(1, J.glyphCount(String(env.cut && env.cut.text || "")));
  const accentOf = (env) => {
    const sc = env.sc;
    return J.contrast(sc.accent, sc.bg) >= 1.8 ? sc.accent : sc.fg;
  };
  const subOf = (env) => {
    const sc = env.sc;
    return J.contrast(sc.sub, sc.bg) >= 1.5 ? sc.sub : sc.fg;
  };
  function addPost(it, fn) {
    const prev = it.post;
    it.post = prev ? (env, x, bb) => {
      prev(env, x, bb);
      fn(env, x, bb);
    } : fn;
  }
  function orders(env, it) {
    const lay = layOf(it), out = new Array(lay.N).fill(0);
    const idx = [];
    for (const g of lay) if (!isSp(g.ch)) idx.push(g.i);
    const n = idx.length;
    if (n <= 1) {
      const N = cutN(env);
      const o = N > 1 ? clamp((+it.mi || 0) / (N - 1)) : 0;
      idx.forEach((i) => {
        out[i] = o;
      });
      return out;
    }
    idx.forEach((i, k) => {
      out[i] = k / (n - 1);
    });
    return out;
  }
  function keyOf(lay) {
    let best = -1, bl = 0;
    for (let i = 0; i < lay.length; i++) {
      if (!J.isKanji(lay[i].ch)) continue;
      let j = i;
      while (j < lay.length && J.isKanji(lay[j].ch)) j++;
      if (j - i > bl) {
        bl = j - i;
        best = i;
      }
      i = j;
    }
    if (best >= 0) return lay[best].i;
    let wb = -1, wl = 0;
    for (let i = 0; i < lay.length; i++) {
      if (!/[A-Za-z0-9]/.test(lay[i].ch)) continue;
      let j = i;
      while (j < lay.length && /[A-Za-z0-9'’]/.test(lay[j].ch)) j++;
      if (j - i > wl) {
        wl = j - i;
        wb = i;
      }
      i = j;
    }
    if (wb >= 0) return lay[wb].i;
    for (const g of lay) if (!isSp(g.ch) && !J.isPunct(g.ch) && !J.isSmallKana(g.ch)) return g.i;
    return lay.length ? lay[0].i : 0;
  }
  function lineInfo(it) {
    const lay = layOf(it), sx = it.sx || 1, sy = it.sy || 1, map = /* @__PURE__ */ new Map();
    for (const g of lay) {
      if (isSp(g.ch)) continue;
      const a0 = it.vertical ? (g.y - g.h / 2) * sy : (g.x - g.w / 2) * sx, a1 = it.vertical ? (g.y + g.h / 2) * sy : (g.x + g.w / 2) * sx;
      const L = map.get(g.li);
      if (!L) map.set(g.li, { li: g.li, a0, a1, c: it.vertical ? g.x * sx : g.y * sy });
      else {
        L.a0 = Math.min(L.a0, a0);
        L.a1 = Math.max(L.a1, a1);
      }
    }
    return [...map.values()].sort((a, b) => a.li - b.li);
  }
  const inItem = (env, it, fn) => {
    if (env.pass !== "main") return;
    const ctx = env.ctx;
    ctx.save();
    ctx.translate(it.x, it.y);
    if (it.rot) ctx.rotate(it.rot * DEG);
    try {
      fn();
    } finally {
      ctx.restore();
    }
  };
  const boxOf = (it) => {
    it._m = J.measure(it);
    return J.itemBox(it);
  };
  reg("enter", "tyKeyFirst", {
    name: "\u30AD\u30FC\u5B57\u5148\u884C",
    tags: ["pop", "graphic", "editorial", "emotional"],
    ae: "pop",
    w: 1,
    apply(env, it, p) {
      const lay = layOf(it), sx = it.sx || 1, sy = it.sy || 1;
      const plain = lay.filter((g) => !isSp(g.ch));
      if (plain.length <= 1) {
        const q = clamp(p / 0.7);
        it.charFns.push(() => q <= 0 ? HIDE : { s: lerp(1.9, 1, E.outExpo(q)), a: clamp(q * 4) });
        return;
      }
      const ki = keyOf(lay), gk = lay.find((g) => g.i === ki) || plain[0];
      let md = 1;
      for (const g of plain) md = Math.max(md, Math.abs(g.i - ki));
      const kq = clamp(p / 0.42);
      it.charFns.push((i, g) => {
        if (i === ki) return kq <= 0 ? HIDE : { s: lerp(2.1, 1, E.outExpo(kq)), a: clamp(kq * 4) };
        const d = Math.abs(i - ki) / md, q = clamp((p - 0.3 - d * 0.35) / 0.35);
        if (q <= 0) return HIDE;
        const e = E.outExpo(q);
        return { dx: (gk.x - g.x) * sx * (1 - e), dy: (gk.y - g.y) * sy * (1 - e), s: lerp(0.4, 1, e), a: clamp(q * 2.5) };
      });
    }
  });
  reg("enter", "tyLineWipe", {
    name: "\u884C\u9001\u308A\u30EF\u30A4\u30D7",
    tags: ["editorial", "graphic", "calm"],
    ae: "wipe",
    w: 1,
    apply(env, it, p) {
      const lines = lineInfo(it), nL = Math.max(1, lines.length), st = Math.min(0.28, 0.6 / nL);
      const qOf = (li) => {
        const k = lines.findIndex((L) => L.li === li);
        return E.inOutCubic(clamp((p - Math.max(0, k) * st) / (1 - (nL - 1) * st)));
      };
      const qs = new Map(lines.map((L) => [L.li, qOf(L.li)]));
      const sx = it.sx || 1, sy = it.sy || 1, V = !!it.vertical;
      it.charFns.push((i, g) => {
        var _a;
        const q = (_a = qs.get(g.li)) != null ? _a : 1;
        if (q >= 1) return null;
        const L = lines.find((l) => l.li === g.li);
        if (!L) return null;
        const edge = lerp(L.a0, L.a1, q);
        const start = V ? (g.y - g.h / 2) * sy : (g.x - g.w / 2) * sx, len = V ? g.h * sy : g.w * sx;
        const f = (edge - start) / Math.max(1, len);
        if (f <= 0) return HIDE;
        if (f >= 1) return null;
        const lim = -0.6 + 1.2 * f;
        return V ? { clipY: [-0.7, lim] } : { clipX: [-0.7, lim] };
      });
      const ac = accentOf(env), lw = Math.max(2, it.size * 0.05);
      addPost(it, (e, i) => inItem(e, i, () => {
        var _a;
        for (const L of lines) {
          const q = qs.get(L.li);
          if (q >= 1) continue;
          const a = lerp(L.a0, L.a1, E.inCubic(clamp(q * 1.4 - 0.4))), b = lerp(L.a0, L.a1, q);
          if (b - a < 1) continue;
          const off = L.c + i.size * (V ? sx : sy) * 0.64;
          e.line(V ? [[off, a], [off, b]] : [[a, off], [b, off]], ac, lw, (_a = i.alpha) != null ? _a : 1, false);
        }
      }));
    }
  });
  reg("enter", "tyZoomOne", {
    name: "\u4E00\u5B57\u305A\u3064\u62E1\u5927",
    tags: ["pop", "graphic", "emotional"],
    ae: "zoom",
    w: 0.8,
    minDur: 1,
    inDur: (dur, n) => J.clamp(0.14 * n + 0.2, 0.4, Math.min(1.5, dur * 0.55)),
    apply(env, it, p) {
      const lay = layOf(it), sx = it.sx || 1, sy = it.sy || 1;
      const plain = lay.filter((g) => !isSp(g.ch)), nn = plain.length;
      if (!nn) return;
      let x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
      for (const g of plain) {
        x0 = Math.min(x0, g.x - g.w / 2);
        x1 = Math.max(x1, g.x + g.w / 2);
        y0 = Math.min(y0, g.y - g.h / 2);
        y1 = Math.max(y1, g.y + g.h / 2);
      }
      const mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
      const big = clamp(Math.min(env.W, env.H) * 0.5 / Math.max(1, it.size * Math.max(sx, sy)), 1.15, 2.6);
      const L = nn > 1 ? Math.min(0.55, 2.2 / (nn + 1)) : 1;
      const rank = new Map(plain.map((g, k) => [g.i, k]));
      const single = nn === 1 && cutN(env) > 1;
      it.charFns.push((i, g) => {
        const k = rank.get(i);
        if (k == null) return null;
        const s0 = nn > 1 ? k / (nn - 1) * (1 - L) : 0;
        const q = clamp((p - s0) / L);
        if (q <= 0) return HIDE;
        if (single) return { s: lerp(big, 1, E.outExpo(q)), a: clamp(q * 5) };
        const cx = (mx - g.x) * sx, cy = (my - g.y) * sy;
        if (q < 0.38) {
          const u = E.outCubic(q / 0.38);
          return { dx: cx, dy: cy, s: lerp(big * 1.25, big, u), a: clamp(q / 0.1) };
        }
        const e = E.inOutCubic((q - 0.38) / 0.62);
        return { dx: cx * (1 - e), dy: cy * (1 - e), s: lerp(big, 1, e) };
      });
    }
  });
  reg("enter", "tyUnderLift", {
    name: "\u4E0B\u7DDA\u304B\u3089\u7ACB\u3064",
    tags: ["editorial", "graphic", "pop"],
    ae: "stretch",
    w: 1,
    apply(env, it, p) {
      const lines = lineInfo(it), ord = orders(env, it), V = !!it.vertical, sx = it.sx || 1, sy = it.sy || 1;
      const multi = layOf(it).filter((g) => !isSp(g.ch)).length > 1;
      it.charFns.push((i, g) => {
        const o = multi ? ord[i] : 0, q = clamp((p - 0.22 - o * 0.38) / 0.4);
        if (q <= 0) return HIDE;
        if (q >= 1) return null;
        const e = Math.max(0.02, E.outBack(q, 1.5));
        return V ? { sx: e, dx: -(1 - e) * g.w * sx * 0.45 } : { sy: e, dy: (1 - e) * g.h * sy * 0.45 };
      });
      const qb = E.outExpo(clamp(p / 0.32)), qr = E.inCubic(clamp((p - 0.72) / 0.28)), ac = accentOf(env);
      addPost(it, (e, i) => inItem(e, i, () => {
        var _a;
        const lw = Math.max(2.5, i.size * 0.055);
        for (const L of lines) {
          const a = lerp(L.a0, L.a1, qr), b = lerp(L.a0, L.a1, qb);
          if (b - a < 1) continue;
          const off = V ? L.c - i.size * sx * 0.56 : L.c + i.size * sy * 0.56;
          e.line(V ? [[off, a], [off, b]] : [[a, off], [b, off]], ac, lw, (_a = i.alpha) != null ? _a : 1, false);
        }
      }));
    }
  });
  reg("enter", "tyDotGrow", {
    name: "\u70B9\u304B\u3089\u5B57",
    tags: ["pop", "calm", "graphic"],
    ae: "pop",
    w: 0.9,
    apply(env, it, p) {
      const ord = orders(env, it), ac = accentOf(env);
      it.charFns.push((i) => {
        const t0 = ord[i] * 0.5, q1 = clamp((p - t0) / 0.18), q2 = clamp((p - t0 - 0.2) / 0.3);
        if (q1 <= 0) return HIDE;
        if (q2 <= 0) return { ch: "\u30FB", s: 1.25 * E.outBack(q1, 2.6), color: ac };
        if (q2 >= 1) return null;
        return { s: lerp(0.3, 1, E.outBack(q2, 1.8)), a: clamp(q2 * 3) };
      });
    }
  });
  const drawBrackets = (e, V, cx, cy, half, w, h, size, col, a) => {
    if (a <= 0.01) return;
    const g = size * 0.18, L = Math.max(size * 0.28, (V ? w : h) * 0.3), lw = Math.max(2, size * 0.055);
    if (!V) {
      const xl = cx - half - g, xr = cx + half + g, yt = cy - h / 2 - g * 0.5, yb = cy + h / 2 + g * 0.5;
      e.line([[xl, yt + L], [xl, yt], [xl + L, yt]], col, lw, a, false);
      e.line([[xr, yb - L], [xr, yb], [xr - L, yb]], col, lw, a, false);
    } else {
      const yt = cy - half - g, yb = cy + half + g, xr = cx + w / 2 + g * 0.5, xl = cx - w / 2 - g * 0.5;
      e.line([[xr - L, yt], [xr, yt], [xr, yt + L]], col, lw, a, false);
      e.line([[xl + L, yb], [xl, yb], [xl, yb - L]], col, lw, a, false);
    }
  };
  reg("enter", "tyBracketOpen", {
    name: "\u62EC\u5F27\u304C\u958B\u304F",
    tags: ["editorial", "graphic", "pop"],
    ae: "wipe",
    w: 0.9,
    apply(env, it, p) {
      const b = boxOf(it), V = !!it.vertical, pad = it.size * 0.12;
      const e = E.inOutCubic(clamp((p - 0.08) / 0.62));
      const half = ((V ? b.h : b.w) / 2 + pad) * e;
      if (V) it.clipY = [b.cy - half, b.cy + half];
      else it.clip = [b.cx - half, b.cx + half];
      const solo = layOf(it).filter((g) => !isSp(g.ch)).length <= 1 && cutN(env) > 1;
      const a = solo ? 0 : clamp(p / 0.08) * (1 - clamp((p - 0.78) / 0.22)), col = accentOf(env);
      addPost(it, (en) => {
        if (en.pass === "main") drawBrackets(en, V, b.cx, b.cy, half, b.w, b.h, it.size, col, a);
      });
    }
  });
  reg("enter", "tyRetype", {
    name: "\u6253\u3061\u76F4\u3057",
    tags: ["editorial", "glitch", "emotional"],
    ae: "type",
    w: 0.8,
    minDur: 1,
    cursor: true,
    inDur: (dur, n) => J.clamp(0.1 * n + 0.4, 0.45, Math.min(1.5, dur * 0.55)),
    apply(env, it, p) {
      const lay = layOf(it), N = lay.N;
      const plain = lay.filter((g) => !isSp(g.ch));
      const seed = it.seed | 0;
      if (plain.length <= 1) {
        it.charFns.push(() => p < 0.35 ? HIDE : null);
        return;
      }
      let wi = -1;
      if (plain.length >= 3) wi = plain[1 + J.h(seed, 31) % (plain.length - 1)].i;
      const S = N + (wi >= 0 ? 2 : 0);
      const k = Math.floor(clamp(p) * (S + 0.999));
      let shown, wrong = false;
      if (wi < 0 || k <= wi) shown = Math.min(N, k);
      else if (k === wi + 1) {
        shown = wi + 1;
        wrong = true;
      } else if (k === wi + 2) shown = wi;
      else shown = Math.min(N, k - 2);
      const pool = J.pool("kana") || "\u3042\u3044\u3046\u3048\u304A";
      const wch = [...pool][J.h(seed, 32) % [...pool].length];
      const ac = accentOf(env);
      it.charFns.push((i) => {
        if (i >= shown) return HIDE;
        if (wrong && i === wi) return { ch: wch === lay[wi].ch ? "\uFF0A" : wch, color: ac };
        return null;
      });
      it.cursorAt = p < 1 && !it.vertical && !it.rot ? shown : -1;
    }
  });
  reg("enter", "tyRubyDrop", {
    name: "\u30EB\u30D3\u304B\u3089",
    tags: ["calm", "editorial", "emotional"],
    ae: "drop",
    w: 1,
    apply(env, it, p) {
      const ord = orders(env, it), V = !!it.vertical, sx = it.sx || 1, sy = it.sy || 1, sub = subOf(env);
      it.charFns.push((i, g) => {
        const q = clamp((p - ord[i] * 0.45) / 0.55);
        if (q <= 0) return HIDE;
        if (q >= 1) return null;
        const off = V ? g.w * sx * 0.8 : -g.h * sy * 0.8;
        if (q < 0.4) {
          const a = clamp(q / 0.4 * 1.6);
          return V ? { dx: off, s: 0.32, a, color: sub } : { dy: off, s: 0.32, a, color: sub };
        }
        const e = E.outCubic((q - 0.4) / 0.6), o = { s: lerp(0.32, 1, e) };
        if (V) o.dx = off * (1 - e);
        else o.dy = off * (1 - e);
        if (e < 0.55) o.color = sub;
        return o;
      });
    }
  });
  reg("exit", "tyStrike", {
    name: "\u7DDA\u3067\u6D88\u3059",
    tags: ["editorial", "graphic", "emotional"],
    ae: "wipe",
    w: 1,
    apply(env, it, p) {
      const lines = lineInfo(it), ord = orders(env, it), V = !!it.vertical;
      it.charFns.push((i) => {
        const q = clamp((p - 0.3 - ord[i] * 0.3) / 0.4);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const e = E.inCubic(q);
        return V ? { sx: 1 - e * 0.95, a: 1 - e * e } : { sy: 1 - e * 0.95, a: 1 - e * e };
      });
      const ql = E.outExpo(clamp(p / 0.38)), qr = E.inCubic(clamp((p - 0.74) / 0.26)), ac = accentOf(env), sx = it.sx || 1, sy = it.sy || 1;
      addPost(it, (e, i) => inItem(e, i, () => {
        const lw = Math.max(2.5, i.size * 0.07);
        for (const L of lines) {
          const pad = i.size * 0.1, a0 = L.a0 - pad, a1 = L.a1 + pad;
          const a = lerp(a0, a1, qr), b = lerp(a0, a1, ql);
          if (b - a < 1) continue;
          const off = L.c + i.size * (V ? sx : sy) * 0.04;
          e.line(V ? [[off, a], [off, b]] : [[a, off], [b, off]], ac, lw, 1, false);
        }
      }));
    }
  });
  reg("exit", "tyToDot", {
    name: "\u70B9\u306B\u623B\u308B",
    tags: ["calm", "pop", "graphic"],
    ae: "shrink",
    w: 0.9,
    apply(env, it, p) {
      const ord = orders(env, it), ac = accentOf(env);
      it.charFns.push((i) => {
        const q = clamp((p - ord[i] * 0.4) / 0.6);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        if (q < 0.5) return { s: lerp(1, 0.3, E.inCubic(q / 0.5)) };
        const u = (q - 0.5) / 0.5;
        return { ch: "\u30FB", color: ac, s: 1.5 * (1 - E.inCubic(u)), a: 1 - u * u };
      });
    }
  });
  reg("exit", "tyLineFeed", {
    name: "\u6539\u884C\u9001\u308A",
    tags: ["editorial", "calm", "graphic"],
    ae: "wipe",
    w: 1,
    apply(env, it, p) {
      const b = boxOf(it), V = !!it.vertical, pad = it.size * 0.12;
      const k = clamp(p) * 3, st = Math.min(3, Math.floor(k)), f = k - st;
      const step = Math.min(1, (st + E.outCubic(clamp(f * 2.6))) / 3);
      const dist = (V ? b.w : b.h) + it.size * 0.3;
      if (V) {
        it.clip = [b.x0 - pad, b.x1 + pad];
        it.x += dist * step;
      } else {
        it.clipY = [b.y0 - pad, b.y1 + pad];
        it.y -= dist * step;
      }
      if (p >= 0.999) it.alpha = 0;
    }
  });
  reg("exit", "tyBracketClose", {
    name: "\u62EC\u5F27\u9589\u3058",
    tags: ["editorial", "graphic", "pop"],
    ae: "wipe",
    w: 0.9,
    apply(env, it, p) {
      const b = boxOf(it), V = !!it.vertical, pad = it.size * 0.12;
      const e = E.inOutCubic(clamp((p - 0.12) / 0.7));
      const half = ((V ? b.h : b.w) / 2 + pad) * (1 - e);
      if (V) it.clipY = [b.cy - half, b.cy + half];
      else it.clip = [b.cx - half, b.cx + half];
      if (e >= 1) it.alpha = 0;
      const solo = layOf(it).filter((g) => !isSp(g.ch)).length <= 1 && cutN(env) > 1;
      const a = solo ? 0 : clamp(p / 0.12) * (1 - clamp((p - 0.84) / 0.16)), col = accentOf(env);
      addPost(it, (en) => {
        if (en.pass === "main") drawBrackets(en, V, b.cx, b.cy, half, b.w, b.h, it.size, col, a);
      });
    }
  });
  reg("exit", "tyToIndex", {
    name: "\u756A\u53F7\u306B\u5909\u308F\u308B",
    tags: ["editorial", "glitch", "graphic"],
    ae: "scatter",
    w: 0.8,
    apply(env, it, p) {
      const lay = layOf(it), ord = orders(env, it), sub = subOf(env), V = !!it.vertical;
      const num = /* @__PURE__ */ new Map();
      let k = 0;
      const plain = lay.filter((g) => !isSp(g.ch));
      const base = plain.length <= 1 ? Math.round(+it.mi || 0) : 0;
      for (const g of plain) num.set(g.i, String(base + ++k).padStart(2, "0"));
      it.charFns.push((i, g) => {
        const q = clamp((p - ord[i] * 0.4) / 0.6);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        if (q < 0.3) return { s: lerp(1, 0.55, E.inCubic(q / 0.3)) };
        const u = (q - 0.3) / 0.7;
        const o = { ch: num.get(i) || "00", s: 0.42, color: sub, a: 1 - E.inCubic(u) };
        if (V) o.dx = -g.w * 0.2 * u;
        else o.dy = -g.h * 0.2 * u;
        return o;
      });
    }
  });
  reg("exit", "tyKeyLast", {
    name: "\u4E00\u5B57\u6B8B\u3057",
    tags: ["emotional", "pop", "graphic"],
    ae: "shrink",
    w: 0.9,
    apply(env, it, p) {
      const lay = layOf(it), sx = it.sx || 1, sy = it.sy || 1;
      const plain = lay.filter((g) => !isSp(g.ch));
      const ki = plain.length > 1 ? keyOf(lay) : plain[0] ? plain[0].i : 0;
      const gk = lay.find((g) => g.i === ki) || plain[0];
      let md = 1;
      for (const g of plain) md = Math.max(md, Math.abs(g.i - ki));
      if (plain.length <= 1 && cutN(env) > 1) {
        const keep = J.isKanji((plain[0] || {}).ch || "") && J.h(env.cut.seed | 0, Math.round(+it.mi || 0), 5) % 2 === 0;
        it.charFns.push(() => {
          if (!keep) {
            const q2 = clamp(p / 0.5);
            return q2 >= 1 ? HIDE : { s: 1 - 0.6 * E.inCubic(q2), a: 1 - q2 };
          }
          const q = clamp((p - 0.4) / 0.6);
          return q >= 1 ? HIDE : { s: 1 + E.inCubic(q) * 1.4, a: 1 - E.inCubic(q) };
        });
        return;
      }
      it.charFns.push((i, g) => {
        if (i === ki) {
          const q2 = clamp((p - 0.45) / 0.55);
          return q2 >= 1 ? HIDE : { s: 1 + E.inCubic(q2) * 1.8, a: 1 - E.inCubic(q2) };
        }
        const d = Math.abs(i - ki) / md, q = clamp((p - (1 - d) * 0.2) / 0.45);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const e = E.inCubic(q);
        return { dx: (gk.x - g.x) * sx * e, dy: (gk.y - g.y) * sy * e, s: 1 - 0.6 * e, a: 1 - e };
      });
    }
  });
  reg("exit", "tyUnderSink", {
    name: "\u4E0B\u7DDA\u3078\u6C88\u3080",
    tags: ["editorial", "calm", "graphic"],
    ae: "fall",
    w: 1,
    apply(env, it, p) {
      const lines = lineInfo(it), ord = orders(env, it), V = !!it.vertical, sx = it.sx || 1, sy = it.sy || 1;
      it.charFns.push((i, g) => {
        const q = clamp((p - 0.2 - ord[i] * 0.4) / 0.35);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const e = E.inCubic(q), d = e * 1.25;
        return V ? { dx: -g.w * sx * d, clipX: [-0.55 + d, 0.7] } : { dy: g.h * sy * d, clipY: [-0.7, 0.55 - d] };
      });
      const qb = E.outExpo(clamp(p / 0.28)), qr = E.inCubic(clamp((p - 0.78) / 0.22)), ac = accentOf(env);
      addPost(it, (e, i) => inItem(e, i, () => {
        const lw = Math.max(2.5, i.size * 0.05);
        for (const L of lines) {
          const a = lerp(L.a0, L.a1, qr), b = lerp(L.a0, L.a1, qb);
          if (b - a < 1) continue;
          const off = V ? L.c - i.size * sx * 0.56 : L.c + i.size * sy * 0.56;
          e.line(V ? [[off, a], [off, b]] : [[a, off], [b, off]], ac, lw, 1, false);
        }
      }));
    }
  });
  reg("exit", "tyFoldVert", {
    name: "\u7E26\u7D44\u306B\u6298\u308C\u308B",
    tags: ["graphic", "editorial", "pop"],
    ae: "fall",
    w: 0.9,
    apply(env, it, p) {
      const lay = layOf(it), V = !!it.vertical, sx = it.sx || 1, sy = it.sy || 1;
      const ln = /* @__PURE__ */ new Map();
      for (const g of lay) {
        if (isSp(g.ch)) continue;
        const L = ln.get(g.li) || { gs: [], c0: 1e9, c1: -1e9 };
        L.gs.push(g);
        L.c0 = Math.min(L.c0, V ? g.y : g.x);
        L.c1 = Math.max(L.c1, V ? g.y : g.x);
        ln.set(g.li, L);
      }
      const e = E.inOutCubic(clamp(p / 0.5)), f = clamp((p - 0.36) / 0.5);
      it.charFns.push((i, g) => {
        if (f >= 1) return HIDE;
        const L = ln.get(g.li);
        if (!L) return null;
        const k = Math.max(0, L.gs.findIndex((q) => q.i === g.i)), n = L.gs.length, mid = (L.c0 + L.c1) / 2;
        const pitch = Math.min(1.02, (V ? env.W : env.H) * 0.82 / Math.max(1, n * (V ? g.w * sx : g.h * sy)));
        const off = (k - (n - 1) / 2) * pitch;
        let dx, dy;
        if (!V) {
          dx = (mid - g.x) * sx;
          dy = off * g.h * sy;
        } else {
          dy = (mid - g.y) * sy;
          dx = off * g.w * sx;
        }
        const drift = E.inQuad(f) * it.size * 0.25;
        return { dx: dx * e - (V ? drift : 0), dy: dy * e + (V ? 0 : drift), a: 1 - E.inQuad(f) };
      });
    }
  });
  reg("hold", "tyKeyPulse", {
    name: "\u4E00\u5B57\u306E\u9F13\u52D5",
    tags: ["emotional", "pop", "calm"],
    ae: "breathe",
    w: 1,
    apply(env, it, amt) {
      const lay = layOf(it), plain = lay.filter((g) => !isSp(g.ch));
      if (!plain.length) return;
      const b = env.beat;
      const pulse = b ? Math.exp(-b.since / Math.max(0.12, b.len * 0.35)) : Math.pow(0.5 + 0.5 * Math.sin(env.ltb * J.TAU * 0.95), 4);
      const k = 0.1 * amt * motionK(env) * pulse;
      if (k < 2e-3) return;
      if (plain.length === 1) {
        if (!J.isKanji(plain[0].ch)) return;
        it.charFns.push(() => ({ s: 1 + k * 0.7 }));
        return;
      }
      const ki = keyOf(lay);
      it.charFns.push((i) => i === ki ? { s: 1 + k } : null);
    }
  });
  reg("hold", "tyReadCursor", {
    name: "\u8AAD\u307F\u9001\u308A",
    tags: ["calm", "editorial", "pop"],
    ae: "wave",
    w: 1,
    apply(env, it, amt) {
      if (amt < 0.3) return;
      const lay = layOf(it), plain = lay.filter((g) => !isSp(g.ch)), n = plain.length;
      if (!n) return;
      const ac = accentOf(env), V = !!it.vertical, lift = it.size * 0.07 * Math.min(1, motionK(env));
      const rate = env.beat ? 1 / Math.max(0.2, env.beat.len) : 4;
      const tot = n <= 1 ? cutN(env) : n;
      const pos = Math.floor(env.ltb * rate) % (tot + 2);
      const frac = env.ltb * rate % 1, up = E.outCubic(clamp(frac * 4)) * (1 - E.inCubic(clamp((frac - 0.6) / 0.4)));
      if (n <= 1) {
        if (Math.round(+it.mi || 0) % (tot + 2) !== pos) return;
        it.charFns.push(() => V ? { dx: lift * up, color: ac } : { dy: -lift * up, color: ac });
        return;
      }
      const gi = pos < n ? plain[pos].i : -1;
      it.charFns.push((i) => i === gi ? V ? { dx: lift * up, color: ac } : { dy: -lift * up, color: ac } : null);
    }
  });
  reg("hold", "tyOutlineBlink", {
    name: "\u767D\u629C\u304D\u660E\u6EC5",
    tags: ["glitch", "graphic", "pop"],
    ae: "glitchtick",
    w: 0.9,
    apply(env, it, amt) {
      const lay = layOf(it), plain = lay.filter((g) => !isSp(g.ch));
      if (!plain.length) return;
      const seed = it.seed | 0, st = env.step >> 1;
      if (J.r(seed, st, 41) > 0.28 * amt * (0.5 + 0.5 * motionK(env)) / (plain.length === 1 ? Math.max(1, cutN(env) * 0.5) : 1)) return;
      const pick = plain[J.h(seed, st, 42) % plain.length].i;
      const two = plain.length > 3 && J.r(seed, st, 43) < 0.3 ? plain[J.h(seed, st, 44) % plain.length].i : -1;
      it.charFns.push((i) => i === pick || i === two ? { outline: true } : null);
    }
  });
  reg("hold", "tyTrackStep", {
    name: "\u5B57\u9593\u30B9\u30C6\u30C3\u30D7",
    tags: ["graphic", "editorial", "pop"],
    ae: "breathe",
    w: 0.8,
    apply(env, it, amt) {
      if (layOf(it).filter((g) => !isSp(g.ch)).length < 2) return;
      const len = env.beat ? Math.max(0.25, env.beat.len) : 0.55;
      const t = env.ltb / len, k = Math.floor(t), f = t - k;
      const lv = [0, 1, 2, 1], a = lv[k % 4], b = lv[(k + 1) % 4];
      const v = lerp(a, b, E.outExpo(clamp((f - 0.85) / 0.15)));
      it.track = (it.track || 0) + v * 0.05 * amt * Math.min(1.2, motionK(env));
      it._m = null;
      it._lay = null;
    }
  });
})();
