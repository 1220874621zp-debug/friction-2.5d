#ifndef LYRICMOTIONAUDIOCORE_H
#define LYRICMOTIONAUDIOCORE_H

#include <QString>
#include <QVector>

// Pure BPM-detection algorithm (no decoder dependency): a C++ port of
// JIZURA's 10_audio.js — onset strength from a high-passed energy
// flux, 70-180 BPM autocorrelation with a log-gaussian 125 BPM prior,
// parabolic lag refinement, half-frame phase scan, 95-percentile
// energy normalization. Input: 50 Hz envelopes over the whole track.
struct LyricBpmResult
{
    qreal bpm = 0;
    QVector<qreal> beats;   // seconds
    QVector<qreal> energy;  // normalized 0..1 at 50 Hz
};

namespace LyricAudioCore
{
// energy/flux: per-50Hz-frame RMS of the signal and of its first-order
// high-pass; duration: track length in seconds
LyricBpmResult detectBpm(const QVector<qreal> &energy,
                         const QVector<qreal> &flux,
                         const qreal duration);
}

#endif // LYRICMOTIONAUDIOCORE_H
