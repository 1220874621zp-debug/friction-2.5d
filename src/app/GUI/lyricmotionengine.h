#ifndef LYRICMOTIONENGINE_H
#define LYRICMOTIONENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QHash>
#include <QList>
#include <memory>

namespace Friction {
namespace Core {
class JsHost;
}
}

// Bridge to the vendored JIZURA planner (src/app/jizura/, MIT,
// (c) 852wa): loads the planner sources into a dedicated JsHost
// (QJSEngine) and exposes lyric planning + style metadata to the
// LyricMotionPanel. Pure planning — nothing here touches the render
// path; results are consumed by the panel's apply-to-scene step.
class LyricMotionEngine : public QObject
{
    Q_OBJECT
public:
    struct StyleInfo {
        QString key;
        QString name;
        QStringList tags;
        QColor bg, fg, accent; // first scheme, for the swatch cards
    };
    struct Params {
        QString lyrics;
        QString style = QStringLiteral("noir");
        QString mood;               // empty = planner's default
        quint32 seed = 1;
        qreal density = 0.55;
        qreal bpm = 0;              // 0 = no beat grid
        QVector<qreal> beats;       // detected beats (overrides bpm grid)
        qreal audioDuration = 0;    // keeps the plan as long as the song
        qreal chroma = -1;          // <0 = keep the style default
    };

    explicit LyricMotionEngine(QObject * const parent = nullptr);
    ~LyricMotionEngine() override;

    // loads stub + planner sources from :/jizura; cheap to call again
    bool ensureLoaded(QString *error = nullptr);
    bool isLoaded() const { return mLoaded; }

    const QList<StyleInfo> &styles() const { return mStyles; }
    const QStringList &moodNames() const { return mMoods; }

    // display name of a part (layout/enter/hold/exit/decor/... key) as
    // registered by the vendored JIZURA engine; falls back to the key
    QString partName(const QString &group, const QString &key) const;

    // runs J.plan for the params; returns the full plan as JSON
    // (cuts/lines/style.schemes/...), empty string on error
    QString planJson(const Params &params, QString *error = nullptr);

    // runs J.omakase against a params-derived project; returns the
    // suggested look {mood, style, fx, colors, seed...} as JSON
    QString omakaseJson(const Params &params, QString *error = nullptr);

    // persisted style/mood catalog (JSON in AppSupport settings):
    // lets the panel build its gallery instantly at startup instead
    // of synchronously compiling the planner on the GUI thread; the
    // engine refreshes the cache after every successful load
    QString catalogJson() const;
    static bool catalogFromJson(const QString &json,
                                QList<StyleInfo> * styles,
                                QStringList * moods);

private:
    QString runJs(const QString &source, QString *error);
    bool collectCatalog(QString *error);

    std::unique_ptr<Friction::Core::JsHost> mHost;
    bool mLoaded = false;
    QList<StyleInfo> mStyles;
    QStringList mMoods;
    QHash<QString, QHash<QString, QString>> mPartNames;
};

#endif // LYRICMOTIONENGINE_H
