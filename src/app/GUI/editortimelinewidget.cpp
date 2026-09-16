#include "editortimelinewidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QtMath>
#include <QRandomGenerator>
#include <QDateTime>

static const double MIN_CLIP_LEN = 0.2;   // seconds
static const int SNAP_PX = 8;
static const int TRIM_PX = 6;

EditorTimelineWidget::EditorTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(320);

    // tracks: two video + one audio (like the reference screenshot)
    m_tracks = {
        {QStringLiteral("V2"), 60, ClipType::Video},
        {QStringLiteral("V1"), 60, ClipType::Video},
        {QStringLiteral("A1"), 52, ClipType::Audio},
    };

    // seed some demo clips
    {
        struct S { const char *n; ClipType t; int tr; double s; double l; };
        const S demo[] = {
            {"jimeng-2026-08-23-6464", ClipType::Video, 0, 0.0, 5.0},
            {"shot_002_take1",         ClipType::Video, 0, 5.0, 3.2},
            {"shot_003_closeup",       ClipType::Video, 0, 8.2, 6.5},
            {"b-roll street",          ClipType::Video, 1, 2.0, 4.0},
            {"title card",             ClipType::Video, 1, 10.0, 3.0},
            {"8.21 voiceover.mp3",     ClipType::Audio, 2, 0.0, 8.0},
            {"bgm_lofi.mp3",           ClipType::Audio, 2, 9.0, 6.0},
        };
        for (const S &d : demo) {
            Clip c;
            c.id = m_nextId++;
            c.name = QString::fromUtf8(d.n);
            c.type = d.t;
            c.track = d.tr;
            c.start = d.s;
            c.length = d.l;
            c.hueSeed = c.id * 37;
            m_clips.push_back(c);
        }
    }

    emitLog(QStringLiteral("timeline ready, %1 clips").arg(m_clips.size()));
}

void EditorTimelineWidget::setScrollBar(QScrollBar *bar)
{
    m_scrollBar = bar;
    if (!m_scrollBar) return;
    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int v){
        m_scrollSec = v / 100.0;
        update();
    });
    updateScrollBar();
}

// ---------------------------------------------------------------- mapping

int EditorTimelineWidget::trackY(int track) const
{
    int y = rulerHeight();
    for (int i = 0; i < track && i < m_tracks.size(); ++i)
        y += m_tracks[i].height;
    return y;
}

int EditorTimelineWidget::trackAtY(int y) const
{
    if (y < rulerHeight()) return -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        int top = trackY(i);
        if (y >= top && y < top + m_tracks[i].height) return i;
    }
    return -1;
}

double EditorTimelineWidget::xToTime(int x) const
{
    return m_scrollSec + (x - headerWidth()) / m_pxPerSec;
}

int EditorTimelineWidget::timeToX(double t) const
{
    return headerWidth() + qRound((t - m_scrollSec) * m_pxPerSec);
}

double EditorTimelineWidget::contentDuration() const
{
    double end = 30.0;
    for (const Clip &c : m_clips)
        end = qMax(end, c.start + c.length + 5.0);
    return end;
}

QRectF EditorTimelineWidget::clipRect(const Clip &c) const
{
    double x = timeToX(c.start);
    double w = c.length * m_pxPerSec;
    int top = trackY(c.track);
    return QRectF(x, top + 2, w, m_tracks[c.track].height - 4);
}

// ---------------------------------------------------------------- painting

void EditorTimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), cBg);

    drawTrackBodies(p);
    drawRuler(p);
    drawTrackHeaders(p);

    // clips
    for (int i = 0; i < m_clips.size(); ++i)
        drawClip(p, i);

    // ghost of dragged clip at original position
    if (m_drag == DragMode::MoveClip && m_dragClip >= 0 && m_dragClip < m_clips.size()) {
        const Clip &cur = m_clips[m_dragClip];
        if (qAbs(cur.start - m_origStart) > 1e-6 || cur.track != m_origTrack)
            drawClip(p, m_dragClip, true);
    }

    // snap guide line
    if (m_snapTarget >= 0.0) {
        int x = timeToX(m_snapTarget);
        p.setPen(QPen(QColor(0xff, 0xd1, 0x54), 1, Qt::DashLine));
        p.drawLine(x, rulerHeight(), x, height());
    }

    drawPlayhead(p);

    // corner between ruler and headers
    p.fillRect(0, 0, headerWidth(), rulerHeight(), cHeader);
    p.setPen(cGridLine);
    p.drawLine(0, rulerHeight() - 1, width(), rulerHeight() - 1);
}

void EditorTimelineWidget::drawRuler(QPainter &p)
{
    p.fillRect(headerWidth(), 0, width() - headerWidth(), rulerHeight(), cRuler);

    // choose tick step so labels stay readable
    double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60, 300};
    double step = 1;
    for (double s : steps) { if (s * m_pxPerSec >= 70) { step = s; break; } }

    p.setFont(font());
    double t0 = qMax(0.0, m_scrollSec - step);
    double first = qFloor(t0 / step) * step;
    for (double t = first; ; t += step) {
        int x = timeToX(t);
        if (x > width() + 4) break;
        if (x < headerWidth()) continue;
        p.setPen(cGridLine);
        p.drawLine(x, rulerHeight() - 8, x, rulerHeight());
        p.setPen(cTextDim);
        p.drawText(QRect(x + 3, 2, 90, rulerHeight() - 10),
                   Qt::AlignLeft | Qt::AlignVCenter, timecode(t));
        // minor ticks
        for (int k = 1; k < 4; ++k) {
            int mx = timeToX(t + step * k / 4.0);
            if (mx <= width()) {
                p.setPen(QColor(0x26, 0x26, 0x26));
                p.drawLine(mx, rulerHeight() - 4, mx, rulerHeight());
            }
        }
    }
}

void EditorTimelineWidget::drawTrackHeaders(QPainter &p)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        int top = trackY(i);
        p.fillRect(0, top, headerWidth(), m_tracks[i].height, cHeader);
        p.setPen(cGridLine);
        p.drawLine(0, top + m_tracks[i].height - 1, headerWidth(), top + m_tracks[i].height - 1);
        p.drawLine(headerWidth() - 1, top, headerWidth() - 1, top + m_tracks[i].height);

        // text-icon per track type
        QString icon = m_tracks[i].type == ClipType::Video ? QStringLiteral("V") : QStringLiteral("A");
        QColor ic = m_tracks[i].type == ClipType::Video ? cAccent : cAudioWave;
        QRect badge(10, top + (m_tracks[i].height - 22) / 2, 22, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(ic.darker(130));
        p.drawRoundedRect(badge, 4, 4);
        p.setPen(QColor(0xe8, 0xe8, 0xe8));
        p.drawText(badge, Qt::AlignCenter, icon);

        p.setPen(cText);
        p.drawText(QRect(38, top, headerWidth() - 44, m_tracks[i].height),
                   Qt::AlignVCenter, m_tracks[i].name);
    }
}

void EditorTimelineWidget::drawTrackBodies(QPainter &p)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        int top = trackY(i);
        p.fillRect(headerWidth(), top, width() - headerWidth(), m_tracks[i].height,
                   (i % 2) ? cBgAlt : cBg);
        p.setPen(QColor(0x26, 0x26, 0x26));
        p.drawLine(headerWidth(), top + m_tracks[i].height - 1, width(), top + m_tracks[i].height - 1);
    }
}

QString EditorTimelineWidget::timecode(double t) const
{
    int total = int(t);
    int mm = total / 60, ss = total % 60;
    int ff = int((t - total) * 25.0 + 0.5); // assume 25fps display
    return QStringLiteral("%1:%2:%3")
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

QPixmap EditorTimelineWidget::thumbnailTile(const Clip &c, int h)
{
    // one tile per clip; repeated across the clip body
    const int tileW = qMax(48, int(h * 16.0 / 9.0));
    QString key = QStringLiteral("%1x%2").arg(c.id).arg(h);
    auto it = m_thumbCache.find(key);
    if (it != m_thumbCache.end()) return it.value();

    QPixmap pm(tileW, h);
    pm.fill(Qt::transparent);
    QPainter tp(&pm);
    tp.setRenderHint(QPainter::Antialiasing);

    QRandomGenerator rng(quint32(c.hueSeed));
    // base gradient, hue varies per clip
    int hue = (c.hueSeed * 47) % 360;
    QColor base = QColor::fromHsv(hue, 110, 150);
    QColor base2 = QColor::fromHsv((hue + 40) % 360, 130, 110);
    QLinearGradient g(0, 0, tileW, h);
    g.setColorAt(0, base);
    g.setColorAt(1, base2);
    tp.fillRect(pm.rect(), g);

    // scenery-ish shapes so tiles look like frames
    for (int i = 0; i < 4; ++i) {
        int w = 10 + rng.bounded(tileW / 2);
        int hh = 6 + rng.bounded(h / 2);
        int x = rng.bounded(qMax(1, tileW - w));
        int y = rng.bounded(qMax(1, h - hh));
        QColor c2 = QColor::fromHsv((hue + rng.bounded(120)) % 360,
                                    60 + rng.bounded(120), 90 + rng.bounded(120));
        c2.setAlpha(150);
        tp.setPen(Qt::NoPen);
        tp.setBrush(c2);
        if (rng.bounded(2)) tp.drawEllipse(x, y, w, hh);
        else tp.drawRoundedRect(x, y, w, hh, 3, 3);
    }
    // silhouette horizon
    tp.setBrush(QColor(0, 0, 0, 90));
    QPolygonF hill;
    hill << QPointF(0, h);
    for (int x = 0; x <= tileW; x += 8)
        hill << QPointF(x, h - 6 - rng.bounded(h / 3));
    hill << QPointF(tileW, h);
    tp.drawPolygon(hill);

    // vignette
    QLinearGradient v(0, 0, 0, h);
    v.setColorAt(0, QColor(255, 255, 255, 26));
    v.setColorAt(0.5, QColor(0, 0, 0, 0));
    v.setColorAt(1, QColor(0, 0, 0, 70));
    tp.fillRect(pm.rect(), v);
    tp.end();

    m_thumbCache.insert(key, pm);
    return pm;
}

void EditorTimelineWidget::drawClip(QPainter &p, int index, bool ghost)
{
    Clip c = m_clips[index];
    if (ghost) { c.start = m_origStart; c.track = m_origTrack; c.length = m_origLength; }
    QRectF r = clipRect(c);
    if (r.right() < headerWidth() || r.left() > width()) return;

    bool selected = (index == m_selected) && !ghost;
    bool hovered  = (index == m_hover) && !ghost;

    p.save();
    if (ghost) p.setOpacity(0.35);
    // keep everything of the clip inside the content area
    p.setClipRect(QRectF(headerWidth(), 0, width() - headerWidth(), height()));

    QPainterPath path;
    path.addRoundedRect(r, 4, 4);

    const int nameBarH = 16;

    if (c.type == ClipType::Video) {
        // body
        p.fillPath(path, QColor(0x2a, 0x2a, 0x2c));
        // thumbnail filmstrip below the name bar
        QRectF body(r.left(), r.top() + nameBarH, r.width(), r.height() - nameBarH);
        if (body.height() > 4) {
            p.save();
            p.setClipRect(body, Qt::IntersectClip);
            QPixmap tile = thumbnailTile(c, int(body.height()));
            for (double x = body.left(); x < body.right(); x += tile.width()) {
                p.drawPixmap(QPointF(x, body.top()), tile);
                p.setPen(QColor(0, 0, 0, 120));
                p.drawLine(QPointF(x, body.top()), QPointF(x, body.bottom()));
            }
            p.restore();
        }
        // name bar
        p.save();
        p.setClipRect(r, Qt::IntersectClip);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), nameBarH),
                   ghost ? cVideoBar.darker(160) : cVideoBar);
        p.restore();
    } else {
        // audio: dark blue body + waveform
        p.fillPath(path, cAudioBody);
        QRectF body = r.adjusted(2, nameBarH + 2, -2, -3);
        if (body.width() > 4 && body.height() > 4) {
            // deterministic pseudo waveform (vertical bars)
            p.setPen(QPen(cAudioWave, 1));
            double mid = body.center().y();
            int n = int(body.width());
            for (int i = 0; i <= n; ++i) {
                quint32 hsh = quint32(c.id * 2654435761u) ^ quint32(i * 40503u);
                hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
                double amp = (hsh % 1000) / 1000.0;
                amp = 0.15 + 0.85 * amp * (0.55 + 0.45 * qSin(i * 0.05));
                double x = body.left() + i;
                p.drawLine(QPointF(x, mid - amp * body.height() / 2),
                           QPointF(x, mid + amp * body.height() / 2));
            }
        }
        p.save();
        p.setClipRect(r, Qt::IntersectClip);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), nameBarH), cAudioBody.lighter(135));
        p.restore();
    }

    // clip name
    p.setPen(QColor(0xec, 0xec, 0xec));
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    p.drawText(r.adjusted(5, 0, -4, -(r.height() - nameBarH)),
               Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(c.name, Qt::ElideRight, int(r.width() - 8)));

    // border: selected = accent, hovered = lighter
    p.setBrush(Qt::NoBrush); // drawPath would otherwise fill with the leftover badge brush
    if (selected) {
        p.setPen(QPen(cAccent, 2));
        p.drawPath(path);
        // trim handles
        p.setPen(Qt::NoPen);
        p.setBrush(cAccent);
        p.drawRoundedRect(QRectF(r.left(), r.top(), 5, r.height()), 2, 2);
        p.drawRoundedRect(QRectF(r.right() - 5, r.top(), 5, r.height()), 2, 2);
    } else {
        p.setPen(QPen(hovered ? QColor(0x9a, 0x9a, 0x9a) : QColor(0x10, 0x10, 0x10), 1));
        p.drawPath(path);
    }
    p.restore();
}

void EditorTimelineWidget::drawPlayhead(QPainter &p)
{
    int x = timeToX(m_playhead);
    if (x < headerWidth()) return;
    p.setPen(QPen(cPlayhead, 1.5));
    p.drawLine(x, 0, x, height());

    // handle in ruler
    QPolygonF head;
    head << QPointF(x - 7, 0) << QPointF(x + 7, 0)
         << QPointF(x + 7, 10) << QPointF(x, 18)
         << QPointF(x - 7, 10);
    p.setPen(Qt::NoPen);
    p.setBrush(cPlayhead);
    p.drawPolygon(head);
}

// ---------------------------------------------------------------- picking

int EditorTimelineWidget::clipAt(const QPoint &pos, QRectF *rectOut) const
{
    // topmost track last drawn wins: iterate from end
    for (int i = m_clips.size() - 1; i >= 0; --i) {
        QRectF r = clipRect(m_clips[i]);
        if (r.contains(pos)) {
            if (rectOut) *rectOut = r;
            return i;
        }
    }
    return -1;
}

double EditorTimelineWidget::snapTime(double t, int ignoreClipIdx, bool *snappedOut) const
{
    double best = t;
    double bestDist = SNAP_PX / m_pxPerSec;
    bool snapped = false;

    auto consider = [&](double cand){
        double d = qAbs(cand - t);
        if (d < bestDist) { bestDist = d; best = cand; snapped = true; }
    };
    consider(m_playhead);
    for (int i = 0; i < m_clips.size(); ++i) {
        if (i == ignoreClipIdx) continue;
        consider(m_clips[i].start);
        consider(m_clips[i].start + m_clips[i].length);
    }
    if (snappedOut) *snappedOut = snapped;
    return best;
}

bool EditorTimelineWidget::overlapsOnTrack(int track, double start, double len, int ignoreIdx) const
{
    for (int i = 0; i < m_clips.size(); ++i) {
        if (i == ignoreIdx || m_clips[i].track != track) continue;
        const Clip &o = m_clips[i];
        if (start < o.start + o.length - 1e-9 && o.start < start + len - 1e-9)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------- events

void EditorTimelineWidget::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    m_pressPos = e->pos();

    if (e->button() == Qt::MiddleButton) {
        // reserved: pan view
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    // playhead: ruler area or near playhead line
    int phx = timeToX(m_playhead);
    if (e->pos().y() <= rulerHeight() || qAbs(e->pos().x() - phx) <= 4) {
        m_drag = DragMode::Playhead;
        m_playhead = qMax(0.0, xToTime(e->pos().x()));
        emitLog(QStringLiteral("playhead -> %1").arg(timecode(m_playhead)));
        update();
        return;
    }

    QRectF r;
    int idx = clipAt(e->pos(), &r);
    if (idx >= 0) {
        m_selected = idx;
        const Clip &c = m_clips[idx];
        emit selectionChanged(QStringLiteral("%1  [%2 → %3]")
                              .arg(c.name, timecode(c.start), timecode(c.start + c.length)));
        m_dragClip = idx;
        m_origStart = c.start;
        m_origLength = c.length;
        m_origTrack = c.track;

        bool nearL = qAbs(e->pos().x() - r.left()) <= TRIM_PX;
        bool nearR = qAbs(e->pos().x() - r.right()) <= TRIM_PX;
        if (nearL && !nearR) {
            m_drag = DragMode::TrimLeft;
        } else if (nearR) {
            m_drag = DragMode::TrimRight;
        } else {
            m_drag = DragMode::MoveClip;
            m_grabOffsetSec = xToTime(e->pos().x()) - c.start;
        }
    } else {
        m_selected = -1;
        emit selectionChanged(QString());
        m_drag = DragMode::None;
    }
    update();
}

void EditorTimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == DragMode::None) {
        // hover + cursor feedback
        QRectF r;
        int idx = clipAt(e->pos(), &r);
        if (idx != m_hover) { m_hover = idx; update(); }
        if (idx >= 0 &&
            (qAbs(e->pos().x() - r.left()) <= TRIM_PX || qAbs(e->pos().x() - r.right()) <= TRIM_PX))
            setCursor(Qt::SizeHorCursor);
        else if (e->pos().y() <= rulerHeight())
            setCursor(Qt::PointingHandCursor);
        else
            unsetCursor();
        return;
    }

    m_snapTarget = -1.0;
    double t = xToTime(e->pos().x());

    switch (m_drag) {
    case DragMode::Playhead:
        m_playhead = qMax(0.0, t);
        break;
    case DragMode::MoveClip: {
        Clip &c = m_clips[m_dragClip];
        double newStart = qMax(0.0, t - m_grabOffsetSec);
        bool snapped = false;
        double snappedT = snapTime(newStart, m_dragClip, &snapped);
        if (snapped) { newStart = snappedT; m_snapTarget = snappedT; }

        // track switching: only to a compatible track
        int tr = trackAtY(e->pos().y());
        if (tr < 0) tr = c.track;
        if (m_tracks[tr].type != c.type) tr = c.track;

        // overlap constraint: revert if colliding
        if (!overlapsOnTrack(tr, newStart, c.length, m_dragClip)) {
            c.start = newStart;
            c.track = tr;
        }
        break;
    }
    case DragMode::TrimLeft: {
        Clip &c = m_clips[m_dragClip];
        double end = m_origStart + m_origLength;
        double ns = qBound(0.0, t, end - MIN_CLIP_LEN);
        bool snapped = false;
        double sT = snapTime(ns, m_dragClip, &snapped);
        if (snapped) { ns = sT; m_snapTarget = sT; }
        if (!overlapsOnTrack(c.track, ns, end - ns, m_dragClip)) {
            c.start = ns;
            c.length = end - ns;
        }
        break;
    }
    case DragMode::TrimRight: {
        Clip &c = m_clips[m_dragClip];
        double ne = qMax(t, m_origStart + MIN_CLIP_LEN);
        bool snapped = false;
        double sT = snapTime(ne, m_dragClip, &snapped);
        if (snapped) { ne = sT; m_snapTarget = sT; }
        if (!overlapsOnTrack(c.track, c.start, ne - c.start, m_dragClip))
            c.length = ne - c.start;
        break;
    }
    default: break;
    }

    // edge auto-scroll while dragging
    if (m_drag != DragMode::Playhead) {
        if (e->pos().x() > width() - 24) m_scrollSec += 20 / m_pxPerSec;
        else if (e->pos().x() < headerWidth() + 24) m_scrollSec = qMax(0.0, m_scrollSec - 20 / m_pxPerSec);
        updateScrollBar();
    }
    update();
}

void EditorTimelineWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;

    if (m_drag != DragMode::None && m_dragClip >= 0 && m_dragClip < m_clips.size()) {
        const Clip &c = m_clips[m_dragClip];
        if (qAbs(c.start - m_origStart) > 1e-6 || qAbs(c.length - m_origLength) > 1e-6 ||
            c.track != m_origTrack) {
            QString what = m_drag == DragMode::MoveClip ? QStringLiteral("move")
                         : m_drag == DragMode::TrimLeft ? QStringLiteral("trim-left")
                                                        : QStringLiteral("trim-right");
            emitLog(QStringLiteral("%1 clip \"%2\": [%3 → %4] track %5")
                    .arg(what, c.name, timecode(c.start), timecode(c.start + c.length))
                    .arg(m_tracks[c.track].name));
            emit selectionChanged(QStringLiteral("%1  [%2 → %3]")
                                  .arg(c.name, timecode(c.start), timecode(c.start + c.length)));
        }
    }
    m_drag = DragMode::None;
    m_dragClip = -1;
    m_snapTarget = -1.0;
    update();
}

void EditorTimelineWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    // double click ruler: move playhead without drag
    if (e->pos().y() <= rulerHeight()) {
        m_playhead = qMax(0.0, xToTime(e->pos().x()));
        update();
    }
}

void EditorTimelineWidget::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        double factor = e->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
        applyZoom(factor, e->position().x());
    } else {
        double delta = -e->angleDelta().y() / 120.0; // steps
        m_scrollSec = qMax(0.0, m_scrollSec + delta * 40.0 / m_pxPerSec);
        clampView();
        updateScrollBar();
    }
    update();
    e->accept();
}

void EditorTimelineWidget::keyPressEvent(QKeyEvent *e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_selected >= 0) {
        removeSelectedClip();
        return;
    }
    QWidget::keyPressEvent(e);
}

void EditorTimelineWidget::leaveEvent(QEvent *)
{
    if (m_hover != -1) { m_hover = -1; update(); }
}

void EditorTimelineWidget::resizeEvent(QResizeEvent *)
{
    updateScrollBar();
}

// ---------------------------------------------------------------- public ops

void EditorTimelineWidget::addVideoClip()
{
    int track = 1; // V1
    double len = 4.0;
    double s = m_playhead;
    while (overlapsOnTrack(track, s, len, -1) && s < 1e6) {
        // jump past the blocking clip
        double next = s + len;
        for (const Clip &o : m_clips)
            if (o.track == track && s < o.start + o.length && o.start < s + len)
                next = qMax(next, o.start + o.length);
        if (next <= s) break;
        s = next;
    }
    Clip c;
    c.id = m_nextId++;
    c.name = QStringLiteral("clip_%1.mp4").arg(c.id, 3, 10, QLatin1Char('0'));
    c.type = ClipType::Video;
    c.track = track;
    c.start = s;
    c.length = len;
    c.hueSeed = c.id * 37;
    m_clips.push_back(c);
    m_selected = m_clips.size() - 1;
    emitLog(QStringLiteral("add video clip \"%1\" at %2").arg(c.name, timecode(s)));
    updateScrollBar();
    update();
}

void EditorTimelineWidget::addAudioClip()
{
    int track = m_tracks.size() - 1; // last = audio
    double len = 5.0;
    double s = m_playhead;
    while (overlapsOnTrack(track, s, len, -1) && s < 1e6) {
        double next = s + len;
        for (const Clip &o : m_clips)
            if (o.track == track && s < o.start + o.length && o.start < s + len)
                next = qMax(next, o.start + o.length);
        if (next <= s) break;
        s = next;
    }
    Clip c;
    c.id = m_nextId++;
    c.name = QStringLiteral("audio_%1.mp3").arg(c.id, 3, 10, QLatin1Char('0'));
    c.type = ClipType::Audio;
    c.track = track;
    c.start = s;
    c.length = len;
    c.hueSeed = c.id * 37;
    m_clips.push_back(c);
    m_selected = m_clips.size() - 1;
    emitLog(QStringLiteral("add audio clip \"%1\" at %2").arg(c.name, timecode(s)));
    updateScrollBar();
    update();
}

void EditorTimelineWidget::removeSelectedClip()
{
    if (m_selected < 0 || m_selected >= m_clips.size()) return;
    emitLog(QStringLiteral("remove clip \"%1\"").arg(m_clips[m_selected].name));
    m_thumbCache.remove(QStringLiteral("%1").arg(m_clips[m_selected].id));
    m_clips.removeAt(m_selected);
    m_selected = -1;
    m_hover = -1;
    emit selectionChanged(QString());
    update();
}

void EditorTimelineWidget::applyZoom(double factor, int anchorX)
{
    double anchorTime = xToTime(anchorX);
    double old = m_pxPerSec;
    m_pxPerSec = qBound(4.0, m_pxPerSec * factor, 1200.0);
    // keep anchorTime under the cursor
    m_scrollSec = anchorTime - (anchorX - headerWidth()) / m_pxPerSec;
    if (m_pxPerSec != old) {
        emitLog(QStringLiteral("zoom %1 px/s").arg(m_pxPerSec, 0, 'f', 1));
    }
    clampView();
    updateScrollBar();
    update();
}

void EditorTimelineWidget::zoomIn()  { applyZoom(1.3, headerWidth() + (width() - headerWidth()) / 2); }
void EditorTimelineWidget::zoomOut() { applyZoom(1.0 / 1.3, headerWidth() + (width() - headerWidth()) / 2); }

void EditorTimelineWidget::zoomFit()
{
    double dur = contentDuration();
    int avail = width() - headerWidth();
    if (dur > 0 && avail > 0)
        m_pxPerSec = qBound(4.0, avail / dur, 1200.0);
    m_scrollSec = 0;
    updateScrollBar();
    update();
}

void EditorTimelineWidget::clampView()
{
    double maxScroll = qMax(0.0, contentDuration() - (width() - headerWidth()) / m_pxPerSec);
    m_scrollSec = qBound(0.0, m_scrollSec, maxScroll + 2.0);
}

void EditorTimelineWidget::updateScrollBar()
{
    if (!m_scrollBar) return;
    clampView();
    double content = contentDuration();
    double page = (width() - headerWidth()) / m_pxPerSec;
    m_scrollBar->setRange(0, int(qMax(0.0, content - page) * 100));
    m_scrollBar->setPageStep(int(page * 100));
    m_scrollBar->setValue(int(m_scrollSec * 100));
    m_scrollBar->setSingleStep(int(0.5 * 100));
}

void EditorTimelineWidget::emitLog(const QString &msg)
{
    emit logMessage(QStringLiteral("[%1] %2")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), msg));
}
