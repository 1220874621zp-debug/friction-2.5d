(() => {
  "use strict";
  J.MOODS = {
    glitch: {
      name: "\u30B0\u30EA\u30C3\u30C1",
      fx: { motion: [0.6, 0.9], glitch: [0.75, 1], chroma: [0.75, 1], decor: [0.3, 0.6], density: [0.6, 0.9], texture: [0.5, 0.9], bgSwitch: [0.3, 0.6] },
      layout: ["center", "condensed", "huge", "tile", "marquee", "vcols", "scatter", "stack"],
      enter: ["slice", "scramble", "assemble", "flicker", "zoom", "stretch"],
      exit: ["glitch", "slice", "explode", "fall"],
      styles: ["noir", "crimson", "mint", "mono", "hud"]
    },
    calm: {
      name: "\u3057\u3063\u3068\u308A",
      fx: { motion: [0.3, 0.55], glitch: [0.05, 0.25], chroma: [0.2, 0.5], decor: [0.2, 0.5], density: [0.25, 0.45], texture: [0.5, 0.85], bgSwitch: [0.1, 0.3] },
      layout: ["center", "vcols", "gloss", "stack", "circle", "type", "mixed"],
      enter: ["blur", "type", "wipe", "assemble"],
      exit: ["blur", "drift", "wipe", "shrink"],
      styles: ["specimen", "paper", "hud", "noir"],
      noHold: ["glitchtick", "jitter"]
    },
    pop: {
      name: "\u30DD\u30C3\u30D7",
      fx: { motion: [0.7, 1], glitch: [0.1, 0.35], chroma: [0.3, 0.6], decor: [0.6, 1], density: [0.5, 0.8], texture: [0.2, 0.5], bgSwitch: [0.4, 0.8] },
      layout: ["mixed", "scatter", "wave", "labels", "pill", "ring", "huge", "diag", "center"],
      enter: ["pop", "drop", "spin", "stretch", "zoom"],
      exit: ["scatter", "shrink", "stretch", "blur"],
      styles: ["magenta", "caution", "transit", "blueprint", "rouge"]
    },
    graphic: {
      name: "\u30B0\u30E9\u30D5\u30A3\u30C3\u30AF",
      fx: { motion: [0.5, 0.8], glitch: [0.2, 0.5], chroma: [0.4, 0.7], decor: [0.7, 1], density: [0.5, 0.8], texture: [0.4, 0.7], bgSwitch: [0.3, 0.7] },
      layout: ["diag", "labels", "marquee", "tile", "condensed", "huge", "circle", "pill"],
      enter: ["wipe", "slice", "stretch", "zoom"],
      exit: ["wipe", "slice", "stretch"],
      styles: ["blueprint", "caution", "rouge", "mint", "transit"]
    },
    editorial: {
      name: "\u30A8\u30C7\u30A3\u30C8\u30EA\u30A2\u30EB",
      fx: { motion: [0.4, 0.65], glitch: [0.1, 0.3], chroma: [0.2, 0.45], decor: [0.4, 0.7], density: [0.35, 0.6], texture: [0.6, 0.9], bgSwitch: [0.2, 0.4] },
      layout: ["gloss", "vcols", "mixed", "stack", "type", "center", "circle"],
      enter: ["type", "blur", "wipe", "assemble"],
      exit: ["blur", "drift", "wipe"],
      styles: ["specimen", "paper", "noir", "mono", "hud"]
    },
    emotional: {
      name: "\u30A8\u30E2\u30FC\u30B7\u30E7\u30CA\u30EB",
      fx: { motion: [0.55, 0.85], glitch: [0.3, 0.6], chroma: [0.5, 0.85], decor: [0.3, 0.6], density: [0.4, 0.7], texture: [0.6, 1], bgSwitch: [0.2, 0.5] },
      layout: ["huge", "center", "vcols", "stack", "condensed", "mixed", "circle"],
      enter: ["assemble", "blur", "zoom", "wipe", "slice"],
      exit: ["drift", "explode", "fall", "blur"],
      styles: ["noir", "paper", "hud", "mono", "crimson"]
    },
    // ホラー: only offered when the project's ホラー switch is on (the horror set's parts come with it)
    horror: {
      name: "\u30DB\u30E9\u30FC",
      set: "horror",
      fx: { motion: [0.35, 0.65], glitch: [0.3, 0.7], chroma: [0.2, 0.5], decor: [0.3, 0.6], density: [0.3, 0.55], texture: [0.7, 1], bgSwitch: [0.1, 0.3] },
      layout: ["center", "vcols", "stack", "huge", "type"],
      enter: ["flicker", "blur", "type", "scramble"],
      exit: ["blur", "glitch", "fall", "drift"],
      styles: ["noir", "mono", "crimson"],
      noHold: ["wave"],
      sprinkle: 0.1
    },
    chaos: {
      name: "\u5168\u90E8\u5165\u308A",
      fx: { motion: [0.5, 1], glitch: [0.3, 1], chroma: [0.4, 1], decor: [0.4, 1], density: [0.45, 0.9], texture: [0.3, 1], bgSwitch: [0.3, 0.9] },
      layout: null,
      enter: null,
      exit: null,
      styles: null
    }
  };
  (() => {
    const add = (g, k, m) => {
      const d = J.registry(g)[k];
      if (!d) return;
      d.tags = d.tags || [];
      if (!d.tags.includes(m)) d.tags.push(m);
    };
    for (const [m, M] of Object.entries(J.MOODS)) for (const g of ["layout", "enter", "exit"]) for (const k of M[g] || []) add(g, k, m);
    const extra = {
      hold: { still: ["calm", "editorial", "emotional", "graphic"], drift: ["calm", "emotional", "editorial"], breathe: ["calm", "emotional"], wave: ["pop"], jitter: ["glitch", "pop"], glitchtick: ["glitch"] },
      decor: {
        brackets: ["graphic", "editorial"],
        rings: ["graphic", "emotional"],
        dots: ["pop", "graphic"],
        arrows: ["pop", "graphic"],
        slash: ["glitch", "graphic"],
        sparks: ["pop"],
        leaders: ["editorial", "calm"],
        waveform: ["emotional", "calm"],
        barcode: ["glitch", "graphic"],
        grid: ["graphic", "editorial"],
        stripes: ["graphic", "pop"],
        blobs: ["pop", "emotional"],
        bars: ["graphic", "glitch"],
        shapes: ["pop", "graphic"],
        counter: ["graphic", "editorial"]
      },
      fx: { chroma: ["glitch", "emotional", "pop", "graphic"], shake: ["pop", "glitch", "emotional"], slice: ["glitch"], block: ["glitch"], invert: ["glitch", "graphic"], flash: ["pop", "emotional", "glitch"], zoom: ["pop", "emotional"], mosaic: ["glitch"] }
    };
    for (const [g, map] of Object.entries(extra)) for (const [k, ms] of Object.entries(map)) ms.forEach((m) => add(g, k, m));
  })();
  J.omakase = (project, rnd = Math.random) => {
    const pick = (a) => a[Math.floor(rnd() * a.length) % a.length];
    const range = (r) => +(r[0] + (r[1] - r[0]) * rnd()).toFixed(2);
    const moodOk = (k) => !J.MOODS[k].set || J.setOn && J.setOn(project, J.MOODS[k].set);
    const moods = Object.keys(J.MOODS).filter((k) => k !== project.mood && moodOk(k));
    const mood = moodOk("horror") && rnd() < 0.55 ? "horror" : pick(moods), M = J.MOODS[mood];
    const moodSetOk = (d) => !(d && d.set) || !Object.values(J.MOODS).some((m) => m.set === d.set) || M.set === d.set;
    const okStyle = (k) => J.STYLES[k] && (!J.randomOk || J.randomOk(project, "style", k)) && moodSetOk(J.STYLES[k]);
    const moodStyles = [.../* @__PURE__ */ new Set([...M.styles || [], ...J.STYLE_ORDER.filter((k) => (J.STYLES[k].moods || []).includes(mood))])].filter(okStyle);
    let pool = (moodStyles.length && rnd() < 0.72 ? moodStyles : J.STYLE_ORDER.filter(okStyle)).filter((k) => k !== project.style);
    if (!pool.length) pool = J.STYLE_ORDER.filter((k) => k !== project.style && okStyle(k));
    if (!pool.length) pool = J.STYLE_ORDER.filter((k) => k !== project.style);
    const style = pick(pool);
    const fx = Object.assign({}, project.fx);
    for (const k of Object.keys(M.fx)) fx[k] = range(M.fx[k]);
    fx.koma = pick({ horror: [12, 8, 0], glitch: [12, 12, 8], pop: [12, 12, 8, 0], calm: [0, 0, 12], editorial: [0, 12], emotional: [12, 0], graphic: [12, 12, 0] }[mood] || [12, 8, 0]);
    fx.onTwos = fx.koma > 0;
    fx.flash = rnd() < 0.65;
    fx.hud = pick(["auto", "auto", "on", "off"]);
    const enabled = {};
    const MIN = { layout: 6, enter: 5, exit: 5, hold: 3, decor: 6, treat: 4, bg: 4, cam: 3, fx: 4, trans: 3 };
    for (const g of J.GROUP_KEYS) {
      const order = J.order(g).filter((k) => !(J.registry(g)[k] || {}).special && (!J.randomOk || J.randomOk(project, g, k)) && moodSetOk(J.registry(g)[k]));
      const hand = ["layout", "enter", "exit"].includes(g) && Array.isArray(M[g]) ? M[g] : [];
      const prefer = mood === "chaos" ? null : /* @__PURE__ */ new Set([...hand, ...J.taggedWith(g, mood)]);
      const on = {};
      for (const k of J.order(g)) if (!moodSetOk(J.registry(g)[k])) on[k] = false;
      for (const k of order) on[k] = prefer ? prefer.has(k) || rnd() < (M.sprinkle || 0.22) : rnd() < 0.8;
      const offs = order.filter((k) => !on[k]);
      let n = order.length - offs.length;
      while (n < Math.min(MIN[g] || 3, order.length) && offs.length) {
        const k = offs.splice(Math.floor(rnd() * offs.length), 1)[0];
        on[k] = true;
        n++;
      }
      enabled[g] = on;
    }
    enabled.enter.cut = true;
    enabled.exit.cut = true;
    if (enabled.treat) enabled.treat.none = true;
    if (enabled.bg) enabled.bg.none = true;
    if (enabled.cam) enabled.cam.push = true;
    for (const k of M.noHold || []) if (enabled.hold[k] !== void 0) enabled.hold[k] = false;
    enabled.hold.still = true;
    const fonts = {};
    const faces = Object.entries(J.FONTS).filter(([k, f]) => !f.user && !["mono", "pixel"].includes(f.kind) && (!J.randomOk || J.randomOk(project, "font", k)));
    if (rnd() < 0.4) fonts.display = pick(faces.filter(([k, f]) => f.weight >= 700 || f.kind === "display" || f.kind === "round"))[0];
    if (rnd() < 0.3) fonts.serif = pick(faces.filter(([k, f]) => f.kind === "mincho" || f.kind === "brush"))[0];
    if (mood === "chaos" && rnd() < 0.2) fonts.display = "dot";
    const colors = Object.assign({}, project.colors, { enabled: false, accentOn: false });
    if (rnd() < 0.38) {
      const bg = J.STYLES[style].schemes[0].bg;
      Object.assign(colors, J.randomPalette(bg, rnd), { accentOn: true });
      delete colors.mode;
    }
    const overrides = {};
    for (const [i, o] of Object.entries(project.overrides || {})) if (o.lock) overrides[i] = o;
    return { mood, style, fx, enabled, fonts, colors, overrides, seed: Math.floor(rnd() * 1e9) };
  };
})();
