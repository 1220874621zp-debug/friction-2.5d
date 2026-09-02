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
*/

// Layer animation presets (shape / image / any-box "one-click"
// animations): each preset ATTACHES GENERATED EXPRESSIONS to the
// layer's box transform sub-animators (position x/y, rotation, scale
// x/y, opacity) instead of baking keyframes - the timeline stays
// clean and loop presets run forever.

#ifndef LAYERANIMPRESETS_H
#define LAYERANIMPRESETS_H

#include "core_global.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QImage>

class BoundingBox;
class AdvancedTransformAnimator;

// everything a script generator needs: rel-frame windows (-1 = that
// direction is not applied), duration in frames, canvas / content
// sizes and the rest-state base values of the transform
struct CORE_EXPORT LayerExprParams {
    qreal inF0 = -1;
    qreal outF0 = -1;
    int D = 2;           // window length in frames (both directions)
    qreal cw = 0, ch = 0;    // canvas size
    qreal bw = 2, bh = 2;    // content bounds size
    qreal px = 0, py = 0;    // rest position
    qreal sx = 1, sy = 1;    // rest scale
    qreal rot = 0;           // rest rotation (deg)
    qreal op = 100;          // rest opacity (0..100)
    qreal winF0() const { return inF0 >= 0 ? inF0 : outF0; }
};

// per-animator expression script bodies (JS, referencing `f` = the
// frame); empty strings mean the preset does not touch that animator
struct CORE_EXPORT LayerExprScripts {
    QString posX, posY, scaleX, scaleY, rot, op;
};

struct CORE_EXPORT LayerAnimPreset {
    QString id;
    QString name;
    QString desc;
    int category = 0;   // 0 = one-shot (in / out), 2 = loop
    qreal duration = 1.0;   // seconds (one cycle for loops)

    // generates the entrance / exit scripts for the animators the
    // preset touches; the active window comes from winF0()
    void (*gen)(const LayerExprParams&, LayerExprScripts&) = nullptr;
    // exit-direction generator; null when the preset is entrance-only
    void (*outGen)(const LayerExprParams&, LayerExprScripts&) = nullptr;
};

namespace LayerAnimPresets {
    CORE_EXPORT int count();
    CORE_EXPORT const QList<LayerAnimPreset>& all();
    CORE_EXPORT const LayerAnimPreset* byId(const QString& id);

    // attaches the preset's generated expressions to the box
    // transform sub-animators; inStartFrame / outStartFrame are the
    // entrance / exit window starts in ABS frames (-1 = direction not
    // applied). action = undoable attach (real layers) vs plain
    // attach (sceneless preview boxes). When both windows are given
    // the exit window takes over from its start frame on.
    CORE_EXPORT void apply(BoundingBox* const box,
                           const LayerAnimPreset& preset,
                           const int inStartFrame,
                           const int outStartFrame,
                           const qreal fps,
                           const qreal durationScale,
                           const qreal canvasW,
                           const qreal canvasH,
                           const bool action = true);

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
