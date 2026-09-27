var __async = (__this, __arguments, generator) => {
  return new Promise((resolve, reject) => {
    var fulfilled = (value) => {
      try {
        step(generator.next(value));
      } catch (e) {
        reject(e);
      }
    };
    var rejected = (value) => {
      try {
        step(generator.throw(value));
      } catch (e) {
        reject(e);
      }
    };
    var step = (x) => x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected);
    step((generator = generator.apply(__this, __arguments)).next());
  });
};
(() => {
  "use strict";
  J.saveFile = (filename, data) => __async(null, null, function* () {
    const blob = data instanceof Blob ? data : new Blob([data]);
    try {
      if (window.claude && typeof window.claude.use === "function") {
        const dl = yield window.claude.use("downloads");
        if (dl) {
          yield dl.save({ filename, data: blob });
          return "saved";
        }
      }
    } catch (e) {
      if (e && e.code === "declined") return "declined";
      if (e && e.code && e.code !== "unavailable" && e.code !== "not_granted") throw e;
    }
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 3e4);
    return "saved";
  });
  const VIDEO_CANDS = [
    { codec: "avc1.640034", mux: "avc", label: "H.264 High" },
    { codec: "avc1.640033", mux: "avc", label: "H.264 High" },
    { codec: "avc1.4d0033", mux: "avc", label: "H.264 Main" },
    { codec: "avc1.42003e", mux: "avc", label: "H.264 Baseline" },
    { codec: "vp09.00.51.08", mux: "vp9", label: "VP9" },
    { codec: "av01.0.12M.08", mux: "av1", label: "AV1" }
  ];
  J.videoBitrate = (w, h, fps, quality) => {
    const want = w * h * fps * (quality === "max" ? 0.42 : quality === "high" ? 0.28 : 0.16);
    const px = w * h, cap = px <= 22e5 ? 4e7 : px <= 38e5 ? 6e7 : 9e7;
    return Math.round(Math.min(want, cap));
  };
  const vcfg = (c, w, h, fps, bitrate, hw) => {
    const cfg = { codec: c.codec, width: w, height: h, bitrate, framerate: fps };
    if (hw) cfg.hardwareAcceleration = hw;
    if (c.mux === "avc") cfg.avc = { format: "avc" };
    return cfg;
  };
  function supported(cfg) {
    return __async(this, null, function* () {
      try {
        const s = yield VideoEncoder.isConfigSupported(cfg);
        return !!(s && s.supported);
      } catch (e) {
        return false;
      }
    });
  }
  const codecMemo = /* @__PURE__ */ new Map();
  J.pickVideoCodec = (w, h, fps, bitrate) => {
    const key = [w, h, fps, bitrate].join("/");
    if (!codecMemo.has(key)) codecMemo.set(key, (() => __async(null, null, function* () {
      if (typeof VideoEncoder === "undefined") return null;
      for (const c of VIDEO_CANDS) {
        const cfg = vcfg(c, w, h, fps, bitrate);
        if (yield supported(cfg)) return Object.assign({}, c, { cfg });
      }
      return null;
    }))());
    return codecMemo.get(key).then((vc) => vc && Object.assign({}, vc, { cfg: Object.assign({}, vc.cfg) }));
  };
  J.videoAttempts = (w, h, fps, bitrate) => __async(null, null, function* () {
    if (typeof VideoEncoder === "undefined") return [];
    const out = [], seen = /* @__PURE__ */ new Set();
    const add = (c, hw, br) => __async(null, null, function* () {
      const key = c.codec + "|" + (hw || "") + "|" + br;
      if (seen.has(key) || out.length >= 5) return;
      const cfg = vcfg(c, w, h, fps, br, hw);
      if (yield supported(cfg)) {
        seen.add(key);
        out.push(Object.assign({}, c, { cfg, hw: hw || "auto" }));
      }
    });
    let first = null;
    for (const c of VIDEO_CANDS) {
      const cfg = vcfg(c, w, h, fps, bitrate);
      if (yield supported(cfg)) {
        first = c;
        break;
      }
    }
    if (first) {
      yield add(first, null, bitrate);
      yield add(first, "prefer-software", bitrate);
    }
    const avc = VIDEO_CANDS.filter((c) => c.mux === "avc" && c !== first);
    for (const c of avc) {
      yield add(c, "prefer-software", Math.round(bitrate * 0.7));
      if (out.length >= 3) break;
    }
    for (const c of VIDEO_CANDS.filter((c2) => c2.mux !== "avc")) yield add(c, "prefer-software", Math.round(bitrate * 0.7));
    return out;
  });
  J.pickAudioCodec = (sr, chn) => __async(null, null, function* () {
    if (typeof AudioEncoder === "undefined") return null;
    for (const c of [{ codec: "mp4a.40.2", mux: "aac", sr: 48e3 }, { codec: "opus", mux: "opus", sr: 48e3 }]) {
      try {
        const s = yield AudioEncoder.isConfigSupported({ codec: c.codec, sampleRate: c.sr, numberOfChannels: chn, bitrate: 192e3 });
        if (s.supported) return c;
      } catch (e) {
      }
    }
    return null;
  });
  function resample(buffer, sr, duration, offset = 0) {
    return __async(this, null, function* () {
      const chn = Math.min(2, buffer.numberOfChannels);
      const len = Math.ceil(duration * sr);
      const oc = new OfflineAudioContext(chn, len, sr);
      const src = oc.createBufferSource();
      src.buffer = buffer;
      src.connect(oc.destination);
      src.start(0, Math.max(0, offset));
      return oc.startRendering();
    });
  }
  J.exportSpan = (plan, range) => {
    const t0 = range ? Math.max(0, range.t0) : 0, t1 = range ? Math.min(plan.duration, range.t1) : plan.duration;
    return { t0, dur: Math.max(1 / plan.fps, t1 - t0) };
  };
  class BlockStore {
    // positioned writes into a list of blocks → Blob (no single giant buffer)
    constructor() {
      this.blocks = [];
      this.end = 0;
    }
    write(data, pos) {
      let u = data instanceof Uint8Array ? data : new Uint8Array(data);
      if (pos > this.end) {
        this.blocks.push({ pos: this.end, u: new Uint8Array(pos - this.end) });
        this.end = pos;
      }
      for (const b of this.blocks) {
        if (pos >= this.end || !u.length) break;
        const s0 = Math.max(pos, b.pos), s1 = Math.min(pos + u.length, b.pos + b.u.length);
        if (s1 > s0) b.u.set(u.subarray(s0 - pos, s1 - pos), s0 - b.pos);
      }
      if (pos + u.length > this.end) {
        const from = Math.max(0, this.end - pos);
        this.blocks.push({ pos: this.end, u: u.slice(from) });
        this.end = pos + u.length;
      }
    }
    blob(type) {
      return new Blob(this.blocks.map((b) => b.u), { type });
    }
  }
  J.exportMP4 = (o) => __async(null, null, function* () {
    const { plan, project, audio, quality = "high", onProgress, signal, range, file = null } = o;
    const [w, h] = J.outputSize(project);
    const fps = plan.fps, bitrate = J.videoBitrate(w, h, fps, quality);
    const attempts = yield J.videoAttempts(w, h, fps, bitrate);
    if (!attempts.length) throw new Error("\u3053\u306E\u30D6\u30E9\u30A6\u30B6\u306F\u52D5\u753B\u30A8\u30F3\u30B3\u30FC\u30C9\uFF08WebCodecs\uFF09\u306B\u5BFE\u5FDC\u3057\u3066\u3044\u307E\u305B\u3093\u3002Chrome \u304B Edge \u306E\u6700\u65B0\u7248\u3067\u958B\u3044\u3066\u304F\u3060\u3055\u3044\u3002");
    const tried = [];
    for (let k = 0; k < attempts.length; k++) {
      const vc = attempts[k];
      try {
        if (file && k > 0) {
          yield file.seek(0);
          yield file.truncate(0);
        }
        const r = yield encodeMP4(Object.assign({}, o, { w, h, vc, note: k > 0 ? `\uFF08${vc.label}\u30FB\u30BD\u30D5\u30C8\u30A6\u30A7\u30A2\u3067\u518D\u8A66\u884C ${k}\uFF09` : "" }));
        r.tried = tried;
        return r;
      } catch (e) {
        if (signal && signal.aborted) throw new Error("\u30AD\u30E3\u30F3\u30BB\u30EB\u3057\u307E\u3057\u305F");
        if (e && e.jzFatal) throw e;
        tried.push(`${vc.label}/${vc.hw}: ${e && e.message ? e.message : e}`);
        console.warn("MP4 export attempt failed", vc.codec, vc.hw, e);
      }
    }
    const err = new Error("MP4 \u3092\u66F8\u304D\u51FA\u305B\u307E\u305B\u3093\u3067\u3057\u305F\u3002" + (file ? "" : "\u300C\u5927\u304D\u306A\u52D5\u753B\u7528\uFF08\u30D5\u30A1\u30A4\u30EB\u306B\u76F4\u63A5\u4FDD\u5B58\uFF09\u300D\u304B\u3001") + "\u89E3\u50CF\u5EA6\u30FBfps\u30FB\u753B\u8CEA\u3092\u4E0B\u3052\u3066\u8A66\u3057\u3066\u304F\u3060\u3055\u3044\u3002\u8A73\u7D30\uFF1A" + tried.join(" \uFF0F "));
    err.detail = tried;
    throw err;
  });
  function encodeMP4(_0) {
    return __async(this, arguments, function* ({ plan, project, audio, onProgress, signal, range, file, w, h, vc, note }) {
      const span = J.exportSpan(plan, range);
      const fps = plan.fps;
      let ac = null;
      if (audio && audio.buffer && project.includeAudio !== false) ac = yield J.pickAudioCodec(48e3, Math.min(2, audio.buffer.numberOfChannels));
      const store = file ? null : new BlockStore();
      const target = file ? new Mp4Muxer.FileSystemWritableFileStreamTarget(file, { chunkSize: 8 * 1048576 }) : new Mp4Muxer.StreamTarget({ onData: (data, pos) => store.write(data, pos), chunked: true, chunkSize: 8 * 1048576 });
      const muxOpts = { target, video: { codec: vc.mux, width: w, height: h, frameRate: fps }, fastStart: false, firstTimestampBehavior: "offset" };
      if (ac) muxOpts.audio = { codec: ac.mux, numberOfChannels: Math.min(2, audio.buffer.numberOfChannels), sampleRate: ac.sr };
      const muxer = new Mp4Muxer.Muxer(muxOpts);
      let err = null, outFrames = 0;
      const venc = new VideoEncoder({ output: (chunk, meta) => {
        outFrames++;
        try {
          muxer.addVideoChunk(chunk, meta);
        } catch (e) {
          err = e;
        }
      }, error: (e) => {
        err = e;
      } });
      venc.configure(Object.assign({}, vc.cfg, { latencyMode: "quality" }));
      const canvas = document.createElement("canvas");
      canvas.width = w;
      canvas.height = h;
      const ctx = canvas.getContext("2d", { alpha: false });
      const R = new J.Renderer();
      const total = Math.max(1, Math.round(span.dur * fps));
      const scale = w / plan.W;
      const prevRes = J.glyphs.maxRes;
      J.glyphs.maxRes = h >= 1e3 ? 768 : 512;
      const closeEnc = () => {
        try {
          if (venc.state !== "closed") venc.close();
        } catch (e) {
        }
      };
      try {
        for (let i = 0; i < total; i++) {
          if (signal && signal.aborted) {
            closeEnc();
            throw new Error("\u30AD\u30E3\u30F3\u30BB\u30EB\u3057\u307E\u3057\u305F");
          }
          if (err) throw err;
          if (venc.state === "closed") throw new Error("\u30A8\u30F3\u30B3\u30FC\u30C0\u30FC\u304C\u505C\u6B62\u3057\u307E\u3057\u305F");
          R.frame(ctx, plan, span.t0 + i / fps, { scale });
          const vf = new VideoFrame(canvas, { timestamp: Math.round(i * 1e6 / fps), duration: Math.round(1e6 / fps) });
          try {
            venc.encode(vf, { keyFrame: i % (fps * 2) === 0 });
          } finally {
            vf.close();
          }
          let spins = 0;
          while (venc.encodeQueueSize > 4 && !err) {
            yield new Promise((r) => setTimeout(r, 2));
            if (!document.hidden && ++spins > 15e3) throw new Error("\u30A8\u30F3\u30B3\u30FC\u30C0\u30FC\u304C\u5FDC\u7B54\u3057\u307E\u305B\u3093");
          }
          if (i === Math.min(total - 1, fps * 3) && outFrames === 0) {
            yield venc.flush();
            if (!outFrames) throw new Error("\u30A8\u30F3\u30B3\u30FC\u30C0\u30FC\u304C\u51FA\u529B\u3092\u8FD4\u3057\u307E\u305B\u3093");
          }
          if (i % 3 === 0) {
            onProgress && onProgress(i / total, `\u30D5\u30EC\u30FC\u30E0 ${i + 1}/${total}${note || ""}`);
            yield new Promise((r) => setTimeout(r, 0));
          }
        }
        yield venc.flush();
        if (err) throw err;
      } catch (e) {
        closeEnc();
        throw e;
      } finally {
        J.glyphs.maxRes = prevRes;
      }
      closeEnc();
      if (outFrames < total * 0.98) throw new Error(`\u52D5\u753B\u306E\u30D5\u30EC\u30FC\u30E0\u304C\u8DB3\u308A\u307E\u305B\u3093\uFF08${outFrames}/${total}\uFF09`);
      if (ac) {
        onProgress && onProgress(0.99, "\u97F3\u58F0\u3092\u30A8\u30F3\u30B3\u30FC\u30C9\u4E2D");
        const rs = yield resample(audio.buffer, ac.sr, span.dur, span.t0);
        const chn = rs.numberOfChannels;
        let aChunks = 0, aEnd = 0, aErr = null;
        const aenc = new AudioEncoder({ output: (chunk, meta) => {
          aChunks++;
          aEnd = Math.max(aEnd, chunk.timestamp + (chunk.duration || 0));
          muxer.addAudioChunk(chunk, meta);
        }, error: (e) => {
          aErr = e;
        } });
        aenc.configure({ codec: ac.codec, sampleRate: ac.sr, numberOfChannels: chn, bitrate: 192e3 });
        const frames = rs.length, block = 4800;
        for (let off = 0; off < frames; off += block) {
          if (aErr) break;
          const n = Math.min(block, frames - off);
          const data = new Float32Array(n * chn);
          for (let c = 0; c < chn; c++) data.set(rs.getChannelData(c).subarray(off, off + n), c * n);
          const ad = new AudioData({ format: "f32-planar", sampleRate: ac.sr, numberOfFrames: n, numberOfChannels: chn, timestamp: Math.round(off * 1e6 / ac.sr), data });
          aenc.encode(ad);
          ad.close();
          if (aenc.encodeQueueSize > 16) yield new Promise((r) => setTimeout(r, 1));
        }
        yield aenc.flush();
        aenc.close();
        const fatal = (m) => {
          const e = new Error(m);
          e.jzFatal = true;
          return e;
        };
        if (aErr) throw fatal("\u97F3\u58F0\u306E\u30A8\u30F3\u30B3\u30FC\u30C9\u306B\u5931\u6557\u3057\u307E\u3057\u305F: " + (aErr.message || aErr));
        if (!aChunks || aEnd < (Math.min(span.dur, audio.buffer.duration - span.t0) - 0.5) * 1e6) throw fatal("\u97F3\u58F0\u306E\u30A8\u30F3\u30B3\u30FC\u30C9\u304C\u9014\u4E2D\u3067\u6B62\u307E\u308A\u307E\u3057\u305F\uFF08" + aChunks + "\uFF09\u3002\u3082\u3046\u4E00\u5EA6\u66F8\u304D\u51FA\u3057\u3066\u304F\u3060\u3055\u3044");
      }
      onProgress && onProgress(0.995, "\u30D5\u30A1\u30A4\u30EB\u3092\u4ED5\u4E0A\u3052\u4E2D");
      muxer.finalize();
      if (file) yield file.close();
      onProgress && onProgress(1, "\u5B8C\u4E86");
      const size = file ? null : store.end;
      return { blob: file ? null : store.blob("video/mp4"), size, codec: vc.label + (vc.hw === "prefer-software" ? "\uFF08\u30BD\u30D5\u30C8\u30A6\u30A7\u30A2\uFF09" : ""), audio: ac ? ac.mux : null, audioWanted: !!(audio && audio.buffer && project.includeAudio !== false), width: w, height: h, toFile: !!file };
    });
  }
  const CRC = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 3988292384 ^ c >>> 1 : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  const crc32 = (u8) => {
    let c = 4294967295;
    for (let i = 0; i < u8.length; i++) c = CRC[(c ^ u8[i]) & 255] ^ c >>> 8;
    return (c ^ 4294967295) >>> 0;
  };
  class ZipWriter {
    constructor() {
      this.parts = [];
      this.central = [];
      this.offset = 0;
    }
    add(name, u8) {
      const nb = new TextEncoder().encode(name), crc = crc32(u8);
      const lh = new DataView(new ArrayBuffer(30));
      lh.setUint32(0, 67324752, true);
      lh.setUint16(4, 20, true);
      lh.setUint16(6, 2048, true);
      lh.setUint16(8, 0, true);
      lh.setUint16(10, 0, true);
      lh.setUint16(12, 33, true);
      lh.setUint32(14, crc, true);
      lh.setUint32(18, u8.length, true);
      lh.setUint32(22, u8.length, true);
      lh.setUint16(26, nb.length, true);
      lh.setUint16(28, 0, true);
      this.parts.push(lh.buffer, nb, u8);
      const ch = new DataView(new ArrayBuffer(46));
      ch.setUint32(0, 33639248, true);
      ch.setUint16(4, 20, true);
      ch.setUint16(6, 20, true);
      ch.setUint16(8, 2048, true);
      ch.setUint16(10, 0, true);
      ch.setUint16(12, 0, true);
      ch.setUint16(14, 33, true);
      ch.setUint32(16, crc, true);
      ch.setUint32(20, u8.length, true);
      ch.setUint32(24, u8.length, true);
      ch.setUint16(28, nb.length, true);
      ch.setUint32(42, this.offset, true);
      this.central.push(ch.buffer, nb);
      this.offset += 30 + nb.length + u8.length;
    }
    finish() {
      const cdSize = this.central.reduce((s, p) => {
        var _a;
        return s + ((_a = p.byteLength) != null ? _a : p.length);
      }, 0);
      const n = this.central.length / 2;
      const end = new DataView(new ArrayBuffer(22));
      end.setUint32(0, 101010256, true);
      end.setUint16(8, n, true);
      end.setUint16(10, n, true);
      end.setUint32(12, cdSize, true);
      end.setUint32(16, this.offset, true);
      return new Blob([...this.parts, ...this.central, end.buffer], { type: "application/zip" });
    }
  }
  J.exportPNGZip = (_0) => __async(null, [_0], function* ({ plan, project, transparent, layers, onProgress, signal, every = 1, range }) {
    const span = J.exportSpan(plan, range);
    const [w, h] = J.outputSize(project);
    const canvas = document.createElement("canvas");
    canvas.width = w;
    canvas.height = h;
    const ctx = canvas.getContext("2d");
    const R = new J.Renderer();
    const fps = plan.fps, total = Math.max(1, Math.round(span.dur * fps));
    const zip = new ZipWriter();
    const scale = w / plan.W;
    for (let i = 0; i < total; i += every) {
      if (signal && signal.aborted) throw new Error("\u30AD\u30E3\u30F3\u30BB\u30EB\u3057\u307E\u3057\u305F");
      const name = `jizura_${String(i).padStart(5, "0")}.png`;
      for (const layer of layers ? ["back", "front"] : [null]) {
        R.frame(ctx, plan, span.t0 + i / fps, { scale, transparent: transparent || !!layers, layer });
        const blob = yield new Promise((r) => canvas.toBlob(r, "image/png"));
        zip.add((layer ? layer + "/" : "") + name, new Uint8Array(yield blob.arrayBuffer()));
      }
      onProgress && onProgress(i / total, `PNG ${i + 1}/${total}`);
    }
    onProgress && onProgress(1, "\u5B8C\u4E86");
    return zip.finish();
  });
  J.AE_MAP = {
    layout: {
      lowerThird: "center",
      corners: "mixed",
      staircase: "mixed",
      zigzag: "wave",
      arcTop: "ring",
      spiral: "ring",
      gridCells: "labels",
      dropCap: "mixed",
      justified: "tile",
      frameBox: "center",
      bubble: "pill",
      subtitleBar: "center",
      ticker: "marquee",
      splitScreen: "diag",
      mirror: "stack",
      sideways: "vcols",
      edgeFrame: "marquee",
      perspective: "stack",
      hanko: "vcols",
      genkou: "vcols",
      panels: "diag",
      filmstrip: "labels",
      quote: "center",
      ruler: "gloss",
      searchBar: "type",
      chat: "labels",
      notification: "pill",
      ticket: "pill",
      rain: "tile",
      hanging: "scatter",
      orbit: "ring",
      tunnel: "tile",
      wordCloud: "scatter",
      bounceLine: "mixed",
      elastic: "condensed",
      crossBands: "diag",
      stickerBomb: "labels",
      neon: "center",
      keycaps: "labels",
      bubbles: "scatter",
      slotMachine: "labels",
      flipBoard: "labels",
      credits: "type",
      zoomRepeat: "stack",
      splitHalves: "stack",
      columnsBig: "vcols",
      circleWords: "ring",
      dotMatrix: "type",
      depthStack: "stack",
      typeSpecimen: "stack",
      kanjiFocus: "huge",
      halfVertical: "vcols",
      curtain: "center",
      equalizer: "mixed",
      tape: "diag"
    },
    enter: { riseMask: "drop", dropMask: "drop", slideL: "wipe", slideR: "wipe", slideWhole: "stretch", flipX: "spin", flipY: "spin", domino: "spin", fold: "pop", unroll: "wipe", strokeDraw: "assemble", outlineFill: "blur", splitJoin: "slice", vSlice: "slice", shutter: "wipe", iris: "zoom", diagWipe: "wipe", blinds: "slice", checker: "flicker", randomOrder: "flicker", bounceBig: "drop", squashDrop: "drop", rubber: "stretch", glitchIn: "scramble", echoIn: "zoom", whip: "stretch", skewIn: "stretch", trackIn: "blur", trackOut: "blur", blurStagger: "blur", fadeStagger: "blur", waveIn: "pop", spiralIn: "spin", zoomOut: "zoom", resolve: "scramble", magnet: "assemble", inkBleed: "blur", neonOn: "flicker", cursorSweep: "type", stamp: "zoom" },
    exit: { sinkMask: "fall", riseOut: "drift", slideOutL: "stretch", slideOutR: "stretch", flipOutX: "shrink", flipOutY: "fall", foldOut: "shrink", squash: "shrink", trackOutWide: "blur", collapse: "shrink", zoomThrough: "blur", zoomFar: "shrink", spinOut: "scatter", twist: "shrink", waveOut: "scatter", blurOutStagger: "blur", undraw: "blur", outlineOut: "blur", irisClose: "shrink", diagWipeOut: "wipe", blindsClose: "slice", checkerOut: "glitch", splitApart: "slice", vSliceDrop: "fall", melt: "fall", dissolve: "drift", backspace: "wipe", scrambleOut: "glitch", glitchDissolve: "glitch", echoOut: "blur", whipOut: "stretch", gravity: "fall", popOut: "scatter", burn: "drift", sweepCover: "wipe", shatterLite: "explode" },
    hold: { float: "drift", sway: "wave", pulse: "breathe", shimmer: "still", colorRun: "still", rotateSlow: "drift", trackBreathe: "breathe", skewWobble: "wave", beatHop: "wave", hWave: "wave", heartbeat: "breathe", orbitSmall: "jitter", jelly: "breathe", scanBand: "glitchtick", noiseDrift: "drift", tilt: "drift", zoomSlow: "drift", stretchPulse: "breathe", glitchJump: "glitchtick", echoTrail: "drift" },
    decor: { crosshair: "brackets", cropMarks: "brackets", reticle: "rings", radar: "rings", progressRing: "rings", timecodeBar: "barcode", rulerEdge: "grid", dimension: "leaders", indexNum: "counter", dateStamp: "barcode", qrBlock: "barcode", glitchRects: "bars", concentricSquares: "shapes", triangleSpin: "shapes", lineBurst: "sparks", plusGrid: "grid", guides: "grid", waveLine: "waveform", spiralLine: "rings", halftonePatch: "shapes", checkerStrip: "stripes", beatRing: "rings", orbitDots: "dots", constellation: "sparks", confetti: "shapes", petals: "shapes", rainStreaks: "slash", snow: "dots", lightLeak: "blobs", bokeh: "blobs", speedCorner: "slash", risingParticles: "sparks", twinkle: "sparks", brushStroke: "bars", tapePieces: "bars", scribbleCircle: "rings", scribbleUnder: "slash", crossOut: "slash", highlightMark: "bars", heartsStars: "shapes", watermarkKanji: "counter", verticalStrip: "leaders", romajiLine: "leaders", bracketsJP: "brackets", seal: "shapes" },
    fx: { rgbSplit: "chroma", smear: "slice", vhsRoll: "slice", trackingNoise: "slice", waveWarp: "slice", pixelDrift: "slice", tileShift: "block", gridRepeat: "block", mirrorFlash: "block", strobe: "invert", blackFrame: "invert", whiteFrame: "flash", filmBurn: "flash", lightSweep: "flash", panelWipe: "flash", zoomPunch: "zoom", whipBlur: "zoom", posterize: "mosaic", hueShift: "chroma", irisTrans: "zoom", doors: "slice", blindsTrans: "slice", splitSlide: "slice", crtOff: "flash" }
  };
  J.planForAE = (plan, project, range) => {
    const clean = JSON.parse(JSON.stringify(plan, (k, v) => k === "energy" || k === "buffer" || k === "peaks" ? void 0 : v));
    clean.version = 2;
    clean.width = J.outputSize(project)[0];
    clean.height = J.outputSize(project)[1];
    clean.extra = project.extra === true;
    clean.wa = project.wa !== false;
    if (J.setOn) for (const s of J.SET_ORDER) clean[s] = J.setOn(project, s);
    clean.fonts = {};
    for (const [role, keys] of Object.entries(plan.style.fonts)) clean.fonts[role] = keys.map((k) => J.FONTS[k] ? J.FONTS[k].label : k);
    clean.fontTable = Object.fromEntries(Object.entries(J.FONTS).map(([k, f]) => [k, { label: f.label, family: f.family.replace(/"/g, ""), weight: f.weight, kind: f.kind }]));
    clean.lang = plan.lang || "ja";
    if (J.setLang && J.faceOf && clean.lang !== "ja") {
      J.setLang(clean.lang);
      for (const k of Object.keys(clean.fontTable)) {
        const f = J.faceOf(k);
        clean.fontTable[k].langFamily = f.family.replace(/"/g, "");
        clean.fontTable[k].langWeight = f.weight;
      }
    }
    for (const c of clean.cuts || []) if (c.morph && !c.trans) {
      c.trans = "inkBlob";
      c.transDur = c.morph.dur;
      c.transP = {};
      c.webMorph = true;
    }
    if (range) {
      const sp = J.exportSpan(plan, range), t0 = sp.t0, t1 = t0 + sp.dur, eps = 1e-3;
      const sh = (o) => {
        o.start -= t0;
        o.end -= t0;
        return o;
      };
      clean.cuts = clean.cuts.filter((c) => c.end > t0 + eps && c.start < t1 - eps).map((c) => {
        if (c.companion) sh(c.companion);
        return sh(c);
      });
      clean.events = (clean.events || []).filter((e) => e.t >= t0 - 1 && e.t < t1).map((e) => Object.assign(e, { t: e.t - t0 }));
      for (const l of clean.lines || []) {
        sh(l);
        if (l.visEnd != null) l.visEnd -= t0;
      }
      clean.duration = sp.dur;
      clean.audioOffset = t0;
      clean.range = { t0, t1 };
    }
    return clean;
  };
})();
