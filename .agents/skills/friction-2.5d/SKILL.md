---
name: friction-2.5d
description: >-
  Expert guide and tool workflow for creating 2D and 2.5D vector animations,
  kinetic typography (Text PV), motion graphics (MG), and controlling the Friction 2.5D timeline
  via real-time MCP / JSON-RPC and declarative HTML/JS API integration
---

# Friction 2.5D Motion Design & MCP Skill

Friction 2.5D is an open-source 2D/2.5D vector animation and motion design software
This skill teaches AI agents how to interact with, query, style, and animate scenes in Friction 2.5D in real-time

--------------------------------------------------------------------------------

## 1. Connection & MCP Architecture

Friction 2.5D runs an embedded high-performance MCP / JSON-RPC server

- **HTTP / REST API Endpoint**: `http://127.0.0.1:9527`
- **MCP Endpoint**: `http://127.0.0.1:9527/mcp`
- **Stdio MCP Bridge**: `python3 tools/mcp/friction_mcp_bridge.py --port 9527`
- **Local Unix IPC Socket**: `/tmp/friction_mcp.sock` (Linux/macOS)

### Quick Status Check
```bash
curl -s http://127.0.0.1:9527/api/status
```

### MCP Client Config (for AI agents)
```json
{
    "mcpServers": {
        "friction": {
            "command": "python3",
            "args": ["tools/mcp/friction_mcp_bridge.py", "--port", "9527"]
        }
    }
}
```

--------------------------------------------------------------------------------

## 2. Declarative HTML/XML Motion Graphics DSL

For fast and expressive motion graphics generation, use the declarative HTML engine (`tools/mcp/friction_markup.py`):

```bash
# Execute from file or inline string
python3 tools/mcp/friction_markup.py my_scene.html
python3 tools/mcp/friction_markup.py -m '<scene><hud /><card w="1000" h="400" 3d="true"><h1 anim="rise">TITLE</h1></card></scene>'
```

### 2.1 Complete Tag & Component Reference

Tag | Purpose | Key Attributes
:--- | :--- | :---
`<scene>` | Root container | `width` (1920), `height` (1080), `fps` (60), `duration` (10.0), `bg` (`#08090e`)
`<seq>` / `<act>` | Time section sequence | `from` (start second), `to` (end second)
`<col>` / `<row>` | Flexbox layout stack | `gap` (spacing), `x`, `y`
`<card>` / `<box>` | 2.5D card container | `w`, `h`, `bg`, `border`, `bw`, `radius`, `3d="true"`, `rotX`, `rotY`, `z`
`<text>` / `<h1>`..`<h3>` / `<p>` | Typography element | `size`, `color`, `font`, `align`, `anim`, `in`, `letter-spacing`, `3d`, `rotX`, `rotY`
`<stagger-text>` | Per-character animator | `anim="rise|typewriter|wave|breathe|zoom|spin|drop"`
`<text-strip>` | Rotated text banner | `text`, `rot` (-22), `w`, `h`, `bg`, `color`, `size`
`<glow-cards>` | Segmented character tiles | `text`, `card-bg`, `color`, `size`, `gap`, `3d="true"`
`<vertical-text>` | Vertical CJK typesetting | `text`, `size`, `color`, `spacing`, `x`, `y`
`<scatter-text>` | Background decorative chars | `chars`, `count`, `color`, `min-size`, `max-size`
`<formula-scatter>` | Background math/physics formulas | `count`, `color`
`<concentric>` | Concentric orbital rings | `count`, `max-r`, `stroke`, `bw`, `3d="true"`, `rotX`, `rotY`
`<burst-lines>` | Radial burst rays / speed lines | `count`, `inner`, `outer`, `color`, `bw`, `rot`
`<ruler>` | Technical measurement scale | `x`, `y`, `w`, `spacing`, `color`, `tick-color`
`<bg-blocks>` | Depth background blocks | `count`, `color`, `alpha`
`<hud>` | Technical border & timestamps | `title`, `sub`, `timecode="true"`, `brackets="true"`, `color`, `accent`
`<crosshair>` | Precise coordinate marker | `x`, `y`, `size`, `color`
`<barcode>` | Vector ID barcode | `x`, `y`, `w`, `h`, `color`
`<line>` | Expanding divider line | `w`, `h`, `color`, `in="expand"`
`<circle>` | Vector circle/ellipse | `r`, `color`, `stroke`, `bw`, `3d="true"`, `rotX`, `rotY`

### 2.2 Built-in Per-Character Text Animation Presets (`anim="..."`)

Preset ID | Chinese Name | Motion Characteristics
:--- | :--- | :---
`rise` | 上浮 | Characters rise from bottom with soft staggered sweep
`typewriter` | 打字机 | Sharp character-by-character typewriter reveal
`fade` | 淡入淡出 | Staggered character fade-in
`slide-l` | 左侧划动 | Staggered slide from left
`slide-r` | 右侧划动 | Staggered slide from right
`zoom` | 缩放 | Characters scale up from center
`spin` | 旋转 | Characters spin and fly in
`drop` | 上方落下 | Characters fall down from above
`word-rise` | 逐词上浮 | Staggered by words instead of letters
`line-reveal` | 逐行展开 | Staggered line by line
`number-roll` | 数字滚动 | Numbers roll from 0 up to target value
`wave` | 波浪 | Continuous character vertical sine wave loop
`wave-sway` | 风吹摇摆 | Continuous character rotation sway loop
`breathe` | 呼吸 | Subtle rhythmic character pulse loop

--------------------------------------------------------------------------------

## 3. JavaScript Scripting API Reference

Evaluate scripts directly via `friction_eval_script` or `POST http://127.0.0.1:9527/api/eval`:

```javascript
var s = app.activeScene;
s.width = 1920; s.height = 1080; s.fps = 60; s.duration = 8.0;

// Add 2.5D Text with character animation preset
var t = s.addText("hero", "MOTION GRAPHICS");
t.setFontFamily("Noto Sans CJK JP");
t.setFontSize(96);
t.setFillColor("#ffffff");
t.setTextAlignment("center");
t.position().setValue([960, 540]);
t.applyTextPreset("rise", 0, 1.0, false);

// Add 2.5D orbital rings
var ring = s.addEllipse("orbit", 0, 0, 240);
ring.position().setValue([960, 540]);
ring.setFillColor("transparent");
ring.setStrokeColor("#00f2fe");
ring.setStrokeWidth(2.0);
ring.set3DEnabled(true);
ring.rotationX().setValueAtFrame(0, 60);
ring.rotationY().setValueAtFrame(0, 0);
ring.rotationY().setValueAtFrame(480, 360);
```

### 3.1 Layer Proxy Methods

Method | Description
:--- | :---
`lay.position()` / `lay.scale()` / `lay.rotation()` | Transform animators (Point / Scalar)
`lay.rotationX()` / `lay.rotationY()` / `lay.zPosition()` | 2.5D spatial rotation and depth
`lay.opacityProp()` | 0-100% opacity animator
`lay.set3DEnabled(true/false)` | Enable/disable 2.5D perspective billboard transforms
`lay.applyTextPreset(id, startFrame, durScale, out)` | Apply AE-style per-character animator
`lay.textPresets()` | Query array of all available text animation presets
`lay.setFillColor(hex)` / `lay.setStrokeColor(hex)` | Fill and outline color (`#rrggbb`, `transparent`)
`lay.setStrokeWidth(w)` | Outline width
`lay.setFontFamily(name)` / `lay.setFontSize(size)` | Typography font and scale
`lay.setLetterSpacing(val)` / `lay.setLineSpacing(val)` | Typographic tracking and line height
`lay.setTextAlignment("left" / "center" / "right")` | Text alignment relative to position anchor
`lay.setCornerRadius(r)` | Rounded rectangle corners
`lay.setBlendMode(mode)` | Blend modes: `screen`, `multiply`, `overlay`, `add`, `difference`
`lay.addEffect(type)` | Add one of 35+ GPU shaders (`glow`, `glitch`, `scanlines`, `liquid_glass`, etc.)
`lay.setAnchorPoint([x, y])` | Shift layer transform pivot

### 3.2 Property Keyframe Methods

Method | Description
:--- | :---
`prop.setValue(v)` | Set static base value (`scalar` or `[x, y]`)
`prop.setValueAtFrame(frame, v)` | Set keyframe value at integer frame
`prop.setValueAtTime(sec, v)` | Set keyframe value at time in seconds
`prop.valueAtFrame(frame)` | Sample interpolated value at frame
`prop.removeKeyAtFrame(frame)` | Remove keyframe

--------------------------------------------------------------------------------

## 4. MCP Tools Catalog (Summary)

Category | Key Tools
:--- | :---
**Scene** | `friction_get_scene_info`, `friction_set_scene_info`, `friction_create_scene`, `friction_list_scenes`
**Layer** | `friction_create_layer`, `friction_list_layers`, `friction_duplicate_layer`, `friction_delete_layer`, `friction_set_3d_mode`
**Keyframe** | `friction_set_property_value`, `friction_set_keyframe`, `friction_remove_keyframe`, `friction_clear_keyframes`
**Effects** | `friction_add_raster_effect`, `friction_remove_raster_effect`, `friction_list_available_effects`
**Script & Viewport** | `friction_eval_script`, `friction_seek_timeline`, `friction_play_pause`, `friction_capture_viewport`
