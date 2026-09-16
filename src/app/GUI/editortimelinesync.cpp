#include "editortimelinesync.h"
#include "editortimelinewidget.h"

#include "Private/document.h"
#include "canvas.h"
#include "Boxes/containerbox.h"
#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "Animators/eboxorsound.h"
#include "Sound/esound.h"
#include "Timeline/durationrectangle.h"
#include "smartPointers/ememory.h"

#include <QMouseEvent>
#include <QSet>
#include <QDebug>
#include <climits>
#include <algorithm>

EditorTimelineSync::EditorTimelineSync(Document &document,
                                       EditorTimelineWidget * const widget,
                                       QObject * const parent)
    : QObject(parent)
    , mDocument(document)
    , mWidget(widget)
{
    mWidget->installEventFilter(this);

    // context-menu merges inside the widget: push them into the native
    // track model and refresh from it right away
    connect(mWidget, &EditorTimelineWidget::trackLayoutChanged,
            this, [this]() {
        applyTrackWriteback();
        rebuild();
    });

    connect(&mDocument, qOverload<Canvas*>(&Document::sceneCreated),
            this, &EditorTimelineSync::rebuild);
    connect(&mDocument, qOverload<Canvas*>(&Document::sceneRemoved),
            this, &EditorTimelineSync::rebuild);
    connect(&mDocument, &Document::activeSceneSet,
            this, &EditorTimelineSync::rebuild);

    rebuild();
}

void EditorTimelineSync::connectPanelScene(Canvas * const scene)
{
    if (scene == mPanelScene.data()) { return; }
    for (const auto &conn : mSceneConns) { disconnect(conn); }
    mSceneConns.clear();
    mPanelScene = scene;
    if (!scene) { return; }

    mSceneConns << connect(scene, &Canvas::ca_childAdded,
                           this, [this](Property*) { rebuild(); });
    mSceneConns << connect(scene, &Canvas::ca_childRemoved,
                           this, [this](Property*) { rebuild(); });
    // native row reorder (drag in the layer panel / track writebacks /
    // undo) changes the lane order the panel derives from
    mSceneConns << connect(scene, &ContainerBox::movedObject,
                           this, [this](const int, const int, eBoxOrSound*) {
        rebuild();
    });
    mSceneConns << connect(scene, &Canvas::prp_currentFrameChanged,
                           this, [this](const UpdateReason) {
        updatePlayheadFromDoc();
    });
    mSceneConns << connect(scene, &Canvas::fpsChanged,
                           this, [this](const qreal) { rebuild(); });
}

void EditorTimelineSync::connectChildren(Canvas * const scene)
{
    for (const auto &conn : mChildConns) { disconnect(conn); }
    mChildConns.clear();
    if (!scene) { return; }
    for (const auto &child : scene->getContained()) {
        const auto layer = child.data();
        if (!layer) { continue; }
        mChildConns << connect(layer, &eBoxOrSound::prp_nameChanged,
                               this, [this](const QString&) { rebuild(); });
        // native track merge / split (layer-panel drags, context menus,
        // undo) must re-map the panel lanes
        mChildConns << connect(layer, &eBoxOrSound::trackIdChanged,
                               this, [this](const int) { rebuild(); });
        const auto dur = layer->getDurationRectangle();
        if (dur) {
            mChildConns << connect(dur, &DurationRectangle::minRelFrameChanged,
                                   this, [this](const int, const int) { rebuild(); });
            mChildConns << connect(dur, &DurationRectangle::maxRelFrameChanged,
                                   this, [this](const int, const int) { rebuild(); });
        }
    }
}

void EditorTimelineSync::rebuild()
{
    if (!mWidget) { return; }
    if (mInWriteback) { return; }
    if (mDragging) { mRebuildQueued = true; return; }

    // panel scene: follow the active scene, but when it has nothing to
    // edit (e.g. the user dove into a child scene), keep showing the
    // last scene that had blocks instead of blanking the panel
    const auto activeScene = mDocument.fActiveScene.data();
    struct Item { eBoxOrSound *layer; bool audio; };
    QList<Item> items;
    const auto collect = [&items](Canvas * const s) {
        items.clear();
        if (!s) { return; }
        // mirror the native timeline: EVERY child layer shows here.
        // Sounds become audio blocks; every visual layer (scene links,
        // vectors, images, text, groups, ...) becomes a video block
        // with the same thumbnail logic
        for (const auto &child : s->getContained()) {
            const auto layer = child.data();
            if (!layer) { continue; }
            const bool audio = enve_cast<eSound*>(layer) != nullptr;
            if (audio) { items.append({layer, true}); }
            else { items.append({layer, false}); }
        }
    };
    collect(activeScene);
    auto scene = activeScene;
    if (items.isEmpty()) {
        const auto fallback = mPanelScene.data();
        if (fallback && fallback != activeScene) {
            collect(fallback);
            if (!items.isEmpty()) { scene = fallback; }
        }
    }

    connectPanelScene(scene);
    connectChildren(scene);
    mClipToLayer.clear();
    // lanes mirror the native track model: siblings sharing a trackId
    // collapse into one lane, everything else owns a lane; lane order
    // follows the contained order (contained[0] = native top row = top
    // lane), so panel lanes and native rows stay two views of one order
    QVector<int> lane(items.size(), 0);
    {
        QHash<int, int> vTidToLane, aTidToLane;
        int vNext = 0, aNext = 0;
        for (int i = 0; i < items.size(); ++i) {
            const int tid = items[i].layer->trackId();
            auto &tidToLane = items[i].audio ? aTidToLane : vTidToLane;
            int &next = items[i].audio ? aNext : vNext;
            if (tid >= 0) {
                const auto it = tidToLane.constFind(tid);
                if (it != tidToLane.constEnd()) { lane[i] = it.value(); continue; }
                tidToLane.insert(tid, next);
            }
            lane[i] = next;
            ++next;
        }
    }
    int videoCount = 0;
    int audioCount = 0;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].audio) { audioCount = qMax(audioCount, lane[i] + 1); }
        else { videoCount = qMax(videoCount, lane[i] + 1); }
    }

    mWidget->rebuildTracks(videoCount, audioCount);
    mWidget->clearAllClips();
    qDebug("[ETL] rebuild panel=%s items=%d video=%d audio=%d active=%s",
           scene ? scene->prp_getName().toUtf8().constData() : "-",
           items.size(), videoCount, audioCount,
           mDocument.fActiveScene ?
               mDocument.fActiveScene->prp_getName().toUtf8().constData() : "-");

    if (!scene) { return; }
    const qreal fps = scene->getFps();
    const double fallbackLen = fps > 0. ?
                scene->getFrameRange().fMax / fps : 0.;
    for (int i = 0; i < items.size(); ++i) {
        const auto &it = items[i];
        double start = 0.;
        double len = fallbackLen;
        const auto dur = it.layer->getDurationRectangle();
        if (dur && fps > 0.) {
            // absolute frames: the native timeline draws and hit-tests
            // the duration bar in abs space (durationrectangle.cpp draw)
            const int minF = dur->getMinAbsFrame();
            const int maxF = dur->getMaxAbsFrame();
            start = minF / fps;
            len = (maxF - minF + 1) / fps;
        }
        const int track = it.audio ? videoCount + lane[i] : lane[i];
        const int id = mWidget->appendClip(it.layer->prp_getName(),
                                           start, len, it.audio, track);
        mClipToLayer.insert(id, it.layer);
    }
    requestThumbnails();
    updatePlayheadFromDoc();
}

void EditorTimelineSync::applyTrackWriteback()
{
    const auto scene = mPanelScene.data();
    if (!mWidget || !scene) { return; }
    const int videoTracks = mWidget->videoTrackCount();

    struct Entry { eBoxOrSound *layer; int lane; double start; };
    QList<Entry> video, audio;
    for (const auto &clip : mWidget->allClips()) {
        const auto layer = mClipToLayer.value(clip.id).data();
        if (!layer) { continue; }
        if (clip.audio) { audio.append({layer, clip.track - videoTracks, clip.start}); }
        else { video.append({layer, clip.track, clip.start}); }
    }

    mInWriteback = true;
    for (const bool isAudio : {false, true}) {
        auto &entries = isAudio ? audio : video;
        if (entries.isEmpty()) { continue; }
        // panel order: top lane first, then clip start (the panel's
        // top-to-bottom reading of the timeline)
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) {
            if (a.lane != b.lane) { return a.lane < b.lane; }
            return a.start < b.start;
        });
        const auto parent = entries.first().layer->getParentGroup();
        if (!parent || parent->getParentScene() != scene) { continue; }

        // 1. persist the panel order as the native row order (contained
        // order). Skip when they already agree; when they differ, the
        // panel rows occupy the topmost slot the group currently holds
        // and the members stack in panel order below it
        bool needReorder = false;
        bool stale = false;
        int prevIdx = -1;
        int minIdx = INT_MAX;
        for (const auto &e : entries) {
            const int idx = parent->getContainedIndex(e.layer);
            if (idx < 0) { stale = true; break; }
            minIdx = qMin(minIdx, idx);
            if (idx <= prevIdx) { needReorder = true; }
            prevIdx = qMax(prevIdx, idx);
        }
        if (!stale && needReorder) {
            parent->moveContainedInList(entries.first().layer, minIdx);
            for (int i = 1; i < entries.size(); ++i) {
                parent->moveContainedBelow(entries[i].layer,
                                           entries[i - 1].layer);
            }
        }

        // 2. lanes -> tracks: a lane with several blocks joins (or keeps)
        //    one shared trackId; a single-block lane leaves any track
        int laneStart = 0;
        while (laneStart < entries.size()) {
            int laneEnd = laneStart;
            while (laneEnd < entries.size() &&
                   entries[laneEnd].lane == entries[laneStart].lane) { ++laneEnd; }
            if (laneEnd - laneStart == 1) {
                auto * const m = entries[laneStart].layer;
                if (m->isInTrack()) { m->setTrackId(-1); }
            } else {
                // keep an existing id when these members already share
                // one, otherwise open a fresh track
                QHash<int, int> votes;
                int best = -1, bestVotes = 0;
                for (int i = laneStart; i < laneEnd; ++i) {
                    const int tid = entries[i].layer->trackId();
                    if (tid < 0) { continue; }
                    const int v = ++votes[tid];
                    if (v > bestVotes) { bestVotes = v; best = tid; }
                }
                const int tid = best >= 0 ? best : scene->newTrackId(parent);
                for (int i = laneStart; i < laneEnd; ++i) {
                    auto * const m = entries[i].layer;
                    if (m->trackId() != tid) { m->setTrackId(tid); }
                }
            }
            laneStart = laneEnd;
        }
    }
    mInWriteback = false;
    if (Document::sInstance) { Document::sInstance->actionFinished(); }
}

void EditorTimelineSync::updatePlayheadFromDoc()
{
    const auto scene = mPanelScene.data();
    if (!mWidget || !scene) { return; }
    const qreal fps = scene->getFps();
    if (fps <= 0.) { return; }
    mWidget->setPlayheadSec(scene->anim_getCurrentAbsFrame() / fps);
}

void EditorTimelineSync::syncPlayheadToDoc()
{
    const auto scene = mPanelScene.data();
    if (!mWidget || !scene) { return; }
    const qreal fps = scene->getFps();
    if (fps <= 0.) { return; }
    const int frame = qRound(mWidget->playheadTime() * fps);
    if (scene == mDocument.fActiveScene.data()) {
        if (frame != mDocument.getActiveSceneFrame()) {
            mDocument.setActiveSceneFrame(frame);
        }
    } else if (frame != scene->anim_getCurrentAbsFrame()) {
        scene->anim_setAbsFrame(frame);
    }
}

void EditorTimelineSync::applyWriteback()
{
    const auto scene = mPanelScene.data();
    if (!mWidget || !scene) { return; }
    const qreal fps = scene->getFps();
    if (fps <= 0.) { return; }

    mInWriteback = true;
    const auto clips = mWidget->allClips();
    for (const auto &clip : clips) {
        const auto layer = mClipToLayer.value(clip.id);
        if (!layer) { continue; }
        const auto dur = layer->getDurationRectangle();
        if (!dur) { continue; }
        const int newMin = qRound(clip.start * fps);
        const int newMax = qRound((clip.start + clip.length) * fps) - 1;
        const int oldMin = dur->getMinAbsFrame();
        const int oldMax = dur->getMaxAbsFrame();
        // rel/abs shift is constant, so abs deltas are valid rel moves
        if (newMin != oldMin) {
            dur->startMinFramePosTransform();
            dur->moveMinFrame(newMin - oldMin);
            dur->finishMinFramePosTransform();
        }
        if (newMax != oldMax) {
            dur->startMaxFramePosTransform();
            dur->moveMaxFrame(newMax - oldMax);
            dur->finishMaxFramePosTransform();
        }
    }
    mInWriteback = false;
}

void EditorTimelineSync::requestThumbnails()
{
    const auto scene = mPanelScene.data();
    if (!mWidget || !scene) { return; }
    if (mThumbDone.size() > 128) { mThumbDone.clear(); }
    for (auto it = mClipToLayer.begin(); it != mClipToLayer.end(); ++it) {
        const int clipId = it.key();
        // every visual layer renders (scene links, vectors, images,
        // text, groups) - not just scene links
        const auto box = enve_cast<BoundingBox*>(it.value().data());
        if (!box) { continue; } // audio blocks draw their waveform
        const auto dur = box->getDurationRectangle();
        if (!dur) { continue; }
        // one real frame per block: the middle of its range (stable
        // across rebuilds; only a re-trim that moves the midpoint
        // re-renders)
        const int absMid = (dur->getMinAbsFrame() + dur->getMaxAbsFrame()) / 2;
        const qreal relFrame = box->prp_absFrameToRelFrameF(absMid);
        const QString key = QStringLiteral("%1:%2")
                .arg(reinterpret_cast<qulonglong>(box), 0, 16)
                .arg(qRound(relFrame));
        const auto cached = mThumbDone.constFind(key);
        if (cached != mThumbDone.constEnd()) {
            mWidget->setClipThumbnail(clipId, cached.value());
            continue;
        }
        if (mThumbPending.contains(key)) { continue; }
        // async offscreen render of the linked scene composition (same
        // path the switch panel uses); result marshalled back to the GUI
        // thread, stale deliveries dropped by clip id
        auto task = box->queExternalRender(relFrame, true);
        if (!task) { continue; }
        mThumbPending.insert(key);
        const std::weak_ptr<BoxRenderData> weak = task;
        const QPointer<EditorTimelineSync> self = this;
        task->addDependent({[self, weak, key]() {
            QImage img;
            const auto t = weak.lock();
            if (t && t->fRenderedImage) {
                SkPixmap pm;
                if (t->fRenderedImage->peekPixels(&pm)) {
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
            }
            QMetaObject::invokeMethod(self, [self, key, img]() {
                if (!self) { return; }
                self->mThumbPending.remove(key);
                if (img.isNull()) { return; }
                self->mThumbDone.insert(key, img);
                self->deliverThumbnail(key, img);
            }, Qt::QueuedConnection);
        }, [](){}});
    }
}

void EditorTimelineSync::deliverThumbnail(const QString &key, const QImage &img)
{
    const auto scene = mPanelScene.data();
    if (!mWidget || !scene) { return; }
    for (auto it = mClipToLayer.begin(); it != mClipToLayer.end(); ++it) {
        const auto box = enve_cast<BoundingBox*>(it.value().data());
        if (!box) { continue; }
        const auto dur = box->getDurationRectangle();
        if (!dur) { continue; }
        const int absMid = (dur->getMinAbsFrame() + dur->getMaxAbsFrame()) / 2;
        const qreal relFrame = box->prp_absFrameToRelFrameF(absMid);
        const QString curKey = QStringLiteral("%1:%2")
                .arg(reinterpret_cast<qulonglong>(box), 0, 16)
                .arg(qRound(relFrame));
        if (curKey == key) {
            mWidget->setClipThumbnail(it.key(), img);
        }
    }
}

bool EditorTimelineSync::eventFilter(QObject * const obj, QEvent * const ev)
{
    if (obj == mWidget.data()) {
        const auto type = ev->type();
        if (type == QEvent::MouseButtonPress) {
            const auto me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) { mDragging = true; }
        } else if (type == QEvent::Hide) {
            // dock toggled off: never leave the dragging lock behind, it
            // would freeze rebuilds until the next click-release
            if (mDragging) { qDebug("[ETL] hidden while dragging, unlock"); }
            mDragging = false;
        } else if (type == QEvent::MouseMove) {
            if (mDragging) { syncPlayheadToDoc(); }
        } else if (type == QEvent::MouseButtonRelease) {
            const auto me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton && mDragging) {
                mDragging = false;
                syncPlayheadToDoc();
                applyWriteback();
                mWidget->compactLanes();      // drop lanes emptied by the drag
                applyTrackWriteback();        // lanes -> native rows/tracks
                rebuild();
                mRebuildQueued = false;
            }
        }
    }
    return QObject::eventFilter(obj, ev);
}
