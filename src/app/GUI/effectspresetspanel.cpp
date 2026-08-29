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

#include "effectspresetspanel.h"
#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QShortcut>

#include "RasterEffects/rastereffectmenucreator.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "PathEffects/patheffectmenucreator.h"
#include "themesupport.h"

EffectsPresetsPanel::EffectsPresetsPanel(MainWindow * const mainWindow,
                                         QWidget * const parent) :
    QWidget(parent),
    mMainWindow(mainWindow)
{
    const auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    mSearchEdit = new QLineEdit(this);
    mSearchEdit->setPlaceholderText(tr("Search Effects & Presets..."));
    mSearchEdit->setClearButtonEnabled(true);
    connect(mSearchEdit, &QLineEdit::textChanged,
            this, &EffectsPresetsPanel::onSearchTextChanged);
    mainLayout->addWidget(mSearchEdit);

    mTreeWidget = new QTreeWidget(this);
    mTreeWidget->setHeaderHidden(true);
    mTreeWidget->setAnimated(true);
    mTreeWidget->setIndentation(16);
    mTreeWidget->setPalette(ThemeSupport::getDarkPalette());
    connect(mTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &EffectsPresetsPanel::onItemDoubleClicked);
    mainLayout->addWidget(mTreeWidget);

    const auto returnShortcut = new QShortcut(QKeySequence(Qt::Key_Return), mTreeWidget);
    connect(returnShortcut, &QShortcut::activated,
            this, &EffectsPresetsPanel::onApplyPressed);

    const auto enterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), mTreeWidget);
    connect(enterShortcut, &QShortcut::activated,
            this, &EffectsPresetsPanel::onApplyPressed);

    populateEffects();
}

void EffectsPresetsPanel::focusSearch()
{
    if (mSearchEdit) {
        mSearchEdit->setFocus();
        mSearchEdit->selectAll();
    }
}

void EffectsPresetsPanel::addEffectItem(const QString &categoryName,
                                        const QString &effectName,
                                        const QString &desc,
                                        const std::function<void()> &applyFunc)
{
    QTreeWidgetItem *catItem = mCategoryItems.value(categoryName, nullptr);
    if (!catItem) {
        catItem = new QTreeWidgetItem(mTreeWidget);
        catItem->setText(0, categoryName);
        catItem->setIcon(0, QIcon::fromTheme("file_folder"));
        QFont f = catItem->font(0);
        f.setBold(true);
        catItem->setFont(0, f);
        mCategoryItems[categoryName] = catItem;
    }

    const auto item = new QTreeWidgetItem(catItem);
    item->setText(0, effectName);
    item->setIcon(0, QIcon::fromTheme("effect"));
    if (!desc.isEmpty()) {
        item->setToolTip(0, desc);
    }
    mApplyCallbacks[item] = applyFunc;
}

void EffectsPresetsPanel::populateEffects()
{
    mTreeWidget->clear();
    mCategoryItems.clear();
    mApplyCallbacks.clear();

    // 1. Raster Effects
    RasterEffectMenuCreator::forEveryEffectCore(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            QString category = cat.isEmpty() ? tr("General") : cat;
            addEffectItem(category, name, QString(), [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffect(creator());
            });
        });

    RasterEffectMenuCreator::forEveryEffectCustom(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            QString category = cat.isEmpty() ? tr("Custom") : cat;
            addEffectItem(category, name, QString(), [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffect(creator());
            });
        });

    RasterEffectMenuCreator::forEveryEffectShader(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            QString category = cat.isEmpty() ? tr("Shader") : cat;
            addEffectItem(category, name, QString(), [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffect(creator());
            });
        });

    // 2. Path Effects
    PathEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const PathEffectMenuCreator::EffectCreator &creator) {
            addEffectItem(tr("Path Effects"), name, QString(), [this, creator]() {
                if (mMainWindow) mMainWindow->addPathEffect(creator());
            });
        });

    // 3. Blend Effects
    BlendEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const BlendEffectMenuCreator::EffectCreator &creator) {
            addEffectItem(tr("Blend Effects"), name, QString(), [this, creator]() {
                if (mMainWindow) mMainWindow->addBlendEffect(creator());
            });
        });

    // 4. Transform Effects
    TransformEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const TransformEffectMenuCreator::EffectCreator &creator) {
            addEffectItem(tr("Transform Effects"), name, QString(), [this, creator]() {
                if (mMainWindow) mMainWindow->addTransformEffect(creator());
            });
        });

    mTreeWidget->expandAll();
}

void EffectsPresetsPanel::onSearchTextChanged(const QString &text)
{
    const QString filter = text.trimmed().toLower();
    const bool isSearching = !filter.isEmpty();

    for (auto catIt = mCategoryItems.begin(); catIt != mCategoryItems.end(); ++catIt) {
        QTreeWidgetItem *cat = catIt.value();
        bool catMatch = cat->text(0).toLower().contains(filter);
        int visibleChildren = 0;

        for (int i = 0; i < cat->childCount(); ++i) {
            QTreeWidgetItem *child = cat->child(i);
            bool childMatch = child->text(0).toLower().contains(filter) || catMatch;
            child->setHidden(isSearching && !childMatch);
            if (!child->isHidden()) {
                visibleChildren++;
            }
        }

        cat->setHidden(isSearching && visibleChildren == 0);
        if (isSearching && visibleChildren > 0) {
            cat->setExpanded(true);
        }
    }
}

void EffectsPresetsPanel::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (!item) { return; }
    if (mApplyCallbacks.contains(item)) {
        mApplyCallbacks[item]();
    }
}

void EffectsPresetsPanel::onApplyPressed()
{
    const auto item = mTreeWidget->currentItem();
    if (item && mApplyCallbacks.contains(item)) {
        mApplyCallbacks[item]();
    }
}
