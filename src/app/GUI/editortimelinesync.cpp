#include "editortimelinesync.h"
#include "editortimelinewidget.h"

#include "Private/document.h"
#include "canvas.h"

EditorTimelineSync::EditorTimelineSync(Document &document,
                                       EditorTimelineWidget * const widget,
                                       QObject * const parent)
    : QObject(parent)
    , mDocument(document)
    , mWidget(widget)
{
    connect(&mDocument, qOverload<Canvas*>(&Document::sceneCreated),
            this, &EditorTimelineSync::rebuild);
    connect(&mDocument, qOverload<Canvas*>(&Document::sceneRemoved),
            this, &EditorTimelineSync::rebuild);

    rebuild();
}

void EditorTimelineSync::connectSceneSignals()
{
    for (const auto &conn : mSceneConns) { disconnect(conn); }
    mSceneConns.clear();
    for (const auto &scene : mDocument.fScenes) {
        if (!scene) { continue; }
        mSceneConns << connect(scene.data(), &Canvas::prp_nameChanged,
                               this, &EditorTimelineSync::rebuild);
        mSceneConns << connect(scene.data(), &Canvas::prp_absFrameRangeChanged,
                               this, &EditorTimelineSync::rebuild);
        mSceneConns << connect(scene.data(), &Canvas::fpsChanged,
                               this, &EditorTimelineSync::rebuild);
    }
}

void EditorTimelineSync::rebuild()
{
    if (!mWidget) { return; }
    connectSceneSignals();

    mWidget->clearAllClips();
    double startSec = 0.0;
    for (const auto &scene : mDocument.fScenes) {
        if (!scene) { continue; }
        const qreal fps = scene->getFps();
        const int maxFrame = scene->getFrameRange().fMax;
        const double lenSec = fps > 0. ? maxFrame / fps : 0.;
        mWidget->appendSceneClip(scene->prp_getName(), startSec, lenSec);
        startSec += lenSec;
    }
}
