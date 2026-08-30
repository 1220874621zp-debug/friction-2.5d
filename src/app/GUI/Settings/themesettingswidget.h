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

#ifndef THEMESETTINGSWIDGET_H
#define THEMESETTINGSWIDGET_H

#include "widgets/settingswidget.h"
#include "themesupport.h"

#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QTabWidget>
#include <QLabel>

class ThemeSettingsWidget : public SettingsWidget
{
    Q_OBJECT
public:
    explicit ThemeSettingsWidget(QWidget *parent = nullptr);

    void applySettings() override;
    void updateSettings(bool restore = false) override;

private slots:
    void onThemePresetChanged(int index);
    void onAccentPresetChanged(int index);
    void pickCustomAccentColor();
    void pickCustomBaseColor();
    void pickCustomAltColor();
    void pickCustomDarkerColor();
    void resetColorsToDefaults();
    void applyThemeLive();

private:
    void initGalleryTab(QTabWidget *tabs);
    void initColorsTab(QTabWidget *tabs);
    void initControlsTab(QTabWidget *tabs);
    void initCustomQssTab(QTabWidget *tabs);
    void updateColorButtons();
    void applyPresetFromGallery(const QString &themeId);

    QComboBox *mThemeCombo = nullptr;
    QComboBox *mAccentCombo = nullptr;
    QPushButton *mCustomAccentBtn = nullptr;
    QPushButton *mCustomBaseBtn = nullptr;
    QPushButton *mCustomAltBtn = nullptr;
    QPushButton *mCustomDarkerBtn = nullptr;

    QComboBox *mRadiusCombo = nullptr;
    QComboBox *mScrollbarCombo = nullptr;
    QTextEdit *mCustomQssEdit = nullptr;

    QString mCurrentThemeId;
    QString mCurrentAccentId;
    QColor mCurrentCustomAccent;
    QColor mCurrentCustomBase;
    QColor mCurrentCustomAlt;
    QColor mCurrentCustomDarker;
    int mCurrentRadius = 6;
    int mCurrentScrollbar = 6;
    QString mCurrentQss;
};

#endif // THEMESETTINGSWIDGET_H
