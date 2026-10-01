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

#ifndef JSAUDIOANALYZER_H
#define JSAUDIOANALYZER_H

#include "core_global.h"

#include <QJSValue>

class QJSEngine;
class QString;

namespace Friction
{
    namespace Core
    {
        // Synchronous FFmpeg audio analysis for the script engine
        // (music visualization: Audio Spectrum / Waveform scripts).
        //
        // Decodes the whole file (resampled to the global sound
        // settings, mixed down to mono) and computes per frame
        // window (sampleRate/fps samples):
        //   peak[f] - max |sample| in the window (0..1+)
        //   rms[f]  - root mean square in the window (0..1+)
        //   bands[b][f] - energy of log-spaced frequency band b in
        //                 frame f (Goertzel), normalized per file
        //                 so the loudest band/frame equals 1
        //
        // The returned JS object has the shape:
        // { ok: true, path, duration, sampleRate, fps, frames,
        //   peak: [f...], rms: [f...],
        //   bands: [[f...] x bands], bandFreqs: [b...] }
        // or { ok: false, error, path } on failure.
        CORE_EXPORT QJSValue analyzeAudioFile(QJSEngine * const engine,
                                              const QString &filePath,
                                              const qreal fps,
                                              const int bands);
    }
}

#endif // JSAUDIOANALYZER_H
