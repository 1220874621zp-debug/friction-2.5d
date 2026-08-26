# Friction

[![Windows](https://github.com/friction2d/friction/actions/workflows/windows.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/windows.yml?query=branch%3Amain) [![Linux](https://github.com/friction2d/friction/actions/workflows/linux.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/linux.yml?query=branch%3Amain) [![macOS](https://github.com/friction2d/friction/actions/workflows/macos.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/macos.yml?query=branch%3Amain)

[Friction](https://friction.graphics) is a powerful and versatile motion graphics application that allows you to create vector and raster animations for web and video.
<img width="3842" height="2090" alt="ScreenShot_2026-08-26_213719_382" src="https://github.com/user-attachments/assets/3c2f9de3-17f6-4fc9-9d96-d49b1ef4aaf8" />
<img width="3842" height="2090" alt="ScreenShot_2026-08-26_213640_298" src="https://github.com/user-attachments/assets/c6302683-41da-498c-9f5c-0895425f5828" />



## Contribute

We accept any contributions, big or small. Before submitting a pull request it's recommended that you communicate with the developers first (on [GitHub](https://github.com/friction2d/friction/issues) or [Codeberg](https://codeberg.org/friction/friction/issues)).

It's always preferred to submit pull requests against the `main` branch.

## Branches and versions


- 实现2.5D图层功能（X/Y/Z旋转、透视、Z排序）
- 添加3D开关按钮及重置按钮
- 修复3D属性名乱码
- 修复图标模糊问题
- 按用户要求优先使用文字字符做UI图标

用户现在只是给出了一个总结，没有提出新的需求。我应该确认当前状态，并询问用户是否需要进一步的帮助或有新的需求。
根据之前的工作记录，Friction 2.5D图层功能的所有需求已全部完成并部署：

**已完成的功能：**

1. ✅ **2.5D图层变换** - 实现了类似AE的轻量3D图层功能，图层为平面面片Billboard，支持：
   - X/Y轴旋转（模拟3D透视效果）
   - Z轴位移（深度排序）
   - 透视强度参数调节
   - 正确的绕自身轴心变换（修复了矩阵合成顺序：3D本地空间 → 2D变换 → 父级变换）

2. ✅ **3D开关按钮** - 在图层面板上添加3D开关，使用⬢（开启）/⬡（关闭）Unicode字符图标

3. ✅ **参数重置按钮** - 每个属性添加重置按钮，使用↻字符图标

4. ✅ **3D属性汉化修复** - 将属性名从乱码修复为正确的中文显示：
   - 3D 旋转 X
   - 3D 旋转 Y
   - 3D 位移 Z
   - 3D 透视
   （根因：`tr()`窄字符串在MSVC GBK编码下转义错误，改用`QStringLiteral`宽字符串解决）

5. ✅ **图标清晰度优化** - 所有图标采用Unicode矢量字符渲染，配合HiDPI适配（按设备像素比渲染），避免缩放模糊

**部署位置：** `friction/build/output/friction-cn-test/friction.exe`

-

## Documentation

See https://friction.graphics/documentation for generic documentation.

### Build instructions

* [Linux](https://friction.graphics/documentation/source-linux.html)
* [Windows](https://friction.graphics/documentation/source-windows.html)
* [macOS](https://friction.graphics/documentation/source-macos.html)

## License (GPL-3.0-only)

Friction is copyright &copy; Ole-André Rodlie and contributors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the [GNU General Public License](LICENSE.md) for more details.**

Friction is based on [enve](https://github.com/MaurycyLiebner/enve) - Copyright &copy; Maurycy Liebner and contributors.

Third-party software may contain other OSS licenses, see 'Help' > 'About' > 'Licenses' in Friction.

Source code for third-party software can be downloaded [here](https://download.friction.graphics/distfiles/).
