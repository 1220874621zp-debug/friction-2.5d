(() => {
  "use strict";
  J.BASE_PACKS = ["core", void 0, "layoutsA", "layoutsB", "enter", "exitHold", "decor", "looks"];
  J.BASE_STYLES = ["noir", "crimson", "caution", "magenta", "paper", "hud", "mint", "specimen", "transit", "blueprint", "rouge", "mono"];
  J.EXTRA_FONTS = ["reggae", "rampart", "potta", "kiwi", "klee", "shippori"];
  J.WA = {
    layout: ["ema", "chochin", "noren", "tanzaku", "omikuji", "kakejiku", "shoji", "karuta", "origami", "postcard", "letterPaper", "genkou", "hanko"],
    enter: ["fanOpen", "brushReveal"],
    exit: ["fanClose"],
    decor: ["seal", "kamon", "seigaiha", "asanoha", "chochin", "shimenawa", "sensu", "tsukiKumo", "momiji", "namiGashira", "kasumi", "brushStroke", "petals"],
    bg: ["seigaiha", "asanoha"],
    treat: ["monoGrid"],
    style: ["sakura", "sumi"]
  };
  J.SETS = { horror: { on: false }, typo: { on: true }, kinetic: { on: true } };
  J.SET_ORDER = Object.keys(J.SETS);
  J.setOn = (project, set) => {
    const v = project && project[set];
    return typeof v === "boolean" ? v : !!(J.SETS[set] && J.SETS[set].on);
  };
  const def = (g, k) => g === "style" ? J.STYLES[k] : g === "font" ? J.FONTS[k] : (J.registry(g) || {})[k];
  for (const g of J.GROUP_KEYS) for (const k of J.order(g)) {
    const d = def(g, k);
    if (d && !d.set && J.SETS[d.pack]) d.set = d.pack;
  }
  for (const g of J.GROUP_KEYS) for (const k of J.order(g)) {
    const d = def(g, k);
    if (d && !d.set && !J.BASE_PACKS.includes(d.pack)) d.extra = true;
  }
  for (const k of J.STYLE_ORDER) if (!J.BASE_STYLES.includes(k) && !J.STYLES[k].set) J.STYLES[k].extra = true;
  for (const k of J.EXTRA_FONTS) if (J.FONTS[k]) J.FONTS[k].extra = true;
  for (const [g, keys] of Object.entries(J.WA)) for (const k of keys) {
    const d = def(g, k);
    if (d) d.wa = true;
  }
  J.isWa = (g, k) => {
    const d = def(g, k);
    return !!(d && d.wa);
  };
  J.isExtra = (g, k) => {
    const d = def(g, k);
    return !!(d && d.extra);
  };
  J.setOf = (g, k) => {
    const d = def(g, k);
    return d && d.set || null;
  };
  J.randomOk = (project, g, k) => {
    const d = def(g, k);
    if (!d) return false;
    if (d.extra && !(project && project.extra === true)) return false;
    if (d.wa && project && project.wa === false) return false;
    if (d.set && !J.setOn(project, d.set)) return false;
    return true;
  };
})();
