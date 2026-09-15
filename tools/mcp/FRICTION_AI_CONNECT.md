# Friction 2.5D AI 动画助手

- **接口**：`http://127.0.0.1:9527/mcp`（POST JSON-RPC），请求头带 `X-Friction-Token: <访问令牌>`（Friction 设置 → AI Agent → 访问令牌）
- **图层寻址**：行号为主（`index: 3` = 顶层第 3 行，1-based）；折叠组内行号与组外重复，嵌套层用组路径 `path: "2/1"`（顶层第 2 行的子第 1 行）；`name` 仅兜底。操作前先 `friction_list_layers`（递归树，带 row/path/isGroup）

## 默认动效流程（无详细要求时，一次调用完成）

```
friction_apply_anim_preset  {"preset": "l-sheet-slide-up", "scope": "all"}
```
= 每个顶层图层套预设 + 按行序在时间轴上错开（默认 8 帧/层，图层连关键帧整体后移，单步撤销）。

**推荐预设**（更多用 `friction_list_anim_presets`）：
- 图层（任何层）：`l-fade` 淡入 / `l-pop` 弹出 / `l-drop` 落下 / `l-sheet-slide-up` 上滑 / `l-elastic-scale` 弹性缩放 / `l-rubber-bounce` 橡皮弹跳 / `l-flip-y` 2.5D 翻转 / `l-expand-h` 横向展开
- 文字（逐字/词/行，`kind:"text"`）：`sharp-elastic-pop` / `sharp-cascade-word-pop` / `sharp-snap-rise` / `sharp-double-bounce`

**已有动画只加节奏**：`friction_stagger_layers {"staggerFrames": 8}`（按行序把图层整体后移，首层不动）。

**自建关键帧 + 缓动面板**：`friction_set_keyframe` 打键 → `friction_set_keyframe_easing`（缓动 id 用 `friction_list_easing_presets` 枚举，如 `easeOutCubic`、`easeOutBack`）。

## 声明式 markup（文字 PV / 排版首选）

一段 HTML 风格标记整体生成动效（`friction_render_markup`，`mode:"append"` 追加）：

```html
<scene width="1920" height="1080" fps="60" duration="6" bg="#0a0c12">
  <seq from="0" to="2.5">
    <card w="1100" h="420" bg="#12151f" 3d="true" rotY="-25->0">
      <col gap="14">
        <text size="24" color="#00f2fe" in="slide-up">PHASE 01</text>
        <h1>KINETIC</h1>
        <line w="600" color="#ff3366" in="expand" />
      </col>
    </card>
  </seq>
</scene>
```

常用标签：`scene/seq/card/col/row/text/h1/p/line/circle/hud`；动画值写 `from->to`。完整参考见 `.agents/skills/friction-2.5d/SKILL.md`。

## 画风参考（HTML → 原生构建）

用户拿 HTML / 技能文件 / 网页当画风参考时，标准句式：

```
参考 xxx.html 的画风，在 Friction 里做一个 XXX 动画（+描述动效节奏）
```

处理方式：读 HTML/CSS 提取设计语言（色板 / border 描边 / border-radius / 硬阴影 box-shadow / 字体气质 / 布局密度）→ 原生图层重建（描边=setStrokeWidth、圆角=setCornerRadius、硬阴影=偏移纯色矩形垫底、web 字体换本地近似如 Arial Black / Consolas / 微软雅黑）→ `friction_eval_script` 打关键帧做动画。**勿手写、勿导出 Lottie**（本版仅支持 Lottie 导入，无导出）。

## 常用工具

`friction_render_markup`｜`friction_apply_anim_preset`｜`friction_stagger_layers`｜`friction_update_layer`（就地改层）｜`friction_animate_layer`（单层宏动画）｜`friction_list_layers`｜`friction_get_scene_info`｜`friction_set_keyframe` + `friction_set_keyframe_easing`｜`friction_eval_script`（全量 JS）｜`friction_capture_viewport`（看画面）｜`friction_undo/redo`

## 协作守则（仅四条）

1. 严禁清屏重绘——改已有内容用 `friction_update_layer` 或 markup `mode:"append"`
2. 报错会附带可用图层/属性清单，直接自纠重试
3. 仅被明确要求时才截图/storyboard 审片，不要主动多轮复查
4. 缓动优先，禁线性插值（预设已内置物理缓动）
