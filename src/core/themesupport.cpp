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

#include "themesupport.h"
#include "appsupport.h"
#include <QImageReader>

#include <QFile>
#include <QIcon>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QToolButton>
#include <QPixmap>
#include <QPainter>
#include <QWidget>

namespace {

ThemeSupport::Theme sTheme = ThemeSupport::Theme::Friction;
QString sAccentPresetId = QStringLiteral("auto");
QColor sCustomAccent;
QColor sCustomBase;
QColor sCustomAlt;
QColor sCustomDarker;
int sBorderRadius = 6;
int sScrollbarWidth = 6;
QString sCustomQss;
bool sThemeConfigLoaded = false;

const QList<ThemePresetInfo> sThemePresets = {
    {
        QStringLiteral("friction"),
        QStringLiteral("Friction"),
        QStringLiteral("Friction 经典默认深蓝调 (Default Classic)"),
        QColor(26, 26, 30),
        QColor(33, 33, 39),
        QColor(19, 19, 21),
        QColor(104, 144, 206), // #6890ce
        QColor(49, 49, 59),
        QColor(65, 65, 80),
        QColor(59, 59, 72),
        QColor(19, 19, 21),
        4
    },
    {
        QStringLiteral("blender"),
        QStringLiteral("Blender"),
        QStringLiteral("Blender 原生深灰极简设计 (Native Dark)"),
        QColor(34, 34, 34),
        QColor(43, 43, 43),
        QColor(29, 29, 29),
        QColor(71, 114, 179), // #4772b3
        QColor(74, 74, 74),
        QColor(62, 62, 62),
        QColor(84, 84, 84),
        QColor(29, 29, 29),
        4
    },
    {
        QStringLiteral("morandi_dark"),
        QStringLiteral("经典深色莫兰迪 (Dark Morandi)"),
        QStringLiteral("经典低饱和深色莫兰迪暖调，搭配鸢尾紫强调色"),
        QColor(33, 32, 28),     // #21201c
        QColor(44, 43, 38),     // #2c2b26
        QColor(24, 23, 21),     // #181715
        QColor(140, 130, 158),  // #8c829e (Iris)
        QColor(56, 54, 48),
        QColor(70, 68, 61),
        QColor(76, 74, 66),
        QColor(26, 25, 22),
        6
    },
    {
        QStringLiteral("morandi_charcoal"),
        QStringLiteral("暖炭灰 (Warm Charcoal)"),
        QStringLiteral("温暖沉稳的炭灰底色，搭配典雅莫兰迪金"),
        QColor(38, 36, 32),     // #262420
        QColor(49, 47, 41),     // #312f29
        QColor(28, 27, 24),     // #1c1b18
        QColor(191, 169, 128),  // #bfa980 (Gold)
        QColor(60, 57, 51),
        QColor(75, 71, 63),
        QColor(82, 78, 70),
        QColor(30, 28, 25),
        6
    },
    {
        QStringLiteral("morandi_graphite"),
        QStringLiteral("石墨深灰 (Graphite Dark)"),
        QStringLiteral("现代冷峻石墨深灰色调，搭配精致鸢尾紫"),
        QColor(29, 31, 33),     // #1d1f21
        QColor(40, 42, 45),     // #282a2d
        QColor(22, 23, 25),     // #161719
        QColor(140, 130, 158),  // #8c829e (Iris)
        QColor(51, 54, 59),
        QColor(65, 69, 76),
        QColor(74, 79, 86),
        QColor(24, 25, 27),
        6
    },
    {
        QStringLiteral("morandi_sage"),
        QStringLiteral("北欧苔原 (Nordic Sage)"),
        QStringLiteral("自然清新的鼠尾草青色与苔原灰绿"),
        QColor(33, 35, 32),     // #212320
        QColor(43, 45, 42),     // #2b2d2a
        QColor(25, 27, 24),     // #191b18
        QColor(143, 163, 130),  // #8fa382 (Sage)
        QColor(53, 57, 52),
        QColor(68, 74, 66),
        QColor(77, 84, 75),
        QColor(27, 29, 26),
        8
    },
    {
        QStringLiteral("morandi_kyoto"),
        QStringLiteral("京都晚秋 (Kyoto Autumn)"),
        QStringLiteral("古朴温润的暖木色与瓦松红强调色"),
        QColor(36, 30, 28),     // #241e1c
        QColor(48, 39, 36),     // #302724
        QColor(26, 21, 20),     // #1a1514
        QColor(190, 122, 107),  // #be7a6b (Terracotta)
        QColor(59, 49, 45),
        QColor(76, 63, 58),
        QColor(86, 71, 65),
        QColor(29, 24, 23),
        8
    },
    {
        QStringLiteral("morandi_rose"),
        QStringLiteral("沙漠玫瑰 (Desert Rose)"),
        QStringLiteral("优雅内敛的玫瑰灰调与沉静深灰"),
        QColor(36, 32, 33),     // #242021
        QColor(47, 42, 43),     // #2f2a2b
        QColor(26, 22, 23),     // #1a1617
        QColor(196, 140, 144),  // #c48c90 (Rose)
        QColor(58, 51, 53),
        QColor(74, 66, 68),
        QColor(84, 75, 78),
        QColor(29, 25, 26),
        8
    },
    {
        QStringLiteral("morandi_midnight"),
        QStringLiteral("极夜黑 (Midnight)"),
        QStringLiteral("深邃极夜纯黑底色，高对比度低疲劳"),
        QColor(22, 22, 24),     // #161618
        QColor(32, 32, 36),     // #202024
        QColor(16, 16, 18),     // #101012
        QColor(140, 130, 158),  // #8c829e (Plum)
        QColor(42, 42, 48),
        QColor(56, 56, 64),
        QColor(66, 66, 75),
        QColor(18, 18, 20),
        4
    },
    {
        QStringLiteral("morandi_mocha"),
        QStringLiteral("燕麦摩卡 (Mocha Cream)"),
        QStringLiteral("醇厚温暖的摩卡咖啡与莫兰迪金"),
        QColor(37, 34, 30),     // #25221e
        QColor(48, 44, 39),     // #302c27
        QColor(27, 25, 22),     // #1b1916
        QColor(191, 169, 128),  // #bfa980 (Gold)
        QColor(59, 54, 48),
        QColor(76, 69, 61),
        QColor(85, 78, 69),
        QColor(30, 27, 24),
        8
    },
    {
        QStringLiteral("morandi_slate"),
        QStringLiteral("雾蓝板岩 (Slate Fog)"),
        QStringLiteral("宁静清冽的板岩冷灰与天蓝灰"),
        QColor(32, 34, 37),     // #202225
        QColor(43, 46, 51),     // #2b2e33
        QColor(23, 24, 26),     // #17181a
        QColor(127, 155, 176),  // #7f9bb0 (Sky)
        QColor(52, 56, 63),
        QColor(68, 74, 83),
        QColor(76, 83, 93),
        QColor(25, 26, 29),
        6
    },
    {
        QStringLiteral("custom"),
        QStringLiteral("自定义主题 (Custom)"),
        QStringLiteral("由用户完全自由定制的调色板与外观"),
        QColor(33, 32, 28),
        QColor(44, 43, 38),
        QColor(24, 23, 21),
        QColor(140, 130, 158),
        QColor(56, 54, 48),
        QColor(70, 68, 61),
        QColor(76, 74, 66),
        QColor(26, 25, 22),
        6
    }
};

const QList<AccentPresetInfo> sAccentPresets = {
    {QStringLiteral("auto"), QStringLiteral("跟随主题默认 (Auto)"), QColor()},
    {QStringLiteral("iris"), QStringLiteral("鸢尾紫 (Iris)"), QColor(140, 130, 158)},       // #8c829e
    {QStringLiteral("gold"), QStringLiteral("莫兰迪金 (Gold)"), QColor(191, 169, 128)},     // #bfa980
    {QStringLiteral("rose"), QStringLiteral("玫瑰灰 (Rose)"), QColor(196, 140, 144)},       // #c48c90
    {QStringLiteral("pine"), QStringLiteral("松绿灰 (Pine)"), QColor(123, 156, 144)},       // #7b9c90
    {QStringLiteral("foam"), QStringLiteral("海泡绿 (Foam)"), QColor(128, 156, 149)},       // #809c95
    {QStringLiteral("peach"), QStringLiteral("蜜桃灰 (Peach)"), QColor(199, 150, 133)},     // #c79685
    {QStringLiteral("sky"), QStringLiteral("天蓝灰 (Sky)"), QColor(127, 155, 176)},         // #7f9bb0
    {QStringLiteral("mustard"), QStringLiteral("芥末黄 (Mustard)"), QColor(189, 165, 114)}, // #bda572
    {QStringLiteral("terracotta"), QStringLiteral("瓦松红 (Terracotta)"), QColor(190, 122, 107)}, // #be7a6b
    {QStringLiteral("mint"), QStringLiteral("薄荷绿 (Mint)"), QColor(120, 163, 145)},       // #78a391
    {QStringLiteral("sage"), QStringLiteral("鼠尾草青 (Sage)"), QColor(143, 163, 130)},     // #8fa382
    {QStringLiteral("blender_blue"), QStringLiteral("Blender 经典蓝 (Blender Blue)"), QColor(71, 114, 179)}, // #4772b3
    {QStringLiteral("friction_blue"), QStringLiteral("Friction 默认蓝 (Friction Blue)"), QColor(104, 144, 206)}, // #6890ce
    {QStringLiteral("custom"), QStringLiteral("自定义强调色 (Custom)"), QColor()}
};

} // namespace

const QList<ThemePresetInfo>& ThemeSupport::themePresetList()
{
    return sThemePresets;
}

const ThemePresetInfo ThemeSupport::themePreset(const QString &id)
{
    for (const auto &preset : sThemePresets) {
        if (preset.id == id) { return preset; }
    }
    return sThemePresets.first();
}

const QList<AccentPresetInfo>& ThemeSupport::accentPresetList()
{
    return sAccentPresets;
}

const QString ThemeSupport::accentPresetId()
{
    return sAccentPresetId;
}

void ThemeSupport::setAccentPresetId(const QString &id)
{
    sAccentPresetId = id;
}

void ThemeSupport::setCustomAccentColor(const QColor &color)
{
    sCustomAccent = color;
}

const QColor ThemeSupport::customAccentColor()
{
    return sCustomAccent;
}

void ThemeSupport::setCustomBaseColor(const QColor &color)
{
    sCustomBase = color;
}

const QColor ThemeSupport::customBaseColor()
{
    return sCustomBase;
}

void ThemeSupport::setCustomAlternateColor(const QColor &color)
{
    sCustomAlt = color;
}

const QColor ThemeSupport::customAlternateColor()
{
    return sCustomAlt;
}

void ThemeSupport::setCustomDarkerColor(const QColor &color)
{
    sCustomDarker = color;
}

const QColor ThemeSupport::customDarkerColor()
{
    return sCustomDarker;
}

int ThemeSupport::borderRadius()
{
    return sBorderRadius;
}

void ThemeSupport::setBorderRadius(int radius)
{
    sBorderRadius = qBound(0, radius, 24);
}

int ThemeSupport::scrollbarWidth()
{
    return sScrollbarWidth;
}

void ThemeSupport::setScrollbarWidth(int width)
{
    sScrollbarWidth = qBound(0, width, 24);
}

const QString ThemeSupport::customQss()
{
    return sCustomQss;
}

void ThemeSupport::setCustomQss(const QString &qss)
{
    sCustomQss = qss;
}

void ThemeSupport::loadThemeConfig()
{
    if (sThemeConfigLoaded) { return; }
    sThemeConfigLoaded = true;

    const QString id = AppSupport::getSettings(QStringLiteral("ui"),
                                              QStringLiteral("theme"),
                                              QStringLiteral("friction")).toString();
    setThemeFromId(id);

    sAccentPresetId = AppSupport::getSettings(QStringLiteral("theme"),
                                              QStringLiteral("accentPreset"),
                                              QStringLiteral("auto")).toString();

    const QString cAccent = AppSupport::getSettings(QStringLiteral("theme"),
                                                    QStringLiteral("customAccent"),
                                                    QString()).toString();
    if (!cAccent.isEmpty()) { sCustomAccent = QColor(cAccent); }

    const QString cBase = AppSupport::getSettings(QStringLiteral("theme"),
                                                  QStringLiteral("customBase"),
                                                  QString()).toString();
    if (!cBase.isEmpty()) { sCustomBase = QColor(cBase); }

    const QString cAlt = AppSupport::getSettings(QStringLiteral("theme"),
                                                 QStringLiteral("customAlt"),
                                                 QString()).toString();
    if (!cAlt.isEmpty()) { sCustomAlt = QColor(cAlt); }

    const QString cDarker = AppSupport::getSettings(QStringLiteral("theme"),
                                                    QStringLiteral("customDarker"),
                                                    QString()).toString();
    if (!cDarker.isEmpty()) { sCustomDarker = QColor(cDarker); }

    sBorderRadius = AppSupport::getSettings(QStringLiteral("theme"),
                                            QStringLiteral("borderRadius"),
                                            themePreset(id).defaultRadius).toInt();

    sScrollbarWidth = AppSupport::getSettings(QStringLiteral("theme"),
                                              QStringLiteral("scrollbarWidth"),
                                              6).toInt();

    sCustomQss = AppSupport::getSettings(QStringLiteral("theme"),
                                         QStringLiteral("customQSS"),
                                         QString()).toString();
}

void ThemeSupport::saveThemeConfig()
{
    AppSupport::setSettings(QStringLiteral("ui"),
                            QStringLiteral("theme"),
                            themeId());

    AppSupport::setSettings(QStringLiteral("theme"),
                            QStringLiteral("accentPreset"),
                            sAccentPresetId);

    if (sCustomAccent.isValid()) {
        AppSupport::setSettings(QStringLiteral("theme"),
                                QStringLiteral("customAccent"),
                                sCustomAccent.name());
    }

    if (sCustomBase.isValid()) {
        AppSupport::setSettings(QStringLiteral("theme"),
                                QStringLiteral("customBase"),
                                sCustomBase.name());
    }

    if (sCustomAlt.isValid()) {
        AppSupport::setSettings(QStringLiteral("theme"),
                                QStringLiteral("customAlt"),
                                sCustomAlt.name());
    }

    if (sCustomDarker.isValid()) {
        AppSupport::setSettings(QStringLiteral("theme"),
                                QStringLiteral("customDarker"),
                                sCustomDarker.name());
    }

    AppSupport::setSettings(QStringLiteral("theme"),
                            QStringLiteral("borderRadius"),
                            sBorderRadius);

    AppSupport::setSettings(QStringLiteral("theme"),
                            QStringLiteral("scrollbarWidth"),
                            sScrollbarWidth);

    AppSupport::setSettings(QStringLiteral("theme"),
                            QStringLiteral("customQSS"),
                            sCustomQss);
}

void ThemeSupport::setTheme(const Theme theme)
{
    sTheme = theme;
}

ThemeSupport::Theme ThemeSupport::theme()
{
    return sTheme;
}

const QString ThemeSupport::themeId()
{
    switch (sTheme) {
    case Theme::Friction: return QStringLiteral("friction");
    case Theme::Blender: return QStringLiteral("blender");
    case Theme::MorandiDark: return QStringLiteral("morandi_dark");
    case Theme::MorandiCharcoal: return QStringLiteral("morandi_charcoal");
    case Theme::MorandiGraphite: return QStringLiteral("morandi_graphite");
    case Theme::MorandiSage: return QStringLiteral("morandi_sage");
    case Theme::MorandiKyoto: return QStringLiteral("morandi_kyoto");
    case Theme::MorandiRose: return QStringLiteral("morandi_rose");
    case Theme::MorandiMidnight: return QStringLiteral("morandi_midnight");
    case Theme::MorandiMocha: return QStringLiteral("morandi_mocha");
    case Theme::MorandiSlate: return QStringLiteral("morandi_slate");
    case Theme::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("friction");
}

void ThemeSupport::setThemeFromId(const QString &id)
{
    if (id == QStringLiteral("blender")) { sTheme = Theme::Blender; }
    else if (id == QStringLiteral("morandi_dark")) { sTheme = Theme::MorandiDark; }
    else if (id == QStringLiteral("morandi_charcoal")) { sTheme = Theme::MorandiCharcoal; }
    else if (id == QStringLiteral("morandi_graphite")) { sTheme = Theme::MorandiGraphite; }
    else if (id == QStringLiteral("morandi_sage")) { sTheme = Theme::MorandiSage; }
    else if (id == QStringLiteral("morandi_kyoto")) { sTheme = Theme::MorandiKyoto; }
    else if (id == QStringLiteral("morandi_rose")) { sTheme = Theme::MorandiRose; }
    else if (id == QStringLiteral("morandi_midnight")) { sTheme = Theme::MorandiMidnight; }
    else if (id == QStringLiteral("morandi_mocha")) { sTheme = Theme::MorandiMocha; }
    else if (id == QStringLiteral("morandi_slate")) { sTheme = Theme::MorandiSlate; }
    else if (id == QStringLiteral("custom")) { sTheme = Theme::Custom; }
    else { sTheme = Theme::Friction; }
}

const QStringList ThemeSupport::availableThemeIds()
{
    QStringList ids;
    for (const auto &preset : sThemePresets) {
        ids << preset.id;
    }
    return ids;
}

const QString ThemeSupport::themeDisplayName(const QString &id)
{
    return themePreset(id).displayName;
}

const QString ThemeSupport::getAppIconName(const bool alt)
{
    const QString name = alt ? "application-x-graphics.friction.Friction" : "graphics.friction.Friction";
    return name;
}

const QColor ThemeSupport::getQColor(int r,
                                     int g,
                                     int b,
                                     int a)
{
    return a == 255 ? QColor(r, g, b) : QColor(r, g, b, a);
}

const QColor ThemeSupport::getThemeBaseColor(int alpha)
{
    if (sTheme == Theme::Custom && sCustomBase.isValid()) {
        QColor col = sCustomBase;
        col.setAlpha(alpha);
        return col;
    }
    const auto p = themePreset(themeId());
    QColor col = p.base;
    col.setAlpha(alpha);
    return col;
}

SkColor ThemeSupport::getThemeBaseSkColor(int alpha)
{
    const QColor col = getThemeBaseColor(alpha);
    return SkColorSetARGB(col.alpha(), col.red(), col.green(), col.blue());
}

const QColor ThemeSupport::getThemeBaseDarkColor(int alpha)
{
    if (sTheme == Theme::Custom && sCustomDarker.isValid()) {
        QColor col = sCustomDarker;
        col.setAlpha(alpha);
        return col;
    }
    const auto p = themePreset(themeId());
    QColor col = p.darker;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeBaseDarkerColor(int alpha)
{
    if (sTheme == Theme::Custom && sCustomDarker.isValid()) {
        QColor col = sCustomDarker;
        col.setAlpha(alpha);
        return col;
    }
    const auto p = themePreset(themeId());
    QColor col = p.darker;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeAlternateColor(int alpha)
{
    if (sTheme == Theme::Custom && sCustomAlt.isValid()) {
        QColor col = sCustomAlt;
        col.setAlpha(alpha);
        return col;
    }
    const auto p = themePreset(themeId());
    QColor col = p.alternate;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeHighlightColor(int alpha)
{
    // If accent preset is explicitly set or custom color is used, use it
    if (sAccentPresetId == QStringLiteral("custom") && sCustomAccent.isValid()) {
        QColor col = sCustomAccent;
        col.setAlpha(alpha);
        return col;
    }
    if (sAccentPresetId != QStringLiteral("auto") && !sAccentPresetId.isEmpty()) {
        for (const auto &acc : sAccentPresets) {
            if (acc.id == sAccentPresetId && acc.color.isValid()) {
                QColor col = acc.color;
                col.setAlpha(alpha);
                return col;
            }
        }
    }
    const auto p = themePreset(themeId());
    QColor col = p.accent;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeHighlightDarkerColor(int alpha)
{
    return getLightDarkColor(getThemeHighlightColor(alpha), 125);
}

const QColor ThemeSupport::getThemeHighlightAlternativeColor(int alpha)
{
    return getLightDarkColor(getThemeHighlightColor(alpha), 115);
}

const QColor ThemeSupport::getThemeHighlightSelectedColor(int alpha)
{
    return getThemeHighlightColor(alpha);
}

SkColor ThemeSupport::getThemeHighlightSkColor(int alpha)
{
    const QColor col = getThemeHighlightColor(alpha);
    return SkColorSetARGB(col.alpha(), col.red(), col.green(), col.blue());
}

const QColor ThemeSupport::getThemeButtonBaseColor(int alpha)
{
    if (sTheme == Theme::Custom && sCustomBase.isValid()) {
        QColor col = sCustomBase.lighter(130);
        col.setAlpha(alpha);
        return col;
    }
    const auto p = themePreset(themeId());
    QColor col = p.buttonBase;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeButtonBorderColor(int alpha)
{
    if (sTheme == Theme::Custom && sCustomAlt.isValid()) {
        QColor col = sCustomAlt.lighter(125);
        col.setAlpha(alpha);
        return col;
    }
    const auto p = themePreset(themeId());
    QColor col = p.buttonBorder;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeButtonHoverColor(int alpha)
{
    if (sTheme == Theme::Custom && sCustomBase.isValid()) {
        QColor col = sCustomBase.lighter(150);
        col.setAlpha(alpha);
        return col;
    }
    const auto p = themePreset(themeId());
    QColor col = p.buttonHover;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeComboBaseColor(int alpha)
{
    return getThemeBaseDarkerColor(alpha);
}

const QColor ThemeSupport::getThemeTimelineColor(int alpha)
{
    return getThemeBaseDarkerColor(alpha);
}

const QColor ThemeSupport::getThemeToolBarColor(int alpha)
{
    const auto p = themePreset(themeId());
    QColor col = p.toolbar;
    col.setAlpha(alpha);
    return col;
}

const QColor ThemeSupport::getThemeRangeColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(58, 74, 92, alpha); }
    return getLightDarkColor(getThemeAlternateColor(alpha), 120);
}

const QColor ThemeSupport::getThemeRangeSelectedColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(94, 142, 179, alpha); }
    return getLightDarkColor(getThemeHighlightColor(alpha), 110);
}

const QColor ThemeSupport::getThemeFrameMarkerColor(int alpha)
{
    return getThemeColorOrange(alpha);
}

const QColor ThemeSupport::getThemeObjectColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(238, 158, 94, alpha); }
    return getThemeHighlightColor(alpha);
}

const QColor ThemeSupport::getThemeColorRed(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(169, 87, 95, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(199, 67, 72, alpha); }
    return getQColor(196, 140, 144, alpha); // Morandi Rose Red
}

const QColor ThemeSupport::getThemeColorBlue(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(99, 135, 210, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(73, 142, 209, alpha); }
    return getQColor(127, 155, 176, alpha); // Morandi Sky Blue
}

const QColor ThemeSupport::getThemeColorYellow(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(238, 205, 94, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(209, 183, 73, alpha); }
    return getQColor(191, 169, 128, alpha); // Morandi Gold
}

const QColor ThemeSupport::getThemeColorPink(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(158, 94, 238, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(169, 73, 209, alpha); }
    return getQColor(140, 130, 158, alpha); // Morandi Iris
}

const QColor ThemeSupport::getThemeColorGreen(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(8, 165, 129, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(73, 209, 132, alpha); }
    return getQColor(120, 163, 145, alpha); // Morandi Mint
}

const QColor ThemeSupport::getThemeColorGreenDark(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(35, 60, 45, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(27, 49, 39, alpha); }
    return getQColor(35, 48, 42, alpha);
}

const QColor ThemeSupport::getThemeColorOrange(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(238, 158, 94, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(255, 123, 0, alpha); }
    return getQColor(190, 122, 107, alpha); // Morandi Terracotta
}

const QColor ThemeSupport::getThemeColorTextDisabled(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(136, 136, 136, alpha); }
    if (sTheme == Theme::Friction) { return getQColor(112, 112, 113, alpha); }
    return getQColor(128, 126, 120, alpha);
}

const QPalette ThemeSupport::getDefaultPalette(const QColor &highlight)
{
    QPalette palette;
    palette.setColor(QPalette::Window, getThemeToolBarColor());
    palette.setColor(QPalette::WindowText, QColor(240, 240, 242));
    palette.setColor(QPalette::Base, getThemeToolBarColor());
    palette.setColor(QPalette::AlternateBase, getThemeBaseDarkerColor());
    palette.setColor(QPalette::Link, getThemeHighlightColor());
    palette.setColor(QPalette::LinkVisited, getThemeHighlightColor());
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, getThemeBaseDarkerColor());
    palette.setColor(QPalette::Text, QColor(240, 240, 242));
    palette.setColor(QPalette::Button, getThemeButtonBaseColor());
    palette.setColor(QPalette::ButtonText, QColor(240, 240, 242));
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, highlight.isValid() ? highlight : getThemeHighlightColor());
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, getThemeColorTextDisabled());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, getThemeColorTextDisabled());
    return palette;
}

const QPalette ThemeSupport::getDarkPalette(int alpha)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, getThemeBaseColor(alpha));
    pal.setColor(QPalette::Base, getThemeBaseColor(alpha));
    pal.setColor(QPalette::Button, getThemeBaseColor(alpha));
    return pal;
}

const QPalette ThemeSupport::getDarkerPalette(int alpha)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, getThemeBaseDarkerColor(alpha));
    pal.setColor(QPalette::Base, getThemeBaseDarkerColor(alpha));
    pal.setColor(QPalette::Button, getThemeBaseDarkerColor(alpha));
    return pal;
}

const QPalette ThemeSupport::getNotSoDarkPalette(int alpha)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, getThemeAlternateColor(alpha));
    pal.setColor(QPalette::Base, getThemeBaseColor(alpha));
    pal.setColor(QPalette::Button, getThemeBaseColor(alpha));
    return pal;
}

const QString ThemeSupport::getThemeStyle(int iconSize)
{
    QString css;
    QFile stylesheet(QString::fromUtf8(":/styles/friction.qss"));
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        css = stylesheet.readAll();
        stylesheet.close();
    }
    qreal dpr = 1.0;
    if (qobject_cast<QApplication*>(qApp) && QApplication::desktop()) {
        dpr = QApplication::desktop()->devicePixelRatioF();
    }
    const qreal iconPixelRatio = iconSize * dpr;
    const int r = borderRadius();
    const int sbW = scrollbarWidth();
    const int sbR = qMax(2, sbW / 2);

    css.replace(QStringLiteral("%10"), QString::number(getIconSize(iconSize / 2).width()));
    css.replace(QStringLiteral("%11"), QString::number(getIconSize(qRound(iconPixelRatio)).width()));
    css.replace(QStringLiteral("%12"), QString::number(getIconSize(qRound(iconPixelRatio / 2)).width()));
    css.replace(QStringLiteral("%13"), getThemeColorTextDisabled().name());
    css.replace(QStringLiteral("%14"), QString::number(getIconSize(iconSize).width() / 4));
    css.replace(QStringLiteral("%15"), getThemeButtonHoverColor().name());
    css.replace(QStringLiteral("%16"), getThemeToolBarColor().name());
    css.replace(QStringLiteral("%17"), QString::number(r));
    css.replace(QStringLiteral("%18"), QString::number(sbW));
    css.replace(QStringLiteral("%19"), QString::number(sbR));

    css.replace(QStringLiteral("%1"), getThemeButtonBaseColor().name());
    css.replace(QStringLiteral("%2"), getThemeButtonBorderColor().name());
    css.replace(QStringLiteral("%3"), getThemeBaseDarkerColor().name());
    css.replace(QStringLiteral("%4"), getThemeHighlightColor().name());
    css.replace(QStringLiteral("%5"), getThemeBaseColor().name());
    css.replace(QStringLiteral("%6"), getThemeAlternateColor().name());
    css.replace(QStringLiteral("%7"), QString::number(getIconSize(iconSize).width()));
    css.replace(QStringLiteral("%8"), getThemeColorOrange().name());
    css.replace(QStringLiteral("%9"), getThemeRangeSelectedColor().name());

    if (!customQss().trimmed().isEmpty()) {
        css += QStringLiteral("\n\n/* User Custom QSS */\n") + customQss();
    }

    return css;
}

void ThemeSupport::setupTheme(const int iconSize)
{
    loadThemeConfig();
    QIcon::setThemeSearchPaths(QStringList() << QString::fromUtf8(":/icons"));
    QIcon::setThemeName(QString::fromUtf8("hicolor"));
    qApp->setStyle(QString::fromUtf8("fusion"));
    qApp->setPalette(getDefaultPalette());
    qApp->setStyleSheet(getThemeStyle(iconSize));
}

void ThemeSupport::applyThemeLive(int iconSize)
{
    if (iconSize <= 0) { iconSize = 20; }
    saveThemeConfig();
    setupTheme(iconSize);

    if (qApp) {
        for (QWidget *widget : qApp->topLevelWidgets()) {
            widget->update();
            for (QWidget *child : widget->findChildren<QWidget*>()) {
                child->update();
            }
        }
    }
}

const QList<QSize> ThemeSupport::getAvailableIconSizes()
{
    return QIcon::fromTheme("visible").availableSizes();
}

const QSize ThemeSupport::getIconSize(const int size)
{
    QSize requestedSize(size, size);
    const auto iconSizes = getAvailableIconSizes();
    bool hasIconSize = iconSizes.contains(requestedSize);
    if (hasIconSize) { return requestedSize; }
    const auto foundIconSize = findClosestIconSize(size);
    return foundIconSize;
}

bool ThemeSupport::hasIconSize(const int size)
{
    return getAvailableIconSizes().contains(QSize(size, size));
}

const QSize ThemeSupport::findClosestIconSize(int iconSize)
{
    const auto iconSizes = getAvailableIconSizes();
    return *std::min_element(iconSizes.begin(),
                             iconSizes.end(),
                             [iconSize](const QSize& a,
                                        const QSize& b) {
        return qAbs(a.width() - iconSize) < qAbs(b.width() - iconSize);
    });
}

void ThemeSupport::setToolbarButtonStyle(const QString &name,
                                         QToolBar *bar,
                                         QAction *act)
{
    if (!bar || !act || name.simplified().isEmpty()) { return; }
    if (QWidget *widget = bar->widgetForAction(act)) {
        if (QToolButton *button = qobject_cast<QToolButton*>(widget)) {
            button->setObjectName(name);
        }
    }
}

const QColor ThemeSupport::getLightDarkColor(const QColor &color,
                                             const int &factor)
{
    const float lightness = color.lightnessF();
    if (lightness < 0.5f) {
        const QColor col = color.lighter(factor);
        const float minLightness = 0.3f;
        if (col.lightnessF() < minLightness) {
            QColor hslColor = color.toHsl();
            hslColor.setHslF(hslColor.hslHueF(),
                             hslColor.hslSaturationF(),
                             minLightness,
                             hslColor.alphaF());
            return hslColor.toRgb();
        }
        return col;
    }

    const QColor col = color.darker(factor);
    const float maxLightness = 0.7f;
    if (col.lightnessF() > maxLightness) {
        QColor hslColor = color.toHsl();
        hslColor.setHslF(hslColor.hslHueF(),
                         hslColor.hslSaturationF(),
                         maxLightness,
                         hslColor.alphaF());
        return hslColor.toRgb();
    }
    return col;
}

const QIcon ThemeSupport::colorizeIcon(const QIcon &icon,
                                       const QColor &color,
                                       const int size)
{
    if (icon.isNull() || !color.isValid()) { return icon; }
    const QPixmap src = icon.pixmap(QSize(size, size));
    if (src.isNull()) { return icon; }
    QPixmap pix = src;
    QPainter p(&pix);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pix.rect(), color);
    p.end();
    return QIcon(pix);
}

const QIcon ThemeSupport::themedToolIcon(const QString &name,
                                         const QColor &color,
                                         const int size)
{
    const QIcon icon = QIcon::fromTheme(name);
    if (sTheme != Theme::Blender || !color.isValid()) { return icon; }
    return colorizeIcon(icon, color, size);
}

ThemeIconProvider::ThemeIconProvider()
{
    mIcon = QIcon::fromTheme(ThemeSupport::getAppIconName(true));
}

QIcon ThemeIconProvider::icon(const QFileInfo &info) const
{
    const QString name = info.fileName().toLower();
    if (name.endsWith(".friction")) { return mIcon; }
    {
        static QHash<QString, QIcon> thumbCache;
        const QString suffix = info.suffix().toLower();
        const auto fmts = QImageReader::supportedImageFormats();
        if(!suffix.isEmpty() && fmts.contains(suffix.toUtf8())) {
            const QString key = info.absoluteFilePath();
            const auto cached = thumbCache.constFind(key);
            if(cached != thumbCache.constEnd()) return cached.value();
            constexpr qint64 maxBytes = 64*1024*1024;
            if(info.size() > 0 && info.size() < maxBytes) {
                QImageReader reader(info.absoluteFilePath());
                const QSize full = reader.size();
                if(full.isValid()) {
                    QSize scaled = full;
                    scaled.scale(64, 64, Qt::KeepAspectRatio);
                    reader.setScaledSize(scaled);
                    const QImage img = reader.read();
                    if(!img.isNull()) {
                        const QIcon ic(QPixmap::fromImage(img));
                        if(thumbCache.size() > 1024) thumbCache.clear();
                        thumbCache.insert(key, ic);
                        return ic;
                    }
                }
            }
        }
    }
    const QString suf = info.suffix().toLower();
    if (suf == QLatin1String("psd") || suf == QLatin1String("psb")) {
        return QFileIconProvider::icon(info);
    }
    if (info.isDir()) {
        return QFileIconProvider::icon(info);
    }
    return QFileIconProvider::icon(QFileIconProvider::File);
}

