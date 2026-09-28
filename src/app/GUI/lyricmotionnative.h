#ifndef LYRICMOTIONNATIVE_H
#define LYRICMOTIONNATIVE_H

#include <QString>
#include <QStringList>
#include <QJsonObject>

class Canvas;
class LyricMotionEngine;

// Native construction of a JIZURA plan inside a friction scene: every
// cut becomes a container group holding real text boxes animated with
// TextEffect presets, raster effects, keyframes and shape primitives.
// No baked images — the whole lyric video stays editable (family-level
// recipes; unknown part keys fall back to their family and are counted
// in Result::substitutions).
class LyricMotionNative
{
public:
    struct Result {
        int cutsBuilt = 0;
        int substitutions = 0; // part keys that fell back to a recipe
        QStringList notes;     // human-readable mapping notes
    };

    // plan: the "plan" object of the engine JSON (cuts/events/fx/style);
    // fonts: the top-level fonts table of the same JSON.
    // params: the original planning inputs; when present the builder
    // first REPLAYS the web renderer per cut (midpoint frame) and
    // materializes its draw calls as editable layers, falling back to
    // the hand-written recipes for cuts that render nothing
    static bool build(Canvas * const scene,
                      const QJsonObject &plan,
                      const QJsonObject &fonts,
                      const QString &audioPath,
                      const bool includeAudio,
                      Result * const result,
                      QString * const error,
                      const void * const rawParams = nullptr);
};

#endif // LYRICMOTIONNATIVE_H
