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

#include "themesettingswidget.h"
#include "themesupport.h"
#include "appsupport.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QColorDialog>
#include <QMessageBox>

namespace {

void styleColorButton(QPushButton *btn, const QColor &color)
{
    if (!btn) { return; }
    if (!color.isValid()) {
        btn->setText(QObject::tr("Default"));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { padding: 4px 10px; border-radius: 4px; border: 1px solid #555; }"
        ));
        return;
    }
    btn->setText(color.name().toUpper());
    const QString textColor = (color.lightnessF() > 0.55) ? QStringLiteral("#1a1a1a") : QStringLiteral("#f8f8f8");
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; font-weight: bold; padding: 4px 12px; border-radius: 4px; border: 1px solid rgba(255,255,255,0.25); }"
        "QPushButton:hover { border: 1px solid #ffffff; }"
    ).arg(color.name(), textColor));
}

} // namespace

ThemeSettingsWidget::ThemeSettingsWidget(QWidget *parent)
    : SettingsWidget(parent)
{
    const auto mainWidget = new QWidget(this);
    const auto mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    const auto tabWidget = new QTabWidget(this);
    tabWidget->setObjectName(QStringLiteral("ThemeTabs"));

    initGalleryTab(tabWidget);
    initColorsTab(tabWidget);
    initControlsTab(tabWidget);
    initCustomQssTab(tabWidget);

    mainLayout->addWidget(tabWidget);

    // Live preview action bar
    const auto previewRow = new QWidget(this);
    const auto previewLayout = new QHBoxLayout(previewRow);
    previewLayout->setContentsMargins(4, 8, 4, 4);

    const auto tipLabel = new QLabel(tr("Click 'Live Preview' to inspect changes immediately without restarting."), this);
    tipLabel->setStyleSheet(QStringLiteral("color: #a0a0a8; font-size: 11px;"));
    const auto previewBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Live Preview Theme"), this);
    previewBtn->setStyleSheet(QStringLiteral("font-weight: bold; padding: 5px 14px;"));

    previewLayout->addWidget(tipLabel);
    previewLayout->addStretch();
    previewLayout->addWidget(previewBtn);

    connect(previewBtn, &QPushButton::clicked, this, &ThemeSettingsWidget::applyThemeLive);

    mainLayout->addWidget(previewRow);

    addWidget(mainWidget);

    updateSettings(false);
}

void ThemeSettingsWidget::initGalleryTab(QTabWidget *tabs)
{
    const auto galleryTab = new QWidget(tabs);
    const auto galleryLayout = new QVBoxLayout(galleryTab);
    galleryLayout->setContentsMargins(4, 6, 4, 4);

    const auto headerLabel = new QLabel(tr("Select any Morandi, Blender, or Friction theme card to apply full palette and styling:"), galleryTab);
    headerLabel->setWordWrap(true);
    galleryLayout->addWidget(headerLabel);

    const auto scrollArea = new QScrollArea(galleryTab);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    const auto scrollContent = new QWidget(scrollArea);
    const auto grid = new QGridLayout(scrollContent);
    grid->setSpacing(10);
    grid->setContentsMargins(4, 4, 4, 4);

    const auto &presets = ThemeSupport::themePresetList();
    int row = 0;
    int col = 0;

    for (const auto &preset : presets) {
        if (preset.id == QStringLiteral("custom")) { continue; }

        const auto card = new QGroupBox(preset.displayName, scrollContent);
        card->setObjectName(QStringLiteral("BlueBox"));
        const auto cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 24, 8, 8);
        cardLayout->setSpacing(6);

        const auto descLabel = new QLabel(preset.description, card);
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet(QStringLiteral("color: #b0b0b8; font-size: 11px;"));
        cardLayout->addWidget(descLabel);

        const auto swatchesRow = new QHBoxLayout();
        swatchesRow->setSpacing(4);

        const QList<QColor> colors = {preset.base, preset.alternate, preset.darker, preset.accent};
        const QStringList titles = {tr("Base"), tr("Alt"), tr("Dark"), tr("Accent")};
        for (int i = 0; i < colors.size(); ++i) {
            const auto swatch = new QLabel(card);
            swatch->setFixedHeight(22);
            swatch->setToolTip(QStringLiteral("%1: %2").arg(titles.at(i), colors.at(i).name()));
            swatch->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px; border: 1px solid rgba(255,255,255,0.15);").arg(colors.at(i).name()));
            swatchesRow->addWidget(swatch);
        }
        cardLayout->addLayout(swatchesRow);

        const auto applyBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), tr("Apply Theme"), card);
        applyBtn->setFocusPolicy(Qt::NoFocus);
        const QString themeId = preset.id;
        connect(applyBtn, &QPushButton::clicked, this, [this, themeId]() {
            applyPresetFromGallery(themeId);
        });

        cardLayout->addWidget(applyBtn);

        grid->addWidget(card, row, col);
        col++;
        if (col > 1) {
            col = 0;
            row++;
        }
    }

    scrollArea->setWidget(scrollContent);
    galleryLayout->addWidget(scrollArea);
    tabs->addTab(galleryTab, tr("Theme Gallery"));
}

void ThemeSettingsWidget::initColorsTab(QTabWidget *tabs)
{
    const auto colorsTab = new QWidget(tabs);
    const auto colorsLayout = new QVBoxLayout(colorsTab);
    colorsLayout->setContentsMargins(6, 8, 6, 6);

    // Group 1: Theme Preset Selector
    const auto themeGroup = new QGroupBox(tr("Theme Preset"), colorsTab);
    themeGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto themeLayout = new QFormLayout(themeGroup);

    mThemeCombo = new QComboBox(themeGroup);
    for (const auto &preset : ThemeSupport::themePresetList()) {
        mThemeCombo->addItem(preset.displayName, preset.id);
    }
    connect(mThemeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ThemeSettingsWidget::onThemePresetChanged);
    themeLayout->addRow(tr("Active Theme:"), mThemeCombo);
    colorsLayout->addWidget(themeGroup);

    // Group 2: Accent Color
    const auto accentGroup = new QGroupBox(tr("Accent & Highlight Color"), colorsTab);
    accentGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto accentLayout = new QFormLayout(accentGroup);

    mAccentCombo = new QComboBox(accentGroup);
    for (const auto &acc : ThemeSupport::accentPresetList()) {
        mAccentCombo->addItem(acc.name, acc.id);
    }
    connect(mAccentCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ThemeSettingsWidget::onAccentPresetChanged);

    mCustomAccentBtn = new QPushButton(tr("Pick Custom..."), accentGroup);
    connect(mCustomAccentBtn, &QPushButton::clicked, this, &ThemeSettingsWidget::pickCustomAccentColor);

    const auto accRow = new QHBoxLayout();
    accRow->addWidget(mAccentCombo, 1);
    accRow->addWidget(mCustomAccentBtn);
    accentLayout->addRow(tr("Accent Preset:"), accRow);
    colorsLayout->addWidget(accentGroup);

    // Group 3: Background Tone Overrides
    const auto toneGroup = new QGroupBox(tr("Background Tones (Custom Overrides)"), colorsTab);
    toneGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto toneLayout = new QFormLayout(toneGroup);

    mCustomBaseBtn = new QPushButton(toneGroup);
    connect(mCustomBaseBtn, &QPushButton::clicked, this, &ThemeSettingsWidget::pickCustomBaseColor);
    toneLayout->addRow(tr("Window Base Color:"), mCustomBaseBtn);

    mCustomAltBtn = new QPushButton(toneGroup);
    connect(mCustomAltBtn, &QPushButton::clicked, this, &ThemeSettingsWidget::pickCustomAltColor);
    toneLayout->addRow(tr("Panel Alternate Color:"), mCustomAltBtn);

    mCustomDarkerBtn = new QPushButton(toneGroup);
    connect(mCustomDarkerBtn, &QPushButton::clicked, this, &ThemeSettingsWidget::pickCustomDarkerColor);
    toneLayout->addRow(tr("Input / Darker Color:"), mCustomDarkerBtn);

    const auto resetColorsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")), tr("Reset Colors to Preset Defaults"), toneGroup);
    connect(resetColorsBtn, &QPushButton::clicked, this, &ThemeSettingsWidget::resetColorsToDefaults);
    toneLayout->addRow(QString(), resetColorsBtn);

    colorsLayout->addWidget(toneGroup);
    colorsLayout->addStretch();
    tabs->addTab(colorsTab, tr("Colors & Tones"));
}

void ThemeSettingsWidget::initControlsTab(QTabWidget *tabs)
{
    const auto controlsTab = new QWidget(tabs);
    const auto controlsLayout = new QVBoxLayout(controlsTab);
    controlsLayout->setContentsMargins(6, 8, 6, 6);

    const auto uiGroup = new QGroupBox(tr("UI Geometry & Components"), controlsTab);
    uiGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto uiForm = new QFormLayout(uiGroup);

    mRadiusCombo = new QComboBox(uiGroup);
    mRadiusCombo->addItem(tr("Sharp Corners (0px)"), 0);
    mRadiusCombo->addItem(tr("Minimal Radius (2px)"), 2);
    mRadiusCombo->addItem(tr("Compact Radius (4px)"), 4);
    mRadiusCombo->addItem(tr("Standard Radius (6px)"), 6);
    mRadiusCombo->addItem(tr("Soft Rounded (8px)"), 8);
    mRadiusCombo->addItem(tr("Large Radius (12px)"), 12);
    uiForm->addRow(tr("Corner Radius:"), mRadiusCombo);

    mScrollbarCombo = new QComboBox(uiGroup);
    mScrollbarCombo->addItem(tr("Hidden Scrollbar (0px)"), 0);
    mScrollbarCombo->addItem(tr("Slim Scrollbar (4px)"), 4);
    mScrollbarCombo->addItem(tr("Standard Scrollbar (6px)"), 6);
    mScrollbarCombo->addItem(tr("Medium Scrollbar (8px)"), 8);
    mScrollbarCombo->addItem(tr("Large Scrollbar (12px)"), 12);
    uiForm->addRow(tr("Scrollbar Width:"), mScrollbarCombo);

    controlsLayout->addWidget(uiGroup);
    controlsLayout->addStretch();
    tabs->addTab(controlsTab, tr("UI & Controls"));
}

void ThemeSettingsWidget::initCustomQssTab(QTabWidget *tabs)
{
    const auto qssTab = new QWidget(tabs);
    const auto qssLayout = new QVBoxLayout(qssTab);
    qssLayout->setContentsMargins(6, 8, 6, 6);

    const auto qssGroup = new QGroupBox(tr("Custom Stylesheet (QSS Injection)"), qssTab);
    qssGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto qssGroupLayout = new QVBoxLayout(qssGroup);

    const auto hint = new QLabel(tr("Enter custom Qt Stylesheet (QSS) rules below to override or extend the active theme:"), qssGroup);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #b0b0b8; font-size: 11px;"));
    qssGroupLayout->addWidget(hint);

    mCustomQssEdit = new QTextEdit(qssGroup);
    mCustomQssEdit->setPlaceholderText(QStringLiteral("/* Example:\nQPushButton { font-weight: bold; }\nQToolBar { padding: 4px; }\n*/"));
    mCustomQssEdit->setFont(QFont(QStringLiteral("monospace"), 10));
    qssGroupLayout->addWidget(mCustomQssEdit);

    const auto clearQssBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), tr("Clear Custom QSS"), qssGroup);
    connect(clearQssBtn, &QPushButton::clicked, this, [this]() {
        if (mCustomQssEdit) { mCustomQssEdit->clear(); }
    });
    qssGroupLayout->addWidget(clearQssBtn, 0, Qt::AlignRight);

    qssLayout->addWidget(qssGroup);
    tabs->addTab(qssTab, tr("Custom QSS"));
}

void ThemeSettingsWidget::updateColorButtons()
{
    const auto p = ThemeSupport::themePreset(mCurrentThemeId);
    styleColorButton(mCustomAccentBtn, mCurrentCustomAccent.isValid() ? mCurrentCustomAccent : p.accent);
    styleColorButton(mCustomBaseBtn, mCurrentCustomBase.isValid() ? mCurrentCustomBase : p.base);
    styleColorButton(mCustomAltBtn, mCurrentCustomAlt.isValid() ? mCurrentCustomAlt : p.alternate);
    styleColorButton(mCustomDarkerBtn, mCurrentCustomDarker.isValid() ? mCurrentCustomDarker : p.darker);
}

void ThemeSettingsWidget::applyPresetFromGallery(const QString &themeId)
{
    const int idx = mThemeCombo->findData(themeId);
    if (idx >= 0) {
        mThemeCombo->setCurrentIndex(idx);
    }
    const auto p = ThemeSupport::themePreset(themeId);
    mCurrentThemeId = themeId;
    mCurrentAccentId = QStringLiteral("auto");
    mCurrentCustomAccent = QColor();
    mCurrentCustomBase = QColor();
    mCurrentCustomAlt = QColor();
    mCurrentCustomDarker = QColor();
    mCurrentRadius = p.defaultRadius;

    const int accIdx = mAccentCombo->findData(QStringLiteral("auto"));
    if (accIdx >= 0) { mAccentCombo->setCurrentIndex(accIdx); }

    const int radIdx = mRadiusCombo->findData(p.defaultRadius);
    if (radIdx >= 0) { mRadiusCombo->setCurrentIndex(radIdx); }

    updateColorButtons();
    applyThemeLive();
}

void ThemeSettingsWidget::onThemePresetChanged(int index)
{
    if (index < 0) { return; }
    mCurrentThemeId = mThemeCombo->itemData(index).toString();
    const auto p = ThemeSupport::themePreset(mCurrentThemeId);
    if (p.id != QStringLiteral("custom") && !mCurrentCustomBase.isValid()) {
        const int radIdx = mRadiusCombo->findData(p.defaultRadius);
        if (radIdx >= 0) { mRadiusCombo->setCurrentIndex(radIdx); }
    }
    updateColorButtons();
}

void ThemeSettingsWidget::onAccentPresetChanged(int index)
{
    if (index < 0) { return; }
    mCurrentAccentId = mAccentCombo->itemData(index).toString();
    if (mCurrentAccentId != QStringLiteral("custom")) {
        mCurrentCustomAccent = QColor();
    }
    updateColorButtons();
}

void ThemeSettingsWidget::pickCustomAccentColor()
{
    const auto p = ThemeSupport::themePreset(mCurrentThemeId);
    const QColor initial = mCurrentCustomAccent.isValid() ? mCurrentCustomAccent : p.accent;
    const QColor col = QColorDialog::getColor(initial, this, tr("Select Accent Color"));
    if (col.isValid()) {
        mCurrentCustomAccent = col;
        const int custIdx = mAccentCombo->findData(QStringLiteral("custom"));
        if (custIdx >= 0) { mAccentCombo->setCurrentIndex(custIdx); }
        updateColorButtons();
    }
}

void ThemeSettingsWidget::pickCustomBaseColor()
{
    const auto p = ThemeSupport::themePreset(mCurrentThemeId);
    const QColor initial = mCurrentCustomBase.isValid() ? mCurrentCustomBase : p.base;
    const QColor col = QColorDialog::getColor(initial, this, tr("Select Base Window Color"));
    if (col.isValid()) {
        mCurrentCustomBase = col;
        updateColorButtons();
    }
}

void ThemeSettingsWidget::pickCustomAltColor()
{
    const auto p = ThemeSupport::themePreset(mCurrentThemeId);
    const QColor initial = mCurrentCustomAlt.isValid() ? mCurrentCustomAlt : p.alternate;
    const QColor col = QColorDialog::getColor(initial, this, tr("Select Alternate Panel Color"));
    if (col.isValid()) {
        mCurrentCustomAlt = col;
        updateColorButtons();
    }
}

void ThemeSettingsWidget::pickCustomDarkerColor()
{
    const auto p = ThemeSupport::themePreset(mCurrentThemeId);
    const QColor initial = mCurrentCustomDarker.isValid() ? mCurrentCustomDarker : p.darker;
    const QColor col = QColorDialog::getColor(initial, this, tr("Select Input / Darker Color"));
    if (col.isValid()) {
        mCurrentCustomDarker = col;
        updateColorButtons();
    }
}

void ThemeSettingsWidget::resetColorsToDefaults()
{
    mCurrentCustomAccent = QColor();
    mCurrentCustomBase = QColor();
    mCurrentCustomAlt = QColor();
    mCurrentCustomDarker = QColor();
    const int accIdx = mAccentCombo->findData(QStringLiteral("auto"));
    if (accIdx >= 0) { mAccentCombo->setCurrentIndex(accIdx); }
    updateColorButtons();
}

void ThemeSettingsWidget::applyThemeLive()
{
    applySettings();
    ThemeSupport::applyThemeLive();
}

void ThemeSettingsWidget::applySettings()
{
    mCurrentThemeId = mThemeCombo->currentData().toString();
    mCurrentAccentId = mAccentCombo->currentData().toString();
    mCurrentRadius = mRadiusCombo->currentData().toInt();
    mCurrentScrollbar = mScrollbarCombo->currentData().toInt();
    if (mCustomQssEdit) { mCurrentQss = mCustomQssEdit->toPlainText(); }

    ThemeSupport::setThemeFromId(mCurrentThemeId);
    ThemeSupport::setAccentPresetId(mCurrentAccentId);
    ThemeSupport::setCustomAccentColor(mCurrentCustomAccent);
    ThemeSupport::setCustomBaseColor(mCurrentCustomBase);
    ThemeSupport::setCustomAlternateColor(mCurrentCustomAlt);
    ThemeSupport::setCustomDarkerColor(mCurrentCustomDarker);
    ThemeSupport::setBorderRadius(mCurrentRadius);
    ThemeSupport::setScrollbarWidth(mCurrentScrollbar);
    ThemeSupport::setCustomQss(mCurrentQss);
    ThemeSupport::saveThemeConfig();
}

void ThemeSettingsWidget::updateSettings(bool restore)
{
    ThemeSupport::loadThemeConfig();

    mCurrentThemeId = restore ? QStringLiteral("friction") : ThemeSupport::themeId();
    mCurrentAccentId = restore ? QStringLiteral("auto") : ThemeSupport::accentPresetId();
    mCurrentCustomAccent = restore ? QColor() : ThemeSupport::customAccentColor();
    mCurrentCustomBase = restore ? QColor() : ThemeSupport::customBaseColor();
    mCurrentCustomAlt = restore ? QColor() : ThemeSupport::customAlternateColor();
    mCurrentCustomDarker = restore ? QColor() : ThemeSupport::customDarkerColor();
    mCurrentRadius = restore ? 6 : ThemeSupport::borderRadius();
    mCurrentScrollbar = restore ? 6 : ThemeSupport::scrollbarWidth();
    mCurrentQss = restore ? QString() : ThemeSupport::customQss();

    if (mThemeCombo) {
        const int idx = mThemeCombo->findData(mCurrentThemeId);
        if (idx >= 0) { mThemeCombo->setCurrentIndex(idx); }
    }
    if (mAccentCombo) {
        const int idx = mAccentCombo->findData(mCurrentAccentId);
        if (idx >= 0) { mAccentCombo->setCurrentIndex(idx); }
    }
    if (mRadiusCombo) {
        const int idx = mRadiusCombo->findData(mCurrentRadius);
        if (idx >= 0) { mRadiusCombo->setCurrentIndex(idx); }
    }
    if (mScrollbarCombo) {
        const int idx = mScrollbarCombo->findData(mCurrentScrollbar);
        if (idx >= 0) { mScrollbarCombo->setCurrentIndex(idx); }
    }
    if (mCustomQssEdit) {
        mCustomQssEdit->setPlainText(mCurrentQss);
    }

    updateColorButtons();
}
