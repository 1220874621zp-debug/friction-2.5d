/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
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

#include "Boxes/lottiebox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

// zlib for the raw-deflate zip entries (same arrangement as the KRA
// importer): Qt5Core bundles and exports zlib on Windows, other
// platforms use the system zlib.
#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

#include "lottieprovider.h"
#include "appsupport.h"
#include "typemenu.h"
#include "filesourcescache.h"
#include "svgexporter.h"
#include "exceptions.h"

#include <algorithm>
#include <limits>

namespace {

void inflateRaw(const uchar* data, const qint64 size,
                uchar* out, const qint64 expectedSize)
{
    if (size < 0 || expectedSize < 0
            || quint64(size) > std::numeric_limits<uInt>::max()
            || quint64(expectedSize) > std::numeric_limits<uInt>::max()) {
        RuntimeThrow("lottie: deflate stream too large");
    }
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    strm.avail_in = uInt(size);
    strm.next_out = reinterpret_cast<Bytef*>(out);
    strm.avail_out = uInt(expectedSize);
    // negative window size: raw deflate, no zlib header/trailer
    if (inflateInit2(&strm, -15) != Z_OK) {
        RuntimeThrow("lottie: inflate init failed");
    }
    const int ret = inflate(&strm, Z_FINISH);
    const quint64 produced = strm.total_out;
    inflateEnd(&strm);
    if (ret != Z_STREAM_END || produced != quint64(expectedSize)) {
        RuntimeThrow("lottie: inflated size mismatch");
    }
}

// ---------------------------------------------------------------------------
// Minimal PKZIP reader (STORED + DEFLATE) for .lottie containers.
// The .lottie format is a zip of manifest.json + the animation json
// (+ images); everything is extracted to a temp dir so skottie's
// file resource provider can resolve relative image references.
// ---------------------------------------------------------------------------

struct LottieZipEntry {
    QString name;
    quint16 method = 0;
    qint64 compressedSize = 0;
    qint64 uncompressedSize = 0;
    qint64 dataStart = 0;
};

quint16 u16(const uchar* p)
{
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 u32(const uchar* p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8)
            | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

bool readZipEntries(const QByteArray& raw, QList<LottieZipEntry>& entries)
{
    const uchar* const d = reinterpret_cast<const uchar*>(raw.constData());
    const int size = raw.size();
    if (size < 22) return false;
    // find the end-of-central-directory record (scan back over a
    // possible comment, max 64k)
    int eocd = -1;
    const int scanFrom = std::max(0, size - 22 - 65535);
    for (int i = size - 22; i >= scanFrom; --i) {
        if (d[i] == 'P' && d[i + 1] == 'K'
                && d[i + 2] == 5 && d[i + 3] == 6) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return false;
    const int entryCount = u16(d + eocd + 10);
    qint64 cdOffset = u32(d + eocd + 16);
    for (int i = 0; i < entryCount; i++) {
        if (cdOffset + 46 > size
                || u32(d + cdOffset) != 0x02014b50 /* central dir sig */) {
            return false;
        }
        LottieZipEntry e;
        e.method = u16(d + cdOffset + 10);
        e.compressedSize = u32(d + cdOffset + 20);
        e.uncompressedSize = u32(d + cdOffset + 24);
        const int nameLen = u16(d + cdOffset + 28);
        const int extraLen = u16(d + cdOffset + 30);
        const int commentLen = u16(d + cdOffset + 32);
        const qint64 localOffset = u32(d + cdOffset + 42);
        if (cdOffset + 46 + nameLen > size) return false;
        // entry names are UTF-8 in .lottie files (flag bit 11); the
        // latin1 fallback keeps ascii names working either way
        e.name = QString::fromUtf8(
                    reinterpret_cast<const char*>(d + cdOffset + 46),
                    nameLen);
        if (e.name.isEmpty()) return false;
        // resolve the entry data through the local header (its own
        // name/extra lengths may differ from the central directory)
        if (localOffset + 30 > size
                || u32(d + localOffset) != 0x04034b50 /* local sig */) {
            return false;
        }
        const int lNameLen = u16(d + localOffset + 26);
        const int lExtraLen = u16(d + localOffset + 28);
        e.dataStart = localOffset + 30 + lNameLen + lExtraLen;
        if (e.dataStart + e.compressedSize > size) return false;
        entries.append(e);
        cdOffset += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool extractZipFile(const QByteArray& raw,
                    const LottieZipEntry& e,
                    const QString& destDir)
{
    // refuse path traversal / absolute paths
    const QString clean = QDir::cleanPath(e.name);
    if (clean.startsWith(QLatin1String(".."))
            || clean.startsWith(QLatin1String("/"))
            || clean.contains(QLatin1String(":"))) {
        return false;
    }
    const QString destPath = destDir + QLatin1Char('/') + clean;
    if (e.name.endsWith(QLatin1Char('/'))) {
        // directory entry
        return QDir().mkpath(destPath);
    }
    if (!QDir().mkpath(QFileInfo(destPath).absolutePath())) return false;
    const uchar* const base =
            reinterpret_cast<const uchar*>(raw.constData());
    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly)) return false;
    if (e.method == 0) { // stored
        if (e.compressedSize != e.uncompressedSize) return false;
        out.write(reinterpret_cast<const char*>(base + e.dataStart),
                  e.compressedSize);
    } else if (e.method == 8) { // deflate
        if (e.uncompressedSize <= 0
                || quint64(e.uncompressedSize) > quint64(256) * 1024 * 1024) {
            return false;
        }
        QByteArray inflated(int(e.uncompressedSize), Qt::Uninitialized);
        try {
            inflateRaw(base + e.dataStart, e.compressedSize,
                       reinterpret_cast<uchar*>(inflated.data()),
                       e.uncompressedSize);
        } catch (...) {
            return false;
        }
        out.write(inflated);
    } else {
        return false;
    }
    return true;
}

bool jsonLooksLikeLottie(const QByteArray& content)
{
    return content.contains("\"layers\"")
            && (content.contains("\"op\"")
                || content.contains("\"fr\"")
                || content.contains("\"v\""));
}

} // namespace

LottieBoxRenderData::LottieBoxRenderData(BoundingBox* const parentBox) :
    ImageRenderData(parentBox) {}

LottieBoxRenderData::~LottieBoxRenderData()
{
    LottieLib::releaseHandle(fHandle);
}

void LottieBoxRenderData::loadImageFromHandler()
{
    if (fHandle) {
        fImage = LottieLib::renderFrame(fHandle, fTime);
    }
}

LottieBox::LottieBox() :
    BoundingBox(QStringLiteral("Lottie"), eBoxType::lottie) {
    connect(this, &eBoxOrSound::parentChanged,
            this, &LottieBox::updateDurationRange);
    setDurationRectangle(enve::make_shared<FixedLenAnimationRect>(*this), true);
}

FixedLenAnimationRect* LottieBox::getAnimationDurationRect() const
{
    return static_cast<FixedLenAnimationRect*>(getDurationRectangle());
}

bool LottieBox::looksLikeLottie(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto doc = QJsonDocument::fromJson(file.read(1024 * 1024));
    if (!doc.isObject()) return false;
    // bodymovin trio; OCA manifests also carry a "layers" array but
    // never frame-rate/out-point, so this keeps the two json formats
    // apart regardless of sniff order
    const auto obj = doc.object();
    return obj.contains(QStringLiteral("layers"))
            && obj.contains(QStringLiteral("fr"))
            && obj.contains(QStringLiteral("op"));
}

void LottieBox::setFilePathNoRename(const QString& path)
{
    mPath = path;
    loadAnimation();
    prp_afterWholeInfluenceRangeChanged();
}

void LottieBox::setFilePath(const QString& path)
{
    setFilePathNoRename(path);
    rename(QFileInfo(path).completeBaseName());
}

void LottieBox::loadAnimation()
{
    if (mHandle) {
        LottieLib::releaseHandle(mHandle);
        mHandle = nullptr;
    }
    mJsonPath.clear();
    mBaseDir.clear();
    mFrameCount = 0;

    const QFileInfo info(mPath);
    if (info.suffix().compare(QLatin1String("lottie"),
                              Qt::CaseInsensitive) == 0) {
        QString jsonPath;
        if (!extractLottieZip(mPath, jsonPath)) {
            qWarning() << "LOTTIEBOX: failed to extract .lottie zip"
                       << mPath;
            return;
        }
        mJsonPath = jsonPath;
    } else {
        mJsonPath = mPath;
    }
    mBaseDir = QFileInfo(mJsonPath).absolutePath();

    QString err;
    mHandle = LottieLib::loadFromFile(mJsonPath, mBaseDir, err);
    if (!mHandle) {
        qWarning() << "LOTTIEBOX: load failed:" << err;
        return;
    }
    const double fileFps = LottieLib::fps(mHandle);
    mFps = fileFps > 0.01 ? qreal(fileFps) : 30.0;
    mFrameCount = qMax(1, qCeil(LottieLib::duration(mHandle) * mFps));
    qWarning() << "LOTTIEBOX: loaded" << mJsonPath
               << "size" << LottieLib::width(mHandle)
               << "x" << LottieLib::height(mHandle)
               << "fps" << mFps << "frames" << mFrameCount;
    updateDurationRange();
}

void LottieBox::updateDurationRange()
{
    const auto durRect = getAnimationDurationRect();
    if (!durRect) return;
    durRect->setAnimationFrameDuration(mFrameCount);
}

int LottieBox::getAnimationFrameForRelFrame(const qreal relFrame)
{
    if (mFrameCount <= 0) return 0;
    const auto durRect = getAnimationDurationRect();
    const int animStartRelFrame =
            durRect ? durRect->getMinAnimRelFrame() : 0;
    const int animFrame = qRound(relFrame - animStartRelFrame);
    return qBound(0, animFrame, mFrameCount - 1);
}

void LottieBox::setupRenderData(const qreal relFrame,
                                const QMatrix& parentM,
                                BoxRenderData* const data,
                                Canvas* const scene)
{
    BoundingBox::setupRenderData(relFrame, parentM, data, scene);
    if (!mHandle) return;
    const auto imgData = static_cast<LottieBoxRenderData*>(data);
    if (!imgData->fHandle) {
        // share the animation with the render task; the task holds
        // its own reference so it outlives layer reloads/deletion
        imgData->fHandle = mHandle;
        LottieLib::addRef(imgData->fHandle);
    }
    imgData->fTime = getAnimationFrameForRelFrame(relFrame) / mFps;
}

stdsptr<BoxRenderData> LottieBox::createRenderData()
{
    return enve::make_shared<LottieBoxRenderData>(this);
}

void LottieBox::writeBoundingBox(eWriteStream& dst) const
{
    BoundingBox::writeBoundingBox(dst);
    dst.writeFilePath(mPath);
}

void LottieBox::readBoundingBox(eReadStream& src)
{
    BoundingBox::readBoundingBox(src);
    const QString path = src.readFilePath();
    setFilePathNoRename(path);
}

QDomElement LottieBox::prp_writePropertyXEV_impl(const XevExporter& exp) const
{
    auto result = BoundingBox::prp_writePropertyXEV_impl(exp);
    XevExportHelpers::setAbsAndRelFileSrc(mPath, result, exp);
    return result;
}

void LottieBox::prp_readPropertyXEV_impl(const QDomElement& ele,
                                         const XevImporter& imp)
{
    BoundingBox::prp_readPropertyXEV_impl(ele, imp);
    const QString absSrc = XevExportHelpers::getAbsAndRelFileSrc(ele, imp);
    setFilePathNoRename(absSrc);
}

void LottieBox::anim_setAbsFrame(const int frame)
{
    BoundingBox::anim_setAbsFrame(frame);
    if (!mHandle) return;
    planUpdate(UpdateReason::frameChange);
}

FrameRange LottieBox::prp_getIdenticalRelRange(const int relFrame) const
{
    if (isVisibleAndInDurationRect(relFrame)) {
        const auto animDur = getAnimationDurationRect();
        if (animDur) {
            const auto animRange = animDur->getAnimRelRange();
            if (animRange.inRange(relFrame)) {
                return {relFrame, relFrame};
            } else if (relFrame > animRange.fMax) {
                const auto baseRange =
                        BoundingBox::prp_getIdenticalRelRange(relFrame);
                const FrameRange durRect{animRange.fMax + 1,
                                         animDur->getRelFrameRange().fMax};
                return baseRange*durRect;
            } else if (relFrame < animRange.fMin) {
                const auto baseRange =
                        BoundingBox::prp_getIdenticalRelRange(relFrame);
                const FrameRange durRect{animDur->getRelFrameRange().fMin,
                                         animRange.fMin - 1};
                return baseRange*durRect;
            }
        }
    }
    return BoundingBox::prp_getIdenticalRelRange(relFrame);
}

bool LottieBox::shouldScheduleUpdate()
{
    if (!mHandle) return false;
    return BoundingBox::shouldScheduleUpdate();
}

void LottieBox::reload()
{
    loadAnimation();
    prp_afterWholeInfluenceRangeChanged();
}

void LottieBox::changeSourceFile()
{
    const QString filters = FileExtensions::lottieFilters();
    QString importPath = AppSupport::getOpenFile(nullptr,
                                                 tr("Change Source"),
                                                 mPath,
                                                 tr("Lottie Files (%1)").arg(filters));
    if (!importPath.isEmpty()) { setFilePath(importPath); }
}

void LottieBox::setupCanvasMenu(PropertyMenu * const menu)
{
    if (menu->hasActionsForType<LottieBox>()) { return; }
    menu->addedActionsForType<LottieBox>();

    const PropertyMenu::PlainSelectedOp<LottieBox> reloadOp =
    [](LottieBox * box) { box->reload(); };
    menu->addPlainAction(QIcon::fromTheme("loop"), tr("Reload"), reloadOp);

    const PropertyMenu::PlainSelectedOp<LottieBox> setSrcOp =
    [](LottieBox * box) { box->changeSourceFile(); };
    menu->addPlainAction(QIcon::fromTheme("document-new"),
                         tr("Set Source File"), setSrcOp);

    menu->addSeparator();

    BoundingBox::setupCanvasMenu(menu);
}

bool LottieBox::extractLottieZip(const QString& zipPath,
                                 QString& jsonPathOut)
{
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray raw = file.readAll();

    QList<LottieZipEntry> entries;
    if (!readZipEntries(raw, entries)) return false;

    // fresh extraction dir per load (QTemporaryDir removes its tree
    // when replaced/destroyed)
    mExtractDir.reset(
                new QTemporaryDir(
                    QDir::temp().absoluteFilePath(
                        QStringLiteral("friction-lottie-XXXXXX"))));
    if (!mExtractDir->isValid()) return false;
    const QString destDir = mExtractDir->path();

    QStringList jsonCandidates;
    for (const auto& e : entries) {
        if (!extractZipFile(raw, e, destDir)) continue;
        if (e.name.endsWith(QLatin1String(".json"),
                            Qt::CaseInsensitive)) {
            jsonCandidates << e.name;
        }
    }

    QString chosen;
    // prefer the animation referenced by the .lottie manifest
    for (const auto& name : jsonCandidates) {
        if (name.compare(QLatin1String("manifest.json"),
                         Qt::CaseInsensitive) == 0) {
            QFile manifest(destDir + QLatin1Char('/') + name);
            if (manifest.open(QIODevice::ReadOnly)) {
                const auto doc = QJsonDocument::fromJson(
                            manifest.readAll());
                const auto anims = doc.object().value(
                            QStringLiteral("Animations")).toArray();
                if (!anims.isEmpty()) {
                    const auto rel = anims.at(0).toObject().value(
                                QStringLiteral("Filepath")).toString();
                    if (!rel.isEmpty()) {
                        chosen = QDir::cleanPath(
                                    destDir + QLatin1Char('/') + rel);
                    }
                }
            }
            break;
        }
    }
    if (chosen.isEmpty()) {
        // fall back: first json entry that smells like bodymovin
        for (const auto& name : jsonCandidates) {
            if (name.compare(QLatin1String("manifest.json"),
                             Qt::CaseInsensitive) == 0) continue;
            QFile cand(destDir + QLatin1Char('/') + name);
            if (!cand.open(QIODevice::ReadOnly)) continue;
            if (jsonLooksLikeLottie(cand.read(1024 * 1024))) {
                chosen = cand.fileName();
                break;
            }
        }
    }
    if (chosen.isEmpty()) return false;
    if (!QFile::exists(chosen)) return false;
    jsonPathOut = chosen;
    return true;
}
