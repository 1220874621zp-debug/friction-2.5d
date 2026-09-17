# 脚本控制台：交互式 JS 编程

脚本控制台是一个 JavaScript REPL（交互式编程环境）停靠面板，可以用完整的 JS 语言操作软件的全部对象：场景、图层、属性、关键帧、路径。打开方式：菜单「脚本 → 脚本控制台」（再次点击隐藏）。

## 输入区操作

- **回车**：执行输入的 JS 代码
- **Shift+回车**：换行（写多行代码）
- **Ctrl+↑ / Ctrl+↓**：浏览历史输入

## 工具栏按钮

| 按钮 | 作用 |
|------|------|
| 打开脚本 | 选择一个 .js 文件在控制台执行 |
| 重新加载脚本 | 重新扫描脚本文件夹并重载全部插件 |
| 清空 | 清空输出区 |

`print()` 输出和报错信息都显示在下方的输出区。脚本文件夹可通过菜单「脚本 → 打开脚本文件夹」直接打开，放进去的 .js 重载后会注册进「脚本」菜单。

## 常用 API 速查

### app 与场景

```javascript
var scene = app.activeScene;          // 当前场景（可能为 null）
print(scene.name + " " + scene.width + "x" + scene.height
      + " 图层数=" + scene.numLayers);
print("当前帧=" + scene.currentFrame + " 帧率=" + scene.fps);
```

### 读取与修改属性

```javascript
var layer = scene.layer(0);           // 按下标取图层
var named = scene.layer("标题文字");   // 按名称取图层（支持中文）
var sel = scene.selectedLayers();     // 选中的图层数组

var pos = layer.position().value();   // [x, y]
print(layer.name + " 位置 x=" + pos[0] + " y=" + pos[1]);
```

### 写操作要包撤销组

多步写操作用 `beginUndoGroup`/`endUndoGroup` 包起来（合成一步撤销），并用 try/finally 保证一定关闭：

```javascript
app.beginUndoGroup("移动图层");
try {
    layer.position().setValue([pos[0] + 50, pos[1]]);
    layer.property("rotation").setValue(15);
} finally {
    app.endUndoGroup();
}
```

### 关键帧

```javascript
var p = layer.position();
p.setValueAtFrame(0, [0, 0]);
p.setValueAtFrame(24, [500, 300]);
print("关键帧数=" + p.numKeys);        // 注意：属性形式，不能加括号
```

### 创建图层

```javascript
var rect = scene.addRect("方块", 100, 100, 200, 200);
var text = scene.addText("文字", "你好");
var ctrl = scene.addNull("控制器");    // 空对象
```

## 易踩的坑

- **不透明度是 0~100**：`layer.opacity = 60` 表示 60%。写 0.6 会得到 0.6%（几乎全透明）。
- **`value` 和 `numKeys` 是属性不是方法**：被同名 Q_PROPERTY 遮蔽，`p.value()`、`p.numKeys()` 调用无效，必须用 `p.value`、`p.numKeys` 属性形式。其他方法（`setValue()`、`keyTime()` 等）正常加括号。
- **关键帧索引从 1 起**：`keyTime(1)` 是第一个关键帧（AE 惯例）。

## 注册命令与面板

脚本（.js 文件）里可以用：

- `registerCommand("显示名", function () { ... })` —— 在「脚本」菜单下加一个命令
- `registerPanel({...})` —— 注册一个停靠面板工具（滑杆/下拉/按钮）

这样常用流程可以固化成一键操作，不进控制台也能用。

## 和命令面板的区别

| | [命令面板](../basics/command-palette.md)（Ctrl+P） | 脚本控制台 |
|---|---|---|
| 本质 | 快速搜索 + 固定语法命令 | JavaScript 编程环境 |
| 能力边界 | 只认内置死语法（跳帧、旋转、缩放等十几条），写不了逻辑 | 完整 JS：循环、函数、条件、撤销组 |
| 适合 | 手快——两三下跳帧、旋转选中层、搜菜单动作 | 批量操作、自动化、开发面板插件 |

简单说：命令面板是「快捷键的图形版」，脚本控制台是「给软件写代码的地方」。带逻辑的活（比如给 20 个图层每个错开 5 帧打关键帧）只有脚本能做。
