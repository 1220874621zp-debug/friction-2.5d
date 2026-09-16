#ifndef EDITORTIMELINESYNC_H
#define EDITORTIMELINESYNC_H

#include <QObject>
#include <QPointer>
#include <QList>
#include <QMetaObject>

class Canvas;
class Document;
class EditorTimelineWidget;

// Scenes -> edit-timeline video blocks bridge: one friction scene =
// one clip, laid out sequentially by scene duration on the bottom
// video track. The demo UI (editortimelinewidget) stays a byte-level
// TimelineDemo port; this adapter only drives it through the
// appended public API (clearAllClips / appendSceneClip).
class EditorTimelineSync : public QObject
{
    Q_OBJECT
public:
    EditorTimelineSync(Document &document,
                       EditorTimelineWidget * const widget,
                       QObject * const parent = nullptr);

public slots:
    void rebuild();

private:
    void connectSceneSignals();

    Document &mDocument;
    QPointer<EditorTimelineWidget> mWidget;
    QList<QMetaObject::Connection> mSceneConns;
};

#endif // EDITORTIMELINESYNC_H
