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

#include "Depth/modelcatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "appsupport.h"

namespace AiDepth {
namespace ModelCatalog {

// filled from the actual downloaded files; defined so the table below
// stays readable and a mismatch shows up at compile time
#define AIDEPTH_LARGE_GRAPH_BYTES 213378LL
#define AIDEPTH_LARGE_GRAPH_SHA "33a6a11a325ad08041d0bc8ca9e5f9e6bbbc6ee6b504b2bfa7848f836f0bbebb"
#define AIDEPTH_LARGE_DATA_BYTES 670965760LL
#define AIDEPTH_LARGE_DATA_SHA "787f75d5dd75a882cdeec5f9e694823c5e09ac3d847a4c279659a78f69ba7694"

const QList<ModelInfo>& models()
{
    static const QList<ModelInfo> table = [] {
        QList<ModelInfo> list;

        // Depth Anything V2 Small, fp16 weights (Apache-2.0).
        // ONNX conversion by onnx-community (Hugging Face).
        {
            ModelInfo m;
            m.fId = QStringLiteral("small");
            m.fDirName = QStringLiteral("depth_anything_v2_small");
            m.fBundled = true;
            m.fFiles = {
                { QStringLiteral("model_fp16.onnx"), 180471,
                  QStringLiteral("3f220770bf259ef0cc1a8253f4f29419d4d15092902d78ded851669291d876e2") },
                { QStringLiteral("model_fp16.onnx_data"), 50392064,
                  QStringLiteral("4c3b600a87aa247593ceaafb11cd1f40568dc391cd1305d6ad01075079297ddd") },
            };
            const QString hfBase = QStringLiteral(
                        "https://hf-mirror.com/onnx-community/"
                        "depth-anything-v2-small-ONNX/resolve/main/onnx/");
            const QString ghBase = QStringLiteral(
                        "https://github.com/1220874621zp-debug/friction-2.5d/"
                        "releases/download/ai-models/");
            m.fSources = {
                { hfBase + QStringLiteral("model_fp16.onnx"),
                  hfBase + QStringLiteral("model_fp16.onnx_data") },
                { ghBase + QStringLiteral("dav2_small_model_fp16.onnx"),
                  ghBase + QStringLiteral("dav2_small_model_fp16.onnx_data") },
            };
            list << m;
        }

        // Depth Anything V2 Base, fp16 weights (CC-BY-NC-4.0, non
        // commercial — never bundled with the app, download only).
        {
            ModelInfo m;
            m.fId = QStringLiteral("base");
            m.fDirName = QStringLiteral("depth_anything_v2_base");
            m.fBundled = false;
            m.fFiles = {
                { QStringLiteral("model_fp16.onnx"), 129526,
                  QStringLiteral("0cb2451b5253813a92461b233f061323d9f19c72f6ecba61f1d8788e4e8b78b8") },
                { QStringLiteral("model_fp16.onnx_data"), 196410368,
                  QStringLiteral("1294e6cca6914b854ebe4daa936f069a89252aefccf86c5a72e8e78fc9b8d54c") },
            };
            const QString hfBase = QStringLiteral(
                        "https://hf-mirror.com/onnx-community/"
                        "depth-anything-v2-base-ONNX/resolve/main/onnx/");
            const QString ghBase = QStringLiteral(
                        "https://github.com/1220874621zp-debug/friction-2.5d/"
                        "releases/download/ai-models/");
            m.fSources = {
                { hfBase + QStringLiteral("model_fp16.onnx"),
                  hfBase + QStringLiteral("model_fp16.onnx_data") },
                { ghBase + QStringLiteral("dav2_base_model_fp16.onnx"),
                  ghBase + QStringLiteral("dav2_base_model_fp16.onnx_data") },
            };
            list << m;
        }

        // Depth Anything V2 Large, fp16 weights (CC-BY-NC-4.0, non
        // commercial — download only, best quality / temporal
        // stability, slowest inference).
        {
            ModelInfo m;
            m.fId = QStringLiteral("large");
            m.fDirName = QStringLiteral("depth_anything_v2_large");
            m.fBundled = false;
            m.fFiles = {
                { QStringLiteral("model_fp16.onnx"), AIDEPTH_LARGE_GRAPH_BYTES,
                  QStringLiteral(AIDEPTH_LARGE_GRAPH_SHA) },
                { QStringLiteral("model_fp16.onnx_data"), AIDEPTH_LARGE_DATA_BYTES,
                  QStringLiteral(AIDEPTH_LARGE_DATA_SHA) },
            };
            const QString hfBase = QStringLiteral(
                        "https://hf-mirror.com/onnx-community/"
                        "depth-anything-v2-large-ONNX/resolve/main/onnx/");
            const QString ghBase = QStringLiteral(
                        "https://github.com/1220874621zp-debug/friction-2.5d/"
                        "releases/download/ai-models/");
            m.fSources = {
                { hfBase + QStringLiteral("model_fp16.onnx"),
                  hfBase + QStringLiteral("model_fp16.onnx_data") },
                { ghBase + QStringLiteral("dav2_large_model_fp16.onnx"),
                  ghBase + QStringLiteral("dav2_large_model_fp16.onnx_data") },
            };
            list << m;
        }

        return list;
    }();
    return table;
}

const ModelInfo* model(const QString& id)
{
    for (const auto& m : models()) {
        if (m.fId == id) { return &m; }
    }
    return nullptr;
}

QString bundledDir()
{
    return AppSupport::getAppPath() + QStringLiteral("/models");
}

QString downloadDir()
{
    if (AppSupport::isAppPortable()) {
        return bundledDir();
    }
    return AppSupport::getAppConfigPath() + QStringLiteral("/models");
}

qint64 totalBytes(const ModelInfo& info)
{
    qint64 total = 0;
    for (const auto& f : info.fFiles) { total += f.fExpectedBytes; }
    return total;
}

QString humanSize(const ModelInfo& info)
{
    const qreal mb = totalBytes(info) / (1024.0 * 1024.0);
    return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
}

static bool dirHasCompleteModel(const QString& dir, const ModelInfo& info)
{
    for (const auto& f : info.fFiles) {
        const QFileInfo fi(dir + QLatin1Char('/') + f.fFileName);
        if (!fi.exists() || !fi.isFile()) { return false; }
        if (fi.size() != f.fExpectedBytes) { return false; }
    }
    return true;
}

QString resolveModelDir(const QString& id)
{
    const ModelInfo* info = model(id);
    if (!info) { return QString(); }

    const QString dlDir = downloadDir() + QLatin1Char('/') + info->fDirName;
    if (dirHasCompleteModel(dlDir, *info)) { return dlDir; }

    const QString bdDir = bundledDir() + QLatin1Char('/') + info->fDirName;
    if (dirHasCompleteModel(bdDir, *info)) { return bdDir; }

    return QString();
}

bool verifySha256(const QString& dir, const ModelInfo& info, QString* errOut)
{
    for (const auto& f : info.fFiles) {
        QFile file(dir + QLatin1Char('/') + f.fFileName);
        if (!file.open(QIODevice::ReadOnly)) {
            if (errOut) { *errOut = f.fFileName + QStringLiteral(": cannot open"); }
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) {
            if (errOut) { *errOut = f.fFileName + QStringLiteral(": read failed"); }
            return false;
        }
        const QString hex = QString::fromLatin1(hash.result().toHex());
        if (hex != f.fSha256) {
            if (errOut) { *errOut = f.fFileName + QStringLiteral(": checksum mismatch"); }
            return false;
        }
    }
    return true;
}

} // namespace ModelCatalog
} // namespace AiDepth
