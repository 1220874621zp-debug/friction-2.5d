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
*/

#ifndef SCENENAVIGATORBAR_H
#define SCENENAVIGATORBAR_H

#include <QWidget>
#include <QMetaObject>

class Canvas;
class Document;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QMenu;

// AE-style scene navigator: breadcrumb of the drill-in path plus the
// nested (linked) scenes of the current scene laid out as directly
// clickable chips, instead of "open a dropdown to pick a scene".
// Every canvas pane owns one bar but they all render the same
// session-wide navigation path.
class SceneNavigatorBar : public QWidget {
    Q_OBJECT
public:
    SceneNavigatorBar(Document& doc, QWidget* const parent = nullptr);

signals:
    void sceneRequested(Canvas* const scene);

protected:
    void resizeEvent(QResizeEvent* const e) override;

private:
    // all scenes the given scene links to (recursively through
    // groups, deduplicated); link-to-link chains resolve to their
    // source scene
    static QList<Canvas*> linkedScenes(Canvas* const scene);

    void onActiveScene(Canvas* const scene);
    void onSceneRemoved(Canvas* const scene);
    // (re)connect child add/remove + rename notifications so chips
    // stay in sync with the scenes currently shown
    void rebind(Canvas* const scene);
    void rebuild();
    // hide leading breadcrumb entries into the "..." overflow button
    // when the row does not fit
    void updateOverflow();

    Document& mDocument;

    QHBoxLayout* mRowLayout = nullptr;
    QPushButton* mMoreBtn = nullptr;
    QMenu* mMoreMenu = nullptr;
    // breadcrumb buttons in path order; mCrumbSeps[i] is the
    // separator AFTER mCrumbBtns[i] (null for the last entry)
    QList<QPushButton*> mCrumbBtns;
    QList<QLabel*> mCrumbSeps;

    QMetaObject::Connection mInsConn;
    QMetaObject::Connection mRemConn;
    QList<QMetaObject::Connection> mNameConns;

    // session-wide drill-in path shared by every bar instance;
    // entries are pruned against Document::fScenes before use
    static QList<Canvas*> sPath;
};

#endif // SCENENAVIGATORBAR_H
