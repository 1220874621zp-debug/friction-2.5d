#ifndef LYRICMOTIONPREVIEW_H
#define LYRICMOTIONPREVIEW_H

#include <QObject>
#include <QVector>
#include <QImage>
#include <QJSEngine>
#include <atomic>
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

    // panel shutdown: in-flight frame loops stop early (a compile
    // already inside evaluate() still runs to completion)
    void setCancelled() { mCancelled.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return mCancelled.load(std::memory_order_relaxed); }

public slots:
    void setup();
    void renderStyle(const QString &styleKey, const quint32 seed,
                     const qreal density, const QString &lyrics,
                     const QVector<qreal> &beats, qreal audioDuration,
                     qreal fps, const int generation);
    // one large frame at an arbitrary time (cut-level preview); reuses
    // the cached plan when the plan inputs are unchanged
    void renderCutFrame(const QString &styleKey, const quint32 seed,
                        const qreal density, const QString &lyrics,
                        const QVector<qreal> &beats, qreal audioDuration,
                        qreal fps, const qreal time,
                        const int width, const int height,
                        const int generation);
    // animated cut-level preview: N frames spanning the cut's
    // [start,end] window at the big-preview size
    void renderCutAnimation(const QString &styleKey, const quint32 seed,
                            const qreal density, const QString &lyrics,
                            const QVector<qreal> &beats, qreal audioDuration,
                            qreal fps, const qreal startTime,
                            const qreal endTime, const int frameCount,
                            const int width, const int height,
                            const int generation);

signals:
    void engineReady();
    void engineFailed(const QString &error);
    void framesReady(const QString &styleKey, const int generation,
                     const QVector<QImage> &frames);
    void styleFailed(const QString &styleKey, const int generation,
                     const QString &error);
    void cutFrameReady(const QImage &frame, const int generation);
    void cutFramesReady(const QVector<QImage> &frames,
                        const int generation);

private:
    bool ensureEngine(QString *error);
    bool ensurePlan(const QString &styleKey, const quint32 seed,
                    const qreal density, const QString &lyrics,
                    const QVector<qreal> &beats, qreal audioDuration,
                    qreal fps, QString *error);
    QImage renderFrameAt(const qreal t);
    QImage renderBigFrameAt(const qreal t, const int width,
                            const int height);
    qreal planDuration() const;

    const int mW, mH, mFrames;
    std::unique_ptr<QJSEngine> mEngine;
    LyricCanvasFactory *mFactory = nullptr;
    bool mLoaded = false;
    QString mPlanKey;
    std::atomic<bool> mCancelled{false};
};

#endif // LYRICMOTIONPREVIEW_H
