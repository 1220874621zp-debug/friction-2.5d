// Smooth Handles - port of the AE 平滑手柄.jsx panel.
// Smoothes the MOTION PATH of the selected layers: the position
// keyframes are treated like AE spatial keyframes, and their bezier
// tangents are rotated/stretched so the motion path flows smoothly.
//   -100% .. 0% .. +100%: rotates the handle direction from
//   along-the-path (linear-ish) to perpendicular and back
// Direction modes: auto (path normal), horizontal, vertical.
//
// AE -> Friction mapping:
//   prop.keyValue(k) spatial      -> layer.motionPath() {keys:[...]}
//   prop.setSpatialTangentsAtKey  -> layer.setMotionKeyTangents(
//                                        frame, inTan, outTan)
//   slider.onChanging live update -> sliders[].onChanging
//
// Requirements: the layer needs at least 2 position keyframes.

(function () {
    var logEntries = [];
    function log(msg) {
        logEntries.push(msg);
        print(msg);
        if (logEntries.length > 200) logEntries.shift();
    }

    // current UI state
    var state = { strength: 0, dirMode: 0 }; // dirMode 0=auto 1=X 2=Y

    function norm2(x, y) { return Math.sqrt(x * x + y * y); }

    // core: same math as the AE processSpatial function
    function smoothLayer(layer, strength, dirMode) {
        var mp = layer.motionPath();
        if (!mp || !mp.keys || mp.keys.length < 2) { return 0; }
        var keys = mp.keys;
        var n = keys.length;

        var angle = strength * Math.PI / 2;
        var cosA = Math.cos(angle);
        var sinA = Math.sin(angle);
        var applied = 0;

        // compute new tangents first (all based on the ORIGINAL
        // positions), then apply - like the AE two-pass approach
        var newIn = [];
        var newOut = [];
        for (var i = 0; i < n; i++) {
            var p = keys[i].point;
            var prev = (i > 0) ? keys[i - 1].point : null;
            var next = (i < n - 1) ? keys[i + 1].point : null;

            var fx = 0, fy = 0;
            var outDist = 0, inDist = 0;
            if (next) {
                fx += next[0] - p[0];
                fy += next[1] - p[1];
                outDist = norm2(next[0] - p[0], next[1] - p[1]);
            }
            if (prev) {
                fx += p[0] - prev[0];
                fy += p[1] - prev[1];
                inDist = norm2(p[0] - prev[0], p[1] - prev[1]);
            }
            var flowLen = norm2(fx, fy);
            if (flowLen < 0.001) { newIn.push(null); newOut.push(null); continue; }
            fx /= flowLen; fy /= flowLen;

            var nx, ny, nLen;
            if (dirMode === 1) { nx = 1; ny = 0; }
            else if (dirMode === 2) { nx = 0; ny = 1; }
            else {
                nx = -fy; ny = fx;
                nLen = norm2(nx, ny);
                if (nLen < 0.001) { nx = 1; ny = 0; }
                else { nx /= nLen; ny /= nLen; }
            }

            var hx = cosA * fx + sinA * nx;
            var hy = cosA * fy + sinA * ny;

            var outLen = outDist / 3 * (1 + Math.abs(strength));
            var inLen = inDist / 3 * (1 + Math.abs(strength));

            // open ends keep the outer handle (AE behavior)
            newIn.push(prev ? [-hx * inLen, -hy * inLen] : null);
            newOut.push(next ? [hx * outLen, hy * outLen] : null);
        }

        // apply
        for (i = 0; i < n; i++) {
            var hasIn = !!newIn[i];
            var hasOut = !!newOut[i];
            if (!hasIn && !hasOut) { continue; }
            try {
                if (layer.setMotionKeyTangents(keys[i].frame,
                                               hasIn ? newIn[i] : null,
                                               hasOut ? newOut[i] : null)) {
                    applied++;
                }
            } catch (e) {
                log("关键帧 " + keys[i].frame + " 处理异常: " + e);
            }
        }
        return applied;
    }

    function executeCurvature() {
        var scene = app.activeScene;
        if (!scene) { log("无活动场景"); return; }
        var sel = scene.selectedLayers();
        if (!sel) { sel = []; }
        if (sel.length === 0) {
            // fallback: if the scene has exactly one top layer, use it
            // (clicking a property row in the timeline does not select
            // the layer itself, which is easy to miss)
            var all = scene.layers();
            if (all && all.length === 1) {
                sel = [all[0]];
                log("未选择图层，自动使用唯一图层: " + all[0].name);
            } else {
                var names = [];
                if (all) {
                    for (var i = 0; i < all.length; i++) {
                        names.push((all[i].selected ? "[选] " : "[ ] ")
                                   + all[i].name);
                    }
                }
                log("未选择图层 (场景=" + scene.name + " 顶层图层数="
                    + (all ? all.length : 0) + ") 请点击图层名称行选中: "
                    + names.join(", "));
                return;
            }
        }

        var strength = state.strength / 100;
        var layerCount = 0;
        var keyCount = 0;
        for (var i = 0; i < sel.length; i++) {
            var c = smoothLayer(sel[i], strength, state.dirMode);
            if (c > 0) { layerCount++; keyCount += c; }
        }
        if (keyCount === 0) {
            log("图层 " + sel[0].name
                + " 没有可用的位置关键帧（需要至少 2 个位置关键帧）");
            return;
        }
        log("执行: 强度=" + Math.round(state.strength) +
            "% 方向=" + ["自动", "水平", "垂直"][state.dirMode] +
            " 图层=" + layerCount + " 关键帧=" + keyCount);
    }

    function reset() {
        state.strength = 0;
        executeCurvature();
    }

    // NOTE: onChanging is coalesced by the engine (100ms tail merge)
    // because every run pushes undo records and schedules renders -
    // raw slider-event rates would flood the undo stack and renderer
    registerPanel({
        title: "平滑手柄",
        columns: 3,
        sliders: [{
            id: "strength",
            label: "强度",
            min: -100,
            max: 100,
            value: 0,
            onChanging: function (v) {
                state.strength = v;
                executeCurvature();
            },
            onChange: function (v) {
                state.strength = v;
                executeCurvature();
            }
        }],
        combos: [{
            id: "dir",
            label: "方向",
            options: ["自动(法线)", "水平(X)", "垂直(Y)"],
            index: 0,
            onChange: function (index) {
                state.dirMode = index;
                executeCurvature();
            }
        }],
        extraButtons: [
            { label: "重置", tooltip: "回到默认平滑手柄", onClick: reset }
        ]
    });
})();
