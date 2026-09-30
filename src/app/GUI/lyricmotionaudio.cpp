#include "lyricmotionaudio.h"

#include "FileCacheHandlers/audiostreamsdata.h"
#include "Sound/esoundsettings.h"

#include <QHash>
#include <algorithm>
#include <QtMath>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace {

// streaming decode → 50 Hz energy/flux envelopes (single pass, no PCM
// buffering); mirrors JIZURA 10_audio.js analyzeAudio
bool buildEnvelopes(const stdsptr<AudioStreamsData> &audio,
                    QVector<qreal> &energy, QVector<qreal> &flux,
                    QString *error) {
    const int dstRate = eSoundSettings::sSampleRate();
    const int hop = qMax(1, qRound(qreal(dstRate) / 50.0));
    const int channels = eChannelCountFromMask(
                eSoundSettings::sChannelLayout());

    const auto formatContext = audio->fFormatContext;
    const auto audioStreamIndex = audio->fAudioStreamIndex;
    const auto packet = audio->fPacket;
    const auto decodedFrame = audio->fDecodedFrame;
    const auto codecContext = audio->fCodecContext;
    const auto swrContext = audio->fSwrContext;

    // per-frame resample output buffer (interleaved FLT)
    uchar *buffer = nullptr;
    int linesize = 0;

    qreal eAcc = 0, ehAcc = 0;
    qreal prevHP = 0, prevX = 0;
    int count = 0;
    bool eof = false;
    while (!eof) {
        const int readRet = av_read_frame(formatContext, packet);
        if (readRet < 0) { break; }
        if (packet->stream_index != audioStreamIndex) {
            av_packet_unref(packet);
            continue;
        }
        const int sendRet = avcodec_send_packet(codecContext, packet);
        av_packet_unref(packet);
        if (sendRet < 0 && sendRet != AVERROR(EAGAIN)) {
            if (error) { *error = QStringLiteral("decoder send failed"); }
            return false;
        }
        while (true) {
            const int recRet = avcodec_receive_frame(codecContext, decodedFrame);
            if (recRet == AVERROR(EAGAIN) || recRet == AVERROR_EOF) {
                if (recRet == AVERROR_EOF) { eof = true; }
                break;
            } else if (recRet < 0) {
                if (error) { *error = QStringLiteral("decode failed"); }
                return false;
            }
            const int bufferSamples = qCeil(
                        qreal(decodedFrame->nb_samples) * dstRate
                        / qreal(audio->fAudioStream->codecpar->sample_rate)) + 4;
            const int res = av_samples_alloc(&buffer, &linesize, 1,
                                             bufferSamples, AV_SAMPLE_FMT_FLT, 0);
            if (res < 0) {
                if (error) { *error = QStringLiteral("resample alloc failed"); }
                return false;
            }
            const int nDst = swr_convert(swrContext, &buffer, bufferSamples,
                        const_cast<const uint8_t **>(decodedFrame->data),
                        decodedFrame->nb_samples);
            if (nDst > 0) {
                const auto *samples = reinterpret_cast<const float *>(buffer);
                // stereo interleaved → mono on the fly
                for (int i = 0; i < nDst * 2; i += 2) {
                    const qreal x = (qreal(samples[i]) + qreal(samples[i + 1])) * 0.5;
                    eAcc += x * x;
                    const qreal hp = 0.92 * (prevHP + x - prevX);
                    prevHP = hp;
                    prevX = x;
                    ehAcc += hp * hp;
                    if (++count >= hop) {
                        energy.append(std::sqrt(eAcc / hop));
                        flux.append(std::sqrt(ehAcc / hop));
                        eAcc = ehAcc = 0;
                        count = 0;
                    }
                }
            }
            av_freep(&buffer);
            av_frame_unref(decodedFrame);
        }
    }
    if (count > 0) { // trailing partial frame
        energy.append(std::sqrt(eAcc / count));
        flux.append(std::sqrt(ehAcc / count));
    }
    return true;
}

} // namespace

bool LyricAudioAnalyzer::analyze(const QString &path,
                                 LyricAudioAnalysis &out,
                                 QString *error) {
    out = LyricAudioAnalysis();
    const stdsptr<AudioStreamsData> audio = AudioStreamsData::sOpen(path);
    if (!audio || !audio->fOpened) {
        if (error) { *error = QStringLiteral("无法打开音频文件"); }
        return false;
    }
    out.duration = audio->fDurationSec;

    QVector<qreal> energy, flux;
    if (!buildEnvelopes(audio, energy, flux, error)) { return false; }
    const int n = energy.size();
    if (n < 64) {
        if (error) { *error = QStringLiteral("音频过短"); }
        return false;
    }

    const LyricBpmResult result =
            LyricAudioCore::detectBpm(energy, flux, out.duration);
    out.bpm = result.bpm;
    out.beats = result.beats;
    out.energy = result.energy;

    out.valid = true;
    return true;
}
