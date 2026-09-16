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

#include "edittimelinepanel.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QtMath>
#include <cmath>
#include <algorithm>
#include <climits>

// ------------------------------ style constants ------------------------------
// Visual spec taken from the reference screenshot: one uniform dark
// canvas, square-cornered clips tightly packed, a 22px teal name strip
// on video clips, filmstrip thumbnails, a teal waveform strip glued to
// the bottom of video clips, blue audio blocks, 2px light playhead.

namespace {
const int kRulerH = 26;         // timecode ruler
const int kVideoTrackH = 114;   // 22 label + 66 thumbs + 26 wave
const int kAudioTrackH = 72;    // 20 label + 52 wave
const int kTrackGap = 6;
const int kClipGap = 2;         // horizontal gap between clips
const int kVideoLabelH = 22;
const int kAudioLabelH = 20;
const int kVideoThumbH = 66;
const int kVideoWaveH = 26;
const int kThumbW = 64;
const int kEdgeZone = 8;        // trim handle hit zone in pixels
const int kSnapPx = 6;
const int kDragThreshold = 5;
const int kScrollW = 12;
const int kHandle = 6;          // selected-clip corner handle size

const QColor kColBg(0x26, 0x26, 0x26);
const QColor kColRuler(0x21, 0x21, 0x21);
const QColor kColRulerText(0x8A, 0x8A, 0x8A);
const QColor kColRulerTick(0x50, 0x50, 0x50);
const QColor kColPlayhead(0xDD, 0xDD, 0xDD);
const QColor kColVideoLabel(0x1B, 0x5E, 0x5A);
const QColor kColVideoLabelText(0xDD, 0xDD, 0xDD);
const QColor kColVideoThumbPh(0x16, 0x40, 0x3C);
const QColor kColVideoWaveBg(0x1A, 0x4A, 0x48);
const QColor kColVideoWave(0x3F, 0xA8, 0xA0);
const QColor kColAudioBlock(0x1D, 0x4F, 0x94);
const QColor kColAudioLabel(0x2A, 0x5F, 0xA8);
const QColor kColAudioText(0xDD, 0xE6, 0xF5);
const QColor kColAudioWave(0x5B, 0xA0, 0xE0);
const QColor kColSelect(0xFF, 0xFF, 0xFF);
const QColor kColHint(0x8A, 0x92, 0x98);

QFont smallFont(const QFont& base)
{
    QFont f = base;
    if (f.pointSize() > 0) f.setPointSize(qMax(7, f.pointSize() - 2));
    return f;
}

QString timeCode(const int frame, const int fps)
{
    const int f = qMax(1, fps);
    const int ff = frame % f;
    const int totalSec = frame / f;
    const int ss = totalSec % 60;
    const int mm = totalSec / 60;
    return QString("%1:%2:%3").arg(mm, 2, 10, QChar('0')).
            arg(ss, 2, 10, QChar('0')).arg(ff, 2, 10, QChar('0'));
}

QString secondsLabel(const int frames, const int fps)
{
    return QString::number(qreal(frames) / qMax(1, fps), 'f', 1) +
            QStringLiteral("s");
}

// scene clips live on video tracks, audio clips on audio tracks
bool clipFitsTrack(const EditClip& clip, const EditTrack& track)
{
    if (track.kind == EditTrack::Kind::Video) {
        return clip.kind == EditClip::Kind::Scene;
    }
    return clip.kind == EditClip::Kind::Audio;
}
}

// ------------------------------ stub API ------------------------------

void EditTimelineApiStub::load(EditTimelineData& out)
{
    EditTimelineData d;
    d.fps = 25;
    d.playheadFrame = 30;

    EditTrack video1;
    video1.kind = EditTrack::Kind::Video;
    video1.name = QStringLiteral("视频轨 1");
    EditClip c1;
    c1.kind = EditClip::Kind::Scene;
    c1.name = QStringLiteral("素材 1");
    c1.startFrame = 0;
    c1.durationFrames = 75;
    video1.clips << c1;
    EditClip c2 = c1;
    c2.name = QStringLiteral("素材 2");
    c2.startFrame = 75;
    c2.durationFrames = 120;
    video1.clips << c2;
    EditClip c3 = c1;
    c3.name = QStringLiteral("素材 3");
    c3.startFrame = 200;
    c3.durationFrames = 64;
    video1.clips << c3;

    EditTrack video2;
    video2.kind = EditTrack::Kind::Video;
    video2.name = QStringLiteral("视频轨 2");
    EditClip o1 = c1;
    o1.name = QStringLiteral("叠加 1");
    o1.startFrame = 96;
    o1.durationFrames = 88;
    video2.clips << o1;

    EditTrack audio1;
    audio1.kind = EditTrack::Kind::Audio;
    audio1.name = QStringLiteral("音频轨 1");
    EditClip a1;
    a1.kind = EditClip::Kind::Audio;
    a1.name = QStringLiteral("音乐");
    a1.startFrame = 0;
    a1.durationFrames = 240;
    audio1.clips << a1;
    EditClip a2 = a1;
    a2.name = QStringLiteral("音效");
    a2.startFrame = 250;
    a2.durationFrames = 60;
    audio1.clips << a2;

    d.tracks << video1 << video2 << audio1;
    out = d;
    mData = d;
}

void EditTimelineApiStub::compositionNames(QStringList& out)
{
    out << QStringLiteral("合成 1") << QStringLiteral("合成 2");
}

void EditTimelineApiStub::sceneNames(QStringList& out)
{
    out = mScenes;
    if (out.isEmpty()) {
        out << QStringLiteral("场景 A") << QStringLiteral("场景 B")
            << QStringLiteral("场景 C") << QStringLiteral("场景 D");
    }
}

void EditTimelineApiStub::moveClip(const int trackIdx, const int clipIdx,
                                   const int newStart)
{
    if (trackIdx < 0 || trackIdx >= mData.tracks.count()) return;
    auto& clips = mData.tracks[trackIdx].clips;
    if (clipIdx < 0 || clipIdx >= clips.count()) return;
    clips[clipIdx].startFrame = newStart;
}

void EditTimelineApiStub::trimClip(const int trackIdx, const int clipIdx,
                                   const int newStart, const int newDuration)
{
    if (trackIdx < 0 || trackIdx >= mData.tracks.count()) return;
    auto& clips = mData.tracks[trackIdx].clips;
    if (clipIdx < 0 || clipIdx >= clips.count()) return;
    clips[clipIdx].startFrame = newStart;
    clips[clipIdx].durationFrames = qMax(1, newDuration);
}

void EditTimelineApiStub::moveClipToTrack(const int fromTrack,
                                          const int clipIdx,
                                          const int toTrack)
{
    if (fromTrack < 0 || fromTrack >= mData.tracks.count()) return;
    if (toTrack < 0 || toTrack >= mData.tracks.count()) return;
    auto& src = mData.tracks[fromTrack].clips;
    if (clipIdx < 0 || clipIdx >= src.count()) return;
    if (!clipFitsTrack(src[clipIdx], mData.tracks[toTrack])) return;
    mData.tracks[toTrack].clips << src.takeAt(clipIdx);
}

void EditTimelineApiStub::setPlayhead(const int frame)
{
    mData.playheadFrame = frame;
}

void EditTimelineApiStub::openClip(const int trackIdx, const int clipIdx)
{
    Q_UNUSED(trackIdx)
    Q_UNUSED(clipIdx)
    // engine adapter: switchToScene(resolved source scene)
}

void EditTimelineApiStub::addSceneClip(const int sceneIndex)
{
    QStringList names;
    sceneNames(names);
    const QString name = names.value(sceneIndex,
                                     QStringLiteral("新素材"));
    if (mData.tracks.isEmpty()) return;
    // append after the last clip of the first video track
    for (auto& track : mData.tracks) {
        if (track.kind != EditTrack::Kind::Video) continue;
        int end = 0;
        for (const auto& c : track.clips) {
            end = qMax(end, c.startFrame + c.durationFrames);
        }
        EditClip c;
        c.kind = EditClip::Kind::Scene;
        c.name = name;
        c.startFrame = end;
        c.durationFrames = 80;
        track.clips << c;
        return;
    }
}

void EditTimelineApiStub::createComposition(int& newCompositionIndex)
{
    newCompositionIndex = ++mCompSeq;
}

void EditTimelineApiStub::requestThumb(
        const QString& key, const int seed, const QSize& size,
        std::function<void(const QString&, const QImage&)> cb)
{
    QImage img(size, QImage::Format_RGB32);
    const int hue = 150 + (seed * 37) % 60;
    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0.0, QColor::fromHsl(hue, 90, 96));
    grad.setColorAt(1.0, QColor::fromHsl(hue + 20, 110, 56));
    QPainter p(&img);
    p.fillRect(img.rect(), grad);
    p.setPen(QColor(255, 255, 255, 90));
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(10, size.height() / 3));
    p.setFont(f);
    p.drawText(img.rect(), Qt::AlignCenter, QString::number(seed));
    p.end();
    cb(key, img);
}

void EditTimelineApiStub::requestWave(
        const QString& key, const int sampleCount,
        std::function<void(const QString&, const QVector<qreal>&)> cb)
{
    QVector<qreal> samples(sampleCount);
    const qreal seed = qreal(qHash(key) % 97) * 0.13;
    for (int i = 0; i < sampleCount; ++i) {
        const qreal v = (0.15 +
                         0.75 * std::fabs(std::sin(i * 0.31 + seed)) *
                         (0.55 + 0.45 * std::fabs(std::sin(i * 0.11 +
                                                   seed * 2.7))));
        samples[i] = qBound(0.0, v, 1.0);
    }
    cb(key, samples);
}

// ------------------------------ view ------------------------------

EditTimelineView::EditTimelineView(EditTimelinePanel* const panel)
    : QWidget(panel)
    , mPanel(panel)
{
    setMouseTracking(true);
    setMinimumHeight(180);
    setFocusPolicy(Qt::ClickFocus);
    mHBar = new QScrollBar(Qt::Horizontal, this);
    mVBar = new QScrollBar(Qt::Vertical, this);
    mHBar->hide();
    mVBar->hide();
    connect(mHBar, &QScrollBar::valueChanged, this, [this](int) {
        update();
    });
    connect(mVBar, &QScrollBar::valueChanged, this, [this](int) {
        update();
    });
}

int EditTimelineView::firstViewedFrame() const
{
    return mHBar->value();
}

qreal EditTimelineView::xAtFrame(const int frame) const
{
    return (frame - firstViewedFrame()) * mPpf;
}

int EditTimelineView::frameAtX(const int x) const
{
    return qRound(firstViewedFrame() + x / mPpf);
}

int EditTimelineView::trackHeight(const int trackIdx) const
{
    const auto& tracks = mPanel->mData.tracks;
    if (trackIdx < 0 || trackIdx >= tracks.count()) return 0;
    return tracks.at(trackIdx).kind == EditTrack::Kind::Video ?
                kVideoTrackH : kAudioTrackH;
}

int EditTimelineView::trackTop(const int trackIdx) const
{
    int y = kRulerH;
    for (int i = 0; i < trackIdx && i < mPanel->mData.tracks.count(); ++i) {
        y += trackHeight(i) + kTrackGap;
    }
    return y;
}

QRect EditTimelineView::clipRect(const int trackIdx,
                                 const int clipIdx) const
{
    const auto& tracks = mPanel->mData.tracks;
    const auto& clip = tracks.at(trackIdx).clips.at(clipIdx);
    const qreal x = xAtFrame(clip.startFrame);
    const qreal w = qMax(4.0, clip.durationFrames * mPpf - kClipGap);
    const int top = trackTop(trackIdx) + 1;
    return QRect(qRound(x), top, qRound(w), trackHeight(trackIdx) - 2);
}

int EditTimelineView::clipAtFrame(const int trackIdx, const int frame) const
{
    const auto& clips = mPanel->mData.tracks.at(trackIdx).clips;
    for (int i = 0; i < clips.count(); ++i) {
        const auto& c = clips.at(i);
        if (frame >= c.startFrame &&
            frame < c.startFrame + c.durationFrames) return i;
    }
    return -1;
}

EditTimelineView::Hit EditTimelineView::hitTest(const QPoint& pos) const
{
    Hit hit;
    const auto& tracks = mPanel->mData.tracks;
    for (int t = 0; t < tracks.count(); ++t) {
        const int top = trackTop(t);
        const int bottom = top + trackHeight(t);
        if (pos.y() < top || pos.y() >= bottom) continue;
        const int frame = frameAtX(pos.x());
        const int c = clipAtFrame(t, frame);
        if (c < 0) return hit;
        const QRect rc = clipRect(t, c);
        if (pos.x() < rc.left() || pos.x() > rc.right()) return hit;
        hit.track = t;
        hit.clip = c;
        if (pos.x() - rc.left() <= kEdgeZone) hit.zone = Zone::EdgeMin;
        else if (rc.right() - pos.x() <= kEdgeZone) hit.zone = Zone::EdgeMax;
        else hit.zone = Zone::Body;
        return hit;
    }
    return hit;
}

void EditTimelineView::dataChanged()
{
    updateScrollRanges();
    update();
}

void EditTimelineView::updateScrollRanges()
{
    const auto& tracks = mPanel->mData.tracks;
    int maxFrame = 300;
    for (const auto& t : tracks) {
        for (const auto& c : t.clips) {
            maxFrame = qMax(maxFrame, c.startFrame + c.durationFrames);
        }
    }
    mHBar->setRange(0, maxFrame + 100);
    mHBar->setPageStep(qMax(1, qRound(width() / mPpf)));

    int contentH = kRulerH + 8;
    for (int t = 0; t < tracks.count(); ++t) {
        contentH += trackHeight(t) + kTrackGap;
    }
    mVBar->setRange(0, qMax(0, contentH - height()));
    mVBar->setPageStep(qMax(1, height() / 2));
    mHBar->setVisible(mHBar->maximum() > mHBar->minimum());
    mVBar->setVisible(mVBar->maximum() > mVBar->minimum());
}

void EditTimelineView::requestClipAssets(const int trackIdx,
                                         const int clipIdx,
                                         const QRect& rc)
{
    if (!mPanel->mListening) return;
    const auto& track = mPanel->mData.tracks.at(trackIdx);
    const auto& clip = track.clips.at(clipIdx);
    if (track.kind == EditTrack::Kind::Video &&
        clip.kind == EditClip::Kind::Scene) {
        const int slotCount = qMax(1, rc.width() / kThumbW);
        for (int s = 0; s < slotCount; ++s) {
            const QString key = QStringLiteral("t%1:c%2:s%3").
                    arg(trackIdx).arg(clipIdx).arg(s);
            if (mPanel->mThumbCache.contains(key)) continue;
            mPanel->mApi->requestThumb(key, clipIdx * 13 + s,
                                       QSize(kThumbW, kVideoThumbH),
                [panel = QPointer<EditTimelinePanel>(mPanel)]
                (const QString& k, const QImage& img) {
                    if (!panel) return;
                    panel->mThumbCache.insert(k, img);
                    panel->mView->update();
                });
        }
    }
    const QString wkey = QStringLiteral("w%1:c%2:%3").
            arg(trackIdx).arg(clipIdx).arg(rc.width());
    if (!mPanel->mWaveCache.contains(wkey)) {
        mPanel->mApi->requestWave(wkey, qMax(2, rc.width() / 2),
            [panel = QPointer<EditTimelinePanel>(mPanel)]
            (const QString& k, const QVector<qreal>& v) {
                if (!panel) return;
                panel->mWaveCache.insert(k, v);
                panel->mView->update();
            });
    }
}

void EditTimelineView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kColBg);
    const auto& tracks = mPanel->mData.tracks;

    p.translate(0, -mVBar->value());

    if (tracks.isEmpty()) {
        p.setPen(kColHint);
        p.drawText(QRect(0, kRulerH + 20, width(), 80),
                   Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                   tr("点击「添加素材」把场景作为素材块加进来"));
    }

    for (int t = 0; t < tracks.count(); ++t) {
        drawTrackBackground(&p, t);
        const auto& clips = tracks.at(t).clips;
        for (int c = 0; c < clips.count(); ++c) {
            const QRect rc = clipRect(t, c);
            if (rc.right() < 0 || rc.left() > width()) continue;
            drawClip(&p, t, c, rc);
        }
    }

    // row-switch ghost: translucent block riding the target track
    if (mDrag == Drag::RowSwitch && mGhostTrack >= 0 &&
        mPressHit.track >= 0 && mPressHit.clip >= 0) {
        const auto& clip = mPanel->mData.tracks.at(mPressHit.track).
                clips.at(mPressHit.clip);
        const int frame = frameAtX(mPressPos.x());
        const qreal gx = xAtFrame(frame - clip.durationFrames / 2);
        const int top = trackTop(mGhostTrack) + 1;
        QRect ghost(qRound(gx), top,
                    qRound(clip.durationFrames * mPpf - kClipGap),
                    trackHeight(mGhostTrack) - 2);
        ghost = ghost.intersected(rect().adjusted(0, kRulerH, 0, 0));
        if (!ghost.isNull()) {
            p.setOpacity(0.55);
            p.fillRect(ghost, clip.kind == EditClip::Kind::Scene ?
                        kColVideoLabel : kColAudioLabel);
            p.setOpacity(1.0);
            p.setPen(QPen(kColSelect, 1));
            p.drawRect(ghost);
        }
    }

    p.resetTransform();

    drawRuler(&p);
    drawPlayhead(&p);
}

void EditTimelineView::drawTrackBackground(QPainter* const p,
                                           const int trackIdx)
{
    Q_UNUSED(p)
    Q_UNUSED(trackIdx)
    // the reference shows one uniform dark canvas: no row stripes, no
    // separators, no track headers
}

void EditTimelineView::drawClip(QPainter* const p, const int trackIdx,
                                const int clipIdx, const QRect& rc)
{
    const auto& track = mPanel->mData.tracks.at(trackIdx);
    const auto& clip = track.clips.at(clipIdx);
    const int fps = qMax(1, mPanel->mData.fps);

    if (clip.kind == EditClip::Kind::Scene) {
        // 22px teal name strip: name on the left, duration on the right
        const QRect label(rc.left(), rc.top(), rc.width(), kVideoLabelH);
        p->fillRect(label, kColVideoLabel);
        p->setFont(smallFont(p->font()));
        const int durW = 52;
        const QString text = p->fontMetrics().elidedText(
                    clip.name, Qt::ElideRight,
                    qMax(0, rc.width() - 12 - durW));
        p->setPen(kColVideoLabelText);
        p->drawText(QRect(label.left() + 6, label.top(),
                          qMax(0, label.width() - 12 - durW), label.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, text);
        p->setPen(QColor(0xAA, 0xDD, 0xD9));
        p->drawText(QRect(label.right() - durW - 4, label.top(),
                          durW, label.height()),
                    Qt::AlignVCenter | Qt::AlignRight,
                    secondsLabel(clip.durationFrames, fps));

        // filmstrip
        const QRect band(rc.left(), rc.top() + kVideoLabelH,
                         rc.width(), kVideoThumbH);
        const int slotCount = qMax(1, rc.width() / kThumbW);
        for (int s = 0; s < slotCount; ++s) {
            const QRect slot(band.left() + s * kThumbW, band.top(),
                             qMin(kThumbW, band.right() -
                                  (band.left() + s * kThumbW) + 1),
                             band.height());
            if (slot.width() <= 0) break;
            const QString key = QStringLiteral("t%1:c%2:s%3").
                    arg(trackIdx).arg(clipIdx).arg(s);
            const auto it = mPanel->mThumbCache.constFind(key);
            if (it != mPanel->mThumbCache.constEnd()) {
                p->drawImage(slot, it.value());
            } else {
                p->fillRect(slot, kColVideoThumbPh);
            }
        }
        requestClipAssets(trackIdx, clipIdx, rc);

        // teal waveform strip glued under the thumbnails
        const QRect wave(rc.left(), rc.top() + kVideoLabelH + kVideoThumbH,
                         rc.width(), kVideoWaveH);
        p->fillRect(wave, kColVideoWaveBg);
        const QString wkey = QStringLiteral("w%1:c%2:%3").
                arg(trackIdx).arg(clipIdx).arg(rc.width());
        const auto wit = mPanel->mWaveCache.constFind(wkey);
        if (wit != mPanel->mWaveCache.constEnd()) {
            drawWaveform(p, wave, wit.value(), kColVideoWave);
        }
    } else {
        // blue audio block: label strip + full-block mirrored waveform
        p->fillRect(rc, kColAudioBlock);
        const QRect label(rc.left(), rc.top(), rc.width(), kAudioLabelH);
        p->fillRect(label, kColAudioLabel);
        p->setFont(smallFont(p->font()));
        const QString text = p->fontMetrics().elidedText(
                    clip.name, Qt::ElideRight, qMax(0, rc.width() - 12));
        p->setPen(kColAudioText);
        p->drawText(QRect(label.left() + 6, label.top(),
                          label.width() - 12, label.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, text);
        const QRect wave(rc.left(), rc.top() + kAudioLabelH,
                         rc.width(), rc.height() - kAudioLabelH);
        const QString wkey = QStringLiteral("w%1:c%2:%3").
                arg(trackIdx).arg(clipIdx).arg(rc.width());
        const auto wit = mPanel->mWaveCache.constFind(wkey);
        if (wit != mPanel->mWaveCache.constEnd()) {
            drawWaveform(p, wave, wit.value(), kColAudioWave);
        }
        requestClipAssets(trackIdx, clipIdx, rc);
    }

    // selection: 1.5px white outline + corner handles (square, no
    // rounding - the whole design is sharp-cornered)
    if (clip.selected) {
        p->setPen(QPen(kColSelect, 1.5));
        p->setBrush(Qt::NoBrush);
        p->drawRect(rc);
        p->setPen(Qt::NoPen);
        p->setBrush(kColSelect);
        p->drawRect(rc.left() - kHandle / 2, rc.top() - kHandle / 2,
                    kHandle, kHandle);
        p->drawRect(rc.right() - kHandle / 2 + 1, rc.top() - kHandle / 2,
                    kHandle, kHandle);
        p->drawRect(rc.left() - kHandle / 2,
                    rc.bottom() - kHandle / 2 + 1, kHandle, kHandle);
        p->drawRect(rc.right() - kHandle / 2 + 1,
                    rc.bottom() - kHandle / 2 + 1, kHandle, kHandle);
    }
}

void EditTimelineView::drawWaveform(QPainter* const p, const QRect& rc,
                                    const QVector<qreal>& samples,
                                    const QColor& color)
{
    const qreal midY = rc.center().y();
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    const int barCount = (rc.width() + 1) / 2;
    for (int i = 0; i < barCount; ++i) {
        const int idx = i * samples.count() / qMax(1, barCount);
        if (idx >= samples.count()) break;
        const qreal h = samples.at(idx) * (rc.height() - 4);
        const int hi = qMax(1, qRound(h));
        p->drawRect(rc.left() + i * 2, qRound(midY - hi / 2.0), 2, hi);
    }
}

void EditTimelineView::drawRuler(QPainter* const p)
{
    const QRect ruler(0, 0, width(), kRulerH);
    p->fillRect(ruler, kColRuler);
    p->setPen(QPen(QColor(0x33, 0x33, 0x33), 1));
    p->drawLine(ruler.bottomLeft(), ruler.bottomRight());

    static const int ladder[] = {1, 2, 5, 10, 25, 50, 100,
                                 250, 500, 1000, 2500, 5000};
    int inc = ladder[11];
    for (int i = 0; i < 12; ++i) {
        if (ladder[i] * mPpf >= 80) { inc = ladder[i]; break; }
    }
    const int sub = qMax(1, inc / 5);
    const int fps = qMax(1, mPanel->mData.fps);
    p->setFont(smallFont(p->font()));
    p->setPen(kColRulerText);
    int f = qCeil(firstViewedFrame() / qreal(inc)) * inc;
    for (;; f += inc) {
        const qreal x = xAtFrame(f);
        if (x > width()) break;
        if (x >= -60) {
            p->setPen(kColRulerTick);
            p->drawLine(QPointF(x, kRulerH - 6), QPointF(x, kRulerH));
            p->setPen(kColRulerText);
            p->drawText(QRectF(x - 40, 2, 80, kRulerH - 9),
                        Qt::AlignVCenter | Qt::AlignHCenter,
                        timeCode(f, fps));
        }
        if (sub * mPpf >= 14) {
            p->setPen(kColRulerTick);
            for (int k = 1; k < 5; ++k) {
                const qreal xm = xAtFrame(f + k * sub);
                if (xm > width()) break;
                p->drawLine(QPointF(xm, kRulerH - 3), QPointF(xm, kRulerH));
            }
        }
    }
}

void EditTimelineView::drawPlayhead(QPainter* const p)
{
    const qreal x = xAtFrame(mPanel->mData.playheadFrame) + mPpf / 2;
    if (x < -10 || x > width() + 10) return;
    p->setPen(QPen(kColPlayhead, 2));
    p->drawLine(QPointF(x, 8), QPointF(x, height()));
    QPainterPath tri;
    tri.moveTo(x - 5, 0);
    tri.lineTo(x + 5, 0);
    tri.lineTo(x, 8);
    tri.closeSubpath();
    p->fillPath(tri, kColPlayhead);
}

void EditTimelineView::setFrameFromX(const int x)
{
    const int f = qBound(0, frameAtX(x), mHBar->maximum());
    mPanel->mData.playheadFrame = f;
    mPanel->mApi->setPlayhead(f);
    mPanel->updateTimeLabel();
    update();
}

void EditTimelineView::beginDragOp(const QPoint& pos)
{
    mPressFrame = frameAtX(pos.x());
    mLastApplied = 0;
    const auto& clip = mPanel->mData.tracks.at(mPressHit.track).
            clips.at(mPressHit.clip);
    mDragBackup = clip;
    mDragBackupTrack = mPressHit.track;
    if (mZone == Zone::EdgeMin) {
        mDrag = Drag::TrimMin;
    } else if (mZone == Zone::EdgeMax) {
        mDrag = Drag::TrimMax;
    } else {
        mDrag = Drag::Move;
    }
}

void EditTimelineView::beginRowSwitch()
{
    mGhostTrack = mPressHit.track;
    mDrag = Drag::RowSwitch;
}

int EditTimelineView::snapDelta(const int unsnapped,
                                const bool draggingEdge) const
{
    if (mPpf < 2.5) return unsnapped;
    const int tol = qMax(1, qRound(kSnapPx / mPpf));
    const auto& backup = mDragBackup;
    // edges of the moving clip in "delta space"
    int best = unsnapped;
    int bestDist = tol + 1;

    QList<int> cands;
    cands << 0 << mPanel->mData.playheadFrame;
    const auto& tracks = mPanel->mData.tracks;
    for (int t = 0; t < tracks.count(); ++t) {
        if (t == mPressHit.track) continue;
        for (const auto& c : tracks.at(t).clips) {
            cands << c.startFrame << c.startFrame + c.durationFrames;
        }
    }
    // same-track neighbours (excluding the dragged clip) still snap
    {
        const auto& clips = tracks.at(mPressHit.track).clips;
        for (int i = 0; i < clips.count(); ++i) {
            if (i == mPressHit.clip) continue;
            cands << clips.at(i).startFrame <<
                     clips.at(i).startFrame + clips.at(i).durationFrames;
        }
    }

    for (const int cand : cands) {
        if (draggingEdge) {
            const int edge = backup.startFrame + unsnapped;
            const int d = qAbs(edge - cand);
            if (d < bestDist) { bestDist = d; best = cand - backup.startFrame; }
        } else {
            const int minEdge = backup.startFrame + unsnapped;
            const int maxEdge = minEdge + backup.durationFrames;
            const int dMin = qAbs(minEdge - cand);
            if (dMin < bestDist) {
                bestDist = dMin;
                best = cand - backup.startFrame;
            }
            const int dMax = qAbs(maxEdge - cand);
            if (dMax < bestDist) {
                bestDist = dMax;
                best = cand - (backup.startFrame + backup.durationFrames);
            }
        }
    }
    return best;
}

void EditTimelineView::applyMoveDelta(const int total)
{
    auto& clip = mPanel->mData.tracks[mPressHit.track].clips[mPressHit.clip];
    clip.startFrame = qMax(0, mDragBackup.startFrame + total);
    update();
}

void EditTimelineView::applyTrimDelta(const int total, const bool minEdge)
{
    auto& clip = mPanel->mData.tracks[mPressHit.track].clips[mPressHit.clip];
    if (minEdge) {
        // dragging the head: move start right = cut content head
        int newStart = mDragBackup.startFrame + total;
        int newDur = mDragBackup.durationFrames - total;
        if (newDur < 1) { newDur = 1; newStart = mDragBackup.startFrame +
                    mDragBackup.durationFrames - 1; }
        if (newStart < 0) { newDur += newStart; newStart = 0; }
        clip.startFrame = qMax(0, newStart);
        clip.durationFrames = qMax(1, newDur);
    } else {
        int newDur = mDragBackup.durationFrames + total;
        if (newDur < 1) newDur = 1;
        clip.durationFrames = newDur;
    }
    update();
}

void EditTimelineView::commitDrag()
{
    const int t = mPressHit.track;
    const int c = mPressHit.clip;
    if (t < 0 || c < 0) return;
    auto& tracks = mPanel->mData.tracks;
    if (t >= tracks.count() || c >= tracks.at(t).clips.count()) return;
    const auto& clip = tracks.at(t).clips.at(c);

    if (mDrag == Drag::Move) {
        mPanel->mApi->moveClip(t, c, clip.startFrame);
    } else if (mDrag == Drag::TrimMin || mDrag == Drag::TrimMax) {
        mPanel->mApi->trimClip(t, c, clip.startFrame, clip.durationFrames);
    } else if (mDrag == Drag::RowSwitch && mGhostTrack >= 0 &&
               mGhostTrack != t) {
        const int fromTrack = t;
        const int fromClip = c;
        const auto moving = clip;
        // local move first (clip kind must fit the target track)
        if (clipFitsTrack(moving, tracks.at(mGhostTrack))) {
            tracks[mGhostTrack].clips << moving;
            tracks[fromTrack].clips.removeAt(fromClip);
            auto& dst = tracks[mGhostTrack].clips;
            std::sort(dst.begin(), dst.end(),
                      [](const EditClip& a, const EditClip& b)
                      { return a.startFrame < b.startFrame; });
            mPanel->mApi->moveClipToTrack(fromTrack, fromClip, mGhostTrack);
        }
    }
    // keep clips ordered after move/trim
    auto& src = tracks[t].clips;
    std::sort(src.begin(), src.end(),
              [](const EditClip& a, const EditClip& b)
              { return a.startFrame < b.startFrame; });
    updateScrollRanges();
    update();
}

void EditTimelineView::rollbackDrag()
{
    if (mDragBackupTrack < 0) return;
    auto& tracks = mPanel->mData.tracks;
    if (mDragBackupTrack >= tracks.count()) return;
    auto& clips = tracks[mDragBackupTrack].clips;
    for (int i = 0; i < clips.count(); ++i) {
        if (i == mPressHit.clip ||
            (mPressHit.clip < 0 && clips.at(i).name == mDragBackup.name)) {
            clips[i] = mDragBackup;
            break;
        }
    }
    update();
}

void EditTimelineView::mousePressEvent(QMouseEvent* const e)
{
    const QPoint pos = e->pos();
    if (e->button() == Qt::MiddleButton) {
        mDrag = Drag::Pan;
        mLastPanPos = pos;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (pos.y() < kRulerH) {
        mDrag = Drag::Playhead;
        setFrameFromX(pos.x());
        return;
    }
    // account for the vertical scroll offset in hit testing
    const QPoint contentPos(pos.x(), pos.y() + mVBar->value());
    const auto hit = hitTest(contentPos);
    if (hit.track < 0) {
        mDrag = Drag::Pan;
        mLastPanPos = pos;
        // clicking empty space clears the selection
        for (auto& t : mPanel->mData.tracks) {
            for (auto& c : t.clips) c.selected = false;
        }
        update();
        return;
    }
    mPressHit = hit;
    mZone = hit.zone;
    mPressPos = contentPos;
    mDrag = Drag::Pending;
    mGhostTrack = -1;
    for (auto& t : mPanel->mData.tracks) {
        for (auto& c : t.clips) c.selected = false;
    }
    mPanel->mData.tracks[hit.track].clips[hit.clip].selected = true;
    update();
}

void EditTimelineView::mouseMoveEvent(QMouseEvent* const e)
{
    const QPoint pos = e->pos();
    if (mDrag == Drag::None) {
        const QPoint contentPos(pos.x(), pos.y() + mVBar->value());
        updateHoverCursor(contentPos);
        return;
    }
    const QPoint contentPos(pos.x(), pos.y() + mVBar->value());
    switch (mDrag) {
    case Drag::Pending: {
        const int dx = contentPos.x() - mPressPos.x();
        const int dy = contentPos.y() - mPressPos.y();
        if (qAbs(dy) >= kVideoTrackH * 0.55) beginRowSwitch();
        else if (qAbs(dx) >= kDragThreshold) beginDragOp(contentPos);
        break;
    }
    case Drag::Playhead:
        setFrameFromX(pos.x());
        break;
    case Drag::Pan: {
        const int dx = pos.x() - mLastPanPos.x();
        const int dy = pos.y() - mLastPanPos.y();
        mHBar->setValue(mHBar->value() - qRound(dx / mPpf));
        mVBar->setValue(mVBar->value() - dy);
        mLastPanPos = pos;
        break;
    }
    case Drag::Move:
        applyMoveDelta(snapDelta(
                           frameAtX(contentPos.x()) - mPressFrame, false));
        break;
    case Drag::TrimMin:
        applyTrimDelta(snapDelta(
                           frameAtX(contentPos.x()) - mPressFrame, true), true);
        break;
    case Drag::TrimMax:
        applyTrimDelta(frameAtX(contentPos.x()) - mPressFrame, false);
        break;
    case Drag::RowSwitch: {
        const auto& tracks = mPanel->mData.tracks;
        int best = mPressHit.track;
        int bestDist = INT_MAX;
        for (int t = 0; t < tracks.count(); ++t) {
            const int top = trackTop(t);
            const int d = qAbs(contentPos.y() - (top + trackHeight(t) / 2));
            if (d < bestDist) { bestDist = d; best = t; }
        }
        mGhostTrack = best;
        mPressPos = contentPos;
        update();
        break;
    }
    default:
        break;
    }
}

void EditTimelineView::mouseReleaseEvent(QMouseEvent* const e)
{
    Q_UNUSED(e)
    if (mDrag == Drag::Move || mDrag == Drag::TrimMin ||
        mDrag == Drag::TrimMax || mDrag == Drag::RowSwitch) {
        commitDrag();
    }
    mDrag = Drag::None;
    mGhostTrack = -1;
    updateScrollRanges();
    update();
}

void EditTimelineView::mouseDoubleClickEvent(QMouseEvent* const e)
{
    if (e->button() != Qt::LeftButton) return;
    const QPoint contentPos(e->pos().x(),
                            e->pos().y() + mVBar->value());
    const auto hit = hitTest(contentPos);
    if (hit.track < 0) return;
    mPanel->mApi->openClip(hit.track, hit.clip);
}

void EditTimelineView::wheelEvent(QWheelEvent* const e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const qreal ax = e->position().x();
        const int anchor = frameAtX(qRound(ax));
        const double factor = e->angleDelta().y() > 0 ? 1.25 : 0.8;
        mPpf = qBound(1.0, mPpf * factor, 60.0);
        updateScrollRanges();
        const int newFirst = qRound(anchor - ax / mPpf);
        mHBar->setValue(qBound(mHBar->minimum(), newFirst,
                               mHBar->maximum()));
        update();
    } else if (e->modifiers() & Qt::ShiftModifier) {
        mVBar->setValue(mVBar->value() -
                        (e->angleDelta().y() > 0 ? 60 : -60));
    } else {
        const int step = qMax(1, mHBar->pageStep() / 4);
        mHBar->setValue(mHBar->value() +
                        (e->angleDelta().y() > 0 ? -step : step));
    }
}

void EditTimelineView::resizeEvent(QResizeEvent* const e)
{
    QWidget::resizeEvent(e);
    mHBar->setGeometry(0, height() - kScrollW,
                       qMax(0, width() - kScrollW), kScrollW);
    mVBar->setGeometry(width() - kScrollW, 0,
                       kScrollW, qMax(0, height() - kScrollW));
    updateScrollRanges();
}

void EditTimelineView::leaveEvent(QEvent* const e)
{
    QWidget::leaveEvent(e);
    if (mDrag == Drag::None) unsetCursor();
}

void EditTimelineView::keyPressEvent(QKeyEvent* const e)
{
    if (e->key() == Qt::Key_Escape &&
        (mDrag == Drag::Move || mDrag == Drag::TrimMin ||
         mDrag == Drag::TrimMax || mDrag == Drag::RowSwitch)) {
        rollbackDrag();
        mDrag = Drag::None;
        mGhostTrack = -1;
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void EditTimelineView::updateHoverCursor(const QPoint& pos)
{
    if (pos.y() < kRulerH) { unsetCursor(); return; }
    const auto hit = hitTest(pos);
    if (hit.track < 0) { unsetCursor(); return; }
    if (hit.zone == Zone::EdgeMin || hit.zone == Zone::EdgeMax) {
        setCursor(Qt::SplitHCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

// ------------------------------ panel ------------------------------

EditTimelinePanel::EditTimelinePanel(QWidget* const parent)
    : QWidget(parent)
{
    setMinimumSize(360, 260);

    const auto topBar = new QWidget();
    topBar->setAutoFillBackground(true);
    const auto topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(6, 3, 6, 3);
    topLay->setSpacing(4);

    mSceneCombo = new QComboBox();
    mSceneCombo->setToolTip(tr("剪辑合成场景"));
    mNewSceneBtn = new QPushButton(tr("新建合成"));
    mAddClipBtn = new QPushButton(tr("添加素材"));
    mTimeLabel = new QLabel();
    mTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    topLay->addWidget(mSceneCombo, 1);
    topLay->addWidget(mNewSceneBtn);
    topLay->addWidget(mAddClipBtn);
    topLay->addWidget(mTimeLabel);

    mView = new EditTimelineView(this);

    const auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(topBar);
    lay->addWidget(mView, 1);

    mApi = std::make_unique<EditTimelineApiStub>();

    connect(mNewSceneBtn, &QPushButton::clicked, this, [this]() {
        int idx = -1;
        mApi->createComposition(idx);
        reload();
    });
    connect(mAddClipBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        QStringList names;
        mApi->sceneNames(names);
        if (names.isEmpty()) {
            const auto a = menu.addAction(tr("没有可添加的场景"));
            a->setEnabled(false);
        } else {
            for (int i = 0; i < names.count(); ++i) {
                menu.addAction(names.at(i), this, [this, i]() {
                    mApi->addSceneClip(i);
                    reload();
                });
            }
        }
        menu.exec(mAddClipBtn->mapToGlobal(
                      QPoint(0, mAddClipBtn->height())));
    });

    setListeningEnabled(true);
}

void EditTimelinePanel::setApi(EditTimelineApi* const api)
{
    if (!api) return;
    mApi.reset(api);
    reload();
}

void EditTimelinePanel::setListeningEnabled(const bool enabled)
{
    mListening = enabled;
    if (enabled) reload();
}

void EditTimelinePanel::reload()
{
    mThumbCache.clear();
    mWaveCache.clear();
    mApi->load(mData);
    rebuildCompositionCombo();
    updateTopBar();
    mView->dataChanged();
    updateTimeLabel();
}

void EditTimelinePanel::rebuildCompositionCombo()
{
    mComboGuard = true;
    mSceneCombo->clear();
    QStringList names;
    mApi->compositionNames(names);
    mSceneCombo->addItems(names);
    if (mSceneCombo->count() > 0) mSceneCombo->setCurrentIndex(0);
    mComboGuard = false;
}

void EditTimelinePanel::updateTopBar()
{
    mAddClipBtn->setEnabled(!mData.tracks.isEmpty());
}

void EditTimelinePanel::updateTimeLabel()
{
    mTimeLabel->setText(timeCode(mData.playheadFrame,
                                 qMax(1, mData.fps)));
}
