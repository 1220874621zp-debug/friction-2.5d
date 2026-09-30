(() => {
  "use strict";
  const E = J.E;
  const P = "exitB";
  const DEG = J.DEG, TAU = J.TAU;
  const HIDE = Object.freeze({ hide: true });
  const isHex = (c) => typeof c === "string" && /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.test(c);
  const mixC = (a, b, t) => {
    t = J.clamp(t);
    if (t <= 2e-3) return a;
    if (t >= 0.998) return b;
    return isHex(a) && isHex(b) ? J.mix(a, b, t) : t < 0.5 ? a : b;
  };
  const colOf = (it) => isHex(it.color) ? it.color : "#ffffff";
  const motionK = (env) => J.clamp((env.fx && env.fx.motion != null ? env.fx.motion : 0.7) / 0.7, 0, 1.6);
  const isSp = (ch) => ch === " " || ch === "\u3000";
  const layOf = (it) => (it._m || (it._m = J.measure(it))).lay;
  const box = (it) => {
    it._m = J.measure(it);
    return J.itemBox(it);
  };
  const cutN = (env) => Math.max(1, J.glyphCount(String(env.cut && env.cut.text || "")));
  const cutBit = (env, k) => (J.h(env.cut && env.cut.seed | 0, k, 1991) & 1) === 1;
  const cutR = (env, k) => J.r(env.cut && env.cut.seed | 0, k, 1993);
  const isSingle = (env, it) => layOf(it).N === 1 && cutN(env) > 1;
  const win = (p, o, spread) => J.clamp((p - o * spread) / (1 - spread));
  const rot2 = (x, y, r) => {
    const c = Math.cos(r), s = Math.sin(r);
    return [x * c - y * s, x * s + y * c];
  };
  const toD = (it, lx, ly) => {
    const [x, y] = rot2(lx, ly, (it.rot || 0) * DEG);
    return [it.x + x, it.y + y];
  };
  const toL = (it, X2, Y) => rot2(X2 - it.x, Y - it.y, -(it.rot || 0) * DEG);
  const downI = (it) => {
    const r = (it.rot || 0) * DEG;
    return [Math.sin(r), Math.cos(r)];
  };
  const rightI = (it) => {
    const r = (it.rot || 0) * DEG;
    return [Math.cos(r), -Math.sin(r)];
  };
  const dirI = (it, X2, Y) => {
    const [a, b] = rightI(it), [c, d] = downI(it);
    return [a * X2 + c * Y, b * X2 + d * Y];
  };
  const fadeEnd = (p, a = 0.85) => 1 - J.smooth(a, 1, p);
  const easeInPow = (x, k) => Math.pow(J.clamp(x), k);
  function orderOf(env, it) {
    const N = cutN(env), mi = +it.mi || 0;
    return (i, n) => n > 1 ? i / (n - 1) : N > 1 ? J.clamp(mi / (N - 1)) : 0;
  }
  function merged(fns) {
    return (i, g, n) => {
      let o = null;
      for (let k = 0; k < fns.length; k++) {
        const r = fns[k](i, g, n);
        if (!r) continue;
        if (r.hide) return HIDE;
        if (!o) o = { dx: 0, dy: 0, rot: 0, s: 1, a: 1 };
        if (r.dx) o.dx += r.dx;
        if (r.dy) o.dy += r.dy;
        if (r.rot) o.rot += r.rot;
        if (r.s != null) o.s *= r.s;
        if (r.a != null) o.a *= r.a;
        if (r.sx != null) o.sx = (o.sx == null ? 1 : o.sx) * r.sx;
        if (r.sy != null) o.sy = (o.sy == null ? 1 : o.sy) * r.sy;
        if (r.skew) o.skew = (o.skew || 0) + r.skew;
        if (r.blur) o.blur = (o.blur || 0) + r.blur;
        if (r.outline) o.outline = true;
        if (r.ch) o.ch = r.ch;
        if (r.color) o.color = r.color;
        if (r.clipX) o.clipX = o.clipX ? [Math.max(o.clipX[0], r.clipX[0]), Math.min(o.clipX[1], r.clipX[1])] : r.clipX;
        if (r.clipY) o.clipY = o.clipY ? [Math.max(o.clipY[0], r.clipY[0]), Math.min(o.clipY[1], r.clipY[1])] : r.clipY;
      }
      if (!o) return null;
      if (o.a <= 3e-3 || Math.abs(o.s) < 4e-3) return HIDE;
      if (o.sx != null && Math.abs(o.sx) < 4e-3) return HIDE;
      if (o.sy != null && Math.abs(o.sy) < 4e-3) return HIDE;
      if (o.clipX && o.clipX[1] <= o.clipX[0] || o.clipY && o.clipY[1] <= o.clipY[0]) return HIDE;
      return o;
    };
  }
  function addC(it, fn) {
    if (!it.charFns) it.charFns = [];
    const prev = it.charFns.splice(0);
    prev.push(fn);
    it.charFns.push(merged(prev));
  }
  function chainPost(it, fn) {
    const p0 = it.post;
    it.post = (env, it2, bb) => {
      if (p0) p0(env, it2, bb);
      fn(env, it2, bb);
    };
  }
  function chainPre(it, fn) {
    const p0 = it.pre;
    it.pre = (env, it2) => {
      if (p0) p0(env, it2);
      fn(env, it2);
    };
  }
  function withCopies(it, mainFn, copyFns, extra) {
    if (!it.charFns) it.charFns = [];
    const prev = it.charFns.slice();
    const cfs = copyFns.map((f) => merged(prev.concat([f])));
    addC(it, mainFn);
    chainPost(it, (env, it2) => {
      for (let k = 0; k < cfs.length; k++) {
        const ex = typeof extra === "function" ? extra(k, env) : extra;
        const c = Object.assign({}, it2, { charFn: cfs[k], pieceFn: null, pre: null, post: null, streak: null, echo: null }, ex || null);
        J.drawItem(env, c);
      }
    });
  }
  const COPY_RESET = { pre: null, post: null, streak: null, echo: null, wipeBar: null, cursorAt: -1, clip: null, clipY: null, clipFn: null, bands: null, vbands: null, pieceFn: null };
  const copyOf = (it2, over) => Object.assign({}, it2, COPY_RESET, over || null);
  function inItem(env, it, fn) {
    const ctx = env.ctx;
    ctx.save();
    ctx.translate(it.x, it.y);
    if (it.rot) ctx.rotate(it.rot * DEG);
    try {
      fn(ctx);
    } finally {
      ctx.restore();
    }
  }
  function lineExt(it) {
    const lay = J.layoutText(it), sx = it.sx || 1, sy = it.sy || 1, out = [];
    for (const g of lay) {
      if (isSp(g.ch)) continue;
      const L = out[g.li] || (out[g.li] = { li: g.li, x0: 1e9, x1: -1e9, y0: 1e9, y1: -1e9 });
      const hw = (it.vertical ? it.size : g.w) / 2, hh = (it.vertical ? g.h : it.size) / 2;
      L.x0 = Math.min(L.x0, (g.x - hw) * sx);
      L.x1 = Math.max(L.x1, (g.x + hw) * sx);
      L.y0 = Math.min(L.y0, (g.y - hh) * sy);
      L.y1 = Math.max(L.y1, (g.y + hh) * sy);
    }
    return out.filter(Boolean);
  }
  function lBox(it, pad = 0) {
    const b = box(it);
    return { x0: b.x0 - it.x - pad, y0: b.y0 - it.y - pad, x1: b.x1 - it.x + pad, y1: b.y1 - it.y + pad, cx: b.cx - it.x, cy: b.cy - it.y, w: b.w + pad * 2, h: b.h + pad * 2 };
  }
  function dBox(it, pad = 0) {
    const b = box(it);
    let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (const [px, py] of [[b.x0, b.y0], [b.x1, b.y0], [b.x0, b.y1], [b.x1, b.y1]]) {
      const [X2, Y] = toD(it, px - it.x, py - it.y);
      x0 = Math.min(x0, X2);
      x1 = Math.max(x1, X2);
      y0 = Math.min(y0, Y);
      y1 = Math.max(y1, Y);
    }
    const sk = Math.abs(Math.tan((it.skew || 0) * DEG)) * b.h * 0.5;
    return { x0: x0 - pad - sk, y0: y0 - pad, x1: x1 + pad + sk, y1: y1 + pad, cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, w: x1 - x0 + 2 * pad + 2 * sk, h: y1 - y0 + 2 * pad };
  }
  function scaleAbout(it, kx, ky, ax, ay) {
    if (ax == null) {
      const b = box(it);
      ax = b.cx - it.x;
      ay = b.cy - it.y;
    }
    const [dx, dy] = rot2(ax * (1 - kx), ay * (1 - ky), (it.rot || 0) * DEG);
    it.x += dx;
    it.y += dy;
    it.sx = (it.sx || 1) * kx;
    it.sy = (it.sy || 1) * ky;
    it._m = null;
  }
  function sizeAbout(it, k) {
    const b = box(it), ax = b.cx - it.x, ay = b.cy - it.y;
    const [dx, dy] = rot2(ax * (1 - k), ay * (1 - k), (it.rot || 0) * DEG);
    it.x += dx;
    it.y += dy;
    it.size *= k;
    it._m = null;
  }
  function rotateAbout(it, deg, ax, ay) {
    const r0 = (it.rot || 0) * DEG, r1 = r0 + deg * DEG;
    const [x0, y0] = rot2(ax, ay, r0), [x1, y1] = rot2(ax, ay, r1);
    it.x += x0 - x1;
    it.y += y0 - y1;
    it.rot = (it.rot || 0) + deg;
  }
  function gclip(g, it, gx, gy, kx, ky, xr, yr) {
    const sx = (it.sx || 1) * kx, sy = (it.sy || 1) * ky, o = {};
    if (!g.r90) {
      if (xr) o.clipX = [(xr[0] - gx) / (g.w * sx), (xr[1] - gx) / (g.w * sx)];
      if (yr) o.clipY = [(yr[0] - gy) / (g.h * sy), (yr[1] - gy) / (g.h * sy)];
    } else {
      if (yr) o.clipX = [(yr[0] - gy) / (sx * g.w), (yr[1] - gy) / (sx * g.w)];
      if (xr) o.clipY = [-(xr[1] - gx) / (sy * g.h), -(xr[0] - gx) / (sy * g.h)];
    }
    return o;
  }
  const gCX = (g, it) => (g.x + g.vx) * (it.sx || 1);
  const gCY = (g, it) => (g.y + g.vy) * (it.sy || 1);
  function halfPlane(ctx, dx, dy, s, sign, L = 1e5) {
    const tx = -dy, ty = dx, a = sign > 0 ? s : s - L, b = sign > 0 ? s + L : s;
    ctx.moveTo(dx * a + tx * L, dy * a + ty * L);
    ctx.lineTo(dx * b + tx * L, dy * b + ty * L);
    ctx.lineTo(dx * b - tx * L, dy * b - ty * L);
    ctx.lineTo(dx * a - tx * L, dy * a - ty * L);
    ctx.closePath();
  }
  function polyL(ctx, it, pts) {
    for (let k = 0; k < pts.length; k++) {
      const [X2, Y] = toD(it, pts[k][0], pts[k][1]);
      k ? ctx.lineTo(X2, Y) : ctx.moveTo(X2, Y);
    }
    ctx.closePath();
  }
  function glyphBlur(it) {
    if (!it.charFns || !it.charFns.length) return false;
    const lay = layOf(it);
    for (const g of lay) for (const f of it.charFns) {
      const r = f(g.i, g, lay.N);
      if (r && r.blur > 0.4) return true;
    }
    return false;
  }
  function scaleGlyphBlur(it, k) {
    if (!it.charFns || !it.charFns.length) return;
    const m = merged(it.charFns.splice(0));
    it.charFns.push((i, g, n) => {
      const r = m(i, g, n);
      return r && r.blur ? Object.assign({}, r, { blur: r.blur * k }) : r;
    });
  }
  const beatLen = (env, fb) => env.beat && env.beat.len ? env.beat.len : fb;
  const beatSince = (env, fb) => env.beat ? env.beat.since : (env.ltb % fb + fb) % fb;
  const beatIdx = (env, fb) => env.beat ? env.beat.index : Math.floor(env.ltb / fb);
  const darkBg = (env) => J.lum(env.sc.bg) < 0.5;
  const primary = (it) => {
    var _a;
    return ((_a = it.alpha) != null ? _a : 1) > 0.9 && it.fill !== false;
  };
  const X = {};
  X.peelOff = {
    name: "\u30B9\u30C6\u30C3\u30AB\u30FC\u5265\u304C\u3057",
    tags: ["pop", "graphic"],
    w: 1,
    ae: "wipe",
    outDur: (dur) => J.clamp(dur * 0.36, 0.3, 0.7),
    apply(env, it, p) {
      var _a;
      const q = isSingle(env, it) ? win(p, orderOf(env, it)(0, 1), 0.4) : p;
      if (q <= 0) return;
      const v = J.h(env.cut.seed | 0, 311) >>> 5 & 3;
      const dx = [0.83, -0.83, 0.83, -0.83][v], dy = [0.56, 0.56, -0.56, -0.56][v];
      const sz = it.size, bb = dBox(it, sz * 0.06);
      const us = [[bb.x0, bb.y0], [bb.x1, bb.y0], [bb.x0, bb.y1], [bb.x1, bb.y1]].map((c) => c[0] * dx + c[1] * dy);
      const u0 = Math.min(...us), u1 = Math.max(...us);
      const PE = 0.74, f = J.clamp(q / PE), fly = J.clamp((q - PE) / (1 - PE));
      const s = J.lerp(u0, u1, 0.1 * f + 0.9 * f * f * (3 - 2 * f));
      const a0 = (_a = it.alpha) != null ? _a : 1, c0 = colOf(it), bg = env.sc.bg;
      const back = mixC(mixC(c0, bg, 0.5), env.sc.accent, 0.18);
      if (f < 1) it.clipFn = (ctx) => halfPlane(ctx, dx, dy, s, 1);
      else it.alpha = 0;
      chainPost(it, (env2, it2) => {
        const ctx = env2.ctx;
        const fa = a0 * (1 - E.inQuad(fly));
        if (fa <= 0.01) return;
        const lift = sz * (0.05 + 0.1 * f);
        const flap = copyOf(it2, {
          alpha: fa,
          color: back,
          gradient: null,
          pattern: null,
          extrude: null,
          blur: 0,
          ghost: false,
          shadow: env2.pass === "main" ? { color: J.rgba("#000000", darkBg(env2) ? 0.5 : 0.28), blur: sz * 0.12, dx: dx * lift, dy: dy * lift + lift } : null
        });
        ctx.save();
        if (fly <= 0) {
          ctx.beginPath();
          halfPlane(ctx, dx, dy, s, 1);
          ctx.clip();
        } else {
          const m = sz * 2.2 * fly * fly;
          ctx.translate(dx * m, dy * m - sz * 1.2 * fly);
        }
        ctx.transform(1 - 2 * dx * dx, -2 * dx * dy, -2 * dx * dy, 1 - 2 * dy * dy, 2 * s * dx, 2 * s * dy);
        J.drawItem(env2, flap);
        ctx.restore();
      });
    }
  };
  X.crumpleOut = {
    name: "\u4E38\u3081\u3066\u6368\u3066\u308B",
    tags: ["pop", "emotional"],
    w: 0.9,
    ae: "shrink",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.42, 0.36, 0.85),
    apply(env, it, p) {
      var _a;
      const seed = it.seed | 0, W = env.W, H2 = env.H, bg = env.sc.bg, c0 = colOf(it);
      const g1 = E.outBack(J.clamp(p / 0.15), 1.6), g2 = E.outBack(J.clamp((p - 0.17) / 0.15), 1.6), g3 = E.outBack(J.clamp((p - 0.34) / 0.15), 1.6);
      const cr = J.clamp(0.45 * g1 + 0.3 * g2 + 0.25 * g3, 0, 1.08);
      const k = 1 - 0.93 * Math.min(1, cr);
      const v = J.clamp((p - 0.5) / 0.5);
      const dir = cutBit(env, 21) ? 1 : -1;
      const spin = dir * (cr * 25 + 620 * v * v);
      const tx = dir * W * 0.42 * v, ty = -H2 * 0.8 * v + H2 * 1.75 * v * v;
      const alpha = fadeEnd(p, 0.88);
      if (isSingle(env, it)) {
        const mi = +it.mi || 0, jx = J.rs(env.cut.seed | 0, mi, 23) * it.size * 0.25, jy = J.rs(env.cut.seed | 0, mi, 24) * it.size * 0.25;
        const [nx, ny] = rot2((it.x - W / 2) * k + jx * cr, (it.y - H2 / 2) * k + jy * cr, spin * DEG);
        it.x = W / 2 + nx + tx;
        it.y = H2 / 2 + ny + ty;
        it.rot = (it.rot || 0) + spin + J.rs(seed, 22) * 150 * cr;
        it.size *= 1 - 0.5 * Math.min(1, cr);
        it._m = null;
        it.skew = (it.skew || 0) + J.rs(seed, 25) * 22 * cr;
        it.alpha = ((_a = it.alpha) != null ? _a : 1) * alpha;
        return;
      }
      const b = lBox(it), ox = b.cx, oy = b.cy, sz = it.size;
      const [tix, tiy] = dirI(it, tx, ty);
      addC(it, (i, g) => {
        const px = gCX(g, it) - ox, py = gCY(g, it) - oy;
        const jx = J.rs(seed, i, 26) * sz * 0.22, jy = J.rs(seed, i, 27) * sz * 0.22;
        const [nx, ny] = rot2(px * k + jx * cr, py * k + jy * cr, spin * DEG);
        const r1 = J.r(seed, i, 28), r2 = J.r(seed, i, 29);
        return {
          dx: nx - px + tix,
          dy: ny - py + tiy,
          rot: spin + J.rs(seed, i, 30) * 170 * cr,
          s: 1 - 0.55 * Math.min(1, cr),
          sx: 1 - 0.35 * r1 * cr,
          sy: 1 - 0.35 * r2 * cr,
          skew: J.rs(seed, i, 31) * 26 * cr,
          color: r1 < 0.45 ? mixC(c0, bg, 0.3 * Math.min(1, cr)) : void 0,
          a: alpha
        };
      });
    }
  };
  X.tearOut = {
    name: "\u7834\u308A\u6368\u3066",
    tags: ["emotional", "graphic"],
    w: 0.9,
    ae: "slice",
    outDur: (dur) => J.clamp(dur * 0.38, 0.3, 0.75),
    apply(env, it, p) {
      var _a;
      const vert = !!it.vertical, seed = it.seed | 0, sz = it.size;
      let polyA, polyB, piv;
      if (isSingle(env, it) && !vert) {
        const cs = env.cut.seed | 0, bb = dBox(it, sz * 0.3), W = env.W;
        const K = J.clamp(Math.ceil((bb.y1 - bb.y0) / (sz * 0.08)), 4, 40), jD = [];
        for (let k = 0; k <= K; k++) {
          const Y = J.lerp(bb.y0, bb.y1, k / K);
          jD.push([W / 2 + J.noise1(Y / (env.H * 0.035), cs + 5) * env.H * 0.02 + J.rs(cs, Math.round(Y / (env.H * 8e-3)), 331) * env.H * 6e-3, Y]);
        }
        const L = (pts) => pts.map(([X2, Y2]) => toL(it, X2, Y2));
        polyA = L([[-W, bb.y0], ...jD, [-W, bb.y1]]);
        polyB = L([[2 * W, bb.y0], ...jD, [2 * W, bb.y1]]);
        piv = toL(it, W / 2, env.H * 0.62);
      } else {
        const b = lBox(it, sz * 0.3);
        const mid = vert ? b.cy : b.cx, a0 = vert ? b.x0 : b.y0, a1 = vert ? b.x1 : b.y1;
        const K = J.clamp(Math.ceil((a1 - a0) / (sz * 0.08)), 6, 48), jag = [];
        for (let k = 0; k <= K; k++) {
          const t = J.lerp(a0, a1, k / K);
          const off = J.rs(seed, k, 331) * sz * 0.05 + J.noise1(k * 0.35, seed + 5) * sz * 0.16;
          jag.push(vert ? [t, mid + off] : [mid + off, t]);
        }
        polyA = vert ? [[b.x0, b.y0 - sz], ...jag, [b.x1, b.y0 - sz]] : [[b.x0 - sz, b.y0], ...jag, [b.x0 - sz, b.y1]];
        polyB = vert ? [[b.x0, b.y1 + sz], ...jag, [b.x1, b.y1 + sz]] : [[b.x1 + sz, b.y0], ...jag, [b.x1 + sz, b.y1]];
        piv = jag[jag.length - 1];
      }
      const o = E.outCubic(J.clamp(p / 0.32)), v = J.clamp((p - 0.26) / 0.74);
      const th = 7 * o + 40 * v * v;
      const fall = env.H * 1.1 * v * v, side = sz * 1.6 * v;
      const place = (h, sgn) => {
        rotateAbout(h, (vert ? -sgn : sgn) * th, piv[0], piv[1]);
        if (vert) {
          h.x += sgn * side * 0.35 - side * 0.4;
          h.y += fall + sgn * sz * 0.3 * v;
        } else {
          h.x += sgn * side;
          h.y += fall * (sgn < 0 ? 1 : 0.85);
        }
      };
      const A = { x: it.x, y: it.y, rot: it.rot || 0 }, B = { x: it.x, y: it.y, rot: it.rot || 0 };
      place(A, -1);
      place(B, 1);
      const alpha = ((_a = it.alpha) != null ? _a : 1) * fadeEnd(p, 0.82);
      it.x = A.x;
      it.y = A.y;
      it.rot = A.rot;
      it.alpha = alpha;
      it.clipFn = (ctx, e2, it3) => polyL(ctx, it3, polyA);
      chainPost(it, (env2, it2) => {
        J.drawFx(env2, copyOf(it2, { x: B.x, y: B.y, rot: B.rot, clipFn: (ctx, e3, it3) => polyL(ctx, it3, polyB) }));
      });
    }
  };
  X.scorchOut = {
    name: "\u7126\u3052\u3066\u6D88\u3048\u308B",
    tags: ["emotional", "glitch"],
    w: 0.8,
    ae: "wipe",
    outDur: (dur) => J.clamp(dur * 0.4, 0.32, 0.8),
    apply(env, it, p) {
      var _a;
      const single = isSingle(env, it), q = p;
      const vert = !!it.vertical, sz = it.size, t = env.ltb, acc = env.sc.accent, c0 = colOf(it), bg = env.sc.bg;
      const seed = single ? env.cut.seed | 0 : it.seed | 0, U = single ? Math.min(env.W, env.H) * 0.16 : sz;
      const bb = dBox(it, sz * 0.2);
      let d = vert ? [0.22, 1] : [1, 0.22];
      const dl = Math.hypot(d[0], d[1]);
      d = [d[0] / dl, d[1] / dl];
      if (cutBit(env, 31)) d = [-d[0], -d[1]];
      const tn = [-d[1], d[0]];
      const cs = [[bb.x0, bb.y0], [bb.x1, bb.y0], [bb.x0, bb.y1], [bb.x1, bb.y1]];
      const us = cs.map((c) => c[0] * d[0] + c[1] * d[1]), ws = cs.map((c) => c[0] * tn[0] + c[1] * tn[1]);
      const w0 = Math.min(...ws), w1 = Math.max(...ws);
      const ux = single ? [[0, 0], [env.W, 0], [0, env.H], [env.W, env.H]].map((c) => c[0] * d[0] + c[1] * d[1]) : us;
      const u0 = Math.min(...ux), u1 = Math.max(...ux);
      const rough = U * 0.2, band = U * 0.4;
      const S = (x) => J.lerp(u0 - rough * 1.3, u1 + rough * 1.3 + band, E.inOutSine(x));
      const s = S(q);
      const K = J.clamp(Math.ceil((w1 - w0) / (U * 0.07)), 6, 60);
      const pt = (u, w) => [d[0] * u + tn[0] * w, d[1] * u + tn[1] * w];
      const edgeU = (w) => s + rough * (0.75 * J.noise1(w / U * 2.4, seed) + 0.3 * J.noise1(w / U * 8 + t * 6, seed + 3));
      const edge = [];
      for (let k = 0; k <= K; k++) {
        const w = J.lerp(w0, w1, k / K);
        edge.push([edgeU(w), w]);
      }
      const uMax = Math.max(...us);
      if (edge.every((e) => e[0] > uMax + rough)) {
        it.alpha = 0;
        return;
      }
      const far = Math.max(u1, uMax) + U * 4;
      const path = (ctx, off0, off1) => {
        edge.forEach(([u, w], k) => {
          const P2 = pt(u + off0, w);
          k ? ctx.lineTo(P2[0], P2[1]) : ctx.moveTo(P2[0], P2[1]);
        });
        if (off1 == null) {
          const A2 = pt(far, w1), B2 = pt(far, w0);
          ctx.lineTo(A2[0], A2[1]);
          ctx.lineTo(B2[0], B2[1]);
        } else for (let k = edge.length - 1; k >= 0; k--) {
          const P2 = pt(edge[k][0] + off1, edge[k][1]);
          ctx.lineTo(P2[0], P2[1]);
        }
        ctx.closePath();
      };
      it.clipFn = (ctx) => path(ctx, 0, null);
      const heat = mixC(c0, acc, 0.85), soot = darkBg(env) ? mixC(bg, acc, 0.25) : mixC(bg, "#000000", 0.45);
      const lw = Math.max(1.5, U * 0.03), a0 = (_a = it.alpha) != null ? _a : 1, rimA = a0 * (1 - J.smooth(0.86, 1, q));
      chainPost(it, (env2, it2) => {
        J.drawFx(env2, copyOf(it2, { color: heat, gradient: null, pattern: null, alpha: a0 * 0.9, clipFn: (ctx) => path(ctx, 0, band) }));
        if (env2.pass !== "main") return;
        const ep = edge.map(([u, w]) => pt(u, w)), sp = edge.map(([u, w]) => pt(u - lw * 1.6, w));
        env2.line(sp, soot, lw * 2.2, 0.8 * rimA, false);
        env2.line(ep, acc, lw * 4, 0.22 * rimA, false);
        env2.line(ep, acc, lw, rimA, false);
        for (let j = 0; j < (single ? 6 : 22); j++) {
          const t0 = J.r(seed, j, 341) * 0.85, age = (q - t0) / 0.28;
          if (age <= 0 || age >= 1) continue;
          const w = J.lerp(w0, w1, J.r(seed, j, 342));
          const [X0, Y0] = pt(edgeU(w) - (s - S(t0)) - U * 0.05, w);
          const X1 = X0 - d[0] * U * 0.3 * age + Math.sin(age * 7 + j) * U * 0.06, Y1 = Y0 - d[1] * U * 0.3 * age - U * 0.9 * age;
          env2.circle(X1, Y1, Math.max(0.8, U * 0.028 * (1 - age)), acc, null, 0, a0 * (1 - age), false);
        }
      });
    }
  };
  X.overexposeOut = {
    name: "\u767D\u98DB\u3073",
    tags: ["emotional", "calm", "pop"],
    w: 1,
    ae: "blur",
    apply(env, it, p) {
      var _a;
      const dark = darkBg(env), c0 = colOf(it), sz = it.size, acc = env.sc.accent;
      const hot = dark ? "#ffffff" : env.sc.bg;
      const glow = dark ? mixC(acc, "#ffffff", 0.45) : acc;
      const a = E.inQuad(J.clamp(p / 0.5)), e = E.outCubic(J.clamp(p / 0.7));
      it.color = mixC(c0, hot, a);
      if (it.gradient) it.gradient = null;
      if (it.pattern) it.pattern = null;
      scaleGlyphBlur(it, 1 - J.smooth(0, 0.12, p));
      if (p > 0.08) {
        it.extrude = null;
        chainPre(it, (e2, i2) => {
          i2.extrude = null;
        });
      }
      sizeAbout(it, 1 + 0.07 * e);
      it.track = (it.track || 0) + 0.06 * e;
      const fade = 1 - E.inOutSine(J.clamp((p - 0.38) / 0.62));
      if (env.pass === "main") it.shadow = { color: J.rgba(glow, 0.95), blur: sz * (0.05 + 0.4 * e), dx: 0, dy: 0 };
      it.alpha = ((_a = it.alpha) != null ? _a : 1) * fade;
      if (env.pass === "main" && e > 0.02 && p > 0.12) {
        const ga = it.alpha;
        chainPre(it, (env2, it2) => J.drawItem(env2, copyOf(it2, { color: glow, gradient: null, pattern: null, extrude: null, stroke: 0, alpha: ga * 0.8, blur: 0, charFn: it2.charFn, shadow: { color: J.rgba(glow, 1), blur: sz * 0.9 * e, dx: 0, dy: 0 } })));
      }
      const st = Math.sin(Math.PI * J.clamp((p - 0.12) / 0.88));
      if (st > 0.02 && primary(it) && (!isSingle(env, it) || Math.round(+it.mi || 0) === 0)) {
        chainPost(it, (env2, it2, bb) => {
          const B = isSingle(env2, it2) ? { cx: env2.W / 2, cy: it2.y } : dBox(it2, 0);
          const w = (isSingle(env2, it2) ? env2.W * 0.5 : B.w) * (0.8 + 0.9 * e), cy = B.cy, cx = B.cx;
          env2.rect(cx - w / 2, cy - sz * 0.09, w, sz * 0.18, glow, 0.14 * st, false);
          env2.rect(cx - w * 0.75, cy - sz * 0.03, w * 1.5, sz * 0.06, glow, 0.35 * st, false);
          env2.rect(cx - w * 0.9, cy - sz * 9e-3, w * 1.8, sz * 0.018, glow, 0.95 * st, false);
        });
      }
    }
  };
  X.scanOut = {
    name: "\u8D70\u67FB\u7DDA\u6D88\u53BB",
    tags: ["glitch", "graphic"],
    w: 0.9,
    ae: "slice",
    apply(env, it, p) {
      var _a;
      const sz = it.size, seed = it.seed | 0, bb = dBox(it, sz * 0.3), h = bb.y1 - bb.y0;
      let pitch = Math.max(2.5, sz * 0.065);
      if (h / pitch > 64) pitch = h / 64;
      const n = Math.ceil(h / pitch), ph = h / n, single = isSingle(env, it);
      const L = single ? env.H * 0.14 : Math.max(sz * 0.9, h * 0.3);
      const ys = single ? J.lerp(env.H * 0.2, env.H * 0.8 + L, p) : J.lerp(bb.y0 - sz * 0.02, bb.y1 + L, p);
      const rects = [];
      let lastScan = -1;
      for (let j = 0; j < n; j++) {
        const y0 = bb.y0 + j * ph, yc = y0 + ph / 2, a = J.clamp((ys - yc) / L);
        if (a <= 0) break;
        lastScan = j;
        const keep = j & 1 ? 1 - J.clamp(a / 0.4) : 1 - E.inQuad(a);
        const hh = ph * keep * 0.8;
        if (hh > 0.08) rects.push([yc - hh / 2, hh]);
      }
      const yU = bb.y0 + (lastScan + 1) * ph;
      if (!rects.length && yU >= bb.y1 - 0.5) {
        it.alpha = 0;
        return;
      }
      it.clipFn = (ctx) => {
        for (const [y, hh] of rects) ctx.rect(bb.x0 - sz * 3, y, bb.w + sz * 6, hh);
        if (yU < bb.y1) ctx.rect(bb.x0 - sz * 3, yU, bb.w + sz * 6, bb.y1 - yU + 1);
      };
      const G = 7, gh = (yU - bb.y0) / G, bands = [];
      if (yU > bb.y0 + 0.5 && env.pass === "main") {
        for (let k = 0; k < G; k++) {
          const y0 = bb.y0 + k * gh, a = J.clamp((ys - (y0 + gh / 2)) / L);
          bands.push([y0, y0 + gh + 0.3, (k & 1 ? -1 : 1) * sz * (0.04 + 1.1 * a * a) * (0.5 + 0.5 * J.r(seed, k, 351))]);
        }
      }
      if (bands.length) {
        bands.push([yU, bb.y1, 0]);
        it.bands = bands;
      }
      if (ys < bb.y1 + sz * 0.1 && ys > bb.y0 - sz * 0.1) {
        const acc = env.sc.accent, lw = Math.max(1.5, sz * 0.025), a0 = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.8, 1, p));
        chainPost(it, (env2) => {
          env2.rect(bb.x0 - sz * 0.2, ys - lw * 3, bb.w + sz * 0.4, lw * 6, acc, 0.18 * a0, false);
          env2.rect(bb.x0 - sz * 0.2, ys - lw / 2, bb.w + sz * 0.4, lw, acc, a0, false);
        });
      }
    }
  };
  X.stripesOut = {
    name: "\u30B9\u30C8\u30E9\u30A4\u30D7\u6D88\u53BB",
    tags: ["graphic", "pop"],
    w: 0.9,
    ae: "wipe",
    apply(env, it, p) {
      var _a;
      const sz = it.size, bb = dBox(it, sz * 0.12), acc = env.sc.accent;
      const ang = (cutBit(env, 41) ? 64 : 116) * DEG;
      const Ld = [Math.cos(ang), Math.sin(ang)], Ad = [-Ld[1], Ld[0]];
      const cs = [[bb.x0, bb.y0], [bb.x1, bb.y0], [bb.x0, bb.y1], [bb.x1, bb.y1]];
      const as = cs.map((c) => c[0] * Ad[0] + c[1] * Ad[1]), ls = cs.map((c) => c[0] * Ld[0] + c[1] * Ld[1]);
      const a0 = Math.min(...as), a1 = Math.max(...as), l0 = Math.min(...ls), l1 = Math.max(...ls);
      let n = Math.max(2, Math.ceil((a1 - a0) / Math.max(6, sz * 0.38)));
      if (n > 48) n = 48;
      const sw = (a1 - a0) / n, rem = [], caps = [];
      const rev = cutBit(env, 42);
      for (let k = 0; k < n; k++) {
        const o = n > 1 ? k / (n - 1) : 0, st = (rev ? 1 - o : o) * 0.42;
        const q = E.inOutCubic(J.clamp((p - st) / 0.58));
        if (q >= 1) continue;
        const fromStart = (k & 1) === 1;
        const la = fromStart ? J.lerp(l0, l1, q) : l0, lb = fromStart ? l1 : J.lerp(l1, l0, q);
        rem.push([a0 + k * sw, a0 + (k + 1) * sw + 0.6, la, lb]);
        if (q > 0) caps.push([a0 + k * sw, a0 + (k + 1) * sw, fromStart ? la : lb, fromStart ? 1 : -1, 1 - J.smooth(0.8, 1, q)]);
      }
      if (!rem.length) {
        it.alpha = 0;
        return;
      }
      const pt = (a, l) => [Ad[0] * a + Ld[0] * l, Ad[1] * a + Ld[1] * l];
      it.clipFn = (ctx) => {
        for (const [aa, ab, la, lb] of rem) {
          const A2 = pt(aa, la), B2 = pt(ab, la), C2 = pt(ab, lb), D2 = pt(aa, lb);
          ctx.moveTo(A2[0], A2[1]);
          ctx.lineTo(B2[0], B2[1]);
          ctx.lineTo(C2[0], C2[1]);
          ctx.lineTo(D2[0], D2[1]);
          ctx.closePath();
        }
      };
      const th = Math.max(2, sz * 0.045), al = (_a = it.alpha) != null ? _a : 1;
      const tb = dBox(it, sz * 0.02);
      if (caps.length) chainPost(it, (env2) => {
        if (env2.pass !== "main") return;
        const ctx = env2.ctx;
        ctx.save();
        ctx.beginPath();
        ctx.rect(tb.x0, tb.y0, tb.w, tb.h);
        ctx.clip();
        for (const [aa, ab, l, sg, ca] of caps) env2.poly([pt(aa + 0.5, l - sg * th), pt(ab - 0.5, l - sg * th), pt(ab - 0.5, l), pt(aa + 0.5, l)], acc, al * ca, false);
        ctx.restore();
      });
    }
  };
  X.halftoneOut = {
    name: "\u7DB2\u70B9\u306B\u6D88\u3048\u308B",
    tags: ["graphic", "calm"],
    w: 0.9,
    ae: "shrink",
    apply(env, it, p) {
      const sz = it.size, bb = dBox(it, sz * 0.1), w = bb.x1 - bb.x0, h = bb.y1 - bb.y0, vert = !!it.vertical;
      let c = Math.max(3, sz * 0.15);
      while ((w / c + 2) * (h / (c * 0.866) + 2) > 220) c *= 1.1;
      const rows = Math.ceil(h / (c * 0.866)) + 1, cols = Math.ceil(w / c) + 2, R0 = c * 0.64;
      const rev = cutBit(env, 51), dots = [];
      for (let r = 0; r < rows; r++) for (let q = 0; q < cols; q++) {
        const cx = bb.x0 + (q + (r & 1) * 0.5 - 0.5) * c, cy = bb.y0 + r * c * 0.866;
        let pos = vert ? (cy - bb.y0) / h * 0.8 + (cx - bb.x0) / w * 0.2 : (cx - bb.x0) / w * 0.8 + (cy - bb.y0) / h * 0.2;
        if (rev) pos = 1 - pos;
        const k = J.clamp(p * 1.55 - pos * 0.55);
        const rr = R0 * (1 - E.inOutSine(k));
        if (rr > 0.3) dots.push(cx, cy, rr);
      }
      if (!dots.length) {
        it.alpha = 0;
        return;
      }
      it.clipFn = (ctx) => {
        for (let j = 0; j < dots.length; j += 3) {
          ctx.moveTo(dots[j] + dots[j + 2], dots[j + 1]);
          ctx.arc(dots[j], dots[j + 1], dots[j + 2], 0, TAU);
        }
      };
      it.color = mixC(colOf(it), env.sc.accent, 0.4 * J.smooth(0.05, 0.5, p));
    }
  };
  X.eraserOut = {
    name: "\u9ED2\u677F\u6D88\u3057",
    tags: ["editorial", "calm"],
    w: 0.9,
    ae: "wipe",
    minDur: 0.9,
    outDur: (dur) => J.clamp(dur * 0.42, 0.36, 0.85),
    apply(env, it, p) {
      var _a;
      const sz = it.size, vert = !!it.vertical, single = isSingle(env, it), W = env.W, H2 = env.H;
      const bb = single ? { x0: W * 0.03, x1: W * 0.97, y0: H2 * 0.3, y1: H2 * 0.7 } : dBox(it, sz * 0.12);
      bb.w = bb.x1 - bb.x0;
      bb.h = bb.y1 - bb.y0;
      const U = single ? H2 * 0.13 : sz;
      const span = vert ? bb.w : bb.h, len = vert ? bb.h : bb.w;
      const K = single ? 3 : J.clamp(Math.round(span / (U * 0.7)), 2, 10), lh = span / K, ew = U * 0.8;
      const leg = len + ew, pos = J.clamp(p / 0.9) * K * leg;
      const lane = Math.min(K - 1, Math.floor(pos / leg)), along = pos - lane * leg - ew / 2;
      const fwd = (j) => (j & 1) === 0;
      const laneR = (j, s0, s1) => {
        const a02 = vert ? bb.x1 - (j + 1) * lh : bb.y0 + j * lh;
        const L0 = vert ? bb.y0 : bb.x0;
        const t0 = fwd(j) ? L0 + s0 : L0 + len - s1, t1 = fwd(j) ? L0 + s1 : L0 + len - s0;
        return vert ? [a02, t0, lh, t1 - t0] : [t0, a02, t1 - t0, lh];
      };
      const cut = J.clamp(along + ew * 0.3, 0, len);
      const keep = [], gone = [];
      if (along + ew * 0.3 < len + 0.5) keep.push(laneR(lane, cut, len));
      for (let j = lane + 1; j < K; j++) keep.push(laneR(j, 0, len));
      for (let j = 0; j < lane; j++) gone.push(laneR(j, 0, len));
      if (cut > 0) gone.push(laneR(lane, 0, cut));
      if (!keep.length && p > 0.9) it.alpha = 0;
      const pad = (a, e2) => {
        for (const r of a) e2.rect(r[0], r[1], r[2] + 0.4, r[3] + 0.4);
      };
      const a0 = (_a = it.alpha) != null ? _a : 1;
      it.clipFn = (ctx) => pad(keep, ctx);
      const smear = a0 * 0.26 * (1 - J.smooth(0.35, 0.95, p));
      const eA = 1 - J.smooth(0.9, 1, p), acc = env.sc.accent, felt = mixC(acc, env.sc.bg, 0.45);
      const drawEraser = !single || Math.round(+it.mi || 0) === 0;
      chainPost(it, (env2, it2) => {
        if (smear > 0.01 && gone.length && env2.pass === "main") {
          const sm = copyOf(it2, { alpha: smear, blur: Math.min(sz * 0.1, 28), clipFn: (ctx) => pad(gone, ctx), gradient: null, pattern: null, shadow: null, extrude: null, color: mixC(colOf(it2), env2.sc.bg, 0.25) });
          if (vert) sm.sy = (sm.sy || 1) * 1.12;
          else sm.sx = (sm.sx || 1) * 1.1;
          J.drawFx(env2, sm);
        }
        if (!drawEraser || eA <= 0.01) return;
        const c = J.clamp(along, -ew / 2, len + ew / 2), r = laneR(lane, c - ew / 2, c + ew / 2);
        const x = r[0] - (vert ? lh * 0.03 : 0), y = r[1] - (vert ? 0 : lh * 0.03), w = r[2] + (vert ? lh * 0.06 : 0), h = r[3] + (vert ? 0 : lh * 0.06);
        env2.rrect(x, y, w, h, Math.min(w, h) * 0.18, acc, eA, false);
        const k = 0.3;
        if (vert) {
          const fy = fwd(lane) ? y : y + h * (1 - k);
          env2.rrect(x, fy, w, h * k, Math.min(w, h) * 0.12, felt, eA, false);
        } else {
          const fx = fwd(lane) ? x : x + w * (1 - k);
          env2.rrect(fx, y, w * k, h, Math.min(w, h) * 0.12, felt, eA, false);
        }
      });
    }
  };
  X.vacuumOut = {
    name: "\u4E00\u70B9\u306B\u5438\u308F\u308C\u308B",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "shrink",
    apply(env, it, p) {
      const vert = !!it.vertical, sz = it.size, acc = env.sc.accent, rev = cutBit(env, 61), seed = it.seed | 0;
      const single = isSingle(env, it);
      const dot = (T, fr2, drawIt) => {
        if (!drawIt) return;
        const pulse = 1 + 0.25 * Math.sin(p * 40) * (1 - p), R = sz * (0.05 + 0.09 * fr2) * pulse * (1 - J.smooth(0.9, 1, p));
        const ring = J.clamp((p - 0.86) / 0.14);
        chainPost(it, (env2) => {
          if (R > 0.3) env2.circle(T[0], T[1], R, acc, null, 0, 1, true);
          if (ring > 0 && ring < 1) env2.circle(T[0], T[1], sz * (0.15 + 0.5 * E.outCubic(ring)), null, acc, Math.max(1, sz * 0.03 * (1 - ring)), 1 - ring, false);
        });
      };
      if (single) {
        const T = [rev ? env.W * 0.08 : env.W * 0.92, env.H / 2];
        const d0 = Math.hypot(T[0] - it.x, T[1] - it.y), dm2 = env.W * 0.85;
        const q = win(p, J.clamp(d0 / dm2), 0.6);
        if (q >= 1) it.alpha = 0;
        else if (q > 0) {
          const e = E.inCubic(q), bend = (J.r(env.cut.seed | 0, +it.mi || 0, 62) - 0.5) * d0 * 0.5;
          const nx = -(T[1] - it.y) / Math.max(1, d0), ny = (T[0] - it.x) / Math.max(1, d0);
          const bx = it.x + (T[0] - it.x) * e + nx * bend * Math.sin(Math.PI * e), by = it.y + (T[1] - it.y) * e + ny * bend * Math.sin(Math.PI * e);
          it.x = bx;
          it.y = by;
          it.size *= 1 - 0.85 * e;
          it.sx = (it.sx || 1) * (1 + 1.3 * q * q);
          it.sy = (it.sy || 1) * (1 - 0.4 * q * q);
          it._m = null;
        }
        dot(T, J.clamp(p * 1.2), Math.round(+it.mi || 0) === 0);
        return;
      }
      const b = lBox(it), lay = layOf(it);
      const Tl = vert ? [b.cx, rev ? b.y0 - sz * 0.8 : b.y1 + sz * 0.8] : [rev ? b.x0 - sz * 0.8 : b.x1 + sz * 0.8, b.cy];
      let dm = 1;
      for (const g of lay) dm = Math.max(dm, Math.hypot(Tl[0] - gCX(g, it), Tl[1] - gCY(g, it)));
      const alongX = !vert;
      addC(it, (i, g) => {
        const gx = gCX(g, it), gy = gCY(g, it), vx = Tl[0] - gx, vy = Tl[1] - gy, dd = Math.hypot(vx, vy);
        const q = win(p, J.clamp((dd - sz * 0.8) / Math.max(1, dm - sz * 0.8)), 0.5);
        if (q <= 0) return null;
        if (q >= 0.94) return HIDE;
        const e = Math.min(1, E.inQuad(q / 0.94)), bend = J.rs(seed, i, 63) * dd * 0.1 * Math.sin(Math.PI * e);
        const nx = -vy / Math.max(1, dd), ny = vx / Math.max(1, dd);
        const st = Math.min(1.8, 3 * q * q);
        const along = alongX !== !!g.r90;
        return { dx: vx * e + nx * bend, dy: vy * e + ny * bend, s: 1 - 0.8 * e, sx: along ? 1 + st : 1 - 0.3 * st / 1.8, sy: along ? 1 - 0.3 * st / 1.8 : 1 + st };
      });
      let fr = 0;
      for (const g of lay) {
        const dd = Math.hypot(Tl[0] - gCX(g, it), Tl[1] - gCY(g, it));
        if (win(p, J.clamp((dd - sz * 0.8) / Math.max(1, dm - sz * 0.8)), 0.5) >= 0.94) fr++;
      }
      dot(toD(it, Tl[0], Tl[1]), fr / Math.max(1, lay.N), true);
    }
  };
  X.sandOut = {
    name: "\u7802\u306B\u306A\u3063\u3066\u98DB\u3076",
    tags: ["emotional", "calm"],
    w: 1,
    ae: "drift",
    outDur: (dur) => J.clamp(dur * 0.44, 0.36, 0.9),
    minDur: 0.8,
    apply(env, it, p) {
      const seed = it.seed | 0, sz = it.size, vert = !!it.vertical, W = env.W;
      const dir = cutBit(env, 71) ? 1 : -1;
      const [wx, wy] = dirI(it, dir, 0), [ux, uy] = dirI(it, 0, -1), sdir = Math.atan2(wy, wx) / DEG;
      const b = lBox(it), single = isSingle(env, it), og = orderOf(env, it)(0, 1);
      const posOf = (ox, oy) => {
        let pos = vert ? (oy - b.y0) / Math.max(1, b.h) : (ox - b.x0) / Math.max(1, b.w);
        if (!vert && dir > 0) pos = 1 - pos;
        if (single) pos = 0.8 * (dir > 0 ? 1 - og : og) + 0.2 * pos;
        return J.clamp(pos);
      };
      const D = sz * 3 + W * 0.18;
      const move = (x, k1, k2) => {
        const along = D * x * x, lift = sz * 0.55 * x + Math.sin(x * 5 + k1 * 6) * sz * 0.07 * x;
        return [wx * along + ux * lift, wy * along + uy * lift, k2];
      };
      if (it.gradient || it.fill === false || it.dash != null || it.pattern) {
        addC(it, (i, g) => {
          const x = (p - posOf(gCX(g, it), gCY(g, it)) * 0.5) / 0.5;
          if (x <= 0) return null;
          if (x >= 1) return HIDE;
          const [dx, dy] = move(x, J.r(seed, i, 82), 0);
          return { dx, dy, a: 1 - x, s: 1 - 0.4 * x, sx: 1 + x, blur: env.pass === "main" ? sz * 0.05 * x : 0 };
        });
        return;
      }
      it.shatter = true;
      it.pieceFns.push((ci, pj, pc, ox, oy) => {
        const t0 = posOf(ox, oy) * 0.5 + J.r(seed, ci, pj, 81) * 0.18;
        const x = (p - t0) / 0.32;
        if (x <= 0) return J.PID;
        if (x >= 1) return null;
        const [dx, dy] = move(x, J.r(seed, ci, pj, 82), 0);
        return J.PT(dx, dy, J.rs(seed, ci, pj, 83) * 140 * x, 1 - 0.55 * x, 1 + 2.4 * x, sdir, 1 - x * x);
      });
    }
  };
  X.shredOut = {
    name: "\u30B7\u30E5\u30EC\u30C3\u30C0\u30FC",
    tags: ["graphic", "pop"],
    w: 0.8,
    ae: "fall",
    minDur: 0.9,
    outDur: (dur) => J.clamp(dur * 0.44, 0.38, 0.9),
    apply(env, it, p) {
      var _a;
      const sz = it.size, seed = it.seed | 0, single = isSingle(env, it), H2 = env.H;
      const bb = dBox(it, sz * 0.06);
      const ySlot = single ? Math.max(bb.y1 + sz * 0.1, H2 * 0.66) : bb.y1 + sz * 0.12;
      const travel = ySlot - bb.y0 + sz * 0.1;
      const f = J.clamp((p - 0.08) / 0.72), D = travel * (0.2 * f * f + 0.8 * f);
      const v = J.clamp((p - 0.72) / 0.28), drop = H2 * 0.9 * v * v;
      it.y += D;
      it.clipY = [-1e5, ySlot];
      const n = J.clamp(Math.round(bb.w / (sz * 0.2)), 5, 20), sw = bb.w / n;
      const below = Math.max(0, bb.y1 + D - ySlot);
      const curl = J.clamp(below / (sz * 1.5));
      const ink = env.sc.ink || env.sc.fg, lw = Math.max(2, sz * 0.05);
      const barA = J.smooth(0, 0.1, p) * (1 - J.smooth(0.82, 0.98, p)), a0 = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.8, 0.97, p));
      chainPost(it, (env2, it2) => {
        const ctx = env2.ctx;
        if (below > 0.5 && env2.pass === "main") {
          for (let k = 0; k < n; k++) {
            const xs0 = bb.x0 + k * sw, xs1 = xs0 + sw * 0.74, xc = (xs0 + xs1) / 2;
            const ang = ((k - (n - 1) / 2) / Math.max(1, n) * 0.5 + J.rs(seed, k, 131) * 0.12) * curl + Math.sin(p * 9 + k) * 0.03 * curl;
            const lx0 = xs0 - it2.x - sz * 0.6, lx1 = xs1 - it2.x + sz * 0.6;
            const cf = merged([it2.charFn || (() => null), (i, g) => {
              const gx = gCX(g, it2);
              return gx + g.w * (it2.sx || 1) / 2 < lx0 || gx - g.w * (it2.sx || 1) / 2 > lx1 ? HIDE : null;
            }]);
            ctx.save();
            ctx.translate(xc, ySlot);
            ctx.rotate(ang);
            ctx.translate(-xc, -ySlot + drop * (0.7 + 0.6 * J.r(seed, k, 132)));
            ctx.beginPath();
            ctx.rect(xs0, ySlot, xs1 - xs0, H2 * 3);
            ctx.clip();
            J.drawItem(env2, copyOf(it2, { charFn: cf, alpha: a0 }));
            ctx.restore();
          }
        }
        if (barA > 0.01) {
          const w = (bb.w + sz * 0.5) * E.outCubic(J.smooth(0, 0.12, p));
          env2.rect(bb.cx - w / 2, ySlot - lw / 2, w, lw, ink, barA, false);
        }
      });
    }
  };
  X.dominoOut = {
    name: "\u30C9\u30DF\u30CE\u5012\u3057",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "fall",
    minDur: 0.8,
    outDur: (dur, n) => J.clamp(dur * 0.42, 0.36, 0.85),
    apply(env, it, p) {
      const ord0 = orderOf(env, it), rev = cutBit(env, 91), sgn = rev ? -1 : 1, sx0 = it.sx || 1, sy0 = it.sy || 1, sz = it.size;
      const ord = (i, n) => rev ? 1 - ord0(i, n) : ord0(i, n);
      const fade = 1 - J.smooth(0.74, 0.98, p), sink = sz * 0.9 * E.inQuad(J.clamp((p - 0.7) / 0.3));
      const [dnx, dny] = downI(it);
      addC(it, (i, g, n) => {
        const q = win(p, ord(i, n), 0.55);
        if (q <= 0) return null;
        const tf = 0.62;
        let th;
        if (q < tf) th = 90 * E.inCubic(q / tf);
        else {
          const u = (q - tf) / (1 - tf);
          th = 90 - 10 * Math.sin(Math.PI * Math.min(1, u * 1.6)) * (1 - u);
        }
        const px = sgn * g.w * sx0 / 2, py = sz * sy0 * 0.5;
        const [rx, ry] = rot2(-px, -py, sgn * th * DEG);
        return { dx: px + rx + dnx * sink, dy: py + ry + dny * sink, rot: sgn * th, a: fade };
      });
    }
  };
  X.hingeOut = {
    name: "\u7247\u7559\u3081\u304C\u5916\u308C\u308B",
    tags: ["pop", "editorial"],
    w: 0.9,
    ae: "fall",
    minDur: 0.9,
    outDur: (dur) => J.clamp(dur * 0.46, 0.42, 0.95),
    apply(env, it, p) {
      var _a;
      const single = isSingle(env, it);
      const q = single ? win(p, J.r(env.cut.seed | 0, +it.mi || 0, 101), 0.3) : p;
      if (q <= 0) return;
      const sz = it.size, b = lBox(it, sz * 0.02), vert = !!it.vertical, left = !cutBit(env, 102), H2 = env.H, acc = env.sc.accent;
      const sg = left ? 1 : -1;
      const ax = (left ? b.x0 : b.x1) + sg * Math.min(sz * 0.14, b.w * 0.2), ay = b.y0 + Math.min(sz * 0.12, b.h * 0.2);
      const reach = Math.max(1, b.w - sz * 0.14);
      const hang = vert ? 24 : Math.min(58, Math.max(14, Math.asin(J.clamp(H2 * 0.3 / reach, 0, 1)) / DEG));
      const t1 = 0.64, u = J.clamp(q / t1), v = J.clamp((q - t1) / (1 - t1));
      const th = hang * (1 - Math.exp(-u * 4.5) * Math.cos(u * 12)) + 30 * v * v;
      const pin0 = toD(it, ax, ay), bx = left ? b.x1 - sz * 0.14 : b.x0 + sz * 0.14, pin1 = toD(it, bx, ay);
      rotateAbout(it, sg * th, ax, ay);
      it.y += H2 * 1.25 * v * v;
      it.x += sg * sz * 0.4 * v;
      it.alpha = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.9, 1, q));
      const r = Math.max(2, sz * 0.045), pa = 1 - J.smooth(0.05, 0.25, v), fl = J.clamp(q / 0.16);
      chainPost(it, (env2) => {
        if (pa > 0.01) env2.circle(pin0[0], pin0[1], r, acc, null, 0, pa, false);
        if (fl < 1) env2.circle(pin1[0] + sg * sz * 0.5 * fl, pin1[1] - sz * 0.6 * fl + sz * 1.6 * fl * fl, r, acc, null, 0, 1 - fl, false);
      });
    }
  };
  X.rocketOff = {
    name: "\u6253\u3061\u4E0A\u3052",
    tags: ["pop"],
    w: 0.9,
    ae: "drift",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.44, 0.36, 0.9),
    apply(env, it, p) {
      const seed = it.seed | 0, sz = it.size, acc = env.sc.accent, sub = env.sc.sub, sy0 = it.sy || 1, lay = layOf(it);
      const [dnx, dny] = downI(it), bb = dBox(it, 0), D = bb.y1 + sz * 1.6, step = env.step;
      const Q = (i) => win(p, J.r(seed, i, 111) * 0.85, 0.55);
      const lift = (u) => D * Math.pow(u, 2.3);
      addC(it, (i, g) => {
        const q = Q(i);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const hh = g.h * sy0 / 2, r90 = !!g.r90;
        if (q < 0.3) {
          const u2 = q / 0.3, sq = Math.sin(u2 * Math.PI / 2), k = 1 - 0.22 * sq, w = 1 + 0.12 * sq;
          const sh = J.rs(seed, i, step, 112) * sz * 0.02 * u2;
          return { dx: dnx * (1 - k) * hh + sh, dy: dny * (1 - k) * hh, sx: r90 ? k : w, sy: r90 ? w : k };
        }
        const u = (q - 0.3) / 0.7, L = lift(u), st = 1 + Math.min(0.8, 3 * u * u);
        return { dx: -dnx * L, dy: -dny * L, sx: r90 ? st : 1 - 0.12 * (st - 1), sy: r90 ? 1 - 0.12 * (st - 1) : st };
      });
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main") return;
        inItem(env2, it2, () => {
          for (const g of lay) {
            if (isSp(g.ch)) continue;
            const q = Q(g.i);
            if (q < 0.3 || q >= 1) continue;
            const u = (q - 0.3) / 0.7, L = lift(u), hh = g.h * sy0 / 2;
            const gx = gCX(g, it2), gy = gCY(g, it2);
            const bx = gx - dnx * L + dnx * hh, by = gy - dny * L + dny * hh;
            const tl = Math.min(L, sz * 2.6), a = 1 - u * 0.8;
            env2.line([[bx, by], [bx + dnx * tl, by + dny * tl]], acc, Math.max(1.5, sz * 0.2 * (1 - u * 0.5)), 0.28 * a, false);
            env2.line([[bx, by], [bx + dnx * tl * 0.75, by + dny * tl * 0.75]], acc, Math.max(1, sz * 0.08), 0.95 * a, false);
            env2.poly([[bx - dny * sz * 0.1, by + dnx * sz * 0.1], [bx + dny * sz * 0.1, by - dnx * sz * 0.1], [bx + dnx * sz * 0.45, by + dny * sz * 0.45]], acc, a, false);
            const ox = gx + dnx * hh, oy = gy + dny * hh;
            for (let k = 0; k < 3; k++) env2.circle(ox + (k - 1) * sz * 0.25 * (0.4 + u), oy - sz * 0.05, sz * (0.08 + 0.22 * u) * (1 - Math.abs(k - 1) * 0.3), sub, null, 0, 0.35 * (1 - u), false);
          }
        });
      });
    }
  };
  function hops(q) {
    const R = 0.64, NH = 4;
    let S = 0;
    for (let j = 0; j < NH; j++) S += Math.pow(R, j);
    let t = q * S;
    for (let j = 0; j < NH; j++) {
      const d = Math.pow(R, j);
      if (t <= d || j === NH - 1) {
        const u = J.clamp(t / d), hk = d * d;
        return { h: hk * 4 * u * (1 - u), land: Math.max(0, 1 - Math.min(u, 1 - u) / 0.09) * Math.sqrt(hk) };
      }
      t -= d;
    }
    return { h: 0, land: 0 };
  }
  X.bounceOff = {
    name: "\u5F3E\u3093\u3067\u53BB\u308B",
    tags: ["pop"],
    w: 0.9,
    ae: "scatter",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.45, 0.38, 0.9),
    apply(env, it, p) {
      const dir = cutBit(env, 121) ? 1 : -1, sz = it.size, ord = orderOf(env, it), sy0 = it.sy || 1;
      const [rx, ry] = rightI(it), [dnx, dny] = downI(it);
      addC(it, (i, g, n) => {
        const o = dir > 0 ? 1 - ord(i, n) : ord(i, n);
        const q = win(p, o, 0.42);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const [gxD] = toD(it, gCX(g, it), gCY(g, it));
        const dist = dir > 0 ? env.W - gxD + sz * 1.2 : gxD + sz * 1.2;
        const along = dist * Math.pow(q, 1.25), hp = hops(q), h = hp.h * sz * 1.4;
        const sq = 0.3 * hp.land, hh = g.h * sy0 / 2, r90 = !!g.r90;
        const k = 1 - sq, w = 1 + sq * 0.7;
        return {
          dx: rx * dir * along - dnx * h + dnx * (1 - k) * hh,
          dy: ry * dir * along - dny * h + dny * (1 - k) * hh,
          rot: dir * 14 * Math.sin(Math.PI * Math.min(1, hp.h * 4)),
          sx: r90 ? k : w,
          sy: r90 ? w : k
        };
      });
    }
  };
  X.balloonOff = {
    name: "\u98A8\u8239\u3067\u98DB\u3076",
    tags: ["emotional", "calm", "pop"],
    w: 0.9,
    ae: "drift",
    minDur: 0.9,
    outDur: (dur) => J.clamp(dur * 0.48, 0.42, 1),
    apply(env, it, p) {
      const seed = it.seed | 0, sz = it.size, sub = env.sc.sub, sy0 = it.sy || 1, lay = layOf(it);
      const bb = dBox(it, 0), D = bb.y1 + sz * 1.8;
      const Q = (i) => win(p, J.r(seed, i, 141), 0.45);
      const st = (i, q) => {
        const L = D * Math.pow(q, 1.7), ph = J.r(seed, i, 142) * TAU, w = 7 + 3 * J.r(seed, i, 143), amp = Math.min(1, q * 3);
        return { L, sw: Math.sin(q * w + ph) * sz * 0.22 * amp, rot: -Math.cos(q * w + ph) * 11 * amp, inf: 1 + 0.07 * Math.sin(Math.PI * Math.min(1, q * 2.5)) };
      };
      addC(it, (i, g) => {
        const q = Q(i);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const s = st(i, q), [dx, dy] = dirI(it, s.sw, -s.L);
        return { dx, dy, rot: s.rot, s: s.inf };
      });
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main") return;
        inItem(env2, it2, () => {
          const [dnx, dny] = downI(it2), [rx, ry] = rightI(it2);
          for (const g of lay) {
            if (isSp(g.ch)) continue;
            const q = Q(g.i);
            if (q <= 0 || q >= 1) continue;
            const s = st(g.i, q), [dx, dy] = dirI(it2, s.sw, -s.L), r = s.rot * DEG, hh = g.h * sy0 / 2 * s.inf;
            const bx = gCX(g, it2) + dx - Math.sin(r) * hh, by = gCY(g, it2) + dy + Math.cos(r) * hh;
            const pts = [];
            for (let k = 0; k <= 6; k++) {
              const u = k / 6, lag = Math.sin(q * 8 + J.r(seed, g.i, 142) * TAU - u * 1.6) * sz * 0.1 * u;
              pts.push([bx + dnx * sz * 0.95 * u + rx * lag, by + dny * sz * 0.95 * u + ry * lag]);
            }
            env2.line(pts, sub, Math.max(1, sz * 0.014), 0.85 * Math.min(1, q * 8), false);
          }
        });
      });
    }
  };
  X.deflateOut = {
    name: "\u3057\u307C\u3093\u3067\u98DB\u3076",
    tags: ["pop"],
    w: 0.8,
    ae: "scatter",
    outDur: (dur) => J.clamp(dur * 0.42, 0.36, 0.85),
    apply(env, it, p) {
      const seed = it.seed | 0, sz = it.size;
      addC(it, (i) => {
        const q = win(p, J.r(seed, i, 151) * 0.8, 0.4);
        if (q <= 0) return null;
        if (q >= 0.97) return HIDE;
        if (q < 0.16) {
          const u2 = q / 0.16;
          return { s: 1 + 0.16 * E.outCubic(u2), sx: 1 + 0.05 * Math.sin(u2 * 25), sy: 1 - 0.05 * Math.sin(u2 * 25), rot: J.rs(seed, i, 152) * 4 * u2 };
        }
        const u = (q - 0.16) / 0.81, th0 = J.r(seed, i, 153) * TAU;
        let x = 0, y = 0, th = th0;
        const NS = 14, uu = u;
        for (let k = 0; k < NS; k++) {
          const t = (k + 0.5) / NS * uu;
          th = th0 + 7 * J.noise1(t * 5, seed + i * 7) + 3 * t;
          const v = sz * 7.5 * (0.5 + t) * (uu / NS);
          x += Math.cos(th) * v;
          y += Math.sin(th) * v;
        }
        const wob = Math.sin(u * 70) * 0.14 * (1 - u);
        return { dx: x, dy: y, rot: (th - th0) / DEG * 0.5, s: 1.16 * (1 - Math.pow(u, 1.3)), sx: 1 + wob, sy: 1 - wob };
      });
    }
  };
  X.hazeOut = {
    name: "\u967D\u708E\u306B\u6D88\u3048\u308B",
    tags: ["emotional", "calm"],
    w: 0.9,
    ae: "blur",
    apply(env, it, p) {
      var _a;
      const sz = it.size, t = env.ltb, vert = !!it.vertical, e = E.inQuad(p);
      const b0 = box(it);
      scaleAbout(it, 1 - 0.05 * e, 1 + 0.3 * e, b0.cx - it.x, b0.y1 - it.y);
      it.y -= sz * 0.3 * e;
      it.color = mixC(colOf(it), env.sc.accent, 0.3 * J.smooth(0.1, 0.7, p));
      it.alpha = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.3, 0.94, p));
      const bb = dBox(it, sz * 0.3), A = sz * (0.02 + 0.26 * E.inOutSine(p));
      if (A < 0.4 || env.pass !== "main") return;
      if (!vert) {
        const n = J.clamp(Math.ceil(bb.h / (sz * 0.09)), 6, isSingle(env, it) ? 7 : 14), bh = bb.h / n, out = [];
        for (let j = 0; j < n; j++) {
          const y = bb.y0 + j * bh, c = (y + bh / 2) / sz;
          out.push([y, y + bh + 0.4, A * Math.sin(c * 6.5 - t * 12) * (0.6 + 0.4 * Math.sin(c * 2.3 + t * 5))]);
        }
        it.bands = out;
      } else {
        const n = J.clamp(Math.ceil(bb.w / (sz * 0.08)), 6, 16), bw = bb.w / n, out = [];
        for (let j = 0; j < n; j++) {
          const x = bb.x0 + j * bw, c = (x + bw / 2) / sz;
          out.push([x, x + bw + 0.4, A * Math.sin(c * 6.5 - t * 12) * (0.6 + 0.4 * Math.sin(c * 2.3 + t * 5))]);
        }
        it.vbands = out;
      }
    }
  };
  X.glassBreak = {
    name: "\u30AC\u30E9\u30B9\u5272\u308C",
    tags: ["graphic", "pop", "glitch"],
    w: 0.8,
    ae: "explode",
    outDur: (dur) => J.clamp(dur * 0.42, 0.36, 0.85),
    apply(env, it, p) {
      var _a;
      const single = isSingle(env, it);
      const q = single ? win(p, orderOf(env, it)(0, 1), 0.4) : p;
      if (q <= 0) return;
      const sz = it.size, seed = it.seed | 0, H2 = env.H, b = lBox(it, sz * 0.12);
      const ix = b.cx + J.rs(seed, 151) * b.w * 0.2, iy = b.cy + J.rs(seed, 152) * b.h * 0.12;
      const K = single ? 4 : J.clamp(Math.round(5 + b.w / Math.max(1, b.h) * 0.8), 6, 10);
      const B = [], R = [];
      for (let k = 0; k < K; k++) {
        const a = (k + 0.35 * J.rs(seed, k, 153)) / K * TAU, c = Math.cos(a), s = Math.sin(a);
        const tx = c > 0 ? (b.x1 - ix) / c : c < 0 ? (b.x0 - ix) / c : 1e9, ty = s > 0 ? (b.y1 - iy) / s : s < 0 ? (b.y0 - iy) / s : 1e9;
        const tt = Math.min(tx, ty), f = 0.3 + 0.25 * J.r(seed, k, 154);
        B.push([ix + c * tt, iy + s * tt]);
        R.push([ix + c * tt * f, iy + s * tt * f]);
      }
      const shards = [];
      for (let k = 0; k < K; k++) {
        const k2 = (k + 1) % K;
        shards.push([[ix, iy], R[k], R[k2]]);
        shards.push([R[k], B[k], B[k2], R[k2]]);
      }
      for (let k = 0; k < K; k++) {
        const k2 = (k + 1) % K, poly = shards[k * 2 + 1];
        const a1 = Math.atan2(B[k][1] - iy, B[k][0] - ix), a2 = Math.atan2(B[k2][1] - iy, B[k2][0] - ix);
        const corners = [[b.x1, b.y1], [b.x0, b.y1], [b.x0, b.y0], [b.x1, b.y0]].filter(([cx, cy]) => {
          let da = Math.atan2(cy - iy, cx - ix) - a1, dd = a2 - a1;
          da = (da % TAU + TAU) % TAU;
          dd = (dd % TAU + TAU) % TAU;
          return da > 0 && da < dd;
        }).sort((m, n2) => ((Math.atan2(m[1] - iy, m[0] - ix) - a1) % TAU + TAU) % TAU - ((Math.atan2(n2[1] - iy, n2[0] - ix) - a1) % TAU + TAU) % TAU);
        if (corners.length) shards[k * 2 + 1] = [R[k], B[k], ...corners, B[k2], R[k2]];
      }
      const crack = J.clamp(q / 0.2), v = J.clamp((q - 0.18) / 0.82);
      const a0 = (_a = it.alpha) != null ? _a : 1, bg = env.sc.bg, lw = Math.max(1.5, sz * 0.032);
      if (v <= 0) {
        chainPost(it, (env2, it2) => {
          if (env2.pass !== "main") return;
          inItem(env2, it2, () => {
            for (let k = 0; k < K; k++) env2.polyPartial([[ix, iy], B[k]], crack, bg, lw, 1, false);
            if (crack > 0.5) for (let k = 0; k < K; k++) env2.polyPartial([R[k], R[(k + 1) % K]], (crack - 0.5) * 2, bg, lw * 0.8, 1, false);
            env2.circle(ix, iy, sz * (0.06 + 0.2 * crack), env2.sc.accent, null, 0, 0.5 * (1 - crack), false);
          });
        });
        return;
      }
      it.alpha = 0;
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main") return;
        shards.forEach((poly, j) => {
          let cx = 0, cy = 0;
          for (const [x, y] of poly) {
            cx += x;
            cy += y;
          }
          cx /= poly.length;
          cy /= poly.length;
          const d = J.r(seed, j, 155) * 0.3 + (j & 1 ? 0 : 0.12), u = J.clamp((v - d) / (1 - d));
          const h = { x: it2.x, y: it2.y, rot: it2.rot || 0 };
          if (u > 0) {
            rotateAbout(h, J.rs(seed, j, 156) * 120 * u * u, cx, cy);
            const ox = (cx - ix) / Math.max(1, b.w) * sz * 1.2 * u, oy = (cy - iy) / Math.max(1, b.h) * sz * 0.5 * u;
            const [Ox, Oy] = rot2(ox, oy, (it2.rot || 0) * DEG);
            h.x += Ox;
            h.y += Oy + H2 * 1.1 * u * u;
          }
          let x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          for (const [x, y] of poly) {
            x0 = Math.min(x0, x);
            x1 = Math.max(x1, x);
            y0 = Math.min(y0, y);
            y1 = Math.max(y1, y);
          }
          const sx2 = it2.sx || 1, sy2 = it2.sy || 1, vt = !!it2.vertical;
          const cf = merged([it2.charFn || (() => null), (i, g) => {
            const gx = gCX(g, it2), gy = gCY(g, it2), hw = (vt ? it2.size : g.w) * sx2 * 0.6, hh = (vt ? g.h : it2.size) * sy2 * 0.6;
            return gx + hw < x0 || gx - hw > x1 || gy + hh < y0 || gy - hh > y1 ? HIDE : null;
          }]);
          J.drawFx(env2, copyOf(it2, { x: h.x, y: h.y, rot: h.rot, charFn: cf, alpha: a0 * (1 - J.smooth(0.75, 1, u)), clipFn: (ctx, e3, it3) => polyL(ctx, it3, poly) }));
        });
        if (env2.pass === "main" && v < 0.25) inItem(env2, it2, () => {
          for (let k = 0; k < K; k++) env2.line([[ix, iy], B[k]], bg, lw * (1 - v * 4), 1, false);
        });
      });
    }
  };
  X.zipOut = {
    name: "\u30B8\u30C3\u30D1\u30FC",
    tags: ["graphic", "pop"],
    w: 0.8,
    ae: "wipe",
    apply(env, it, p) {
      var _a;
      const vert = !!it.vertical, sz = it.size, acc = env.sc.accent, rev = cutBit(env, 161), sub = env.sc.sub;
      const single = isSingle(env, it), N = cutN(env);
      const pos = J.lerp(-0.12, 1.12, E.inOutSine(J.clamp(p / 0.8)));
      const lines = lineExt(it), sx0 = it.sx || 1, sy0 = it.sy || 1;
      const band = single ? 1.2 / Math.max(1, N - 1) : 0.22;
      const og = orderOf(env, it)(0, 1);
      const along = (L, g) => {
        const a0 = vert ? L.y0 : L.x0, a1 = vert ? L.y1 : L.x1, v = vert ? gCY(g, it) : gCX(g, it);
        const o = (v - a0) / Math.max(1, a1 - a0);
        return rev ? 1 - o : o;
      };
      const byLi = {};
      for (const L of lines) byLi[L.li] = L;
      addC(it, (i, g) => {
        const L = byLi[g.li];
        if (!L) return null;
        const o = single ? rev ? 1 - og : og : along(L, g);
        const c = E.inOutSine(J.clamp((pos - o) / band + 0.15));
        if (c <= 0) return null;
        if (c >= 0.99) return HIDE;
        const k = 1 - c, acrossX = vert !== !!g.r90;
        return acrossX ? { sx: k } : { sy: k };
      });
      const fa = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.76, 0.96, p));
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main" || fa <= 0.01) return;
        inItem(env2, it2, () => {
          for (const L of lines) {
            const a0 = vert ? L.y0 : L.x0, a1 = vert ? L.y1 : L.x1, mid = vert ? (L.x0 + L.x1) / 2 : (L.y0 + L.y1) / 2, len = a1 - a0;
            let sp = single ? a0 + len * ((pos - (rev ? 1 - og : og)) * Math.max(1, N - 1) + 0.5) : a0 + len * pos;
            if (rev) sp = a0 + a1 - sp;
            const z0 = rev ? Math.max(sp, a0 - sz * 0.1) : a0 - sz * 0.1, z1 = rev ? a1 + sz * 0.1 : Math.min(sp, a1 + sz * 0.1);
            const tp = Math.max(3, sz * 0.09), th = sz * 0.05;
            for (let a = z0, k = 0; a < z1; a += tp, k++) {
              const o = (k & 1 ? 1 : -1) * th * 0.5;
              if (vert) env2.rect(mid - th * 0.5 + o, a, th, tp * 0.6, sub, fa, false);
              else env2.rect(a, mid - th * 0.5 + o, tp * 0.6, th, sub, fa, false);
            }
            if (sp > a0 - sz * 0.5 && sp < a1 + sz * 0.5) {
              const w = sz * 0.3, h = sz * 0.46;
              if (vert) {
                env2.rrect(mid - h / 2, sp - w / 2, h, w, w * 0.3, acc, fa, false);
                env2.rrect(mid + h / 2 - sz * 0.02, sp - w * 0.2, sz * 0.36, w * 0.4, w * 0.2, acc, fa, false);
              } else {
                env2.rrect(sp - w / 2, mid - h / 2, w, h, w * 0.3, acc, fa, false);
                env2.rrect(sp - w * 0.2, mid + h / 2 - sz * 0.02, w * 0.4, sz * 0.36, w * 0.2, acc, fa, false);
              }
            }
          }
        });
      });
    }
  };
  X.clapShut = {
    name: "\u4E2D\u592E\u3067\u9589\u3058\u308B",
    tags: ["graphic", "pop"],
    w: 0.9,
    ae: "wipe",
    apply(env, it, p) {
      const vert = !!it.vertical, sz = it.size, acc = env.sc.accent, W = env.W;
      const e = E.inCubic(J.clamp(p / 0.62)), hit = J.clamp((p - 0.6) / 0.4);
      if (isSingle(env, it)) {
        const left = it.x < W / 2, D2 = W * 0.47 * e;
        it.x += left ? D2 : -D2;
        it.clipFn = (ctx) => left ? ctx.rect(-W, -env.H, W * 1.5, env.H * 3) : ctx.rect(W / 2, -env.H, W * 1.5, env.H * 3);
        if (e >= 0.999) it.alpha = 0;
        if (hit > 0 && hit < 1 && Math.round(+it.mi || 0) === 0) {
          const h = env.H * 0.3 * (1 + 0.6 * E.outCubic(hit)), lw = sz * 0.12 * (1 - hit);
          chainPost(it, (env2) => env2.rect(W / 2 - lw / 2, env.H / 2 - h / 2, lw, h, acc, 1 - hit, false));
        }
        return;
      }
      const b = lBox(it, sz * 0.04);
      const seam = vert ? b.cy : b.cx, half = (vert ? b.h : b.w) / 2 + sz * 0.05, D = half * e;
      const side = (sgn) => (i, g) => {
        const gx = gCX(g, it), gy = gCY(g, it), c = vert ? gy : gx, hw = (vert ? g.h * (it.sy || 1) : g.w * (it.sx || 1)) / 2;
        if (sgn < 0 ? c - hw >= seam : c + hw <= seam) return HIDE;
        const k = 1 - 0.12 * e;
        const nx = vert ? gx : gx - sgn * D, ny = vert ? gy - sgn * D : gy;
        const r = vert ? gclip(g, it, nx, ny, 1, k, null, sgn < 0 ? [-1e5, seam] : [seam, 1e5]) : gclip(g, it, nx, ny, k, 1, sgn < 0 ? [-1e5, seam] : [seam, 1e5], null);
        return Object.assign({ dx: nx - gx, dy: ny - gy }, vert ? g.r90 ? { sx: k } : { sy: k } : g.r90 ? { sy: k } : { sx: k }, r);
      };
      withCopies(it, side(-1), [side(1)]);
      const sa = J.smooth(0, 0.15, p) * (1 - J.smooth(0.6, 0.7, p));
      if (sa > 0.01) chainPost(it, (env2, it2) => {
        if (env2.pass !== "main") return;
        const lw = Math.max(1.5, sz * 0.025), ext = (vert ? b.w : b.h) * 1.1 * E.outCubic(J.smooth(0, 0.15, p)), mc = vert ? b.cx : b.cy;
        inItem(env2, it2, () => {
          if (vert) env2.rect(mc - ext / 2, seam - lw / 2, ext, lw, acc, sa, false);
          else env2.rect(seam - lw / 2, mc - ext / 2, lw, ext, acc, sa, false);
        });
      });
      if (hit > 0 && hit < 1) {
        const lw = sz * 0.14 * (1 - E.outCubic(hit)), ext = (vert ? b.w : b.h) * (1 + 0.7 * E.outCubic(hit)), fa = 1 - hit;
        chainPost(it, (env2, it2) => {
          if (env2.pass !== "main") return;
          inItem(env2, it2, () => {
            const mc = vert ? b.cx : b.cy;
            if (vert) env2.rect(mc - ext / 2, seam - lw / 2, ext, lw, acc, fa, false);
            else env2.rect(seam - lw / 2, mc - ext / 2, lw, ext, acc, fa, false);
            for (let k = 0; k < 4; k++) {
              const a = (k + 0.5) / 4 * TAU + 0.3, r0 = sz * (0.2 + 0.6 * hit), r1 = r0 + sz * 0.25 * (1 - hit);
              const cx = vert ? mc : seam, cy = vert ? seam : mc;
              env2.line([[cx + Math.cos(a) * r0, cy + Math.sin(a) * r0], [cx + Math.cos(a) * r1, cy + Math.sin(a) * r1]], acc, Math.max(1, sz * 0.03), fa, false);
            }
          });
        });
      }
    }
  };
  X.lampOff = {
    name: "\u6D88\u706F",
    tags: ["glitch", "emotional"],
    w: 0.8,
    ae: "glitch",
    apply(env, it, p) {
      const seed = it.seed | 0, step = env.step, c0 = colOf(it), bg = env.sc.bg, dark = darkBg(env), sz = it.size;
      const dimC = mixC(c0, bg, 0.78), hot = dark ? mixC(c0, "#ffffff", 0.5) : c0;
      const N = layOf(it).N;
      const T = (i) => J.r(seed, i, 171) * 0.46;
      let lit = 0;
      for (let i = 0; i < N; i++) {
        const q = (p - T(i)) / 0.36;
        lit += q <= 0 ? 1 : q < 0.5 ? 0.5 : 0;
      }
      addC(it, (i) => {
        const q = (p - T(i)) / 0.36;
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        if (q > 0.55) return { color: dimC, a: 1 - (q - 0.55) / 0.45 };
        const off = J.r(seed, i, step, 172) < 0.3 + 0.7 * (q / 0.55);
        return off ? { color: dimC } : { color: hot };
      });
      if (dark && env.pass === "main" && !it.shadow) it.shadow = { color: J.rgba(mixC(c0, env.sc.accent, 0.4), 0.85), blur: sz * 0.22 * lit / Math.max(1, N), dx: 0, dy: 0 };
    }
  };
  X.slotOut = {
    name: "\u30B9\u30ED\u30C3\u30C8\u56DE\u8EE2",
    tags: ["glitch", "pop"],
    w: 0.8,
    ae: "glitch",
    outDur: (dur) => J.clamp(dur * 0.38, 0.3, 0.7),
    apply(env, it, p) {
      const seed = it.seed | 0, ord = orderOf(env, it), sy0 = it.sy || 1, acc = env.sc.accent;
      const M = (i) => 3 + J.h(seed, i, 182) % 4;
      const chOf = (i, m) => J.pool("reel")[J.h(seed, i, m, 181) % J.pool("reel").length];
      const state = (i, n) => {
        const q = win(p, ord(i, n), 0.35);
        if (q <= 0) return null;
        const ph = (M(i) + 1) * E.inOutCubic(q), m = Math.floor(ph);
        return { q, ph, m, fr: ph - m, sp: Math.min(0.6, 6 * Math.sin(Math.PI * q) * 0.12) };
      };
      const reel = (next) => (i, g, n) => {
        const s = state(i, n);
        if (!s) return next ? HIDE : null;
        const m = s.m + (next ? 1 : 0);
        if (m > M(i)) return HIDE;
        const pitch = g.h * sy0 * 1.12, dy = next ? (1 - s.fr) * pitch : -s.fr * pitch;
        const gx = gCX(g, it), gy = gCY(g, it), hh = g.h * sy0 * 0.55, ky = 1 + s.sp;
        const r = gclip(g, it, gx, gy + dy, 1, ky, null, [gy - hh, gy + hh]);
        const o = Object.assign({ dy }, r, g.r90 ? { sx: ky } : { sy: ky });
        if (m > 0) {
          o.ch = chOf(i, m);
          if (J.r(seed, i, m, 183) < 0.3) o.color = acc;
        }
        return o;
      };
      withCopies(it, reel(false), [reel(true)]);
    }
  };
  X.clockOut = {
    name: "\u6642\u8A08\u30EF\u30A4\u30D7",
    tags: ["graphic", "editorial"],
    w: 0.9,
    ae: "wipe",
    apply(env, it, p) {
      var _a;
      const sz = it.size, acc = env.sc.accent, cw = !cutBit(env, 191), sg = cw ? 1 : -1, a0 = -Math.PI / 2;
      const ord = orderOf(env, it), lay = layOf(it), vert = !!it.vertical, sx0 = it.sx || 1, sy0 = it.sy || 1;
      const cells = [];
      for (const g of lay) {
        if (isSp(g.ch)) continue;
        const q = win(p, ord(g.i, lay.N), 0.55);
        if (q >= 1) continue;
        const e = E.inOutSine(q);
        cells.push({ cx: gCX(g, it), cy: gCY(g, it), hw: (vert ? sz * sx0 : g.w * sx0) * 0.56, hh: (vert ? g.h * sy0 : sz * sy0) * 0.56, e });
      }
      if (!cells.length) {
        it.alpha = 0;
        return;
      }
      const P2 = (c, a) => {
        const ca = Math.cos(a), sa = Math.sin(a), t = Math.min(Math.abs(ca) > 1e-6 ? c.hw / Math.abs(ca) : 1e9, Math.abs(sa) > 1e-6 ? c.hh / Math.abs(sa) : 1e9);
        return [c.cx + ca * t, c.cy + sa * t];
      };
      const region = (c) => {
        const ah = a0 + sg * c.e * TAU, span = (1 - c.e) * TAU, pts = [[c.cx, c.cy], P2(c, ah)];
        const cor = [[c.hw, -c.hh], [c.hw, c.hh], [-c.hw, c.hh], [-c.hw, -c.hh]].map(([x, y]) => {
          const rel = ((Math.atan2(y, x) - ah) * sg % TAU + TAU) % TAU;
          return [rel, c.cx + x, c.cy + y];
        }).filter((k) => k[0] > 1e-6 && k[0] < span).sort((m, n2) => m[0] - n2[0]);
        for (const k of cor) pts.push([k[1], k[2]]);
        pts.push([c.cx, c.cy - c.hh]);
        return pts;
      };
      it.clipFn = (ctx, e2, it3) => {
        for (const c of cells) polyL(ctx, it3, c.e <= 1e-3 ? [[c.cx - c.hw, c.cy - c.hh], [c.cx + c.hw, c.cy - c.hh], [c.cx + c.hw, c.cy + c.hh], [c.cx - c.hw, c.cy + c.hh]] : region(c));
      };
      const fa = (_a = it.alpha) != null ? _a : 1;
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main") return;
        inItem(env2, it2, () => {
          for (const c of cells) {
            if (c.e <= 1e-3) continue;
            const ah = a0 + sg * c.e * TAU, glow = [[c.cx, c.cy]], a = fa * Math.min(1, (1 - c.e) * 6);
            for (let k = 0; k <= 5; k++) glow.push(P2(c, ah - sg * Math.min(c.e * TAU, 0.6) * k / 5));
            env2.poly(glow, acc, 0.22 * a, false);
            env2.line([[c.cx, c.cy], P2(c, ah)], acc, Math.max(1.5, sz * 0.03), a, false);
            env2.circle(c.cx, c.cy, Math.max(1.5, sz * 0.04), acc, null, 0, a, false);
          }
        });
      });
    }
  };
  X.matrixOut = {
    name: "\u30C7\u30B8\u30BF\u30EB\u96E8",
    tags: ["glitch"],
    w: 0.8,
    ae: "glitch",
    outDur: (dur) => J.clamp(dur * 0.42, 0.34, 0.85),
    apply(env, it, p) {
      const seed = it.seed | 0, step = env.step, sz = it.size, sy0 = it.sy || 1, acc = env.sc.accent, c0 = colOf(it);
      const bb = dBox(it, 0), D = env.H - bb.y0 + sz * 1.2, head = darkBg(env) ? mixC(c0, "#ffffff", 0.4) : c0;
      const Q = (i) => win(p, J.r(seed, i, 201) * 0.9, 0.5);
      const rch = (i, k) => J.pool("half")[J.h(seed, i, k, Math.floor(step / 2), 202) % J.pool("half").length];
      const fall = (i, g) => {
        const q = Q(i);
        if (q <= 0) return null;
        const u = J.clamp((q - 0.22) / 0.78);
        return { q, u, L: D * Math.pow(u, 1.7), pitch: g.h * sy0 * 0.92 };
      };
      const headFn = (i, g) => {
        const f = fall(i, g);
        if (!f) return null;
        if (f.q >= 1) return HIDE;
        const [dx, dy] = dirI(it, 0, f.L);
        if (f.q < 0.22) return J.r(seed, i, step, 203) < f.q / 0.22 + 0.2 ? { ch: rch(i, 0), color: acc } : null;
        return { dx, dy, ch: rch(i, 0), color: head };
      };
      const trail = (k) => (i, g) => {
        const f = fall(i, g);
        if (!f || f.q < 0.22 || f.q >= 1 || f.L < k * f.pitch * 0.6) return HIDE;
        const [dx, dy] = dirI(it, 0, f.L - k * f.pitch);
        return { dx, dy, ch: rch(i, k), color: acc, a: (1 - k / 6) * (1 - 0.5 * f.u) };
      };
      withCopies(it, headFn, [trail(1), trail(2), trail(3), trail(4), trail(5)]);
    }
  };
  X.tornadoOut = {
    name: "\u7ADC\u5DFB",
    tags: ["pop"],
    w: 0.8,
    ae: "scatter",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.45, 0.38, 0.9),
    apply(env, it, p) {
      var _a;
      const seed = it.seed | 0, sz = it.size, dir = cutBit(env, 211) ? 1 : -1, single = isSingle(env, it), W = env.W;
      const bb = dBox(it, 0), up = bb.y1 + sz * 1.8;
      const lay = layOf(it);
      let cx = bb.cx, R0 = 1;
      if (single) {
        cx = W / 2;
        R0 = W * 0.45;
      } else for (const g of lay) R0 = Math.max(R0, Math.abs(toD(it, gCX(g, it), gCY(g, it))[0] - cx));
      R0 += sz * 0.3;
      const orbit = (X0, i, q, hx) => {
        const ox = X0 - cx, th0 = Math.asin(J.clamp(ox / R0, -1, 1)), r = J.r(seed, i, 212);
        const th = th0 + dir * Math.PI * (3.2 + r) * q * q;
        const lift = up * Math.pow(q, 2.2) * (0.8 + 0.3 * r) + sz * 1.4 * hx * E.outCubic(q);
        const R = R0 * (1 - 0.35 * E.outCubic(q)) + lift * 0.25 + sz * 0.6 * q;
        return { dx: R * Math.sin(th) - ox, dy: -lift, z: Math.cos(th), tilt: -Math.sin(th) * 14 * q };
      };
      const alpha = 1 - J.smooth(0.8, 0.98, p);
      if (single) {
        const q = win(p, J.r(env.cut.seed | 0, +it.mi || 0, 213) * 0.5, 0.25);
        if (q <= 0) return;
        const o = orbit(it.x, +it.mi || 0, q, orderOf(env, it)(0, 1));
        it.x += o.dx;
        it.y += o.dy;
        it.rot = (it.rot || 0) + o.tilt;
        it.size *= 0.72 + 0.28 * o.z;
        it._m = null;
        it.alpha = ((_a = it.alpha) != null ? _a : 1) * (0.3 + 0.7 * (o.z + 1) / 2) * alpha;
        return;
      }
      addC(it, (i, g) => {
        const q = win(p, J.r(seed, i, 214) * 0.5, 0.25);
        if (q <= 0) return null;
        const [X0] = toD(it, gCX(g, it), gCY(g, it)), o = orbit(X0, i, q, lay.N > 1 ? i / (lay.N - 1) : 0), [dx, dy] = dirI(it, o.dx, o.dy);
        return { dx, dy, rot: o.tilt, s: 0.72 + 0.28 * o.z, a: (0.3 + 0.7 * (o.z + 1) / 2) * alpha };
      });
    }
  };
  X.rollUpOut = {
    name: "\u5DFB\u304D\u53D6\u308B",
    tags: ["graphic", "editorial"],
    w: 0.8,
    ae: "wipe",
    outDur: (dur) => J.clamp(dur * 0.4, 0.32, 0.8),
    apply(env, it, p) {
      var _a;
      const sz = it.size, vert = !!it.vertical, rev = cutBit(env, 221), single = isSingle(env, it), c0 = colOf(it), bg = env.sc.bg;
      const b = lBox(it, sz * 0.08), sx0 = it.sx || 1, sy0 = it.sy || 1;
      let a0 = vert ? b.y0 : b.x0, a1 = vert ? b.y1 : b.x1;
      const c0x = vert ? b.x0 : b.y0, c1x = vert ? b.x1 : b.y1;
      let Xr, r0 = sz * 0.26;
      const e = E.inOutSine(J.clamp(p / 0.86));
      if (single) {
        const X0 = env.W * 0.03 - it.x, X1 = env.W * 0.97 - it.x;
        Xr = rev ? J.lerp(X1, X0, e) : J.lerp(X0, X1, e);
      } else Xr = rev ? J.lerp(a1 + r0, a0 - r0 * 3, e) : J.lerp(a0 - r0, a1 + r0 * 3, e);
      const rolled = rev ? Math.max(0, (single ? env.W * 0.97 - it.x : a1) - Xr) : Math.max(0, Xr - (single ? env.W * 0.03 - it.x : a0));
      const r = r0 * Math.sqrt(1 + rolled / (sz * 2));
      const sg = rev ? -1 : 1;
      const edge = Xr + sg * r;
      addC(it, (i, g) => {
        const c = vert ? gCY(g, it) : gCX(g, it), hw = (vert ? g.h * sy0 : g.w * sx0) / 2;
        const d = (c - edge) * sg;
        if (d + hw <= 0) return HIDE;
        const near = J.clamp(1 - (d - hw) / (r * 2.6));
        if (near <= 0) return null;
        const k = 1 - 0.55 * Math.pow(near, 1.6), nc = c - sg * (1 - k) * hw;
        const alongLocalX = vert ? !!g.r90 : !g.r90, kx = alongLocalX ? k : 1, ky = alongLocalX ? 1 : k;
        const o = Object.assign({ color: mixC(c0, bg, 0.45 * near), sx: kx, sy: ky }, vert ? { dy: nc - c } : { dx: nc - c });
        if ((nc - edge) * sg - k * hw < 0) Object.assign(o, vert ? gclip(g, it, gCX(g, it), nc, kx, ky, null, sg > 0 ? [edge, 1e5] : [-1e5, edge]) : gclip(g, it, nc, gCY(g, it), kx, ky, sg > 0 ? [edge, 1e5] : [-1e5, edge], null));
        return o;
      });
      const fa = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.82, 0.98, p)), back = mixC(c0, bg, 0.55), hi = mixC(c0, bg, 0.25), lo = mixC(c0, bg, 0.8);
      if (fa <= 0.01) return;
      chainPost(it, (env2, it2) => {
        inItem(env2, it2, () => {
          const lift = E.inCubic(J.clamp((p - 0.8) / 0.18)), cm = (c0x + c1x) / 2;
          const ext0 = J.lerp(c0x - sz * 0.06, cm, lift), ext1 = J.lerp(c1x + sz * 0.06, cm, lift), x0 = Xr - r * (1 - lift), w = 2 * r * (1 - lift);
          if (w < 0.3 || ext1 - ext0 < 0.3) return;
          const R = (s0, s1, col, a) => vert ? env2.rect(ext0, x0 + s0 * w, ext1 - ext0, (s1 - s0) * w, col, a, false) : env2.rect(x0 + s0 * w, ext0, (s1 - s0) * w, ext1 - ext0, col, a, false);
          R(0, 1, back, fa);
          R(0.18, 0.38, hi, fa);
          R(0.72, 1, lo, fa);
        });
      });
    }
  };
  X.snakeOut = {
    name: "\u5217\u306B\u306A\u3063\u3066\u53BB\u308B",
    tags: ["calm", "pop"],
    w: 0.9,
    ae: "stretch",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.45, 0.38, 0.9),
    apply(env, it, p) {
      var _a;
      const sz = it.size, rev = cutBit(env, 231), n = cutBit(env, 232) ? 1 : -1, W = env.W, H2 = env.H;
      const d = (a2, o, aEnd2, R2) => {
        if (a2 <= aEnd2) return [a2, o, 0];
        const s = a2 - aEnd2, arc = R2 * Math.PI / 2;
        if (s <= arc) {
          const f = s / R2, ca = Math.cos(f), sa = Math.sin(f);
          return [aEnd2 + R2 * sa - n * sa * o, n * R2 * (1 - ca) + ca * o, n * f / DEG];
        }
        return [aEnd2 + R2 - n * o, n * (R2 + s - arc), n * 90];
      };
      const ease = (x) => 0.35 * x * x + 0.65 * Math.pow(x, 2.4);
      if (isSingle(env, it)) {
        const ux = rev ? -1 : 1, aEnd2 = rev ? -W * 0.1 : W * 0.9, R2 = Math.min(W, H2) * 0.2;
        const a = it.x * ux, o = (it.y - H2 / 2) * ux;
        const Dt2 = W * 1.2 + R2 * 2 + H2 * 0.8, [na, no, rd] = d(a + Dt2 * ease(p), o, aEnd2, R2);
        it.x = na * ux;
        it.y = H2 / 2 + no * ux;
        it.rot = (it.rot || 0) + rd;
        return;
      }
      const vert = !!it.vertical, b = lBox(it);
      let u = vert ? [0, 1] : [1, 0];
      if (rev) u = [-u[0], -u[1]];
      const v = [-u[1], u[0]];
      const lay = layOf(it);
      let aMin = 1e9, aMax = -1e9;
      for (const g of lay) {
        const a = gCX(g, it) * u[0] + gCY(g, it) * u[1];
        aMin = Math.min(aMin, a);
        aMax = Math.max(aMax, a);
      }
      const aEnd = aMax + sz * 0.5, R = sz * 1.3, oc = b.cx * v[0] + b.cy * v[1];
      const Dt = aEnd - aMin + R * 2 + Math.max(W, H2) * 0.75;
      const trav = Dt * ease(p);
      addC(it, (i, g) => {
        const gx = gCX(g, it), gy = gCY(g, it), a = gx * u[0] + gy * u[1], o = gx * v[0] + gy * v[1] - oc;
        const [na, no, rd] = d(a + trav, o, aEnd, R);
        const X2 = u[0] * na + v[0] * (no + oc), Y2 = u[1] * na + v[1] * (no + oc);
        return { dx: X2 - gx, dy: Y2 - gy, rot: rd };
      });
      it.alpha = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.9, 1, p));
    }
  };
  X.flutterOut = {
    name: "\u3072\u3089\u3072\u3089\u843D\u3061\u308B",
    tags: ["emotional", "calm"],
    w: 1,
    ae: "fall",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.48, 0.42, 1),
    apply(env, it, p) {
      const seed = it.seed | 0, sz = it.size, bb = dBox(it, 0), D = env.H - bb.y0 + sz * 1.2, c0 = colOf(it), back = mixC(c0, env.sc.bg, 0.45);
      addC(it, (i) => {
        const q = win(p, J.r(seed, i, 241), 0.5);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const ph = J.r(seed, i, 242) * TAU, w = 7 + 4 * J.r(seed, i, 243), amp = Math.min(1, q * 4);
        const sway = Math.sin(q * w + ph) * sz * 0.55 * amp, L = D * (0.25 * q + 0.75 * Math.pow(q, 1.6));
        const flip = Math.cos(q * (9 + 5 * J.r(seed, i, 244)) + ph * 0.5);
        const [dx, dy] = dirI(it, sway, L);
        const shiver = q < 0.08 ? Math.sin(q * 300) * 6 * (1 - q / 0.08) : 0;
        return { dx, dy, rot: Math.cos(q * w + ph) * 28 * amp + shiver, sx: J.lerp(1, flip, amp), color: flip < 0 && amp > 0.5 ? back : void 0 };
      });
    }
  };
  X.rollOff = {
    name: "\u8EE2\u304C\u3063\u3066\u53BB\u308B",
    tags: ["pop"],
    w: 0.8,
    ae: "scatter",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.45, 0.38, 0.9),
    apply(env, it, p) {
      const dir = cutBit(env, 251) ? 1 : -1, sz = it.size, ord = orderOf(env, it), sx0 = it.sx || 1, sy0 = it.sy || 1;
      const [rx, ry] = rightI(it), [dnx, dny] = downI(it);
      addC(it, (i, g, n) => {
        const o = dir > 0 ? 1 - ord(i, n) : ord(i, n);
        const q = win(p, o, 0.45);
        if (q <= 0) return null;
        if (q >= 1) return HIDE;
        const [gxD] = toD(it, gCX(g, it), gCY(g, it));
        const dist = dir > 0 ? env.W - gxD + sz * 1.3 : gxD + sz * 1.3;
        const r = Math.max(g.w * sx0, sz * sy0 * 0.8) / 2;
        const along = dist * Math.pow(q, 1.7), th = along / r;
        const ph = (th % (Math.PI / 2) + Math.PI / 2) % (Math.PI / 2);
        const rise = r * (Math.SQRT2 * Math.cos(ph - Math.PI / 4) - 1);
        return { dx: rx * dir * along - dnx * rise, dy: ry * dir * along - dny * rise, rot: dir * th / DEG };
      });
    }
  };
  X.fanClose = {
    name: "\u6247\u3092\u9589\u3058\u308B",
    tags: ["graphic", "editorial", "emotional"],
    w: 0.9,
    ae: "shrink",
    minDur: 0.8,
    outDur: (dur) => J.clamp(dur * 0.44, 0.36, 0.85),
    apply(env, it, p) {
      var _a;
      const sz = it.size, rev = cutBit(env, 261), bend = E.inOutSine(J.clamp(p / 0.3)), close = E.inOutCubic(J.clamp((p - 0.24) / 0.5));
      const fin = J.clamp((p - 0.72) / 0.28), fs = 1 - 0.75 * E.inQuad(fin), alpha = 1 - J.smooth(0.8, 0.99, p);
      const spin = (rev ? 1 : -1) * 0.5 * E.inQuad(fin);
      const place = (a, w, R2, aLim2) => {
        const alT = J.lerp(a / R2, aLim2, close) + spin, rr = R2 - w;
        return [J.lerp(a, Math.sin(alT) * rr, bend), J.lerp(w, R2 - Math.cos(alT) * rr, bend), alT * bend / DEG];
      };
      if (isSingle(env, it)) {
        const W = env.W, H2 = env.H, R2 = W * 0.55, aLim2 = (rev ? 1 : -1) * W * 0.45 / R2;
        const [na, nw, rd] = place(it.x - W / 2, it.y - H2 / 2, R2, aLim2);
        const N = cutN(env), lead = Math.round(+it.mi || 0) === (rev ? N - 1 : 0);
        it.x = W / 2 + na;
        it.y = H2 / 2 + nw;
        it.rot = (it.rot || 0) + rd;
        it.size *= fs;
        it._m = null;
        it.alpha = ((_a = it.alpha) != null ? _a : 1) * alpha * (lead ? 1 : 1 - J.smooth(0.55, 0.95, close));
        return;
      }
      const vert = !!it.vertical, b = lBox(it), lay = layOf(it);
      const u = vert ? [0, 1] : [1, 0], v = [-u[1], u[0]];
      const ac = b.cx * u[0] + b.cy * u[1], oc = b.cx * v[0] + b.cy * v[1];
      let aMin = 1e9, aMax = -1e9;
      for (const g of lay) {
        const a = gCX(g, it) * u[0] + gCY(g, it) * u[1] - ac;
        aMin = Math.min(aMin, a);
        aMax = Math.max(aMax, a);
      }
      const half = Math.max(sz, (aMax - aMin) / 2), R = Math.max(sz * 2, half * 1.15);
      const aLim = (rev ? aMax : aMin) / R;
      addC(it, (i, g) => {
        const gx = gCX(g, it), gy = gCY(g, it);
        const a = gx * u[0] + gy * u[1] - ac, [na, nw, rd] = place(a, gx * v[0] + gy * v[1] - oc, R, aLim);
        const lead = Math.abs(a / R - aLim) < 1e-3;
        return { dx: u[0] * (na + ac) + v[0] * (nw + oc) - gx, dy: u[1] * (na + ac) + v[1] * (nw + oc) - gy, rot: rd, s: fs, a: alpha * (lead ? 1 : 1 - J.smooth(0.55, 0.95, close)) };
      });
    }
  };
  X.rgbSplitOut = {
    name: "\u8272\u5206\u89E3",
    tags: ["glitch", "pop"],
    w: 0.9,
    ae: "glitch",
    apply(env, it, p) {
      var _a;
      const sc = env.sc, sz = it.size, seed = it.seed | 0, step = env.step, dark = darkBg(env);
      const cols = [sc.ghostA || sc.accent, sc.ghostB || sc.fg, sc.accent];
      const e = E.inCubic(p), sp = sz * (0.05 + 1.5 * e);
      const dirs = [[-1, -0.3], [1, 0.25], [0.15, 0.9]];
      const a0 = (_a = it.alpha) != null ? _a : 1, ca = a0 * J.smooth(0, 0.1, p) * (1 - J.smooth(0.55, 0.96, p));
      it.alpha = a0 * (1 - J.smooth(0.05, 0.35, p));
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main" || ca <= 0.01) return;
        for (let k = 0; k < 3; k++) {
          const jx = J.rs(seed, step, k, 271) * sz * 0.06 * (0.3 + e), jy = J.rs(seed, step, k, 272) * sz * 0.015;
          const [ox, oy] = [dirs[k][0] * sp + jx, dirs[k][1] * sp * 0.5 + jy];
          J.drawItem(env2, copyOf(it2, {
            x: it2.x + ox,
            y: it2.y + oy,
            size: it2.size * (1 + 0.08 * k * e),
            _m: null,
            _lay: null,
            color: cols[k],
            gradient: null,
            pattern: null,
            shadow: null,
            extrude: null,
            alpha: ca * (k === 2 ? 0.8 : 0.9),
            blend: dark ? "screen" : "multiply",
            fill: true
          }));
        }
      });
    }
  };
  X.shockOut = {
    name: "\u885D\u6483\u6CE2",
    tags: ["pop", "graphic"],
    w: 0.9,
    ae: "explode",
    apply(env, it, p) {
      var _a;
      const sz = it.size, seed = it.seed | 0, acc = env.sc.accent, single = isSingle(env, it);
      const pre = Math.sin(Math.PI * J.clamp(p / 0.16)) * (p < 0.16 ? 1 : 0);
      const push = (dd, i) => {
        const passed = r - dd;
        if (passed <= 0) return [-pre * sz * 0.08, 1 - 0.05 * pre, 1, 0];
        const k = 1 - Math.exp(-passed / (sz * 0.7));
        return [sz * 1.6 * k, 1 + 0.2 * Math.exp(-passed / (sz * 0.25)) - 0.45 * J.smooth(0, sz * 2, passed), 1 - J.smooth(sz * 0.15, sz * 1.5, passed), J.rs(seed, i, 281) * 50 * k];
      };
      let r, Rmax, C;
      if (single) {
        C = [env.W / 2, env.H / 2];
        Rmax = Math.hypot(env.W, env.H) * 0.55;
        r = Rmax * E.outCubic(J.clamp((p - 0.12) / 0.88));
        const dx = it.x - C[0], dy = it.y - C[1], dd = Math.hypot(dx, dy) || 1, [m, s, a, rt] = push(dd, +it.mi || 0);
        it.x += dx / dd * m;
        it.y += dy / dd * m;
        it.size *= Math.max(0.05, s);
        it._m = null;
        it.rot = (it.rot || 0) + rt;
        it.alpha = ((_a = it.alpha) != null ? _a : 1) * a;
        if (Math.round(+it.mi || 0) !== 0) return;
      } else {
        const b = lBox(it);
        C = toD(it, b.cx, b.cy);
        Rmax = Math.hypot(b.w, b.h) / 2 + sz * 1.8;
        r = Rmax * E.outCubic(J.clamp((p - 0.12) / 0.88));
        addC(it, (i, g) => {
          const dx = gCX(g, it) - b.cx, dy = gCY(g, it) - b.cy, dd = Math.hypot(dx, dy) || 1, [m, s, a, rt] = push(dd, i);
          return { dx: dx / dd * m, dy: dy / dd * m, s: Math.max(0.05, s), a, rot: rt };
        });
      }
      if (r <= 0.5 || !primary(it)) return;
      const fa = 1 - J.smooth(0.3, 1, p);
      chainPost(it, (env2) => {
        env2.circle(C[0], C[1], r, null, acc, Math.max(1.5, sz * 0.09 * fa), fa, false);
        env2.circle(C[0], C[1], r * 0.82, null, acc, Math.max(1, sz * 0.025 * fa), fa * 0.6, false);
      });
    }
  };
  X.floodOut = {
    name: "\u6C34\u6CA1",
    tags: ["emotional", "calm"],
    w: 0.9,
    ae: "wipe",
    outDur: (dur) => J.clamp(dur * 0.4, 0.32, 0.8),
    apply(env, it, p) {
      var _a;
      const sz = it.size, t = env.ltb, acc = env.sc.accent, c0 = colOf(it), single = isSingle(env, it), H2 = env.H;
      const bb = dBox(it, sz * 0.15), U = single ? H2 * 0.12 : sz, A = U * 0.07;
      const e = E.inOutSine(J.clamp(p / 0.9));
      const Y = single ? J.lerp(H2 * 0.74, H2 * 0.24, e) : J.lerp(bb.y1 + A * 2, bb.y0 - A * 3, e);
      const K = J.clamp(Math.ceil(bb.w / (U * 0.12)), 8, 80), x0 = bb.x0 - sz * 0.2, x1 = bb.x1 + sz * 0.2;
      const surf = [];
      for (let k = 0; k <= K; k++) {
        const x = J.lerp(x0, x1, k / K);
        surf.push([x, Y + A * Math.sin(x / U * 4.2 + t * 5) + A * 0.5 * Math.sin(x / U * 9.5 - t * 7.3)]);
      }
      if (Y < bb.y0 - A * 2) {
        it.alpha = 0;
      }
      const above = (ctx) => {
        surf.forEach(([x, y], k) => k ? ctx.lineTo(x, y) : ctx.moveTo(x, y));
        ctx.lineTo(x1, -H2 * 2);
        ctx.lineTo(x0, -H2 * 2);
        ctx.closePath();
      };
      const below = (ctx) => {
        surf.forEach(([x, y], k) => k ? ctx.lineTo(x, y) : ctx.moveTo(x, y));
        ctx.lineTo(x1, H2 * 3);
        ctx.lineTo(x0, H2 * 3);
        ctx.closePath();
      };
      it.clipFn = above;
      const a0 = (_a = it.alpha) != null ? _a : 1, uw = (1 - J.smooth(0.3, 0.95, p)) * 0.5, fa = 1 - J.smooth(0.82, 0.98, p);
      chainPost(it, (env2, it2) => {
        if (uw > 0.01 && env2.pass === "main") {
          const n = 6, h = bb.h + sz, out = [];
          for (let j = 0; j < n; j++) {
            const y = bb.y0 - sz * 0.5 + j * h / n;
            out.push([y, y + h / n + 0.4, Math.sin(y / sz * 7 + t * 6) * sz * 0.04]);
          }
          J.drawFx(env2, copyOf(it2, { y: it2.y + sz * 0.05, alpha: a0 * uw, color: mixC(c0, acc, 0.55), gradient: null, pattern: null, shadow: null, clipFn: below, bands: out }));
        }
        if (env2.pass !== "main" || fa <= 0.01) return;
        env2.line(surf, acc, Math.max(4, U * 0.1), 0.15 * fa, false);
        env2.line(surf, acc, Math.max(1.5, U * 0.028), fa, false);
        for (let j = 0; j < 7; j++) {
          const x = J.lerp(x0, x1, J.r(it2.seed | 0, j, 291)), ph = (p * 2.2 + J.r(it2.seed | 0, j, 292)) % 1;
          const y = Y + A + (1 - ph) * sz * 0.9;
          if (y > bb.y1 + sz * 0.3) continue;
          env2.circle(x + Math.sin(ph * 9 + j) * sz * 0.03, y, Math.max(1, sz * 0.03 * (0.5 + ph)), null, acc, Math.max(1, sz * 0.01), 0.7 * fa * (1 - ph * 0.5), false);
        }
      });
    }
  };
  X.slashOut = {
    name: "\u4E00\u5200\u4E21\u65AD",
    tags: ["graphic", "emotional", "pop"],
    w: 1,
    ae: "slice",
    outDur: (dur) => J.clamp(dur * 0.4, 0.32, 0.8),
    apply(env, it, p) {
      var _a;
      const sz = it.size, vert = !!it.vertical, acc = env.sc.accent, b = lBox(it, sz * 0.2), seed = it.seed | 0;
      const tilt = (cutBit(env, 301) ? 1 : -1) * (vert ? 16 + 8 * cutR(env, 302) : 7 + 5 * cutR(env, 302));
      const th = ((vert ? 90 : 0) + tilt) * DEG, d = [Math.cos(th), Math.sin(th)], n = [-d[1], d[0]];
      const c = [b.cx, b.cy], L = Math.hypot(b.w, b.h) + sz * 4;
      const halfL = (sign) => {
        const o = [c[0] + n[0] * sign * L, c[1] + n[1] * sign * L];
        return [[c[0] - d[0] * L, c[1] - d[1] * L], [c[0] + d[0] * L, c[1] + d[1] * L], [o[0] + d[0] * L, o[1] + d[1] * L], [o[0] - d[0] * L, o[1] - d[1] * L]];
      };
      const lower = halfL(1), upper = halfL(-1);
      const down = d[1] > 0 ? d : [-d[0], -d[1]];
      const fl = J.clamp(p / 0.12), gap = J.smooth(0.12, 0.3, p), v = J.clamp((p - 0.3) / 0.7);
      const slide = sz * 0.06 * gap + sz * 2.2 * E.inQuad(v), a0 = (_a = it.alpha) != null ? _a : 1;
      const [ux, uy] = rot2(down[0] * slide - n[0] * sz * 0.05 * gap, down[1] * slide - n[1] * sz * 0.05 * gap, (it.rot || 0) * DEG);
      const [lx, ly] = rot2(-down[0] * sz * 0.35 * E.inQuad(v) + n[0] * sz * 0.03 * gap, -down[1] * sz * 0.35 * E.inQuad(v) + n[1] * (sz * 0.03 * gap + sz * 0.5 * E.inQuad(v)), (it.rot || 0) * DEG);
      const X0 = it.x, Y0 = it.y;
      it.x = X0 + lx;
      it.y = Y0 + ly;
      it.alpha = a0 * (1 - J.smooth(0.5, 0.95, p));
      it.clipFn = (ctx, e2, it3) => polyL(ctx, it3, lower);
      const ua = a0 * (1 - J.smooth(0.62, 0.98, p));
      chainPost(it, (env2, it2) => {
        J.drawFx(env2, copyOf(it2, { x: X0 + ux, y: Y0 + uy, alpha: ua, clipFn: (ctx, e3, it3) => polyL(ctx, it3, upper) }));
        if (env2.pass !== "main") return;
        const fa = 1 - J.smooth(0.12, 0.34, p);
        if (fa <= 0.01) return;
        const ext = (vert ? b.h : b.w) / 2 + sz * 0.9, P0 = [c[0] - d[0] * ext, c[1] - d[1] * ext], P1 = [c[0] + d[0] * ext, c[1] + d[1] * ext];
        const st = cutBit(env2, 303), A = st ? P1 : P0, B = st ? P0 : P1, e = E.outExpo(fl);
        const tail = J.smooth(0.08, 0.3, p);
        const S = [J.lerp(A[0], B[0], tail), J.lerp(A[1], B[1], tail)], T = [J.lerp(A[0], B[0], e), J.lerp(A[1], B[1], e)];
        inItem(env2, { x: X0, y: Y0, rot: it2.rot }, () => {
          env2.line([S, T], acc, Math.max(2, sz * 0.09 * fa), 0.35 * fa, false);
          env2.line([S, T], acc, Math.max(1.5, sz * 0.03 * fa), fa, false);
        });
      });
    }
  };
  let MOS = null;
  X.mosaicOut = {
    name: "\u30E2\u30B6\u30A4\u30AF",
    tags: ["glitch", "graphic"],
    w: 0.9,
    ae: "glitch",
    apply(env, it, p) {
      var _a;
      const sz = it.size, blk = sz * (0.01 + 0.34 * Math.pow(p, 1.5));
      if (blk * (env.scale || 1) < 1.6) return;
      const a0 = (_a = it.alpha) != null ? _a : 1, fade = 1 - J.smooth(0.55, 0.97, p), bb = dBox(it, sz * 0.15);
      it.alpha = 0;
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main" || typeof document === "undefined") return;
        let b = blk, ow = Math.ceil(bb.w / b), oh = Math.ceil(bb.h / b);
        if (ow > 360 || oh > 360) {
          b *= Math.max(ow, oh) / 360;
          ow = Math.ceil(bb.w / b);
          oh = Math.ceil(bb.h / b);
        }
        const cv = MOS || (MOS = document.createElement("canvas"));
        if (cv.width < ow + 2 || cv.height < oh + 2) {
          cv.width = Math.max(cv.width, ow + 2, 64);
          cv.height = Math.max(cv.height, oh + 2, 64);
        }
        const c2 = cv.getContext("2d");
        c2.setTransform(1, 0, 0, 1, 0, 0);
        c2.globalAlpha = 1;
        c2.globalCompositeOperation = "source-over";
        c2.filter = "none";
        c2.clearRect(0, 0, ow + 2, oh + 2);
        c2.setTransform(1 / b, 0, 0, 1 / b, -bb.x0 / b, -bb.y0 / b);
        J.drawItem(Object.assign({}, env2, { ctx: c2, scale: 1 / b, allowFilter: false }), copyOf(it2, { alpha: a0 * fade, blur: 0, shadow: null, blend: null }));
        const ctx = env2.ctx;
        ctx.save();
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(cv, 0, 0, ow, oh, bb.x0, bb.y0, ow * b, oh * b);
        ctx.restore();
      });
    }
  };
  X.scribbleOut = {
    name: "\u3050\u3057\u3083\u3050\u3057\u3083\u6D88\u3057",
    tags: ["editorial", "emotional", "pop"],
    w: 0.8,
    ae: "wipe",
    outDur: (dur) => J.clamp(dur * 0.4, 0.32, 0.8),
    apply(env, it, p) {
      var _a;
      const sz = it.size, seed = it.seed | 0, vert = !!it.vertical, acc = env.sc.accent, lines = lineExt(it);
      if (!lines.length) return;
      const paths = lines.map((L, li) => {
        const a0 = (vert ? L.y0 : L.x0) - sz * 0.15, a1 = (vert ? L.y1 : L.x1) + sz * 0.15, mid = vert ? (L.x0 + L.x1) / 2 : (L.y0 + L.y1) / 2;
        const half = (vert ? L.x1 - L.x0 : L.y1 - L.y0) / 2, len = a1 - a0, pts = [];
        const P2 = (a2, o) => vert ? [mid + o, a2] : [a2, mid + o];
        let a = a0, k = 0;
        while (a < a1 && k < 200) {
          const drift = Math.sin(a / sz * 1.3 + li) * half * 0.18;
          pts.push(P2(a, drift + (k & 1 ? 1 : -1) * half * (0.8 + 0.45 * J.r(seed, li, k, 312))));
          a += sz * (0.14 + 0.2 * J.r(seed, li, k, 311));
          k++;
        }
        a = a1;
        while (a > a0 && k < 400) {
          const drift = Math.sin(a / sz * 2.1 + li * 3) * half * 0.25;
          pts.push(P2(a, drift + (k & 1 ? 1 : -1) * half * (0.45 + 0.4 * J.r(seed, li, k, 314))));
          a -= sz * (0.2 + 0.25 * J.r(seed, li, k, 313));
          k++;
        }
        return pts;
      });
      const dr = E.inOutSine(J.clamp(p / 0.58));
      const lw = sz * 0.15 * (1 - 0.7 * J.smooth(0.62, 0.95, p)), sa = 1 - J.smooth(0.66, 0.97, p);
      it.alpha = ((_a = it.alpha) != null ? _a : 1) * (1 - J.smooth(0.45, 0.75, p));
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main" || sa <= 0.01) return;
        inItem(env2, it2, (ctx) => {
          ctx.save();
          ctx.lineJoin = "round";
          ctx.lineCap = "round";
          ctx.strokeStyle = acc;
          ctx.lineWidth = Math.max(1, lw);
          ctx.globalAlpha = sa;
          for (const pts of paths) {
            let tot = 0;
            const seg = [];
            for (let k = 1; k < pts.length; k++) {
              const s2 = Math.hypot(pts[k][0] - pts[k - 1][0], pts[k][1] - pts[k - 1][1]);
              seg.push(s2);
              tot += s2;
            }
            let rem = tot * dr;
            ctx.beginPath();
            ctx.moveTo(pts[0][0], pts[0][1]);
            for (let k = 1; k < pts.length && rem > 0; k++) {
              const s2 = seg[k - 1];
              if (rem >= s2) ctx.lineTo(pts[k][0], pts[k][1]);
              else {
                const f = rem / s2;
                ctx.lineTo(J.lerp(pts[k - 1][0], pts[k][0], f), J.lerp(pts[k - 1][1], pts[k][1], f));
              }
              rem -= s2;
            }
            ctx.stroke();
          }
          ctx.restore();
        });
      });
    }
  };
  X.candleOut = {
    name: "\u5439\u304D\u6D88\u3059",
    tags: ["emotional", "calm"],
    w: 0.9,
    ae: "drift",
    outDur: (dur) => J.clamp(dur * 0.45, 0.38, 0.9),
    apply(env, it, p) {
      const seed = it.seed | 0, sz = it.size, step = env.step, ord0 = orderOf(env, it), dir = cutBit(env, 321) ? 1 : -1;
      const ord = (i, n) => dir > 0 ? ord0(i, n) : 1 - ord0(i, n);
      const warm = mixC(colOf(it), env.sc.accent, 0.5), sub = env.sc.sub, lay = layOf(it), sy0 = it.sy || 1;
      const U = (i, n) => (p - ord(i, n) * 0.42) / 0.5;
      addC(it, (i, g, n) => {
        const u = U(i, n);
        if (u <= 0) return null;
        if (u >= 0.45) return HIDE;
        const lean = Math.sin(Math.PI * Math.min(1, u / 0.45)), fl = J.r(seed, i, step, 322);
        return { skew: -dir * 22 * lean, dx: dir * sz * 0.06 * lean, color: warm, a: u > 0.3 ? (1 - (u - 0.3) / 0.15) * (fl < 0.5 ? 1 : 0.6) : 0.65 + 0.35 * fl, s: 1 + 0.04 * lean };
      });
      chainPost(it, (env2, it2) => {
        if (env2.pass !== "main") return;
        inItem(env2, it2, () => {
          const [ux, uy] = dirI(it2, 0, -1), [rx, ry] = dirI(it2, 1, 0);
          for (const g of lay) {
            if (isSp(g.ch)) continue;
            const u = U(g.i, lay.N);
            if (u < 0.36 || u >= 1) continue;
            const age = (u - 0.36) / 0.64, tx = gCX(g, it2) + ux * g.h * sy0 * 0.4, ty = gCY(g, it2) + uy * g.h * sy0 * 0.4, pts = [];
            for (let k = 0; k <= 9; k++) {
              const f = k / 9, h = sz * (0.25 + 1.3 * age) * f, w = Math.sin(f * 5 + age * 7 + g.i) * sz * 0.09 * f + dir * sz * 0.4 * age * f;
              pts.push([tx + ux * h + rx * w, ty + uy * h + ry * w]);
            }
            env2.line(pts, sub, Math.max(1.2, sz * 0.03 * (1 - age * 0.5)), 0.9 * (1 - age), false);
          }
        });
      });
    }
  };
  for (const k of Object.keys(X)) {
    const f = X[k].apply;
    X[k].apply = (env, it, p, ctx) => {
      if (p >= 0.998) {
        it.alpha = 0;
        return;
      }
      f(env, it, p, ctx);
    };
    J.register("exit", k, X[k], P);
  }
  const H = {};
  H.glowFlicker = {
    name: "\u706F\u706B\u306E\u3086\u3089\u304E",
    tags: ["calm", "emotional"],
    w: 0.5,
    ae: "breathe",
    apply(env, it, amt) {
      var _a;
      const k = amt * motionK(env);
      if (k < 0.01) return;
      const t = env.ltb, seed = it.seed | 0, sz = it.size, kk = Math.min(1, k), sy0 = it.sy || 1;
      const f = 0.6 + 0.28 * J.noise1(t * 3.3, seed) + 0.12 * J.noise1(t * 11, seed + 5);
      const lvl = J.clamp(f * (1 - 0.45 * J.smooth(0.35, 0.8, J.noise1(t * 1.1, seed + 9))));
      const glow = darkBg(env) ? mixC(env.sc.accent, "#ffffff", 0.3) : env.sc.accent;
      if (env.pass === "main" && !it.shadow && !it.extrude && !glyphBlur(it)) it.shadow = { color: J.rgba(glow, darkBg(env) ? 0.9 : 0.6), blur: sz * (0.06 + 0.3 * lvl) * kk, dx: 0, dy: -sz * 0.02 * lvl * kk };
      it.alpha = ((_a = it.alpha) != null ? _a : 1) * (1 - 0.16 * (1 - lvl) * kk);
      it.color = mixC(colOf(it), env.sc.accent, 0.1 * lvl * kk);
      addC(it, (i, g) => {
        const st = 1 + 0.025 * k * (0.5 + 0.5 * J.noise1(t * 6 + i * 2.3, seed + i));
        return g.r90 ? { sx: st } : { sy: st, dy: -(st - 1) * sz * sy0 * 0.5 };
      });
    }
  };
  H.windGust = {
    name: "\u7A81\u98A8",
    tags: ["pop", "emotional"],
    w: 0.5,
    ae: "wave",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01) return;
      const cs = env.cut.seed | 0, sz = it.size, ord = orderOf(env, it);
      let cyc, since;
      if (env.beat && env.beat.len) {
        const L = env.beat.len, bi = env.beat.index - 1;
        cyc = Math.floor(bi / 4);
        since = (bi % 4 + 4) % 4 * L + env.beat.since;
      } else {
        const per = 2.2, T = env.ltb - 0.5 - cs % 5 * 0.06;
        cyc = Math.floor(T / per);
        since = T - cyc * per;
      }
      const dir = J.r(cs, cyc, 601) < 0.5 ? 1 : -1, str = 0.7 + 0.3 * J.r(cs, cyc, 602);
      addC(it, (i, g, n) => {
        const tau = since - (dir > 0 ? ord(i, n) : 1 - ord(i, n)) * 0.4;
        if (tau <= 0 || tau > 1.7) return null;
        const r = tau < 0.25 ? J.smooth(0, 0.25, tau) : tau < 0.6 ? 1 + 0.08 * Math.sin((tau - 0.25) * 30) : Math.cos((tau - 0.6) * 9) * Math.exp(-(tau - 0.6) * 4);
        return { dx: dir * sz * 0.12 * r * k * str, skew: -dir * 18 * r * k * str, rot: dir * 3 * r * k * str };
      });
    }
  };
  H.dangle = {
    name: "\u3076\u3089\u4E0B\u304C\u308A",
    tags: ["calm", "emotional"],
    w: 0.5,
    ae: "wave",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01) return;
      const t = env.ltb, seed = it.seed | 0, sz = it.size, sy0 = it.sy || 1, vert = !!it.vertical;
      addC(it, (i, g) => {
        const w = TAU * (0.5 + 0.28 * J.r(seed, i, 611)), ph = J.r(seed, i, 612) * TAU;
        const th = (6.5 * Math.sin(w * t + ph) + 1.6 * Math.sin(w * 2.7 * t + ph * 2)) * k * DEG;
        const hh = (vert ? g.h : sz) * sy0 / 2;
        return { dx: -hh * Math.sin(th), dy: hh * (Math.cos(th) - 1), rot: th / DEG };
      });
    }
  };
  H.eqBounce = {
    name: "\u97F3\u5727\u3067\u4F38\u3073\u308B",
    tags: ["pop", "graphic"],
    w: 0.5,
    ae: "breathe",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01) return;
      const t = env.ltb, seed = it.seed | 0, sz = it.size, sy0 = it.sy || 1, acc = env.sc.accent, c0 = colOf(it), vert = !!it.vertical;
      const base = env.energy != null ? J.clamp(env.energy * 1.2) : env.beat ? 0.25 + 0.75 * Math.exp(-env.beat.since * 7) : 0.45 + 0.4 * J.noise1(t * 2.6, seed);
      const mi = +it.mi || 0;
      addC(it, (i, g) => {
        const band = J.clamp(base * (0.3 + 0.7 * (0.5 + 0.5 * J.noise1(t * 6.5 + (i + mi) * 1.9, seed + 3))) * 1.15);
        const st = 1 + 0.32 * band * k, col = band > 0.72 ? mixC(c0, acc, (band - 0.72) / 0.28 * 0.7 * Math.min(1, k)) : void 0;
        if (vert) return g.r90 ? { sy: st, color: col } : { sx: st, color: col };
        return { sy: st, dy: -(st - 1) * sz * sy0 * 0.5, color: col };
      });
    }
  };
  H.flashBox = {
    name: "\u62CD\u3067\u53CD\u8EE2",
    tags: ["pop", "graphic", "glitch"],
    w: 0.4,
    ae: "glitchtick",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.35) return;
      const len = beatLen(env, 0.62), since = beatSince(env, 0.62), idx = beatIdx(env, 0.62);
      const u = since / (len * 0.6);
      if (u >= 1) return;
      const N = cutN(env), lay = layOf(it), vis = lay.filter((g2) => !isSp(g2.ch));
      if (!vis.length) return;
      const pick = J.h(env.cut.seed | 0, idx, 621) % N;
      let g;
      if (isSingle(env, it)) {
        if (Math.round(+it.mi || 0) !== pick) return;
        g = vis[0];
      } else g = vis[pick % vis.length];
      const pop = E.outBack(J.clamp(u / 0.18), 2.2), sz = it.size, sx0 = it.sx || 1, sy0 = it.sy || 1, acc = env.sc.accent, vert = !!it.vertical;
      const bg = J.fitContrast ? J.fitContrast(env.sc.bg, acc, 2.5) : env.sc.bg;
      addC(it, (i) => i === g.i ? { color: bg, s: 1 + 0.05 * (1 - u) } : null);
      chainPre(it, (env2, it2) => {
        if (env2.pass !== "main") return;
        inItem(env2, it2, () => {
          const w = (vert ? sz * sx0 : g.w * sx0) * 1.06 * pop, h = (vert ? g.h * sy0 : sz * sy0) * 1.06 * pop;
          env2.rect(gCX(g, it2) - w / 2, gCY(g, it2) - h / 2, w, h, acc, 1, false);
        });
      });
    }
  };
  H.glintSweep = {
    name: "\u5149\u6CA2",
    tags: ["graphic", "calm"],
    w: 0.5,
    ae: "still",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.05 || env.pass !== "main") return;
      const per = 2.4, T = env.ltb - 0.5 - (+it.mi || 0) * 0.04, u = (T % per + per) % per / 0.75;
      if (u >= 1) return;
      const sz = it.size, bb = dBox(it, sz * 0.2), c0 = colOf(it);
      const gc = J.lum(c0) > 0.72 ? env.sc.accent : mixC(c0, "#ffffff", 0.85);
      const bw = sz * 0.34, sl = bb.h * 0.45, x = J.lerp(bb.x0 - bw - sl, bb.x1 + bw + sl, E.inOutSine(u)), a = Math.min(1, k) * 0.9;
      chainPost(it, (env2, it2) => {
        var _a;
        const ctx = env2.ctx;
        ctx.save();
        ctx.beginPath();
        const band = (x0, w) => {
          ctx.moveTo(x0 + sl, bb.y0);
          ctx.lineTo(x0 + sl + w, bb.y0);
          ctx.lineTo(x0 - sl + w, bb.y1);
          ctx.lineTo(x0 - sl, bb.y1);
          ctx.closePath();
        };
        band(x - bw / 2, bw);
        band(x + bw * 0.75, bw * 0.28);
        ctx.clip();
        J.drawItem(env2, copyOf(it2, { color: gc, gradient: null, pattern: null, shadow: null, extrude: null, alpha: ((_a = it2.alpha) != null ? _a : 1) * a }));
        ctx.restore();
      });
    }
  };
  H.flipSwap = {
    name: "\u6642\u3005\u88CF\u8FD4\u308B",
    tags: ["glitch", "pop"],
    w: 0.4,
    ae: "glitchtick",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.5) return;
      const cs = env.cut.seed | 0, per = 1.6, T = env.ltb - 0.45, cyc = Math.floor(T / per), u = (T - cyc * per) / 0.95;
      if (u >= 1) return;
      const N = cutN(env), lay = layOf(it), vis = lay.filter((g) => !isSp(g.ch));
      if (!vis.length) return;
      const pick = J.h(cs, cyc, 631) % N;
      let gi;
      if (isSingle(env, it)) {
        if (Math.round(+it.mi || 0) !== pick) return;
        gi = vis[0].i;
      } else gi = vis[pick % vis.length].i;
      const ch = J.pool("kana")[J.h(cs, cyc, 632) % J.pool("kana").length], acc = env.sc.accent;
      let sx, alt;
      if (u < 0.18) {
        sx = Math.cos(u / 0.18 * Math.PI / 2);
        alt = false;
      } else if (u < 0.36) {
        sx = Math.sin((u - 0.18) / 0.18 * Math.PI / 2);
        alt = true;
      } else if (u < 0.64) {
        sx = 1;
        alt = true;
      } else if (u < 0.82) {
        sx = Math.cos((u - 0.64) / 0.18 * Math.PI / 2);
        alt = true;
      } else {
        sx = Math.sin((u - 0.82) / 0.18 * Math.PI / 2);
        alt = false;
      }
      sx = Math.max(0.02, sx);
      addC(it, (i, g) => i === gi ? Object.assign(g.r90 ? { sy: sx } : { sx }, alt ? { ch, color: acc } : null) : null);
    }
  };
  H.shadowSway = {
    name: "\u5F71\u304C\u63FA\u308C\u308B",
    tags: ["calm", "emotional"],
    w: 0.5,
    ae: "drift",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01 || it.extrude || env.pass !== "main") return;
      const t = env.ltb, seed = it.seed | 0, sz = it.size;
      const ang = (60 + 45 * Math.sin(t * TAU / 5.5 + seed % 10)) * DEG, L = sz * (0.12 + 0.05 * Math.sin(t * TAU / 3.3 + 1)) * Math.min(1.3, k);
      const col = darkBg(env) ? mixC(env.sc.bg, env.sc.accent, 0.42) : mixC(env.sc.bg, colOf(it), 0.3);
      it.extrude = { n: 12, dx: Math.cos(ang) * L, dy: Math.sin(ang) * L, color: col, fade: true, a: 0.9 };
    }
  };
  H.magnetJiggle = {
    name: "\u78C1\u529B",
    tags: ["pop", "glitch"],
    w: 0.4,
    ae: "jitter",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01) return;
      const t = env.ltb, seed = it.seed | 0, sz = it.size, step = env.step;
      let Mx, My, toItem = null;
      if (isSingle(env, it)) {
        const W = env.W, H2 = env.H;
        const [X2, Y2] = [W / 2 + Math.sin(t * 0.9 + env.cut.seed % 9) * W * 0.36, H2 / 2 + Math.sin(t * 1.7 + 1) * H2 * 0.12];
        [Mx, My] = toL(it, X2, Y2);
        toItem = true;
      } else {
        const b = lBox(it);
        Mx = b.cx + Math.sin(t * 0.9 + seed % 9) * b.w * 0.5;
        My = b.cy + Math.sin(t * 1.7 + 1) * b.h * 0.9;
      }
      addC(it, (i, g) => {
        const gx = toItem ? 0 : gCX(g, it), gy = toItem ? 0 : gCY(g, it), vx = Mx - gx, vy = My - gy, d = Math.hypot(vx, vy) || 1;
        const f = 1 / (1 + Math.pow(d / (sz * 1.1), 2)), pull = sz * 0.2 * f * k;
        const buzz = f > 0.35 ? J.rs(seed, step, i, 641) * sz * 0.014 * k * f : 0;
        return { dx: vx / d * pull + buzz, dy: vy / d * pull, rot: vx / d * 7 * f * k };
      });
    }
  };
  H.typeRattle = {
    name: "\u30BF\u30A4\u30D7\u306E\u9707\u3048",
    tags: ["editorial", "glitch"],
    w: 0.5,
    ae: "jitter",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01) return;
      const idx = beatIdx(env, 0.45), since = beatSince(env, 0.45), seed = it.seed | 0, sz = it.size, kk = Math.min(1, k);
      addC(it, (i) => {
        let ep = -1;
        for (let b = idx; b > idx - 7; b--) if (J.r(seed, b, i, 651) < 0.3) {
          ep = b;
          break;
        }
        const hit = ep === idx && since < 0.1 ? 1 - since / 0.1 : 0;
        return { dy: J.rs(seed, ep, i, 652) * sz * 0.045 * k + hit * sz * 0.04 * k, dx: J.rs(seed, ep, i, 655) * sz * 0.01 * k, rot: J.rs(seed, ep, i, 653) * 3.2 * k, a: 1 - 0.2 * J.r(seed, ep, i, 654) * kk - 0.15 * hit * kk };
      });
    }
  };
  H.focusRack = {
    name: "\u30D4\u30F3\u30C8\u9001\u308A",
    tags: ["calm", "emotional"],
    w: 0.5,
    ae: "breathe",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01) return;
      const ord = orderOf(env, it), t = env.ltb, seed = it.seed | 0, sz = it.size, kk = Math.min(1, k);
      const fpos = 0.5 + 0.62 * Math.sin(t * TAU / 4.6 + (env.cut.seed | 0) % 7);
      const maxB = env.pass === "main" && env.allowFilter && !it.extrude ? Math.min(sz * 0.065, 18) * kk : 0;
      addC(it, (i, g, n) => {
        const df = J.clamp((Math.abs(ord(i, n) - fpos) - 0.1) / 0.5);
        if (df <= 0) return null;
        return { blur: maxB * df * df, a: 1 - 0.28 * df * kk, s: 1 + 0.035 * df * k };
      });
    }
  };
  H.pluckString = {
    name: "\u5F26\u306E\u632F\u52D5",
    tags: ["pop", "emotional"],
    w: 0.4,
    ae: "wave",
    apply(env, it, amt) {
      const k = amt * motionK(env);
      if (k < 0.01) return;
      let tau;
      if (env.beat && env.beat.len) tau = (env.beat.index % 4 + 4) % 4 * env.beat.len + env.beat.since;
      else {
        const per = 2.4;
        tau = ((env.ltb - 0.45) % per + per) % per;
      }
      const sz = it.size, A = sz * 0.18 * k * Math.exp(-tau * 1.7) * Math.min(1, tau / 0.04);
      if (A < 0.3) return;
      const vert = !!it.vertical, single = isSingle(env, it), N = cutN(env), mi = +it.mi || 0;
      const w1 = Math.cos(tau * TAU * 3.2), w2 = Math.cos(tau * TAU * 6.6 + 1);
      addC(it, (i, g) => {
        const x = single ? (mi + 1) / (N + 1) : (g.ci + 1) / (g.n + 1);
        const y = A * (Math.sin(Math.PI * x) * w1 + 0.3 * Math.sin(TAU * x) * w2);
        return vert ? { dx: y } : { dy: y };
      });
    }
  };
  for (const k of Object.keys(H)) J.register("hold", k, H[k], P);
})();
