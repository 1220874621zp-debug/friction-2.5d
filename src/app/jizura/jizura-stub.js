// Load-time shims so the JIZURA planner sources (browser code, MIT,
// (c) 852wa - see LICENSE in this folder) evaluate inside a bare
// QJSEngine. Only the planning path is exercised; render / DOM / audio
// surfaces degrade to no-ops. Must be evaluated BEFORE any jizura src.
(function (g) {
    if (typeof globalThis !== 'undefined') { g = globalThis; }
    else if (typeof window !== 'undefined') { g = window; }
    if (typeof g.window === 'undefined') { g.window = g; }

    function ctxStub(cv) {
        var noop = function () {};
        return {
            canvas: cv,
            measureText: function (s) {
                var n = String(s == null ? '' : s).length;
                return { width: n * 16 + 8 };
            },
            createLinearGradient: function () { return { addColorStop: noop }; },
            createRadialGradient: function () { return { addColorStop: noop }; },
            createConicGradient: function () { return { addColorStop: noop }; },
            createPattern: function () { return null; },
            getImageData: function (x, y, w, h) {
                var ww = Math.max(1, w | 0), hh = Math.max(1, h | 0);
                return { width: ww, height: hh, data: new Uint8ClampedArray(ww * hh * 4) };
            },
            putImageData: noop,
            isPointInPath: function () { return false; },
            clearRect: noop, fillRect: noop, strokeRect: noop,
            beginPath: noop, closePath: noop, moveTo: noop, lineTo: noop,
            arc: noop, ellipse: noop, rect: noop, roundRect: noop,
            bezierCurveTo: noop, quadraticCurveTo: noop,
            clip: noop, fill: noop, stroke: noop,
            drawImage: noop, drawBitmap: noop,
            save: noop, restore: noop, translate: noop, rotate: noop,
            scale: noop, transform: noop, setTransform: noop, resetTransform: noop,
            fillText: noop, strokeText: noop, setLineDash: noop,
            drawFocusIfNeeded: noop
        };
    }

    function canvasStub() {
        return {
            width: 300, height: 150, style: {},
            getContext: function () { return ctxStub(this); },
            addEventListener: function () {}, removeEventListener: function () {},
            getBoundingClientRect: function () {
                return { left: 0, top: 0, width: this.width, height: this.height };
            },
            toDataURL: function () { return ''; },
            captureStream: function () { return null; },
            transferControlToOffscreen: function () { return null; }
        };
    }

    if (typeof g.document === 'undefined') {
        g.document = {
            createElement: function (tag) {
                return String(tag).toLowerCase() === 'canvas'
                        ? canvasStub()
                        : { style: {}, appendChild: function () {}, setAttribute: function () {}, remove: function () {} };
            },
            createElementNS: function (ns, tag) { return g.document.createElement(tag); },
            createTextNode: function (t) { return { text: t }; },
            head: { appendChild: function () {} },
            body: { appendChild: function () {}, removeChild: function () {} },
            addEventListener: function () {}, removeEventListener: function () {},
            fonts: { load: function () { return Promise.resolve([]); }, check: function () { return true; }, ready: Promise.resolve(true) },
            documentElement: { style: { setProperty: function () {} } }
        };
    }
    if (typeof g.navigator === 'undefined') {
        g.navigator = { userAgent: 'friction-lyricmotion', language: 'ja', languages: ['ja'] };
    }
    if (typeof g.location === 'undefined') {
        g.location = { href: '', protocol: 'file:' };
    }
    if (typeof g.localStorage === 'undefined') {
        g.localStorage = { getItem: function () { return null; }, setItem: function () {}, removeItem: function () {} };
    }
    if (typeof g.requestAnimationFrame === 'undefined') {
        g.requestAnimationFrame = function () { return 0; };
    }
    if (typeof g.matchMedia === 'undefined') {
        g.matchMedia = function () { return { matches: false, addEventListener: function () {}, addListener: function () {} }; };
    }
    if (typeof g.Image === 'undefined') {
        g.Image = function () { this.addEventListener = function () {}; };
    }

    // ---- runtime builtin polyfills (this QJSEngine's builtin set is
    // missing several ES2019+ helpers the planner sources touch) ----
    g.globalThis = g;
    if (typeof g.Object.fromEntries !== 'function') {
        g.Object.fromEntries = function (entries) {
            var out = {};
            if (!entries) { return out; }
            var it = typeof entries[Symbol.iterator] === 'function'
                    ? entries[Symbol.iterator]() : null;
            var step;
            while (it && !(step = it.next()).done) {
                var e = step.value;
                if (e && e.length) { out[e[0]] = e[1]; }
            }
            return out;
        };
    }
    if (typeof g.Object.hasOwn !== 'function') {
        g.Object.hasOwn = function (o, k) { return Object.prototype.hasOwnProperty.call(o, k); };
    }
    function atPoly(n) {
        n = Math.trunc(n) || 0;
        if (n < 0) { n += this.length; }
        if (n < 0 || n >= this.length) { return undefined; }
        return this[n];
    }
    if (typeof g.String.prototype.at !== 'function') {
        g.String.prototype.at = atPoly;
    }
    if (typeof g.String.prototype.replaceAll !== 'function') {
        g.String.prototype.replaceAll = function (find, rep) {
            var s = String(this);
            if (find instanceof RegExp) {
                if (!find.global) {
                    throw new TypeError('replaceAll requires a global regex');
                }
                return s.replace(find, rep);
            }
            return s.split(String(find)).join(String(rep));
        };
    }
    if (typeof g.String.prototype.trimStart !== 'function') {
        g.String.prototype.trimStart = function () { return this.replace(/^\s+/, ''); };
        g.String.prototype.trimEnd = function () { return this.replace(/\s+$/, ''); };
    }
    if (typeof g.Array.prototype.at !== 'function') {
        g.Array.prototype.at = atPoly;
    }
    if (typeof g.Array.prototype.flat !== 'function') {
        g.Array.prototype.flat = function (depth) {
            var d = depth === undefined ? 1 : Math.max(0, Math.trunc(depth));
            var out = [];
            var push = function (v, lvl) {
                if (Array.isArray(v) && lvl > 0) {
                    for (var i = 0; i < v.length; i++) { push(v[i], lvl - 1); }
                } else { out.push(v); }
            };
            push(this, d);
            return out;
        };
    }
    if (typeof g.Array.prototype.flatMap !== 'function') {
        g.Array.prototype.flatMap = function (fn, that) {
            var out = [];
            for (var i = 0; i < this.length; i++) {
                var r = fn.call(that, this[i], i, this);
                if (Array.isArray(r)) { for (var j = 0; j < r.length; j++) { out.push(r[j]); } }
                else { out.push(r); }
            }
            return out;
        };
    }
    if (typeof g.Array.prototype.findLast !== 'function') {
        g.Array.prototype.findLast = function (fn, that) {
            for (var i = this.length - 1; i >= 0; i--) {
                if (fn.call(that, this[i], i, this)) { return this[i]; }
            }
            return undefined;
        };
    }
    if (typeof g.TextEncoder === 'undefined') {
        g.TextEncoder = function () {};
        g.TextEncoder.prototype.encode = function (str) {
            var s = String(str);
            var utf8 = [];
            for (var i = 0; i < s.length; i++) {
                var c = s.charCodeAt(i);
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.length) {
                    var c2 = s.charCodeAt(i + 1);
                    if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                        c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                        i++;
                    }
                }
                if (c < 0x80) { utf8.push(c); }
                else if (c < 0x800) {
                    utf8.push(0xC0 | (c >> 6), 0x80 | (c & 0x3F));
                } else if (c < 0x10000) {
                    utf8.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F));
                } else {
                    utf8.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 0x3F),
                              0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F));
                }
            }
            var out = new Uint8Array(utf8.length);
            for (var j = 0; j < utf8.length; j++) { out[j] = utf8[j]; }
            return out;
        };
    }
    if (typeof g.structuredClone === 'undefined') {
        g.structuredClone = function (v) { return JSON.parse(JSON.stringify(v)); };
    }
})(typeof globalThis !== 'undefined' ? globalThis : (typeof window !== 'undefined' ? window : this));
