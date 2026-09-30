(() => {
  "use strict";
  const E = J.E;
  const P = "kinetic";
  const DEG = J.DEG, TAU = J.TAU, clamp = J.clamp, lerp = J.lerp;
  const HIDE = Object.freeze({ hide: true });
  const reg = (g, key, def) => J.register(g, key, Object.assign(def, { set: "kinetic" }), P);
  const strip = (t) => String(t || "").replace(/\s+/g, "");
  const hasLatin = (t) => /[A-Za-z]/.test(String(t || ""));
  const isBlank = (ch) => ch === " " || ch === "\u3000";
  const bellK = (k) => Math.sin(Math.PI * clamp(k));
  const rotV = (x, y, a) => {
    const c = Math.cos(a * DEG), s = Math.sin(a * DEG);
    return [x * c - y * s, x * s + y * c];
  };
  const kick = (t, k = 9, f = 26) => t <= 0 ? 0 : Math.exp(-k * t) * Math.sin(f * t);
  function bounceE(x) {
    const n1 = 7.5625, d1 = 2.75;
    if (x < 1 / d1) return n1 * x * x;
    if (x < 2 / d1) return n1 * (x -= 1.5 / d1) * x + 0.75;
    if (x < 2.5 / d1) return n1 * (x -= 2.25 / d1) * x + 0.9375;
    return n1 * (x -= 2.625 / d1) * x + 0.984375;
  }
  const stg = (p, k, n, spread) => clamp((p - (n > 1 ? k / (n - 1) : 0) * spread) / (1 - spread));
  const glyphs = (it, fn) => {
    (it.charFns || (it.charFns = [])).push(fn);
  };
  const mot = (env) => {
    var _a;
    return (_a = env.fx.motion) != null ? _a : 0.7;
  };
  const beatSince = (env, per) => {
    var _a;
    return env.beat && env.beat.len > 0.15 ? env.beat.since : (((_a = env.ltb) != null ? _a : env.lt) % per + per) % per;
  };
  const beatIdx = (env, per) => {
    var _a;
    return env.beat && env.beat.len > 0.15 ? env.beat.index : Math.floor(((_a = env.ltb) != null ? _a : env.lt) / per);
  };
  const geoCache = /* @__PURE__ */ new Map();
  function wordsFor(env, text) {
    const lat = hasLatin(text);
    let ws = (env.cut && env.cut.words ? env.cut.words : []).map(strip).filter(Boolean);
    const flatS = strip(text), all = ws.join("");
    let off = ws.length ? all.indexOf(flatS) : -1;
    if (off < 0) {
      ws = (lat ? String(text).split(/\s+/) : J.chunkText ? J.chunkText(String(text).replace(/\n/g, "")) : [text]).map(strip).filter(Boolean);
      off = 0;
    }
    return { ws, off };
  }
  function wgeo(env, it) {
    const text = String(it.text || "");
    const key = [text, it.font, it.track || 0, it.lead || 0, it.vertical ? 1 : 0, it.align || "", it.sx || 1, it.sy || 1, J.TYPESET ? 1 : 0, (env.cut && env.cut.words || []).join("")].join("");
    let G = geoCache.get(key);
    if (G) return G;
    const L1 = J.layoutText(Object.assign({}, it, { size: 1, _lay: null, _m: null }));
    const { ws, off } = wordsFor(env, text);
    const bnd = [];
    let acc = 0;
    ws.forEach((w) => {
      acc += [...w].length;
      bnd.push(acc);
    });
    const map = new Array(L1.length).fill(-1), inW = new Array(L1.length).fill(0);
    let k = 0, jmin = 1e9, jmax = -1;
    for (const g of L1) {
      if (isBlank(g.ch)) continue;
      const pos = off + k;
      let j = bnd.findIndex((b) => pos < b);
      if (j < 0) j = Math.max(0, bnd.length - 1);
      map[g.i] = j;
      jmin = Math.min(jmin, j);
      jmax = Math.max(jmax, j);
      k++;
    }
    if (jmax < 0) {
      jmin = 0;
      jmax = 0;
    }
    const nW = jmax - jmin + 1, sx = it.sx || 1, sy = it.sy || 1;
    const wb = [];
    for (let j = 0; j < nW; j++) wb.push({ x0: 1e9, x1: -1e9, y0: 1e9, y1: -1e9, n: 0 });
    for (const g of L1) {
      if (map[g.i] < 0) continue;
      const j = map[g.i] - jmin;
      map[g.i] = j;
      const b = wb[j], x = (g.x + g.vx) * sx, y = (g.y + g.vy) * sy;
      if (!b.n) b.li = g.li;
      inW[g.i] = b.n++;
      b.x0 = Math.min(b.x0, x - g.w * sx / 2);
      b.x1 = Math.max(b.x1, x + g.w * sx / 2);
      b.y0 = Math.min(b.y0, y - g.h * sy / 2);
      b.y1 = Math.max(b.y1, y + g.h * sy / 2);
    }
    wb.forEach((b) => {
      if (!b.n) {
        b.x0 = b.x1 = b.y0 = b.y1 = 0;
      }
      b.cx = (b.x0 + b.x1) / 2;
      b.cy = (b.y0 + b.y1) / 2;
    });
    let X0 = 1e9, X1 = -1e9, Y0 = 1e9, Y1 = -1e9;
    wb.forEach((b) => {
      if (!b.n) return;
      X0 = Math.min(X0, b.x0);
      X1 = Math.max(X1, b.x1);
      Y0 = Math.min(Y0, b.y0);
      Y1 = Math.max(Y1, b.y1);
    });
    if (X0 > X1) {
      X0 = X1 = Y0 = Y1 = 0;
    }
    G = { L1, map, inW, nW, wb, box: { x0: X0, x1: X1, y0: Y0, y1: Y1, cx: (X0 + X1) / 2, cy: (Y0 + Y1) / 2 }, vert: !!it.vertical };
    if (geoCache.size > 300) geoCache.clear();
    geoCache.set(key, G);
    return G;
  }
  const lineWords = (G) => {
    if (G._lw) return G._lw;
    const m = /* @__PURE__ */ new Map();
    for (let j = 0; j < G.nW; j++) {
      const li = G.wb[j].li | 0;
      if (!m.has(li)) m.set(li, []);
      m.get(li).push(j);
    }
    return G._lw = [...m.entries()];
  };
  const kOf = (G, g, it) => {
    const r = G.L1[g.i];
    return r && r.w > 1e-6 ? g.w / r.w : it.size;
  };
  const gpos = (g, it) => [(g.x + g.vx) * (it.sx || 1), (g.y + g.vy) * (it.sy || 1)];
  const WIN = (dur) => clamp(dur * 0.45, 0.32, 1);
  reg("enter", "knWordSlam", {
    // words slam down one after another from a huge scale; the ones already down take a knock at each impact
    name: "\u8A9E\u3054\u3068\u30B9\u30E9\u30E0",
    tags: ["pop", "graphic", "glitch"],
    w: 1.1,
    ae: "pop",
    minDur: 0.5,
    inDur: WIN,
    apply(env, it, p, ctx) {
      const G = wgeo(env, it), n = G.nW, sp = n > 1 ? 0.62 : 0, secs = ctx.inDur * (1 - sp);
      const land = (j) => stg(p, j, n, sp);
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const q = land(j);
        if (q <= 0) return HIDE;
        const k = kOf(G, g, it), b = G.wb[j], [gx, gy] = gpos(g, it);
        const e = E.outExpo(clamp(q * 1.35)), s = 1 + 2.1 * (1 - e);
        let dy = 0;
        for (let j2 = n - 1; j2 > j; j2--) {
          const q2 = land(j2);
          if (q2 >= 0.74) {
            dy = it.size * 0.09 * kick((q2 - 0.74) * secs, 11, 30);
            break;
          }
        }
        dy *= clamp((1 - p) * 10);
        return { s, dx: (gx - b.cx * k) * (s - 1), dy: (gy - b.cy * k) * (s - 1) + dy, a: clamp(q * 7), rot: (1 - e) * (j % 2 ? 7 : -7) };
      });
    }
  });
  reg("enter", "knTypeToSlam", {
    // each word is typed small at its start, then snaps up to full size
    name: "\u6253\u9375\u2192\u62E1\u5927",
    tags: ["pop", "editorial", "graphic"],
    w: 0.9,
    ae: "type",
    minDur: 0.55,
    inDur: WIN,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, sp = n > 1 ? 0.55 : 0;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const q = stg(p, j, n, sp);
        if (q <= 0) return HIDE;
        const b = G.wb[j], cnt = b.n, k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const tq = 0.55;
        if (q < tq && G.inW[i] >= Math.floor(q / tq * cnt + 1e-6) + 1) return HIDE;
        const e = q < tq ? 0 : E.outBack(clamp((q - tq) / (1 - tq)), 2.2), s = lerp(0.42, 1, e);
        const ax = G.vert ? b.cx * k : b.x0 * k, ay = G.vert ? b.y0 * k : b.cy * k;
        return { s, dx: (gx - ax) * (s - 1), dy: (gy - ay) * (s - 1), a: q < tq ? 0.85 : 1 };
      });
    }
  });
  reg("enter", "knReplaceIn", {
    // the words flash big in the middle one by one, replacing each other, then all fly to their places
    name: "\u5165\u308C\u66FF\u308F\u308A\u767B\u5834",
    tags: ["pop", "graphic", "glitch"],
    w: 0.9,
    ae: "scramble",
    minDur: 0.7,
    inDur: (dur) => clamp(dur * 0.5, 0.45, 1.2),
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, T = n > 1 ? 0.7 : 0.2, B = G.box, bw = Math.max(1e-3, G.vert ? B.y1 - B.y0 : B.x1 - B.x0);
      const fly = E.outExpo(clamp((p - T) / (1 - T)));
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const ww = Math.max(1e-3, G.vert ? b.y1 - b.y0 : b.x1 - b.x0), big = Math.min(2.4, Math.max(1, bw / ww * 0.8));
        const w0 = j / n * T, w1 = (j + 1) / n * T;
        let s, cx, cy, a = 1;
        if (p < T) {
          if (p < w0 || p >= w1) return HIDE;
          const q = (p - w0) / (w1 - w0);
          s = big * (1 + 0.35 * Math.exp(-q * 9));
          cx = B.cx * k;
          cy = B.cy * k;
          a = clamp(q * 8);
        } else {
          const last = j === n - 1, s0 = last ? big : 0.3;
          s = lerp(s0, 1, fly);
          cx = lerp(B.cx, b.cx, fly) * k;
          cy = lerp(B.cy, b.cy, fly) * k;
          a = last ? 1 : clamp(fly * 3);
        }
        return { s, dx: cx + (gx - b.cx * k) * s - gx, dy: cy + (gy - b.cy * k) * s - gy, a };
      });
    }
  });
  reg("enter", "knHingeDrop", {
    // each word swings down from upright on a hinge at its bottom-left corner and bounces level
    name: "\u8776\u756A\u304A\u308D\u3057",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "drop",
    minDur: 0.5,
    inDur: WIN,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, sp = n > 1 ? 0.5 : 0, dir = J.h(env.cut.seed | 0, it.mi | 0, 61) & 1 ? 1 : -1;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const q = stg(p, j, n, sp);
        if (q <= 0) return HIDE;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const th = -88 * dir * (1 - bounceE(q));
        const px = (dir > 0 ? b.x0 : b.x1) * k, py = b.y1 * k;
        const [rx, ry] = rotV(gx - px, gy - py, th);
        return { dx: px + rx - gx, dy: py + ry - gy, rot: th, a: clamp(q * 6) };
      });
    }
  });
  reg("enter", "knLoopIn", {
    // every glyph rides the same looping track into its place, one after another like a train
    name: "\u30EB\u30FC\u30D7\u5165\u308A",
    tags: ["pop", "graphic"],
    w: 0.8,
    ae: "spin",
    minDur: 0.5,
    inDur: (dur) => clamp(dur * 0.5, 0.4, 1.1),
    apply(env, it, p) {
      const vert = !!it.vertical, dir = J.h(env.cut.seed | 0, it.mi | 0, 63) & 1 ? 1 : -1;
      const D = Math.min(env.W, env.H) * 0.55, R = D / TAU * 1.5;
      glyphs(it, (i, g, n) => {
        const q = stg(p, dir > 0 ? i : n - 1 - i, n, 0.5);
        if (q <= 0) return HIDE;
        const v = 1 - E.outCubic(q), ph = TAU * v;
        const a0 = v * D - R * Math.sin(ph), b0 = -R * (1 - Math.cos(ph));
        const ta = D - TAU * R * Math.cos(ph), tb = -TAU * R * Math.sin(ph);
        const ang = Math.atan2(tb, ta * dir) / DEG * Math.min(1, v * 4);
        return vert ? { dx: b0, dy: -a0 * dir, rot: ang, a: clamp(q * 6) } : { dx: a0 * dir, dy: b0, rot: ang, a: clamp(q * 6) };
      });
    }
  });
  reg("enter", "knPushIn", {
    // words arrive at the end of the line one by one and push the ones before them into place
    name: "\u62BC\u3057\u8FBC\u307F",
    tags: ["pop", "editorial", "graphic"],
    w: 1,
    ae: "type",
    minDur: 0.45,
    inDur: WIN,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, vert = G.vert;
      const f = p * n, k0 = Math.min(n - 1, Math.floor(f)), e = E.outBack(clamp((f - k0) / 0.75), 1.5);
      const S = /* @__PURE__ */ new Map();
      for (const [li, js] of lineWords(G)) {
        const end = vert ? G.wb[js[js.length - 1]].y1 : G.wb[js[js.length - 1]].x1, far = (j) => end - (vert ? G.wb[j].y1 : G.wb[j].x1);
        const vis = js.filter((j) => j <= k0);
        if (!vis.length) continue;
        const last = vis[vis.length - 1], prev = vis.length > 1 ? vis[vis.length - 2] : -1;
        let v = far(last);
        if (last === k0) v += prev >= 0 ? (far(prev) - far(last)) * (1 - e) : (end - (vert ? G.wb[last].y0 : G.wb[last].x0)) * 0.6 * (1 - e);
        S.set(li, v);
      }
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        if (j > k0) return HIDE;
        const k = kOf(G, g, it), a = j === k0 ? clamp(e * 3) : 1, v = S.get(G.wb[j].li | 0) || 0;
        return vert ? { dy: v * k, a } : { dx: v * k, a };
      });
    }
  });
  reg("enter", "knInertia", {
    // the line brakes into place: the front stops first, the rest bunch up behind it and spring apart
    name: "\u6025\u30D6\u30EC\u30FC\u30AD",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "stretch",
    minDur: 0.45,
    inDur: (dur) => clamp(dur * 0.4, 0.3, 0.8),
    apply(env, it, p) {
      const vert = !!it.vertical, dir = J.h(env.cut.seed | 0, it.mi | 0, 67) & 1 ? 1 : -1;
      const D = (vert ? env.H : env.W) * 0.6;
      glyphs(it, (i, g, n) => {
        const back = n > 1 ? dir > 0 ? (n - 1 - i) / (n - 1) : i / (n - 1) : 0;
        const q = clamp((p - back * 0.3) / 0.7);
        const e = E.outBack(q, 2.1), v = (E.outBack(Math.min(1, q + 0.02), 2.1) - e) / 0.02;
        const off = -dir * D * (1 - e), st = 1 + Math.min(0.5, Math.abs(v) * 0.08);
        const sq = q > 0.55 ? 1 - 0.18 * back * bellK((q - 0.55) / 0.45) : 1;
        return vert ? { dy: off, sy: st * sq, sx: 1 / st } : { dx: off, sx: st * sq, sy: 1 / st };
      });
    }
  });
  reg("enter", "knWordSpin", {
    // each word spins in as one rigid piece, neighbours turning the opposite way
    name: "\u8A9E\u3054\u3068\u56DE\u8EE2",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "spin",
    inDur: WIN,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, sp = n > 1 ? 0.5 : 0;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const q = stg(p, j, n, sp);
        if (q <= 0) return HIDE;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const e = E.outBack(q, 1.6), th = (1 - E.outCubic(q)) * 200 * (j % 2 ? -1 : 1), s = lerp(0.15, 1, e);
        const [rx, ry] = rotV((gx - b.cx * k) * s, (gy - b.cy * k) * s, th);
        return { dx: b.cx * k + rx - gx, dy: b.cy * k + ry - gy, rot: th, s, a: clamp(q * 5) };
      });
    }
  });
  reg("enter", "knDiveIn", {
    // words fly in from behind the camera one after another, huge and soft, landing sharp
    name: "\u624B\u524D\u304B\u3089\u8A9E",
    tags: ["pop", "emotional", "graphic"],
    w: 0.9,
    ae: "zoom",
    inDur: WIN,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, sp = n > 1 ? 0.55 : 0;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const q = stg(p, j, n, sp);
        if (q <= 0) return HIDE;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const e = E.outCubic(q), s = lerp(5.5, 1, e);
        return { s, dx: (gx - b.cx * k) * (s - 1) + (b.cx - G.box.cx) * k * (s - 1) * 0.6, dy: (gy - b.cy * k) * (s - 1), a: clamp(q * 2.2) * lerp(0.35, 1, e) };
      });
    }
  });
  reg("enter", "knStretchOut", {
    // each word shoots out from its first letter like a tape measure and snaps back to length
    name: "\u4F38\u3073\u51FA\u3057",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "stretch",
    inDur: WIN,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, sp = n > 1 ? 0.55 : 0;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const q = stg(p, j, n, sp);
        if (q <= 0) return HIDE;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const s = q >= 1 ? 1 : Math.max(0.02, E.outElastic(q));
        return G.vert ? { dy: (gy - b.y0 * k) * (s - 1), sy: s, a: clamp(q * 8) } : { dx: (gx - b.x0 * k) * (s - 1), sx: s, a: clamp(q * 8) };
      });
    }
  });
  const WOUT = (dur) => clamp(dur * 0.32, 0.3, 0.75);
  reg("exit", "knWordKick", {
    // the words are kicked out one after another, up and down in turn, tumbling
    name: "\u8A9E\u3054\u3068\u8E74\u308A\u51FA\u3057",
    tags: ["pop", "graphic"],
    w: 1,
    ae: "scatter",
    outDur: WOUT,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, sp = n > 1 ? 0.4 : 0, H = env.H;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const q = stg(p, j, n, sp);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const up = j % 2 ? 1 : -1, e = E.inCubic(q), th = up * 70 * e;
        const [rx, ry] = rotV(gx - b.cx * k, gy - b.cy * k, th);
        return { dx: b.cx * k + rx - gx + up * it.size * 0.6 * e, dy: b.cy * k + ry - gy + up * H * 0.9 * e - up * it.size * 0.25 * bellK(q * 2), rot: th, a: 1 - E.inQuad(clamp((q - 0.45) / 0.55)) };
      });
    }
  });
  reg("exit", "knPushOut", {
    // the line is shunted along in word-sized steps; each word fades as it passes the line's start
    name: "\u62BC\u3057\u51FA\u3057\u9000\u5834",
    tags: ["editorial", "graphic", "pop"],
    w: 0.9,
    ae: "wipe",
    outDur: WOUT,
    apply(env, it, p) {
      var _a;
      const G = wgeo(env, it), vert = G.vert;
      const st = (j) => vert ? G.wb[j].y0 : G.wb[j].x0, en = (j) => vert ? G.wb[j].y1 : G.wb[j].x1;
      const S = /* @__PURE__ */ new Map(), start = /* @__PURE__ */ new Map();
      for (const [li, js] of lineWords(G)) {
        const m = js.length, s0 = st(js[0]), f = p * m, k0 = Math.min(m - 1, Math.floor(f)), e = E.outBack(clamp((f - k0) / 0.8), 1.6);
        const to = (q) => q >= m ? en(js[m - 1]) - s0 + 0.2 : st(js[q]) - s0;
        S.set(li, lerp(to(k0), to(k0 + 1), e));
        start.set(li, s0);
      }
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const li = G.wb[j].li | 0, sh = S.get(li) || 0, k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const pos = (vert ? gy / k : gx / k) - sh, over = (start.get(li) || 0) - pos;
        const a = (1 - clamp(over / 0.45)) * (1 - J.smooth(0.72, 1, p));
        if (a <= 0.01) return HIDE;
        return vert ? { dy: -sh * k, a } : { dx: -sh * k, a };
      });
      if (p > 0.97) it.alpha = ((_a = it.alpha) != null ? _a : 1) * (1 - clamp((p - 0.97) / 0.03));
    }
  });
  reg("exit", "knDiveGlyph", {
    // the camera dives into one letter: the line blows up around it and the rest flies past
    name: "\u4E00\u5B57\u3078\u7A81\u5165",
    tags: ["pop", "emotional", "graphic"],
    w: 0.9,
    ae: "shrink",
    outDur: (dur) => clamp(dur * 0.3, 0.3, 0.7),
    apply(env, it, p) {
      var _a;
      const lay = J.layoutText(Object.assign({}, it, { size: 1, _lay: null, _m: null })).filter((g) => !isBlank(g.ch));
      if (!lay.length) return;
      const f = lay.find((g) => J.isKanji(g.ch)) || lay[Math.floor(lay.length / 2)];
      const one = lay.length <= 1, fi = f.i, S = Math.exp(Math.pow(p, 1.7) * Math.log(one ? 3 : 34));
      if (one) {
        it.alpha = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.3, 1, p));
      } else {
        const e = E.inOutCubic(p), fx0 = (f.x + f.vx) * (it.sx || 1) * it.size, fy0 = (f.y + f.vy) * (it.sy || 1) * it.size;
        if (!it.rot) {
          it.x += (env.W / 2 - (it.x + fx0)) * e;
          it.y += (env.H / 2 - (it.y + fy0)) * e;
        }
      }
      let fx = null, fy = null;
      glyphs(it, (i, g, n) => {
        if (fx == null) {
          const k = g.w / Math.max(1e-6, (lay.find((q) => q.i === g.i) || f).w);
          fx = (f.x + f.vx) * (it.sx || 1) * k;
          fy = (f.y + f.vy) * (it.sy || 1) * k;
        }
        const [gx, gy] = gpos(g, it);
        const a = i === fi ? 1 - J.smooth(0.6, 1, p) : 1 - J.smooth(0.25, 0.7, p);
        if (a <= 0.01) return HIDE;
        return { s: S, dx: (gx - fx) * (S - 1), dy: (gy - fy) * (S - 1), a };
      });
    }
  });
  reg("exit", "knLaunch", {
    // the line pulls away: the front glyph goes first, the rest follow on a stretching chain
    name: "\u6025\u767A\u9032",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "stretch",
    outDur: (dur) => clamp(dur * 0.3, 0.3, 0.65),
    apply(env, it, p) {
      const vert = !!it.vertical, dir = J.h(env.cut.seed | 0, it.mi | 0, 71) & 1 ? 1 : -1, D = (vert ? env.H : env.W) * 1.25;
      glyphs(it, (i, g, n) => {
        const back = n > 1 ? dir > 0 ? (n - 1 - i) / (n - 1) : i / (n - 1) : 0;
        const q = clamp((p - back * 0.35) / 0.65);
        const antic = -0.06 * bellK(clamp(p / 0.18));
        const e = E.inCubic(q) + antic, v = 3 * q * q;
        const st = 1 + Math.min(1.2, v * 0.35);
        if (q >= 1) return HIDE;
        return vert ? { dy: dir * D * e, sy: st, sx: 1 / Math.sqrt(st) } : { dx: dir * D * e, sx: st, sy: 1 / Math.sqrt(st) };
      });
    }
  });
  reg("exit", "knWordBlink", {
    // one word at a time: a punch in the accent colour, then gone — on the beat when there is one
    name: "\u4E00\u8A9E\u305A\u3064\u6D88\u706F",
    tags: ["pop", "glitch", "graphic"],
    w: 0.9,
    ae: "cut",
    outDur: (dur) => clamp(dur * 0.35, 0.3, 0.8),
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, rev = (J.h(env.cut.seed | 0, 73) & 1) === 1, acc = env.sc.accent;
      glyphs(it, (i, g) => {
        const j0 = G.map[i];
        if (j0 < 0) return null;
        const j = rev ? n - 1 - j0 : j0;
        const w0 = n > 1 ? j / n : J.r(env.cut.seed | 0, it.mi | 0, 74) * 0.6, w1 = n > 1 ? (j + 1) / n : w0 + 0.4;
        if (p >= w1 || p >= 0.999) return HIDE;
        if (p < w0) return null;
        const q = (p - w0) / (w1 - w0), b = G.wb[j0], k = kOf(G, g, it), [gx, gy] = gpos(g, it), s = 1 + 0.16 * bellK(q * 1.4);
        return { s, dx: (gx - b.cx * k) * (s - 1), dy: (gy - b.cy * k) * (s - 1), color: q > 0.25 ? acc : null, a: q > 0.7 ? 0.35 : 1 };
      });
    }
  });
  reg("exit", "knCloseGap", {
    // words drop out one by one and the rest slide together, re-centring, until the last one pops
    name: "\u8A70\u3081\u3066\u6D88\u3048\u308B",
    tags: ["editorial", "graphic", "pop"],
    w: 0.9,
    ae: "shrink",
    outDur: (dur) => clamp(dur * 0.35, 0.35, 0.8),
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, vert = G.vert;
      const lo = (j) => vert ? G.wb[j].y0 : G.wb[j].x0, hi = (j) => vert ? G.wb[j].y1 : G.wb[j].x1;
      const ord = [];
      for (let a = 0, b = n - 1; a <= b; a++, b--) {
        ord.push(a);
        if (b !== a) ord.push(b);
      }
      const rank = new Array(n);
      ord.forEach((j, r) => {
        rank[j] = r;
      });
      const c = new Array(n);
      for (let j = 0; j < n; j++) c[j] = E.inOutCubic(clamp(p * n - rank[j]));
      const ctr = new Array(n).fill(0), byLine = /* @__PURE__ */ new Map();
      for (let j = 0; j < n; j++) {
        const li = G.wb[j].li | 0;
        if (!byLine.has(li)) byLine.set(li, []);
        byLine.get(li).push(j);
      }
      for (const js of byLine.values()) {
        const m = js.length, wid = js.map((j) => hi(j) - lo(j)), gap = js.map((j, q) => q < m - 1 ? lo(js[q + 1]) - hi(j) : 0);
        const cc = (q) => q < m - 1 ? gap[q] * (1 - Math.max(c[js[q]], c[js[q + 1]])) : 0;
        let tot = 0;
        for (let q = 0; q < m; q++) tot += wid[q] * (1 - c[js[q]]) + cc(q);
        let x = (lo(js[0]) + hi(js[m - 1])) / 2 - tot / 2;
        for (let q = 0; q < m; q++) {
          const w = wid[q] * (1 - c[js[q]]);
          ctr[js[q]] = x + w / 2;
          x += w + cc(q);
        }
      }
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        if (c[j] >= 0.999) return HIDE;
        const k = kOf(G, g, it), [gx, gy] = gpos(g, it), s = 1 - c[j], wc = (lo(j) + hi(j)) / 2;
        const along = vert ? gy : gx, cross = vert ? gx : gy, cc = vert ? G.wb[j].cx : G.wb[j].cy;
        const na = ctr[j] * k + (along - wc * k) * s, nc = cc * k + (cross - cc * k) * s;
        return vert ? { dx: nc - gx, dy: na - gy, s: Math.max(0.01, s) } : { dx: na - gx, dy: nc - gy, s: Math.max(0.01, s) };
      });
    }
  });
  reg("exit", "knJumpCutOut", {
    // three hard jump cuts (closer, wider, tilted) and the line is gone — no in-betweens
    name: "\u30B8\u30E3\u30F3\u30D7\u30AB\u30C3\u30C8",
    tags: ["pop", "glitch", "graphic"],
    w: 0.8,
    ae: "glitch",
    outDur: (dur) => clamp(dur * 0.3, 0.3, 0.6),
    apply(env, it, p) {
      const sd = J.h(env.cut.seed | 0, it.mi | 0, 79), side = sd & 1 ? 1 : -1;
      const st = Math.floor(clamp(p) * 4);
      if (st >= 3) {
        it.alpha = 0;
        return;
      }
      const S = [[1.22, 0.05 * side, -0.02, 0], [0.8, -0.07 * side, 0.03, 0], [1.5, 0.02 * side, 0, 3 * side]][st];
      it.size *= S[0];
      it.x += S[1] * env.W;
      it.y += S[2] * env.H;
      it.rot = (it.rot || 0) + S[3];
      it._lay = null;
      it._m = null;
    }
  });
  reg("exit", "knStackAway", {
    // the words hop into a tower one by one, then the whole tower drops out of frame
    name: "\u7A4D\u3093\u3067\u843D\u3068\u3059",
    tags: ["pop", "graphic"],
    w: 0.8,
    ae: "fall",
    outDur: (dur) => clamp(dur * 0.4, 0.45, 0.9),
    minDur: 0.9,
    apply(env, it, p) {
      const G = wgeo(env, it), n = G.nW, T = 0.62, H = env.H;
      const lh = G.vert ? null : G.box.y1 - G.box.y0;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const q = E.inOutCubic(clamp((p * (1 / T) - j / Math.max(1, n) * 0.7) / 0.3 + 0));
        const h = G.vert ? b.x1 - b.x0 : Math.max(1e-3, b.y1 - b.y0);
        const tx = G.vert ? G.box.cx - (j - (n - 1) / 2) * h * 1.05 : G.box.cx, ty = G.vert ? G.box.cy : G.box.cy - (j - (n - 1) / 2) * h * 1.05;
        const hop = -bellK(q) * (lh || h) * 0.8;
        let dx = (tx - b.cx) * k * q, dy = (ty - b.cy) * k * q + hop * k;
        const fall = clamp((p - T) / (1 - T));
        dy += E.inQuad(fall) * H * 1.2;
        const rot = fall * (j % 2 ? 8 : -8);
        if (fall >= 1) return HIDE;
        return { dx, dy, rot, a: 1 - J.smooth(0.8, 1, fall) };
      });
    }
  });
  reg("hold", "knWordPulse", {
    // one word at a time swells on the beat, cycling through the line
    name: "\u8A9E\u3054\u3068\u306E\u62CD",
    tags: ["pop", "graphic"],
    w: 1,
    ae: "breathe",
    apply(env, it, amt) {
      const G = wgeo(env, it), n = G.nW;
      if (n < 1) return;
      const per = 0.52, s0 = beatSince(env, per), cur = (beatIdx(env, per) % n + n) % n;
      const pulse = Math.exp(-s0 * 4) * 0.14 * amt * (0.4 + mot(env));
      if (pulse < 2e-3) return;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j !== cur) return null;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it), s = 1 + pulse;
        return { s, dx: (gx - b.cx * k) * (s - 1), dy: (gy - b.cy * k) * (s - 1) };
      });
    }
  });
  reg("hold", "knCounterRock", {
    // neighbouring words rock the opposite way, like meshed gears
    name: "\u9006\u56DE\u8EE2\u3086\u308C",
    tags: ["pop", "calm", "graphic"],
    w: 0.9,
    ae: "wave",
    apply(env, it, amt) {
      var _a;
      const G = wgeo(env, it), n = G.nW, t = (_a = env.ltb) != null ? _a : env.lt;
      const A = 4 * amt * (0.4 + mot(env)) * Math.sin(t * TAU * 0.55);
      if (Math.abs(A) < 0.05) return;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const th = A * (j % 2 ? -1 : 1) * (n === 1 ? 0.6 : 1);
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const [rx, ry] = rotV(gx - b.cx * k, gy - b.cy * k, th);
        return { dx: b.cx * k + rx - gx, dy: b.cy * k + ry - gy, rot: th };
      });
    }
  });
  reg("hold", "knWordRide", {
    // a slow swell travels along the line; each word rides it as one piece, tilting with the slope
    name: "\u8A9E\u306E\u6CE2\u4E57\u308A",
    tags: ["calm", "emotional", "pop"],
    w: 0.9,
    ae: "wave",
    apply(env, it, amt) {
      var _a;
      const G = wgeo(env, it), t = (_a = env.ltb) != null ? _a : env.lt, A = it.size * 0.07 * amt * (0.4 + mot(env));
      if (A < 0.2) return;
      glyphs(it, (i, g) => {
        const j = G.map[i];
        if (j < 0) return null;
        const b = G.wb[j], k = kOf(G, g, it), [gx, gy] = gpos(g, it);
        const ph = t * 3.1 - j * 1.25, off = A * Math.sin(ph), th = Math.cos(ph) * 3.5 * amt;
        const [rx, ry] = rotV(gx - b.cx * k, gy - b.cy * k, th);
        return G.vert ? { dx: b.cx * k + rx - gx + off, dy: b.cy * k + ry - gy, rot: th } : { dx: b.cx * k + rx - gx, dy: b.cy * k + ry - gy + off, rot: th };
      });
    }
  });
  reg("hold", "knTickShift", {
    // the line ticks sideways on every beat like a second hand: snap, tiny overshoot, hold
    name: "\u523B\u307F\u30B7\u30D5\u30C8",
    tags: ["graphic", "pop", "editorial"],
    w: 0.8,
    ae: "jitter",
    apply(env, it, amt) {
      const per = 0.5, s0 = beatSince(env, per), idx = beatIdx(env, per);
      const d = it.size * 0.05 * amt * (0.4 + mot(env));
      if (d < 0.2) return;
      const from = idx % 2 ? 1 : -1, e = E.outBack(clamp(s0 / 0.09), 2.6);
      const x = lerp(-from, from, e) * d;
      if (it.vertical) it.y += x;
      else it.x += x;
    }
  });
  reg("hold", "knBeatLean", {
    // on each beat the words lean over, alternately forward and back, and spring upright
    name: "\u62CD\u3067\u50BE\u304F",
    tags: ["pop", "glitch", "graphic"],
    w: 0.8,
    ae: "jitter",
    apply(env, it, amt) {
      const G = wgeo(env, it), s0 = beatSince(env, 0.55), idx = beatIdx(env, 0.55);
      const A = 18 * amt * (0.4 + mot(env)) * Math.exp(-s0 * 4.5) * Math.cos(s0 * 15);
      if (Math.abs(A) < 0.1) return;
      glyphs(it, (i) => {
        const j = G.map[i];
        if (j < 0) return null;
        return { skew: A * ((j + idx) % 2 ? 1 : -1) };
      });
    }
  });
  reg("hold", "knGapBreath", {
    // the spaces between words breathe in and out; the words themselves keep still
    name: "\u8A9E\u9593\u306E\u547C\u5438",
    tags: ["calm", "editorial", "emotional"],
    w: 0.9,
    ae: "breathe",
    apply(env, it, amt) {
      var _a;
      const G = wgeo(env, it), n = G.nW, t = (_a = env.ltb) != null ? _a : env.lt;
      if (n < 2) return;
      const A = it.size * 0.16 * amt * (0.4 + mot(env)) * (0.5 - 0.5 * Math.cos(t * TAU * 0.4));
      if (A < 0.2) return;
      glyphs(it, (i) => {
        const j = G.map[i];
        if (j < 0) return null;
        const o = (j - (n - 1) / 2) * A;
        return G.vert ? { dy: o } : { dx: o };
      });
    }
  });
})();
