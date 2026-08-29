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

#ifndef BOXESLISTANIMATIONDOCKWIDGET_H
#define BOXESLISTANIMATIONDOCKWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QApplication>
#include <QScrollBar>
#include <QComboBox>
#include <QMenuBar>
#include <QLineEdit>
#include <QWidgetAction>
#include <QToolBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QProgressBar>
#include <QTimer>

#include "smartPointers/ememory.h"
#include "framerange.h"
#include "timelinebasewrappernode.h"
#include "widgets/qdoubleslider.h"
#include "renderhandler.h"
#include "widgets/framespinbox.h"

class FrameScrollBar;
class TimelineWidget;
class MainWindow;
class AnimationDockWidget;
class RenderWidget;
class ActionButton;
class Canvas;
class Document;
class LayoutHandler;
class BrushContexedWrapper;

enum class CanvasMode : short;

class TimelineDockWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineDockWidget(Document &document,
                                LayoutHandler* const layoutH,
                                MainWindow * const parent);

    // A sane fixed size hint keeps the dock at a reasonable initial
    // height; the content-driven hint would otherwise claim most of
    // the window. This does not limit manual resizing (only the
    // minimum size hint does).
    QSize sizeHint() const override { return QSize(600, 300); }

    bool processKeyPress(QKeyEvent *event);
    void previewFinished();
    void previewBeingPlayed();
    void previewBeingRendered();
    void previewPaused();
    void stepPreview();

    bool setPreviewFromStart(PreviewState state);
    bool setNextKeyframe();
    bool setPrevKeyframe();

    void updateSettingsForCurrentCanvas(Canvas * const canvas);

    void stopPreview();

    void setIn();
    void setOut();
    void setMarker();
    void splitClip();

private:
    void setLoop(const bool loop);
    // quick PNG export of the current canvas frame
    void snapshotCurrentFrame();
    void interruptPreview();
    void jumpToIntermediateFrame(bool forward);

    // AE-like property reveal shortcuts: expand the matching
    // property row of every selected layer in the timeline tree
    void showTransformProperty(const int which); // 0 pivot 1 pos 2 scale 3 rot 4 opacity
    void showAnimatedProperties();               // U key behavior
    void setupPropertyShortcuts();

    bool playPreview();
    void renderPreview();
    void pausePreview();
    void resumePreview();
    void setStepPreviewStop(const bool pause = false);
    void setStepPreviewStart();
    void gotoFrame(int frame);

    void updateButtonsVisibility(const CanvasMode mode);

    void updateFrameRange(const FrameRange &range);
    void handleCurrentFrameChanged(int frame);

    void showRenderStatus(bool show);

    void addSpacer();
    void addBlankAction();

    Document& mDocument;
    MainWindow* const mMainWindow;
    QStackedWidget* const mTimelineLayout;

    QToolBar *mToolBar;

    QVBoxLayout *mMainLayout;

    QAction *mPlayFromBeginningButton;
    QAction *mPlayButton;
    QAction *mStopButton;
    QAction *mLoopButton;
    QAction *mSnapshotButton = nullptr;
    QAction *mSafeFramesButton = nullptr;
    QAction *mTransparencyGridButton = nullptr;

    FrameSpinBox *mFrameStartSpin;
    FrameSpinBox *mFrameEndSpin;

    QAction *mFrameRewindAct;
    QAction *mFrameFastForwardAct;
    QAction *mSetInPointAct;
    QAction *mSetOutPointAct;
    QAction *mSplitClipAct;
    QAction *mCurrentFrameSpinAct;
    FrameSpinBox *mCurrentFrameSpin;

    QAction *mRenderProgressAct;
    QProgressBar *mRenderProgress;

    QTimer *mStepPreviewTimer;

    QList<TimelineWidget*> mTimelineWidgets;
    //AnimationDockWidget *mAnimationDockWidget;

    QPair<bool,int> mPausedPreviewState;
};

#endif // BOXESLISTANIMATIONDOCKWIDGET_H
