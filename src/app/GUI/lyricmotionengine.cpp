#include "lyricmotionengine.h"

#include "Scripting/jsapi.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// evaluated before anything else so the vendored browser sources see a
// (very small) DOM and the runtime builtins this QJSEngine lacks
const auto kStubResource = QStringLiteral(":/jizura/jizura-stub.js");

QStringList plannerSources(QString *error) {
    QStringList files;
    QDirIterator it(QStringLiteral(":/jizura"), {QStringLiteral("*.js")},
                    QDir::Files);
    while (it.hasNext()) { files << it.next(); }
    files.sort();
    // the stub is evaluated explicitly first
    files.removeAll(kStubResource);
    if (files.isEmpty()) {
        if (error) { *error = QStringLiteral("no planner sources under :/jizura"); }
    }
    return files;
}

// embeds a JSON document as a JS string literal (the same quoting
// trick McpDispatcher uses for user code)
QString jsStringLiteral(const QString &value) {
    const QByteArray bytes = QJsonDocument(QJsonArray{value})
            .toJson(QJsonDocument::Compact);
    return QString::fromUtf8(bytes.mid(1).chopped(1));
}

// params → JSON text handed to the JS side
QString paramsToJson(const LyricMotionEngine::Params &p) {
    QJsonObject o;
    o.insert(QStringLiteral("lyrics"), p.lyrics);
    o.insert(QStringLiteral("style"), p.style);
    o.insert(QStringLiteral("mood"), p.mood);
    o.insert(QStringLiteral("seed"), static_cast<double>(p.seed));
    o.insert(QStringLiteral("density"), p.density);
    o.insert(QStringLiteral("bpm"), p.bpm);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

LyricMotionEngine::LyricMotionEngine(QObject * const parent) :
    QObject(parent) {}

LyricMotionEngine::~LyricMotionEngine() {
    // JsHost owns the engine; garbage is not collected on destruction
    if (mHost) { mHost.reset(); }
}

bool LyricMotionEngine::ensureLoaded(QString *error) {
    if (mLoaded) { return true; }
    mHost = std::make_unique<Friction::Core::JsHost>(this);
    mHost->setHandlers(
        [](const QString &) {}, // print: planning is silent
        [](const QString &) {}, // alert: never used by the planner
        [](const QString &) { return false; });

    QString err;
    {
        QFile stub(kStubResource);
        stub.open(QIODevice::ReadOnly);
        if (!stub.isOpen()) {
            err = QStringLiteral("cannot open %1").arg(kStubResource);
        } else {
            err = mHost->evaluate(QString::fromUtf8(stub.readAll()));
            if (!err.isEmpty() && err.startsWith(QStringLiteral("Uncaught"))) {
                err = QStringLiteral("stub: %1").arg(err);
            } else { err.clear(); }
        }
    }
    if (err.isEmpty()) {
        for (const QString &file : plannerSources(&err)) {
            if (!err.isEmpty()) { break; }
            err = mHost->loadScript(file);
            if (!err.isEmpty()) {
                err = QStringLiteral("%1: %2")
                        .arg(QFileInfo(file).fileName(), err);
            }
        }
    }
    if (!err.isEmpty()) {
        if (error) { *error = err; }
        mHost.reset();
        return false;
    }
    if (!collectCatalog(&err)) {
        if (error) { *error = err; }
        mHost.reset();
        return false;
    }
    mLoaded = true;
    return true;
}

bool LyricMotionEngine::collectCatalog(QString *error) {
    // style catalog: key, display name, tags and the first scheme's
    // swatch colors (the cards only need these; the plan itself
    // carries the full schemes for apply)
    QString err;
    const QString src = QStringLiteral(
        "(function(){"
        "  return JSON.stringify(Object.keys(J.STYLES).map(function(k){"
        "    var s = J.STYLES[k]; var sc = s.schemes && s.schemes[0] ? s.schemes[0] : {};"
        "    return { key: k, name: s.name || k, tags: s.tags || [],"
        "      bg: sc.bg || '#101010', fg: sc.fg || '#FFFFFF',"
        "      accent: sc.accent || '#FFFFFF' };"
        "  }));"
        "})()");
    const QString json = runJs(src, &err);
    if (!err.isEmpty()) {
        if (error) { *error = QStringLiteral("styles: %1").arg(err); }
        return false;
    }
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    mStyles.clear();
    for (const auto v : doc.array()) {
        const auto o = v.toObject();
        LyricMotionEngine::StyleInfo info;
        info.key = o.value(QStringLiteral("key")).toString();
        info.name = o.value(QStringLiteral("name")).toString();
        info.bg = QColor(o.value(QStringLiteral("bg")).toString());
        info.fg = QColor(o.value(QStringLiteral("fg")).toString());
        info.accent = QColor(o.value(QStringLiteral("accent")).toString());
        for (const auto t : o.value(QStringLiteral("tags")).toArray()) {
            info.tags << t.toString();
        }
        if (!info.key.isEmpty()) { mStyles << info; }
    }

    const QString moodSrc = QStringLiteral(
        "(function(){"
        "  return JSON.stringify(Object.keys(J.MOODS).map(function(k){"
        "    return { key: k, name: J.MOODS[k].name || k };"
        "  }));"
        "})()");
    const QString moodJson = runJs(moodSrc, &err);
    if (!err.isEmpty()) {
        if (error) { *error = QStringLiteral("moods: %1").arg(err); }
        return false;
    }
    mMoods.clear();
    for (const auto v : QJsonDocument::fromJson(moodJson.toUtf8()).array()) {
        const auto o = v.toObject();
        // "key|name" — the panel shows the name, keeps the key
        mMoods << QStringLiteral("%1|%2")
                .arg(o.value(QStringLiteral("key")).toString(),
                     o.value(QStringLiteral("name")).toString());
    }
    return true;
}

QString LyricMotionEngine::runJs(const QString &source, QString *error) {
    const QString result = mHost->evaluate(source, 30000);
    if (result.startsWith(QStringLiteral("Uncaught"))) {
        if (error) { *error = result; }
        return QString();
    }
    return result;
}

QString LyricMotionEngine::planJson(const Params &params, QString *error) {
    if (!ensureLoaded(error)) { return QString(); }
    // mirrors 12_ui.js audioLike(): no audio, optional manual BPM grid
    // (600 s span covers any song the panel can plan ahead of loading)
    const QString src = QStringLiteral(
        "(function(){"
        "  var p = JSON.parse(%1);"
        "  var pr = J.defaultProject();"
        "  pr.lyrics = p.lyrics;"
        "  pr.style = p.style;"
        "  pr.mood = p.mood || null;"
        "  pr.seed = p.seed;"
        "  pr.fx.density = p.density;"
        "  pr.timing.bpm = p.bpm;"
        "  var audio = null;"
        "  if (p.bpm > 0) audio = { beats: J.beatGrid(p.bpm, pr.timing.offset || 0, 600) };"
        "  var plan = J.plan(pr, audio);"
        "  var fonts = {};"
        "  for (var k in J.FONTS) fonts[k] = { label: J.FONTS[k].label,"
        "    family: J.FONTS[k].family, weight: J.FONTS[k].weight,"
        "    kind: J.FONTS[k].kind };"
        "  return JSON.stringify({ plan: plan, fonts: fonts });"
        "})()").arg(jsStringLiteral(paramsToJson(params)));
    return runJs(src, error);
}

QString LyricMotionEngine::omakaseJson(const Params &params, QString *error) {
    if (!ensureLoaded(error)) { return QString(); }
    // seeded LCG stands in for Math.random so "one-shot random" stays
    // reproducible for a given seed
    const QString src = QStringLiteral(
        "(function(){"
        "  var p = JSON.parse(%1);"
        "  var pr = J.defaultProject();"
        "  pr.lyrics = p.lyrics; pr.style = p.style; pr.mood = p.mood || null;"
        "  pr.seed = p.seed; pr.fx.density = p.density;"
        "  var s = p.seed >>> 0;"
        "  var rnd = function(){ s = (s * 1103515245 + 12345) >>> 0;"
        "    return (s >>> 8) / 16777216; };"
        "  return JSON.stringify(J.omakase(pr, rnd));"
        "})()").arg(jsStringLiteral(paramsToJson(params)));
    return runJs(src, error);
}
