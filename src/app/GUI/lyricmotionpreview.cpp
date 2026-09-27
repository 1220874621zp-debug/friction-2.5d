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
const auto kDriverJs = QStringLiteral(
    "(function(){"
    "  var st = { canvas: null, ctx: null, renderer: null, plan: null };"
    "  globalThis.__jz = st;"
    "  globalThis.__jzInit = function(w, h){"
    "    st.canvas = document.createElement('canvas');"
    "    st.canvas.width = w; st.canvas.height = h;"
    "    st.ctx = st.canvas.getContext('2d');"
    "  };"
    "  globalThis.__jzPlan = function(styleKey, seed, density){"
    "    var pr = J.defaultProject();"
    "    pr.lyrics = \"\\u591c\\u660e\\u3051\\u306e\\u8272\\n*\\u6587\\u5b57* Motion\\n\\u6b4c\\u8a5e\\u30a2\\u30cb\\u30e1\\u3067\\u3059\";"
    "    pr.style = styleKey; pr.seed = seed;"
    "    pr.fx.density = (density == null ? 0.55 : density);"
    "    pr.timing.bpm = 0;"
    "    st.plan = J.plan(pr, null);"
    "    st.renderer = new J.Renderer();"
    "    return JSON.stringify({ duration: st.plan.duration, cuts: st.plan.cuts.length });"
    "  };"
    "  globalThis.__jzFrame = function(i, n){"
    "    var plan = st.plan;"
    "    var t = plan.duration * (i + 0.5) / n;"
    "    st.ctx.setTransform(1, 0, 0, 1, 0, 0);"
    "    st.ctx.clearRect(0, 0, st.canvas.width, st.canvas.height);"
    "    st.renderer.frame(st.ctx, plan, t, { scale: st.canvas.width / plan.W });"
    "    return JSON.stringify({ t: t });"
    "  };"
    "})();");

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
            err = mEngine->evaluate(QString::fromUtf8(stub.readAll()),
                                    QStringLiteral("jizura-stub.js")).toString();
        }
    }
    if (err.isEmpty()) {
        for (const QString &file : plannerSources()) {
            QFile f(file);
            f.open(QIODevice::ReadOnly);
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

void LyricPreviewWorker::renderStyle(const QString &styleKey,
                                     const quint32 seed,
                                     const qreal density,
                                     const int generation) {
    QString error;
    if (!ensureEngine(&error)) {
        Q_EMIT styleFailed(styleKey, generation, error);
        return;
    }
    // plan + renderer for this style
    const QString quoted = QString::fromUtf8(
                QJsonDocument(QJsonArray{styleKey})
                .toJson(QJsonDocument::Compact).mid(1).chopped(1));
    auto r = mEngine->evaluate(QStringLiteral(
        "__jzPlan(%1, %2, %3)").arg(quoted)
            .arg(seed).arg(QString::number(density, 'f', 3)));
    if (r.isError()) {
        Q_EMIT styleFailed(styleKey, generation, r.toString());
        return;
    }
    QVector<QImage> frames;
    frames.reserve(mFrames);
    for (int i = 0; i < mFrames; i++) {
        r = mEngine->evaluate(QStringLiteral("__jzFrame(%1, %2)")
                              .arg(i).arg(mFrames));
        if (r.isError()) {
            if (frames.isEmpty()) {
                Q_EMIT styleFailed(styleKey, generation, r.toString());
                return;
            }
            break; // keep whatever rendered; preview is best-effort
        }
        const auto canvasObj = mEngine->globalObject()
                .property(QStringLiteral("__jz"))
                .property(QStringLiteral("canvas"));
        const auto *canvas = qobject_cast<const JsCanvas2D *>(
                    canvasObj.toQObject());
        if (!canvas) {
            Q_EMIT styleFailed(styleKey, generation,
                               QStringLiteral("preview canvas missing"));
            return;
        }
        frames.append(canvas->image().copy());
    }
    Q_EMIT framesReady(styleKey, generation, frames);
}
