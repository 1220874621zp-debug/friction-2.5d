#include "lyricmotionaudiocore.h"

#include <QHash>
#include <QtMath>
#include <algorithm>
#include <cmath>

LyricBpmResult LyricAudioCore::detectBpm(const QVector<qreal> &energy,
                                         const QVector<qreal> &flux,
                                         const qreal duration) {
    LyricBpmResult out;
    const int n = energy.size();
    if (n < 64 || flux.size() != n) { return out; }

    // onset: positive log-flux increment over the trailing 4-frame mean
    QVector<qreal> onset(n, 0.0);
    for (int f = 1; f < n; f++) {
        const qreal cur = std::log(1e-4 + flux[f]);
        qreal m = 0;
        int k = 0;
        for (int j = qMax(0, f - 4); j < f; j++) {
            m += std::log(1e-4 + flux[j]);
            k++;
        }
        onset[f] = qMax(0.0, cur - m / qMax(1, k));
    }

    // tempo: autocorrelation over 70-180 BPM with a log-gaussian prior
    constexpr qreal kRate = 50.0;
    const int minLag = qRound(kRate * 60.0 / 180.0);
    const int maxLag = qRound(kRate * 60.0 / 70.0);
    qreal best = 0;
    int bestLag = qRound(kRate * 0.5);
    QHash<int, qreal> scores;
    for (int lag = minLag; lag <= maxLag; lag++) {
        qreal s = 0;
        for (int f = lag; f < n; f++) { s += onset[f] * onset[f - lag]; }
        const qreal bpm = 60.0 * kRate / lag;
        s *= std::exp(-0.5 * std::pow(std::log2(bpm / 125.0) / 0.7, 2));
        scores.insert(lag, s);
        if (s > best) { best = s; bestLag = lag; }
    }
    // parabolic refinement
    qreal lagF = bestLag;
    if (scores.contains(bestLag - 1) && scores.contains(bestLag + 1)) {
        const qreal a = scores.value(bestLag - 1);
        const qreal b = scores.value(bestLag);
        const qreal c = scores.value(bestLag + 1);
        const qreal d = a - 2 * b + c;
        if (d != 0) { lagF = bestLag + 0.5 * (a - c) / d; }
    }
    const qreal period = lagF / kRate;
    out.bpm = qRound(60.0 / period * 10.0) / 10.0;

    // phase: first-beat scan at half-frame resolution
    qreal bestPh = 0, bestPS = -1;
    for (qreal ph = 0; ph < lagF; ph += 0.5) {
        qreal s = 0;
        for (qreal t = ph; t < n; t += lagF) {
            s += onset.value(qRound(t), 0.0);
        }
        if (s > bestPS) { bestPS = s; bestPh = ph; }
    }
    for (qreal t = bestPh / kRate; t < duration; t += period) {
        out.beats.append(t);
    }

    // energy: 95-percentile normalized
    QVector<qreal> sorted = energy;
    std::sort(sorted.begin(), sorted.end());
    qreal p95 = sorted.isEmpty() ? 1.0
              : sorted[qFloor(sorted.size() * 0.95)];
    if (p95 <= 0) { p95 = 1.0; }
    for (const qreal e : energy) { out.energy.append(qMin(1.0, e / p95)); }
    return out;
}
