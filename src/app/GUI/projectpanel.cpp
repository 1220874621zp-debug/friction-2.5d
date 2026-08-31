/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors.
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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "projectpanel.h"

#include <QVBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>
#include <QMenu>
#include <QMimeData>
#include <QDrag>
#include <QUrl>
#include <QFileInfo>
#include <QDesktopServices>

#include "Private/document.h"
#include "canvas.h"
#include "fileshandler.h"
#include "FileCacheHandlers/filecachehandler.h"
#include "GUI/mainwindow.h"
#include "GUI/layouthandler.h"
#include "GUI/dialogsinterface.h"
#include "dialogs/scenesettingsdialog.h"
#include "themesupport.h"

namespace {
// the item data role carrying the raw Canvas pointer
const int kScenePtrRole = Qt::UserRole + 1;
// the item data role carrying the raw FileCacheHandler pointer
const int kFilePtrRole = Qt::UserRole + 2;

QString fileIconName(const QString& path)
{
    static const QStringList imageExt = {"png", "jpg", "jpeg", "bmp",
                                         "gif", "webp", "tif", "tiff",
                                         "kra", "psd", "ora"};
    static const QStringList videoExt = {"mp4", "mov", "avi", "mkv",
                                         "webm", "gifv"};
    static const QStringList audioExt = {"mp3", "wav", "ogg", "flac",
                                         "aac", "m4a", "aiff"};
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (imageExt.contains(suffix)) { return "file_image"; }
    if (videoExt.contains(suffix)) { return "file_movie"; }
    if (audioExt.contains(suffix)) { return "file_sound"; }
    return "file_blank";
}

QString humanSize(const qint64 bytes)
{
    if (bytes < 1024) { return QString("%1 B").arg(bytes); }
    if (bytes < 1024*1024) { return QString("%1 KB").arg(bytes/1024); }
    return QString("%1 MB").arg(bytes/(1024*1024));
}

class SceneTreeWidget : public QTreeWidget {
public:
    explicit SceneTreeWidget(QWidget* const parent = nullptr) :
        QTreeWidget(parent) {}

protected:
    // custom drag payload: scenes drag as their raw pointer in our
    // private mime format, file assets drag as their file url (the
    // canvas already accepts url drops as imports)
    void startDrag(Qt::DropActions) {
        const auto item = currentItem();
        if (!item) { return; }
        const auto scene = reinterpret_cast<Canvas*>(
                    item->data(0, kScenePtrRole).toULongLong());
        auto mimeData = new QMimeData;
        if (scene) {
            mimeData->setData(ProjectPanel::sMimeFormat(),
                              QByteArray::number(
                                  reinterpret_cast<qulonglong>(scene)));
        } else {
            const auto handler = reinterpret_cast<FileCacheHandler*>(
                        item->data(0, kFilePtrRole).toULongLong());
            if (!handler) { delete mimeData; return; }
            mimeData->setUrls({QUrl::fromLocalFile(handler->path())});
        }
        QDrag drag(this);
        drag.setMimeData(mimeData);
        const auto pm = item->icon(0).pixmap(32, 32);
        if (!pm.isNull()) {
            drag.setPixmap(pm);
            drag.setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
        }
        drag.exec(Qt::CopyAction);
    }
};
}

const QString& ProjectPanel::sMimeFormat()
{
    static const QString format = QStringLiteral(
                "application/x-friction-scene");
    return format;
}

ProjectPanel::ProjectPanel(Document& doc, QWidget* const parent) :
    QWidget(parent), mDocument(doc)
{
    setPalette(ThemeSupport::getDefaultPalette());
    setAutoFillBackground(true);

    mTree = new SceneTreeWidget(this);
    mTree->setPalette(ThemeSupport::getDefaultPalette());
    mTree->setAutoFillBackground(true);
    if (mTree->viewport()) {
        mTree->viewport()->setPalette(ThemeSupport::getDefaultPalette());
        mTree->viewport()->setAutoFillBackground(true);
    }
    mTree->setColumnCount(2);
    mTree->setHeaderLabels({tr("Scene"), tr("Info")});
    mTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    mTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    mTree->header()->resizeSection(0, 130);
    mTree->setRootIsDecorated(false);
    mTree->setUniformRowHeights(true);
    mTree->setAllColumnsShowFocus(true);
    mTree->setSelectionMode(QAbstractItemView::SingleSelection);
    mTree->setDragEnabled(true);
    mTree->setDragDropMode(QAbstractItemView::DragOnly);
    mTree->setDefaultDropAction(Qt::CopyAction);
    mTree->setContextMenuPolicy(Qt::CustomContextMenu);
    mTree->setFrameShape(QFrame::NoFrame);

    const auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(mTree);

    connect(mTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
        if (const auto scene = sceneAt(item)) {
            switchToScene(scene);
        } else if (const auto handler = fileAt(item)) {
            // preview with the system default viewer
            QDesktopServices::openUrl(
                        QUrl::fromLocalFile(handler->path()));
        }
    });
    connect(mTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        // the signal delivers viewport coordinates already
        showContextMenu(pos);
    });

    connect(&mDocument, qOverload<Canvas*>(&Document::sceneCreated),
            this, [this](Canvas*) { rebuild(); });
    connect(&mDocument, qOverload<Canvas*>(&Document::sceneRemoved),
            this, [this](Canvas*) { rebuild(); });
    connect(&mDocument, &Document::activeSceneSet,
            this, [this](Canvas*) { updateActiveMark(); });

    // imported file assets live in the same tree (AE-style project
    // panel: compositions and footage together)
    if (FilesHandler::sInstance) {
        connect(FilesHandler::sInstance,
                &FilesHandler::addedCacheHandler,
                this, [this](FileCacheHandler* const handler) {
            addFileItem(handler);
        });
        connect(FilesHandler::sInstance,
                &FilesHandler::removedCacheHandler,
                this, [this](FileCacheHandler* const handler) {
            removeFileItem(handler);
        });
    }

    rebuild();
}

void ProjectPanel::showEvent(QShowEvent* const e)
{
    QWidget::showEvent(e);
    // cheap safety net: catch scene lists changed without a signal
    if (mTree->topLevelItemCount() != mDocument.fScenes.count()) {
        rebuild();
    }
}

Canvas* ProjectPanel::sceneAt(QTreeWidgetItem* const item) const
{
    if (!item) { return nullptr; }
    const auto raw = reinterpret_cast<Canvas*>(
                item->data(0, kScenePtrRole).toULongLong());
    // guard against a stale pointer (scene deleted after build)
    for (const auto& scene : mDocument.fScenes) {
        if (scene.get() == raw) { return raw; }
    }
    return nullptr;
}

Canvas* ProjectPanel::activeScene() const
{
    return *mDocument.fActiveScene;
}

FileCacheHandler* ProjectPanel::fileAt(QTreeWidgetItem* const item) const
{
    if (!item) { return nullptr; }
    const auto raw = reinterpret_cast<FileCacheHandler*>(
                item->data(0, kFilePtrRole).toULongLong());
    // guard against a stale pointer (handler deleted after build)
    if (FilesHandler::sInstance) {
        for (const auto& fh : FilesHandler::sInstance->fileHandlers()) {
            if (fh.get() == raw) { return raw; }
        }
    }
    return nullptr;
}

QString ProjectPanel::fileInfo(const FileCacheHandler* const handler) const
{
    const QFileInfo info(handler->path());
    if (!info.exists()) { return tr("Missing"); }
    return QStringLiteral("%1 | %2")
            .arg(info.suffix().toUpper())
            .arg(humanSize(info.size()));
}

void ProjectPanel::addFileItem(FileCacheHandler* const handler)
{
    if (!handler || fileItemExists(handler)) { return; }
    const auto item = new QTreeWidgetItem(mTree);
    const QString path = handler->path();
    const QFileInfo info(path);
    item->setText(0, info.fileName());
    item->setIcon(0, QIcon::fromTheme(fileIconName(path)));
    item->setText(1, fileInfo(handler));
    item->setToolTip(0, path);
    item->setToolTip(1, path);
    item->setData(0, kFilePtrRole,
                  reinterpret_cast<qulonglong>(handler));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                   Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren);
}

void ProjectPanel::removeFileItem(FileCacheHandler* const handler)
{
    for (int i = 0; i < mTree->topLevelItemCount(); i++) {
        const auto item = mTree->topLevelItem(i);
        if (reinterpret_cast<FileCacheHandler*>(
                    item->data(0, kFilePtrRole).toULongLong()) == handler) {
            delete item;
            return;
        }
    }
}

bool ProjectPanel::fileItemExists(FileCacheHandler* const handler) const
{
    for (int i = 0; i < mTree->topLevelItemCount(); i++) {
        const auto item = mTree->topLevelItem(i);
        if (reinterpret_cast<FileCacheHandler*>(
                    item->data(0, kFilePtrRole).toULongLong()) == handler) {
            return true;
        }
    }
    return false;
}

QString ProjectPanel::sceneInfo(const Canvas* const scene) const
{
    const auto range = scene->getFrameRange();
    return QStringLiteral("%1 x %2 | %3fps | %4-%5")
            .arg(scene->getCanvasWidth())
            .arg(scene->getCanvasHeight())
            .arg(scene->getFps())
            .arg(range.fMin)
            .arg(range.fMax);
}

void ProjectPanel::rebuild()
{
    for (const auto& conn : mNameConns) { disconnect(conn); }
    mNameConns.clear();

    mTree->clear();
    const auto icon = QIcon::fromTheme("sequence");
    for (const auto& scenePtr : mDocument.fScenes) {
        const auto scene = scenePtr.get();
        if (!scene) { continue; }
        const auto item = new QTreeWidgetItem(mTree);
        item->setText(0, scene->prp_getName());
        item->setIcon(0, icon);
        item->setText(1, sceneInfo(scene));
        item->setToolTip(0, scene->prp_getName());
        item->setToolTip(1, sceneInfo(scene));
        item->setData(0, kScenePtrRole,
                      reinterpret_cast<qulonglong>(scene));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                       Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren);
        // rename in place: a full rebuild inside the nameChanged
        // signal would disconnect/reconnect its own sender
        mNameConns << connect(scene, &Canvas::prp_nameChanged,
                this, [this, scene](const QString&) {
            for (int i = 0; i < mTree->topLevelItemCount(); i++) {
                const auto item = mTree->topLevelItem(i);
                if (sceneAt(item) != scene) { continue; }
                item->setText(0, scene->prp_getName());
                item->setToolTip(0, scene->prp_getName());
                break;
            }
        });
    }
    // imported file assets below the scenes (AE-style)
    if (FilesHandler::sInstance) {
        for (const auto& fh : FilesHandler::sInstance->fileHandlers()) {
            if (fh) { addFileItem(fh.get()); }
        }
    }
    updateActiveMark();
}

void ProjectPanel::updateActiveMark()
{
    const auto active = activeScene();
    for (int i = 0; i < mTree->topLevelItemCount(); i++) {
        const auto item = mTree->topLevelItem(i);
        auto font = item->font(0);
        font.setBold(sceneAt(item) == active && active);
        item->setFont(0, font);
    }
}

void ProjectPanel::switchToScene(Canvas* const scene)
{
    if (!scene || scene == activeScene()) { return; }
    const auto mwd = MainWindow::sGetInstance();
    if (!mwd) { return; }
    const auto lay = mwd->getLayoutHandler();
    if (!lay) { return; }
    lay->switchToScene(scene);
}

void ProjectPanel::linkToActiveScene(Canvas* const scene)
{
    const auto active = activeScene();
    if (!scene || !active || scene == active) { return; }
    // mirrors the canvas right-click "Link Scene" action
    const auto newLink = scene->createLink(false);
    active->getCurrentGroup()->addContained(newLink);
    newLink->centerPivotPosition();
    Document::sInstance->actionFinished();
}

void ProjectPanel::showContextMenu(const QPoint& pos)
{
    const auto item = mTree->itemAt(pos);
    const auto scene = sceneAt(item);
    const auto handler = scene ? nullptr : fileAt(item);

    QMenu menu(this);
    menu.addAction(QIcon::fromTheme("file_new"),
                   tr("New Scene"),
                   this, [this]() {
        SceneSettingsDialog::sNewSceneDialog(mDocument, this);
    });

    if (scene) {
        const auto active = activeScene();
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("sequence"),
                       tr("Open Scene"),
                       this, [this, scene]() {
            switchToScene(scene);
        })->setEnabled(scene != active);
        menu.addAction(QIcon::fromTheme("linked"),
                       tr("Link to Active Canvas"),
                       this, [this, scene]() {
            linkToActiveScene(scene);
        })->setEnabled(scene != active);
        menu.addAction(QIcon::fromTheme("sequence"),
                       tr("Scene Properties..."),
                       this, [scene]() {
            const auto& dialogs = DialogsInterface::instance();
            dialogs.showSceneSettingsDialog(scene);
        });
    } else if (handler) {
        const QString path = handler->path();
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(fileIconName(path)),
                       tr("Open"),
                       this, [path]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        menu.addAction(QIcon::fromTheme("file_folder"),
                       tr("Reveal in Folder"),
                       this, [path]() {
            QDesktopServices::openUrl(
                        QUrl::fromLocalFile(
                            QFileInfo(path).absolutePath()));
        });
    }

    menu.exec(mTree->viewport()->mapToGlobal(pos));
}
