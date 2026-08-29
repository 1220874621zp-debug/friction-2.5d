# AGENTS.md

Friction 2.5D 动画与动态图形项目开发者与 AI 智能体协作指南

## 1. 项目概览与系统架构
Friction 2.5D 是一款开源 2D/2.5D 矢量动画与运动设计软件，基于 C++17、Qt 5、Skia 与 OpenGL 构建

### 核心目录结构
- `src/core/` 核心引擎模块（动画、渲染、数学与特效管线）
  - `Animators/` 关键帧动画、缓动曲线、属性树与 2.5D 变换控制器
  - `Boxes/` 图层图元（路径、文本、图像、空对象、容器分层架构）
  - `RasterEffects/` GPU 硬件加速 GLSL 着色器与 CPU 降级栅格特效
  - `PathEffects/` / `BlendEffects/` / `TransformEffects/` 矢量与混合修改器
  - `Timeline/` 图层入出点、片段裁切与持续时间条管理
- `src/app/` 应用程序交互与 UI 架构
  - `GUI/` 主窗口、工具栏、菜单系统、时间轴交互与对话框
  - `GUI/BoxesList/` 图层属性树控件与中英文双语属性翻译字典
  - `translations/` Qt 语言包源文件（`.ts`）与编译后二进制文件（`.qm`）
  - `icons/` Hicolor 图标主题资源与 SVG/PNG 资产
- `src/ui/` 自定义 UI 控件、数值微调框、颜色选择器与曲线编辑器
- `src/skia/` Skia 2D 图形库集成与底层渲染绑定

## 2. 构建、测试与部署命令

### 全量多核编译
```bash
cmake --build build -j$(nproc)
```

### 执行自动化单元测试
```bash
./build/src/app/friction_effects_test
```

### 安装到本地用户环境
```bash
cmake --install build --prefix /home/lanrhyme/.local
```

### 一键构建、验证与部署流
```bash
cmake --build build -j$(nproc) && ./build/src/app/friction_effects_test && cmake --install build --prefix /home/lanrhyme/.local
```

## 3. 核心子系统开发与扩展规范

### 新增栅格特效标准接入流程
添加新特效时必须严格执行以下 12 步，确保系统完整集成：
1. 在 `src/core/RasterEffects/rastereffect.h` 的 `RasterEffectType` 枚举中声明新类型
2. 创建头文件 `src/core/RasterEffects/<name>effect.h`（继承自 `RasterEffect`）
3. 创建片段着色器 `src/core/RasterEffects/<name>effect.frag`（使用 `#version 330 core`）
4. 创建实现文件 `src/core/RasterEffects/<name>effect.cpp`（实现 `OpenGLRasterEffectCaller` 与 `processCpu` 降级算法）
5. 在 `src/core/RasterEffects/rastereffectsinclude.h` 中引入头文件
6. 在 `src/core/RasterEffects/rastereffectcollection.cpp` 的 `createRasterEffectForNonCustomType` 中注册工厂实例化分支
7. 在 `src/core/RasterEffects/rastereffectmenucreator.cpp` 中添加到对应分类菜单（菜单名称统一包含中文与英文原名）
8. 在 `src/core/coreresources.qrc` 中注册 `.frag` 着色器资源
9. 在 `src/core/CMakeLists.txt` 中添加 `.cpp` 与 `.h` 源码文件
10. 在 `src/app/GUI/BoxesList/boxsinglewidget.cpp` 的 `translatePropertyName` 映射表中补充特效名与所有参数属性翻译
11. 在 `src/app/translations/friction_zh_CN.ts` 中补充中英文翻译条目并通过 `lrelease` 编译 `.qm` 二进制语言包
12. 在 `src/app/effects_unittest.cpp` 中补充该特效的工厂测试与 CPU 渲染测试用例

### 时间轴图层入出点与片段系统
- 所有图层均派生自 `eBoxOrSound`，持有可选或隐式 `DurationRectangle`
- 入点与出点裁切交互：
  - `Canvas::setSelectedBoxesInPoint()`（快捷键 `Alt+[`）
  - `Canvas::setSelectedBoxesOutPoint()`（快捷键 `Alt+]`）
  - `TimelineDockWidget::splitClip()`（快捷键 `Ctrl+Shift+D`）
- 界面绘制与交互由 `DurationRectangle::draw`、`getMovableAt` 与 `KeysView::paintEvent` 驱动

### 图标系统与主题管理
- 图标通过 `QIcon::fromTheme("name")` 从 `:/icons/hicolor/` 资源中解析
- 资源清单位于 `src/app/icons/hicolor.qrc`
- 矢量 SVG 源文件位于 `src/app/icons/hicolor/scalable/`
- 多分辨率 PNG 图标（16x16 至 256x256）通过 `rsvg-convert` 工具批量渲染生成

### 多语言本地化（i18n）
- 源翻译文件：`src/app/translations/friction_zh_CN.ts`
- 编译目标文件：`src/app/translations/friction_zh_CN.qm`
- 编译命令：
```bash
lrelease src/app/translations/friction_zh_CN.ts -qm src/app/translations/friction_zh_CN.qm
```
- 图层属性面板文本集中在 `src/app/GUI/BoxesList/boxsinglewidget.cpp` 中的 `translatePropertyName` 映射

## 4. 文档与代码规范
- **禁止在 Markdown 文档或 README 中使用句号**
- **禁止在 Markdown 文档中使用 Emoji**
- 保持对话与技术回复精炼专业、列表优先
- Git 提交信息统一使用中文编写，清晰表达改动意图
