// Auto Arrange Layers - port of the AE 自动排列图层.jsx panel.
// Arranges selected layers into a grid of rows (horizontal) or
// columns (vertical): scales each layer to fit, places it in its
// cell, optionally duplicates to tile beyond the scene edge, and
// optionally binds rows/columns to null controllers.
//
// VERIFIED engine transform model (src/core/Animators/transformanimator.cpp,
// valuesToMatrix):   rel(p) = pivot + pos + R*S*shear*(p - pivot)
//   - worldPosition() == pos + pivot. Scaling happens AROUND the pivot
//     and NEVER moves it. (The previous "pos + pivot*scale" model was
//     wrong and caused all the placement drift.)
//   - property("scale"/"position").setValue() sets values ABSOLUTELY.
//   - Once the pivot sits on the content center, the content center's
//     world position is simply pos + pivot, for ANY scale.
//
// Engine quirks this script deliberately works around:
//   - worldBounds()/worldPosition()/mapAbsPosToRel() read CACHED
//     matrices that only refresh on frame changes or box insertion;
//     mid-script they can return stale data. Therefore this script
//     relies ONLY on animator values (position/scale/anchorPoint) and
//     bounds() (the local source rect, unaffected by transforms).
//   - setParentLayer()'s built-in compensation misbehaves for scaled
//     children (a child-local delta is applied as a parent-space
//     position increment). We parent while the null is still at
//     identity and then overwrite the position with the exact value,
//     which also neutralizes that compensation.

(function () {
    var SCRIPT_NAME = "自动排列图层";

    var settings = {
        rows: 3, gap: 2, maxLimit: 1080,
        startX: 0, startY: 0,
        direction: 0, bindMode: 1, preset: 0, autoFill: true
    };
    var PRESET_RATIOS = [0, 9/16, 3/4, 16/9, 1, 4/3, 0];
    var debugLog = [];
    function log(msg) { debugLog.push(msg); print(msg); if (debugLog.length > 300) debugLog.shift(); }

    function requireSelection() {
        var scene = app.activeScene;
        if (!scene) { alert("请先打开一个场景"); return null; }
        var sel = scene.selectedLayers();
        if (!sel || sel.length === 0) { alert("请先在时间轴中选择要排列的图层"); return null; }
        return { scene: scene, sel: sel };
    }

    function computeMaxLimit(scene) {
        var p = settings.preset;
        if (p === 6) return settings.maxLimit;
        if (p === 0) return settings.direction === 0 ? scene.height : scene.width;
        return Math.round(scene.width * PRESET_RATIOS[p]);
    }

    // ---- geometry (pure animator values, no cached matrices) ----------

    // Center the layer pivot on its content (engine keeps the visual
    // position), then scale so the content height (or width) equals the
    // target. Returns the scaled cell size, or null when unusable.
    function prepareFit(layer, target, byWidth) {
        var b = layer.bounds();
        if (!b || b.width <= 0 || b.height <= 0) return null;
        // 轴心居中于图像
        layer.setAnchorPoint([b.left + b.width / 2, b.top + b.height / 2]);
        var rotProp = layer.property("rotation");
        var rot = rotProp ? rotProp.value : 0;
        if (rot) log("提示 " + layer.name + " 已旋转 " + rot + "°, 按未旋转尺寸排列");
        var sc = layer.property("scale").value;
        if (!sc) return null;
        var curSx = Math.abs(sc[0]) < 1e-6 ? 1 : sc[0];
        var curSy = Math.abs(sc[1]) < 1e-6 ? 1 : sc[1];
        var ref = byWidth ? b.width * Math.abs(curSx) : b.height * Math.abs(curSy);
        if (ref <= 0) return null;
        var factor = target / ref;
        layer.property("scale").setValue([curSx * factor, curSy * factor]);
        return {
            w: Math.abs(b.width * curSx * factor),
            h: Math.abs(b.height * curSy * factor)
        };
    }

    // Place the content center at (tx, ty). Exact for any scale once
    // the pivot sits on the content center: worldCenter == pos + pivot.
    function placeCenter(layer, tx, ty) {
        var piv = layer.anchorPoint();
        if (!piv) return;
        layer.property("position").setValue([tx - piv[0], ty - piv[1]]);
    }

    // ---- binding ---------------------------------------------------------

    // Parent every layer of each group to a fresh null whose pivot ends
    // on the group center. Children keep their exact world positions.
    function bindGroups(groups, prefix, scene) {
        for (var g = 0; g < groups.length; g++) {
            var grp = groups[g];
            if (!grp || grp.length === 0) continue;
            var nullLayer = scene.addLayer(prefix + (g + 1));
            if (!nullLayer) { log("创建空对象失败: " + prefix + (g + 1)); continue; }
            var np = nullLayer.anchorPoint();
            var npx = np ? np[0] : 0, npy = np ? np[1] : 0;
            // group center in world coords (pivot == content center)
            var cx = 0, cy = 0;
            for (var i = 0; i < grp.length; i++) {
                var p = grp[i].property("position").value;
                var a = grp[i].anchorPoint();
                cx += p[0] + a[0];
                cy += p[1] + a[1];
            }
            cx /= grp.length; cy /= grp.length;
            // null position so its pivot lands on the group center
            var nx = cx - npx, ny = cy - npy;
            for (var j = 0; j < grp.length; j++) {
                var layer = grp[j];
                var pos = layer.property("position").value;
                if (!layer.setParentLayer(nullLayer))
                    log("无法将 " + layer.name + " 绑定到 " + nullLayer.name);
                // exact local position for the final null placement
                // (also overwrites setParentLayer's compensation)
                layer.property("position").setValue([pos[0] - nx, pos[1] - ny]);
            }
            nullLayer.property("position").setValue([nx, ny]);
            log("已创建 " + nullLayer.name + " 并绑定 " + grp.length + " 个图层");
        }
    }

    // ---- horizontal: rows=N, height=maxH, fill right -----------------

    function arrangeHorizontal(scene, items, N, gap, maxH, startX, startY, autoFill, bindMode) {
        var M = items.length;
        var visualRows = N;
        var visualCols = Math.ceil(M / visualRows);
        var rowGroups = [];
        for (var r = 0; r < visualRows; r++) rowGroups.push([]);
        for (var i = 0; i < M; i++) {
            var rowIdx = Math.floor(i / visualCols);
            if (rowIdx >= visualRows) rowIdx = visualRows - 1;
            rowGroups[rowIdx].push(i);
        }
        var targetH = (maxH + gap) / visualRows - gap;
        if (targetH <= 0) targetH = 1;

        var infos = [];
        for (var i0 = 0; i0 < M; i0++) {
            var info = prepareFit(items[i0].layer, targetH, false);
            infos.push(info);
            if (info) log("图层 " + items[i0].layer.name + " 缩放至 " +
                          info.w.toFixed(1) + "x" + info.h.toFixed(1));
        }

        var rowCopies = [];
        for (var r3 = 0; r3 < visualRows; r3++) rowCopies.push([]);

        for (var r2 = 0; r2 < visualRows; r2++) {
            var cy = startY + r2 * (targetH + gap) + targetH / 2;
            var cx = startX;
            for (var c = 0; c < rowGroups[r2].length; c++) {
                var idx = rowGroups[r2][c];
                var info2 = infos[idx];
                if (!info2) continue;
                placeCenter(items[idx].layer, cx + info2.w / 2, cy);
                cx += info2.w + gap;
            }
            if (autoFill && rowGroups[r2].length > 0) {
                var guard = 0;
                while (cx < scene.width && guard < 2000) {
                    guard++;
                    for (var c2 = 0; c2 < rowGroups[r2].length; c2++) {
                        var oi = rowGroups[r2][c2];
                        var inf = infos[oi];
                        if (!inf) continue;
                        var nl = items[oi].layer.duplicate();
                        if (!nl) { log("复制失败"); cx = scene.width; break; }
                        placeCenter(nl, cx + inf.w / 2, cy);
                        cx += inf.w + gap;
                        rowCopies[r2].push(nl);
                    }
                }
                log("第 " + (r2 + 1) + " 行铺满: 共 " +
                    (rowGroups[r2].length + rowCopies[r2].length) + " 个图层");
            }
        }

        if (bindMode === 1) {
            var groups = [];
            for (var r5 = 0; r5 < visualRows; r5++) {
                var grpR = [];
                for (var k = 0; k < rowGroups[r5].length; k++) {
                    var it = items[rowGroups[r5][k]];
                    if (it) grpR.push(it.layer);
                }
                groups.push(grpR.concat(rowCopies[r5]));
            }
            bindGroups(groups, "NULL_Row_", scene);
        } else if (bindMode === 2) {
            bindHorizontalByCol(visualRows, visualCols, rowGroups, items, rowCopies, scene);
        }
    }

    // ---- vertical: cols=N, width=maxW, fill down ---------------------

    function arrangeVertical(scene, items, N, gap, maxW, startX, startY, autoFill, bindMode) {
        var M = items.length;
        var visualCols = N;
        var visualRows = Math.ceil(M / visualCols);
        var colGroups = [];
        for (var c = 0; c < visualCols; c++) colGroups.push([]);
        for (var i = 0; i < M; i++) colGroups[i % visualCols].push(i);
        var targetW = (maxW + gap) / visualCols - gap;
        if (targetW <= 0) targetW = 1;

        var infos = [];
        for (var i0 = 0; i0 < M; i0++) {
            var info = prepareFit(items[i0].layer, targetW, true);
            infos.push(info);
            if (info) log("图层 " + items[i0].layer.name + " 缩放至 " +
                          info.w.toFixed(1) + "x" + info.h.toFixed(1));
        }

        var colCopies = [];
        for (var c3 = 0; c3 < visualCols; c3++) colCopies.push([]);

        for (var c2 = 0; c2 < visualCols; c2++) {
            var cx = startX + c2 * (targetW + gap) + targetW / 2;
            var cy = startY;
            for (var r = 0; r < colGroups[c2].length; r++) {
                var idx = colGroups[c2][r];
                var info2 = infos[idx];
                if (!info2) continue;
                placeCenter(items[idx].layer, cx, cy + info2.h / 2);
                cy += info2.h + gap;
            }
            if (autoFill && colGroups[c2].length > 0) {
                var guard = 0;
                while (cy < scene.height && guard < 2000) {
                    guard++;
                    for (var r2 = 0; r2 < colGroups[c2].length; r2++) {
                        var oi = colGroups[c2][r2];
                        var inf = infos[oi];
                        if (!inf) continue;
                        var nl = items[oi].layer.duplicate();
                        if (!nl) { log("复制失败"); cy = scene.height; break; }
                        placeCenter(nl, cx, cy + inf.h / 2);
                        cy += inf.h + gap;
                        colCopies[c2].push(nl);
                    }
                }
                log("第 " + (c2 + 1) + " 列铺满: 共 " +
                    (colGroups[c2].length + colCopies[c2].length) + " 个图层");
            }
        }

        if (bindMode === 1) {
            bindVerticalByRow(visualRows, visualCols, colGroups, items, colCopies, scene);
        } else if (bindMode === 2) {
            var groups = [];
            for (var c5 = 0; c5 < visualCols; c5++) {
                var grpC = [];
                for (var k = 0; k < colGroups[c5].length; k++) {
                    var it = items[colGroups[c5][k]];
                    if (it) grpC.push(it.layer);
                }
                groups.push(grpC.concat(colCopies[c5]));
            }
            bindGroups(groups, "NULL_Col_", scene);
        }
    }

    // ---- cross binding --------------------------------------------------

    function bindHorizontalByCol(vR, vC, rowGroups, items, rowCopies, scene) {
        var colOrig = [], colCp = [];
        for (var c = 0; c < vC; c++) { colOrig.push([]); colCp.push([]); }
        for (var r = 0; r < vR; r++)
            for (var c2 = 0; c2 < rowGroups[r].length; c2++)
                colOrig[c2].push(items[rowGroups[r][c2]].layer);
        for (var r2 = 0; r2 < vR; r2++) {
            var cycle = rowGroups[r2].length;
            for (var k = 0; k < rowCopies[r2].length; k++)
                if (cycle > 0) colCp[k % cycle].push(rowCopies[r2][k]);
        }
        var groups = [];
        for (var c3 = 0; c3 < vC; c3++)
            groups.push(colOrig[c3].concat(colCp[c3]));
        bindGroups(groups, "NULL_Col_", scene);
    }

    function bindVerticalByRow(vR, vC, colGroups, items, colCopies, scene) {
        var rowOrig = [], rowCp = [];
        for (var r = 0; r < vR; r++) { rowOrig.push([]); rowCp.push([]); }
        for (var c = 0; c < vC; c++)
            for (var r2 = 0; r2 < colGroups[c].length; r2++)
                rowOrig[r2].push(items[colGroups[c][r2]].layer);
        for (var c2 = 0; c2 < vC; c2++) {
            var cycle = colGroups[c2].length;
            for (var k = 0; k < colCopies[c2].length; k++)
                if (cycle > 0 && (k % cycle) < vR)
                    rowCp[k % cycle].push(colCopies[c2][k]);
        }
        var groups = [];
        for (var r3 = 0; r3 < vR; r3++)
            groups.push(rowOrig[r3].concat(rowCp[r3]));
        bindGroups(groups, "NULL_Row_", scene);
    }

    // ---- main entry -----------------------------------------------------

    function runArrange() {
        var ctx = requireSelection();
        if (!ctx) return;
        var scene = ctx.scene;
        var layers = [];
        for (var i = 0; i < ctx.sel.length; i++) layers.push(ctx.sel[i]);
        layers.sort(function (a, b) { return a.index - b.index; });

        var N = settings.rows;
        var items = [];
        for (var j = 0; j < layers.length; j++) {
            var b = layers[j].bounds();
            if (!b || b.width <= 0 || b.height <= 0) {
                log("跳过 " + layers[j].name + "：无法获取图层边界");
                continue;
            }
            items.push({ layer: layers[j] });
        }
        var M = items.length;
        if (M < N) {
            alert("可用图层数(" + M + ")少于" +
                  (settings.direction === 0 ? "行" : "列") + "数(" + N + ")");
            return;
        }
        var maxLimit = computeMaxLimit(scene);
        var hasUndo = (typeof app.beginUndoGroup === "function");
        log((settings.direction === 0 ? "横向" : "竖向") +
            " 排列 " + M + " 个图层, " +
            (settings.direction === 0 ? "行" : "列") + "数=" + N +
            ", 间隔=" + settings.gap + ", 最大=" + maxLimit +
            ", 起点=[" + settings.startX + ", " + settings.startY + "]" +
            (settings.autoFill ? ", 自动铺满" : ""));
        if (hasUndo) app.beginUndoGroup(SCRIPT_NAME);
        try {
            if (settings.direction === 0)
                arrangeHorizontal(scene, items, N, settings.gap, maxLimit,
                    settings.startX, settings.startY, settings.autoFill, settings.bindMode);
            else
                arrangeVertical(scene, items, N, settings.gap, maxLimit,
                    settings.startX, settings.startY, settings.autoFill, settings.bindMode);
            log("排列完成");
        } catch (err) {
            log("执行出错: " + err);
            alert("执行出错: " + err);
        } finally {
            if (hasUndo) app.endUndoGroup();
        }
    }

    function showHelp() {
        alert("将选中图层按行/列网格排列，可选复制铺满和空对象绑定。\n" +
              "行/列：横向=行数；竖向=列数\n" +
              "间隔：图层间距\n最大：横向=高度上限；竖向=宽度上限\n" +
              "预设：自动按场景比例计算最大值\n" +
              "X/Y：网格左上角起点\n方向：横向/竖向\n绑定：无/按行/按列\n" +
              "排列后每个图层轴心自动居中于图像中心");
    }
    function showDebugLog() {
        alert(debugLog.length > 0 ? debugLog.join("\n") : "暂无日志");
    }

    registerPanel({
        title: SCRIPT_NAME, columns: 3,
        sliders: [
            { label: "行/列", id: "rows", min: 1, max: 12, value: settings.rows, decimals: 0,
              tooltip: "横向时为行数；竖向时为列数",
              onChange: function (v) { settings.rows = Math.round(v); } },
            { label: "间隔", id: "gap", min: 0, max: 200, value: settings.gap, decimals: 0,
              tooltip: "图层之间的像素间距",
              onChange: function (v) { settings.gap = v; } },
            { label: "最大", id: "max", min: 16, max: 8192, value: settings.maxLimit, decimals: 0,
              tooltip: "横向=高度上限；竖向=宽度上限（仅预设=手动时生效）",
              onChange: function (v) { settings.maxLimit = v; } },
            { label: "X", id: "startX", min: -4096, max: 4096, value: settings.startX, decimals: 0,
              tooltip: "网格左上角 X 起点", onChange: function (v) { settings.startX = v; } },
            { label: "Y", id: "startY", min: -4096, max: 4096, value: settings.startY, decimals: 0,
              tooltip: "网格左上角 Y 起点", onChange: function (v) { settings.startY = v; } }
        ],
        combos: [
            { label: "方向", id: "dir", options: ["横向", "竖向"], index: settings.direction,
              tooltip: "横向=多行（向右铺满）；竖向=多列（向下铺满）",
              onChange: function (i) { settings.direction = i; } },
            { label: "绑定", id: "bind", options: ["无", "按行", "按列"], index: settings.bindMode,
              tooltip: "创建空对象并绑定所有相关图层",
              onChange: function (i) { settings.bindMode = i; } },
            { label: "预设", id: "preset", options: ["自动", "16:9", "4:3", "9:16", "1:1", "3:4", "手动"],
              index: settings.preset,
              tooltip: "根据场景比例自动计算最大尺寸；手动=使用最大滑块值",
              onChange: function (i) { settings.preset = i; } },
            { label: "复制", id: "fill", options: ["开", "关"], index: settings.autoFill ? 0 : 1,
              tooltip: "未铺满时自动复制图层",
              onChange: function (i) { settings.autoFill = (i === 0); } }
        ],
        extraButtons: [
            { label: "▶ 执行排列", tooltip: "按当前设置排列选中的图层", onClick: runArrange },
            { label: "? 帮助", tooltip: "功能与参数说明", onClick: showHelp }
        ]
    });
    registerCommand("自动排列图层: 执行排列", runArrange);
})();
