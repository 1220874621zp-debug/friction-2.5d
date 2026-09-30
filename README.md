# Friction 2.5D

[![Windows](https://github.com/friction2d/friction/actions/workflows/windows.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/windows.yml?query=branch%3Amain) [![Linux](https://github.com/friction2d/friction/actions/workflows/linux.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/linux.yml?query=branch%3Amain) [![macOS](https://github.com/friction2d/friction/actions/workflows/macos.yml/badge.svg)](https://github.com/friction2d/friction/actions/workflows/macos.yml?query=branch%3Amain)

基于 [Friction](https://friction.graphics) 的深度定制版：一款兼具矢量与位图能力的 MG 动画应用，面向 Web 与视频创作。本分支长期融合 **After Effects / Moho / TVPaint** 的工作流经验，并内置 AI 协作接口。

<img width="1988" height="1080" alt="8月29日(1)" src="https://github.com/user-attachments/assets/08773cea-45d9-4771-a23f-9a872756b53b" />
<img width="1988" height="1080" alt="8月29日" src="https://github.com/user-attachments/assets/12b06cd3-9b08-40dc-aede-613e402c4abf" />
<img width="3842" height="2090" alt="ScreenShot_2026-08-26_213719_382" src="https://github.com/user-attachments/assets/3c2f9de3-17f6-4fc9-9d96-d49b1ef4aaf8" />
<img width="1988" height="1080" alt="8月29日(3)" src="https://github.com/user-attachments/assets/cf32b2e9-2da1-4f1a-8961-4c6c2e1ab82f" />
<img width="1988" height="1080" alt="8月29日(2)" src="https://github.com/user-attachments/assets/4ecc6773-7dfb-4d2e-89c6-261f6f39601a" />

## ✨ 功能总览

### 🎨 界面与工作流

- **主题系统** — 多套莫兰迪主题 / 经典深蓝 / 石墨 / 北欧苔原 / 京都晚秋等一键切换，全 UI 配色统一
- **中文本地化** — 界面全面汉化（1600+ 条），首选项支持中英切换
- **面板系统** — AE 式停靠合并（标签带入口）、脚本浮窗面板（懒创建 + 工作区记忆）、项目面板（场景 + 素材统一管理）
- **队列面板** — 渲染队列卡片式管理，完成后可直接播放预览
- **顶视图** — AE 式顶视图：单级网格 / 深度轴线 / 相机线框足迹，可直接拖动相机
- **画布增强** — 标尺与可拖参考线、透明网格切换、快照导出、安全框、灰黑渐变工作区背景、**临时画布模式**（位置临时 / 结构永久）
- **快捷面板** — 快捷键面板、运动曲线快捷面板、快捷特效搜索（Ctrl+Space 胶囊浮窗，逐词模糊匹配）

### 🎬 图层与动画

- **2.5D 图层** — X/Y 轴旋转、Z 深度排序、透视强度，3D 开关一键切换，绕自身轴心的正确变换
- **骨骼系统** — Moho 式 FK 链 / 刚体绑定 / 冻结姿势 / 拖拽自动 K 帧
- **摄像机** — AE 式摄像机层（轨道 / 平移 / 缩放 / 焦距），跨图层 3D 联动，视差生成器深度集成
- **切换面板** — 复刻 Moho 切换器，口型动画 / 动作切换原生面板，性能优于脚本方案
- **可见性关键帧** — 图层小眼睛可记录动画（AE 没有的能力），为深度自动化打基础
- **图层样式** — PS 式复合特效（描边等），支持 PSD lfxp 导入
- **蒙版系统** — 钢笔 / 矩形工具在图像层自动建蒙版，AE 式 TrkMat 轨道遮罩（蒙版源自动隐藏 / 擦除 / 相加相减 / 羽化）
- **批量与多选** — 多选行内开关、图层自然数序号、Ctrl+Alt 阶梯错开与整层平移、框选穿组
- **固态层 / 调整图层 / 空对象** — AE 语义完整

### 🧰 生成器与脚本引擎

- **AE 脚本兼容引擎** — jsapi 对标 ExtendScript，AE 脚本简单移植即可运行；表达式桥（`setExpression`）、`$frame` / `$camera` / `$path` 绑定源、父子绑定（pick-whip）、共享路径、自定义滑块属性
- **地图划线** — CEP 版全功能移植：三模式线型 / 虚线流动 / 端点形状 / 道路框法线偏移 / 实时预览
- **视差生成器** — 逐层 2.5D 相机矩阵（Parallaxer 语义），原生摄像机 + 针孔投影，一键烘焙
- **天平生成器** — AE 版移植：秤杆角度表达式 + 平台挂杆保持水平，关键词自动识别
- **轮播三合一** — 水平环 / 竖直环 / 线性，参数上控制器可 K 帧，滑杆实时预览
- **自动化图层工具集** — 表达式注入、卡片缩放、批量动效

### 🤖 AI

- **AI 深度估计** — Depth Anything V2 本机推理（三档模型 7.6s~640MB），帧批量 + 中值 / EMA 时序平滑，等比插入画布
- **AI 动效引擎 + MCP 工具链** — 多态图层寻址、递归遍历、复合动效、事务隔离；HTTP 鉴权 + Bearer 令牌；声明式 MG 排版原语；**Lottie AI 创作工具**（提示词直接生成 Lottie 动画，画风参考句式）
- **动效预设库** — 文字 / 图层动画预设 **230 种**（含 AE 码头人式预设面板，胶囊分类筛选）

### 📥 导入导出

- **FFmpeg 9 视频引擎** — 内置 FFmpeg 由 4.2 整体升级至 **9.0.2**（全套新 API 移植：AVChannelLayout 声道布局、avcodec_get_supported_config 编码器能力查询、av_packet 全托管生命周期），视频导入 / 导出的编解码支持全面更新
- **Lottie 支持** — Bodymovin JSON / .lottie 导入（DaVinci 式逐图层还原：变换/形状/混合全映射为原生图层，导入自动居中），可继续编辑
- **PSD 分层导入** — 图层 / 组 / 混合模式 / 剪贴蒙版 / 图层样式
- **Krita .kra 原生导入** — 绘画层静态 + 逐关键帧动画、组递归、混合模式、帧率画布
- **OCA 导入** — 开放动画格式，与 OpenToonz / Krita 互通
- **文件对话框缩略图** — 常规图自解码，PSD / PSB 走系统解码器
- **矢量描摹** — vtracer 位图转矢量，菜单栏独立入口

### ✂️ 特效

- **特效画布手柄** — AE 式参数直接上画布拖：阴影 / 变焦模糊的偏移量拖拽调节，带虚线引导线，位移一眼可读
- **晶格变形 LatticeWarp** — 网格晶格扭曲特效，控制点画布可拖
- **液态玻璃** — 分流型折射特效（锐边高光可调）
- **扣绿 Chroma Key** — 移植 Enhanced Hybrid Keyer 3.1（MIT），13 参数 4 键控法
- **黑白闪烁 BWF / 像素化 Pixelate** — 按 AE 插件源码忠实移植
- **AE 特效家族 20+** — 暗角 / 色差 / 信箱 / 扫描线 / 方向模糊 / 径向模糊 / 波浪扭曲 / 雨 / 边缘检测 / 反转 / 噪波 / 镜像 / 故障 / 色调分离 / 旋涡 / 半调 / 抖动 / 条纹 / 运动平铺 / 分形噪波 / 光扫 / 位移扭曲 / 胶片颗粒……

### ⏱ 时间轴

- **TVP 风格改造** — 块语义操作 / 块缘 trim / 一拍 N 碰撞推挤 / 末尾块拖长实体化
- **图层即轨道** — 同类折叠单行、拖拽合并 / Alt 分离、轨道遮罩列
- **实用改进** — XY 等比缩放链接、匹配画布按钮、输出预设下拉、场景对话框时间码表单、播放栏全面汉化

## 本地开发

**部署位置：** `friction/build/output/friction-cn-test/friction.exe`

## Contribute

We accept any contributions, big or small. Before submitting a pull request it's recommended that you communicate with the developers first (on [GitHub](https://github.com/friction2d/friction/issues) or [Codeberg](https://codeberg.org/friction/friction/issues))

It's always preferred to submit pull requests against the `main` branch

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
