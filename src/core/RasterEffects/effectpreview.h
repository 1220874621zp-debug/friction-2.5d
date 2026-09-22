/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

#ifndef EFFECTPREVIEW_H
#define EFFECTPREVIEW_H

class RasterEffect;
enum class RasterEffectType : short;

#include "core_global.h"
#include <QImage>
#include <QSize>

// 可视化特效面板的预览帧生成器：为一个内置光栅特效渲染循环
// 播放的演示帧序列。全部在 CPU 上离屏完成（效果Caller 的
// processCpu 通道），不依赖场景、画布、GUI 或 GL 上下文，
// 因此可以安全地在 QtConcurrent 工作线程中并发调用。
namespace EffectPreview {

// 该特效类型是否支持离屏预览（backdrop 采样型特效需要下方
// 画布合成内容，无法在单图层预览中表达）
CORE_EXPORT bool canPreview(const RasterEffectType type);

// 渲染 nFrames 帧循环演示动画；失败时返回空列表（调用方显示
// 占位图）。imgSize 建议 160x160 上下。
CORE_EXPORT QList<QImage> renderEffectFrames(const RasterEffectType type,
                                             const int nFrames,
                                             const QSize& imgSize);

}

#endif // EFFECTPREVIEW_H
