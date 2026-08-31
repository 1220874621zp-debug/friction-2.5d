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
*/

#ifndef AIDEPTH_MODELCATALOG_H
#define AIDEPTH_MODELCATALOG_H

#include "core_global.h"

#include <QString>
#include <QList>

namespace AiDepth {

struct ModelFile {
    QString fFileName;
    qint64 fExpectedBytes;
    QString fSha256; // lowercase hex
};

struct ModelInfo {
    QString fId;       // "small" / "base"
    QString fDirName;  // sub folder below the models root
    bool fBundled = false; // ships with the installer next to the exe
    QList<ModelFile> fFiles;
    // Download sources in priority order; every source holds one full
    // URL per file (same order as fFiles).
    QList<QStringList> fSources;
};

namespace ModelCatalog {

CORE_EXPORT const QList<ModelInfo>& models();
CORE_EXPORT const ModelInfo* model(const QString& id);

// <exe>/models — where bundled models live (portable and installed).
CORE_EXPORT QString bundledDir();
// Writable dir for downloads: portable keeps everything next to the exe,
// installed builds use the user config location.
CORE_EXPORT QString downloadDir();

CORE_EXPORT qint64 totalBytes(const ModelInfo& info);
CORE_EXPORT QString humanSize(const ModelInfo& info);

// Directory that contains the complete file group with matching sizes,
// empty string when the model is missing or corrupt.
CORE_EXPORT QString resolveModelDir(const QString& id);

// Full sha256 verification (used after downloads).
CORE_EXPORT bool verifySha256(const QString& dir, const ModelInfo& info,
                              QString* errOut = nullptr);

} // namespace ModelCatalog
} // namespace AiDepth

#endif // AIDEPTH_MODELCATALOG_H
