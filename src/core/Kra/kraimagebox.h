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

#ifndef KRAIMAGEBOX_H
#define KRAIMAGEBOX_H

// Image box bound to a paint layer of a source .kra document.
//
// Carries the kra path + the layer uuid + the frame file name, so a
// single layer can be re-read from the source document and its pixels
// replaced in the pixel cache without touching any animation data
// (transform, keyframes, effects, blend mode...). Change detection
// uses the zip entry crc32: unchanged layers are skipped without
// decoding their tiles.

#include "Boxes/imagebox.h"

class CORE_EXPORT KraImageBox : public ImageBox {
    e_OBJECT
    e_DECLARE_TYPE(KraImageBox)
protected:
    KraImageBox();
    KraImageBox(const QString &filePath,
                const QString &sourceKra,
                const QString &layerUuid,
                const QString &frameFile,
                const quint32 crc);

    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp);
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;
public:
    void setupCanvasMenu(PropertyMenu * const menu);

    void writeBoundingBox(eWriteStream& dst) const;
    void readBoundingBox(eReadStream& src);

    // Re-read this layer from the source kra, replace its pixels in
    // the pixel cache, then reload the texture. Keeps all animation
    // data. Returns false when the layer is gone from the source.
    bool updateFromSource();

    // Update every kra-bound box of the same source document in the
    // current scene.
    void syncAllFromSource();

    const QString& sourceKra() const { return mSourceKra; }
    const QString& layerUuid() const { return mLayerUuid; }
    const QString& frameFile() const { return mFrameFile; }
    quint32 sourceCrc() const { return mCrc; }

    // If the pixel cache file is gone (cache cleared), re-extract the
    // layer pixels from the source kra.
    bool ensureCachedFile();

private:
    QString mSourceKra;
    QString mLayerUuid;
    QString mFrameFile; // pixel entry name inside "<image>/layers/"
    quint32 mCrc = 0;   // zip crc of the pixel entry at import/update
};

#endif // KRAIMAGEBOX_H
