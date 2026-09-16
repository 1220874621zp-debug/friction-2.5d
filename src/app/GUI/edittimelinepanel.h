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

// Edit timeline panel - UI/interaction copied verbatim from
// ceshi/TimelineDemo (user-approved). Pure UI, no core deps;
// engine wiring plugs into the widget later.

#ifndef EDITTIMELINEPANEL_H
#define EDITTIMELINEPANEL_H

#include <QWidget>
#include <QVector>
#include <QPixmap>
#include <QHash>
#include <QElapsedTimer>

class QScrollBar;

// Pure-UI editing timeline: ruler, tracks, draggable clips with
// thumbnails (video) / waveforms (audio), snapping, zoom, playhead.
// No media backend - thumbnails are procedurally generated previews.
class EditTimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EditTimelineWidget(QWidget *parent = nullptr);

    void setScrollBar(QScrollBar *bar);
    double playheadTime() const { return m_playhead; }

public slots:
    void addVideoClip();
    void addAudioClip();
    void removeSelectedClip();
    void zoomIn();
    void zoomOut();
    void zoomFit();

signals:
    void logMessage(const QString &msg);
    void selectionChanged(const QString &info);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class ClipType { Video, Audio };
    struct Clip {
        int id = 0;
        QString name;
        ClipType type = ClipType::Video;
        int track = 0;       // index into m_tracks
        double start = 0.0;  // seconds
        double length = 4.0; // seconds
        int hueSeed = 0;     // thumbnail variation seed
    };
    struct Track {
        QString name;
        int height = 64;
        ClipType type = ClipType::Video;
    };

    // ---- layout / mapping ----
    int rulerHeight() const { return 30; }
    int headerWidth() const { return 132; }
    int trackY(int track) const;           // top y of track content
    int trackAtY(int y) const;             // -1 if none
    double xToTime(int x) const;           // content area x -> seconds
    int timeToX(double t) const;           // seconds -> content area x
    double contentDuration() const;        // right edge of content (seconds)
    QRectF clipRect(const Clip &c) const;

    // ---- painting ----
    void drawRuler(QPainter &p);
    void drawTrackHeaders(QPainter &p);
    void drawTrackBodies(QPainter &p);
    void drawClip(QPainter &p, int index, bool ghost = false);
    void drawPlayhead(QPainter &p);
    QPixmap thumbnailTile(const Clip &c, int h);
    QString timecode(double t) const;

    // ---- interaction ----
    enum class DragMode { None, MoveClip, TrimLeft, TrimRight, Playhead };
    int clipAt(const QPoint &pos, QRectF *rectOut = nullptr) const; // index or -1
    double snapTime(double t, int ignoreClipIdx, bool *snappedOut) const;
    void applyZoom(double factor, int anchorX);
    void clampView();
    void updateScrollBar();
    void emitLog(const QString &msg);
    bool overlapsOnTrack(int track, double start, double len, int ignoreIdx) const;

    QVector<Track> m_tracks;
    QVector<Clip>  m_clips;
    int m_nextId = 1;

    double m_pxPerSec = 60.0;
    double m_scrollSec = 0.0;   // left edge in seconds
    double m_playhead = 2.0;

    int m_selected = -1;        // clip index
    int m_hover = -1;

    DragMode m_drag = DragMode::None;
    int m_dragClip = -1;
    double m_grabOffsetSec = 0.0; // move: cursor time - clip start
    double m_origStart = 0.0;
    double m_origLength = 0.0;
    int m_origTrack = 0;
    double m_snapTarget = -1.0;   // for drawing snap guide, -1 = none
    QPoint m_pressPos;

    QScrollBar *m_scrollBar = nullptr;
    QHash<QString, QPixmap> m_thumbCache;

    // theme
    QColor cBg       {0x1b,0x1b,0x1b};
    QColor cBgAlt    {0x22,0x22,0x22};
    QColor cRuler    {0x1d,0x1d,0x1d};
    QColor cHeader   {0x24,0x24,0x26};
    QColor cGridLine {0x2c,0x2c,0x2c};
    QColor cText     {0xc8,0xc8,0xc8};
    QColor cTextDim  {0x77,0x77,0x77};
    QColor cAccent   {0x08,0xa5,0x81}; // pencil-dream green
    QColor cPlayhead {0xe8,0x4c,0x4c};
    QColor cVideoBar {0x0e,0x7d,0x6c}; // clip name bar (teal)
    QColor cAudioBody{0x1d,0x33,0x52}; // audio body (dark blue)
    QColor cAudioWave{0x4f,0x8f,0xd6};
};

// dock content hosting the timeline: toolbar, status label and the
// debug log dialog, ported 1:1 from the demo's MainWindow
class QLabel;
class QDialog;
class QPlainTextEdit;
class EditTimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit EditTimelinePanel(QWidget *parent = nullptr);

    // dock visibility gate (kept for the mainwindow connection; the
    // pure-UI widget has nothing to stop while hidden)
    void setListeningEnabled(const bool enabled);

private slots:
    void showDebugLog();

private:
    EditTimelineWidget *m_timeline;
    QPlainTextEdit *m_logView;   // owned by log dialog
    QDialog *m_logDlg = nullptr;
    QLabel *m_selLabel;
};

#endif // EDITTIMELINEPANEL_H
