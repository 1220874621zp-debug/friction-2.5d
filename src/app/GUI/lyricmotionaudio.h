#ifndef LYRICMOTIONAUDIO_H
#define LYRICMOTIONAUDIO_H

#include <QString>
#include <QVector>
#include "lyricmotionaudiocore.h"

// Audio analysis for lyric planning: streaming FFmpeg decode (via
// AudioStreamsData, no full PCM buffer is ever held) → 50 Hz energy /
// high-passed flux envelopes → the JIZURA BPM detector in
// lyricmotionaudiocore (10_audio.js port).
struct LyricAudioAnalysis
{
    bool valid = false;
    qreal bpm = 0;
    qreal duration = 0;
    QVector<qreal> beats;   // seconds
    QVector<qreal> energy;  // 50 Hz, 95-percentile normalized 0..1
};

namespace LyricAudioAnalyzer
{
bool analyze(const QString &path, LyricAudioAnalysis &out,
             QString *error = nullptr);
}

#endif // LYRICMOTIONAUDIO_H
