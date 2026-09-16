#ifndef EDITORTIMELINESYNC_H
#define EDITORTIMELINESYNC_H

#include <QObject>
#include <QPointer>
#include <QList>
#include <QHash>
#include <QMetaObject>

class Canvas;
class Document;
class EditorTimelineWidget;
class eBoxOrSound;

// Edit-timeline semantic bridge. The panel shows the children of the
// ACTIVE scene: scene-link layers become video blocks, sound layers
// become audio blocks. When the active scene has no link/sound children
// (e.g. the user dove into a child scene), the panel keeps showing the
// last scene that had blocks instead of blanking. The playhead tracks
// the shown scene both ways; block drags/trims write back into the
// layers' duration rectangles on mouse release.
class EditorTimelineSync : public QObject
{
    Q_OBJECT
public:
    EditorTimelineSync(Document &document,
                       EditorTimelineWidget * const widget,
                       QObject * const parent = nullptr);

    bool eventFilter(QObject * const obj, QEvent * const ev) override;

public slots:
    void rebuild();
    void updatePlayheadFromDoc();

private:
    void connectPanelScene(Canvas * const scene);
    void connectChildren(Canvas * const scene);
    void syncPlayheadToDoc();
    void applyWriteback();
    void storeTrackAssignments();

    Document &mDocument;
    QPointer<EditorTimelineWidget> mWidget;
    // scene whose children the panel shows: the active scene, or the
    // last one that had blocks when the active scene has none
    QPointer<Canvas> mPanelScene;
    QHash<int, QPointer<eBoxOrSound>> mClipToLayer;
    // per-layer lane within its type group (video lanes 0.., audio lanes
    // 0..), remembered in-session so merged tracks and manual track moves
    // survive rebuilds; empty lanes are kept until a merge compacts them
    QHash<eBoxOrSound*, int> mLayerLane;
    QList<QMetaObject::Connection> mSceneConns;
    QList<QMetaObject::Connection> mChildConns;
    bool mInWriteback = false;
    bool mDragging = false;
    bool mRebuildQueued = false;
};

#endif // EDITORTIMELINESYNC_H
