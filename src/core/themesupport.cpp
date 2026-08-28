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
#include <QImageReader>

#include <QFile>
#include <QIcon>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QToolButton>
#include <QPixmap>
#include <QPainter>

namespace {
ThemeSupport::Theme sTheme = ThemeSupport::Theme::Friction;
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
    return sTheme == Theme::Blender ? QStringLiteral("blender")
                                    : QStringLiteral("friction");
}

void ThemeSupport::setThemeFromId(const QString &id)
{
    sTheme = id == QStringLiteral("blender") ? Theme::Blender
                                             : Theme::Friction;
}

const QStringList ThemeSupport::availableThemeIds()
{
    return {QStringLiteral("friction"), QStringLiteral("blender")};
}

const QString ThemeSupport::themeDisplayName(const QString &id)
{
    return id == QStringLiteral("blender") ? QStringLiteral("Blender")
                                           : QStringLiteral("Friction");
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
    if (sTheme == Theme::Blender) { return getQColor(34, 34, 34, alpha); }
    return getQColor(26, 26, 30, alpha);
}

SkColor ThemeSupport::getThemeBaseSkColor(int alpha)
{
    if (sTheme == Theme::Blender) { return SkColorSetARGB(alpha, 34, 34, 34); }
    return SkColorSetARGB(alpha, 26, 26, 30);
}

const QColor ThemeSupport::getThemeBaseDarkColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(26, 26, 26, alpha); }
    return getQColor(25, 25, 25, alpha);
}

const QColor ThemeSupport::getThemeBaseDarkerColor(int alpha)
{
    // Blender: #1D1D1D (menu bar / input base, qss %3)
    if (sTheme == Theme::Blender) { return getQColor(29, 29, 29, alpha); }
    return getQColor(19, 19, 21, alpha);
}

const QColor ThemeSupport::getThemeAlternateColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(43, 43, 43, alpha); }
    return getQColor(33, 33, 39, alpha);
}

const QColor ThemeSupport::getThemeHighlightColor(int alpha)
{
    // Blender: #4772B3 (selection highlight for menu/tool/property)
    if (sTheme == Theme::Blender) { return getQColor(71, 114, 179, alpha); }
    return getQColor(104, 144, 206, alpha);
}

const QColor ThemeSupport::getThemeHighlightDarkerColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(78, 126, 163, alpha); }
    return getQColor(53, 101, 176, alpha);
}

const QColor ThemeSupport::getThemeHighlightAlternativeColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(111, 158, 195, alpha); }
    return getQColor(167, 185, 222, alpha);
}

const QColor ThemeSupport::getThemeHighlightSelectedColor(int alpha)
{
    // Blender: #4772B3 (timeline layer/box selection fill, same as highlight)
    if (sTheme == Theme::Blender) { return getQColor(71, 114, 179, alpha); }
    return getQColor(150, 191, 255, alpha);
}

SkColor ThemeSupport::getThemeHighlightSkColor(int alpha)
{
    // Blender: #4772B3 (kept in sync with getThemeHighlightColor)
    if (sTheme == Theme::Blender) { return SkColorSetARGB(alpha, 71, 114, 179); }
    return SkColorSetARGB(alpha, 104, 144, 206);
}

const QColor ThemeSupport::getThemeButtonBaseColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(74, 74, 74, alpha); }
    return getQColor(49, 49, 59, alpha);
}

const QColor ThemeSupport::getThemeButtonBorderColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(62, 62, 62, alpha); }
    return getQColor(65, 65, 80, alpha);
}

const QColor ThemeSupport::getThemeButtonHoverColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(84, 84, 84, alpha); }
    return getThemeBaseDarkerColor(alpha);
}

const QColor ThemeSupport::getThemeComboBaseColor(int alpha)
{
    // Blender: #1D1D1D (combo/input base)
    if (sTheme == Theme::Blender) { return getQColor(29, 29, 29, alpha); }
    return getQColor(36, 36, 53, alpha);
}

const QColor ThemeSupport::getThemeTimelineColor(int alpha)
{
    // Blender: #1D1D1D (timeline ruler base)
    if (sTheme == Theme::Blender) { return getQColor(29, 29, 29, alpha); }
    return getQColor(44, 44, 49, alpha);
}

const QColor ThemeSupport::getThemeToolBarColor(int alpha)
{
    // Blender: #1D1D1D toolbar / timeline ruler background
    if (sTheme == Theme::Blender) { return getQColor(29, 29, 29, alpha); }
    return getQColor(19, 19, 21, alpha);
}

const QColor ThemeSupport::getThemeRangeColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(58, 74, 92, alpha); }
    return getQColor(56, 73, 101, alpha);
}

const QColor ThemeSupport::getThemeRangeSelectedColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(94, 142, 179, alpha); }
    return getQColor(87, 120, 173, alpha);
}

const QColor ThemeSupport::getThemeFrameMarkerColor(int alpha)
{
    return getThemeColorOrange(alpha);
}

const QColor ThemeSupport::getThemeObjectColor(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(238, 158, 94, alpha); }
    return getQColor(0, 102, 255, alpha);
}

const QColor ThemeSupport::getThemeColorRed(int alpha)
{
    // Blender: #A9575F (tool icon red)
    if (sTheme == Theme::Blender) { return getQColor(169, 87, 95, alpha); }
    return getQColor(199, 67, 72, alpha);
}

const QColor ThemeSupport::getThemeColorBlue(int alpha)
{
    // Blender: #6387D2 (tool icon blue)
    if (sTheme == Theme::Blender) { return getQColor(99, 135, 210, alpha); }
    return getQColor(73, 142, 209, alpha);
}

const QColor ThemeSupport::getThemeColorYellow(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(238, 205, 94, alpha); }
    return getQColor(209, 183, 73, alpha);
}

const QColor ThemeSupport::getThemeColorPink(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(158, 94, 238, alpha); }
    return getQColor(169, 73, 209, alpha);
}

const QColor ThemeSupport::getThemeColorGreen(int alpha)
{
    // Blender: #08A581 (tool icon green)
    if (sTheme == Theme::Blender) { return getQColor(8, 165, 129, alpha); }
    return getQColor(73, 209, 132, alpha);
}

const QColor ThemeSupport::getThemeColorGreenDark(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(35, 60, 45, alpha); }
    return getQColor(27, 49, 39, alpha);
}

const QColor ThemeSupport::getThemeColorOrange(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(238, 158, 94, alpha); }
    return getQColor(255, 123, 0, alpha);
}

const QColor ThemeSupport::getThemeColorTextDisabled(int alpha)
{
    if (sTheme == Theme::Blender) { return getQColor(136, 136, 136, alpha); }
    return getQColor(112, 112, 113, alpha);
}

const QPalette ThemeSupport::getDefaultPalette(const QColor &highlight)
{
    QPalette palette;
    palette.setColor(QPalette::Window, getThemeAlternateColor());
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, getThemeBaseColor());
    palette.setColor(QPalette::AlternateBase, getThemeAlternateColor());
    palette.setColor(QPalette::Link, Qt::white);
    palette.setColor(QPalette::LinkVisited, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, Qt::black);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, getThemeBaseColor());
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, highlight.isValid() ? highlight : getThemeHighlightColor());
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, Qt::darkGray);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::darkGray);
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
    const qreal iconPixelRatio = iconSize * qApp->desktop()->devicePixelRatioF();
    return css.arg(getThemeButtonBaseColor().name(),
                   getThemeButtonBorderColor().name(),
                   getThemeBaseDarkerColor().name(),
                   getThemeHighlightColor().name(),
                   getThemeBaseColor().name(),
                   getThemeAlternateColor().name(),
                   QString::number(getIconSize(iconSize).width()),
                   getThemeColorOrange().name(),
                   getThemeRangeSelectedColor().name(),
                   QString::number(getIconSize(iconSize / 2).width()),
                   QString::number(getIconSize(qRound(iconPixelRatio)).width()),
                   QString::number(getIconSize(qRound(iconPixelRatio / 2)).width()),
                   getThemeColorTextDisabled().name(),
                   QString::number(getIconSize(iconSize).width() / 4),
                   getThemeButtonHoverColor().name(),
                   getThemeToolBarColor().name());
}

void ThemeSupport::setupTheme(const int iconSize)
{
    QIcon::setThemeSearchPaths(QStringList() << QString::fromUtf8(":/icons"));
    QIcon::setThemeName(QString::fromUtf8("hicolor"));
    qApp->setStyle(QString::fromUtf8("fusion"));
    qApp->setPalette(getDefaultPalette());
    qApp->setStyleSheet(getThemeStyle(iconSize));
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
    // Render at a single 'size': the SVG is rasterized sharply at this
    // size, then Qt scales it to any button size (same scale path as the
    // white fromTheme icons), so colored icons match the white ones.
    const QPixmap src = icon.pixmap(QSize(size, size));
    if (src.isNull()) { return icon; }
    // Tint the source pixmap in place (implicit copy) so it keeps its
    // devicePixelRatio. With AA_UseHighDpiPixmaps icon.pixmap() returns
    // size*dpr physical pixels with the ratio set; drawing it onto a
    // fresh dpr=1.0 canvas would only cover its top-left corner and the
    // icon would render at 1/dpr of the intended size (half size at 200%).
    QPixmap pix = src;
    QPainter p(&pix);
    // Keep the original alpha, tint the pixels to the target color.
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
    // Tint only for the Blender theme; other themes keep the default icon.
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
    // real image thumbnails for formats Qt can decode itself (png/jpg/
    // bmp/gif/webp/tiff/...): decoded + downscaled HERE, never via the
    // Windows shell (shell icon handlers may hang on huge files).
    // PSD & friends keep the generic icon (no decoder available here).
    {
        static QHash<QString, QIcon> thumbCache;
        const QString suffix = info.suffix().toLower();
        const auto fmts = QImageReader::supportedImageFormats();
        if(!suffix.isEmpty() && fmts.contains(suffix.toUtf8())) {
            const QString key = info.absoluteFilePath();
            const auto cached = thumbCache.constFind(key);
            if(cached != thumbCache.constEnd()) return cached.value();
            // skip absurdly big files - decoding those would stall the
            // dialog (the very hang the fork avoids by not using the
            // native dialog)
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
    // PSD/PSB: go through the system shell icon (SHGetFileInfo) so a
    // installed PSD codec shows real thumbnails in the dialog; without
    // a codec the shell returns a generic icon (modern codecs do not
    // hang - the old blanket shell ban was for legacy handlers)
    const QString suf = info.suffix().toLower();
    if (suf == QLatin1String("psd") || suf == QLatin1String("psb")) {
        return QFileIconProvider::icon(info);
    }
    // Never query the Windows shell for per-file icons: shell
    // extensions (e.g. the Photoshop PSD handler) may hang on large
    // files. Generic icons are used for files instead. Directories
    // are safe (no per-file icon handlers are involved) and use the
    // real system icon, so known folders (Desktop/Music/Pictures)
    // and drives keep their distinct icons in the file dialog
    // sidebar and the folder list.
    if (info.isDir()) {
        return QFileIconProvider::icon(info);
    }
    return QFileIconProvider::icon(QFileIconProvider::File);
}
