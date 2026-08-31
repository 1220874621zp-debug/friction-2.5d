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

// Moho-style switch panel: bound to a "switch group" (a ContainerBox
// flagged via 转换为切换组), shows a single preview of the child layer
// active at the current frame and a ruler slider with one tick per
// child. Dragging the ruler previews live; releasing commits a switch
// by writing visibility keys on the children ("Visible" rows).
//
// Listening is visibility-gated: the dock being closed disconnects all
// scene/group signals (zero cost), interacting with the ruler suspends
// external updates until release.

#ifndef SWITCHPANEL_H
#define SWITCHPANEL_H

#include <QWidget>
#include <QPointer>
#include <QImage>
#include <QHash>
#include <QStringList>

#include "smartPointers/ememory.h"

class QComboBox;
class QLabel;
class QCheckBox;
class QTimer;
class QMouseEvent;
class QWheelEvent;
class Canvas;
class ContainerBox;
class BoundingBox;
struct BoxRenderData;
class Document;

// single large preview of the active child: square, rounded corners,
// black frame, the layer name overlaid on a strip at the bottom
class SwitchPreview : public QWidget {
    Q_OBJECT
public:
    explicit SwitchPreview(QWidget* const parent = nullptr);

    void setImage(const QImage& img, const QString& name);
    void setPlaceholder(const QString& text);
protected:
    void paintEvent(QPaintEvent*) override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(const int w) const override { return w; }
private:
    QImage mImage;
    QString mName;
    QString mPlaceholder;
};

// discrete slider: a groove with one tick per child layer, the handle
// snaps tick to tick (never continuous); layer names are NOT drawn here
// (they live in the preview) but serve as the hover tooltip
class SwitchRuler : public QWidget {
    Q_OBJECT
public:
    explicit SwitchRuler(QWidget* const parent = nullptr);

    void setItems(const QStringList& names);
    void setActiveIndex(const int index);
    int activeIndex() const { return mActive; }
signals:
    void interactionStarted();
    void livePreview(const int index);
    void committed(const int index);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* const e) override;
    void mouseMoveEvent(QMouseEvent* const e) override;
    void mouseReleaseEvent(QMouseEvent* const e) override;
    void leaveEvent(QEvent* const e) override;
    void wheelEvent(QWheelEvent* const e) override;
private:
    int indexAt(const int x) const;
    qreal tickX(const int index) const;

    QStringList mNames;
    int mActive = -1;
    int mDrag = -1;   // >= 0 while the handle is held
    int mHover = -1;
};

class SwitchPanel : public QWidget {
    Q_OBJECT
public:
    SwitchPanel(Document& doc, QWidget* const parent = nullptr);

    // dock visibility gate: false = disconnect everything, true = bind
    // the active scene again
    void setListeningEnabled(const bool enabled);
private:
    void setCurrentScene(Canvas* const scene);
    void scheduleGroupRescan();
    void rescanGroups();
    void bindGroup(ContainerBox* const group);
    void clearBinding();
    void scheduleChildrenRefresh();
    void refreshChildren();
    // null-checked child access (entries die between rebuilds)
    BoundingBox* childAt(const int index) const;
    void updateActive();
    void scheduleSelectionCheck();
    void checkSelection();
    void commitSwitch(const int index);
    void requestPreview(BoundingBox* const box);
    void insertPreviewCache(const QString& key, const QImage& img);

    Document& mDocument;
    QPointer<Canvas> mScene;
    QPointer<ContainerBox> mGroup;
    // guarded pointers: a child can be deleted at any time between the
    // debounced rebuilds (undo, script, parallel session)
    QList<QPointer<BoundingBox>> mChildren;
    QList<QMetaObject::Connection> mGroupConns;
    // groups listed in the combo, parallel to its items
    QList<QPointer<ContainerBox>> mComboGroups;

    QComboBox* mGroupCombo = nullptr;
    SwitchPreview* mPreview = nullptr;
    SwitchRuler* mRuler = nullptr;
    QCheckBox* mAutoKey = nullptr;

    QTimer* mSelectionDebounce = nullptr;
    QTimer* mChildrenDebounce = nullptr;
    QTimer* mRescanDebounce = nullptr;

    bool mListening = false;
    bool mInteracting = false;
    bool mComboGuard = false;
    int mActiveIndex = -1;

    // preview: LRU cache keyed "ptr:frame" + one in-flight render task
    QHash<QString, QImage> mPreviewCache;
    QStringList mPreviewOrder;
    stdsptr<BoxRenderData> mPreviewTask;
    int mPreviewToken = 0;
};

#endif // SWITCHPANEL_H
