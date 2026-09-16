#ifndef EDITORTIMELINESYNC_H
#define EDITORTIMELINESYNC_H

#include <QObject>
#include <QPointer>
#include <QList>
#include <QHash>
#include <QImage>
#include <QMetaObject>
#include <QSet>
#include <QString>

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
//
// Track semantics are aligned with the native "layer-as-track" model
// (eBoxOrSound::trackId): a panel lane with several blocks IS a native
// track (siblings sharing a trackId collapse into one row), a
// single-block lane IS an independent row (trackId -1). Reads derive
// lanes from trackId; releases write the panel order back into both the
// contained order (row order) and the trackIds (merge / split).
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
    // translate the panel lane layout into the native track model:
    // panel row order -> contained order, shared lanes -> shared trackId,
    // solo lanes -> independent rows
    void applyTrackWriteback();
    // async offscreen render of one mid-clip frame per video block
    // (real content thumbnails), delivered through setClipThumbnail
    void requestThumbnails();
    void deliverThumbnail(const QString &key, const QImage &img);

    Document &mDocument;
    QPointer<EditorTimelineWidget> mWidget;
    // scene whose children the panel shows: the active scene, or the
    // last one that had blocks when the active scene has none
    QPointer<Canvas> mPanelScene;
    QHash<int, QPointer<eBoxOrSound>> mClipToLayer;
    // thumbnail cache keyed by "<boxPtr>:<relFrame>" so renames and
    // rebuilds reuse renders; only a changed mid frame re-renders
    QHash<QString, QImage> mThumbDone;
    QSet<QString> mThumbPending;
    QList<QMetaObject::Connection> mSceneConns;
    QList<QMetaObject::Connection> mChildConns;
    bool mInWriteback = false;
    bool mDragging = false;
    bool mRebuildQueued = false;
};

#endif // EDITORTIMELINESYNC_H
