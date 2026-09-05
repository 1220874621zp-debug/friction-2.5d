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

#ifndef PSDIMAGEBOX_H
#define PSDIMAGEBOX_H

// Image box bound to a .fpsd package layer.
//
// Carries the package path + the Photoshop native layer id, so a
// single layer can be re-read from the source PSD and its pixels
// replaced inside the package / pixel cache without touching any
// animation data (transform, keyframes, effects, blend mode...).

#include "Boxes/imagebox.h"

class CORE_EXPORT PsdImageBox : public ImageBox {
    Q_OBJECT
    e_OBJECT
    e_DECLARE_TYPE(PsdImageBox)
protected:
    PsdImageBox();
    PsdImageBox(const QString &filePath,
                const QString &sourcePackage,
                const QString &sourceLayerKey);

    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp);
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;
public:
    void setupCanvasMenu(PropertyMenu * const menu);

    void writeBoundingBox(eWriteStream& dst) const;
    void readBoundingBox(eReadStream& src);

    void setupRenderData(const qreal relFrame,
                         const QMatrix& parentM,
                         BoxRenderData * const data,
                         Canvas* const scene);

    void setPsdSource(const QString &sourcePackage,
                      const QString &sourceLayerKey);

    // PSD clipping-mask membership (independent of the preserve-alpha
    // switch: the flag says "this layer belongs to a clipped stack",
    // the switch says "the clip is enabled"). Fellow clipping layers
    // are skipped by the preserve-alpha source search so the whole
    // stack clips to the one shared base below it.
    void setClippingMask(const bool clip);
    bool isClippingMaskLayer() const override
    { return mClippingMask; }

    // Re-extract this single layer from the source PSD, replace its
    // pixels inside the package and the pixel cache, then reload the
    // texture. Keeps all animation data.
    bool updateFromSource();

    // Scan the source PSD for ALL layers of the package, update every
    // bound box in the scene, mark removed layers and offer to import
    // newly added ones.
    void syncAllFromSource();

    const QString& sourcePackage() const { return mSourcePackage; }
    const QString& sourceLayerKey() const { return mSourceLayerKey; }

    // If the pixel cache file is gone (cache cleared), re-extract it
    // from the .fpsd package. Public so the bone-layer conversion can
    // re-ensure pixels after moving layers around.
    bool ensureCachedFile();
private:
    QString mSourcePackage;
    QString mSourceLayerKey;
    bool mClippingMask = false;
};

#endif // PSDIMAGEBOX_H
