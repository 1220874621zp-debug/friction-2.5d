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

// NLE-style edit timeline panel (CapCut-like): a standalone editing
// surface where scenes are clips stitched on tracks. Pure UI layer -
// the "edit composition" is an ordinary scene; each clip is an
// InternalLinkCanvas inside it, so playback, export, undo and project
// persistence all run on the existing engine. All mutations go through
// public eBoxOrSound/DurationRectangle/ContainerBox APIs (drag = move
// duration rect, edges = trim, vertical drag = z reorder via moveTo,
// double-click = switchToScene of the linked source).
//
// Listening is visibility-gated like SwitchPanel: a closed dock drops
// every scene/document connection and the thumbnail pipeline.

#ifndef EDITTIMELINEPANEL_H
#define EDITTIMELINEPANEL_H

#include <QWidget>
#include <QPointer>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QStringList>

class QComboBox;
class QPushButton;
class QLabel;
class QTimer;
class QScrollBar;
class QPainter;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QResizeEvent;
class Canvas;
class eBoxOrSound;
class BoundingBox;
class Document;
struct BoxRenderData;

// the custom-painted NLE surface: frame ruler with timecode + playhead,
// one row per top-level box of the target scene (row 0 = topmost z),
// rounded clips with a name strip and a filmstrip of source frames.
// Manual scrollbars: the horizontal one works in frame units (its value
// is the leftmost viewed frame), the vertical one in row units.
class EditTimelineView : public QWidget {
    Q_OBJECT
public:
    explicit EditTimelineView(class EditTimelinePanel* const panel);

    void sceneChanged();          // target scene / clip list rebuilt
    void updateScrollRanges();
    void scheduleThumbRequest();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* const e) override;
    void mouseMoveEvent(QMouseEvent* const e) override;
    void mouseReleaseEvent(QMouseEvent* const e) override;
    void mouseDoubleClickEvent(QMouseEvent* const e) override;
    void wheelEvent(QWheelEvent* const e) override;
    void resizeEvent(QResizeEvent* const e) override;
    void leaveEvent(QEvent* const e) override;
    void keyPressEvent(QKeyEvent* const e) override;
private:
    enum class Drag { None, Pending, Playhead, Pan,
                      Move, TrimMin, TrimMax, RowSwitch };
    enum class Zone { None, Body, EdgeMin, EdgeMax };
    struct Hit { eBoxOrSound* box = nullptr; Zone zone = Zone::None; };

    Canvas* scene() const;
    int firstViewedFrame() const;
    qreal xAtFrame(const int frame) const;
    int frameAtX(const int x) const;
    int firstVisibleRow() const;
    int rowOfClip(eBoxOrSound* const clip) const;
    QRect clipRect(eBoxOrSound* const clip) const;
    Hit hitTest(const QPoint& pos) const;
    void drawRuler(QPainter* const p);
    void drawPlayhead(QPainter* const p);
    void drawClip(QPainter* const p, eBoxOrSound* const clip,
                  const int row);
    void beginHorizontal(const QPoint& pos);
    void beginRowSwitch();
    void applyMoveDelta(const int total);
    void applyMinDelta(const int total);
    void applyMaxDelta(const int total);
    int snappedMoveDelta(const int total) const;
    int snappedEdgeDelta(const int total, const bool minEdge) const;
    QList<int> snapCandidates() const;
    void setFrameFromX(const int x);
    void updateHoverCursor(const QPoint& pos);

    EditTimelinePanel* const mPanel;
    QScrollBar* mHBar = nullptr;
    QScrollBar* mVBar = nullptr;
    qreal mPpf = 8.0;          // pixels per frame (zoom)

    Drag mDrag = Drag::None;
    Zone mZone = Zone::None;
    QPointer<eBoxOrSound> mPressBox;
    QPoint mPressPos;
    QPoint mLastPanPos;
    int mPressFrame = 0;       // frame under the press x
    int mLastApplied = 0;      // frames already fed to moveXxx()
    int mOrigMin = 0;
    int mOrigMax = 0;
    int mStartRow = 0;
    int mTargetRow = -1;
};

class EditTimelinePanel : public QWidget {
    Q_OBJECT
public:
    EditTimelinePanel(Document& doc, QWidget* const parent = nullptr);

    // dock visibility gate: false = disconnect everything, true = bind
    // scenes again (target defaults to the active scene)
    void setListeningEnabled(const bool enabled);
private:
    friend class EditTimelineView;

    void setTargetScene(Canvas* const scene);
    void rebuildSceneCombo();
    void scheduleClipsRefresh();
    void refreshClips();
    void clearSourceConns();
    void createCompositionScene();
    void showAddClipMenu();
    void addSceneAsClip(Canvas* const source);
    // deferred canvas sync: pressing the view schedules a switch to the
    // target scene (cancelled when the press turns out to be a
    // double-click navigation, so no flicker of two switches)
    void scheduleAutoSwitch();
    void cancelAutoSwitch();
    void requestSwitchScene(Canvas* const scene);
    void updateReturnButton();
    void updateTimeLabel();
    Canvas* resolveSourceScene(eBoxOrSound* const box) const;

    // filmstrip thumbnails: LRU cache keyed "ptr:frame", FIFO queue,
    // at most kThumbInFlightMax renders in flight (SwitchPanel style)
    QString thumbKey(Canvas* const scene, const int frame) const;
    void enqueueThumb(Canvas* const scene, const int frame);
    void pumpThumbQueue();
    void thumbArrived(const QString& key, const QImage& img);
    void thumbFailed();
    void clearThumbCache();
    void clearSceneThumbs(Canvas* const scene);
    void markSceneThumbsDirty(Canvas* const scene);

    Document& mDocument;
    QPointer<Canvas> mTargetScene;
    QPointer<Canvas> mActiveScene;
    QList<QPointer<eBoxOrSound>> mClips;
    QList<QMetaObject::Connection> mDocConns;
    QList<QMetaObject::Connection> mSceneConns;
    QHash<Canvas*, QList<QMetaObject::Connection>> mSourceConns;
    QSet<Canvas*> mDirtySources;

    QComboBox* mSceneCombo = nullptr;
    QPushButton* mNewSceneBtn = nullptr;
    QPushButton* mAddClipBtn = nullptr;
    QPushButton* mReturnBtn = nullptr;
    QLabel* mTimeLabel = nullptr;
    EditTimelineView* mView = nullptr;
    QTimer* mRefreshDebounce = nullptr;
    QTimer* mThumbDirtyDebounce = nullptr;
    QTimer* mAutoSwitchTimer = nullptr;

    bool mListening = false;
    bool mComboGuard = false;
    bool mThumbSuppress = false; // true while a drag edits geometry

    struct ThumbReq { QPointer<Canvas> scene; int frame; };
    QHash<QString, QImage> mThumbCache;
    QStringList mThumbOrder;
    QSet<QString> mQueuedKeys;
    QList<ThumbReq> mThumbQueue;
    int mThumbInFlight = 0;
    int mThumbToken = 0;
};

#endif // EDITTIMELINEPANEL_H
