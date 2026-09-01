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
#include <functional>
#include <QTreeWidget>
#include <QRubberBand>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <QMenu>
#include <QMimeData>
#include <QDrag>
#include <QUrl>
#include <QFileInfo>
#include <QDesktopServices>
#include <QSettings>
#include <QInputDialog>
#include <QLineEdit>

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
// the item data role carrying the folder id (folder rows only)
const int kFolderIdRole = Qt::UserRole + 3;

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

    // internal drop handling (drag rows onto folder rows); wired to
    // ProjectPanel::handleTreeDrop
    std::function<void(const QPoint&)> dropHandler;

    // live drop-target highlight: the folder row under the cursor
    // shows a translucent background while an internal drag hovers it
    QTreeWidgetItem* mDropHover = nullptr;
    QBrush mDropHoverBrush0;
    QBrush mDropHoverBrush1;

    QTreeWidgetItem* folderUnder(const QPoint& pos) const {
        auto item = itemAt(pos);
        if (!item) { return nullptr; }
        if (item->data(0, kFolderIdRole).isValid()) { return item; }
        if (item->parent() &&
            item->parent()->data(0, kFolderIdRole).isValid()) {
            return item->parent();
        }
        return nullptr;
    }
    void setDropHover(QTreeWidgetItem* const item) {
        if (mDropHover == item) { return; }
        if (mDropHover) {
            mDropHover->setBackground(0, mDropHoverBrush0);
            mDropHover->setBackground(1, mDropHoverBrush1);
        }
        mDropHover = item;
        if (item) {
            mDropHoverBrush0 = item->background(0);
            mDropHoverBrush1 = item->background(1);
            const QColor hl(70, 135, 220, 95);
            item->setBackground(0, hl);
            item->setBackground(1, hl);
        }
    }

    // QTreeWidget has no rubber-band selection of its own: pressing
    // the empty area below/between rows starts one
    QRubberBand* mRubber = nullptr;
    QPoint mRubberOrigin;
    bool mRubberActive = false;

    void updateRubberSelection() {
        const QRect rubber = mRubber->geometry().adjusted(-3, -3, 3, 3);
        QTreeWidgetItemIterator it(this);
        while (*it) {
            (*it)->setSelected(rubber.intersects(visualItemRect(*it)));
            ++it;
        }
    }

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

    // accept only internal drags (folder drops); external payloads
    // keep the default (ignored by this panel)
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->source() == this) { e->acceptProposedAction(); }
        else { QTreeWidget::dragEnterEvent(e); }
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        if (e->source() == this) {
            e->acceptProposedAction();
            setDropHover(folderUnder(e->pos()));
            return;
        }
        QTreeWidget::dragMoveEvent(e);
    }
    void dragLeaveEvent(QDragLeaveEvent* e) override {
        setDropHover(nullptr);
        QTreeWidget::dragLeaveEvent(e);
    }
    void dropEvent(QDropEvent* e) override {
        setDropHover(nullptr);
        if (e->source() == this) {
            e->acceptProposedAction();
            if (dropHandler) { dropHandler(e->pos()); }
            return;
        }
        QTreeWidget::dropEvent(e);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && !itemAt(e->pos())) {
            mRubberActive = true;
            mRubberOrigin = e->pos();
            if (!mRubber) {
                mRubber = new QRubberBand(QRubberBand::Rectangle, viewport());
            }
            mRubber->setGeometry(QRect(mRubberOrigin, QSize()));
            mRubber->show();
            return;
        }
        QTreeWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (mRubberActive) {
            mRubber->setGeometry(
                        QRect(mRubberOrigin, e->pos()).normalized());
            updateRubberSelection();
            return;
        }
        QTreeWidget::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (mRubberActive && e->button() == Qt::LeftButton) {
            mRubberActive = false;
            mRubber->hide();
            updateRubberSelection();
            return;
        }
        QTreeWidget::mouseReleaseEvent(e);
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
    mTree->setHeaderLabels({tr("场景"), tr("信息")});
    mTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    mTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    mTree->header()->resizeSection(0, 130);
    mTree->setRootIsDecorated(false);
    mTree->setUniformRowHeights(true);
    mTree->setAllColumnsShowFocus(true);
    mTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mTree->setDragEnabled(true);
    mTree->setDragDropMode(QAbstractItemView::DragDrop);
    mTree->setDefaultDropAction(Qt::CopyAction);
    static_cast<SceneTreeWidget*>(mTree)->dropHandler =
            [this](const QPoint& pos) { handleTreeDrop(pos); };
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
    // folders expand/collapse on a single click (frequent organizing
    // should not need a double click); the default double-click
    // expansion is off so the two never fight
    mTree->setExpandsOnDoubleClick(false);
    connect(mTree, &QTreeWidget::itemClicked,
            this, [this](QTreeWidgetItem* item, int) {
        if (item && item->data(0, kFolderIdRole).isValid()) {
            item->setExpanded(!item->isExpanded());
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
    if (!info.exists()) { return tr("文件丢失"); }
    return QStringLiteral("%1 | %2")
            .arg(info.suffix().toUpper())
            .arg(humanSize(info.size()));
}

void ProjectPanel::addFileItem(FileCacheHandler* const handler)
{
    if (!handler || fileItemExists(handler)) { return; }
    QTreeWidgetItem* item = nullptr;
    if (const auto parent = parentItemForFile(handler)) {
        item = new QTreeWidgetItem(parent);
    } else {
        // keep loose files above the folder rows
        int at = mTree->topLevelItemCount();
        for (int i = 0; i < mTree->topLevelItemCount(); i++) {
            if (folderItemAt(mTree->topLevelItem(i))) { at = i; break; }
        }
        item = new QTreeWidgetItem;
        mTree->insertTopLevelItem(at, item);
    }
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
    QTreeWidgetItemIterator it(mTree);
    while (*it) {
        if (reinterpret_cast<FileCacheHandler*>(
                    (*it)->data(0, kFilePtrRole).toULongLong()) == handler) {
            delete *it;
            return;
        }
        ++it;
    }
}

bool ProjectPanel::fileItemExists(FileCacheHandler* const handler) const
{
    QTreeWidgetItemIterator it(mTree);
    while (*it) {
        if (reinterpret_cast<FileCacheHandler*>(
                    (*it)->data(0, kFilePtrRole).toULongLong()) == handler) {
            return true;
        }
        ++it;
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

    readFolderState();

    mTree->clear();
    // folder rows first so member items can find their parents; loose
    // rows insert before them, keeping folders visually last
    for (const auto& folder : mFolders) {
        addFolderWidget(folder);
    }
    int looseAt = mFolders.count();
    const auto icon = QIcon::fromTheme("sequence");
    for (const auto& scenePtr : mDocument.fScenes) {
        const auto scene = scenePtr.get();
        if (!scene) { continue; }
        const auto parent = parentItemForScene(scene);
        QTreeWidgetItem* item = nullptr;
        if (parent) { item = new QTreeWidgetItem(parent); }
        else {
            item = new QTreeWidgetItem;
            mTree->insertTopLevelItem(looseAt++, item);
        }
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
            QTreeWidgetItemIterator it(mTree);
            while (*it) {
                if (sceneAt(*it) == scene) {
                    (*it)->setText(0, scene->prp_getName());
                    (*it)->setToolTip(0, scene->prp_getName());
                }
                ++it;
            }
        });
        // keep the info column live: fps / dimensions edited in the
        // scene settings must show up without a rebuild
        const auto updateInfo = [this, scene]() {
            QTreeWidgetItemIterator it(mTree);
            while (*it) {
                if (sceneAt(*it) == scene) {
                    const QString info = sceneInfo(scene);
                    (*it)->setText(1, info);
                    (*it)->setToolTip(1, info);
                }
                ++it;
            }
        };
        mNameConns << connect(scene, &Canvas::fpsChanged,
                               this, [updateInfo](qreal) { updateInfo(); });
        mNameConns << connect(scene, &Canvas::dimensionsChanged,
                               this, [updateInfo](int, int) { updateInfo(); });
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
    QTreeWidgetItemIterator it(mTree);
    while (*it) {
        auto font = (*it)->font(0);
        font.setBold(sceneAt(*it) == active && active);
        (*it)->setFont(0, font);
        ++it;
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
    const auto handler = (!scene && item && !folderItemAt(item)) ?
                fileAt(item) : nullptr;
    const auto folderItem = folderItemAt(item);
    const FolderInfo* memberFolder = scene ? folderOfScene(scene) :
            handler ? folderOfFile(handler) : nullptr;

    // multi-select aware: the move actions apply to every selected
    // scene/file (or to the single right-clicked row when alone)
    QList<Canvas*> selScenes;
    QList<FileCacheHandler*> selFiles;
    if (scene || handler) {
        for (const auto sel : mTree->selectedItems()) {
            const auto s = sceneAt(sel);
            if (s) { selScenes << s; continue; }
            const auto h = fileAt(sel);
            if (h) { selFiles << h; }
        }
        if (selScenes.isEmpty() && selFiles.isEmpty()) {
            if (scene) { selScenes << scene; }
            if (handler) { selFiles << handler; }
        }
    }

    QMenu menu(this);
    menu.addAction(QIcon::fromTheme("file_new"),
                   tr("新建场景"),
                   this, [this]() {
        SceneSettingsDialog::sNewSceneDialog(mDocument, this);
    });
    menu.addAction(QIcon::fromTheme("file_folder"),
                   tr("新建文件夹"),
                   this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
                    this, tr("新建文件夹"), tr("名称："),
                    QLineEdit::Normal, tr("文件夹"), &ok);
        if (ok && !name.simplified().isEmpty()) {
            createFolder(name.simplified());
            rebuild();
            writeFolderState();
        }
    });

    if (folderItem) {
        menu.addSeparator();
        menu.addAction(tr("重命名文件夹"),
                       this, [this, folderItem]() {
            const int id = folderItem->data(0, kFolderIdRole).toInt();
            bool ok = false;
            const QString name = QInputDialog::getText(
                        this, tr("重命名文件夹"), tr("名称："),
                        QLineEdit::Normal, folderItem->text(0), &ok);
            if (!ok || name.simplified().isEmpty()) { return; }
            if (auto* info = folderById(id)) {
                info->name = name.simplified();
                writeFolderState();
                rebuild();
            }
        });
        menu.addAction(tr("删除文件夹"),
                       this, [this, folderItem]() {
            const int id = folderItem->data(0, kFolderIdRole).toInt();
            for (int i = 0; i < mFolders.count(); i++) {
                if (mFolders.at(i).id == id) {
                    mFolders.removeAt(i);
                    break;
                }
            }
            writeFolderState();
            rebuild();
        });
    } else if (scene || handler) {
        menu.addSeparator();
        auto moveMenu = menu.addMenu(tr("移动到文件夹"));
        for (const auto& folder : mFolders) {
            const int folderId = folder.id;
            moveMenu->addAction(folder.name, this,
                    [this, folderId, selScenes, selFiles]() {
                FolderInfo* info = folderById(folderId);
                if (!info) { return; }
                for (const auto s : selScenes) {
                    // remove from any other folder first
                    for (auto& f : mFolders) { f.scenes.removeAll(s); }
                    info->scenes << s;
                }
                for (const auto h : selFiles) {
                    for (auto& f : mFolders) { f.files.removeAll(h); }
                    info->files << h;
                }
                writeFolderState();
                rebuild();
            });
        }
        if (memberFolder) {
            const int memberId = memberFolder->id;
            moveMenu->addAction(tr("（移出文件夹）"), this,
                    [this, memberId, selScenes, selFiles]() {
                if (auto* info = folderById(memberId)) {
                    for (const auto s : selScenes) { info->scenes.removeAll(s); }
                    for (const auto h : selFiles) { info->files.removeAll(h); }
                    writeFolderState();
                    rebuild();
                }
            });
        }
        if (mFolders.isEmpty()) {
            moveMenu->setEnabled(false);
            moveMenu->setToolTip(tr("先在空白处右键新建文件夹"));
        }
    }

    if (scene) {
        const auto active = activeScene();
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("sequence"),
                       tr("打开场景"),
                       this, [this, scene]() {
            switchToScene(scene);
        })->setEnabled(scene != active);
        menu.addAction(QIcon::fromTheme("linked"),
                       tr("链接到当前画布"),
                       this, [this, scene]() {
            linkToActiveScene(scene);
        })->setEnabled(scene != active);
        menu.addAction(QIcon::fromTheme("sequence"),
                       tr("场景属性..."),
                       this, [scene]() {
            const auto& dialogs = DialogsInterface::instance();
            dialogs.showSceneSettingsDialog(scene);
        });
    } else if (handler) {
        const QString path = handler->path();
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(fileIconName(path)),
                       tr("打开"),
                       this, [path]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        menu.addAction(QIcon::fromTheme("file_folder"),
                       tr("在文件夹中显示"),
                       this, [path]() {
            QDesktopServices::openUrl(
                        QUrl::fromLocalFile(
                            QFileInfo(path).absolutePath()));
        });
    }

    menu.exec(mTree->viewport()->mapToGlobal(pos));
}

// internal drag onto a folder row: move every selected scene/file
// into that folder (drop on a folder child targets its parent folder)
void ProjectPanel::handleTreeDrop(const QPoint& pos)
{
    const auto targetItem = mTree->itemAt(pos);
    if (!targetItem) { return; }
    QTreeWidgetItem* folderItem = folderItemAt(targetItem);
    if (!folderItem && targetItem->parent()) {
        folderItem = folderItemAt(targetItem->parent());
    }
    if (!folderItem) { return; }
    FolderInfo* info = folderById(
                folderItem->data(0, kFolderIdRole).toInt());
    if (!info) { return; }
    bool moved = false;
    for (const auto item : mTree->selectedItems()) {
        const auto scene = sceneAt(item);
        const auto handler = scene ? nullptr : fileAt(item);
        if (scene) {
            for (auto& f : mFolders) { f.scenes.removeAll(scene); }
            info->scenes << scene;
            moved = true;
        } else if (handler) {
            for (auto& f : mFolders) { f.files.removeAll(handler); }
            info->files << handler;
            moved = true;
        }
    }
    if (moved) {
        writeFolderState();
        rebuild();
    }
}

// ---- folder management ----

QTreeWidgetItem* ProjectPanel::folderItemAt(QTreeWidgetItem* const item) const
{
    if (!item) { return nullptr; }
    return item->data(0, kFolderIdRole).isValid() ? item : nullptr;
}

ProjectPanel::FolderInfo* ProjectPanel::folderById(const int id)
{
    for (auto& folder : mFolders) {
        if (folder.id == id) { return &folder; }
    }
    return nullptr;
}

ProjectPanel::FolderInfo* ProjectPanel::folderOfScene(Canvas* const scene)
{
    for (auto& folder : mFolders) {
        if (folder.scenes.contains(scene)) { return &folder; }
    }
    return nullptr;
}

ProjectPanel::FolderInfo* ProjectPanel::folderOfFile(FileCacheHandler* const handler)
{
    for (auto& folder : mFolders) {
        if (folder.files.contains(handler)) { return &folder; }
    }
    return nullptr;
}

QTreeWidgetItem* ProjectPanel::folderWidget(const int id) const
{
    QTreeWidgetItemIterator it(mTree);
    while (*it) {
        if ((*it)->data(0, kFolderIdRole).toInt() == id &&
            (*it)->data(0, kFolderIdRole).isValid()) { return *it; }
        ++it;
    }
    return nullptr;
}

ProjectPanel::FolderInfo* ProjectPanel::createFolder(const QString& name)
{
    FolderInfo info;
    info.id = mNextFolderId++;
    info.name = name;
    mFolders << info;
    return folderById(info.id);
}

void ProjectPanel::addFolderWidget(const FolderInfo& info)
{
    const auto item = new QTreeWidgetItem(mTree);
    item->setText(0, info.name);
    item->setIcon(0, QIcon::fromTheme("file_folder"));
    item->setText(1, tr("%1 项").arg(info.scenes.count() + info.files.count()));
    item->setData(0, kFolderIdRole, info.id);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                   Qt::ItemIsDropEnabled);
}

QTreeWidgetItem* ProjectPanel::parentItemForScene(Canvas* const scene) const
{
    for (const auto& folder : mFolders) {
        if (folder.scenes.contains(scene)) {
            return folderWidget(folder.id);
        }
    }
    return nullptr;
}

QTreeWidgetItem* ProjectPanel::parentItemForFile(FileCacheHandler* const handler) const
{
    for (const auto& folder : mFolders) {
        if (folder.files.contains(handler)) {
            return folderWidget(folder.id);
        }
    }
    return nullptr;
}

void ProjectPanel::writeFolderState() const
{
    // folder layout persists per project file (scenes by name, files
    // by path); untitled projects stay session-only
    if (mDocument.fEvFile.isEmpty()) { return; }
    QStringList lines;
    for (const auto& folder : mFolders) {
        QStringList sceneNames;
        for (const auto scene : folder.scenes) {
            if (scene) { sceneNames << scene->prp_getName(); }
        }
        QStringList filePaths;
        for (const auto handler : folder.files) {
            if (handler) { filePaths << handler->path(); }
        }
        lines << QStringLiteral("%1|%2|%3")
                .arg(folder.name,
                     sceneNames.join(QLatin1Char('\x1')),
                     filePaths.join(QLatin1Char('\x1')));
    }
    QSettings settings;
    settings.beginGroup("projectPanelFolders");
    settings.setValue(mDocument.fEvFile, lines.join(QLatin1Char('\n')));
    settings.endGroup();
}

void ProjectPanel::readFolderState()
{
    if (mDocument.fEvFile.isEmpty()) { return; }
    if (mLoadedForPath == mDocument.fEvFile) { return; }
    mLoadedForPath = mDocument.fEvFile;
    mFolders.clear();
    mNextFolderId = 1;

    QSettings settings;
    settings.beginGroup("projectPanelFolders");
    const QString all = settings.value(
                mDocument.fEvFile).toString();
    settings.endGroup();
    const auto lines = all.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        const auto parts = line.split(QLatin1Char('|'));
        if (parts.count() != 3) { continue; }
        FolderInfo info;
        info.id = mNextFolderId++;
        info.name = parts.at(0);
        for (const auto& name : parts.at(1).split(QLatin1Char('\x1'),
                                                  Qt::SkipEmptyParts)) {
            for (const auto& scenePtr : mDocument.fScenes) {
                if (scenePtr && scenePtr->prp_getName() == name) {
                    info.scenes << scenePtr.get();
                    break;
                }
            }
        }
        if (FilesHandler::sInstance) {
            for (const auto& path : parts.at(2).split(QLatin1Char('\x1'),
                                                      Qt::SkipEmptyParts)) {
                for (const auto& fh : FilesHandler::sInstance->fileHandlers()) {
                    if (fh && fh->path() == path) {
                        info.files << fh.get();
                        break;
                    }
                }
            }
        }
        mFolders << info;
    }
}
