// audioSpectrum.js — Audio Spectrum / Waveform 音乐可视化生成器
//
// 用法：
//   1. 场景里放一个音频图层（或用面板的"[选择音频文件…]"直接选文件）
//   2. 选择模式（频谱柱 / 波形）、柱数、灵敏度等参数
//   3. 点"▶ 生成可视化"：脚本解码音频（FFmpeg 同步解码），
//      按帧把幅值烘成每根柱子的 scale 关键帧，所有柱子
//      绑到一个控制组图层下，一个撤销步骤可整体回退
//
// 依赖的引擎 API（本次同步新增）：
//   scene.sounds() / sound.filePath() / sound.frameRange() / sound.analyze(bands)
//   app.analyzeAudio(path, fps, bands)
//   prop.setValuesAtFrames(startFrame, values)  批量打关键帧
//   chooseFile(caption, filter)                 原生文件对话框
(function () {
    var debugLog = [];
    function log(msg) {
        debugLog.push(msg);
        print(msg);
        if (debugLog.length > 300) debugLog.shift();
    }

    var FILE_OPTION = "[选择音频文件…]";

    var state = {
        mode: 0,          // 0 = 频谱柱, 1 = 波形
        bars: 32,
        sensitivity: 1.5,
        maxH: 400,
        smooth: 0.25,     // 衰减平滑 0..0.95
        barW: 12,
        gap: 4,
        soundIndex: 0,    // combo 索引；最后一项是 FILE_OPTION
        sounds: [],       // JsSoundProxy 缓存（与 combo 对齐）
        filePath: ""      // 外部文件路径（选择 FILE_OPTION 时）
    };

    function refreshSounds() {
        state.sounds = [];
        var scene = app.activeScene;
        if (scene) {
            var s = scene.sounds();
            if (s && s.length) {
                for (var i = 0; i < s.length; i++) {
                    state.sounds.push(s[i]);
                }
            }
        }
        var names = [];
        for (var j = 0; j < state.sounds.length; j++) {
            var snd = state.sounds[j];
            names.push(snd.name + " (" + snd.duration.toFixed(1) + "s)");
        }
        names.push(FILE_OPTION);
        if (state.soundIndex >= names.length) { state.soundIndex = 0; }
        updateCombo("audio", names, state.soundIndex);
        log("音频图层列表刷新: " + (names.length - 1) + " 个");
    }

    // 一阶衰减平滑：上升沿立即响应，下降沿按 smooth 比例拖尾
    function smoothSeries(values, smooth) {
        if (!smooth || smooth <= 0) { return values; }
        var out = new Array(values.length);
        var prev = 0;
        for (var i = 0; i < values.length; i++) {
            var v = values[i];
            if (v >= prev) {
                prev = v;
            } else {
                prev = v + (prev - v) * smooth;
            }
            out[i] = prev;
        }
        return out;
    }

    function analyzeCurrentSource(bands) {
        var scene = app.activeScene;
        if (state.soundIndex < state.sounds.length) {
            var snd = state.sounds[state.soundIndex];
            log("分析音频图层: " + snd.name + " 文件: " + snd.filePath());
            var data = snd.analyze(bands);
            var range = snd.frameRange();
            data.__startFrame = range ? range[0] : 0;
            data.__sourceName = snd.name;
            return data;
        }
        if (!state.filePath) {
            return { ok: false, error: "未选择音频文件（点音频来源下拉的 " +
                     FILE_OPTION + "）" };
        }
        log("分析外部文件: " + state.filePath);
        var d2 = app.analyzeAudio(state.filePath, scene.fps, bands);
        d2.__startFrame = 0;
        d2.__sourceName = state.filePath.replace(/^.*[\\/]/, "");
        return d2;
    }

    function generate() {
        var scene = app.activeScene;
        if (!scene) { alert("请先打开场景"); return; }

        var nBars = Math.max(2, Math.round(state.bars));
        var useBands = (state.mode === 0);
        // 波形模式用 RMS 能量（单通道幅值），频谱模式用频带能量
        var data = analyzeCurrentSource(useBands ? nBars : 0);
        if (!data || !data.ok) {
            var err = data && data.error ? data.error : "分析失败";
            log("分析失败: " + err);
            alert("音频分析失败: " + err);
            return;
        }
        log("解码完成: " + data.__sourceName +
            " 时长=" + data.duration.toFixed(2) + "s" +
            " 采样率=" + data.sampleRate +
            " 帧数=" + data.frames + " @ " + data.fps + "fps" +
            " 起始帧=" + data.__startFrame);
        if (useBands) {
            log("频段范围: " + data.bandFreqs[0].toFixed(0) + "Hz ~ " +
                data.bandFreqs[data.bandFreqs.length - 1].toFixed(0) + "Hz");
        }

        // 限制规模，防止误操作生成几十万关键帧
        var totalKeys = nBars * data.frames;
        if (totalKeys > 400000) {
            if (!confirm("将生成约 " + totalKeys + " 个关键帧（" + nBars +
                         " 柱 × " + data.frames + " 帧），可能较慢。继续？")) {
                log("用户取消（关键帧过多）");
                return;
            }
        }

        var W = scene.width;
        var H = scene.height;
        var barW = Math.max(1, state.barW);
        var gap = Math.max(0, state.gap);
        var totalW = nBars * barW + (nBars - 1) * gap;
        if (totalW > W * 1.5) {
            log("提示: 总宽度 " + totalW + "px 超出画布较多，柱会伸出画面");
        }
        var startX = (W - totalW) / 2 + barW / 2;
        var centerY = H / 2;      // 柱以中线为中心上下对称生长
        var maxH = Math.max(10, state.maxH);
        var sens = state.sensitivity;
        var startFrame = data.__startFrame;

        app.beginUndoGroup("生成音频可视化");
        try {
            var groupName = (useBands ? "音频频谱 " : "音频波形 ") +
                            data.__sourceName;
            var group = scene.addLayer(groupName);
            if (!group) { throw new Error("创建控制组失败"); }

            for (var b = 0; b < nBars; b++) {
                var series;
                if (useBands) {
                    series = data.bands[b];
                } else {
                    series = data.rms;
                }
                series = smoothSeries(series, state.smooth);

                var values = new Array(data.frames);
                for (var f = 0; f < data.frames; f++) {
                    var v = series[f] * sens;
                    if (v < 0.02) { v = 0.02; }   // 保留细线底
                    values[f] = [1, v];
                }

                var x = startX + b * (barW + gap);
                var bar = scene.addRect("bar_" + b,
                                        x - barW / 2, centerY - maxH / 2,
                                        barW, maxH);
                if (!bar) {
                    log("警告: 第 " + b + " 根柱创建失败，跳过");
                    continue;
                }
                bar.scale().setValuesAtFrames(startFrame, values);
                bar.setParentLayer(group);
            }

            log("完成: " + nBars + " 柱 × " + data.frames +
                " 帧关键帧已烘焙（起始帧 " + startFrame + "）");
            log("提示: 选中控制组「" + groupName +
                "」可整体移动/缩放/改色；灵敏度=" + sens +
                " 过大导致削顶时可调低后重新生成");
        } catch (e) {
            log("出错: " + e);
            alert("生成失败: " + e);
        } finally {
            app.endUndoGroup();
        }
    }

    registerPanel({
        title: "音频可视化",
        columns: 3,
        combos: [
            {
                label: "音频来源", id: "audio",
                options: ["(加载中…)"],
                index: 0,
                tooltip: "场景中的音频图层，或选择外部音频文件",
                onChange: function (i, text) {
                    state.soundIndex = i;
                    if (text === FILE_OPTION) {
                        var p = chooseFile(
                            "选择音频文件",
                            "音频文件 (*.mp3 *.wav *.aac *.m4a *.ogg *.flac);;所有文件 (*)");
                        if (p) {
                            state.filePath = p;
                            log("已选文件: " + p);
                            updateCombo("audio",
                                        [p.replace(/^.*[\\/]/, "") +
                                         " (外部文件)", FILE_OPTION], 0);
                            state.soundIndex = 0;
                            state.sounds = []; // 标记使用外部文件
                        } else {
                            log("未选择文件");
                        }
                    } else {
                        state.filePath = "";
                    }
                }
            },
            {
                label: "模式", id: "mode",
                options: ["频谱柱 (Spectrum)", "波形 (Waveform)"],
                index: 0,
                tooltip: "频谱柱=每柱一个频段；波形=整体响度",
                onChange: function (i) {
                    state.mode = i;
                    log("模式: " + (i === 0 ? "频谱柱" : "波形"));
                }
            }
        ],
        sliders: [
            {
                label: "柱数量", id: "bars",
                min: 4, max: 128, value: 32, decimals: 0,
                tooltip: "频谱模式的频段数 / 波形模式的柱数",
                onChanging: function (v) { state.bars = Math.round(v); },
                onChange: function (v) { state.bars = Math.round(v); }
            },
            {
                label: "灵敏度", id: "sens",
                min: 0.1, max: 5, value: 1.5, decimals: 2,
                tooltip: "幅值放大倍数，过大柱会削顶",
                onChanging: function (v) { state.sensitivity = v; },
                onChange: function (v) { state.sensitivity = v; }
            },
            {
                label: "最大高度", id: "maxH",
                min: 20, max: 1500, value: 400, decimals: 0,
                tooltip: "满幅值时的柱高（像素）",
                onChanging: function (v) { state.maxH = v; },
                onChange: function (v) { state.maxH = v; }
            },
            {
                label: "平滑", id: "smooth",
                min: 0, max: 0.95, value: 0.25, decimals: 2,
                tooltip: "下降沿拖尾：0=硬切，越大越柔",
                onChanging: function (v) { state.smooth = v; },
                onChange: function (v) { state.smooth = v; }
            },
            {
                label: "柱宽", id: "barW",
                min: 2, max: 80, value: 12, decimals: 0,
                onChanging: function (v) { state.barW = v; },
                onChange: function (v) { state.barW = v; }
            },
            {
                label: "间隙", id: "gap",
                min: 0, max: 60, value: 4, decimals: 0,
                onChanging: function (v) { state.gap = v; },
                onChange: function (v) { state.gap = v; }
            }
        ],
        buttons: [
            { label: "↻ 刷新列表", tooltip: "重新扫描场景音频图层",
              onClick: refreshSounds }
        ],
        extraButtons: [
            { label: "▶ 生成可视化", tooltip: "解码音频并生成关键帧动画",
              onClick: generate },
            {
                label: "☰ 调试日志",
                tooltip: "查看并复制调试日志",
                onClick: function () {
                    alert(debugLog.length > 0 ? debugLog.join("\n")
                                              : "暂无日志");
                }
            }
        ]
    });

    // evaluate 同步阶段：面板注册完成后立刻填充音频列表
    refreshSounds();
    log("音频可视化面板已加载（模式=频谱柱）");
})();
