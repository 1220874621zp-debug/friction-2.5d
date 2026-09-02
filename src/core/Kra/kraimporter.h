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

#ifndef KRAIMPORTER_H
#define KRAIMPORTER_H

#include <QString>
#include <QStringList>
#include <functional>

#include "skia/skiaincludes.h"
#include "smartPointers/selfref.h"
#include "core_global.h"

#include <QImage>

class ContainerBox;
class BoundingBox;
class Canvas;

// Native importer for the Krita document format (.kra, syntaxVersion 2,
// as written by Krita 2.x through 6.x).
//
// A .kra file is a ZIP container holding:
//   mimetype                 "application/x-krita" (stored)
//   maindoc.xml              layer tree (deflate)
//   <image>/layers/layerN    paint layer pixels, Krita tiled format (stored)
//   <image>/layers/layerN.fK per-keyframe pixel files (stored)
//   <image>/layers/layerN.keyframes.xml  keyframe times + offsets (deflate)
//   <image>/animation/index.xml  framerate + playback range (deflate)
//
// Tiled pixel files: 5 text header lines (VERSION/TILEWIDTH/TILEHEIGHT/
// PIXELSIZE/DATA n) followed by per-tile "x,y,LZF,size\n" text headers,
// each followed by a flag byte (0 = raw interleaved, 1 = LZF compressed
// planar) and the tile payload (64x64 pixels). RGBA pixel bytes are
// stored B,G,R,A; tiles belong to the 64-aligned grid cell derived from
// the header coordinates (exactly what Krita's own loader does).
//
// Imported as: group layers -> ContainerBox; paint layers -> one
// ImageBox per keyframe (static layers get a single ImageBox) with a
// DurationRectangle for the exposure window. Vector/filter/generator/
// clone/file layers and masks have no friction equivalent and are
// reported through the skipped list.
// State of one bound paint layer inside a kra document, used by
// KraImageBox for incremental updates from the source file.
struct CORE_EXPORT KraLayerState {
    bool found = false;      // layer (uuid + frame file) still exists
    quint32 crc = 0;         // zip crc of the pixel entry (change detector)
    bool pixelsChanged = false;
    QImage image;            // decoded pixels (only when pixelsChanged)
    int posX = 0;            // canvas-space position (origin + offset)
    int posY = 0;
    QString name;
    int opacity = 255;
    bool visible = true;
    QString blendMode;       // krita compositeop id
};

// Cache directory of a kra file (same location the importer uses).
QString CORE_EXPORT kraCacheDirForFile(const QString& filePath);

// Write a PNG of 'image' into the kra cache dir, hash-named.
// Returns the cache file path, empty on failure.
QString CORE_EXPORT kraCachePng(const QString& filePath,
                                const QString& frameFile,
                                const QImage& image);

class CORE_EXPORT ImportKRA {
public:
    using ProgressReporter = std::function<void(int, int)>;

    // cheap check: ZIP magic + "mimetype" entry holding the kra magic
    static bool looksLikeKRA(const QString& filePath);
    // parses the document and builds a group containing the layer tree;
    // throws enve exceptions on invalid data. skippedOut (optional)
    // receives "<layer name>: <reason>" entries for everything that
    // could not be imported.
    static qsptr<ContainerBox> loadKRAFile(const QString& filePath,
                                           Canvas* const scene,
                                           const ProgressReporter& report
                                               = ProgressReporter(),
                                           QStringList* skippedOut = nullptr);

    // Locate the paint layer with the given uuid (and, for animated
    // layers, the given frame file) and report its current state.
    // Pixels are decoded only when the zip entry crc differs from
    // knownCrc, so unchanged layers cost a directory scan only.
    static KraLayerState checkLayerUpdate(const QString& filePath,
                                          const QString& uuid,
                                          const QString& frameFile,
                                          const quint32 knownCrc);
};

#endif // KRAIMPORTER_H
