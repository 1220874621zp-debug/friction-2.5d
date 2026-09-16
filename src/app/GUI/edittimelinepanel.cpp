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
#include <QTimer>

#include "Private/document.h"
#include "themesupport.h"
#include "appsupport.h"
#include "canvas.h"
#include "Boxes/containerbox.h"
#include "Boxes/boundingbox.h"
#include "Boxes/internallinkcanvas.h"
#include "Boxes/boxrenderdata.h"
#include "Animators/eboxorsound.h"
#include "Timeline/durationrectangle.h"
#include "mainwindow.h"
#include "layouthandler.h"

namespace {
const int kRulerH = 24;        // frame ruler height
const int kRowH = 64;          // one track row per top-level box
const int kLabelH = 16;        // clip name strip on top of the block
const int kClipVMargin = 5;    // vertical inset of the clip inside its row
const int kThumbW = 64;        // filmstrip thumbnail slot width
const int kEdgeZone = 7;       // trim handle hit zone in pixels
const int kSnapPx = 6;         // snap threshold in pixels
const int kDragThreshold = 5;  // px before a pending press becomes a drag
const int kScrollW = 12;       // manual scrollbar thickness
const int kMaxThumbsPerClip = 48;
const int kThumbCacheMax = 128;
const int kThumbInFlightMax = 2;

const QColor kColBg(0x26, 0x28, 0x2B);
const QColor kColRowA(0x2B, 0x3A, 0x3E);
const QColor kColRowB(0x30, 0x40, 0x44);
const QColor kColRuler(0x1F, 0x21, 0x24);
const QColor kColLabelScene(0x3E, 0x7C, 0x8A);
const QColor kColLabelPlain(0x4A, 0x4F, 0x54);
const QColor kColClipBody(0x2B, 0x37, 0x3D);
const QColor kColThumbPh(0x22, 0x30, 0x33);
const QColor kColGhost(0x3A, 0x3F, 0x44);
const QColor kColText(0xE8, 0xEC, 0xEE);
const QColor kColHint(0x8A, 0x92, 0x98);

QFont smallFont(const QFont& base)
{
    QFont f = base;
    if (f.pointSize() > 0) f.setPointSize(qMax(7, f.pointSize() - 2));
    return f;
}
}

// ------------------------------ EditTimelineView ------------------------------

EditTimelineView::EditTimelineView(EditTimelinePanel* const panel)
    : QWidget(panel)
    , mPanel(panel)
{
    setMouseTracking(true);
    setMinimumHeight(140);
    setFocusPolicy(Qt::ClickFocus);
    mHBar = new QScrollBar(Qt::Horizontal, this);
    mVBar = new QScrollBar(Qt::Vertical, this);
    mHBar->hide();
    mVBar->hide();
    connect(mHBar, &QScrollBar::valueChanged, this, [this](int) {
        update();
        scheduleThumbRequest();
    });
    connect(mVBar, &QScrollBar::valueChanged, this, [this](int) {
        update();
    });
}

Canvas* EditTimelineView::scene() const
{
    return mPanel->mTargetScene.data();
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

int EditTimelineView::firstVisibleRow() const
{
    return mVBar->value();
}

int EditTimelineView::rowOfClip(eBoxOrSound* const clip) const
{
    const int n = mPanel->mClips.count();
    for (int i = 0; i < n; ++i) {
        if (mPanel->mClips.at(i).data() == clip) return i;
    }
    return -1;
}

QRect EditTimelineView::clipRect(eBoxOrSound* const clip) const
{
    int f0 = 0;
    int f1 = 0;
    const auto rect = clip->getDurationRectangle();
    if (rect) {
        f0 = rect->getMinAbsFrame();
        f1 = rect->getMaxAbsFrame();
    } else if (scene()) {
        const auto sfr = scene()->getFrameRange();
        f0 = sfr.fMin;
        f1 = sfr.fMax;
    }
    const qreal x = xAtFrame(f0);
    const qreal w = qMax(4.0, (f1 - f0 + 1) * mPpf - 1.0);
    return QRect(qRound(x), 0, qRound(w), kRowH - 2 * kClipVMargin);
}

EditTimelineView::Hit EditTimelineView::hitTest(const QPoint& pos) const
{
    Hit hit;
    if (pos.y() < kRulerH) return hit;
    const int rows = mPanel->mClips.count();
    const int row = firstVisibleRow() + (pos.y() - kRulerH) / kRowH;
    if (row < 0 || row >= rows) return hit;
    auto* clip = mPanel->mClips.at(row).data();
    if (!clip) return hit;
    const QRect rc = clipRect(clip);
    if (pos.x() < rc.left() - 2 || pos.x() > rc.right() + 2) return hit;
    hit.box = clip;
    if (pos.x() - rc.left() <= kEdgeZone) hit.zone = Zone::EdgeMin;
    else if (rc.right() - pos.x() <= kEdgeZone) hit.zone = Zone::EdgeMax;
    else hit.zone = Zone::Body;
    return hit;
}

void EditTimelineView::sceneChanged()
{
    updateScrollRanges();
    update();
    scheduleThumbRequest();
}

void EditTimelineView::updateScrollRanges()
{
    const auto s = scene();
    if (!s) {
        mHBar->setRange(0, 0);
        mVBar->setRange(0, 0);
        mHBar->hide();
        mVBar->hide();
        return;
    }
    const auto fr = s->getFrameRange();
    const int hMin = fr.fMin - 50;
    const int hMax = qMax(fr.fMax + 250,
                          hMin + qRound(width() / mPpf));
    mHBar->setRange(hMin, hMax);
    mHBar->setPageStep(qMax(1, qRound(width() / mPpf)));
    const int rows = mPanel->mClips.count();
    const int visRows = qMax(1, (height() - kRulerH) / kRowH);
    mVBar->setRange(0, qMax(0, rows - visRows));
    mVBar->setPageStep(visRows);
    mHBar->setVisible(mHBar->maximum() > mHBar->minimum());
    mVBar->setVisible(mVBar->maximum() > mVBar->minimum());
}

void EditTimelineView::scheduleThumbRequest()
{
    if (!mPanel->mListening || mPanel->mThumbSuppress) return;
    const auto s = scene();
    if (!s) return;
    const int rows = mPanel->mClips.count();
    for (int row = 0; row < rows; ++row) {
        const int y = kRulerH + (row - firstVisibleRow()) * kRowH;
        if (y + kRowH < kRulerH) continue;
        if (y >= height()) break;
        auto* clip = mPanel->mClips.at(row).data();
        if (!clip) continue;
        const auto rect = clip->getDurationRectangle();
        if (!rect) continue;
        auto* src = mPanel->resolveSourceScene(clip);
        if (!src) continue;
        const QRect rc = clipRect(clip);
        if (rc.width() < kThumbW) continue;
        const int shift = rect->getRelShift();
        const auto sfr = src->getFrameRange();
        const int slotCount = qMin(kMaxThumbsPerClip, rc.width() / kThumbW);
        for (int k = 0; k < slotCount; ++k) {
            const int absF = frameAtX(rc.x() + k * kThumbW + kThumbW / 2);
            const int srcF = qBound(sfr.fMin, absF - shift, sfr.fMax);
            mPanel->enqueueThumb(src, srcF);
        }
    }
}

void EditTimelineView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kColBg);
    const auto s = scene();

    if (!s) {
        p.setPen(kColHint);
        p.drawText(rect().adjusted(12, 12, -12, -12),
                   Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                   tr("从顶部选择一个场景作为剪辑合成，\n或点击「新建合成」。"));
        p.fillRect(QRect(0, 0, width(), kRulerH), kColRuler);
        return;
    }

    const int first = firstVisibleRow();
    const int rows = mPanel->mClips.count();

    // row backgrounds
    for (int r = first; r < rows; ++r) {
        const int y = kRulerH + (r - first) * kRowH;
        if (y >= height()) break;
        p.fillRect(QRect(0, y, width(), kRowH),
                   r % 2 == 0 ? kColRowA : kColRowB);
    }

    if (rows == 0) {
        p.setPen(kColHint);
        p.drawText(QRect(0, kRulerH, width(), height() - kRulerH),
                   Qt::AlignCenter | Qt::TextWordWrap,
                   tr("点击「添加素材」把场景作为素材块加进来"));
    }

    // clips
    for (int r = 0; r < rows; ++r) {
        const int y = kRulerH + (r - first) * kRowH;
        if (y + kRowH < kRulerH) continue;
        if (y >= height()) break;
        auto* clip = mPanel->mClips.at(r).data();
        if (!clip) continue;
        drawClip(&p, clip, r);
    }

    // row-switch ghost + insertion band
    if (mDrag == Drag::RowSwitch && mTargetRow >= 0 && mPressBox) {
        const int y = kRulerH + (mTargetRow - first) * kRowH;
        if (y > -kRowH && y < height()) {
            const auto hl = ThemeSupport::getThemeHighlightColor(40);
            p.fillRect(QRect(0, y, width(), kRowH), hl);
            p.setPen(QPen(ThemeSupport::getThemeHighlightColor(), 1));
            p.drawLine(0, y, width(), y);
            p.drawLine(0, y + kRowH, width(), y + kRowH);
            p.setOpacity(0.6);
            drawClip(&p, mPressBox.data(), mTargetRow);
            p.setOpacity(1.0);
        }
    }

    drawRuler(&p);
    drawPlayhead(&p);
}

void EditTimelineView::drawClip(QPainter* const p,
                                eBoxOrSound* const clip,
                                const int row)
{
    const int first = firstVisibleRow();
    const int y = kRulerH + (row - first) * kRowH;
    QRect rc = clipRect(clip);
    rc.setY(y + kClipVMargin);
    if (rc.right() < 0 || rc.left() > width()) return;

    const bool ghost = !clip->hasDurationRectangle();
    auto* src = mPanel->resolveSourceScene(clip);

    p->save();
    if (!clip->isVisible()) p->setOpacity(0.45);

    QPainterPath path;
    path.addRoundedRect(QRectF(rc), 3, 3);
    p->fillPath(path, ghost ? kColGhost :
                             (src ? kColLabelScene : kColLabelPlain));

    const QRectF body(rc.x() + 1, rc.y() + kLabelH,
                      rc.width() - 2, rc.height() - kLabelH - 1);
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(body, 2.5, 2.5);
    p->fillPath(bodyPath, kColClipBody);

    // filmstrip: one thumbnail per kThumbW px slot, the source frame is
    // the block frame minus the duration rect shift (identity remap)
    if (src && !ghost) {
        const auto rect = clip->getDurationRectangle();
        const int shift = rect->getRelShift();
        const auto sfr = src->getFrameRange();
        p->setClipPath(bodyPath);
        for (int x = rc.x(); x < rc.right(); x += kThumbW) {
            const int absF = frameAtX(x + kThumbW / 2);
            const int srcF = qBound(sfr.fMin, absF - shift, sfr.fMax);
            const auto it = mPanel->mThumbCache.constFind(
                        mPanel->thumbKey(src, srcF));
            if (it != mPanel->mThumbCache.constEnd()) {
                p->drawImage(QRectF(x, body.top(),
                                    kThumbW, body.height()), it.value());
            } else {
                p->fillRect(QRectF(x, body.top(),
                                   kThumbW, body.height()), kColThumbPh);
            }
        }
    }
    p->restore();

    // name strip
    const QString name = src ? src->prp_getName() : clip->prp_getName();
    p->setFont(smallFont(p->font()));
    const QString text = p->fontMetrics().elidedText(
                name, Qt::ElideRight, qMax(0, rc.width() - 10));
    p->setPen(kColText);
    p->drawText(QRect(rc.x() + 5, rc.y(), rc.width() - 10, kLabelH),
                Qt::AlignVCenter | Qt::AlignLeft, text);

    if (clip->isSelected()) {
        p->setPen(QPen(ThemeSupport::getThemeHighlightColor(), 2));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(rc).adjusted(1, 1, -1, -1), 3, 3);
    }
}

void EditTimelineView::drawRuler(QPainter* const p)
{
    p->fillRect(QRect(0, 0, width(), kRulerH), kColRuler);
    const auto s = scene();
    if (!s) return;

    static const int ladder[] = {1, 2, 5, 10, 25, 50, 100,
                                 250, 500, 1000, 2500, 5000};
    int inc = ladder[11];
    for (int i = 0; i < 12; ++i) {
        if (ladder[i] * mPpf >= 70) { inc = ladder[i]; break; }
    }
    const int sub = qMax(1, inc / 5);

    const qreal fps = s->getFps();
    p->setFont(smallFont(p->font()));
    p->setPen(QColor(0x9A, 0xA2, 0xA8));
    const int first = firstViewedFrame();
    int f = qCeil(first / qreal(inc)) * inc;
    for (;; f += inc) {
        const qreal x = xAtFrame(f);
        if (x > width()) break;
        if (x < -60) continue;
        p->drawLine(QPointF(x, kRulerH - 7), QPointF(x, kRulerH));
        p->drawText(QRectF(x - 40, 2, 80, kRulerH - 9),
                    Qt::AlignVCenter | Qt::AlignHCenter,
                    AppSupport::getTimeCodeFromFrame(f, float(fps)));
        if (sub * mPpf >= 12) {
            for (int k = 1; k < 5; ++k) {
                const qreal xm = xAtFrame(f + k * sub);
                if (xm > width()) break;
                p->drawLine(QPointF(xm, kRulerH - 4), QPointF(xm, kRulerH));
            }
        }
    }
}

void EditTimelineView::drawPlayhead(QPainter* const p)
{
    const auto s = scene();
    if (!s) return;
    const qreal x = xAtFrame(s->getCurrentFrame()) + mPpf / 2;
    if (x < -10 || x > width() + 10) return;
    const auto col = ThemeSupport::getThemeHighlightColor();
    p->setPen(QPen(col, 1));
    p->drawLine(QPointF(x, kRulerH - 6), QPointF(x, height()));
    QPainterPath tri;
    tri.moveTo(x - 6, 0);
    tri.lineTo(x + 6, 0);
    tri.lineTo(x, 9);
    tri.closeSubpath();
    p->fillPath(tri, col);
}

void EditTimelineView::setFrameFromX(const int x)
{
    const auto s = scene();
    if (!s) return;
    const int f = qBound(mHBar->minimum(), frameAtX(x), mHBar->maximum());
    s->anim_setAbsFrame(f);
}

void EditTimelineView::beginHorizontal(const QPoint& pos)
{
    auto* box = mPressBox.data();
    if (!box) { mDrag = Drag::None; return; }
    if (!box->hasDurationRectangle()) box->createDurationRectangle();
    const auto rect = box->getDurationRectangle();
    if (!rect) { mDrag = Drag::None; return; }
    mPressFrame = frameAtX(pos.x());
    mLastApplied = 0;
    mOrigMin = rect->getMinAbsFrame();
    mOrigMax = rect->getMaxAbsFrame();
    if (mZone == Zone::EdgeMin) {
        box->startMinFramePosTransform();
        mDrag = Drag::TrimMin;
    } else if (mZone == Zone::EdgeMax) {
        box->startMaxFramePosTransform();
        mDrag = Drag::TrimMax;
    } else {
        box->startDurationRectPosTransform();
        mDrag = Drag::Move;
    }
    mPanel->mThumbSuppress = true;
}

void EditTimelineView::beginRowSwitch()
{
    mStartRow = rowOfClip(mPressBox.data());
    mTargetRow = mStartRow;
    mDrag = Drag::RowSwitch;
    mPanel->mThumbSuppress = true;
}

QList<int> EditTimelineView::snapCandidates() const
{
    QList<int> out;
    out << 0;
    const auto s = scene();
    if (s) out << s->getCurrentFrame();
    const int n = mPanel->mClips.count();
    for (int i = 0; i < n; ++i) {
        auto* c = mPanel->mClips.at(i).data();
        if (!c || c == mPressBox.data()) continue;
        const auto r = c->getDurationRectangle();
        if (!r) continue;
        out << r->getMinAbsFrame() << r->getMaxAbsFrame() + 1;
    }
    return out;
}

int EditTimelineView::snappedMoveDelta(const int total) const
{
    if (mPpf < 2.5) return total;
    const int tol = qMax(1, qRound(kSnapPx / mPpf));
    const int newMin = mOrigMin + total;
    const int newMax = mOrigMax + total;
    int best = total;
    int bestDist = tol + 1;
    const auto cands = snapCandidates();
    for (const int c : cands) {
        const int dMin = qAbs(newMin - c);
        if (dMin < bestDist) { bestDist = dMin; best = c - mOrigMin; }
        const int dMax = qAbs(newMax - c);
        if (dMax < bestDist) { bestDist = dMax; best = c - mOrigMax; }
    }
    return best;
}

int EditTimelineView::snappedEdgeDelta(const int total,
                                       const bool minEdge) const
{
    if (mPpf < 2.5) return total;
    const int tol = qMax(1, qRound(kSnapPx / mPpf));
    const int orig = minEdge ? mOrigMin : mOrigMax;
    const int edge = orig + total;
    int best = total;
    int bestDist = tol + 1;
    const auto cands = snapCandidates();
    for (const int c : cands) {
        const int d = qAbs(edge - c);
        if (d < bestDist) { bestDist = d; best = c - orig; }
    }
    return best;
}

void EditTimelineView::applyMoveDelta(const int total)
{
    auto* box = mPressBox.data();
    if (!box) return;
    const int step = total - mLastApplied;
    if (step == 0) return;
    box->moveDurationRect(step);
    mLastApplied = total;
    update();
}

void EditTimelineView::applyMinDelta(const int total)
{
    auto* box = mPressBox.data();
    if (!box) return;
    const int step = total - mLastApplied;
    if (step == 0) return;
    box->moveMinFrame(step);
    mLastApplied = total;
    update();
}

void EditTimelineView::applyMaxDelta(const int total)
{
    auto* box = mPressBox.data();
    if (!box) return;
    const int step = total - mLastApplied;
    if (step == 0) return;
    box->moveMaxFrame(step);
    mLastApplied = total;
    update();
}

void EditTimelineView::mousePressEvent(QMouseEvent* const e)
{
    mPanel->scheduleAutoSwitch();
    const QPoint pos = e->pos();
    if (e->button() == Qt::MiddleButton) {
        mDrag = Drag::Pan;
        mLastPanPos = pos;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const auto s = scene();
    if (!s) return;
    if (pos.y() < kRulerH) {
        mDrag = Drag::Playhead;
        setFrameFromX(pos.x());
        return;
    }
    const auto hit = hitTest(pos);
    if (!hit.box) {
        mDrag = Drag::Pan;
        mLastPanPos = pos;
        s->clearBoxesSelection();
        return;
    }
    mPressBox = hit.box;
    mZone = hit.zone;
    mPressPos = pos;
    mDrag = Drag::Pending;
    mTargetRow = -1;
    const auto bb = dynamic_cast<BoundingBox*>(hit.box);
    if (bb) {
        s->clearBoxesSelection();
        s->addBoxToSelection(bb);
    }
}

void EditTimelineView::mouseMoveEvent(QMouseEvent* const e)
{
    const QPoint pos = e->pos();
    if (mDrag == Drag::None) {
        updateHoverCursor(pos);
        return;
    }
    switch (mDrag) {
    case Drag::Pending: {
        const int dx = pos.x() - mPressPos.x();
        const int dy = pos.y() - mPressPos.y();
        if (qAbs(dy) >= kRowH * 0.55) beginRowSwitch();
        else if (qAbs(dx) >= kDragThreshold) beginHorizontal(pos);
        break;
    }
    case Drag::Playhead:
        setFrameFromX(pos.x());
        break;
    case Drag::Pan: {
        const int dx = pos.x() - mLastPanPos.x();
        const int dy = pos.y() - mLastPanPos.y();
        mHBar->setValue(mHBar->value() - qRound(dx / mPpf));
        mVBar->setValue(mVBar->value() - qRound(double(dy) / kRowH));
        mLastPanPos = pos;
        break;
    }
    case Drag::Move:
        applyMoveDelta(snappedMoveDelta(frameAtX(pos.x()) - mPressFrame));
        break;
    case Drag::TrimMin:
        applyMinDelta(snappedEdgeDelta(
                          frameAtX(pos.x()) - mPressFrame, true));
        break;
    case Drag::TrimMax:
        applyMaxDelta(snappedEdgeDelta(
                          frameAtX(pos.x()) - mPressFrame, false));
        break;
    case Drag::RowSwitch: {
        const int rows = mPanel->mClips.count();
        int row = firstVisibleRow() + (pos.y() - kRulerH) / kRowH;
        if (pos.y() < kRulerH) row = firstVisibleRow();
        mTargetRow = qBound(0, row, qMax(0, rows - 1));
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
    auto* box = mPressBox.data();
    if (mDrag == Drag::Move && box) {
        box->prp_pushUndoRedoName(tr("移动素材块"));
        box->finishDurationRectPosTransform();
        Document::sInstance->actionFinished();
    } else if (mDrag == Drag::TrimMin && box) {
        box->prp_pushUndoRedoName(tr("修剪入点"));
        box->finishMinFramePosTransform();
        Document::sInstance->actionFinished();
    } else if (mDrag == Drag::TrimMax && box) {
        box->prp_pushUndoRedoName(tr("修剪出点"));
        box->finishMaxFramePosTransform();
        Document::sInstance->actionFinished();
    } else if (mDrag == Drag::RowSwitch && box &&
               mTargetRow >= 0 && mTargetRow != mStartRow) {
        box->prp_pushUndoRedoName(tr("调整轨道"));
        box->moveTo(mTargetRow);
        Document::sInstance->actionFinished();
        mPanel->refreshClips();
    }
    mDrag = Drag::None;
    mPressBox = nullptr;
    mTargetRow = -1;
    mPanel->mThumbSuppress = false;
    updateScrollRanges();
    scheduleThumbRequest();
    update();
}

void EditTimelineView::mouseDoubleClickEvent(QMouseEvent* const e)
{
    if (e->button() != Qt::LeftButton) return;
    const auto s = scene();
    if (!s) return;
    const auto hit = hitTest(e->pos());
    if (!hit.box) return;
    // cancel the deferred auto-switch: this click navigates instead
    mPanel->cancelAutoSwitch();
    auto* src = mPanel->resolveSourceScene(hit.box);
    if (src) mPanel->requestSwitchScene(src);
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
        scheduleThumbRequest();
    } else if (e->modifiers() & Qt::ShiftModifier) {
        const int step = qMax(1, mVBar->pageStep() / 2);
        mVBar->setValue(mVBar->value() +
                        (e->angleDelta().y() > 0 ? -step : step));
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
    if (e->key() == Qt::Key_Escape) {
        auto* box = mPressBox.data();
        if (mDrag == Drag::Move && box) box->cancelDurationRectPosTransform();
        else if (mDrag == Drag::TrimMin && box) box->cancelMinFramePosTransform();
        else if (mDrag == Drag::TrimMax && box) box->cancelMaxFramePosTransform();
        else { QWidget::keyPressEvent(e); return; }
        mDrag = Drag::None;
        mPressBox = nullptr;
        mPanel->mThumbSuppress = false;
        Document::sInstance->actionFinished();
        updateScrollRanges();
        scheduleThumbRequest();
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void EditTimelineView::updateHoverCursor(const QPoint& pos)
{
    if (pos.y() < kRulerH) { unsetCursor(); return; }
    const auto hit = hitTest(pos);
    if (!hit.box) { unsetCursor(); return; }
    if (hit.zone == Zone::EdgeMin || hit.zone == Zone::EdgeMax) {
        setCursor(Qt::SplitHCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

// ------------------------------ EditTimelinePanel ------------------------------

EditTimelinePanel::EditTimelinePanel(Document& doc, QWidget* const parent)
    : QWidget(parent)
    , mDocument(doc)
{
    setMinimumSize(360, 240);

    const auto topBar = new QWidget();
    const auto topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(6, 4, 6, 4);
    topLay->setSpacing(4);

    mSceneCombo = new QComboBox();
    mSceneCombo->setToolTip(
                tr("剪辑合成场景：素材块存放在这个普通场景里，"
                   "渲染该场景即输出剪辑成片"));
    mNewSceneBtn = new QPushButton(tr("新建合成"));
    mNewSceneBtn->setToolTip(tr("新建一个场景并作为剪辑合成"));
    mAddClipBtn = new QPushButton(tr("添加素材"));
    mAddClipBtn->setToolTip(tr("把项目里的一个场景作为素材块加进来"));
    mReturnBtn = new QPushButton(tr("返回剪辑"));
    mReturnBtn->setToolTip(tr("画布切回剪辑合成场景"));
    mTimeLabel = new QLabel();
    mTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    topLay->addWidget(mSceneCombo, 1);
    topLay->addWidget(mNewSceneBtn);
    topLay->addWidget(mAddClipBtn);
    topLay->addWidget(mReturnBtn);
    topLay->addWidget(mTimeLabel);

    mView = new EditTimelineView(this);

    const auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(topBar);
    lay->addWidget(mView, 1);

    mRefreshDebounce = new QTimer(this);
    mRefreshDebounce->setSingleShot(true);
    mRefreshDebounce->setInterval(120);
    connect(mRefreshDebounce, &QTimer::timeout,
            this, &EditTimelinePanel::refreshClips);

    mThumbDirtyDebounce = new QTimer(this);
    mThumbDirtyDebounce->setSingleShot(true);
    mThumbDirtyDebounce->setInterval(800);
    connect(mThumbDirtyDebounce, &QTimer::timeout, this, [this]() {
        for (Canvas* s : mDirtySources) clearSceneThumbs(s);
        mDirtySources.clear();
        mView->update();
        mView->scheduleThumbRequest();
    });

    mAutoSwitchTimer = new QTimer(this);
    mAutoSwitchTimer->setSingleShot(true);
    mAutoSwitchTimer->setInterval(180);
    connect(mAutoSwitchTimer, &QTimer::timeout, this, [this]() {
        if (mTargetScene) requestSwitchScene(mTargetScene);
    });

    connect(mNewSceneBtn, &QPushButton::clicked,
            this, &EditTimelinePanel::createCompositionScene);
    connect(mAddClipBtn, &QPushButton::clicked,
            this, &EditTimelinePanel::showAddClipMenu);
    connect(mReturnBtn, &QPushButton::clicked, this, [this]() {
        if (mTargetScene) requestSwitchScene(mTargetScene);
    });
    connect(mSceneCombo, qOverload<int>(&QComboBox::activated),
            this, [this](const int index) {
        if (index < 0 || index >= mDocument.fScenes.count()) return;
        setTargetScene(mDocument.fScenes.at(index).get());
    });

    mReturnBtn->hide();
    mAddClipBtn->setEnabled(false);
}

void EditTimelinePanel::setListeningEnabled(const bool enabled)
{
    if (mListening == enabled) return;
    mListening = enabled;
    for (const auto& c : mDocConns) QObject::disconnect(c);
    mDocConns.clear();

    if (!enabled) {
        mAutoSwitchTimer->stop();
        mThumbDirtyDebounce->stop();
        mRefreshDebounce->stop();
        mDirtySources.clear();
        mThumbSuppress = false;
        setTargetScene(nullptr);
        mActiveScene = nullptr;
        rebuildSceneCombo();
        updateReturnButton();
        return;
    }

    mActiveScene = mDocument.fActiveScene.get();
    mDocConns << connect(&mDocument, &Document::sceneCreated,
                         this, [this](Canvas*) { rebuildSceneCombo(); });
    mDocConns << connect(&mDocument,
                         qOverload<Canvas*>(&Document::sceneRemoved),
                         this, [this](Canvas* s) {
        rebuildSceneCombo();
        if (s == mTargetScene) {
            Canvas* fallback = nullptr;
            for (const auto& sc : mDocument.fScenes) {
                fallback = sc.get();
                break;
            }
            setTargetScene(fallback);
        }
    });
    mDocConns << connect(&mDocument, &Document::activeSceneSet,
                         this, [this](Canvas* s) {
        mActiveScene = s;
        updateReturnButton();
    });

    rebuildSceneCombo();
    Canvas* initial = mTargetScene.data();
    if (!initial) initial = mDocument.fActiveScene.get();
    if (!initial) {
        for (const auto& sc : mDocument.fScenes) {
            initial = sc.get();
            break;
        }
    }
    setTargetScene(initial);
}

void EditTimelinePanel::setTargetScene(Canvas* const scene)
{
    if (mTargetScene == scene) return;
    mAutoSwitchTimer->stop();
    for (const auto& c : mSceneConns) QObject::disconnect(c);
    mSceneConns.clear();
    mThumbQueue.clear();
    mQueuedKeys.clear();
    mThumbToken++;
    mThumbInFlight = 0;
    clearThumbCache();
    clearSourceConns();
    mTargetScene = scene;
    mClips.clear();

    if (scene) {
        auto& cs = mSceneConns;
        cs << connect(scene, &ContainerBox::insertedObject,
                      this, [this](int, eBoxOrSound*) {
            scheduleClipsRefresh();
        });
        cs << connect(scene, &ContainerBox::removedObject,
                      this, [this](int, eBoxOrSound*) {
            scheduleClipsRefresh();
        });
        cs << connect(scene, &ContainerBox::movedObject,
                      this, [this](int, int, eBoxOrSound*) {
            scheduleClipsRefresh();
        });
        cs << connect(scene, &Canvas::objectSelectionChanged,
                      this, [this]() { mView->update(); });
        cs << connect(scene, &Canvas::currentFrameChanged,
                      this, [this](int) {
            updateTimeLabel();
            mView->update();
        });
        cs << connect(scene, &Canvas::requestUpdate,
                      this, [this]() { mView->update(); });
    }

    mAddClipBtn->setEnabled(scene);
    updateReturnButton();
    updateTimeLabel();
    refreshClips();
    rebuildSceneCombo();
}

void EditTimelinePanel::rebuildSceneCombo()
{
    mComboGuard = true;
    mSceneCombo->clear();
    int idx = -1;
    int cur = 0;
    for (const auto& s : mDocument.fScenes) {
        Canvas* sc = s.get();
        if (!sc) continue;
        mSceneCombo->addItem(sc->prp_getName());
        if (sc == mTargetScene.data()) idx = cur;
        cur++;
    }
    if (idx >= 0) mSceneCombo->setCurrentIndex(idx);
    mComboGuard = false;
}

void EditTimelinePanel::scheduleClipsRefresh()
{
    if (!mListening) return;
    mRefreshDebounce->start();
}

void EditTimelinePanel::clearSourceConns()
{
    for (auto it = mSourceConns.cbegin();
         it != mSourceConns.cend(); ++it) {
        for (const auto& c : it.value()) QObject::disconnect(c);
    }
    mSourceConns.clear();
}

void EditTimelinePanel::refreshClips()
{
    mClips.clear();
    const auto target = mTargetScene.data();
    if (target) {
        for (auto* b : target->getContainedBoxes()) mClips.append(b);
    }
    clearSourceConns();
    QSet<Canvas*> sources;
    const int n = mClips.count();
    for (int i = 0; i < n; ++i) {
        auto* s = resolveSourceScene(mClips.at(i).data());
        if (s) sources.insert(s);
    }
    for (Canvas* s : sources) {
        QList<QMetaObject::Connection> cs;
        cs << connect(s, &Canvas::requestUpdate, this, [this, s]() {
            markSceneThumbsDirty(s);
        });
        mSourceConns.insert(s, cs);
    }
    mView->sceneChanged();
    updateTimeLabel();
}

Canvas* EditTimelinePanel::resolveSourceScene(eBoxOrSound* const box) const
{
    auto* cur = dynamic_cast<BoundingBox*>(box);
    if (!cur) return nullptr;
    for (int i = 0; i < 16; ++i) {
        const auto link = dynamic_cast<InternalLinkCanvas*>(cur);
        if (!link) break;
        auto* next = link->getLinkTarget();
        if (!next) return nullptr;
        cur = next;
    }
    return dynamic_cast<Canvas*>(cur);
}

void EditTimelinePanel::createCompositionScene()
{
    const auto scene = mDocument.createNewScene();
    if (!scene) return;
    setTargetScene(scene);
    requestSwitchScene(scene);
}

void EditTimelinePanel::showAddClipMenu()
{
    if (!mTargetScene) return;
    QMenu menu(this);
    bool any = false;
    for (const auto& s : mDocument.fScenes) {
        Canvas* sc = s.get();
        if (!sc || sc == mTargetScene.data()) continue;
        any = true;
        menu.addAction(sc->prp_getName(), this, [this, sc]() {
            addSceneAsClip(sc);
        });
    }
    if (!any) {
        const auto a = menu.addAction(tr("没有可添加的场景"));
        a->setEnabled(false);
    }
    menu.exec(mAddClipBtn->mapToGlobal(
                  QPoint(0, mAddClipBtn->height())));
}

void EditTimelinePanel::addSceneAsClip(Canvas* const source)
{
    const auto target = mTargetScene.data();
    if (!target || !source || source == target) return;
    const int insert = target->getCurrentFrame();

    target->prp_pushUndoRedoName(tr("添加素材块"));
    const auto link = target->createLink(false);
    if (!link) return;
    target->addContained(link);
    if (!link->hasDurationRectangle()) link->createDurationRectangle();
    const auto rect = link->getDurationRectangle();
    const auto fr = source->getFrameRange();
    if (rect) {
        // content window = the whole source scene; placed at the
        // playhead: shift = insert - srcMin keeps the mapping intact
        rect->setValues(RangeRectValues{insert - fr.fMin, fr.fMin, fr.fMax});
    }
    link->centerPivotPosition();
    link->moveTo(0);
    target->clearBoxesSelection();
    target->addBoxToSelection(link.get());
    Document::sInstance->actionFinished();
    refreshClips();
}

void EditTimelinePanel::scheduleAutoSwitch()
{
    if (!mTargetScene) return;
    if (mDocument.fActiveScene == mTargetScene.data()) return;
    mAutoSwitchTimer->start();
}

void EditTimelinePanel::cancelAutoSwitch()
{
    mAutoSwitchTimer->stop();
}

void EditTimelinePanel::requestSwitchScene(Canvas* const scene)
{
    if (!scene) return;
    const auto mw = MainWindow::sGetInstance();
    if (!mw) return;
    const auto lay = mw->getLayoutHandler();
    if (lay) lay->switchToScene(scene);
}

void EditTimelinePanel::updateReturnButton()
{
    const bool away = mTargetScene && mActiveScene &&
            mActiveScene != mTargetScene;
    mReturnBtn->setVisible(away);
    if (mTargetScene) {
        mReturnBtn->setToolTip(
                    tr("返回剪辑场景：%1").arg(mTargetScene->prp_getName()));
    }
}

void EditTimelinePanel::updateTimeLabel()
{
    const auto s = mTargetScene.data();
    if (!s) { mTimeLabel->setText(QString()); return; }
    mTimeLabel->setText(AppSupport::getTimeCodeFromFrame(
                            s->getCurrentFrame(), float(s->getFps())));
}

QString EditTimelinePanel::thumbKey(Canvas* const scene,
                                    const int frame) const
{
    return QStringLiteral("%1:%2").arg(
                reinterpret_cast<qulonglong>(scene), 0, 16).arg(frame);
}

void EditTimelinePanel::enqueueThumb(Canvas* const scene,
                                     const int frame)
{
    if (!mListening || !scene || mThumbSuppress) return;
    const QString key = thumbKey(scene, frame);
    if (mThumbCache.contains(key) || mQueuedKeys.contains(key)) return;
    mQueuedKeys.insert(key);
    ThumbReq req;
    req.scene = scene;
    req.frame = frame;
    mThumbQueue.append(req);
    pumpThumbQueue();
}

void EditTimelinePanel::pumpThumbQueue()
{
    while (mThumbInFlight < kThumbInFlightMax && !mThumbQueue.isEmpty()) {
        const auto req = mThumbQueue.takeFirst();
        auto* scene = req.scene.data();
        if (!scene) continue;
        const QString key = thumbKey(scene, req.frame);
        mQueuedKeys.remove(key);
        const auto task = scene->queExternalRender(req.frame, true);
        if (!task) continue;
        mThumbInFlight++;
        const std::weak_ptr<BoxRenderData> weak = task;
        const QPointer<EditTimelinePanel> self = this;
        const int token = mThumbToken;
        task->addDependent({[self, weak, key, token]() {
            const auto task = weak.lock();
            if (!task) return;
            QImage img;
            const auto sk = task->fRenderedImage;
            SkPixmap pm;
            if (sk && sk->peekPixels(&pm)) {
                QImage::Format fmt = QImage::Format_Invalid;
                if (pm.colorType() == kBGRA_8888_SkColorType) {
                    fmt = QImage::Format_ARGB32_Premultiplied;
                } else if (pm.colorType() == kRGBA_8888_SkColorType) {
                    fmt = QImage::Format_RGBA8888_Premultiplied;
                }
                if (fmt != QImage::Format_Invalid) {
                    img = QImage(reinterpret_cast<const uchar*>(pm.addr()),
                                 pm.width(), pm.height(),
                                 int(pm.rowBytes()), fmt).copy();
                }
            }
            if (img.width() > 256 || img.height() > 256) {
                img = img.scaled(256, 256, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
            }
            QMetaObject::invokeMethod(self.data(), [self, key, img, token]() {
                if (!self || token != self->mThumbToken) return;
                if (img.isNull()) self->thumbFailed();
                else self->thumbArrived(key, img);
            }, Qt::QueuedConnection);
        }, [self, token]() {
            // cancelled tasks must also free their in-flight slot or
            // the pipeline stalls with a permanently consumed slot
            QMetaObject::invokeMethod(self.data(), [self, token]() {
                if (!self || token != self->mThumbToken) return;
                self->thumbFailed();
            }, Qt::QueuedConnection);
        }});
    }
}

void EditTimelinePanel::thumbArrived(const QString& key, const QImage& img)
{
    if (mThumbCache.contains(key)) {
        mThumbOrder.removeAll(key);
    } else if (mThumbOrder.count() >= kThumbCacheMax) {
        mThumbCache.remove(mThumbOrder.takeFirst());
    }
    mThumbCache.insert(key, img);
    mThumbOrder << key;
    mThumbInFlight = qMax(0, mThumbInFlight - 1);
    mView->update();
    pumpThumbQueue();
}

void EditTimelinePanel::thumbFailed()
{
    mThumbInFlight = qMax(0, mThumbInFlight - 1);
    pumpThumbQueue();
}

void EditTimelinePanel::clearThumbCache()
{
    mThumbCache.clear();
    mThumbOrder.clear();
}

void EditTimelinePanel::clearSceneThumbs(Canvas* const scene)
{
    if (!scene) return;
    const QString prefix = QStringLiteral("%1:").arg(
                reinterpret_cast<qulonglong>(scene), 0, 16);
    const QStringList keys = mThumbCache.keys();
    for (const auto& k : keys) {
        if (k.startsWith(prefix)) {
            mThumbCache.remove(k);
            mThumbOrder.removeAll(k);
        }
    }
}

void EditTimelinePanel::markSceneThumbsDirty(Canvas* const scene)
{
    if (!mListening || !scene) return;
    mDirtySources.insert(scene);
    mThumbDirtyDebounce->start();
}
