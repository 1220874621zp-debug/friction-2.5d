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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#ifndef EVFORMAT_H
#define EVFORMAT_H

namespace EvFormat {
    enum {
        dataCompression = 16,
        textSkFont = 17,
        oilEffectImprov = 18,
        betterSWTAbsReadWrite = 19,
        readSceneSettingsBeforeContent = 20,
        relativeFilePathSave = 21,
        flipBook = 22,
        colorizeInfluence = 23,
        transformEffects = 24,
        transformEffects2 = 25,
        codecProfile = 26,
        effectCustomName = 27,
        markers = 28,
        svgBeginEnd = 29,
        formatOptions = 30,
        formatOptions2 = 31,
        subPathOffset = 32,
        avStretch = 33,
        grid = 34,
        v100 = 35,
        boxLayerSwitches = 36,
        layerLabelColor = 37,
        // trackIds and trackRows deliberately share the value 38: v38
        // files write mTrackId TWICE (see eBoxOrSound::prp_writeProperty_
        // impl). Never bump one without the other - the readers are gated
        // on these two separately, so a lone bump shifts the whole stream
        trackIds = 38,
        trackRows = 38,
        // per-layer motion blur switch (eBoxOrSound) - shared with the
        // track matte fields written by BoundingBox in the same version
        layerFxColumns = 39,
        trackMatte = 39,
        // bone warp era: the effect itself has since been REMOVED
        // (binding is reparent-based now); dev-era files that still
        // carry a bone warp effect are not loadable - re-create those
        // rigs (development artifacts only)
        boneWarpRemoved = 40,
        // keyframable layer visibility ("可见" bool animator) became a
        // serialized child of every eBoxOrSound; older files lack the
        // block and must skip it (positional binary property tree)
        visibilityKeyframes = 41,
        // Moho-style switch group marker (ContainerBox child property);
        // older files lack the block and must skip it (positional)
        switchLayers = 42,
        // PS-style ruler guides (Canvas settings tail: h/v lists);
        // older files lack the block and must skip it (positional)
        canvasGuides = 43,
        // AE-style mask flag on SmartVectorPath (appended byte in
        // writeBoundingBox, after the base box block); older files
        // lack the byte and must skip it (positional)
        maskPathMode = 44,
        // PSD clipping-mask member flag on PsdImageBox (appended byte
        // in writeBoundingBox, after the package/layerKey block); older
        // files lack the byte and must skip it (positional)
        psdClippingMask = 45,

        nextVersion
    };

    const int version = nextVersion - 1;
}

#endif // EVFORMAT_H
