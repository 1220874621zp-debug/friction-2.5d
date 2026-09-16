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

// NLE-style edit timeline panel, visually modeled 1:1 on the reference
// screenshot (dark #262626 canvas, square-cornered clips, 22px teal
// name strip, filmstrip thumbnails, teal waveform strip attached under
// video clips, blue audio blocks, 2px light playhead).
//
// This file is UI-ONLY by design: the panel owns a plain data model
// (EditTimelineData) and talks to the engine exclusively through the
// EditTimelineApi interface below. EditTimelineApiStub feeds fake data
// so the whole look & feel can be validated before the real adapter
// (InternalLinkCanvas / DurationRectangle / queExternalRender) is
// plugged in - swapping the stub is the only change needed then.

#ifndef EDITTIMELINEPANEL_H
#define EDITTIMELINEPANEL_H

#include <QWidget>
#include <QPointer>
#include <QImage>
#include <QHash>
#include <QString>
#include <QList>
#include <QVector>
#include <functional>
#include <memory>

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
class EditTimelineView;
class EditTimelinePanel;

// ------------------------------ data model ------------------------------
// Pure presentation data. No engine types here on purpose: the future
// engine adapter converts between these and scene/link objects.

struct EditClip {
    enum class Kind { Scene, Audio };
    Kind kind = Kind::Scene;
    QString name;
    int startFrame = 0;       // position on the edit timeline
    int durationFrames = 96;
    int sourceInFrame = 0;    // source in-point (thumbnail mapping)
    bool selected = false;
};

struct EditTrack {
    enum class Kind { Video, Audio };
    Kind kind = Kind::Video;
    QString name;
    QList<EditClip> clips;    // kept sorted by startFrame
};

struct EditTimelineData {
    int fps = 25;
    int playheadFrame = 0;
    QList<EditTrack> tracks;  // visual order: index 0 = topmost
};

// ------------------------------ API layer ------------------------------
// The single seam between this UI and the engine. UI drags mutate the
// local data live and commit once on release through these calls; the
// engine implementation is authoritative and may push back a reload.
// All callbacks must be invoked on the UI thread.

class EditTimelineApi {
public:
    virtual ~EditTimelineApi() = default;

    virtual void load(EditTimelineData& out) = 0;
    virtual void compositionNames(QStringList& out) = 0;
    virtual void sceneNames(QStringList& out) = 0;

    virtual void moveClip(const int trackIdx, const int clipIdx,
                          const int newStart) = 0;
    virtual void trimClip(const int trackIdx, const int clipIdx,
                          const int newStart, const int newDuration) = 0;
    virtual void moveClipToTrack(const int fromTrack, const int clipIdx,
                                 const int toTrack) = 0;
    virtual void setPlayhead(const int frame) = 0;
    virtual void openClip(const int trackIdx, const int clipIdx) = 0;
    virtual void addSceneClip(const int sceneIndex) = 0;
    virtual void createComposition(int& newCompositionIndex) = 0;

    // thumbnails/waveform fill asynchronously (or synchronously) into
    // the caches keyed by the UI; key is opaque to the provider
    virtual void requestThumb(const QString& key, const int seed,
                              const QSize& size,
                              std::function<void(const QString&,
                                                 const QImage&)>) = 0;
    virtual void requestWave(const QString& key, const int sampleCount,
                             std::function<void(const QString&,
                                                const QVector<qreal>&)>) = 0;
};

// Fake implementation: three tracks, a few clips, generated thumbnails
// (teal gradients with a frame number) and pseudo waveforms. Replace
// with the engine adapter when wiring the real data.
class EditTimelineApiStub : public EditTimelineApi {
public:
    void load(EditTimelineData& out) override;
    void compositionNames(QStringList& out) override;
    void sceneNames(QStringList& out) override;

    void moveClip(const int trackIdx, const int clipIdx,
                  const int newStart) override;
    void trimClip(const int trackIdx, const int clipIdx,
                  const int newStart, const int newDuration) override;
    void moveClipToTrack(const int fromTrack, const int clipIdx,
                         const int toTrack) override;
    void setPlayhead(const int frame) override;
    void openClip(const int trackIdx, const int clipIdx) override;
    void addSceneClip(const int sceneIndex) override;
    void createComposition(int& newCompositionIndex) override;

    void requestThumb(const QString& key, const int seed,
                      const QSize& size,
                      std::function<void(const QString&,
                                         const QImage&)>) override;
    void requestWave(const QString& key, const int sampleCount,
                     std::function<void(const QString&,
                                        const QVector<qreal>&)>) override;

private:
    EditTimelineData mData;
    QStringList mScenes;
    int mCompSeq = 0;
};

// ------------------------------ view ------------------------------
// The custom-painted NLE surface: timecode ruler on top (with the
// triangular playhead handle), then the tracks. Manual scrollbars:
// horizontal one works in frame units, vertical one in pixel units.

class EditTimelineView : public QWidget {
    Q_OBJECT
public:
    explicit EditTimelineView(EditTimelinePanel* const panel);

    void dataChanged();
    void updateScrollRanges();
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
    struct Hit { int track = -1; int clip = -1; Zone zone = Zone::None; };

    EditTimelinePanel* const mPanel;
    QScrollBar* mHBar = nullptr;
    QScrollBar* mVBar = nullptr;
    qreal mPpf = 8.0;          // pixels per frame (zoom)

    Drag mDrag = Drag::None;
    Zone mZone = Zone::None;
    Hit mPressHit;
    QPoint mPressPos;
    QPoint mLastPanPos;
    int mPressFrame = 0;
    int mLastApplied = 0;
    EditClip mDragBackup;      // pre-drag state for Esc/rollback
    int mDragBackupTrack = -1;
    int mGhostTrack = -1;      // RowSwitch target

    int firstViewedFrame() const;
    qreal xAtFrame(const int frame) const;
    int frameAtX(const int x) const;
    int trackTop(const int trackIdx) const;
    int trackHeight(const int trackIdx) const;
    QRect clipRect(const int trackIdx, const int clipIdx) const;
    Hit hitTest(const QPoint& pos) const;
    int clipAtFrame(int trackIdx, int frame) const;

    void drawRuler(QPainter* const p);
    void drawPlayhead(QPainter* const p);
    void drawTrackBackground(QPainter* const p, const int trackIdx);
    void drawClip(QPainter* const p, const int trackIdx, const int clipIdx,
                  const QRect& rc);
    void drawWaveform(QPainter* const p, const QRect& rc,
                      const QVector<qreal>& samples, const QColor& color);
    void requestClipAssets(const int trackIdx, const int clipIdx,
                           const QRect& rc);

    void beginDragOp(const QPoint& pos);
    void beginRowSwitch();
    void applyMoveDelta(const int total);
    void applyTrimDelta(const int total, const bool minEdge);
    int snapDelta(const int unsnapped, const bool draggingEdge) const;
    void commitDrag();
    void rollbackDrag();
    void setFrameFromX(const int x);
    void updateHoverCursor(const QPoint& pos);
};

// ------------------------------ panel ------------------------------

class EditTimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit EditTimelinePanel(QWidget* const parent = nullptr);

    // dock visibility gate: false = stop asset requests/timers
    void setListeningEnabled(const bool enabled);

    // replace the stub with the engine adapter (kept for the wiring)
    void setApi(EditTimelineApi* const api);

private:
    friend class EditTimelineView;

    void reload();
    void rebuildCompositionCombo();
    void updateTopBar();
    void updateTimeLabel();

    std::unique_ptr<EditTimelineApi> mApi;
    EditTimelineData mData;

    QComboBox* mSceneCombo = nullptr;
    QPushButton* mNewSceneBtn = nullptr;
    QPushButton* mAddClipBtn = nullptr;
    QLabel* mTimeLabel = nullptr;
    EditTimelineView* mView = nullptr;

    bool mListening = false;
    bool mComboGuard = false;

    // asset caches, filled through the API (stub generates them)
    QHash<QString, QImage> mThumbCache;
    QHash<QString, QVector<qreal>> mWaveCache;
};

#endif // EDITTIMELINEPANEL_H
