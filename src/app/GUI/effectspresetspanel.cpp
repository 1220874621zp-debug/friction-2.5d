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
#include <QPushButton>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QShortcut>
#include <QDrag>
#include <QMimeData>

#include "RasterEffects/rastereffectmenucreator.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "PathEffects/patheffectmenucreator.h"
#include "themesupport.h"
#include "appsupport.h"
#include "effectsloader.h"

namespace {
// drag payload = generation token; the closure itself lives in the
// panel's static registry (see beginEffectDrag/takeEffectDrag)
class EffectsTreeWidget : public QTreeWidget {
public:
    explicit EffectsTreeWidget(EffectsPresetsPanel* const panel,
                               QWidget* const parent = nullptr) :
        QTreeWidget(parent), mPanel(panel) {}

protected:
    void startDrag(Qt::DropActions) {
        const auto item = currentItem();
        if (!item || !mPanel) { return; }
        const auto apply = mPanel->effectCallback(item);
        if (!apply) { return; }
        auto mimeData = new QMimeData;
        mimeData->setData(EffectsPresetsPanel::sMimeFormat(),
                          EffectsPresetsPanel::beginEffectDrag(apply));
        QDrag drag(this);
        drag.setMimeData(mimeData);
        const auto pm = item->icon(0).pixmap(32, 32);
        if (!pm.isNull()) {
            drag.setPixmap(pm);
            drag.setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
        }
        drag.exec(Qt::CopyAction);
    }

private:
    EffectsPresetsPanel* mPanel = nullptr;
};
}

quint64 EffectsPresetsPanel::sDragGeneration = 0;
std::function<void()> EffectsPresetsPanel::sDragCallback;

const QString& EffectsPresetsPanel::sMimeFormat()
{
    static const QString format = QStringLiteral(
                "application/x-friction-effect");
    return format;
}

QByteArray EffectsPresetsPanel::beginEffectDrag(
        const std::function<void()> &apply)
{
    sDragCallback = apply;
    return QByteArray::number(++sDragGeneration);
}

std::function<void()> EffectsPresetsPanel::takeEffectDrag(
        const QByteArray &token)
{
    if (token != QByteArray::number(sDragGeneration)) { return nullptr; }
    auto apply = sDragCallback;
    sDragCallback = nullptr;
    return apply;
}

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

    mTreeWidget = new EffectsTreeWidget(this, this);
    mTreeWidget->setHeaderHidden(true);
    mTreeWidget->setAnimated(true);
    mTreeWidget->setIndentation(16);
    mTreeWidget->setPalette(ThemeSupport::getDefaultPalette());
    mTreeWidget->setFrameShape(QFrame::NoFrame);
    mTreeWidget->setDragEnabled(true);
    mTreeWidget->setDragDropMode(QAbstractItemView::DragOnly);
    mTreeWidget->setDefaultDropAction(Qt::CopyAction);
    connect(mTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &EffectsPresetsPanel::onItemDoubleClicked);
    mainLayout->addWidget(mTreeWidget);

    const auto returnShortcut = new QShortcut(QKeySequence(Qt::Key_Return), mTreeWidget);
    connect(returnShortcut, &QShortcut::activated,
            this, &EffectsPresetsPanel::onApplyPressed);

    const auto enterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), mTreeWidget);
    connect(enterShortcut, &QShortcut::activated,
            this, &EffectsPresetsPanel::onApplyPressed);

    // Bottom tool buttons: Import custom effect / Open folder / Refresh
    const auto btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(2);

    const auto importBtn = new QPushButton(QIcon::fromTheme("document-open"), tr("Import..."), this);
    importBtn->setToolTip(tr("Import custom GLSL shader effect (.frag / .json)"));
    importBtn->setFocusPolicy(Qt::NoFocus);
    connect(importBtn, &QPushButton::clicked, this, &EffectsPresetsPanel::onImportEffectClicked);

    const auto folderBtn = new QPushButton(QIcon::fromTheme("file_folder"), tr("Folder"), this);
    folderBtn->setToolTip(tr("Open custom effects & shaders directory"));
    folderBtn->setFocusPolicy(Qt::NoFocus);
    connect(folderBtn, &QPushButton::clicked, this, &EffectsPresetsPanel::onOpenFolderClicked);

    const auto refreshBtn = new QPushButton(QIcon::fromTheme("reload"), tr("Refresh"), this);
    refreshBtn->setToolTip(tr("Reload shader effects and presets"));
    refreshBtn->setFocusPolicy(Qt::NoFocus);
    connect(refreshBtn, &QPushButton::clicked, this, &EffectsPresetsPanel::onRefreshClicked);

    btnLayout->addWidget(importBtn);
    btnLayout->addWidget(folderBtn);
    btnLayout->addWidget(refreshBtn);
    mainLayout->addLayout(btnLayout);

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

void EffectsPresetsPanel::onImportEffectClicked()
{
    const QString targetDir = AppSupport::getAppShaderEffectsPath();
    QDir().mkpath(targetDir);

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Import Custom GLSL Shader Effect"),
        QDir::homePath(),
        tr("Shader Effects (*.frag *.json *.glsl);;All Files (*)")
    );

    if (files.isEmpty()) { return; }

    int importedCount = 0;
    for (const auto &filePath : files) {
        const QFileInfo fi(filePath);
        const QString destPath = targetDir + QDir::separator() + fi.fileName();
        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        if (QFile::copy(filePath, destPath)) {
            importedCount++;
        }
    }

    if (EffectsLoader::sInstance) {
        EffectsLoader::sInstance->iniShaderEffects();
    }
    populateEffects();

    QMessageBox::information(
        this,
        tr("Import Complete"),
        tr("Successfully imported %1 shader effect(s).\nSaved to: %2").arg(importedCount).arg(targetDir)
    );
}

void EffectsPresetsPanel::onOpenFolderClicked()
{
    const QString dirPath = AppSupport::getAppShaderEffectsPath();
    QDir().mkpath(dirPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void EffectsPresetsPanel::onRefreshClicked()
{
    if (EffectsLoader::sInstance) {
        EffectsLoader::sInstance->iniShaderEffects();
    }
    populateEffects();
}
