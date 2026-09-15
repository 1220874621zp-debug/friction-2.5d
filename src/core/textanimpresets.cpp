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

#include "textanimpresets.h"
#include "textanimdebug.h"

#include "Boxes/textbox.h"
#include "Boxes/textboxrenderdata.h"
#include "Animators/texteffectcollection.h"
#include "Animators/qstringanimator.h"

#include "skia/skqtconversions.h"
#include "skia/skiaincludes.h"

#include <QDir>
#include "Private/esettings.h"
#include "Private/document.h"
#include "Private/Tasks/taskscheduler.h"
#include <cstdio>
#include <QImage>
#include <QTransform>
#include <QRectF>

namespace {
QList<TextAnimPreset> gPresets;

void addPreset(const TextAnimPreset& p)
{
    gPresets << p;
}

struct PresetBuilder {
    TextAnimPreset p;
    PresetBuilder& in() { p.category = 0; return *this; }
    PresetBuilder& out() {
        p.category = 1;
        p.kind = TextAnim::sweepOut;
        return *this;
    }
    PresetBuilder& loop() { p.category = 2; return *this; }
    PresetBuilder& letters() { p.fragment = 0; return *this; }
    PresetBuilder& words() { p.fragment = 1; return *this; }
    PresetBuilder& lines() { p.fragment = 2; return *this; }
    PresetBuilder& tag(const QString& t) { p.tag = t; return *this; }
    PresetBuilder& pos(const qreal x, const qreal y)
    { p.posX = x; p.posY = y; return *this; }
    PresetBuilder& rot(const qreal r)
    { p.rot = r; return *this; }
    PresetBuilder& scale(const qreal s)
    { p.scaleX = s; p.scaleY = s; return *this; }
    PresetBuilder& scaleX(const qreal sx)
    { p.scaleX = sx; return *this; }
    PresetBuilder& scaleY(const qreal sy)
    { p.scaleY = sy; return *this; }
    PresetBuilder& scaleXY(const qreal sx, const qreal sy)
    { p.scaleX = sx; p.scaleY = sy; return *this; }
    PresetBuilder& shear(const qreal sx, const qreal sy = 0)
    { p.shearX = sx; p.shearY = sy; return *this; }
    PresetBuilder& opacity(const qreal o)
    { p.opacity = o; return *this; }
    PresetBuilder& center()
    { p.pivotCenter = true; return *this; }
    PresetBuilder& dur(const qreal d)
    { p.duration = d; return *this; }
    PresetBuilder& soft(const qreal s)
    { p.softness = s; return *this; }
    PresetBuilder& rtl()
    { p.direction = TextAnimDirection::rightToLeft; return *this; }
    PresetBuilder& easing(const TextEasing e)
    { p.easing = e; return *this; }
    PresetBuilder& stagger(const qreal s)
    { p.staggerPercent = s; return *this; }
    PresetBuilder& wave(const int cycles, const qreal periodSec)
    { p.kind = TextAnim::wave; p.waveCycles = cycles; p.waveTime = periodSec; return *this; }
    PresetBuilder& pulse(const qreal peak = 1.)
    { p.kind = TextAnim::pulse; p.pulsePeak = peak; return *this; }
};

void ensurePresets()
{
    if (!gPresets.isEmpty()) { return; }
    const auto B = [](const char* const id, const QString& name,
                      const QString& desc) {
        PresetBuilder b;
        b.p.id = id;
        b.p.name = name;
        b.p.desc = desc;
        return b;
    };

    // =========================================================================
    // 1. 锐利瞬态与冲击动力学（Kinetic Sharp - 25 项）
    // =========================================================================
    addPreset(B("sharp-snap-rise", QString::fromUtf8("迅猛上冲"),
                QString::fromUtf8("从下方疾速上冲并带有硬朗刹车感"))
              .in().letters().tag("sharp").pos(0, 90).opacity(0).dur(0.6).soft(0.12)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("sharp-snap-drop", QString::fromUtf8("急坠刹车"),
                QString::fromUtf8("高空垂直极速俯冲并在着陆点骤停"))
              .in().letters().tag("sharp").pos(0, -110).opacity(0).dur(0.6).soft(0.12)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("sharp-snap-left", QString::fromUtf8("左侧疾冲"),
                QString::fromUtf8("自左侧高速瞬移滑入刹车"))
              .in().letters().tag("sharp").pos(-130, 0).opacity(0).dur(0.6).soft(0.12)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("sharp-snap-right", QString::fromUtf8("右侧疾冲"),
                QString::fromUtf8("自右侧高速瞬移滑入刹车"))
              .in().letters().tag("sharp").pos(130, 0).opacity(0).dur(0.6).soft(0.12)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("sharp-overshoot-up", QString::fromUtf8("上冲回弹"),
                QString::fromUtf8("大位移向上冲过头后轻微反弹就位"))
              .in().letters().tag("sharp").pos(0, 100).scaleXY(0.9, 1.2).opacity(0).dur(0.7).soft(0.15)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("sharp-overshoot-down", QString::fromUtf8("重力回弹"),
                QString::fromUtf8("向下重重砸落后反弹复位"))
              .in().letters().tag("sharp").pos(0, -120).scaleXY(1.2, 0.8).opacity(0).dur(0.75).soft(0.15)
              .easing(TextEasing::bounce).stagger(0.45).p);
    addPreset(B("sharp-elastic-pop", QString::fromUtf8("高弹爆破"),
                QString::fromUtf8("从零极速膨胀并伴随剧烈弹性振荡"))
              .in().letters().tag("sharp").scale(0).opacity(0).center().dur(0.65).soft(0.12)
              .easing(TextEasing::elastic).stagger(0.45).p);
    addPreset(B("sharp-squash-h", QString::fromUtf8("横向挤压弹"),
                QString::fromUtf8("横向拉伸3倍后像皮筋一样骤然收紧弹定"))
              .in().letters().tag("sharp").scaleXY(3.2, 0.3).opacity(0).center().dur(0.7).soft(0.15)
              .easing(TextEasing::elastic).stagger(0.40).p);
    addPreset(B("sharp-squash-v", QString::fromUtf8("纵向重压弹"),
                QString::fromUtf8("纵向极度压扁贴地后猛烈反弹站立"))
              .in().letters().tag("sharp").scaleXY(0.3, 3.2).pos(0, -60).opacity(0).center().dur(0.75).soft(0.15)
              .easing(TextEasing::bounce).stagger(0.45).p);
    addPreset(B("sharp-whip-twist", QString::fromUtf8("鞭击抽打"),
                QString::fromUtf8("快速大幅度旋转并伴随位移抽击落定"))
              .in().letters().tag("sharp").rot(75).pos(-80, 40).opacity(0).center().dur(0.65).soft(0.12)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("sharp-whip-ccw", QString::fromUtf8("反向鞭甩"),
                QString::fromUtf8("逆时针高速反抽甩入视界"))
              .in().letters().tag("sharp").rot(-75).pos(80, 40).opacity(0).center().dur(0.65).soft(0.12)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("sharp-slam-front", QString::fromUtf8("正面撞击"),
                QString::fromUtf8("从超大超近视距猛烈撞击砸入画面"))
              .in().letters().tag("sharp").scale(4.0).opacity(0).center().dur(0.55).soft(0.1)
              .easing(TextEasing::bounce).stagger(0.30).p);
    addPreset(B("sharp-blade-cut", QString::fromUtf8("利刃切入"),
                QString::fromUtf8("大角度斜切配合横向高速划割入场"))
              .in().letters().tag("sharp").pos(-150, 0).shear(1.0, 0).opacity(0).dur(0.55).soft(0.1)
              .easing(TextEasing::sharpSnap).stagger(0.30).p);
    addPreset(B("sharp-blade-slash", QString::fromUtf8("纵向斜斩"),
                QString::fromUtf8("垂直方向极速斜斩下切"))
              .in().letters().tag("sharp").pos(0, -100).shear(0, 0.8).opacity(0).dur(0.6).soft(0.12)
              .easing(TextEasing::sharpSnap).stagger(0.30).p);
    addPreset(B("sharp-stagger-impact", QString::fromUtf8("顿挫重击"),
                QString::fromUtf8("几乎无羽化的硬性顿挫逐字砸落"))
              .in().letters().tag("sharp").pos(0, 70).opacity(0).dur(0.5).soft(0.04)
              .easing(TextEasing::bounce).stagger(0.55).p);
    addPreset(B("sharp-rebound-diag", QString::fromUtf8("对角弹射"),
                QString::fromUtf8("沿45度对角线高速暴射进入反弹"))
              .in().letters().tag("sharp").pos(-100, 100).rot(15).opacity(0).center().dur(0.65).soft(0.14)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("sharp-pop-jitter", QString::fromUtf8("硬脆爆字"),
                QString::fromUtf8("零尺寸微小点瞬间炸裂绽放"))
              .in().letters().tag("sharp").scale(0.05).rot(30).opacity(0).center().dur(0.5).soft(0.08)
              .easing(TextEasing::elastic).stagger(0.40).p);
    addPreset(B("sharp-punch-burst", QString::fromUtf8("重拳出击"),
                QString::fromUtf8("极速前突放大并伴随缩放震荡"))
              .in().letters().tag("sharp").scaleXY(0.4, 0.4).pos(0, 30).opacity(0).center().dur(0.6).soft(0.1)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("sharp-scissor-kick", QString::fromUtf8("剪刀错位"),
                QString::fromUtf8("上下反向错位猛力咬合平正"))
              .in().letters().tag("sharp").pos(0, -60).shear(-0.7, 0).opacity(0).dur(0.65).soft(0.12)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("sharp-spring-rise", QString::fromUtf8("弹簧跃起"),
                QString::fromUtf8("自底部像紧压的弹簧猛然释放跳起"))
              .in().letters().tag("sharp").pos(0, 120).scaleXY(0.7, 1.4).opacity(0).center().dur(0.7).soft(0.15)
              .easing(TextEasing::elastic).stagger(0.45).p);
    addPreset(B("sharp-snap-word-rise", QString::fromUtf8("逐词弹射"),
                QString::fromUtf8("单词为单位的高速强力弹跃入场"))
              .in().words().tag("sharp").pos(0, 80).opacity(0).dur(0.7).soft(0.15)
              .easing(TextEasing::overshoot).stagger(0.45).p);
    addPreset(B("sharp-snap-word-drop", QString::fromUtf8("逐词重砸"),
                QString::fromUtf8("单词自上方高空高速俯冲砸落"))
              .in().words().tag("sharp").pos(0, -100).scaleXY(1.2, 0.8).opacity(0).center().dur(0.7).soft(0.15)
              .easing(TextEasing::bounce).stagger(0.45).p);
    addPreset(B("sharp-snap-word-slam", QString::fromUtf8("逐词撞镜"),
                QString::fromUtf8("单词近景超大比例极速拍打在屏幕上"))
              .in().words().tag("sharp").scale(3.5).opacity(0).center().dur(0.6).soft(0.12)
              .easing(TextEasing::bounce).stagger(0.40).p);
    addPreset(B("sharp-snap-line-whip", QString::fromUtf8("逐行鞭扫"),
                QString::fromUtf8("整行文本高速斜切鞭甩就位"))
              .in().lines().tag("sharp").pos(-180, 0).shear(0.6, 0).opacity(0).dur(0.8).soft(0.15)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("sharp-snap-line-slam", QString::fromUtf8("逐行重扣"),
                QString::fromUtf8("多行文本依次垂直暴扣砸定"))
              .in().lines().tag("sharp").pos(0, -110).opacity(0).dur(0.75).soft(0.12)
              .easing(TextEasing::bounce).stagger(0.45).p);

    // =========================================================================
    // 2. 丝滑流体与影视长尾（Kinetic Smooth - 20 项）
    // =========================================================================
    addPreset(B("smooth-float-rise", QString::fromUtf8("云雾升腾"),
                QString::fromUtf8("如云烟般超柔和长距离平滑袅袅升起"))
              .in().letters().tag("smooth").pos(0, 80).opacity(0).dur(1.8).soft(0.65)
              .easing(TextEasing::smooth).stagger(0.60).p);
    addPreset(B("smooth-float-sink", QString::fromUtf8("落羽沉降"),
                QString::fromUtf8("宛如轻羽在微风中慢速摇曳下落"))
              .in().letters().tag("smooth").pos(0, -80).opacity(0).dur(1.8).soft(0.65)
              .easing(TextEasing::smooth).stagger(0.60).p);
    addPreset(B("smooth-glide-left", QString::fromUtf8("丝绸左滑"),
                QString::fromUtf8("超大羽化丝滑自左向右长距蔓延"))
              .in().letters().tag("smooth").pos(-120, 0).opacity(0).dur(1.6).soft(0.55)
              .easing(TextEasing::smooth).stagger(0.55).p);
    addPreset(B("smooth-glide-right", QString::fromUtf8("丝绸右滑"),
                QString::fromUtf8("超大羽化丝滑自右向左长距蔓延"))
              .in().letters().tag("smooth").pos(120, 0).opacity(0).dur(1.6).soft(0.55)
              .easing(TextEasing::smooth).stagger(0.55).p);
    addPreset(B("smooth-cinematic-fade", QString::fromUtf8("胶片渐显"),
                QString::fromUtf8("电影级超平滑长曝光逐渐显现"))
              .in().letters().tag("smooth").opacity(0).dur(2.2).soft(0.7)
              .easing(TextEasing::smooth).stagger(0.65).p);
    addPreset(B("smooth-focus-zoom", QString::fromUtf8("景深推镜"),
                QString::fromUtf8("轻柔镜头微推，伴随渐变显露"))
              .in().letters().tag("smooth").scale(0.6).opacity(0).center().dur(1.6).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-drift-diag-tl", QString::fromUtf8("左上幽浮"),
                QString::fromUtf8("自左上方深邃空间斜向平缓飘浮而来"))
              .in().letters().tag("smooth").pos(-70, -70).opacity(0).dur(1.7).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-drift-diag-tr", QString::fromUtf8("右上幽浮"),
                QString::fromUtf8("自右上方深邃空间斜向平缓飘浮而来"))
              .in().letters().tag("smooth").pos(70, -70).opacity(0).dur(1.7).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-drift-diag-bl", QString::fromUtf8("左下漫步"),
                QString::fromUtf8("自左下方悠缓漫步升上地平线"))
              .in().letters().tag("smooth").pos(-70, 70).opacity(0).dur(1.7).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-drift-diag-br", QString::fromUtf8("右下漫步"),
                QString::fromUtf8("自右下方悠缓漫步升上地平线"))
              .in().letters().tag("smooth").pos(70, 70).opacity(0).dur(1.7).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-gentle-tilt", QString::fromUtf8("微风轻抚"),
                QString::fromUtf8("带有轻微倾角与微弱位移的优雅拂入"))
              .in().letters().tag("smooth").pos(0, 30).shear(0.25, 0).opacity(0).dur(1.5).soft(0.45)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-twirl-bloom", QString::fromUtf8("花瓣旋放"),
                QString::fromUtf8("低转角慢速旋转绽放展开"))
              .in().letters().tag("smooth").rot(30).scale(0.5).opacity(0).center().dur(1.6).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-waterfall", QString::fromUtf8("水幔倾泻"),
                QString::fromUtf8("逐字如同瀑布水流柔美延绵垂挂"))
              .in().letters().tag("smooth").pos(0, -60).scaleXY(0.9, 1.2).opacity(0).dur(1.7).soft(0.55)
              .easing(TextEasing::smooth).stagger(0.55).p);
    addPreset(B("smooth-aurora", QString::fromUtf8("极光漫卷"),
                QString::fromUtf8("宽阔光带般在文本间轻柔流淌点亮"))
              .in().letters().tag("smooth").pos(-40, 20).opacity(0).dur(2.0).soft(0.75)
              .easing(TextEasing::smooth).stagger(0.65).p);
    addPreset(B("smooth-word-float", QString::fromUtf8("逐词幽浮"),
                QString::fromUtf8("单词单元的长尾优雅徐徐升起"))
              .in().words().tag("smooth").pos(0, 60).opacity(0).dur(1.6).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.55).p);
    addPreset(B("smooth-word-glide", QString::fromUtf8("逐词流光"),
                QString::fromUtf8("单词单元依次自左侧滑出并展开"))
              .in().words().tag("smooth").pos(-90, 0).opacity(0).dur(1.5).soft(0.45)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-word-bloom", QString::fromUtf8("逐词绽放"),
                QString::fromUtf8("单词居中舒缓放大显现"))
              .in().words().tag("smooth").scale(0.4).opacity(0).center().dur(1.5).soft(0.45)
              .easing(TextEasing::smooth).stagger(0.50).p);
    addPreset(B("smooth-line-rise", QString::fromUtf8("逐行升腾"),
                QString::fromUtf8("多行段落依次优雅上浮展开"))
              .in().lines().tag("smooth").pos(0, 60).opacity(0).dur(1.8).soft(0.55)
              .easing(TextEasing::smooth).stagger(0.55).p);
    addPreset(B("smooth-line-fade", QString::fromUtf8("逐行漫溢"),
                QString::fromUtf8("多行文字层层递进如墨色漫溢"))
              .in().lines().tag("smooth").opacity(0).dur(1.8).soft(0.6)
              .easing(TextEasing::smooth).stagger(0.60).p);
    addPreset(B("smooth-line-slide", QString::fromUtf8("逐行徐进"),
                QString::fromUtf8("多行排版依次优雅横向推入"))
              .in().lines().tag("smooth").pos(-120, 0).opacity(0).dur(1.7).soft(0.5)
              .easing(TextEasing::smooth).stagger(0.55).p);

    // =========================================================================
    // 3. 通道属性分离几何（Channel Properties - 25 项）
    // =========================================================================
    addPreset(B("prop-pos-x-left", QString::fromUtf8("纯X轴-左向进入"),
                QString::fromUtf8("严格锁定水平X轴，纯位移无缩放旋转"))
              .in().letters().tag("prop").pos(-140, 0).dur(1.0).soft(0.3)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("prop-pos-x-right", QString::fromUtf8("纯X轴-右向进入"),
                QString::fromUtf8("严格锁定水平X轴，自右侧纯平移进入"))
              .in().letters().tag("prop").pos(140, 0).dur(1.0).soft(0.3)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("prop-pos-y-up", QString::fromUtf8("纯Y轴-底部上升"),
                QString::fromUtf8("严格锁定垂直Y轴，纯高度抬升就位"))
              .in().letters().tag("prop").pos(0, 90).dur(1.0).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("prop-pos-y-down", QString::fromUtf8("纯Y轴-顶部降落"),
                QString::fromUtf8("严格锁定垂直Y轴，纯高度下降就位"))
              .in().letters().tag("prop").pos(0, -90).dur(1.0).soft(0.3)
              .easing(TextEasing::bounce).stagger(0.40).p);
    addPreset(B("prop-scale-uniform", QString::fromUtf8("纯缩放-全尺寸扩张"),
                QString::fromUtf8("居中纯等比缩放从0放大至100%"))
              .in().letters().tag("prop").scale(0).center().dur(1.0).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("prop-scale-shrink", QString::fromUtf8("纯缩放-远景缩小"),
                QString::fromUtf8("居中纯等比缩放从2.5倍收缩至标准"))
              .in().letters().tag("prop").scale(2.5).center().dur(1.1).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("prop-scale-stretch-x", QString::fromUtf8("纯比例-横向拉宽"),
                QString::fromUtf8("垂直高度不变，水平拉宽4倍聚拢"))
              .in().letters().tag("prop").scaleXY(4.0, 1.0).center().dur(1.1).soft(0.35)
              .easing(TextEasing::elastic).stagger(0.40).p);
    addPreset(B("prop-scale-squeeze-y", QString::fromUtf8("纯比例-纵向拉长"),
                QString::fromUtf8("水平宽度不变，垂直拉长4倍收缩"))
              .in().letters().tag("prop").scaleXY(1.0, 4.0).center().dur(1.1).soft(0.35)
              .easing(TextEasing::elastic).stagger(0.40).p);
    addPreset(B("prop-scale-line-x", QString::fromUtf8("纯比例-横线延展"),
                QString::fromUtf8("文字高度压缩为0呈细线横向展开"))
              .in().letters().tag("prop").scaleXY(0, 1).center().dur(1.0).soft(0.25)
              .easing(TextEasing::sharpSnap).stagger(0.30).p);
    addPreset(B("prop-scale-bar-y", QString::fromUtf8("纯比例-坚柱立起"),
                QString::fromUtf8("文字宽度压缩为0呈细柱纵向撑开"))
              .in().letters().tag("prop").scaleXY(1, 0).center().dur(1.0).soft(0.25)
              .easing(TextEasing::sharpSnap).stagger(0.30).p);
    addPreset(B("prop-rot-cw-90", QString::fromUtf8("纯旋转-顺转90度"),
                QString::fromUtf8("居中纯直角90度旋转摆正"))
              .in().letters().tag("prop").rot(90).center().dur(1.1).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("prop-rot-ccw-90", QString::fromUtf8("纯旋转-逆转90度"),
                QString::fromUtf8("居中纯负90度逆向旋转摆正"))
              .in().letters().tag("prop").rot(-90).center().dur(1.1).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("prop-rot-cw-180", QString::fromUtf8("纯旋转-倒置翻转"),
                QString::fromUtf8("纯180度半周回旋倒立翻转摆正"))
              .in().letters().tag("prop").rot(180).center().dur(1.2).soft(0.35)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("prop-rot-full-360", QString::fromUtf8("纯旋转-整周回旋"),
                QString::fromUtf8("纯360度整圈顺时针旋转进入"))
              .in().letters().tag("prop").rot(360).center().dur(1.3).soft(0.35)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("prop-rot-sway-45", QString::fromUtf8("纯旋转-45度倾摆"),
                QString::fromUtf8("45度对角倾摆复位"))
              .in().letters().tag("prop").rot(45).center().dur(1.0).soft(0.25)
              .easing(TextEasing::elastic).stagger(0.35).p);
    addPreset(B("prop-shear-slash-x", QString::fromUtf8("纯斜切-水平倾斜"),
                QString::fromUtf8("无旋转纯剪切力，倾角45度平移摆正"))
              .in().letters().tag("prop").shear(0.8, 0).dur(1.0).soft(0.3)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("prop-shear-back-x", QString::fromUtf8("纯斜切-反向倾斜"),
                QString::fromUtf8("反向水平剪切，倾角-45度摆正"))
              .in().letters().tag("prop").shear(-0.8, 0).dur(1.0).soft(0.3)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("prop-shear-vertical", QString::fromUtf8("纯斜切-纵向切片"),
                QString::fromUtf8("纵向剪切错位依次拉正"))
              .in().letters().tag("prop").shear(0, 0.8).dur(1.0).soft(0.3)
              .easing(TextEasing::sharpSnap).stagger(0.35).p);
    addPreset(B("prop-shear-cross", QString::fromUtf8("纯斜切-十字交叉"),
                QString::fromUtf8("双向同时剪切变形复原"))
              .in().letters().tag("prop").shear(0.5, 0.5).dur(1.1).soft(0.35)
              .easing(TextEasing::elastic).stagger(0.35).p);
    addPreset(B("prop-opacity-stagger", QString::fromUtf8("纯透明度-阶梯淡入"),
                QString::fromUtf8("位置比例完全静止，逐字阶梯渐显"))
              .in().letters().tag("prop").opacity(0).dur(1.0).soft(0.1)
              .easing(TextEasing::stepped).stagger(0.65).p);
    addPreset(B("prop-opacity-steep", QString::fromUtf8("纯透明度-硬切显现"),
                QString::fromUtf8("零过渡瞬间逐字亮起"))
              .in().letters().tag("prop").opacity(0).dur(0.8).soft(0.02)
              .easing(TextEasing::sharpSnap).stagger(0.50).p);
    addPreset(B("prop-opacity-glow", QString::fromUtf8("纯透明度-呼吸渐显"),
                QString::fromUtf8("超宽漫反射大羽化淡入"))
              .in().letters().tag("prop").opacity(0).dur(1.6).soft(0.6)
              .easing(TextEasing::smooth).stagger(0.65).p);
    addPreset(B("prop-word-scale", QString::fromUtf8("逐词通道-等比放大"),
                QString::fromUtf8("单词居中纯缩放显现"))
              .in().words().tag("prop").scale(0).center().dur(1.0).soft(0.25)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("prop-word-rot", QString::fromUtf8("逐词通道-直角转入"),
                QString::fromUtf8("单词顺时针90度转入"))
              .in().words().tag("prop").rot(90).center().dur(1.1).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("prop-line-shear", QString::fromUtf8("逐行通道-整体斜切"),
                QString::fromUtf8("整行文本高速斜切拉直"))
              .in().lines().tag("prop").shear(0.6, 0).dur(1.2).soft(0.3)
              .easing(TextEasing::sharpSnap).stagger(0.45).p);

    // =========================================================================
    // 4. 空间与透视维度（3D Perspective - 12 项）
    // =========================================================================
    addPreset(B("3d-flip-y-cw", QString::fromUtf8("3D翻转-横向顺转"),
                QString::fromUtf8("模拟绕Y轴3D立体卡片翻转入场"))
              .in().letters().tag("3d").scaleXY(-0.1, 1.0).center().dur(1.0).soft(0.25)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("3d-flip-y-ccw", QString::fromUtf8("3D翻转-横向逆转"),
                QString::fromUtf8("模拟绕Y轴逆向3D翻出就位"))
              .in().letters().tag("3d").scaleXY(0.01, 1.0).shear(-0.3, 0).center().dur(1.0).soft(0.25)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("3d-flip-x-down", QString::fromUtf8("3D翻折-百叶下翻"),
                QString::fromUtf8("模拟绕X轴水平轴线向下翻折展开"))
              .in().letters().tag("3d").scaleXY(1.0, -0.1).center().dur(1.0).soft(0.25)
              .easing(TextEasing::bounce).stagger(0.40).p);
    addPreset(B("3d-flip-x-up", QString::fromUtf8("3D翻折-百叶上翻"),
                QString::fromUtf8("模拟绕X轴水平轴线自下向上翻折"))
              .in().letters().tag("3d").scaleXY(1.0, 0.01).pos(0, 30).center().dur(1.0).soft(0.25)
              .easing(TextEasing::overshoot).stagger(0.35).p);
    addPreset(B("3d-perspective-drop", QString::fromUtf8("3D俯冲坠入"),
                QString::fromUtf8("大景深纵深透视自镜头高远深处俯冲砸下"))
              .in().letters().tag("3d").scale(3.2).pos(0, -90).opacity(0).center().dur(1.2).soft(0.3)
              .easing(TextEasing::bounce).stagger(0.40).p);
    addPreset(B("3d-perspective-rise", QString::fromUtf8("3D仰天飞出"),
                QString::fromUtf8("自地平面以下纵深透视角度破土冲出"))
              .in().letters().tag("3d").scale(0.15).pos(0, 100).opacity(0).center().dur(1.2).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("3d-tumble-diag", QString::fromUtf8("3D对角翻滚"),
                QString::fromUtf8("双轴混合立体翻滚入场"))
              .in().letters().tag("3d").scaleXY(-0.2, 0.2).rot(45).opacity(0).center().dur(1.2).soft(0.3)
              .easing(TextEasing::elastic).stagger(0.40).p);
    addPreset(B("3d-corkscrew", QString::fromUtf8("3D螺旋穿刺"),
                QString::fromUtf8("沿纵深轴旋转360度穿刺飞出"))
              .in().letters().tag("3d").scale(0.1).rot(360).opacity(0).center().dur(1.4).soft(0.35)
              .easing(TextEasing::anticipate).stagger(0.45).p);
    addPreset(B("3d-depth-zoom", QString::fromUtf8("3D空间推远"),
                QString::fromUtf8("自画面正前方极近处穿透后退至焦点"))
              .in().letters().tag("3d").scale(4.5).opacity(0).center().dur(1.1).soft(0.3)
              .easing(TextEasing::bounce).stagger(0.35).p);
    addPreset(B("3d-yaw-swing", QString::fromUtf8("3D偏航悬摆"),
                QString::fromUtf8("如挂在立轴上的广告牌3D悬摆定型"))
              .in().letters().tag("3d").rot(40).scaleXY(0.4, 1.0).opacity(0).center().dur(1.1).soft(0.28)
              .easing(TextEasing::elastic).stagger(0.40).p);
    addPreset(B("3d-word-flip", QString::fromUtf8("逐词3D翻转"),
                QString::fromUtf8("单词为单位绕纵轴依次3D翻面展现"))
              .in().words().tag("3d").scaleXY(-0.1, 1.0).center().dur(1.1).soft(0.28)
              .easing(TextEasing::overshoot).stagger(0.40).p);
    addPreset(B("3d-line-fold", QString::fromUtf8("逐行3D折叠"),
                QString::fromUtf8("多行文字如同三折页宣传册立体翻开"))
              .in().lines().tag("3d").scaleXY(1.0, -0.1).center().dur(1.2).soft(0.3)
              .easing(TextEasing::overshoot).stagger(0.45).p);

    // =========================================================================
    // 5. 极客终端与离散时序（Code & Terminal - 8 项）
    // =========================================================================
    addPreset(B("tech-typewriter-std", QString::fromUtf8("标准打字机"),
                QString::fromUtf8("经典终端逐字敲入，硬性光标级顿挫"))
              .in().letters().tag("tech").opacity(0).dur(1.5).soft(0.04)
              .easing(TextEasing::stepped).stagger(0.85).p);
    addPreset(B("tech-typewriter-fast", QString::fromUtf8("高速代码输入"),
                QString::fromUtf8("极速计算机黑客命令行逐字灌入"))
              .in().letters().tag("tech").opacity(0).dur(0.75).soft(0.02)
              .easing(TextEasing::stepped).stagger(0.85).p);
    addPreset(B("tech-typewriter-slow", QString::fromUtf8("机械击字机"),
                QString::fromUtf8("沉重慢速机械铅字逐字敲击印上纸面"))
              .in().letters().tag("tech").opacity(0).dur(2.5).soft(0.02)
              .easing(TextEasing::stepped).stagger(0.85).p);
    addPreset(B("tech-number-roll", QString::fromUtf8("数字滚动增长"),
                QString::fromUtf8("数字从 0 滚动增长到当前数值（保留前后缀），仅入点"))
              .in().letters().tag("tech").dur(1.6).p);
    gPresets.last().supportsOut = false;
    addPreset(B("tech-cursor-stream", QString::fromUtf8("光标流接入"),
                QString::fromUtf8("带有垂直微位移的数据流跳动录入"))
              .in().letters().tag("tech").pos(0, -18).opacity(0).dur(1.1).soft(0.06)
              .easing(TextEasing::sharpSnap).stagger(0.65).p);
    addPreset(B("tech-glitch-burst", QString::fromUtf8("故障瞬切"),
                QString::fromUtf8("数字故障偏折错位瞬间复原显现"))
              .in().letters().tag("tech").pos(12, -6).rot(5).opacity(0).dur(0.9).soft(0.06)
              .easing(TextEasing::stepped).stagger(0.70).p);
    addPreset(B("tech-matrix-rain", QString::fromUtf8("矩阵代码雨"),
                QString::fromUtf8("代码从天顶逐列疾速降落接入系统"))
              .in().letters().tag("tech").pos(0, -45).opacity(0).dur(1.2).soft(0.07)
              .easing(TextEasing::sharpSnap).stagger(0.75).p);
    addPreset(B("tech-cyber-step", QString::fromUtf8("阶梯二进制"),
                QString::fromUtf8("段落感分明的离散二进制电平跳跃跳入"))
              .in().letters().tag("tech").pos(0, 30).opacity(0).dur(1.0).soft(0.05)
              .easing(TextEasing::stepped).stagger(0.80).p);

    // =========================================================================
    // 6. 持续波动与周期循环（Continuous Loops - 15 项）
    // =========================================================================
    addPreset(B("loop-sine-wave", QString::fromUtf8("标准正弦波"),
                QString::fromUtf8("柔美正弦水波在文字间连绵传递流淌"))
              .loop().letters().tag("loop").pos(0, 14).wave(2, 1.1).p);
    addPreset(B("loop-fast-ripple", QString::fromUtf8("急促涟漪"),
                QString::fromUtf8("高频率小振幅水纹疾速拂过"))
              .loop().letters().tag("loop").pos(0, 12).wave(4, 0.6).p);
    addPreset(B("loop-bouncy-hop", QString::fromUtf8("波浪跳跃"),
                QString::fromUtf8("文字按波形依次原地欢快跳跃"))
              .loop().letters().tag("loop").pos(0, 24).wave(2, 0.8).p);
    addPreset(B("loop-wind-sway", QString::fromUtf8("风吹麦浪"),
                QString::fromUtf8("左右角度周期性倾斜摇曳"))
              .loop().letters().tag("loop").rot(9).wave(3, 1.4).p);
    addPreset(B("loop-flow-light", QString::fromUtf8("流光溢彩"),
                QString::fromUtf8("光斑与透明度呈波浪状在文字间流转"))
              .loop().letters().tag("loop").opacity(35).wave(3, 1.0).p);
    addPreset(B("loop-skew-groove", QString::fromUtf8("倾斜律动"),
                QString::fromUtf8("动感斜切剪切角周期性波形传导"))
              .loop().letters().tag("loop").shear(0.3, 0).wave(2, 1.2).p);
    addPreset(B("loop-stretch-pulse", QString::fromUtf8("压扁拉伸波"),
                QString::fromUtf8("交替压扁与拉长的弹性形变波浪"))
              .loop().letters().tag("loop").scaleXY(1.18, 0.82).wave(2, 1.1).p);
    addPreset(B("loop-breathe-soft", QString::fromUtf8("轻柔呼吸"),
                QString::fromUtf8("全体文字柔和均匀微弱缩放呼吸"))
              .loop().letters().tag("loop").scale(1.08).opacity(80).center().dur(1.6).pulse().p);
    addPreset(B("loop-breathe-deep", QString::fromUtf8("深沉呼吸"),
                QString::fromUtf8("大幅度舒展的慢速深呼吸"))
              .loop().letters().tag("loop").scale(1.2).opacity(60).center().dur(2.4).pulse().p);
    addPreset(B("loop-heartbeat", QString::fromUtf8("心跳脉动"),
                QString::fromUtf8("双重强弱律动的紧凑心跳节奏"))
              .loop().letters().tag("loop").scale(1.2).center().dur(0.75).pulse().p);
    addPreset(B("loop-gentle-float", QString::fromUtf8("气泡悬浮"),
                QString::fromUtf8("仿佛失重空间中低频起伏悬停"))
              .loop().letters().tag("loop").pos(0, 10).wave(1, 2.5).p);
    addPreset(B("loop-micro-jitter", QString::fromUtf8("高频抖颤"),
                QString::fromUtf8("文字高频微弱颤抖表现紧张或不稳定"))
              .loop().letters().tag("loop").pos(3, 2).dur(0.12).pulse().p);
    addPreset(B("loop-glitch-twitch", QString::fromUtf8("故障抽搐"),
                QString::fromUtf8("突发性错位与偏折的电磁干扰抖动"))
              .loop().letters().tag("loop").pos(8, 5).rot(3).dur(0.1).pulse().p);
    addPreset(B("loop-neon-flicker", QString::fromUtf8("霓虹灯闪烁"),
                QString::fromUtf8("老旧霓虹灯管般的剧烈明暗频闪"))
              .loop().letters().tag("loop").opacity(15).dur(0.2).pulse().p);
    addPreset(B("loop-orbit-dance", QString::fromUtf8("轨道巡游"),
                QString::fromUtf8("微幅旋转结合位移的立体椭圆漫游"))
              .loop().letters().tag("loop").pos(5, 7).rot(5).wave(2, 1.5).p);

    // =========================================================================
    // 6. 新增动力学拓展：弹性与物理重击（Elastic & Physics Bounce Expanded）
    // =========================================================================
    addPreset(B("sharp-double-bounce", QString::fromUtf8("双重回弹"),
                QString::fromUtf8("重力砸地后产生两次清脆的回弹落定"))
              .in().letters().tag("sharp").pos(0, -180).easing(TextEasing::bounce).stagger(0.35).dur(0.9).p);
    addPreset(B("sharp-jelly-squash", QString::fromUtf8("果冻挤压"),
                QString::fromUtf8("如弹性果冻般受力压缩后弹回原状"))
              .in().letters().tag("sharp").scaleXY(1.6, 0.4).easing(TextEasing::elastic).stagger(0.40).dur(0.85).p);
    addPreset(B("sharp-trampoline", QString::fromUtf8("蹦床弹跃"),
                QString::fromUtf8("文字如蹦床般自底部高速弹入并小幅减速落定"))
              .in().letters().tag("sharp").pos(0, 160).scaleY(1.35).easing(TextEasing::bounce).stagger(0.45).dur(0.8).p);
    addPreset(B("sharp-rubber-whip", QString::fromUtf8("胶带抽拉"),
                QString::fromUtf8("受橡皮筋拉伸般伴随大剪切角瞬时回弹抽正"))
              .in().letters().tag("sharp").shear(0.65, 0).scaleX(1.4).easing(TextEasing::elastic).stagger(0.30).dur(0.75).p);
    addPreset(B("sharp-anvil-slam", QString::fromUtf8("铁砧重砸"),
                QString::fromUtf8("超重力高空坠地砸落，伴随垂直形变与地面震感"))
              .in().letters().tag("sharp").pos(0, -260).scaleY(0.65).easing(TextEasing::bounce).stagger(0.25).dur(0.7).p);
    addPreset(B("sharp-domino-fall", QString::fromUtf8("多米诺骨牌"),
                QString::fromUtf8("如骨牌依次向后蓄力后翻转立起"))
              .in().letters().tag("sharp").rot(-85).pos(0, 40).easing(TextEasing::anticipate).stagger(0.60).dur(0.85).p);
    addPreset(B("sharp-gelatin-settle", QString::fromUtf8("微震软胶"),
                QString::fromUtf8("软胶质感微幅谐振衰减落定"))
              .in().letters().tag("sharp").scale(1.35).easing(TextEasing::elastic).stagger(0.35).dur(0.7).p);
    addPreset(B("sharp-horizontal-whip", QString::fromUtf8("横向鞭响"),
                QString::fromUtf8("水平超高速疾速斩出并在末端急停抽击"))
              .in().letters().tag("sharp").pos(320, 0).shear(0.45, 0).easing(TextEasing::sharpSnap).stagger(0.20).dur(0.6).p);
    addPreset(B("sharp-vertical-rebound", QString::fromUtf8("纵向回冲"),
                QString::fromUtf8("从下方超速上冲冲过头后反拉到位"))
              .in().letters().tag("sharp").pos(0, 180).easing(TextEasing::overshoot).stagger(0.35).dur(0.7).p);
    addPreset(B("sharp-jack-in-box", QString::fromUtf8("惊奇弹簧盒"),
                QString::fromUtf8("自缩微极小点破壳弹出伴随强劲多重阻尼回弹"))
              .in().letters().tag("sharp").scale(0.05).pos(0, 80).easing(TextEasing::elastic).stagger(0.40).dur(0.9).p);
    addPreset(B("sharp-recoil-blast", QString::fromUtf8("后坐力喷射"),
                QString::fromUtf8("火炮后坐力般先剧烈后撤蓄势再高爆发喷射到位"))
              .in().letters().tag("sharp").pos(-90, 0).scale(1.25).easing(TextEasing::anticipate).stagger(0.30).dur(0.65).p);
    addPreset(B("sharp-slap-down", QString::fromUtf8("重掌拍击"),
                QString::fromUtf8("由上向下如重掌拍在台面上般扁平触地"))
              .in().letters().tag("sharp").pos(0, -130).scaleXY(1.45, 0.55).easing(TextEasing::bounce).stagger(0.35).dur(0.75).p);
    addPreset(B("sharp-scissor-cut-v", QString::fromUtf8("垂直纵切"),
                QString::fromUtf8("垂直刀刃高速纵向切削进入"))
              .in().letters().tag("sharp").pos(0, 150).shear(0, 0.4).easing(TextEasing::sharpSnap).stagger(0.25).dur(0.6).p);
    addPreset(B("sharp-cascade-word-pop", QString::fromUtf8("逐词阶梯跃"),
                QString::fromUtf8("单词按阶梯节拍依次如弹簧般跃起"))
              .in().words().tag("sharp").pos(0, 90).easing(TextEasing::elastic).stagger(0.60).dur(0.9).p);
    addPreset(B("sharp-line-rebound-drop", QString::fromUtf8("逐行弹跳落"),
                QString::fromUtf8("整行文字以重力加速度逐行砸落弹跳"))
              .in().lines().tag("sharp").pos(0, -110).easing(TextEasing::bounce).stagger(0.50).dur(0.85).p);

    // =========================================================================
    // 7. 新增丝滑流体与影视字幕（Kinetic Smooth & Fluid Expanded）
    // =========================================================================
    addPreset(B("smooth-par-float", QString::fromUtf8("视差浮出"),
                QString::fromUtf8("多维坐标微位移与透明度优雅漫步显形"))
              .in().letters().tag("smooth").pos(30, 60).opacity(0).easing(TextEasing::smooth).stagger(0.50).dur(1.2).soft(0.35).p);
    addPreset(B("smooth-velvet-slide", QString::fromUtf8("天鹅绒轻推"),
                QString::fromUtf8("丝绒质感水平低速滑入，边缘柔焦漫化"))
              .in().letters().tag("smooth").pos(-120, 0).opacity(0).easing(TextEasing::smooth).stagger(0.40).dur(1.1).soft(0.45).p);
    addPreset(B("smooth-bloom-slow", QString::fromUtf8("空灵微绽"),
                QString::fromUtf8("宛如花朵在静谧晨光中缓慢绽开"))
              .in().letters().tag("smooth").scale(0.7).opacity(0).easing(TextEasing::smooth).stagger(0.45).dur(1.4).soft(0.3).p);
    addPreset(B("smooth-mist-spread", QString::fromUtf8("晨雾弥漫"),
                QString::fromUtf8("大范围柔和渐变与微缩放交织如晨雾扩散"))
              .in().letters().tag("smooth").scale(1.15).opacity(0).easing(TextEasing::smooth).stagger(0.55).dur(1.3).soft(0.5).p);
    addPreset(B("smooth-drift-ne", QString::fromUtf8("东北幽浮"),
                QString::fromUtf8("向东北对角线优雅轻柔飘移进入"))
              .in().letters().tag("smooth").pos(-80, 80).opacity(0).easing(TextEasing::smooth).stagger(0.45).dur(1.15).soft(0.3).p);
    addPreset(B("smooth-drift-sw", QString::fromUtf8("西南游弋"),
                QString::fromUtf8("向西南对角线静谧游弋入画"))
              .in().letters().tag("smooth").pos(80, -80).opacity(0).easing(TextEasing::smooth).stagger(0.45).dur(1.15).soft(0.3).p);
    addPreset(B("smooth-waterfall-drop", QString::fromUtf8("水瀑垂落"),
                QString::fromUtf8("如瀑布水帘垂落，透明度随行间渐次倾泻"))
              .in().letters().tag("smooth").pos(0, -140).opacity(0).easing(TextEasing::smooth).stagger(0.40).dur(1.2).soft(0.35).p);
    addPreset(B("smooth-dawn-rise", QString::fromUtf8("破晓初升"),
                QString::fromUtf8("自下方轻盈抬升并伴随微小尺寸舒展"))
              .in().letters().tag("smooth").pos(0, 90).scale(0.9).opacity(0).easing(TextEasing::smooth).stagger(0.45).dur(1.2).p);
    addPreset(B("smooth-word-mist", QString::fromUtf8("逐词迷雾"),
                QString::fromUtf8("每个词语如同自薄雾深处逐一浮现显影"))
              .in().words().tag("smooth").scale(1.1).opacity(0).easing(TextEasing::smooth).stagger(0.50).dur(1.3).soft(0.4).p);
    addPreset(B("smooth-word-glimmer", QString::fromUtf8("逐词微光"),
                QString::fromUtf8("逐词以微小位移和柔和亮度渐次点亮"))
              .in().words().tag("smooth").pos(0, 30).opacity(20).easing(TextEasing::smooth).stagger(0.50).dur(1.1).p);
    addPreset(B("smooth-line-cascade-up", QString::fromUtf8("逐行梯次升"),
                QString::fromUtf8("排版文本逐行优雅梯次抬升就位"))
              .in().lines().tag("smooth").pos(0, 60).opacity(0).easing(TextEasing::smooth).stagger(0.55).dur(1.2).p);
    addPreset(B("smooth-line-curtain", QString::fromUtf8("逐行帘幕拉"),
                QString::fromUtf8("宛如幕布横向轻柔拉开显露整行文本"))
              .in().lines().tag("smooth").pos(-150, 0).opacity(0).easing(TextEasing::smooth).stagger(0.50).dur(1.1).soft(0.4).p);

    // =========================================================================
    // 8. 新增通道属性与形变（Channel Properties Expanded）
    // =========================================================================
    addPreset(B("prop-scale-wide-8x", QString::fromUtf8("极致横向拉伸"),
                QString::fromUtf8("横向压缩 8 倍后以超强爆发力弹开铺平"))
              .in().letters().tag("prop").scaleXY(8.0, 1.0).opacity(0).easing(TextEasing::sharpSnap).stagger(0.30).dur(0.7).p);
    addPreset(B("prop-scale-tall-8x", QString::fromUtf8("极致天顶拔起"),
                QString::fromUtf8("纵向拉伸 8 倍从天顶如擎天柱般拔地而起"))
              .in().letters().tag("prop").scaleXY(1.0, 8.0).opacity(0).easing(TextEasing::sharpSnap).stagger(0.30).dur(0.7).p);
    addPreset(B("prop-shear-lean-left", QString::fromUtf8("向左倾角切"),
                QString::fromUtf8("左侧动感大倾角剪切并高速摆正"))
              .in().letters().tag("prop").shear(-0.8, 0).easing(TextEasing::sharpSnap).stagger(0.35).dur(0.65).p);
    addPreset(B("prop-shear-lean-right", QString::fromUtf8("向右倾角切"),
                QString::fromUtf8("右侧动感大倾角剪切并高速摆正"))
              .in().letters().tag("prop").shear(0.8, 0).easing(TextEasing::sharpSnap).stagger(0.35).dur(0.65).p);
    addPreset(B("prop-rot-ccw-360", QString::fromUtf8("逆时针全回转"),
                QString::fromUtf8("逆时针 360 度自转并伴随轻微过冲回摆"))
              .in().letters().tag("prop").rot(-360).easing(TextEasing::overshoot).stagger(0.45).dur(0.9).p);
    addPreset(B("prop-rot-pendulum", QString::fromUtf8("钟摆起摇"),
                QString::fromUtf8("自 75 度大倾角如钟摆放手般弹性衰减摇正"))
              .in().letters().tag("prop").rot(75).easing(TextEasing::elastic).stagger(0.40).dur(1.1).p);
    addPreset(B("prop-pos-diagonal-down", QString::fromUtf8("对角急冲入"),
                QString::fromUtf8("自左上方 45 度角高速俯冲归位"))
              .in().letters().tag("prop").pos(-150, -150).easing(TextEasing::sharpSnap).stagger(0.30).dur(0.65).p);
    addPreset(B("prop-pos-diagonal-up", QString::fromUtf8("仰冲对角"),
                QString::fromUtf8("自右下方 45 度角高速仰冲归位"))
              .in().letters().tag("prop").pos(150, 150).easing(TextEasing::sharpSnap).stagger(0.30).dur(0.65).p);
    addPreset(B("prop-word-scale-explode", QString::fromUtf8("逐词膨胀炸裂"),
                QString::fromUtf8("词语从 2.5 倍超大尺寸剧烈收缩炸落"))
              .in().words().tag("prop").scale(2.5).opacity(0).easing(TextEasing::overshoot).stagger(0.55).dur(0.85).p);
    addPreset(B("prop-line-shear-wave", QString::fromUtf8("逐行错位剪切"),
                QString::fromUtf8("行间剪切角波浪式错开归位"))
              .in().lines().tag("prop").shear(0.5, 0).easing(TextEasing::overshoot).stagger(0.50).dur(0.8).p);

    // =========================================================================
    // 9. 新增 3D 空间立体构造（3D Spatial Kinetics Expanded）
    // =========================================================================
    addPreset(B("3d-barrel-roll", QString::fromUtf8("3D滚筒翻滚"),
                QString::fromUtf8("文字沿 X 轴翻滚并伴随蓄力后撤"))
              .in().letters().tag("3d").rot(180).scale(0.5).easing(TextEasing::anticipate).stagger(0.40).dur(0.9).p);
    addPreset(B("3d-vortex-in", QString::fromUtf8("3D旋涡漏斗"),
                QString::fromUtf8("从微型漏斗中心高速螺旋放大飞出"))
              .in().letters().tag("3d").rot(270).scale(0.15).opacity(0).easing(TextEasing::sharpSnap).stagger(0.45).dur(0.85).p);
    addPreset(B("3d-cube-fold-top", QString::fromUtf8("3D顶折立方"),
                QString::fromUtf8("立方体顶面般自上方 90 度折叠掀开入场"))
              .in().letters().tag("3d").rot(90).pos(0, -60).easing(TextEasing::overshoot).stagger(0.40).dur(0.8).p);
    addPreset(B("3d-cube-fold-bottom", QString::fromUtf8("3D底折掀开"),
                QString::fromUtf8("立方体底面般自下方 90 度翻起就位"))
              .in().letters().tag("3d").rot(-90).pos(0, 60).easing(TextEasing::overshoot).stagger(0.40).dur(0.8).p);
    addPreset(B("3d-door-swing-left", QString::fromUtf8("3D门扉单开"),
                QString::fromUtf8("以左侧边框为轴如开门般 90 度推开"))
              .in().letters().tag("3d").rot(-90).pos(-80, 0).easing(TextEasing::anticipate).stagger(0.40).dur(0.85).p);
    addPreset(B("3d-door-swing-right", QString::fromUtf8("3D门扉右开"),
                QString::fromUtf8("以右侧边框为轴如开门般 90 度拉开"))
              .in().letters().tag("3d").rot(90).pos(80, 0).easing(TextEasing::anticipate).stagger(0.40).dur(0.85).p);
    addPreset(B("3d-spatial-fan", QString::fromUtf8("3D空间扇形展开"),
                QString::fromUtf8("文字沿空间圆弧扇面逐字展开摆正"))
              .in().letters().tag("3d").rot(60).scale(0.6).easing(TextEasing::elastic).stagger(0.50).dur(1.0).p);
    addPreset(B("3d-depth-push", QString::fromUtf8("3D景深前推"),
                QString::fromUtf8("自深度景深原点瞬间放大冲到镜头前"))
              .in().letters().tag("3d").scale(0.05).opacity(0).easing(TextEasing::overshoot).stagger(0.35).dur(0.75).p);
    addPreset(B("3d-word-flip-cascade", QString::fromUtf8("逐词3D阶梯翻转"),
                QString::fromUtf8("词语沿空间轴依次翻折 90 度落定"))
              .in().words().tag("3d").rot(90).easing(TextEasing::overshoot).stagger(0.55).dur(0.9).p);
    addPreset(B("3d-line-fold-cascade", QString::fromUtf8("逐行3D折叠推落"),
                QString::fromUtf8("整行文字沿水平轴如百叶窗翻折就位"))
              .in().lines().tag("3d").rot(-90).easing(TextEasing::overshoot).stagger(0.50).dur(0.85).p);

    // =========================================================================
    // 10. 新增赛博极客与故障解码（Cyber Glitch & Tech Expanded）
    // =========================================================================
    addPreset(B("tech-binary-matrix", QString::fromUtf8("二进制阵列解算"),
                QString::fromUtf8("数字矩阵运算般离散 5 阶量子跳变入场"))
              .in().letters().tag("tech").pos(0, 20).opacity(0).easing(TextEasing::stepped).stagger(0.65).dur(0.75).p);
    addPreset(B("tech-hex-decode", QString::fromUtf8("十六进制跳变解码"),
                QString::fromUtf8("十六进制跳变阶跃显示"))
              .in().letters().tag("tech").scale(0.8).opacity(0).easing(TextEasing::stepped).stagger(0.60).dur(0.8).p);
    addPreset(B("tech-crt-scan", QString::fromUtf8("CRT显像管横扫"),
                QString::fromUtf8("老式显像管电子枪横扫点亮，先扁后正"))
              .in().letters().tag("tech").scaleXY(2.0, 0.1).opacity(0).easing(TextEasing::sharpSnap).stagger(0.30).dur(0.6).p);
    addPreset(B("tech-radar-sweep", QString::fromUtf8("雷达波速扫"),
                QString::fromUtf8("雷达波束圆周扫描后迅速点亮"))
              .in().letters().tag("tech").rot(90).opacity(0).easing(TextEasing::smooth).stagger(0.40).dur(0.8).soft(0.3).p);
    addPreset(B("tech-terminal-blink", QString::fromUtf8("终端命令闪进"),
                QString::fromUtf8("光标停留后瞬间硬切显现的硬朗终端命令"))
              .in().letters().tag("tech").opacity(0).easing(TextEasing::stepped).stagger(0.70).dur(0.65).p);
    addPreset(B("tech-strobe-alert", QString::fromUtf8("频闪警报切入"),
                QString::fromUtf8("警报频闪信号快速同步显影"))
              .in().letters().tag("tech").opacity(10).easing(TextEasing::stepped).stagger(0.50).dur(0.5).p);
    addPreset(B("tech-analog-noise", QString::fromUtf8("模拟噪波切片"),
                QString::fromUtf8("伴随水平剪切与离散错位的模拟信号波"))
              .in().letters().tag("tech").pos(25, 0).shear(0.3, 0).easing(TextEasing::stepped).stagger(0.55).dur(0.7).p);
    addPreset(B("tech-digital-glitch-drop", QString::fromUtf8("数字故障跌落"),
                QString::fromUtf8("离散步进下落带有角度偏转的故障风格"))
              .in().letters().tag("tech").pos(0, -40).rot(10).easing(TextEasing::stepped).stagger(0.50).dur(0.6).p);
    addPreset(B("tech-quantum-jump", QString::fromUtf8("量子跳跃显影"),
                QString::fromUtf8("尺寸在离散步进间跳变放大最终定格"))
              .in().letters().tag("tech").scale(1.5).opacity(0).easing(TextEasing::stepped).stagger(0.45).dur(0.65).p);
    addPreset(B("tech-code-decrypt-word", QString::fromUtf8("逐词代码破译"),
                QString::fromUtf8("词语级阶跃解密显现"))
              .in().words().tag("tech").opacity(0).easing(TextEasing::stepped).stagger(0.60).dur(0.7).p);

    // =========================================================================
    // 11. 新增持续文字循环动效（Continuous Loops Expanded）
    // =========================================================================
    addPreset(B("loop-rainbow-wave", QString::fromUtf8("波形起伏浪"),
                QString::fromUtf8("4 周期密集起伏水波"))
              .loop().letters().tag("loop").pos(0, 15).wave(4, 1.0).p);
    addPreset(B("loop-liquid-ripple", QString::fromUtf8("液体涟漪"),
                QString::fromUtf8("微小形变膨胀拉伸的液体循环波"))
              .loop().letters().tag("loop").scaleXY(1.1, 0.9).wave(3, 1.2).p);
    addPreset(B("loop-pendulum-swing", QString::fromUtf8("单摆悬荡"),
                QString::fromUtf8("单周期宽幅钟摆持续摇荡"))
              .loop().letters().tag("loop").rot(12).wave(1, 1.6).p);
    addPreset(B("loop-earthquake", QString::fromUtf8("震颤共振"),
                QString::fromUtf8("超高频急促共振抖动"))
              .loop().letters().tag("loop").pos(5, 4).dur(0.08).pulse().p);
    addPreset(B("loop-zero-gravity", QString::fromUtf8("失重微沉浮"),
                QString::fromUtf8("太空失重环境极慢起伏悬停"))
              .loop().letters().tag("loop").pos(0, 8).dur(2.8).pulse().p);
    addPreset(B("loop-neon-strobe", QString::fromUtf8("高频霓虹频闪"),
                QString::fromUtf8("极低暗度的高速频闪电光循环"))
              .loop().letters().tag("loop").opacity(5).dur(0.15).pulse().p);
}
}

namespace TextAnimPresets {
int count()
{
    ensurePresets();
    return gPresets.count();
}

const QList<TextAnimPreset>& all()
{
    ensurePresets();
    return gPresets;
}

const TextAnimPreset* byId(const QString& id)
{
    ensurePresets();
    for (const auto& p : gPresets) {
        if (p.id == id) { return &p; }
    }
    return nullptr;
}

bool apply(TextBox* const box,
           const TextAnimPreset& preset,
           const int startFrame,
           const qreal fps,
           const qreal durationScale,
           const bool out)
{
    if (!box) { return false; }
    if (preset.id == "tech-number-roll" || preset.id == "number-roll") {
        // bake text keys counting 0 -> N, preserving any prefix and
        // suffix around the number (e.g. "progress: 42%")
        const QRegularExpression re(QStringLiteral("(\\D*)(-?\\d+)(\\D*)"));
        const auto match = re.match(box->getCurrentValue());
        if (!match.hasMatch()) { return false; }
        const QString prefix = match.captured(1);
        const QString suffix = match.captured(3);
        const int target = match.captured(2).toInt();
        const auto textAnim = box->getStringAnimator();
        if (!textAnim) { return false; }
        const int durF = qMax(2, qRound(preset.duration*durationScale*fps));
        const int steps = qBound(2, qRound(durF/2.), 40);
        for (int i = 0; i <= steps; i++) {
            const int value = qRound(qreal(target)*i/steps);
            const int frame = startFrame + qRound(qreal(durF)*i/steps);
            const auto textKey = enve::make_shared<QStringKey>(
                        prefix + QString::number(value) + suffix,
                        frame, textAnim);
            textAnim->anim_appendKey(textKey);
        }
        return true;
    }
    qreal W = box->getRelBoundingRect().width();
    if (W <= 1.) {
        W = qMax(horizontalAdvance(box->getSkFont(),
                                   box->getCurrentValue()), 10.);
    }
    TextAnimPreset p = preset;
    if (out && p.kind == TextAnim::sweepIn) { p.kind = TextAnim::sweepOut; }
    const auto effect = enve::make_shared<TextEffect>();
    effect->setupFromPreset(p, W, box->getFontSize(),
                            startFrame, fps, durationScale);
    effect->prp_setName(out && p.supportsOut ?
                        p.name + QString::fromUtf8("（出）") : p.name);
    box->getTextEffects()->addChild(effect);
    return true;
}

QList<QImage> renderPreviewSequence(TextBox* const box,
                                    const QList<qreal>& frames,
                                    const QSize& imgSize)
{
    QList<QImage> result;
    if (!box || imgSize.width() < 2 || imgSize.height() < 2) { return result; }

    struct FrameData {
        stdsptr<TextBoxRenderData> data;
        QList<LetterRenderData*> letters;
    };
    QList<FrameData> built;
    QRectF unionBounds;

    for (const auto relFrame : frames) {
        const auto textData = enve::make_shared<TextBoxRenderData>(box);
        textData->fRelFrame = relFrame;
        textData->initialize(box->getTextAtRelFrame(relFrame),
                             box->getSkFont(),
                             box->getLetterSpacingAt(relFrame),
                             box->getWordSpacingAt(relFrame),
                             box->getLineSpacingAt(relFrame),
                             box->getTextHAlignment(),
                             box->getTextVAlignment(),
                             box, nullptr);
        FrameData fd;
        fd.data = textData;
        for (const auto& line : textData->fLines) {
            line->fRelFrame = relFrame;
            line->fOpacity = 100;
            for (const auto& word : line->fWords) {
                word->fRelFrame = relFrame;
                word->fOpacity = 100;
                for (const auto& letter : word->fLetters) {
                    letter->fRelFrame = relFrame;
                    // sceneless: the regular pipeline never assigned
                    // this (setupWithoutRasterEffects bails on null
                    // scene), restore the layer default
                    letter->fOpacity = 100;
                    fd.letters << letter.get();
                }
            }
        }
        if (fd.letters.isEmpty()) { continue; }
        QList<TextEffect*> effects;
        box->getTextEffects()->addEffects(effects);
        for (const auto effect : effects) {
            effect->apply(textData.get());
        }
        // collapse word/line level opacity (effects targeting words or
        // lines multiply the container opacity) onto the letters,
        // since only letters are drawn below
        for (const auto& line : textData->fLines) {
            for (const auto& word : line->fWords) {
                for (const auto& letter : word->fLetters) {
                    letter->fOpacity *= word->fOpacity*line->fOpacity/10000.;
                }
            }
        }
        for (const auto letter : fd.letters) {
            if (letter->fPath.isEmpty()) { continue; }
            SkPath transformed = letter->fPath;
            transformed.transform(toSkMatrix(letter->fTotalTransform));
            const QRectF b = toQRectF(transformed.computeTightBounds());
            unionBounds = unionBounds.isNull() ? b : unionBounds.united(b);
        }
        built << fd;
    }

    if (built.isEmpty() || unionBounds.isEmpty()) { return result; }

    const qreal margin = 0.06;
    const qreal availW = imgSize.width()*(1. - 2.*margin);
    const qreal availH = imgSize.height()*(1. - 2.*margin);
    const qreal scale = qMin(availW/qMax(unionBounds.width(), 1.),
                             availH/qMax(unionBounds.height(), 1.));
    const qreal tx = imgSize.width()/2. - scale*unionBounds.center().x();
    const qreal ty = imgSize.height()/2. - scale*unionBounds.center().y();

    for (const auto& fd : built) {
        QImage img(imgSize, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        const auto info = SkImageInfo::Make(imgSize.width(),
                                            imgSize.height(),
                                            kN32_SkColorType,
                                            kPremul_SkAlphaType);
        const auto surface = SkSurface::MakeRasterDirect(
                    info, img.bits(), static_cast<size_t>(img.bytesPerLine()));
        if (!surface) {
            result << img;
            continue;
        }
        const auto canvas = surface->getCanvas();
        canvas->translate(toSkScalar(tx), toSkScalar(ty));
        canvas->scale(toSkScalar(scale), toSkScalar(scale));
        for (const auto letter : fd.letters) {
            canvas->save();
            canvas->concat(toSkMatrix(letter->fTotalTransform));
            SkPaint paint;
            paint.setAntiAlias(true);
            const auto alpha = toSkScalar(letter->fOpacity*0.01);
            if (!letter->fFillPath.isEmpty()) {
                letter->fPaintSettings.applyPainterSettingsSk(paint, alpha);
                canvas->drawPath(letter->fFillPath, paint);
            }
            if (!letter->fOutlinePath.isEmpty()) {
                paint.setShader(nullptr);
                letter->fStrokeSettings.applyPainterSettingsSk(paint, alpha);
                canvas->drawPath(letter->fOutlinePath, paint);
            }
            canvas->restore();
        }
        result << img;
    }
    return result;
}
}

// dev-only: bake a few presets onto a sceneless box and dump the
// rendered frames as PNGs (used by the textanimtest tool)
bool textAnimDebugDumpFrames(const QString& outDir)
{
    // console test env: the app normally creates these singletons
    if (!eSettings::sInstance) {
        const auto settings = new eSettings(4, intKB(8*1024*1024));
        Q_UNUSED(settings)
    }
    if (!Document::sInstance) {
        static TaskScheduler taskScheduler;
        static Document document(taskScheduler);
        Q_UNUSED(document)
    }
    if (!eFilterSettings::sInstance) {
        static eFilterSettings filterSettings;
        Q_UNUSED(filterSettings)
    }
    QDir().mkpath(outDir);
    const QStringList ids = { "sharp-snap-rise", "tech-typewriter-std", "smooth-cinematic-fade",
                              "loop-sine-wave", "loop-breathe-soft", "smooth-line-rise" };
    for (const auto& id : ids) {
        const auto preset = TextAnimPresets::byId(id);
        if (!preset) { continue; }
        const auto box = enve::make_shared<TextBox>();
        box->setCurrentValue(QString::fromUtf8("你好啊，动画测试\n第二行文本"));
        box->setFontFamilyAndStyle(QString::fromUtf8("微软雅黑"),
                                   SkFontStyle());
        box->setFontSize(64);
        const auto fill = box->getFillSettings();
        if (fill) {
            fill->setPaintType(PaintType::FLATPAINT);
            fill->setCurrentColor(QColor(235, 235, 235), false);
        }
        TextAnimPresets::apply(box.get(), *preset, 0, 24., 1.);
        QList<qreal> frames;
        for (int i = 0; i < 6; i++) { frames << 8.4*i; }
        const auto imgs = TextAnimPresets::renderPreviewSequence(
                    box.get(), frames, QSize(480, 220));
        int n = 0;
        for (const auto& img : imgs) {
            img.save(outDir + "/" + id + "_" +
                     QString::number(n++) + ".png");
        }
    }
    return true;
}
