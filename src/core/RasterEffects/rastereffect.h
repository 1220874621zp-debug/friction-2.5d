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

#ifndef RASTEREFFECT_H
#define RASTEREFFECT_H
#include "../Animators/eeffect.h"
#include "../glhelpers.h"
#include "rastereffectcaller.h"

// Windows GDI defines these as macros
#ifdef INVERT
#undef INVERT
#endif
#ifdef HALFTONE
#undef HALFTONE
#endif
#ifdef MIRROR
#undef MIRROR
#endif
#ifdef NOISE
#undef NOISE
#endif
#ifdef TINT
#undef TINT
#endif
#ifdef RAIN
#undef RAIN
#endif
#ifdef SHAKE
#undef SHAKE
#endif
#ifdef STRIPE
#undef STRIPE
#endif
#ifdef GLOW
#undef GLOW
#endif
#ifdef CHROMA_KEY
#undef CHROMA_KEY
#endif
#ifdef TRANSPARENT
#undef TRANSPARENT
#endif
#ifdef OPAQUE
#undef OPAQUE
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef ECHO
#undef ECHO
#endif

enum class RasterEffectType : short {
    BLUR,
    SHADOW,
    CUSTOM, // C++
    CUSTOM_SHADER, // xml, GLSL
    MOTION_BLUR,
    WIPE,
    // 6 was BONE_WARP (removed) - kept reserved so the serialized ids
    // of the effects below do not shift against old project files
    NOISE_FADE = 7,
    COLORIZE,
    BRIGHTNESS_CONTRAST,
    CHROMA_KEY,      // 10, as in every build since the chroma-key commit
    LIQUID_GLASS,    // 11, ditto - keep 0..11 stable for saved projects
    // ported AE effect family re-appended after the fork's own slots
    // (merge fix: the rebase dropped these while their .cpps stayed)
    VIGNETTE,
    CHROMATIC_ABERRATION,
    LETTERBOX,
    SCANLINES,
    GLOW,
    DIRECTIONAL_BLUR,
    RADIAL_BLUR,
    WAVE_WARP,
    RAIN,
    EDGE_DETECT,
    INVERT,
    TINT,
    PIXELATE,
    NOISE,
    MIRROR,
    GLITCH,
    POSTERIZE,
    TWIRL,
    CHANNEL_BLUR,
    HALFTONE,
    SHAKE,
    DROP_SHADOW,
    ZOOM_BLUR,
    COLOR_GRADING,
    STRIPE,
    MOTION_TILE,
    FRACTAL_NOISE,
    LIGHT_SWEEP,
    DISPLACEMENT_WARP,
    FILM_GRAIN,
    BLACK_WHITE_FLASH,
    PIXEL_ART,
    // Photoshop-style layer styles container (shadow/glow/stroke);
    // appended last, never reorder - serialized ids must stay stable
    LAYER_STYLES,
    SHATTER,
    SMEAR,
    ROUGHEN_EDGES,
    // analytic particle emitter (stateless, per-frame deterministic)
    PARTICLE,
    // cylindrical page curl with N.L shading (Foldspace-style roll)
    PAGE_CURL,
    // AE Putty-style lattice (FFD) deformation - appended last,
    // never reorder, serialized ids must stay stable
    LATTICE_WARP,
    // flat-cel region segmentation + volumetric 4-stop gradient
    // (appended, never reorder - serialized ids must stay stable)
    CEL_VOLUME,
    // AE Threshold: binarize luminance at a level percent
    THRESHOLD,
    // AE Simple Choker: choke/spread the alpha matte
    SIMPLE_CHOKER,
    // AE Black & White / PS Desaturate: collapse to Rec.601 luminance
    DESATURATE,
    // PS Levels: input black/white points + midtone gamma + output
    // black/white points, per channel or composite (appended last,
    // never reorder - serialized ids must stay stable)
    LEVELS,
    // AE Hue/Saturation (master): hue rotation + saturation and
    // lightness scaling (appended last, never reorder - serialized
    // ids must stay stable)
    HUE_SATURATION,
    // AE Gradient Ramp: two-color linear/radial gradient generator
    // (appended, never reorder - serialized ids must stay stable)
    RAMP,
    // AE Grid: anchored grid-line generator (appended, never
    // reorder - serialized ids must stay stable)
    GRID,
    // AE Echo: composite past/future frame samples under the current
    // frame with decaying intensity (appended last, never reorder -
    // serialized ids must stay stable)
    ECHO,
    // AE Corner Pin: four-point perspective fit (screen replacement);
    // appended last, never reorder - serialized ids must stay stable
    CORNER_PIN,
    // AE Camera Lens Blur: defocus blur + thresholded highlight bokeh
    // (appended last, never reorder - serialized ids must stay stable)
    CAMERA_LENS_BLUR,
    // AE Set Matte: mask this layer with another layer's alpha/luma,
    // as an effect-stack entry (appended last, never reorder -
    // serialized ids must stay stable)
    SET_MATTE
};

struct BoxRenderData;

class CORE_EXPORT RasterEffect : public eEffect {
    e_OBJECT
    Q_OBJECT
protected:
    RasterEffect(const QString &name,
                 const HardwareSupport hwSupport,
                 const bool hwInterchangeable,
                 const RasterEffectType type);
public:
    virtual stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame,
            const qreal resolution,
            const qreal influence,
            BoxRenderData * const data) const = 0;

    virtual bool forceMargin() const { return false; }
    virtual QMargins getMargin() const { return QMargins(); }

    QMimeData *SWT_createMimeData() final;

    void prp_setupTreeViewMenu(PropertyMenu * const menu);

    void writeIdentifier(eWriteStream& dst) const;
    void writeIdentifierXEV(QDomElement& ele) const;

    // AE-style reset: rebuild a factory-default instance of this
    // effect's type and copy its parameter values over (undoable)
    void resetToDefault();

    RasterEffectType getEffectType() const {
        return mType;
    }

    HardwareSupport instanceHwSupport() const {
        return mInstHwSupport;
    }

    void switchInstanceHwSupport();
signals:
    void hardwareSupportChanged();
    void forcedMarginChanged();
private:
    const RasterEffectType mType;
    const HardwareSupport mTypeHwSupport;
    const bool mHwInterchangeable;
    HardwareSupport mInstHwSupport;
};

#endif // RASTEREFFECT_H
