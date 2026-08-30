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
#ifdef HALFTONE
#undef HALFTONE
#endif
#ifdef INVERT
#undef INVERT
#endif
#ifdef MIRROR
#undef MIRROR
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
    PIXEL_ART
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
