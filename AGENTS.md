# AGENTS.md

Developer and AI agent guidelines for the Friction 2.5D animation and motion graphics codebase

## 1. Project Overview & Architecture
Friction 2.5D is an open source 2D and 2.5D vector animation and motion design application built on C++17, Qt 5, Skia, and OpenGL

### Directory Structure
- `src/core/` Core animation, rendering, math, and effect engine
  - `Animators/` Keyframe animation, easing curves, property trees, and transform controllers
  - `Boxes/` Layer primitives, paths, text, images, null objects, and container hierarchies
  - `RasterEffects/` Hardware-accelerated GLSL shader and CPU fallback raster effects
  - `PathEffects/` / `BlendEffects/` / `TransformEffects/` Vector and compositing modifier pipelines
  - `Timeline/` Duration rectangle, in/out trimming, and clip sequencing
- `src/app/` Application UI, dock widgets, and main window orchestration
  - `GUI/` Main window, toolbar, menu system, timeline view, and dialogs
  - `GUI/BoxesList/` Layer tree property widgets and property name translation mapping
  - `translations/` Qt translation source (`.ts`) and binary (`.qm`) localization files
  - `icons/` Hicolor icon theme resources and SVG/PNG assets
- `src/ui/` Custom UI widgets, spinboxes, color pickers, and curve editors
- `src/skia/` Skia 2D graphics library integration and bindings

## 2. Build, Test & Install Commands

### Full Build
```bash
cmake --build build -j$(nproc)
```

### Run Unit Tests
```bash
./build/src/app/friction_effects_test
```

### Install to Local User Prefix
```bash
cmake --install build --prefix /home/lanrhyme/.local
```

### All-in-One Build, Test, and Deploy
```bash
cmake --build build -j$(nproc) && ./build/src/app/friction_effects_test && cmake --install build --prefix /home/lanrhyme/.local
```

## 3. Subsystem Implementation Guides

### Adding a New Raster Effect
Follow this sequence to ensure full system integration:
1. Define effect enum in `src/core/RasterEffects/rastereffect.h` (`enum class RasterEffectType`)
2. Create header `src/core/RasterEffects/<name>effect.h` inheriting from `RasterEffect`
3. Create fragment shader `src/core/RasterEffects/<name>effect.frag` (`#version 330 core`)
4. Create implementation `src/core/RasterEffects/<name>effect.cpp` with `OpenGLRasterEffectCaller` and `processCpu`
5. Include header in `src/core/RasterEffects/rastereffectsinclude.h`
6. Register in `createRasterEffectForNonCustomType` within `src/core/RasterEffects/rastereffectcollection.cpp`
7. Add menu entry in `src/core/RasterEffects/rastereffectmenucreator.cpp` (`RasterEffectMenuCreator::forEveryEffectCore`)
8. Add shader file to `src/core/coreresources.qrc`
9. Add `.cpp` and `.h` files to `src/core/CMakeLists.txt`
10. Add bilingual Chinese and English names to `translatePropertyName` in `src/app/GUI/BoxesList/boxsinglewidget.cpp`
11. Add translations in `src/app/translations/friction_zh_CN.ts` and compile `.qm` via `lrelease`
12. Add test cases in `src/app/effects_unittest.cpp`

### Timeline & Layer Duration System
- Every layer derives from `eBoxOrSound` and can hold an optional or implicit `DurationRectangle`
- In-point and out-point trimming:
  - `Canvas::setSelectedBoxesInPoint()` (shortcut `Alt+[`)
  - `Canvas::setSelectedBoxesOutPoint()` (shortcut `Alt+]`)
  - `TimelineDockWidget::splitClip()` (shortcut `Ctrl+Shift+D`)
- Visualization is drawn in `DurationRectangle::draw` and `KeysView::paintEvent`

### Icon System & Theming
- Icons are resolved via `QIcon::fromTheme("name")` against `:/icons/hicolor/`
- Resource definition located at `src/app/icons/hicolor.qrc`
- Vector scalable SVGs stored in `src/app/icons/hicolor/scalable/`
- Multi-resolution PNGs (16x16 to 256x256) rendered via `rsvg-convert`

### Localization (i18n)
- Source translation: `src/app/translations/friction_zh_CN.ts`
- Binary compiled translation: `src/app/translations/friction_zh_CN.qm`
- Compile command:
```bash
lrelease src/app/translations/friction_zh_CN.ts -qm src/app/translations/friction_zh_CN.qm
```
- Layer tree property mapping is centralized in `translatePropertyName` within `src/app/GUI/BoxesList/boxsinglewidget.cpp`

## 4. Documentation & Formatting Rules
- **NEVER** use full stops or periods (句号) in markdown documentation or README files
- **NEVER** use emojis in markdown documentation
- Keep responses and explanations concise and direct
