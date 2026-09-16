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
// ACTIVE scene only: scene-link layers become video blocks (one per
// video track), sound layers become audio blocks. The playhead tracks
// the document frame both ways; block drags/trims write back into the
// layers' duration rectangles on mouse release. The demo UI
// (editortimelinewidget) stays a byte-level TimelineDemo port and is
// only driven through its appended public API.
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
    void connectActiveScene();
    void connectChildren();
    void syncPlayheadToDoc();
    void applyWriteback();
    void storeTrackAssignments();

    Document &mDocument;
    QPointer<EditorTimelineWidget> mWidget;
    QPointer<Canvas> mActiveScene;
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
