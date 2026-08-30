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

#ifndef THEMESUPPORT_H
#define THEMESUPPORT_H

#include "core_global.h"

#include <QPalette>
#include <QList>
#include <QSize>
#include <QToolBar>
#include <QFileIconProvider>
#include <QIcon>
#include <QMap>

#include "include/core/SkColor.h"

struct CORE_EXPORT ThemePresetInfo
{
    QString id;
    QString displayName;
    QString description;
    QColor base;
    QColor alternate;
    QColor darker;
    QColor accent;
    QColor buttonBase;
    QColor buttonBorder;
    QColor buttonHover;
    QColor toolbar;
    int defaultRadius = 6;
};

struct CORE_EXPORT AccentPresetInfo
{
    QString id;
    QString name;
    QColor color;
};

class CORE_EXPORT ThemeSupport
{
public:
    enum class Theme {
        Friction,
        Blender,
        MorandiDark,
        MorandiCharcoal,
        MorandiGraphite,
        MorandiSage,
        MorandiKyoto,
        MorandiRose,
        MorandiMidnight,
        MorandiMocha,
        MorandiSlate,
        Custom
    };

    static void setTheme(const Theme theme);
    static Theme theme();
    static const QString themeId();
    static void setThemeFromId(const QString &id);
    static const QStringList availableThemeIds();
    static const QString themeDisplayName(const QString &id);

    static const QList<ThemePresetInfo>& themePresetList();
    static const ThemePresetInfo themePreset(const QString &id);

    static const QList<AccentPresetInfo>& accentPresetList();
    static const QString accentPresetId();
    static void setAccentPresetId(const QString &id);

    static void setCustomAccentColor(const QColor &color);
    static const QColor customAccentColor();
    static void setCustomBaseColor(const QColor &color);
    static const QColor customBaseColor();
    static void setCustomAlternateColor(const QColor &color);
    static const QColor customAlternateColor();
    static void setCustomDarkerColor(const QColor &color);
    static const QColor customDarkerColor();

    static int borderRadius();
    static void setBorderRadius(int radius);

    static int scrollbarWidth();
    static void setScrollbarWidth(int width);

    static const QString customQss();
    static void setCustomQss(const QString &qss);

    static void loadThemeConfig();
    static void saveThemeConfig();
    static void applyThemeLive(int iconSize = -1);

    static const QString getAppIconName(const bool alt = false);
    static const QColor getQColor(int r, int g, int b, int a = 255);
    static const QColor getThemeBaseColor(int alpha = 255);
    static SkColor getThemeBaseSkColor(int alpha = 255);
    static const QColor getThemeBaseDarkColor(int alpha = 255);
    static const QColor getThemeBaseDarkerColor(int alpha = 255);
    static const QColor getThemeAlternateColor(int alpha = 255);
    static const QColor getThemeHighlightColor(int alpha = 255);
    static const QColor getThemeHighlightDarkerColor(int alpha = 255);
    static const QColor getThemeHighlightAlternativeColor(int alpha = 255);
    static const QColor getThemeHighlightSelectedColor(int alpha = 255);
    static SkColor getThemeHighlightSkColor(int alpha = 255);
    static const QColor getThemeButtonBaseColor(int alpha = 255);
    static const QColor getThemeButtonBorderColor(int alpha = 255);
    static const QColor getThemeButtonHoverColor(int alpha = 255);
    static const QColor getThemeComboBaseColor(int alpha = 255);
    static const QColor getThemeTimelineColor(int alpha = 255);
    static const QColor getThemeToolBarColor(int alpha = 255);
    static const QColor getThemeRangeColor(int alpha = 255);
    static const QColor getThemeRangeSelectedColor(int alpha = 255);
    static const QColor getThemeFrameMarkerColor(int alpha = 255);
    static const QColor getThemeObjectColor(int alpha = 255);
    static const QColor getThemeColorRed(int alpha = 255);
    static const QColor getThemeColorBlue(int alpha = 255);
    static const QColor getThemeColorYellow(int alpha = 255);
    static const QColor getThemeColorPink(int alpha = 255);
    static const QColor getThemeColorGreen(int alpha = 255);
    static const QColor getThemeColorGreenDark(int alpha = 255);
    static const QColor getThemeColorOrange(int alpha = 255);
    static const QColor getThemeColorTextDisabled(int alpha = 255);
    static const QPalette getDefaultPalette(const QColor &highlight = QColor());
    static const QPalette getDarkPalette(int alpha = 255);
    static const QPalette getDarkerPalette(int alpha = 255);
    static const QPalette getNotSoDarkPalette(int alpha = 255);
    static const QString getThemeStyle(int iconSize = 20);
    static void setupTheme(const int iconSize = 20);
    static const QList<QSize> getAvailableIconSizes();
    static const QSize getIconSize(const int size);
    static bool hasIconSize(const int size);
    static const QSize findClosestIconSize(int iconSize);
    static void setToolbarButtonStyle(const QString &name,
                                      QToolBar *bar,
                                      QAction *act);
    static const QColor getLightDarkColor(const QColor &color,
                                          const int &factor);
    /** Tint a monochrome icon to the given color (alpha preserved,
        used for Blender theme tool/panel icons). */
    static const QIcon colorizeIcon(const QIcon &icon,
                                    const QColor &color,
                                    const int size = 64);
    /** Tint a theme icon by name; returns the default icon when the
        color is invalid (non-Blender themes keep the default white). */
    static const QIcon themedToolIcon(const QString &name,
                                      const QColor &color,
                                      const int size = 64);
};

class CORE_EXPORT ThemeIconProvider : public QFileIconProvider
{
public:
    ThemeIconProvider();
    QIcon icon(const QFileInfo & info) const;

private:
    QIcon mIcon;
};

#endif // THEMESUPPORT_H
