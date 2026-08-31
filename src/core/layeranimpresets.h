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

// Layer animation presets (shape / image / any-box "one-click"
// animations): each preset bakes keyframes onto the layer's box
// transform animator (position / rotation / scale / opacity).

#ifndef LAYERANIMPRESETS_H
#define LAYERANIMPRESETS_H

#include "core_global.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QImage>

class BoundingBox;
class AdvancedTransformAnimator;

struct CORE_EXPORT LayerAnimPreset {
    QString id;
    QString name;
    QString desc;
    int category = 0;   // 0 = one-shot (in / out), 2 = loop
    qreal duration = 1.0;   // seconds (one cycle for loops)

    // bakes the preset's keyframes; F0 = start frame, durF =
    // duration in frames, cw/ch = canvas size, bw/bh = content
    // bounds size (for off-canvas slides)
    void (*bake)(AdvancedTransformAnimator* const t, const int F0,
                 const int durF, const qreal cw, const qreal ch,
                 const qreal bw, const qreal bh);
    // exit-direction recipe; null when the preset is entrance-only
    void (*outBake)(AdvancedTransformAnimator* const t, const int F0,
                    const int durF, const qreal cw, const qreal ch,
                    const qreal bw, const qreal bh) = nullptr;
};

namespace LayerAnimPresets {
    CORE_EXPORT int count();
    CORE_EXPORT const QList<LayerAnimPreset>& all();
    CORE_EXPORT const LayerAnimPreset* byId(const QString& id);

    // bakes the preset's keyframes onto the box transform animator
    // starting at startFrame; out = exit direction (outBake)
    CORE_EXPORT void apply(BoundingBox* const box,
                           const LayerAnimPreset& preset,
                           const int startFrame,
                           const qreal fps,
                           const qreal durationScale,
                           const qreal canvasW,
                           const qreal canvasH,
                           const bool out = false);

    // renders the given preview frames of a (sceneless) sample box;
    // animates the box's own transform, auto-fitted like the text
    // preview (union bounds across frames). When content is non-null
    // it is drawn (aspect-fitted into the content bounds) instead of
    // the box path - used for the image-layer mascot proxy
    CORE_EXPORT QList<QImage> renderPreviewSequence(
            BoundingBox* const box,
            const QList<qreal>& frames,
            const QSize& imgSize,
            const QImage& content = QImage());
}

#endif // LAYERANIMPRESETS_H
