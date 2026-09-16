#include "editortimelinesync.h"
#include "editortimelinewidget.h"

#include "Private/document.h"
#include "canvas.h"
#include "Boxes/internallinkcanvas.h"
#include "Animators/eboxorsound.h"
#include "Sound/esound.h"
#include "Timeline/durationrectangle.h"
#include "smartPointers/ememory.h"

#include <QMouseEvent>

EditorTimelineSync::EditorTimelineSync(Document &document,
                                       EditorTimelineWidget * const widget,
                                       QObject * const parent)
    : QObject(parent)
    , mDocument(document)
    , mWidget(widget)
{
    mWidget->installEventFilter(this);

    connect(&mDocument, qOverload<Canvas*>(&Document::sceneCreated),
            this, &EditorTimelineSync::rebuild);
    connect(&mDocument, qOverload<Canvas*>(&Document::sceneRemoved),
            this, &EditorTimelineSync::rebuild);
    connect(&mDocument, &Document::activeSceneSet,
            this, &EditorTimelineSync::rebuild);

    rebuild();
}

void EditorTimelineSync::connectActiveScene()
{
    Canvas * const scene = mDocument.fActiveScene ?
                mDocument.fActiveScene.data() : nullptr;
    if (scene == mActiveScene.data()) { return; }
    for (const auto &conn : mSceneConns) { disconnect(conn); }
    mSceneConns.clear();
    mActiveScene = scene;
    if (!scene) { return; }

    mSceneConns << connect(scene, &Canvas::ca_childAdded,
                           this, [this](Property*) { rebuild(); });
    mSceneConns << connect(scene, &Canvas::ca_childRemoved,
                           this, [this](Property*) { rebuild(); });
    mSceneConns << connect(scene, &Canvas::prp_currentFrameChanged,
                           this, [this](const UpdateReason) {
        updatePlayheadFromDoc();
    });
    mSceneConns << connect(scene, &Canvas::fpsChanged,
                           this, [this](const qreal) { rebuild(); });
}

void EditorTimelineSync::connectChildren()
{
    for (const auto &conn : mChildConns) { disconnect(conn); }
    mChildConns.clear();
    const auto scene = mActiveScene.data();
    if (!scene) { return; }
    for (const auto &child : scene->getContained()) {
        const auto layer = child.data();
        if (!layer) { continue; }
        mChildConns << connect(layer, &eBoxOrSound::prp_nameChanged,
                               this, [this](const QString&) { rebuild(); });
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

    connectActiveScene();
    connectChildren();
    mClipToLayer.clear();

    const auto scene = mActiveScene.data();
    struct Item { eBoxOrSound *layer; bool audio; };
    QList<Item> items;
    if (scene) {
        for (const auto &child : scene->getContained()) {
            const auto layer = child.data();
            if (!layer) { continue; }
            const bool audio = enve_cast<eSound*>(layer) != nullptr;
            const bool video = enve_cast<InternalLinkCanvas*>(layer) != nullptr;
            if (audio) { items.append({layer, true}); }
            else if (video) { items.append({layer, false}); }
        }
    }
    int videoCount = 0;
    int audioCount = 0;
    for (const auto &it : items) { it.audio ? audioCount++ : videoCount++; }

    mWidget->rebuildTracks(videoCount, audioCount);
    mWidget->clearAllClips();

    if (!scene) { return; }
    const qreal fps = scene->getFps();
    const double fallbackLen = fps > 0. ?
                scene->getFrameRange().fMax / fps : 0.;
    int vIdx = 0;
    int aIdx = 0;
    for (const auto &it : items) {
        double start = 0.;
        double len = fallbackLen;
        const auto dur = it.layer->getDurationRectangle();
        if (dur && fps > 0.) {
            const int minF = dur->getMinRelFrame();
            const int maxF = dur->getMaxRelFrame();
            start = minF / fps;
            len = (maxF - minF + 1) / fps;
        }
        const int track = it.audio ?
                    videoCount + (audioCount - 1 - aIdx++) :
                    (videoCount - 1 - vIdx++);
        const int id = mWidget->appendClip(it.layer->prp_getName(),
                                           start, len, it.audio, track);
        mClipToLayer.insert(id, it.layer);
    }
    updatePlayheadFromDoc();
}

void EditorTimelineSync::updatePlayheadFromDoc()
{
    if (!mWidget || !mDocument.fActiveScene) { return; }
    const auto scene = mDocument.fActiveScene.data();
    const qreal fps = scene->getFps();
    if (fps <= 0.) { return; }
    mWidget->setPlayheadSec(mDocument.getActiveSceneFrame() / fps);
}

void EditorTimelineSync::syncPlayheadToDoc()
{
    if (!mWidget || !mDocument.fActiveScene) { return; }
    const auto scene = mDocument.fActiveScene.data();
    const qreal fps = scene->getFps();
    if (fps <= 0.) { return; }
    const int frame = qRound(mWidget->playheadTime() * fps);
    if (frame != mDocument.getActiveSceneFrame()) {
        mDocument.setActiveSceneFrame(frame);
    }
}

void EditorTimelineSync::applyWriteback()
{
    const auto scene = mActiveScene.data();
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
        const int oldMin = dur->getMinRelFrame();
        const int oldMax = dur->getMaxRelFrame();
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

bool EditorTimelineSync::eventFilter(QObject * const obj, QEvent * const ev)
{
    if (obj == mWidget.data()) {
        const auto type = ev->type();
        if (type == QEvent::MouseButtonPress) {
            const auto me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) { mDragging = true; }
        } else if (type == QEvent::MouseMove) {
            if (mDragging) { syncPlayheadToDoc(); }
        } else if (type == QEvent::MouseButtonRelease) {
            const auto me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton && mDragging) {
                mDragging = false;
                syncPlayheadToDoc();
                applyWriteback();
                rebuild();
                mRebuildQueued = false;
            }
        }
    }
    return QObject::eventFilter(obj, ev);
}
