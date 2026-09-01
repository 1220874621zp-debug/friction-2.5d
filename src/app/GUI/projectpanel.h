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

#ifndef PROJECTPANEL_H
#define PROJECTPANEL_H

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class Canvas;
class Document;
class FileCacheHandler;

// AE-like project panel: a single tree managing both the scenes
// (compositions) and the imported file assets of the project.
// Dragging a scene onto the active canvas creates an
// InternalLinkCanvas (scene link), dragging a file asset out
// imports/places it. User folders group scenes and file assets
// (context menu "move to folder"); the folder layout persists per
// project file via settings (scenes matched by name, files by path).
class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    ProjectPanel(Document& doc, QWidget* const parent = nullptr);

    // mime format used while dragging a scene out of the panel
    static const QString& sMimeFormat();

protected:
    void showEvent(QShowEvent* const e);

private:
    struct FolderInfo {
        int id = 0;
        QString name;
        QList<Canvas*> scenes;
        QList<FileCacheHandler*> files;
    };

    void rebuild();
    void updateActiveMark();
    void switchToScene(Canvas* const scene);
    void linkToActiveScene(Canvas* const scene);
    Canvas* sceneAt(QTreeWidgetItem* const item) const;
    FileCacheHandler* fileAt(QTreeWidgetItem* const item) const;
    Canvas* activeScene() const;
    QString sceneInfo(const Canvas* const scene) const;
    QString fileInfo(const FileCacheHandler* const handler) const;
    void addFileItem(FileCacheHandler* const handler);
    void removeFileItem(FileCacheHandler* const handler);
    bool fileItemExists(FileCacheHandler* const handler) const;
    void showContextMenu(const QPoint& pos);

    // folder management
    void handleTreeDrop(const QPoint& pos);
    QTreeWidgetItem* folderItemAt(QTreeWidgetItem* const item) const;
    FolderInfo* folderOfScene(Canvas* const scene);
    FolderInfo* folderOfFile(FileCacheHandler* const handler);
    FolderInfo* folderById(const int id);
    QTreeWidgetItem* folderWidget(const int id) const;
    FolderInfo* createFolder(const QString& name);
    void addFolderWidget(const FolderInfo& info);
    QTreeWidgetItem* parentItemForScene(Canvas* const scene) const;
    QTreeWidgetItem* parentItemForFile(FileCacheHandler* const handler) const;
    void writeFolderState() const;
    void readFolderState();

    Document& mDocument;
    QTreeWidget* mTree = nullptr;
    QList<QMetaObject::Connection> mNameConns;
    QList<FolderInfo> mFolders;
    int mNextFolderId = 1;
    QString mLoadedForPath;
};

#endif // PROJECTPANEL_H
