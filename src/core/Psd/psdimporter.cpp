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

// PSD import, architecture inspired by PhotoshopAPI
// https://github.com/EmilDohne/PhotoshopAPI (BSD-3-Clause)
//
// Layers are packed into a single .fpsd asset package next to the
// source PSD (fallback: app cache dir) and bound to PsdImageBoxes
// via package path + native layer id, enabling incremental per-layer
// updates without touching animation data.

#include "psdimporter.h"
#include "psdfile.h"
#include "fpsdpackage.h"
#include "psdimagebox.h"

#include <QAtomicInt>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QThread>

#include <QtConcurrent/QtConcurrentMap>

#include "appsupport.h"
#include "exceptions.h"
#include "Boxes/containerbox.h"
#include "Boxes/imagebox.h"
#include "Animators/transformanimator.h"

namespace {

SkBlendMode psdBlendToSk(const QString &key)
{
    if (key == QLatin1String("mul"))  { return SkBlendMode::kMultiply; }
    if (key == QLatin1String("scrn")) { return SkBlendMode::kScreen; }
    if (key == QLatin1String("over")) { return SkBlendMode::kOverlay; }
    if (key == QLatin1String("dark")) { return SkBlendMode::kDarken; }
    if (key == QLatin1String("lite")) { return SkBlendMode::kLighten; }
    if (key == QLatin1String("diff")) { return SkBlendMode::kDifference; }
    if (key == QLatin1String("smud")) { return SkBlendMode::kExclusion; }
    if (key == QLatin1String("div"))  { return SkBlendMode::kColorDodge; }
    if (key == QLatin1String("idiv")) { return SkBlendMode::kColorBurn; }
    if (key == QLatin1String("hLit")) { return SkBlendMode::kHardLight; }
    if (key == QLatin1String("sLit")) { return SkBlendMode::kSoftLight; }
    if (key == QLatin1String("hue"))  { return SkBlendMode::kHue; }
    if (key == QLatin1String("sat"))  { return SkBlendMode::kSaturation; }
    if (key == QLatin1String("colr")) { return SkBlendMode::kColor; }
    if (key == QLatin1String("lum"))  { return SkBlendMode::kLuminosity; }
    // linear dodge (add) has a real skia equivalent
    if (key == QLatin1String("lddg")) { return SkBlendMode::kPlus; }
    // linear burn / vivid/linear/pin light / hard mix / subtract /
    // divide / darker|lighter color have no skia blend mode; fall
    // back to normal rather than silently mis-blending
    if (key == QLatin1String("lbrn") || key == QLatin1String("vLit")
            || key == QLatin1String("lLit") || key == QLatin1String("pLit")
            || key == QLatin1String("hMix") || key == QLatin1String("fsub")
            || key == QLatin1String("fdiv") || key == QLatin1String("dkCl")
            || key == QLatin1String("lgCl")) {
        qWarning() << "PSD import: blend mode" << key
                   << "has no skia equivalent, using normal";
        return SkBlendMode::kSrcOver;
    }
    // norm, pass and unknown keys
    return SkBlendMode::kSrcOver;
}

bool isWritableDir(const QString &path)
{
    QTemporaryFile probe(QDir(path).filePath(QStringLiteral("XXXXXX.tmp")));
    if (probe.open()) {
        probe.close(); // auto-removed on destruction
        return true;
    }
    return false;
}

// Package next to the psd when the folder is writable, otherwise in
// the app cache (some setups block writes next to the source file).
QString resolvePackagePath(const QFileInfo &psdInfo)
{
    // always use '/' separators: QDir::separator() ('\') mixed with the
    // '/' from absolutePath() produces paths whose hash changes after
    // readFilePath() normalization, splitting the pixel cache directory
    const QString localPath = psdInfo.absolutePath() + QStringLiteral("/")
            + psdInfo.completeBaseName() + QStringLiteral(".fpsd");
    if (isWritableDir(psdInfo.absolutePath())) { return localPath; }

    qWarning() << "PSD import: source folder not writable,"
               << "caching package to the app cache";
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
            psdInfo.absoluteFilePath().toUtf8(),
            QCryptographicHash::Md5).toHex().left(12));
    const QString dir = AppSupport::getAppCachePath()
            + QStringLiteral("/PSDPackages");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/") + hash + QStringLiteral(".fpsd");
}

// parallel decode task (QtConcurrent::mapped with a plain function
// pointer: this Qt5's result-type deduction needs result_type, which
// lambdas do not provide - function pointers map cleanly)
struct PsdDecodedLayer {
    Fpsd::LayerMeta lm;
    QByteArray png;
    bool ok = false;
};

struct PsdDecodeCtx {
    const psd::PsdFile *psd = nullptr;
    const psd::LayerRecord *rec = nullptr;
    QAtomicInt *done = nullptr;
};

PsdDecodedLayer decodeLayerTask(const PsdDecodeCtx &ctx)
{
    const psd::LayerRecord &rec = *ctx.rec;
    PsdDecodedLayer out;
    out.lm.key = ImportPSD::PsdSync::layerKeyForRecord(rec);
    out.lm.layerId = rec.layerId;
    out.lm.name = rec.name;
    out.lm.x = rec.rect.left();
    out.lm.y = rec.rect.top();
    out.lm.w = rec.rect.width();
    out.lm.h = rec.rect.height();
    // raw channel bytes hash: later syncs detect "unchanged"
    // without re-decoding the pixels
    out.lm.hash = ctx.psd->rawLayerHash(rec);
    out.lm.opacity = rec.opacity;
    out.lm.visible = rec.visible;
    out.lm.blendKey = rec.blendKey;
    const QByteArray rgba = ctx.psd->extractLayerRGBA(rec);
    if (!rgba.isEmpty()) {
        out.png = Fpsd::rgbaToPng(rgba, out.lm.w, out.lm.h);
        out.ok = !out.png.isEmpty();
    }
    ctx.done->fetchAndAddRelaxed(1);
    return out;
}

qsptr<BoundingBox> compositeAsImageBox(const psd::PsdFile &psd,
                                       const QString &path,
                                       const QString &packagePath)
{
    QString error;
    const QByteArray rgba = psd.extractCompositeRGBA(&error);
    if (rgba.isEmpty()) {
        const std::string msg = error.isEmpty()
                ? std::string("No layers and no composite image")
                : error.toStdString();
        RuntimeThrow(msg);
    }
    const QString key = ImportPSD::PsdSync::compositeKey();
    const QByteArray png = Fpsd::rgbaToPng(rgba, psd.width(), psd.height());
    const QString cachePath = Fpsd::writeLayerCacheFile(packagePath,
                                                        key, png);
    if (png.isEmpty() || cachePath.isEmpty()) {
        RuntimeThrow("Failed to write composite image cache");
    }

    Fpsd::Meta meta;
    meta.sourcePsd = QFileInfo(path).absoluteFilePath();
    meta.width = psd.width();
    meta.height = psd.height();
    meta.composite = true;
    Fpsd::LayerMeta lm;
    lm.key = key;
    lm.name = QFileInfo(path).completeBaseName();
    lm.w = psd.width();
    lm.h = psd.height();
    lm.hash = psd.rawCompositeHash();
    meta.layers.append(lm);

    QMap<QString, QByteArray> entries;
    entries.insert(Fpsd::layerEntryName(key), png);
    entries.insert(QStringLiteral("meta.json"), Fpsd::metaToJson(meta));
    if (!Fpsd::writePackage(packagePath, entries)) {
        RuntimeThrow("Failed to write PSD package " + packagePath.toStdString());
    }

    const auto imgBox = enve::make_shared<PsdImageBox>(cachePath,
                                                       packagePath, key);
    imgBox->prp_setName(QFileInfo(path).completeBaseName());
    imgBox->planCenterPivotPosition();
    return imgBox;
}

} // namespace

namespace ImportPSD {

namespace PsdSync {

QString compositeKey()
{
    return QStringLiteral("composite");
}

QString layerKeyForRecord(const psd::LayerRecord &rec)
{
    if (rec.layerId != 0) { return QString::number(rec.layerId); }
    // file without 'lyid' blocks: positional fallback
    return QStringLiteral("fb%1").arg(rec.index);
}

qsptr<PsdImageBox> createLayerBox(const QString &packagePath,
                                  const QString &cachePath,
                                  const Fpsd::LayerMeta &lm)
{
    const auto box = enve::make_shared<PsdImageBox>(cachePath,
                                                    packagePath, lm.key);
    if (!lm.name.isEmpty()) { box->prp_setName(lm.name); }
    const auto trans = box->getBoxTransformAnimator();
    // opacity animator value range is 0-100 (percent), PSD stores 0-255
    trans->setOpacity(qBound(0., lm.opacity / 2.55, 100.));
    box->setBlendModeSk(psdBlendToSk(lm.blendKey));
    if (!lm.visible) { box->hide(); }
    box->planCenterPivotPosition();
    trans->translate(lm.x, lm.y);
    return box;
}

int updateLayerPixels(const psd::PsdFile &psd,
                      Fpsd::Meta &meta,
                      QMap<QString, QByteArray> &updates,
                      PsdImageBox *box)
{
    const QString key = box->sourceLayerKey();

    const psd::LayerRecord *rec = nullptr;
    QString rawHash;
    int x = 0, y = 0, w = 0, h = 0;
    if (key == compositeKey()) {
        w = psd.width();
        h = psd.height();
        rawHash = psd.rawCompositeHash();
        if (rawHash.isEmpty()) { return -1; }
    } else {
        if (key.startsWith(QStringLiteral("fb"))) {
            const int index = key.mid(2).toInt();
            for (const auto &l : psd.layers()) {
                if (l.index == index) { rec = &l; break; }
            }
        } else {
            const qint32 id = key.toInt();
            for (const auto &l : psd.layers()) {
                if (l.layerId == id) { rec = &l; break; }
            }
        }
        if (!rec || rec->divider != psd::Divider::None
                || rec->rect.isEmpty()) {
            return -1; // layer gone (deleted / merged in the psd)
        }
        x = rec->rect.left();
        y = rec->rect.top();
        w = rec->rect.width();
        h = rec->rect.height();
        rawHash = psd.rawLayerHash(*rec);
        // non-pixel attributes follow the source document
        const auto trans = box->getBoxTransformAnimator();
        trans->setOpacity(qBound(0., rec->opacity / 2.55, 100.));
        if (rec->visible) { box->show(); } else { box->hide(); }
        box->setBlendModeSk(psdBlendToSk(rec->blendKey));
    }

    Fpsd::LayerMeta *lm = nullptr;
    for (auto &l : meta.layers) {
        if (l.key == key) { lm = &l; break; }
    }

    // cheap path: the raw channel bytes (and the rect, which is part
    // of the hash input) are identical - pixels cannot differ, skip
    // the whole decode + PNG re-encode
    if (lm && lm->hash == rawHash && lm->w == w && lm->h == h
            && lm->x == x && lm->y == y) {
        // pixels unchanged; refresh the non-pixel attributes
        if (rec) {
            if (!rec->name.isEmpty()) { lm->name = rec->name; }
            lm->opacity = rec->opacity;
            lm->visible = rec->visible;
            lm->blendKey = rec->blendKey;
        }
        return 0;
    }

    QByteArray rgba;
    if (key == compositeKey()) {
        rgba = psd.extractCompositeRGBA();
    } else {
        rgba = psd.extractLayerRGBA(*rec);
    }
    if (rgba.isEmpty() || w <= 0 || h <= 0) { return -1; }

    const QByteArray png = Fpsd::rgbaToPng(rgba, w, h);
    if (png.isEmpty()) { return -1; }
    updates.insert(Fpsd::layerEntryName(key), png);

    if (lm) {
        // layer moved in the psd: shift the whole transform (base
        // value + every keyframe) so animation data is preserved
        const int dx = x - lm->x;
        const int dy = y - lm->y;
        if (dx != 0 || dy != 0) {
            box->getBoxTransformAnimator()->translate(dx, dy);
        }
        if (lm->w != w || lm->h != h) {
            qWarning() << "PSD sync: layer" << lm->name << "resized from"
                       << lm->w << "x" << lm->h << "to" << w << "x" << h;
        }
        lm->x = x;
        lm->y = y;
        lm->w = w;
        lm->h = h;
        lm->hash = rawHash;
        if (rec) {
            if (!rec->name.isEmpty()) { lm->name = rec->name; }
            lm->opacity = rec->opacity;
            lm->visible = rec->visible;
            lm->blendKey = rec->blendKey;
        }
    } else {
        Fpsd::LayerMeta nlm;
        nlm.key = key;
        nlm.layerId = rec ? rec->layerId : 0;
        nlm.name = rec ? rec->name : QString();
        nlm.x = x;
        nlm.y = y;
        nlm.w = w;
        nlm.h = h;
        nlm.hash = rawHash;
        nlm.opacity = rec ? rec->opacity : 255;
        nlm.visible = rec ? rec->visible : true;
        nlm.blendKey = rec ? rec->blendKey : QStringLiteral("norm");
        meta.layers.append(nlm);
    }
    return 1;
}

} // namespace PsdSync

qsptr<BoundingBox> loadPSDFile(
        const QString &path,
        const std::function<void(int, int)>& progress)
{
    psd::PsdFile psd;
    QString error;
    qWarning() << "PSD import: start" << path;
    if (!psd.load(path, &error)) {
        const std::string msg = error.isEmpty()
                ? std::string("Failed to parse PSD file")
                : error.toStdString();
        RuntimeThrow(msg);
    }

    const QFileInfo psdInfo(path);
    const QString packagePath = resolvePackagePath(psdInfo);

    if (progress) progress(0, 1);
    if (!psd.hasLayers()) {
        if (progress) progress(1, 1);
        return compositeAsImageBox(psd, path, packagePath);
    }

    const auto root = enve::make_shared<ContainerBox>(eBoxType::group);
    root->prp_setName(psdInfo.completeBaseName());

    // Layer records are stored bottom-to-top; ContainerBox::addContained
    // inserts at the top (list index 0 = panel top = canvas top), so file
    // order maps directly to stacking order.
    // Groups are NOT recreated: Photoshop blends group layers in
    // pass-through mode (a layer's blend mode applies to the whole stack
    // below it), while Friction renders groups onto isolated surfaces.
    // Soft-light / linear-light layers would blend against transparent
    // black inside an isolated group, producing dark haze. Verified
    // against the PSD's own composite image: flat stacking matches
    // Photoshop (mean diff 12.9) far better than isolated groups (40.8).
    Fpsd::Meta meta;
    meta.sourcePsd = psdInfo.absoluteFilePath();
    meta.width = psd.width();
    meta.height = psd.height();

    // Phase 1: decode + PNG-encode the eligible layers in worker
    // threads (pure pixel work, no document/box state touched);
    // Phase 2: create cache files and boxes on the calling thread in
    // file order (mapped() preserves the input order).
    QVector<PsdDecodeCtx> tasks;
    for (const auto &rec : psd.layers()) {
        if (rec.divider != psd::Divider::None) {
            // group boundary records (folder begin/end) - skipped
            continue;
        }
        if (rec.rect.isEmpty()) {
            qWarning() << "PSD import: skip empty layer" << rec.index << rec.name;
            continue;
        }
        tasks.append({&psd, &rec, nullptr});
    }
    const int totalLayers = tasks.count();
    QAtomicInt decodedCount = 0;
    for (auto &t : tasks) { t.done = &decodedCount; }
    QFuture<PsdDecodedLayer> future = QtConcurrent::mapped(
                tasks, &decodeLayerTask);
    while (!future.isFinished()) {
        if (progress) progress(decodedCount.loadAcquire(), totalLayers);
        QThread::msleep(15);
    }
    if (progress) progress(totalLayers, totalLayers);

    QMap<QString, QByteArray> entries;
    int imagesCreated = 0;
    const auto results = future.results();
    for (const auto &out : results) {
        if (!out.ok) {
            qWarning() << "PSD import: skipping layer" << out.lm.name;
            continue;
        }
        const QString key = out.lm.key;
        const QString cachePath = Fpsd::writeLayerCacheFile(packagePath,
                                                            key, out.png);
        if (cachePath.isEmpty()) {
            qWarning() << "PSD import: failed to cache layer" << out.lm.name;
            continue;
        }
        entries.insert(Fpsd::layerEntryName(key), out.png);
        meta.layers.append(out.lm);

        const auto imgBox = PsdSync::createLayerBox(packagePath,
                                                    cachePath, out.lm);
        imagesCreated++;
        root->addContained(imgBox);
        // apply the name AFTER addContained: insertContained() runs the
        // name through makeNameUniqueForDescendants() whose prp_sFixName()
        // strips non-ASCII characters, replacing Chinese names with
        // "Object N" - set the real name back afterwards.
        if (!out.lm.name.isEmpty()) { imgBox->prp_setName(out.lm.name); }
    }

    if (imagesCreated == 0) {
        // not a single pixel layer could be cached; fall back to the
        // flattened composite (counting containers is not enough -
        // empty group boxes would sneak through)
        return compositeAsImageBox(psd, path, packagePath);
    }

    entries.insert(QStringLiteral("meta.json"), Fpsd::metaToJson(meta));
    if (!Fpsd::writePackage(packagePath, entries)) {
        RuntimeThrow("Failed to write PSD package " + packagePath.toStdString());
    }
    qWarning() << "PSD import: package written" << packagePath
               << "with" << imagesCreated << "layer(s)";

    if (progress) progress(totalLayers, totalLayers);
    if (root->getContainedBoxesCount() == 1 && imagesCreated == 1) {
        return qSharedPointerCast<BoundingBox>(root->takeContained_k(0));
    }
    root->planCenterPivotPosition();
    return root;
}

} // namespace ImportPSD
