# Friction

[![Windows](https://github.com/friction2d/friction/actions/workflows/windows.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/windows.yml?query=branch%3Amain) [![Linux](https://github.com/friction2d/friction/actions/workflows/linux.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/linux.yml?query=branch%3Amain) [![macOS](https://github.com/friction2d/friction/actions/workflows/macos.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/macos.yml?query=branch%3Amain)

[Friction](https://friction.graphics) is a powerful and versatile motion graphics application that allows you to create vector and raster animations for web and video
<img width="1988" height="1080" alt="8月29日(1)" src="https://github.com/user-attachments/assets/08773cea-45d9-4771-a23f-9a872756b53b" />
<img width="1988" height="1080" alt="8月29日" src="https://github.com/user-attachments/assets/12b06cd3-9b08-40dc-aede-613e402c4abf" />
<img width="3842" height="2090" alt="ScreenShot_2026-08-26_213719_382" src="https://github.com/user-attachments/assets/3c2f9de3-17f6-4fc9-9d96-d49b1ef4aaf8" />
<img width="1988" height="1080" alt="8月29日(3)" src="https://github.com/user-attachments/assets/cf32b2e9-2da1-4f1a-8961-4c6c2e1ab82f" />
<img width="1988" height="1080" alt="8月29日(2)" src="https://github.com/user-attachments/assets/4ecc6773-7dfb-4d2e-89c6-261f6f39601a" />

## Contribute

We accept any contributions, big or small. Before submitting a pull request it's recommended that you communicate with the developers first (on [GitHub](https://github.com/friction2d/friction/issues) or [Codeberg](https://codeberg.org/friction/friction/issues))

It's always preferred to submit pull requests against the `main` branch

## Branches and versions

- 实现2.5D图层功能（X/Y/Z旋转、透视、Z排序）
- 添加3D开关按钮及重置按钮
- 修复3D属性名乱码
- 修复图标模糊问题

**已完成的功能：**

1. **2.5D图层变换** - 实现了类似AE的轻量3D图层功能，图层为平面面片Billboard，支持：
   - X/Y轴旋转（模拟3D透视效果）
   - Z轴位移（深度排序）
   - 透视强度参数调节
   - 正确的绕自身轴心变换（修复了矩阵合成顺序：3D本地空间 → 2D变换 → 父级变换）

2. **3D开关按钮** - 在图层面板上添加3D开关，使用⬢（开启）/⬡（关闭）Unicode字符图标

3. **参数重置按钮** - 每个属性添加重置按钮，使用↻字符图标

4. **3D属性汉化修复** - 将属性名从乱码修复为正确的中文显示：
   - 3D 旋转 X
   - 3D 旋转 Y
   - 3D 位移 Z
   - 3D 透视
   （根因：`tr()`窄字符串在MSVC GBK编码下转义错误，改用`QStringLiteral`宽字符串解决）
5. **psd分层导入**
6. **快捷键面板** 
7. **主题模块**
8. **优化时间轴**
9. **脚本引擎系统** - 兼容ae脚本只需简单移植代码
10. **i18n汉化架构**
11. **运动曲线快捷面板**
12. **多个脚本内置**
13. **骨骼系统和工具** - 类似moho操作逻辑和绑定方式
14. **自由层级拖动** - 类似剪辑软件一样的自由层级，可自由合并和分解，拖动左侧拖出栏即可合并，按住alt拖动图层即可分解，也可以右键菜单
15. **层属性按钮完善** - 和ae一样的层属性按钮功能
16. **新增摄像机功能** - 类似ae摄像机
17. **中英文切换** - 支持中文，英文切换，首选项可设置语言
18. **新增色轮** - 填色收藏持久化
19. **透明网格切换，快照导出，安全框** - 类似ae的功能
20. **图层眼睛可见动画记录** - 可见属性可记录动画，这是ae没有的功能，方便后续的深度开发作为基础
21. **骨骼动画自动记录关键帧** - 类似moho的操作体验
22. **新增固态层**
23. **特效模块加强** - 由协助者移植开发，我做的ae特效黑白闪，像素化，移植到了friction

**部署位置：** `friction/build/output/friction-cn-test/friction.exe`

## Documentation

See https://friction.graphics/documentation for generic documentation

### Build instructions

* [Linux](https://friction.graphics/documentation/source-linux.html)
* [Windows](https://friction.graphics/documentation/source-windows.html)
* [macOS](https://friction.graphics/documentation/source-macos.html)

## License (GPL-3.0-only)

Friction is copyright &copy; Ole-André Rodlie and contributors

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the [GNU General Public License](LICENSE.md) for more details**

Friction is based on [enve](https://github.com/MaurycyLiebner/enve) - Copyright &copy; Maurycy Liebner and contributors

Third-party software may contain other OSS licenses, see 'Help' > 'About' > 'Licenses' in Friction

Source code for third-party software can be downloaded [here](https://download.friction.graphics/distfiles/)
