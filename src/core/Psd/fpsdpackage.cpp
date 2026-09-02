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

#include "fpsdpackage.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "appsupport.h"

namespace {

// Minimal ZIP support (entries always STORED - layer pixels are PNG
// data, already compressed, so deflating again would only waste cpu).

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool initialized = false;
    if (!initialized) {
        for (quint32 i = 0; i < 256; i++) {
            quint32 c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (const char b : data) {
        crc = table[(crc ^ quint8(b)) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

struct ZipEntry {
    QString name;
    quint16 method = 0;
    quint32 crc = 0;
    quint32 size = 0;
    quint32 localOffset = 0;
    // extent of the complete local record (header + name + extra +
    // data) inside the source image; used by updatePackage to copy
    // unchanged entries verbatim without decoding them
    qint64 rawStart = 0;
    qint64 rawLen = 0;
    QByteArray data;
};

void writeZipEntries(QDataStream &s, QList<ZipEntry> &entries)
{
    for (auto &e : entries) {
        const QByteArray nameBytes = e.name.toUtf8();
        e.crc = crc32(e.data);
        e.size = quint32(e.data.size());
        e.localOffset = quint32(s.device()->pos());

        // local file header
        s << quint32(0x04034b50);
        s << quint16(20)    // version needed
          << quint16(0)     // flags
          << quint16(0)     // method: stored
          << quint16(0)     // mod time
          << quint16(0)     // mod date
          << e.crc
          << e.size         // compressed size
          << e.size         // uncompressed size
          << quint16(nameBytes.size())
          << quint16(0);    // extra len
        s.writeRawData(nameBytes.constData(), nameBytes.size());
        s.writeRawData(e.data.constData(), int(e.data.size()));
    }

    const quint32 cdOffset = quint32(s.device()->pos());
    for (const auto &e : entries) {
        const QByteArray nameBytes = e.name.toUtf8();
        // central directory header
        s << quint32(0x02014b50);
        s << quint16(20)    // version made by
          << quint16(20)    // version needed
          << quint16(0)     // flags
          << quint16(0)     // method
          << quint16(0)     // mod time
          << quint16(0)     // mod date
          << e.crc
          << e.size
          << e.size
          << quint16(nameBytes.size())
          << quint16(0)     // extra len
          << quint16(0)     // comment len
          << quint16(0)     // disk number
          << quint16(0)     // internal attrs
          << quint32(0)     // external attrs
          << e.localOffset;
        s.writeRawData(nameBytes.constData(), nameBytes.size());
    }
    const quint32 cdSize = quint32(s.device()->pos()) - cdOffset;

    // end of central directory
    s << quint32(0x06054b50);
    s << quint16(0)                   // disk
      << quint16(0)                   // cd disk
      << quint16(entries.size())
      << quint16(entries.size())
      << cdSize
      << cdOffset
      << quint16(0);                  // comment len
}

bool readZipEntries(const QByteArray &raw, QList<ZipEntry> *entries,
                    const bool fillData = true)
{
    // locate the end-of-central-directory record (scan backwards,
    // comment can be up to 64k but ours are written comment-less)
    qint64 eocd = -1;
    const qint64 minPos = qMax<qint64>(0, raw.size() - 65558);
    for (qint64 i = raw.size() - 22; i >= minPos; i--) {
        if (quint8(raw.at(int(i)))     == 0x50 &&
            quint8(raw.at(int(i) + 1)) == 0x4b &&
            quint8(raw.at(int(i) + 2)) == 0x05 &&
            quint8(raw.at(int(i) + 3)) == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) { return false; }

    // parse the central directory location + size from the eocd:
    //   [16..19] = cd start offset, [12..15] = cd size
    quint32 cdSize = 0, cdStart = 0;
    for (int b = 0; b < 4; b++) {
        cdSize  |= quint32(quint8(raw.at(int(eocd) + 12 + b))) << (8 * b);
        cdStart |= quint32(quint8(raw.at(int(eocd) + 16 + b))) << (8 * b);
    }
    qint64 pos = qint64(cdStart);
    if (pos < 0 || pos + qint64(cdSize) > eocd) { return false; }

    while (pos + 46 <= eocd) {
        if (!(quint8(raw.at(int(pos)))     == 0x50 &&
              quint8(raw.at(int(pos) + 1)) == 0x4b &&
              quint8(raw.at(int(pos) + 2)) == 0x01 &&
              quint8(raw.at(int(pos) + 3)) == 0x02)) {
            break;
        }
        const quint16 nameLen = quint8(raw.at(int(pos) + 28))
                | (quint8(raw.at(int(pos) + 29)) << 8);
        const quint16 extraLen = quint8(raw.at(int(pos) + 30))
                | (quint8(raw.at(int(pos) + 31)) << 8);
        const quint16 commentLen = quint8(raw.at(int(pos) + 32))
                | (quint8(raw.at(int(pos) + 33)) << 8);
        quint32 crc = 0, csize = 0, usize = 0, localOff = 0;
        for (int b = 0; b < 4; b++) {
            crc      |= quint32(quint8(raw.at(int(pos) + 16 + b))) << (8 * b);
            csize    |= quint32(quint8(raw.at(int(pos) + 20 + b))) << (8 * b);
            usize    |= quint32(quint8(raw.at(int(pos) + 24 + b))) << (8 * b);
            localOff |= quint32(quint8(raw.at(int(pos) + 42 + b))) << (8 * b);
        }
        const QString name = QString::fromUtf8(
                    raw.constData() + pos + 46, int(nameLen));

        ZipEntry e;
        e.name = name;
        e.method = quint8(raw.at(int(pos) + 10))
                | (quint8(raw.at(int(pos) + 11)) << 8);
        e.crc = crc;
        e.size = usize;
        e.localOffset = localOff;

        // read the local header to find the real data start
        // (the local extra field length may differ from central)
        const qint64 lh = qint64(localOff);
        if (lh + 30 <= raw.size()) {
            const quint16 lNameLen = quint8(raw.at(int(lh) + 26))
                    | (quint8(raw.at(int(lh) + 27)) << 8);
            const quint16 lExtraLen = quint8(raw.at(int(lh) + 28))
                    | (quint8(raw.at(int(lh) + 29)) << 8);
            const qint64 dataStart = lh + 30 + lNameLen + lExtraLen;
            e.rawStart = lh;
            e.rawLen = dataStart + qint64(csize) - lh;
            if (e.rawStart + e.rawLen > raw.size()) {
                e.rawStart = 0;
                e.rawLen = 0;
            }
            if (fillData) {
                const int count = int(qMin<qint64>(usize,
                                                   raw.size() - dataStart));
                if (count > 0) {
                    e.data = QByteArray(raw.constData() + dataStart, count);
                }
            }
        }
        entries->append(e);

        pos += 46 + nameLen + extraLen + commentLen;
    }
    return !entries->isEmpty();
}

} // namespace

namespace Fpsd {

bool writePackage(const QString &path, const QMap<QString, QByteArray> &entries)
{
    QList<ZipEntry> list;
    for (auto it = entries.begin(); it != entries.end(); it++) {
        ZipEntry e;
        e.name = it.key();
        e.data = it.value();
        list.append(e);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Fpsd: cannot open package for writing" << path;
        return false;
    }
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    writeZipEntries(s, list);
    if (s.status() != QDataStream::Ok) { return false; }
    return file.commit();
}

QMap<QString, QByteArray> readPackage(const QString &path)
{
    QMap<QString, QByteArray> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return result; }
    const QByteArray raw = file.readAll();

    QList<ZipEntry> entries;
    if (!readZipEntries(raw, &entries)) {
        qWarning() << "Fpsd: not a valid package" << path;
        return result;
    }
    for (const auto &e : entries) {
        if (e.size > 0 && e.data.size() == int(e.size)) {
            result.insert(e.name, e.data);
        } else if (e.size == 0) {
            result.insert(e.name, QByteArray());
        }
    }
    return result;
}

QByteArray readPackageEntry(const QString &path, const QString &name)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
    const qint64 fileSize = file.size();
    if (fileSize < 22) { return QByteArray(); }

    // the eocd lives in the last 64k + 22 bytes - read only the tail
    const qint64 tailLen = qMin(fileSize, qint64(65558));
    if (!file.seek(fileSize - tailLen)) { return QByteArray(); }
    const QByteArray tail = file.read(tailLen);
    qint64 eocd = -1;
    for (qint64 i = tail.size() - 22; i >= 0; i--) {
        if (quint8(tail.at(int(i)))     == 0x50 &&
            quint8(tail.at(int(i) + 1)) == 0x4b &&
            quint8(tail.at(int(i) + 2)) == 0x05 &&
            quint8(tail.at(int(i) + 3)) == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) { return QByteArray(); }
    quint32 cdSize = 0, cdStart = 0;
    for (int b = 0; b < 4; b++) {
        cdSize  |= quint32(quint8(tail.at(int(eocd) + 12 + b))) << (8 * b);
        cdStart |= quint32(quint8(tail.at(int(eocd) + 16 + b))) << (8 * b);
    }
    if (qint64(cdStart) + qint64(cdSize) > fileSize) { return QByteArray(); }
    if (!file.seek(cdStart)) { return QByteArray(); }
    const QByteArray cd = file.read(cdSize);
    if (cd.size() != int(cdSize)) { return QByteArray(); }

    const QByteArray nameUtf8 = name.toUtf8();
    qint64 pos = 0;
    while (pos + 46 <= cd.size()) {
        if (!(quint8(cd.at(int(pos)))     == 0x50 &&
              quint8(cd.at(int(pos) + 1)) == 0x4b &&
              quint8(cd.at(int(pos) + 2)) == 0x01 &&
              quint8(cd.at(int(pos) + 3)) == 0x02)) {
            break;
        }
        const quint16 nameLen = quint8(cd.at(int(pos) + 28))
                | (quint8(cd.at(int(pos) + 29)) << 8);
        const quint16 extraLen = quint8(cd.at(int(pos) + 30))
                | (quint8(cd.at(int(pos) + 31)) << 8);
        const quint16 commentLen = quint8(cd.at(int(pos) + 32))
                | (quint8(cd.at(int(pos) + 33)) << 8);
        const quint16 method = quint8(cd.at(int(pos) + 10))
                | (quint8(cd.at(int(pos) + 11)) << 8);
        quint32 csize = 0, usize = 0, localOff = 0;
        for (int b = 0; b < 4; b++) {
            csize    |= quint32(quint8(cd.at(int(pos) + 20 + b))) << (8 * b);
            usize    |= quint32(quint8(cd.at(int(pos) + 24 + b))) << (8 * b);
            localOff |= quint32(quint8(cd.at(int(pos) + 42 + b))) << (8 * b);
        }
        if (int(nameLen) == nameUtf8.size()
                && qstrncmp(cd.constData() + pos + 46,
                            nameUtf8.constData(), nameLen) == 0) {
            // found: read only this entry's local header + payload
            if (method != 0 || !file.seek(localOff)) { return QByteArray(); }
            const QByteArray lh = file.read(30);
            if (lh.size() < 30) { return QByteArray(); }
            const quint16 lNameLen = quint8(lh.at(26))
                    | (quint8(lh.at(27)) << 8);
            const quint16 lExtraLen = quint8(lh.at(28))
                    | (quint8(lh.at(29)) << 8);
            if (!file.seek(qint64(localOff) + 30 + lNameLen + lExtraLen)) {
                return QByteArray();
            }
            QByteArray data = file.read(csize);
            if (data.size() != int(csize)) { return QByteArray(); }
            Q_UNUSED(usize)
            return data;
        }
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return QByteArray();
}

bool updatePackage(const QString &path,
                   const QMap<QString, QByteArray> &updates)
{
    QByteArray raw;
    QList<ZipEntry> oldEntries;
    {
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly)) { return false; }
            raw = file.readAll();
            // read the directory only; entry payloads stay in 'raw'
            if (!readZipEntries(raw, &oldEntries, false)) {
                oldEntries.clear();
                raw.clear();
            }
        }
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning() << "Fpsd: cannot open package for writing" << path;
        return false;
    }
    QDataStream s(&out);
    s.setByteOrder(QDataStream::LittleEndian);

    QList<ZipEntry> finalEntries;
    // unchanged entries: raw-copy the local record verbatim
    // (valid because the package writer always produces STORED
    // entries without data descriptors; anything else falls back
    // to the decoded data read from the old file)
    for (const auto &e : oldEntries) {
        if (updates.contains(e.name)) { continue; }
        // only STORED entries with a verified extent can be kept -
        // anything else (foreign/deflated, truncated record) is
        // dropped rather than risking a corrupt package
        if (e.method != 0 || e.rawLen <= 0) {
            qWarning() << "Fpsd: dropping non-stored entry" << e.name;
            continue;
        }
        ZipEntry keep = e;
        keep.data = QByteArray(raw.constData() + e.rawStart,
                               int(e.rawLen));
        finalEntries.append(keep);
    }
    // new / replaced entries
    for (auto it = updates.begin(); it != updates.end(); it++) {
        ZipEntry e;
        e.name = it.key();
        e.data = it.value();
        finalEntries.append(e);
    }

    // write local records, raw copies verbatim
    for (auto &e : finalEntries) {
        const bool rawCopy = e.rawLen > 0 && e.method == 0
                && !updates.contains(e.name);
        const QByteArray nameBytes = e.name.toUtf8();
        e.localOffset = quint32(s.device()->pos());
        if (rawCopy) {
            s.writeRawData(e.data.constData(), int(e.data.size()));
            continue;
        }
        e.method = 0;
        e.crc = crc32(e.data);
        e.size = quint32(e.data.size());
        s << quint32(0x04034b50);
        s << quint16(20)
          << quint16(0)
          << quint16(0)
          << quint16(0)
          << quint16(0)
          << e.crc
          << e.size
          << e.size
          << quint16(nameBytes.size())
          << quint16(0);
        s.writeRawData(nameBytes.constData(), nameBytes.size());
        s.writeRawData(e.data.constData(), int(e.data.size()));
    }

    const quint32 cdOffset = quint32(s.device()->pos());
    for (const auto &e : finalEntries) {
        const QByteArray nameBytes = e.name.toUtf8();
        s << quint32(0x02014b50);
        s << quint16(20)
          << quint16(20)
          << quint16(0)
          << quint16(0)
          << quint16(0)
          << quint16(0)
          << e.crc
          << e.size
          << e.size
          << quint16(nameBytes.size())
          << quint16(0)
          << quint16(0)
          << quint16(0)
          << quint16(0)
          << quint32(0)
          << e.localOffset;
        s.writeRawData(nameBytes.constData(), nameBytes.size());
    }
    const quint32 cdSize = quint32(s.device()->pos()) - cdOffset;

    s << quint32(0x06054b50);
    s << quint16(0)
      << quint16(0)
      << quint16(finalEntries.size())
      << quint16(finalEntries.size())
      << cdSize
      << cdOffset
      << quint16(0);
    if (s.status() != QDataStream::Ok) { return false; }
    return out.commit();
}

QByteArray metaToJson(const Meta &meta)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("sourcePsd"), meta.sourcePsd);
    root.insert(QStringLiteral("width"), meta.width);
    root.insert(QStringLiteral("height"), meta.height);
    root.insert(QStringLiteral("composite"), meta.composite);

    QJsonArray layers;
    for (const auto &l : meta.layers) {
        QJsonObject o;
        o.insert(QStringLiteral("key"), l.key);
        o.insert(QStringLiteral("layerId"), int(l.layerId));
        o.insert(QStringLiteral("name"), l.name);
        o.insert(QStringLiteral("x"), l.x);
        o.insert(QStringLiteral("y"), l.y);
        o.insert(QStringLiteral("w"), l.w);
        o.insert(QStringLiteral("h"), l.h);
        o.insert(QStringLiteral("hash"), l.hash);
        o.insert(QStringLiteral("opacity"), l.opacity);
        o.insert(QStringLiteral("visible"), l.visible);
        o.insert(QStringLiteral("blendKey"), l.blendKey);
        layers.append(o);
    }
    root.insert(QStringLiteral("layers"), layers);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool metaFromJson(const QByteArray &json, Meta *meta)
{
    if (!meta) { return false; }
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) { return false; }
    const auto root = doc.object();
    meta->sourcePsd = root.value(QStringLiteral("sourcePsd")).toString();
    meta->width = root.value(QStringLiteral("width")).toInt();
    meta->height = root.value(QStringLiteral("height")).toInt();
    meta->composite = root.value(QStringLiteral("composite")).toBool();
    meta->layers.clear();
    const auto layers = root.value(QStringLiteral("layers")).toArray();
    for (const auto &v : layers) {
        const auto o = v.toObject();
        LayerMeta l;
        l.key = o.value(QStringLiteral("key")).toString();
        l.layerId = qint32(o.value(QStringLiteral("layerId")).toInt());
        l.name = o.value(QStringLiteral("name")).toString();
        l.x = o.value(QStringLiteral("x")).toInt();
        l.y = o.value(QStringLiteral("y")).toInt();
        l.w = o.value(QStringLiteral("w")).toInt();
        l.h = o.value(QStringLiteral("h")).toInt();
        l.hash = o.value(QStringLiteral("hash")).toString();
        l.opacity = o.value(QStringLiteral("opacity")).toInt();
        l.visible = o.value(QStringLiteral("visible")).toBool();
        l.blendKey = o.value(QStringLiteral("blendKey")).toString();
        meta->layers.append(l);
    }
    return true;
}

QString cacheDirForPackage(const QString &packagePath)
{
    // normalize separators: the package path may carry a mix of '/' and
    // '\', and readFilePath() resolves to '/'-only on reload, which
    // would silently change the hash and split the cache directory
    const QString clean = QDir::cleanPath(packagePath);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
            clean.toUtf8(), QCryptographicHash::Md5).toHex().left(12));
    return AppSupport::getAppCachePath()
            + QStringLiteral("/PSDCache/") + hash;
}

QString layerEntryName(const QString &key)
{
    return QStringLiteral("layers/") + key + QStringLiteral(".png");
}

QByteArray rgbaToPng(const QByteArray &rgba, const int w, const int h)
{
    if (w <= 0 || h <= 0 || rgba.size() < 4 * w * h) { return QByteArray(); }
    const QImage image(reinterpret_cast<const uchar*>(rgba.constData()),
                       w, h, 4 * w, QImage::Format_RGBA8888);
    if (image.isNull()) { return QByteArray(); }
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly)) { return QByteArray(); }
    // quality 85 -> Qt maps PNG quality to 100-quality compression
    // -> zlib level 1. PNG is lossless: this only trades file size
    // for encode speed (3-5x faster than the default level)
    if (!image.save(&buffer, "PNG", 85)) { return QByteArray(); }
    return png;
}

QString writeLayerCacheFile(const QString &packagePath,
                            const QString &key,
                            const QByteArray &pngData)
{
    const QString dir = cacheDirForPackage(packagePath);
    if (!QDir().mkpath(dir)) { return QString(); }
    // file name carries a short hash of the pixel data: every update
    // writes a brand-new file instead of replacing the old one, which
    // avoids Windows file-locking issues (a loader task may hold the
    // previous cache file open) and lets the box simply point to the
    // new path to force a clean reload
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
            pngData, QCryptographicHash::Md5).toHex().left(8));
    const QString path = dir + QStringLiteral("/") + key
            + QStringLiteral("_") + hash + QStringLiteral(".png");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { return QString(); }
    if (file.write(pngData) != pngData.size()) { return QString(); }
    if (!file.commit()) { return QString(); }
    // opportunistically drop stale versions of the same layer
    QDir cacheDir(dir);
    const auto entries = cacheDir.entryList(
                {key + QStringLiteral("_*.png")}, QDir::Files);
    for (const auto &entry : entries) {
        if (entry != QFileInfo(path).fileName()) {
            QFile::remove(dir + QStringLiteral("/") + entry);
        }
    }
    return path;
}

} // namespace Fpsd
