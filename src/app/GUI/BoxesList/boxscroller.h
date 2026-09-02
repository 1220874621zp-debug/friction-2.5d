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

#ifndef BOXSCROLLWIDGETVISIBLEPART_H
#define BOXSCROLLWIDGETVISIBLEPART_H

#include <QWidget>
#include "optimalscrollarena/scrollwidgetvisiblepart.h"
#include "singlewidgettarget.h"
#include "framerange.h"

class BoxSingleWidget;
class TimelineMovable;
class Key;
class KeysView;
class Canvas;
class TimelineHighlightWidget;
class eBoxOrSound;

class BoxScroller : public ScrollWidgetVisiblePart {
public:
    explicit BoxScroller(ScrollWidget * const parent);

    QWidget *createNewSingleWidget();

    void updateDropTarget();

    void stopScrolling();
    void scrollUp();
    void scrollDown();

    KeysView *getKeysView() const
    { return mKeysView; }

    Canvas* currentScene() const
    { return mCurrentScene; }

    void setCurrentScene(Canvas* const scene)
    { mCurrentScene = scene; }

    void setKeysView(KeysView *keysView)
    { mKeysView = keysView; }

    TimelineHighlightWidget* requestHighlighter();

    // parent-link drag visual state, shared with BoxSingleWidget:
    // while a link drag is alive BoxScroller paints an AE-style
    // connector line from the source row's link button to the cursor
    static void plDragStarted(const QPoint& srcGlobalCenter);
    static void plDragEnded();
    static bool plDragActive() { return sPlActive; }
protected:
    void paintEvent(QPaintEvent *);
    void resizeEvent(QResizeEvent *e);
    void dropEvent(QDropEvent *event);
    void dragLeaveEvent(QDragLeaveEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    // rubber-band multi-row selection (AE-like layer marquee): row
    // presses keep flowing natively so clicks/shift-clicks/double-clicks
    // are untouched; only moves past the drag threshold turn into a band
    void mousePressEvent(QMouseEvent *e);
    void mouseMoveEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *e);
    // Escape cancels a pending/active rubber band
    void keyPressEvent(QKeyEvent *e);
    bool eventFilter(QObject *obj, QEvent *event);
private:
    enum class DropType {
        none, on, into
    };

    struct DropTarget {
        SWT_Abstraction * fTargetParent = nullptr;
        int fTargetId = 0;
        DropType fDropType = DropType::none;

        bool isValid() const {
            return fTargetParent && fDropType != DropType::none;
        }

        void reset() {
            fTargetParent = nullptr;
            fDropType = DropType::none;
        }
    };

    DropTarget getClosestDropTarget(const int yPos);

    bool tryDropIntoAbs(SWT_Abstraction * const abs,
                        const int idInAbs, DropTarget &dropTarget);

    // can the currently dragged rows merge onto 'target'? plain rows
    // (not groups) whose track kind matches the dragged batch
    bool dragCombineTarget(const SingleWidgetTarget *target) const;
    // Alt held during the drag: releasing detaches members from their
    // track instead of combining/gathering
    bool altDetachActive() const
    { return mDragModifiers & Qt::AltModifier; }

    TimelineHighlightWidget* mHighlighter = nullptr;
    Canvas* mCurrentScene = nullptr;

    QRect mCurrentDragRect;
    int mLastDragMoveY;

    QTimer *mScrollTimer = nullptr;
    KeysView *mKeysView = nullptr;

    const QMimeData* mCurrentMimeData = nullptr;
    Qt::KeyboardModifiers mDragModifiers = Qt::NoModifier;
    // true while the hovered drop target means "combine into track"
    // (drawn as a filled row) vs a plain reorder insert line
    bool mDropIsCombine = false;

    DropTarget mDropTarget{nullptr, 0, DropType::none};

    // rubber band state (see the mouse overrides above)
    bool mRubberPotential = false; // plain left press seen, threshold unmet
    bool mRubberStarted = false;   // band visible, moves are ours
    QPoint mRubberStart;
    QRect mRubberRect;
    void rubberUpdateRect(const QPoint& pos);
    void rubberFinish(const QPoint& pos);
    void rubberReset();

    // parent-link drag visual state
    static bool  sPlActive;
    static QPoint sPlSrcGlobal;
};

#endif // BOXSCROLLWIDGETVISIBLEPART_H
