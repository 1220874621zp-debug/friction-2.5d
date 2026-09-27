(() => {
  "use strict";
  J.TREAT = { none: { name: "\u306A\u3057", apply() {
  } } };
  J.TREAT_ORDER = ["none"];
  J.BG = { none: { name: "\u7121\u5730", draw() {
  } } };
  J.BG_ORDER = ["none"];
  J.CAMERA = {
    push: {
      name: "\u3086\u3063\u304F\u308A\u5BC4\u308B",
      tags: ["calm", "editorial", "emotional", "graphic", "pop", "glitch"],
      w: 5,
      get: (env) => {
        var _a;
        return { s: 1 + 0.03 * ((_a = env.fx.motion) != null ? _a : 0.7) * J.clamp(env.lt / Math.max(0.3, env.cut.dur)) };
      }
    }
  };
  J.CAMERA_ORDER = ["push"];
  J.FXE = {
    slice: { name: "\u30B9\u30E9\u30A4\u30B9\u30B0\u30EA\u30C3\u30C1", builtin: true },
    block: { name: "\u30D6\u30ED\u30C3\u30AF\u30B0\u30EA\u30C3\u30C1", builtin: true },
    invert: { name: "\u53CD\u8EE2", builtin: true },
    flash: { name: "\u30D5\u30E9\u30C3\u30B7\u30E5", builtin: true },
    zoom: { name: "\u30BA\u30FC\u30E0\u30D6\u30E9\u30FC", builtin: true },
    mosaic: { name: "\u30E2\u30B6\u30A4\u30AF", builtin: true },
    shake: { name: "\u63FA\u308C", builtin: true },
    chroma: { name: "\u8272\u30BA\u30EC\u306E\u8DF3\u306D", builtin: true }
  };
  J.FXE_ORDER = ["chroma", "shake", "slice", "block", "invert", "flash", "zoom", "mosaic"];
  J.TRANS = {};
  J.TRANS_ORDER = [];
  const GROUPS = {
    layout: ["LAYOUTS", "LAYOUT_ORDER"],
    enter: ["ENTER", "ENTER_ORDER"],
    hold: ["HOLD", "HOLD_ORDER"],
    exit: ["EXIT", "EXIT_ORDER"],
    decor: ["DECOR", "DECOR_ORDER"],
    treat: ["TREAT", "TREAT_ORDER"],
    bg: ["BG", "BG_ORDER"],
    cam: ["CAMERA", "CAMERA_ORDER"],
    fx: ["FXE", "FXE_ORDER"],
    trans: ["TRANS", "TRANS_ORDER"]
  };
  J.GROUP_KEYS = Object.keys(GROUPS);
  J.registry = (g) => J[GROUPS[g][0]];
  J.order = (g) => J[GROUPS[g][1]];
  J.register = (group, key, def, pack) => {
    const G = GROUPS[group];
    if (!G) throw new Error("unknown group " + group);
    if (!def || !def.name) throw new Error(`${group}.${key}: name is required`);
    const reg = J[G[0]], order = J[G[1]];
    if (reg[key] && reg[key].pack !== pack) console.warn(`JIZURA: ${group}.${key} is being replaced`);
    def.pack = pack || def.pack || "core";
    reg[key] = def;
    if (!def.special && !order.includes(key)) order.push(key);
    return def;
  };
  J.registerAll = (group, defs, pack) => {
    for (const k of Object.keys(defs)) J.register(group, k, defs[k], pack);
  };
  J.taggedWith = (group, mood) => J.order(group).filter((k) => {
    const d = J.registry(group)[k];
    return d && d.tags && d.tags.includes(mood);
  });
})();
