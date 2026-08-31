// Anchor 9Grid - port of the AE AnchorPoint9Grid.jsx panel.
// Moves the layer anchor point to 9 positions of the layer content
// bounds, keeping the layer visually in place (so rotation/scaling
// afterwards pivot around the new anchor).
//
// Anchor semantics AE vs Friction:
//   AE "anchor point" = reference point in layer content coords.
//   Friction "center" (pivot) = same content-coords reference point.
//   setAnchorPoint() moves the center X/Y while compensating the
//   position, so the picture stays put - identical to what the AE
//   script does by hand (anchor + position delta).
//
// Friction JS engine notes (AE -> Friction):
//   layer.sourceRectAtTime()      -> layer.bounds()  (content coords)
//   tg.property("ADBE Anchor Point") -> layer.setAnchorPoint([x, y])
//   comp.selectedLayers           -> scene.selectedLayers()
//   parent = nullLayer            -> layer.setParentLayer(ctrl)
//     (Friction NullObject is a leaf; an AE "null as parent" maps to
//      a layer-type container created with scene.addLayer())
//   $.writeln / debug log         -> print() lands in Script Console

(function () {
    var scriptName = "Anchor 9Grid";

    var LABELS = [
        ["\u2196", "\u2191", "\u2197"],
        ["\u2190", "\u2295", "\u2192"],
        ["\u2199", "\u2193", "\u2198"]
    ];

    var TIPS = [
        ["左上角", "上中", "右上角"],
        ["左中", "居中", "右中"],
        ["左下角", "下中", "右下角"]
    ];

    var RATIOS = [
        [[0, 0], [0.5, 0], [1, 0]],
        [[0, 0.5], [0.5, 0.5], [1, 0.5]],
        [[0, 1], [0.5, 1], [1, 1]]
    ];

    var debugLog = [];

    function logDebug(msg) {
        debugLog.push(msg);
        print(msg);
        if (debugLog.length > 200) debugLog.shift();
    }

    function requireSelection() {
        var scene = app.activeScene;
        if (!scene) {
            alert("请先打开一个场景");
            return null;
        }
        var sel = scene.selectedLayers();
        if (!sel || sel.length === 0) {
            alert("请先选择图层");
            return null;
        }
        return sel;
    }

    // ---- anchor 9 grid ------------------------------------------------

    function processLayer(layer, ratio) {
        var rect = layer.bounds();
        if (!rect || rect.width <= 0 || rect.height <= 0) {
            logDebug("跳过 " + layer.name + "：无法获取图层边界");
            return;
        }

        // new anchor in layer content coordinates (AE anchor space)
        var newAX = rect.left + rect.width * ratio[0];
        var newAY = rect.top + rect.height * ratio[1];

        // moving the Friction pivot keeps the transform fixed, so no
        // manual position compensation is needed (the engine's
        // setPivotFixedTransform does what AE scripts do by hand)
        layer.setAnchorPoint([newAX, newAY]);
        logDebug(layer.name + " 锚点 -> [" +
                 newAX.toFixed(1) + ", " + newAY.toFixed(1) + "]  边界 " +
                 rect.width.toFixed(1) + "x" + rect.height.toFixed(1));
    }

    function applyAnchor(ratio) {
        var sel = requireSelection();
        if (!sel) { return; }
        for (var i = 0; i < sel.length; i++) {
            try {
                processLayer(sel[i], ratio);
            } catch (e) {
                logDebug("图层处理异常: " + e);
            }
        }
    }

    // ---- null controller ----------------------------------------------

    function createNullAndParent() {
        var sel = requireSelection();
        if (!sel) { return; }

        var scene = app.activeScene;
        // Friction's NullObject is a leaf layer (canvas crosshair
        // marker) and cannot hold children. The AE "null as parent"
        // workflow maps to a layer-type container, which owns a
        // transform and can parent any layers while keeping their
        // world position.
        var ctrl = scene.addLayer("空对象");
        if (!ctrl) {
            alert("创建空对象失败");
            return;
        }

        // 1) place the null at the selected content's center BEFORE
        //    attaching children (moving it afterwards would drag the
        //    children along); the engine's setParentLayer keeps each
        //    child's world position fixed, so the order matters
        try {
            var minX = Infinity, minY = Infinity;
            var maxX = -Infinity, maxY = -Infinity;
            for (var i = 0; i < sel.length; i++) {
                var b = sel[i].worldBounds();
                if (b.left < minX) minX = b.left;
                if (b.top < minY) minY = b.top;
                if (b.right > maxX) maxX = b.right;
                if (b.bottom > maxY) maxY = b.bottom;
            }
            ctrl.setWorldPosition([(minX + maxX) / 2, (minY + maxY) / 2]);
        } catch (e) {}

        // 2) re-parent; setParentLayer compensates the coordinate
        //    system change so every child stays exactly in place
        for (var i = 0; i < sel.length; i++) {
            if (!sel[i].setParentLayer(ctrl)) {
                logDebug("无法将 " + sel[i].name + " 挂到空对象下");
            }
        }

        logDebug("已创建空对象并链接 " + sel.length + " 个图层");
    }

    // ---- debug log dialog ----------------------------------------------

    function showDebugLog() {
        var text = debugLog.length > 0 ? debugLog.join("\n") : "暂无日志记录";
        // QInputDialog-free multi-line view: reuse alert with the log
        // (the console already receives every logDebug line via print)
        alert(text);
    }

    // ---- panel registration --------------------------------------------

    var buttons = [];
    for (var r = 0; r < 3; r++) {
        for (var c = 0; c < 3; c++) {
            (function (rr, cc) {
                buttons.push({
                    label: LABELS[rr][cc],
                    tooltip: TIPS[rr][cc],
                    onClick: function () { applyAnchor(RATIOS[rr][cc]); }
                });
            })(r, c);
        }
    }

    registerPanel({
        title: scriptName,
        columns: 3,
        buttonMinSize: 34,
        buttons: buttons,
        extraButtons: [
            {
                label: "\u2299 空对象",
                tooltip: "创建控制器并链接所有选中图层",
                onClick: createNullAndParent
            }
        ]
    });

    // menu commands (also usable without the panel)
    registerCommand("Anchor 9Grid: 居中锚点", function () {
        applyAnchor([0.5, 0.5]);
    });
})();
