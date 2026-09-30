#include "lyricmotionpreview.h"

#include "lyricmotioncanvas.h"

#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace {

// friction-side glue driving the vendored JIZURA renderer for preview
// tiles; installed after stub + sources. The C++ canvas document is
// already on the global object by then.
const QString kDriverJs = LyricCanvasDriver::source();

QStringList plannerSources() {
    QStringList files;
    QDirIterator it(QStringLiteral(":/jizura"), {QStringLiteral("*.js")},
                    QDir::Files);
    while (it.hasNext()) { files << it.next(); }
    files.sort();
    files.removeAll(QStringLiteral(":/jizura/jizura-stub.js"));
    return files;
}

} // namespace

LyricPreviewWorker::LyricPreviewWorker(const int canvasWidth,
                                       const int canvasHeight,
                                       const int frameCount,
                                       QObject * const parent) :
    QObject(parent), mW(canvasWidth), mH(canvasHeight), mFrames(frameCount) {}

void LyricPreviewWorker::setup() {
    QString error;
    if (!ensureEngine(&error)) {
        Q_EMIT engineFailed(error);
    } else {
        Q_EMIT engineReady();
    }
}

bool LyricPreviewWorker::ensureEngine(QString *error) {
    if (mLoaded) { return true; }
    mEngine = std::make_unique<QJSEngine>();
    // the C++ canvas document replaces the stub's fake one (the stub
    // only installs its own when no document exists)
    mFactory = new LyricCanvasFactory(mEngine.get());
    mFactory->install(mEngine.get());

    QString err;
    {
        QFile stub(QStringLiteral(":/jizura/jizura-stub.js"));
        stub.open(QIODevice::ReadOnly);
        if (!stub.isOpen()) {
            err = QStringLiteral("cannot open stub");
        } else {
            // the stub is an IIFE returning undefined — that is
            // success; only an actual script error aborts the load
            // (toString() would turn every undefined into "undefined"
            // and skip the planner + driver entirely)
            const auto stubResult = mEngine->evaluate(
                        QString::fromUtf8(stub.readAll()),
                        QStringLiteral("jizura-stub.js"));
            err = stubResult.isError() ? stubResult.toString()
                                       : QString();
        }
    }
    if (err.isEmpty()) {
        for (const QString &file : plannerSources()) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly)) {
                err = QStringLiteral("cannot open %1").arg(file);
                break;
            }
            const auto r = mEngine->evaluate(QString::fromUtf8(f.readAll()), file);
            if (r.isError()) {
                err = QStringLiteral("%1: %2").arg(
                            QFileInfo(file).fileName(), r.toString());
                break;
            }
        }
    }
    if (err.isEmpty()) {
        const auto r = mEngine->evaluate(kDriverJs, QStringLiteral("driver"));
        if (r.isError()) { err = QStringLiteral("driver: %1").arg(r.toString()); }
    }
    if (!err.isEmpty()) {
        if (error) { *error = err; }
        mEngine.reset();
        mFactory = nullptr;
        return false;
    }
    mLoaded = true;
    return true;
}

QString planCacheKey(const QString &styleKey, const quint32 seed,
                     const qreal density, const QString &lyrics,
                     const QVector<qreal> &beats, const qreal audioDuration,
                     const qreal fps) {
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
            .arg(styleKey).arg(seed).arg(QString::number(density, 'f', 3))
            .arg(QString::number(qHash(lyrics)))
            .arg(beats.size())
            .arg(beats.isEmpty() ? 0.0 : beats.first(), 0, 'f', 3)
            .arg(QString::number(fps, 'f', 3))
            + (beats.isEmpty() ? QString()
                               : QStringLiteral("|%1").arg(audioDuration));
}

bool LyricPreviewWorker::ensurePlan(const QString &styleKey,
                                    const quint32 seed,
                                    const qreal density,
                                    const QString &lyrics,
                                    const QVector<qreal> &beats,
                                    const qreal audioDuration,
                                    const qreal fps, QString *error) {
    const QString key = planCacheKey(styleKey, seed, density, lyrics,
                                     beats, audioDuration, fps);
    if (mLoaded && mPlanKey == key) { return true; } // cached plan matches
    // a genuinely different plan crashes QV4 (vendored global state;
    // verified with the --preview same2/3-styles probes: cache hits are
    // fine, a second distinct plan dies mid-render) — rebuild the
    // engine per plan change
    if (mLoaded) {
        mEngine.reset();
        mFactory = nullptr;
        mLoaded = false;
        mPlanKey.clear();
        QString engineErr;
        if (!ensureEngine(&engineErr)) {
            if (error) { *error = engineErr; }
            return false;
        }
    }
    const auto js = [](const QString &v) {
        return QString::fromUtf8(QJsonDocument(QJsonArray{v})
                                 .toJson(QJsonDocument::Compact)
                                 .mid(1).chopped(1));
    };
    QString beatsSrc = QStringLiteral("null");
    if (!beats.isEmpty()) {
        QJsonArray arr;
        for (const qreal b : beats) { arr.append(b); }
        beatsSrc = QString::fromUtf8(QJsonDocument(arr)
                                     .toJson(QJsonDocument::Compact));
    }
    const auto r = mEngine->evaluate(QStringLiteral(
        "__jzPlan(%1, %2, %3, %4, %5, %6, %7)")
            .arg(js(styleKey)).arg(seed)
            .arg(QString::number(density, 'f', 3)).arg(js(lyrics))
            .arg(beatsSrc)
            .arg(QString::number(audioDuration, 'f', 4))
            .arg(QString::number(fps, 'f', 3)));
    if (r.isError()) {
        if (error) { *error = r.toString(); }
        return false;
    }
    mPlanKey = key;
    return true;
}

qreal LyricPreviewWorker::planDuration() const {
    if (!mLoaded || !mEngine) { return 0; }
    return mEngine->evaluate(QStringLiteral("__jz.plan.duration")).toNumber();
}

QImage LyricPreviewWorker::renderFrameAt(const qreal t) {
    const auto canvasObj = mEngine->globalObject()
            .property(QStringLiteral("__jz"))
            .property(QStringLiteral("canvas"));
    const auto *canvas = qobject_cast<const JsCanvas2D *>(
                canvasObj.toQObject());
    if (!canvas) { return QImage(); }
    const auto r = mEngine->evaluate(QStringLiteral("__jzRenderTime(%1)")
                                     .arg(QString::number(t, 'f', 5)));
    if (r.isError()) { return QImage(); }
    return canvas->image().copy();
}

void LyricPreviewWorker::renderStyle(const QString &styleKey,
                                     const quint32 seed,
                                     const qreal density,
                                     const QString &lyrics,
                                     const QVector<qreal> &beats,
                                     const qreal audioDuration,
                                     const qreal fps,
                                     const int generation) {
    if (cancelled()) { return; }
    QString error;
    if (!ensureEngine(&error)) {
        Q_EMIT styleFailed(styleKey, generation, error);
        return;
    }
    QString planErr;
    if (!ensurePlan(styleKey, seed, density, lyrics, beats, audioDuration,
                    fps, &planErr)) {
        Q_EMIT styleFailed(styleKey, generation, planErr);
        return;
    }
    // preview tiles render on the shared canvas at the tile size (a
    // cut-level preview may have resized it)
    mEngine->evaluate(QStringLiteral("__jzInit(%1, %2)").arg(mW).arg(mH));
    QVector<QImage> frames;
    frames.reserve(mFrames);
    for (int i = 0; i < mFrames; i++) {
        if (cancelled()) { return; }
        const QImage frame = renderFrameAt(
                    planDuration() * (i + 0.5) / mFrames);
        if (frame.isNull()) {
            if (frames.isEmpty()) {
                Q_EMIT styleFailed(styleKey, generation,
                                   QStringLiteral("frame render failed"));
                return;
            }
            break; // keep whatever rendered; preview is best-effort
        }
        frames.append(frame);
    }
    Q_EMIT framesReady(styleKey, generation, frames);
}
