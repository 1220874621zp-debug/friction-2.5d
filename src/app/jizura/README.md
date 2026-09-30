# JIZURA planner vendor

JIZURA（字面）lyric motion engine planner 侧源码的 vendored 副本。

- 出典: https://github.com/852wa/JIZURA （pin: main 8da975f, 2026-09-27）
- 许可: MIT（见 `LICENSE`，© 852wa）
- 用途: `LyricMotionPanel`（歌词动画面板）的规划引擎。不含 `12_ui.js`
  （浏览器 UI）；只用 `J.plan()` / `J.omakase` / 风格与部件注册表，
  绘制路径不参与。
- `jizura-stub.js` 是 friction 侧追加的加载期 shim（QJSEngine 用），
  不是 JIZURA 本体代码。

## 重要：本目录的 .js 是转译产物

本机 Qt 的 QJSEngine 特性面有限（无 async 函数、无对象展开等，
详见记忆 qjsengine-es-level-cachyos），因此这里的 JS 已用 esbuild
降级到 es2016：

```
esbuild <file>.js --target=es2016 --allow-overwrite --outfile=<file>.js
```

esbuild 无需 node，可从 npm registry 取单文件二进制：
`https://registry.npmjs.org/@esbuild/linux-x64/-/linux-x64-<ver>.tgz`
（包内 `package/bin/esbuild`）。

## 同步上游流程

1. 复制上游 `src/*.js` 到本目录（除 `12_ui.js`），保留 `jizura-stub.js`。
2. 对每个新文件跑上述 esbuild 转译。
3. 在 resources.qrc 的 `/jizura` 前缀登记（若增删文件）。
4. 用裸 QJSEngine probe（g++ -lQt6Qml 的 test.cpp）先验证加载与
   `J.plan` 冒烟，再进 app 编译。
