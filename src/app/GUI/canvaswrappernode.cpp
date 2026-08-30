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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "canvaswrappernode.h"
#include "widgets/scenechooser.h"
#include "scenenavigatorbar.h"
#include "mainwindow.h"
#include "Private/document.h"

class CanvasWrapperMenuBar : public StackWrapperMenu {
public:
    CanvasWrapperMenuBar(Document& document, CanvasWindow * const window) :
        mDocument(document), mWindow(window) {
        mSceneMenu = new SceneChooser(mDocument, false, this);
        addMenu(mSceneMenu);
        connect(mSceneMenu, &SceneChooser::currentChanged,
                this, &CanvasWrapperMenuBar::requestSceneSwitch);
        connect(window, &CanvasWindow::currentSceneChanged,
                mSceneMenu, qOverload<Canvas*>(&SceneChooser::setCurrentScene));

        // AE-style breadcrumb + nested-scene chips next to the
        // dropdown; nested (linked) scenes are one click away
        mNavigator = new SceneNavigatorBar(mDocument, this);
        addWidget(mNavigator);
        connect(mNavigator, &SceneNavigatorBar::sceneRequested,
                this, &CanvasWrapperMenuBar::requestSceneSwitch);
    }

    // initial binding only (ctor / project load)
    void setCurrentScene(Canvas * const scene) {
        mWindow->setCurrentCanvas(scene);
        mSceneMenu->setCurrentScene(scene);
    }

    Canvas* getCurrentScene() const { return mWindow->getCurrentCanvas(); }
private:
    // scene switching from the dropdown or the navigator now does a
    // FULL page switch (canvas + timeline + workspace combo stay in
    // sync); the old setCurrentCanvas call only repainted this
    // window and left the timeline on the previous scene
    void requestSceneSwitch(Canvas * const scene) {
        if (!scene) { return; }
        const auto mwd = MainWindow::sGetInstance();
        if (!mwd) { return; }
        const auto lay = mwd->getLayoutHandler();
        if (!lay) { return; }
        // during project load panes are created while not being the
        // current page - keep the old local behavior then
        const int curId = lay->getSceneId(mWindow->getCurrentCanvas());
        if (lay->isCurrentScene(curId)) {
            lay->switchToScene(scene);
            // this pane's page is no longer shown; restore its
            // dropdown to its own scene for when the user returns
            // (the emit re-enters requestSceneSwitch but the fallback
            // no-ops on the unchanged canvas)
            mSceneMenu->setCurrentScene(mWindow->getCurrentCanvas());
        }
        else { mWindow->setCurrentCanvas(scene); }
    }

    Document& mDocument;
    CanvasWindow* const mWindow;
    SceneChooser * mSceneMenu;
    SceneNavigatorBar * mNavigator = nullptr;
    Canvas * mCurrentScene = nullptr;
    std::map<Canvas*, QAction*> mSceneToAct;
};

CanvasWrapperNode::CanvasWrapperNode(Canvas* const scene) :
    WidgetWrapperNode([](Canvas* const scene) {
        return new CanvasWrapperNode(scene);
    }) {
    mCanvasWindow = new CanvasWindow(*Document::sInstance, this);
    mMenu = new CanvasWrapperMenuBar(*Document::sInstance, mCanvasWindow);
    setMenuBar(mMenu);
    setCentralWidget(mCanvasWindow);
    mMenu->setCurrentScene(scene);
}

void CanvasWrapperNode::readData(eReadStream &src) {
    mCanvasWindow->readState(src);
    mMenu->setCurrentScene(mCanvasWindow->getCurrentCanvas());
}

void CanvasWrapperNode::writeData(eWriteStream &dst) {
    mCanvasWindow->writeState(dst);
}

void CanvasWrapperNode::readDataXEV(XevReadBoxesHandler& boxReadHandler,
                                    const QDomElement& ele,
                                    RuntimeIdToWriteId& objListIdConv) {
    Q_UNUSED(objListIdConv)
    mCanvasWindow->readStateXEV(boxReadHandler, ele);
    mMenu->setCurrentScene(mCanvasWindow->getCurrentCanvas());
}

void CanvasWrapperNode::writeDataXEV(QDomElement& ele, QDomDocument& doc,
                                     RuntimeIdToWriteId& objListIdConv) {
    Q_UNUSED(objListIdConv)
    mCanvasWindow->writeStateXEV(ele, doc);
}
