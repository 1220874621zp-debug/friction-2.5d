# AGENTS.md

Friction 2.5D 动画与动态图形项目开发者与 AI 智能体协作指南

## 1. 项目概览与系统架构
Friction 2.5D 是一款开源 2D/2.5D 矢量动画与运动设计软件，基于 C++17、Qt 5、Skia 与 OpenGL 构建

### 核心目录结构
- `src/core/` 核心引擎模块（动画、渲染、数学与特效管线）
  - `Animators/` 关键帧动画、缓动曲线、属性树与 2.5D 变换控制器
  - `Boxes/` 图层图元（路径、文本、图像、空对象、容器分层架构）
  - `Expressions/` 表达式解析、JavaScript 运行环境与属性驱动引擎
  - `RasterEffects/` GPU 硬件加速 GLSL 着色器与 CPU 降级栅格特效（含液态玻璃、辉光、故障艺术等）
  - `PathEffects/` / `BlendEffects/` / `TransformEffects/` 矢量与混合修改器
  - `Timeline/` 图层入出点、片段裁切与持续时间条管理
- `src/app/` 应用程序交互与 UI 架构
  - `GUI/` 主窗口、工具栏、菜单系统、时间轴交互、项目面板与场景导航栏
  - `GUI/BoxesList/` 图层属性树控件、属性行渲染与中英文双语属性翻译字典
  - `translations/` Qt 语言包源文件（`.ts`）与编译后二进制文件（`.qm`）
  - `icons/` Hicolor 图标主题资源与 SVG/PNG 资产
- `src/ui/` 自定义 UI 控件、数值微调框、颜色选择器与曲线编辑器
- `src/skia/` Skia 2D 图形库集成与底层渲染绑定

## 2. 跨平台构建、测试与部署命令

### Linux (GCC / Clang)

#### 全量多核编译
```bash
cmake --build build -j$(nproc)
```

#### 执行自动化单元测试
```bash
./build/src/app/friction_effects_test
```

#### 安装到本地用户环境
```bash
cmake --install build --prefix ~/.local
```

#### 一键构建、验证与部署流
```bash
cmake --build build -j$(nproc) && ./build/src/app/friction_effects_test && cmake --install build --prefix ~/.local
```

### Windows (MSVC / Visual Studio / Ninja)

#### 工程配置 (Visual Studio 2022)
```cmd
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
```

#### 工程配置 (Ninja 生成器)
```cmd
cmake -B build -S . -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

#### 多核编译
```cmd
cmake --build build --config Release -j%NUMBER_OF_PROCESSORS%
```

#### 执行自动化单元测试
```cmd
build\src\app\Release\friction_effects_test.exe
```
若使用 Ninja 生成器，测试可执行文件路径为：
```cmd
build\src\app\friction_effects_test.exe
```

#### 产物输出路径
编译生成的可执行程序位于 `build\src\app\Release\friction.exe` 或 `build\output\friction.exe`

## 3. 核心子系统开发与扩展规范

### 新增栅格特效标准接入流程
添加新特效时必须严格执行以下步骤，确保系统完整集成：
1. 在 `src/core/RasterEffects/rastereffect.h` 的 `RasterEffectType` 枚举中声明新类型
2. 创建头文件 `src/core/RasterEffects/<name>effect.h`（继承自 `RasterEffect`）
3. 若包含 GPU 着色器，创建片段着色器 `src/core/RasterEffects/<name>effect.frag`（使用 `#version 330 core`）并在 `src/core/coreresources.qrc` 中注册
4. 创建实现文件 `src/core/RasterEffects/<name>effect.cpp`（实现 `OpenGLRasterEffectCaller` 或 Backdrop / CPU 算法）
5. 在 `src/core/RasterEffects/rastereffectsinclude.h` 中引入头文件
6. 在 `src/core/RasterEffects/rastereffectcollection.cpp` 的 `createRasterEffectForNonCustomType` 中注册工厂实例化分支
7. 在 `src/core/RasterEffects/rastereffectmenucreator.cpp` 中添加到对应分类菜单（菜单名称统一包含英文原名）
8. 在 `src/core/CMakeLists.txt` 中添加 `.cpp` 与 `.h` 源码文件
9. 在 `src/app/GUI/BoxesList/boxsinglewidget.cpp` 的 `translatePropertyName` 映射表中补充特效名与所有参数属性翻译
10. 在 `src/app/translations/friction_zh_CN.ts` 中补充中英文翻译条目并通过 `lrelease` 编译 `.qm` 二进制语言包
11. 在 `src/app/effects_unittest.cpp` 中补充该特效的工厂测试与渲染测试用例

### 属性检查器与双轴联动
- `AEPropertiesInspector` 提供分组折叠的 AE 风格紧凑属性面板
- `位置`、`缩放`、`锚点` 等二维属性通过 `QPointFAnimator` 或 `DynamicComplexAnimator` 聚合管理
- 缩放支持通过锁定按钮在等比缩放与独立缩放模式之间切换
- 图层头部提供图层锁定、可见性控制与 3D 空间开关

### 曲线编辑器（Graph Editor）与缓动系统
- 在时间轴中选中 `位置` 等复合主属性时，系统自动将 X 与 Y 双轴曲线同步载入曲线编辑器
- 单独选中具体子属性时精准加载单轴曲线
- 关键帧缓动计算支持在主属性或单轴属性上批量应用缓动预设

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
- 全局支持莫兰迪动态配色方案，由 `ThemeSupport` 统一派发控件色彩

### 多语言本地化（i18n）
- 源翻译文件：`src/app/translations/friction_zh_CN.ts`
- 编译目标文件：`src/app/translations/friction_zh_CN.qm`
- 提取与更新翻译：
```bash
lupdate src/app src/core src/ui -ts src/app/translations/friction_zh_CN.ts
```
- 编译语言包二进制：
```bash
lrelease src/app/translations/friction_zh_CN.ts -qm src/app/translations/friction_zh_CN.qm
```

## 4. 跨平台适配与打包发布规范

### Windows (MSVC) 平台兼容性要点
- **宏冲突防护**：Windows SDK 与 GDI 头文件预定义了大量大写全局宏（如 `INVERT`、`HALFTONE`、`TRANSPARENT`、`OPAQUE`、`ERROR` 等），在声明同名枚举项或成员时，必须在头文件包含后显式使用 `#undef` 宏防护，防止 MSVC 预处理器将枚举项展开为整型字面量引发语法错误
- **字符编码标准**：跨平台源文件一律采用 UTF-8 编码，CMake 中对 MSVC 编译器强制配置 `/utf-8` 编译参数，防止含非 ASCII 字符（如中文翻译字面量）引发 C2001/C2015 语法断裂
- **动态库符号导出**：Windows 下 `frictioncore` 与 `frictionui` 作为共享库生成，对外暴露的类与函数必须标注 `CORE_EXPORT` 或 `UI_EXPORT` 导出修饰符
- **跨平台语法严谨性**：避免使用特定编译器的非标准扩展，重写虚函数时必须显式标注 `override`，杜绝头文件中重复声明成员函数与变量

### CMake 资产与源码完整性
- **全量注册规则**：新增任何 `.cpp` 实现文件或包含 `Q_OBJECT` 宏的 `.h` 头文件，必须显式登记到所属子目录 `CMakeLists.txt` 的 `SOURCES` 与 `HEADERS` 列表中，确保跨平台 CMake AUTOMOC 机制在 Visual Studio / Ninja / Make 生成器下均能完整生成 MOC 元对象源码
- **资源路径有效性**：`.qrc` 资源文件所引用的相对路径必须真实存在，禁止残留无效或已删除的文件引用，避免在跨平台 CPack / Inno Setup 打包阶段因找不到文件而中断

### 持续集成（CI）与多平台打包管线
- **Linux AppImage**：基于 Docker VFX Platform SDK 容器构建，输出便携式 `.AppImage` 镜像文件
- **Linux DEB**：基于 Ubuntu Runner 通过 Clang + CPack 构建生成 `.deb` 安装包
- **Windows x64**：基于 Visual Studio 2022 x64 构建核心程序与动态库，通过 Inno Setup（`src/app/friction.iss`）构建安装向导程序，并通过 7-Zip 生成便携版 `.7z` 压缩包
- **macOS Universal**：通过 Xcode 工具链构建跨架构 Universal Binary 应用包
- **构建输出位置**：各平台打包产物统一输出到 `build/output/` 或 `distfiles/builds/`

## 5. 文档与代码规范
- **禁止在 Markdown 文档或 README 中使用句号**
- **禁止在 Markdown 文档中使用 Emoji**
- 保持对话与技术回复精炼专业、列表优先
- Git 提交信息统一使用中文编写，清晰表达改动意图
