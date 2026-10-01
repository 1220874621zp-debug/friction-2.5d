/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

#include "jsaudioanalyzer.h"

#include <cmath>
#include <vector>

#include <QJSEngine>
#include <QFileInfo>
#include <QString>

#include "FileCacheHandlers/audiostreamsdata.h"
#include "Sound/esoundsettings.h"

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/samplefmt.h>
    #include <libswresample/swresample.h>
}

namespace Friction
{
    namespace Core
    {
        namespace
        {
            QJSValue analyzerError(QJSEngine * const engine,
                                   const QString &filePath,
                                   const QString &error)
            {
                QJSValue obj = engine->newObject();
                obj.setProperty(QStringLiteral("ok"), false);
                obj.setProperty(QStringLiteral("error"), error);
                obj.setProperty(QStringLiteral("path"), filePath);
                return obj;
            }

            // Goertzel magnitude of `freq` (Hz) over samples [i0, i1),
            // normalized by window length
            float goertzelMagnitude(const std::vector<float> &samples,
                                    const int i0, const int i1,
                                    const double freq,
                                    const int sampleRate)
            {
                const int n = i1 - i0;
                if (n <= 0) { return 0.f; }
                const double omega = 2.*M_PI*freq/sampleRate;
                const double coeff = 2.*std::cos(omega);
                double s0 = 0., s1 = 0., s2 = 0.;
                for (int i = i0; i < i1; i++) {
                    s0 = samples[size_t(i)] + coeff*s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                const double power = s1*s1 + s2*s2 - coeff*s1*s2;
                if (power <= 0.) { return 0.f; }
                return float(std::sqrt(power)*2./n);
            }
        }

        QJSValue analyzeAudioFile(QJSEngine * const engine,
                                  const QString &filePath,
                                  const qreal fps,
                                  const int bands)
        {
            if (!engine) { return QJSValue(); }
            if (fps <= 0) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("fps must be > 0"));
            }
            if (!QFileInfo::exists(filePath)) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("file does not exist"));
            }

            stdsptr<AudioStreamsData> audio;
            try {
                audio = AudioStreamsData::sOpen(filePath);
            } catch (const std::exception &e) {
                return analyzerError(engine, filePath,
                                     QString::fromUtf8(e.what()));
            } catch (...) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("failed to open audio stream"));
            }
            if (!audio || !audio->fOpened) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("failed to open audio stream"));
            }

            const int sr = eSoundSettings::sSampleRate();
            const int chCount = eSoundSettings::sChannelCount();
            const bool planar = eSoundSettings::sPlanarFormat();
            const AVSampleFormat fmt = eSoundSettings::sSampleFormat();
            if (fmt != AV_SAMPLE_FMT_FLT && fmt != AV_SAMPLE_FMT_FLTP) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("unsupported sample format"));
            }
            if (chCount < 1) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("no audio channels"));
            }

            // decode the whole file sequentially, mixed down to mono
            const auto formatContext = audio->fFormatContext;
            const auto packet = audio->fPacket;
            const auto decodedFrame = audio->fDecodedFrame;
            const auto codecContext = audio->fCodecContext;
            const auto swrContext = audio->fSwrContext;
            const int audioStreamIndex = audio->fAudioStreamIndex;
            const auto codecPars = audio->fAudioStream->codecpar;
            const qreal dstPerSrc = sr/qreal(codecPars->sample_rate);

            std::vector<float> mono;
            while (true) {
                const int readRet = av_read_frame(formatContext, packet);
                if (readRet < 0) { break; }
                if (packet->stream_index != audioStreamIndex) {
                    av_packet_unref(packet);
                    continue;
                }
                const int sendRet = avcodec_send_packet(codecContext, packet);
                av_packet_unref(packet);
                if (sendRet < 0) { continue; }
                while (true) {
                    const int recRet = avcodec_receive_frame(codecContext,
                                                             decodedFrame);
                    if (recRet < 0) { break; } // EAGAIN / EOF / error
                    uchar **buffer = nullptr;
                    const int bufferSamples =
                            qCeil(decodedFrame->nb_samples*dstPerSrc);
                    int linesize = 0;
                    const int res = av_samples_alloc_array_and_samples(
                                &buffer, &linesize, chCount,
                                bufferSamples, fmt, 0);
                    if (res < 0) {
                        av_frame_unref(decodedFrame);
                        continue;
                    }
                    const int nDst = swr_convert(
                                swrContext, buffer, bufferSamples,
                                const_cast<const uint8_t**>(decodedFrame->data),
                                decodedFrame->nb_samples);
                    if (nDst > 0) {
                        const size_t oldSize = mono.size();
                        mono.resize(oldSize + size_t(nDst));
                        if (planar) {
                            for (int i = 0; i < nDst; i++) {
                                float v = 0.f;
                                for (int ch = 0; ch < chCount; ch++) {
                                    const auto chData =
                                        reinterpret_cast<const float*>(buffer[ch]);
                                    v += chData[i];
                                }
                                mono[oldSize + size_t(i)] = v/chCount;
                            }
                        } else {
                            const auto src =
                                reinterpret_cast<const float*>(buffer[0]);
                            for (int i = 0; i < nDst; i++) {
                                float v = 0.f;
                                for (int ch = 0; ch < chCount; ch++) {
                                    v += src[i*chCount + ch];
                                }
                                mono[oldSize + size_t(i)] = v/chCount;
                            }
                        }
                    }
                    if (buffer) { av_freep(&buffer[0]); }
                    av_freep(&buffer);
                    av_frame_unref(decodedFrame);
                }
            }

            if (mono.empty()) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("no audio samples decoded"));
            }

            const int nSamples = int(mono.size());
            const int frames = qFloor(nSamples*fps/sr);
            if (frames < 1) {
                return analyzerError(engine, filePath,
                                     QStringLiteral("audio shorter than one frame"));
            }

            QJSValue result = engine->newObject();
            result.setProperty(QStringLiteral("ok"), true);
            result.setProperty(QStringLiteral("path"), filePath);
            result.setProperty(QStringLiteral("duration"),
                               nSamples/qreal(sr));
            result.setProperty(QStringLiteral("sampleRate"), sr);
            result.setProperty(QStringLiteral("fps"), fps);
            result.setProperty(QStringLiteral("frames"), frames);

            // per-frame peak / rms
            QJSValue peakArr = engine->newArray(uint(frames));
            QJSValue rmsArr = engine->newArray(uint(frames));
            for (int f = 0; f < frames; f++) {
                const int i0 = qFloor(f*sr/fps);
                const int i1 = qMin(nSamples, qFloor((f + 1)*sr/fps));
                float peak = 0.f;
                double sumSq = 0.;
                for (int i = i0; i < i1; i++) {
                    const float v = mono[size_t(i)];
                    const float a = std::fabs(v);
                    if (a > peak) { peak = a; }
                    sumSq += double(v)*v;
                }
                const int n = qMax(1, i1 - i0);
                peakArr.setProperty(uint(f), peak);
                rmsArr.setProperty(uint(f), std::sqrt(sumSq/n));
            }
            result.setProperty(QStringLiteral("peak"), peakArr);
            result.setProperty(QStringLiteral("rms"), rmsArr);

            // log-spaced frequency bands (Goertzel), normalized 0..1
            const int nBands = qBound(0, bands, 256);
            QJSValue bandsArr = engine->newArray(uint(nBands));
            QJSValue freqsArr = engine->newArray(uint(nBands));
            if (nBands > 0) {
                const double fMin = 30.;
                const double fMax = qMin(16000., sr/2.*0.95);
                std::vector<std::vector<float>> bandData{size_t(nBands)};
                double globalMax = 0.;
                for (int b = 0; b < nBands; b++) {
                    const double t = nBands > 1
                            ? b/double(nBands - 1) : 0.;
                    const double freq = fMin*std::pow(fMax/fMin, t);
                    freqsArr.setProperty(uint(b), freq);
                    auto &values = bandData[size_t(b)];
                    values.resize(size_t(frames));
                    for (int f = 0; f < frames; f++) {
                        const int i0 = qFloor(f*sr/fps);
                        const int i1 = qMin(nSamples,
                                            qFloor((f + 1)*sr/fps));
                        const float mag = goertzelMagnitude(mono, i0, i1,
                                                            freq, sr);
                        values[size_t(f)] = mag;
                        if (mag > globalMax) { globalMax = mag; }
                    }
                }
                const double inv = globalMax > 0. ? 1./globalMax : 0.;
                for (int b = 0; b < nBands; b++) {
                    QJSValue bandArr = engine->newArray(uint(frames));
                    const auto &values = bandData[size_t(b)];
                    for (int f = 0; f < frames; f++) {
                        bandArr.setProperty(uint(f),
                                            values[size_t(f)]*inv);
                    }
                    bandsArr.setProperty(uint(b), bandArr);
                }
            }
            result.setProperty(QStringLiteral("bands"), bandsArr);
            result.setProperty(QStringLiteral("bandFreqs"), freqsArr);
            return result;
        }
    }
}
