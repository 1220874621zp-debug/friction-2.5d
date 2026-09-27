#ifndef LYRICMOTIONPREVIEW_H
#define LYRICMOTIONPREVIEW_H

#include <QObject>
#include <QVector>
#include <QImage>
#include <QJSEngine>
#include <memory>

class LyricCanvasFactory;

// Renders style preview tiles with the real JIZURA renderer running on
// a dedicated QJSEngine + the Canvas2D-over-QPainter shim. Lives on a
// worker thread (created by LyricMotionPanel); requests are queued via
// signal/slot connections and executed one at a time.
class LyricPreviewWorker : public QObject
{
    Q_OBJECT
public:
    explicit LyricPreviewWorker(const int canvasWidth, const int canvasHeight,
                                const int frameCount,
                                QObject * const parent = nullptr);

public slots:
    void setup();
    void renderStyle(const QString &styleKey, const quint32 seed,
                     const qreal density, const int generation);

signals:
    void engineReady();
    void engineFailed(const QString &error);
    void framesReady(const QString &styleKey, const int generation,
                     const QVector<QImage> &frames);
    void styleFailed(const QString &styleKey, const int generation,
                     const QString &error);

private:
    bool ensureEngine(QString *error);

    const int mW, mH, mFrames;
    std::unique_ptr<QJSEngine> mEngine;
    LyricCanvasFactory *mFactory = nullptr;
    bool mLoaded = false;
};

#endif // LYRICMOTIONPREVIEW_H
