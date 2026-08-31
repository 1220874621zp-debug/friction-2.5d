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

// Text animation presets (AE "text animator"-style one-click recipes).
// Each preset bakes a fully configured TextEffect (keyframed influence,
// diminish sweep front and/or periodic wave) onto a TextBox.

#ifndef TEXTANIMPRESETS_H
#define TEXTANIMPRESETS_H

#include "texteffect.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QImage>

class TextBox;

// how the sweep front travels across the text
enum class TextAnimDirection : short {
    leftToRight, rightToLeft
};

struct CORE_EXPORT TextAnimPreset {
    QString id;
    QString name;
    QString desc;
    int category = 0;   // 0 = one-shot (in / out), 2 = loop
    int fragment = 0;   // matches the TextEffect target combo: 0 letters, 1 words, 2 lines
    bool byIndex = true;// stagger by fragment index (AE feel) instead of x position
    TextAnim::Kind kind = TextAnim::sweepIn;

    // transform "start state" applied at full influence
    // (influence animates from 1 -> 0 for entrances, so these describe
    // where the text comes FROM; loops oscillate around the rest state)
    qreal posX = 0;     // px, y+ is down
    qreal posY = 0;
    qreal rot = 0;      // degrees
    qreal scaleX = 1;
    qreal scaleY = 1;
    qreal opacity = 100;// %
    bool pivotCenter = false; // pivot near the fragment visual center

    // sweep timing / shape
    qreal duration = 1.0;     // seconds
    qreal softness = 0.25;    // sweep soft edge, fraction of text width
    TextAnimDirection direction = TextAnimDirection::leftToRight;

    // wave (loop)
    int waveCycles = 2;       // periods across the text width
    qreal waveTime = 1.2;     // seconds for the wave to travel one period

    // pulse (loop): influence keys 0 -> peak -> 0, half-cycle = duration
    qreal pulsePeak = 1;

    // one-shot sweeps can be applied as an entrance (in) or exit
    // (out); loops and number-roll ignore the direction
    bool supportsOut = true;
};

namespace TextAnimPresets {
    CORE_EXPORT int count();
    CORE_EXPORT const QList<TextAnimPreset>& all();
    CORE_EXPORT const TextAnimPreset* byId(const QString& id);

    // creates a fully configured TextEffect and adds it to the box's
    // text effect collection (single undoable step); keyframes start
    // at startFrame, durations are scaled by durationScale.
    // returns false when the preset could not be applied (e.g.
    // number-roll on non-numeric text)
    CORE_EXPORT bool apply(TextBox* const box,
                           const TextAnimPreset& preset,
                           const int startFrame,
                           const qreal fps,
                           const qreal durationScale = 1.0,
                           const bool out = false);

    // renders the given preview frames of a (sceneless) TextBox with
    // its current text effects applied; frames are relFrame values on
    // a preview timeline (0 = rest). All frames share one auto-fit
    // transform computed from the union bounds of every frame, so the
    // text never jumps between frames.
    CORE_EXPORT QList<QImage> renderPreviewSequence(
            TextBox* const box,
            const QList<qreal>& frames,
            const QSize& imgSize);
}

#endif // TEXTANIMPRESETS_H
