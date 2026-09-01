# vtracer-ffi（矢量描摹 FFI 封装）

把开源位图描摹库 [vtracer 0.6.5](https://github.com/visioncortex/vtracer)（MIT）
封装成 C ABI DLL，供 Friction 的「矢量描摹」功能运行时加载。

## 构建（需要 Rust MSVC 工具链）

```bash
cd src/vtracer-ffi
cargo build --release
cp target/release/vtracer.dll ../../sdk/bin/      # 构建期头文件目录
cp target/release/vtracer.dll <部署目录>/          # 与 friction.exe 同目录
```

- 产物：`vtracer.dll`（约 300 KB，QLibrary 动态加载，缺失不影响程序启动）
- 头文件：`include/vtracer/vtracer_ffi.h`（C ABI 说明，随本仓库分发）
- C++ 侧接入：`src/core/vtracerprovider.h/.cpp`（QLibrary 解析 + Skia 像素转换）

## 自适应路径上限

`vtracer_trace_rgba` 的 `max_paths` 参数：结果超限时自动收紧
filter_speckle / color_precision / layer_difference 重试最多 4 轮；
仍超限返回 status=1（C++ 侧按"图像过于复杂"拦截，用于劝退插画/照片类图像）。
