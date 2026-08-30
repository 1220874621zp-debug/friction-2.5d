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

// AE-like project panel: lists every scene in the project.
// Dragging a scene onto the active canvas creates an
// InternalLinkCanvas (scene link) at the drop position.
class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    ProjectPanel(Document& doc, QWidget* const parent = nullptr);

    // mime format used while dragging a scene out of the panel
    static const QString& sMimeFormat();

protected:
    void showEvent(QShowEvent* const e);

private:
    void rebuild();
    void updateActiveMark();
    void switchToScene(Canvas* const scene);
    void linkToActiveScene(Canvas* const scene);
    Canvas* sceneAt(QTreeWidgetItem* const item) const;
    Canvas* activeScene() const;
    QString sceneInfo(const Canvas* const scene) const;
    void showContextMenu(const QPoint& pos);

    Document& mDocument;
    QTreeWidget* mTree = nullptr;
    QList<QMetaObject::Connection> mNameConns;
};

#endif // PROJECTPANEL_H
