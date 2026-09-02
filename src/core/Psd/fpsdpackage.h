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

#ifndef FPSDPACKAGE_H
#define FPSDPACKAGE_H

// .fpsd single-file PSD asset package.
//
// A standard ZIP container (entries STORED, no compression - PNG data
// is already compressed) with the following layout:
//
//   xxx.fpsd
//   |- meta.json          // layer ids, names, rects, pixel hashes, source psd
//   |- layers/<key>.png   // per-layer pixels, key = psd layer id
//
// Layers are matched by the Photoshop native layer id ('lyid'), so a
// single layer can be re-extracted and replaced inside the package
// without touching any other layer or animation data.

#include "core_global.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

namespace Fpsd {

struct CORE_EXPORT LayerMeta {
    QString key;        // entry key inside the package ("layers/<id>.png")
    qint32 layerId = 0; // psd native layer id, 0 for the composite fallback
    QString name;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    QString hash;       // md5 hex of the straight rgba pixel data
    int opacity = 255;
    bool visible = true;
    QString blendKey = QStringLiteral("norm");
};

struct CORE_EXPORT Meta {
    QString sourcePsd;  // absolute path of the source psd file
    int width = 0;
    int height = 0;
    bool composite = false; // package holds the flattened composite only
    QList<LayerMeta> layers;
};

// --- zip container io -------------------------------------------------

// Write a brand new package with the given entries
// (entry name -> file data). Existing file is overwritten.
bool CORE_EXPORT writePackage(const QString &path,
                              const QMap<QString, QByteArray> &entries);

// Read the whole package into memory. Empty map on failure.
QMap<QString, QByteArray> CORE_EXPORT readPackage(const QString &path);

// Read a single entry without loading the whole package (seek-based:
// only the central directory and the requested entry are read).
QByteArray CORE_EXPORT readPackageEntry(const QString &path,
                                        const QString &name);

// Apply updates to an existing package: entries whose names appear in
// 'updates' are (re)written, all other entries are raw-copied from the
// old file (STORED entries are copied verbatim - no decode/re-encode,
// no full in-memory load of unchanged layer PNGs).
bool CORE_EXPORT updatePackage(const QString &path,
                               const QMap<QString, QByteArray> &updates);

// --- meta.json io ------------------------------------------------------

QByteArray CORE_EXPORT metaToJson(const Meta &meta);
bool CORE_EXPORT metaFromJson(const QByteArray &json, Meta *meta);

// --- helpers ------------------------------------------------------------

// Per-package pixel cache dir:
//   <app cache>/PSDCache/<md5 of package path>/
QString CORE_EXPORT cacheDirForPackage(const QString &packagePath);

// PNG entry name for a layer key.
QString CORE_EXPORT layerEntryName(const QString &key);

// --- pixel helpers (shared by importer and image box) -------------------

// Encode straight (unpremultiplied) rgba8 as PNG. Empty on failure.
// Uses fast deflate (level 1): PNG is lossless, the level only trades
// file size for encode speed, and this data is an intermediate cache.
QByteArray CORE_EXPORT rgbaToPng(const QByteArray &rgba, const int w,
                                 const int h);

// Write layer PNG data into the package pixel cache and return the
// cache file path. Empty string on failure.
QString CORE_EXPORT writeLayerCacheFile(const QString &packagePath,
                                        const QString &key,
                                        const QByteArray &pngData);

} // namespace Fpsd

#endif // FPSDPACKAGE_H
