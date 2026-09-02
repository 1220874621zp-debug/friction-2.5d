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

#ifndef PSDIMPORTER_H
#define PSDIMPORTER_H

#include "smartPointers/selfref.h"
#include <functional>
#include "Psd/psdfile.h"
#include "Psd/fpsdpackage.h"

class BoundingBox;
class PsdImageBox;

namespace ImportPSD {
    CORE_EXPORT
    // progress callback: (currentLayer, totalLayers) - called on the
    // calling thread; used by the app for a progress dialog (big PSDs
    // take seconds and look like a freeze without feedback)
    qsptr<BoundingBox> loadPSDFile(
            const QString &path,
            const std::function<void(int, int)>& progress = nullptr);

    // shared helpers used by PsdImageBox for incremental updates
    namespace PsdSync {
        // Key of the flattened-composite entry of a package.
        CORE_EXPORT
        QString compositeKey();

        // Stable key of a layer inside the package: the psd native
        // layer id, or a positional fallback for files without 'lyid'.
        CORE_EXPORT
        QString layerKeyForRecord(const psd::LayerRecord &rec);

        // Create a box bound to a package layer whose pixels are
        // already extracted to cachePath.
        CORE_EXPORT
        qsptr<PsdImageBox> createLayerBox(const QString &packagePath,
                                          const QString &cachePath,
                                          const Fpsd::LayerMeta &lm);

        // Update the pixels of one bound layer from a freshly loaded
        // psd. Modifies meta in place; changed layer PNGs go into
        // 'updates' (caller persists via Fpsd::updatePackage).
        // Change detection hashes the raw (compressed) channel bytes
        // before any decoding, so unchanged layers cost a hash only.
        // Returns -1 layer gone / error, 0 unchanged, 1 updated.
        CORE_EXPORT
        int updateLayerPixels(const psd::PsdFile &psd,
                              Fpsd::Meta &meta,
                              QMap<QString, QByteArray> &updates,
                              PsdImageBox *box);
    }
}

#endif // PSDIMPORTER_H
