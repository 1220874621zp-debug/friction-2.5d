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

#include "kraimporter.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>

#include "appsupport.h"
#include "Boxes/imagebox.h"
#include "Boxes/containerbox.h"
#include "canvas.h"
#include "exceptions.h"
#include "Timeline/durationrectangle.h"
#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"

namespace {

// ---------------------------------------------------------------------------
// Minimal raw-DEFLATE inflater (puff-style, RFC 1951).
//
// Needed because Qt's qUncompress only accepts zlib-wrapped streams (it
// verifies the adler32 trailer) while ZIP entries store raw deflate.
// The implementation was validated byte-for-byte against zlib on real
// .kra entries (xml, icc profiles and an 8 MB merged image).
// ---------------------------------------------------------------------------

struct InflateBitIn {
    const uchar* d;
    const qint64 len;
    qint64 pos = 0;
    unsigned bitbuf = 0;
    int bitcnt = 0;

    unsigned bits(const int need) {
        unsigned val = bitbuf;
        while (bitcnt < need) {
            if (pos >= len) RuntimeThrow("kra: unexpected end of deflate stream");
            val |= unsigned(d[pos]) << bitcnt;
            pos++;
            bitcnt += 8;
        }
        bitbuf = val >> need;
        bitcnt -= need;
        return val & ((1u << need) - 1u);
    }
};

struct InflateHuff {
    int count[16] = {0};
    std::vector<int> symbol;

    InflateHuff(const std::vector<int>& lengths) {
        symbol.assign(lengths.size(), 0);
        for (const int l : lengths) {
            if (l < 0 || l > 15) RuntimeThrow("kra: bad huffman code length");
            count[l]++;
        }
        if (count[0] == int(lengths.size())) return; // no codes at all
        // reject over-subscribed code sets
        long left = 1;
        for (int b = 1; b < 16; b++) {
            left <<= 1;
            left -= count[b];
            if (left < 0) RuntimeThrow("kra: over-subscribed huffman code");
        }
        int offs[16] = {0};
        for (int b = 1; b < 15; b++) offs[b + 1] = offs[b] + count[b];
        for (int sym = 0; sym < int(lengths.size()); sym++) {
            const int l = lengths[sym];
            if (l != 0) {
                symbol[offs[l]] = sym;
                offs[l]++;
            }
        }
    }

    int decode(InflateBitIn& bi) const {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length < 16; length++) {
            code |= int(bi.bits(1));
            const int cnt = count[length];
            if (code - first < cnt) return symbol[index + (code - first)];
            index += cnt;
            first += cnt;
            first <<= 1;
            code <<= 1;
        }
        RuntimeThrow("kra: invalid huffman code");
        return -1;
    }
};

const int kInfLens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19,
                          23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                          131, 163, 195, 227, 258};
const int kInfLext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                          3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kInfDists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97,
                           129, 193, 257, 385, 513, 769, 1025, 1537, 2049,
                           3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kInfDext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                          7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const int kInfCOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3,
                            13, 2, 14, 1, 15};

void inflateCodes(InflateBitIn& bi,
                  const InflateHuff& lencode,
                  const InflateHuff& distcode,
                  uchar* out,
                  qint64& outLen,
                  const qint64 outCap)
{
    while (true) {
        const int sym = lencode.decode(bi);
        if (sym < 256) {
            if (outLen >= outCap) RuntimeThrow("kra: inflate output overflow");
            out[outLen++] = uchar(sym);
        } else if (sym == 256) {
            return;
        } else {
            const int li = sym - 257;
            if (li >= 29) RuntimeThrow("kra: bad length symbol");
            const int length = kInfLens[li] + int(bi.bits(kInfLext[li]));
            const int dsym = distcode.decode(bi);
            if (dsym >= 30) RuntimeThrow("kra: bad distance symbol");
            const int dist = kInfDists[dsym] + int(bi.bits(kInfDext[dsym]));
            if (dist > int(outLen)) RuntimeThrow("kra: distance too far back");
            if (outLen + length > outCap) RuntimeThrow("kra: inflate output overflow");
            int from = int(outLen) - dist;
            for (int k = 0; k < length; k++) out[outLen++] = out[from++];
        }
    }
}

void inflateRaw(const uchar* data, const qint64 size,
                uchar* out, const qint64 expectedSize)
{
    InflateBitIn bi{data, size};
    qint64 outLen = 0;
    bool last = false;
    while (!last) {
        last = bi.bits(1) != 0;
        const int type = int(bi.bits(2));
        if (type == 0) {
            // stored block: drop remaining bits, LEN/NLEN, raw copy
            bi.bitbuf = 0;
            bi.bitcnt = 0;
            if (bi.pos + 4 > bi.len) RuntimeThrow("kra: truncated stored block");
            const int ln = bi.d[bi.pos] | (bi.d[bi.pos + 1] << 8);
            const int nln = bi.d[bi.pos + 2] | (bi.d[bi.pos + 3] << 8);
            if (ln != (~nln & 0xFFFF)) RuntimeThrow("kra: stored block length mismatch");
            bi.pos += 4;
            if (bi.pos + ln > bi.len || outLen + ln > expectedSize) {
                RuntimeThrow("kra: truncated stored block");
            }
            memcpy(out + outLen, bi.d + bi.pos, size_t(ln));
            outLen += ln;
            bi.pos += ln;
        } else if (type == 1) {
            std::vector<int> lengths(288);
            int i = 0;
            for (; i < 144; i++) lengths[i] = 8;
            for (; i < 256; i++) lengths[i] = 9;
            for (; i < 280; i++) lengths[i] = 7;
            for (; i < 288; i++) lengths[i] = 8;
            const InflateHuff lencode(lengths);
            const InflateHuff distcode(std::vector<int>(30, 5));
            inflateCodes(bi, lencode, distcode, out, outLen, expectedSize);
        } else if (type == 2) {
            const int nlen = int(bi.bits(5)) + 257;
            const int ndist = int(bi.bits(5)) + 1;
            const int ncode = int(bi.bits(4)) + 4;
            if (nlen > 286 || ndist > 30) RuntimeThrow("kra: too many code lengths");
            std::vector<int> clLengths(19, 0);
            for (int j = 0; j < ncode; j++) clLengths[kInfCOrder[j]] = int(bi.bits(3));
            const InflateHuff clcode(clLengths);
            std::vector<int> lengths(nlen + ndist, 0);
            int i = 0;
            while (i < nlen + ndist) {
                const int sym = clcode.decode(bi);
                if (sym < 16) {
                    lengths[i++] = sym;
                } else {
                    int prev = 0;
                    int rep;
                    if (sym == 16) {
                        if (i == 0) RuntimeThrow("kra: repeat with no previous length");
                        prev = lengths[i - 1];
                        rep = 3 + int(bi.bits(2));
                    } else if (sym == 17) {
                        rep = 3 + int(bi.bits(3));
                    } else {
                        rep = 11 + int(bi.bits(7));
                    }
                    if (i + rep > nlen + ndist) RuntimeThrow("kra: too many lengths");
                    for (int j = 0; j < rep; j++) lengths[i++] = prev;
                }
            }
            if (lengths[256] == 0) RuntimeThrow("kra: missing end-of-block code");
            const InflateHuff lencode(std::vector<int>(lengths.begin(),
                                                       lengths.begin() + nlen));
            const InflateHuff distcode(std::vector<int>(lengths.begin() + nlen,
                                                        lengths.end()));
            inflateCodes(bi, lencode, distcode, out, outLen, expectedSize);
        } else {
            RuntimeThrow("kra: invalid deflate block type");
        }
    }
    if (outLen != expectedSize) RuntimeThrow("kra: inflated size mismatch");
}

// ---------------------------------------------------------------------------
// ZIP container reader (STORED + DEFLATE entries)
// ---------------------------------------------------------------------------

struct KraZipEntry {
    QString name;
    quint16 method = 0;
    quint32 csize = 0;
    quint32 usize = 0;
    qint64 dataStart = 0;
};

class KraZip {
public:
    bool load(const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        mRaw = file.readAll();
        if (mRaw.size() < 22) return false;

        // locate the end-of-central-directory record (scan backwards)
        qint64 eocd = -1;
        const qint64 minPos = qMax<qint64>(0, mRaw.size() - 65558);
        for (qint64 i = mRaw.size() - 22; i >= minPos; i--) {
            if (quint8(mRaw.at(int(i))) == 0x50 && quint8(mRaw.at(int(i) + 1)) == 0x4b &&
                quint8(mRaw.at(int(i) + 2)) == 0x05 && quint8(mRaw.at(int(i) + 3)) == 0x06) {
                eocd = i;
                break;
            }
        }
        if (eocd < 0) return false;

        quint32 cdSize = 0, cdStart = 0;
        for (int b = 0; b < 4; b++) {
            cdSize |= quint32(quint8(mRaw.at(int(eocd) + 12 + b))) << (8 * b);
            cdStart |= quint32(quint8(mRaw.at(int(eocd) + 16 + b))) << (8 * b);
        }
        qint64 pos = qint64(cdStart);
        if (pos < 0 || pos + qint64(cdSize) > eocd) return false;

        while (pos + 46 <= eocd) {
            if (!(quint8(mRaw.at(int(pos))) == 0x50 &&
                  quint8(mRaw.at(int(pos) + 1)) == 0x4b &&
                  quint8(mRaw.at(int(pos) + 2)) == 0x01 &&
                  quint8(mRaw.at(int(pos) + 3)) == 0x02)) {
                break;
            }
            const quint16 nameLen = quint8(mRaw.at(int(pos) + 28)) |
                    (quint8(mRaw.at(int(pos) + 29)) << 8);
            const quint16 extraLen = quint8(mRaw.at(int(pos) + 30)) |
                    (quint8(mRaw.at(int(pos) + 31)) << 8);
            const quint16 commentLen = quint8(mRaw.at(int(pos) + 32)) |
                    (quint8(mRaw.at(int(pos) + 33)) << 8);
            KraZipEntry e;
            e.method = quint8(mRaw.at(int(pos) + 10)) |
                    (quint8(mRaw.at(int(pos) + 11)) << 8);
            for (int b = 0; b < 4; b++) {
                e.csize |= quint32(quint8(mRaw.at(int(pos) + 20 + b))) << (8 * b);
                e.usize |= quint32(quint8(mRaw.at(int(pos) + 24 + b))) << (8 * b);
                const quint32 localOff =
                        quint32(quint8(mRaw.at(int(pos) + 42 + b))) << (8 * b);
                e.dataStart |= localOff;
            }
            e.name = QString::fromUtf8(mRaw.constData() + pos + 46, int(nameLen));
            if (e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu) {
                RuntimeThrow("kra: ZIP64 archives are not supported");
            }
            // local header holds the real data offset (its extra field
            // length may differ from the central directory one)
            const qint64 lh = e.dataStart;
            if (lh + 30 > mRaw.size()) return false;
            const quint16 lNameLen = quint8(mRaw.at(int(lh) + 26)) |
                    (quint8(mRaw.at(int(lh) + 27)) << 8);
            const quint16 lExtraLen = quint8(mRaw.at(int(lh) + 28)) |
                    (quint8(mRaw.at(int(lh) + 29)) << 8);
            e.dataStart = lh + 30 + lNameLen + lExtraLen;
            if (e.dataStart + qint64(e.csize) > mRaw.size()) return false;
            mEntries << e;
            pos += 46 + nameLen + extraLen + commentLen;
        }
        if (mEntries.isEmpty()) return false;
        return true;
    }

    bool has(const QString& name) const {
        for (const auto& e : mEntries) {
            if (e.name == name) return true;
        }
        return false;
    }

    QByteArray read(const QString& name) const {
        for (const auto& e : mEntries) {
            if (e.name != name) continue;
            if (e.method == 0) {
                return QByteArray(mRaw.constData() + e.dataStart, int(e.csize));
            } else if (e.method == 8) {
                QByteArray out(int(e.usize), Qt::Uninitialized);
                inflateRaw(reinterpret_cast<const uchar*>(mRaw.constData() + e.dataStart),
                           e.csize,
                           reinterpret_cast<uchar*>(out.data()),
                           qint64(e.usize));
                return out;
            } else {
                RuntimeThrow("kra: unsupported zip compression method " +
                             std::to_string(e.method));
            }
        }
        RuntimeThrow("kra: missing zip entry " + name.toStdString());
        return QByteArray();
    }

    // any "<dir>/layers/..." entries -> "<dir>/layers/" prefixes
    QStringList layersPrefixes() const {
        QStringList result;
        for (const auto& e : mEntries) {
            const int idx = e.name.indexOf(QLatin1String("/layers/"));
            if (idx > 0) {
                const QString prefix = e.name.left(idx + 8);
                if (!result.contains(prefix)) result << prefix;
            }
        }
        return result;
    }

private:
    QByteArray mRaw;
    QList<KraZipEntry> mEntries;
};

// ---------------------------------------------------------------------------
// LZF tile decompression + tiled paint-device decoding
// ---------------------------------------------------------------------------

// port of Krita's lzff_decompress semantics (kis_lzf_compression.cpp)
int lzfDecompress(const uchar* in, const int inLen,
                  uchar* out, const int outLen)
{
    int ip = 0, op = 0;
    const int ipLimit = inLen - 1;
    while (ip < ipLimit) {
        const unsigned ctrl = unsigned(in[ip]) + 1u;
        const unsigned ofs = (unsigned(in[ip]) & 31u) << 8;
        unsigned len = unsigned(in[ip]) >> 5;
        ip++;
        if (ctrl < 33) {
            if (op + int(ctrl) > outLen || ip + int(ctrl) > inLen) return -1;
            for (unsigned k = 0; k < ctrl; k++) out[op++] = in[ip++];
        } else {
            len--;
            int ref = op - int(ofs) - 1;
            if (len == 7 - 1) {
                if (ip >= inLen) return -1;
                len += in[ip++];
            }
            if (ip >= inLen) return -1;
            ref -= in[ip++];
            if (ref < 0) return -1;
            const unsigned total = len + 3;
            if (op + int(total) > outLen) return -1;
            for (unsigned k = 0; k < total; k++) { out[op++] = out[ref++]; }
        }
    }
    return op;
}

// channel-planar -> interleaved (see KisAbstractCompression::delinearizeColors)
void delinearize(const uchar* in, uchar* out, const int dataSize,
                 const int pixelSize)
{
    const int stride = dataSize / pixelSize;
    int ob = 0;
    int start = 0;
    while (ob < dataSize) {
        int ib = start;
        for (int i = 0; i < pixelSize; i++) {
            out[ob++] = in[ib];
            ib += stride;
        }
        start++;
    }
}

// Krita's divideRoundDown
qint32 kraFloorDiv(const qint32 x, const qint32 y)
{
    return x >= 0 ? x / y : -(((-x - 1) / y) + 1);
}

QByteArray readLineAt(const uchar*& p, const uchar* end)
{
    const uchar* start = p;
    while (p < end && *p != '\n') p++;
    if (p >= end) RuntimeThrow("kra: truncated tile header");
    const QByteArray line(reinterpret_cast<const char*>(start), int(p - start));
    p++; // skip '\n'
    return line;
}

struct KraImageData {
    QImage mImage;
    int mOriginX = 0; // canvas-space origin of mImage (before layer offset)
    int mOriginY = 0;
};

// Decodes a Krita tiled paint-device file ("VERSION 2 ... DATA n" +
// tiles) into an ARGB32 image. tileSize is 64 in every file seen in
// practice, but the header value is honored.
KraImageData decodeTiled(const QByteArray& raw, const QString& colorSpace)
{
    const uchar* p = reinterpret_cast<const uchar*>(raw.constData());
    const uchar* end = p + raw.size();

    int tileW = 64, tileH = 64, pixelSize = 4, tileCount = -1;
    for (int line = 0; line < 5; line++) {
        const QByteArray l = readLineAt(p, end);
        int sp = l.indexOf(' ');
        if (sp < 0) RuntimeThrow("kra: bad tile header line");
        const QString key = QString::fromLatin1(l.left(sp));
        const int value = QString::fromLatin1(l.mid(sp + 1)).toInt();
        if (key == QLatin1String("VERSION")) {
            if (value != 2) {
                RuntimeThrow("kra: unsupported tile format version " +
                             std::to_string(value));
            }
        } else if (key == QLatin1String("TILEWIDTH")) tileW = value;
        else if (key == QLatin1String("TILEHEIGHT")) tileH = value;
        else if (key == QLatin1String("PIXELSIZE")) pixelSize = value;
        else if (key == QLatin1String("DATA")) tileCount = value;
    }
    if (tileCount < 0 || tileW <= 0 || tileH <= 0 || pixelSize <= 0) {
        RuntimeThrow("kra: bad tile header");
    }
    if (tileCount == 0) return KraImageData(); // empty layer

    // pixel format from colorspace name + pixel size
    enum class PxFormat { bgra8, rgba16, gray8, gray16, unsupported };
    PxFormat fmt = PxFormat::unsupported;
    {
        const QString cs = colorSpace.toUpper();
        if (cs.startsWith(QLatin1String("RGBA")) ||
            cs.startsWith(QLatin1String("SRGB"))) {
            if (pixelSize == 4) fmt = PxFormat::bgra8;
            else if (pixelSize == 8) fmt = PxFormat::rgba16;
        } else if (cs.startsWith(QLatin1String("GRAYA"))) {
            if (pixelSize == 2) fmt = PxFormat::gray8;
            else if (pixelSize == 4) fmt = PxFormat::gray16;
        }
    }
    if (fmt == PxFormat::unsupported) {
        RuntimeThrow("kra: unsupported color space " + colorSpace.toStdString() +
                     " (pixel size " + std::to_string(pixelSize) + ")");
    }

    // pass 1: parse tile headers to find the cell extent
    struct TileHdr { int col, row; const uchar* data; int dataLen; };
    QVector<TileHdr> headers;
    headers.reserve(tileCount);
    int minCol = INT_MAX, minRow = INT_MAX, maxCol = INT_MIN, maxRow = INT_MIN;
    const int tileBytes = tileW * tileH * pixelSize;
    const uchar* rewind = p;
    for (int i = 0; i < tileCount; i++) {
        const QByteArray l = readLineAt(p, end);
        const QList<QByteArray> parts = l.trimmed().split(',');
        if (parts.count() != 4) RuntimeThrow("kra: bad tile header");
        bool okX = false, okY = false, okS = false;
        const int x = parts.at(0).toInt(&okX);
        const int y = parts.at(1).toInt(&okY);
        const int size = parts.at(3).toInt(&okS);
        if (!okX || !okY || !okS || parts.at(2) != "LZF") {
            RuntimeThrow("kra: bad tile header");
        }
        if (p + size > end) RuntimeThrow("kra: truncated tile data");
        // like Krita's own reader: tiles live on the tileW/tileH grid
        // (files written by current Krita always store aligned extents)
        const int col = kraFloorDiv(x, tileW);
        const int row = kraFloorDiv(y, tileH);
        minCol = qMin(minCol, col);
        minRow = qMin(minRow, row);
        maxCol = qMax(maxCol, col);
        maxRow = qMax(maxRow, row);
        // data points at the flag byte; the header size counts it, so
        // the payload at data+1 holds size-1 bytes
        headers.append({col, row, p, size});
        p += size;
    }
    const int imgW = (maxCol - minCol + 1) * tileW;
    const int imgH = (maxRow - minRow + 1) * tileH;
    KraImageData result;
    result.mImage = QImage(imgW, imgH, QImage::Format_ARGB32);
    if (result.mImage.isNull()) RuntimeThrow("kra: cannot allocate layer image");
    result.mImage.fill(Qt::transparent);
    result.mOriginX = minCol * tileW;
    result.mOriginY = minRow * tileH;

    QByteArray planarBuf; // reused for LZF tiles
    QByteArray interBuf;
    const QRgb* dstBase = reinterpret_cast<const QRgb*>(result.mImage.bits());
    const int dstStride = result.mImage.bytesPerLine() / 4;
    for (const auto& h : headers) {
        const uchar* tileData = nullptr;
        if (h.data[0] == 0) { // flag byte: raw, interleaved
            tileData = h.data + 1;
            if (h.dataLen - 1 < tileBytes) RuntimeThrow("kra: short raw tile");
        } else {
            // LZF-compressed planar -> decompress -> interleave
            if (planarBuf.size() < tileBytes) planarBuf.resize(tileBytes);
            if (interBuf.size() < tileBytes) interBuf.resize(tileBytes);
            const int got = lzfDecompress(h.data + 1, h.dataLen - 1,
                                          reinterpret_cast<uchar*>(planarBuf.data()),
                                          tileBytes);            if (got != tileBytes) RuntimeThrow("kra: LZF tile decode failed");
            delinearize(reinterpret_cast<const uchar*>(planarBuf.constData()),
                        reinterpret_cast<uchar*>(interBuf.data()),
                        tileBytes, pixelSize);
            tileData = reinterpret_cast<const uchar*>(interBuf.constData());
        }
        const int baseX = (h.col - minCol) * tileW;
        const int baseY = (h.row - minRow) * tileH;
        const int clipW = qMin(tileW, imgW - baseX);
        const int clipH = qMin(tileH, imgH - baseY);
        for (int r = 0; r < clipH; r++) {
            QRgb* dst = const_cast<QRgb*>(dstBase + (baseY + r) * dstStride + baseX);
            const uchar* src = tileData + r * tileW * pixelSize;
            for (int c = 0; c < clipW; c++) {
                const uchar* px = src + c * pixelSize;
                int r8, g8, b8, a8;
                switch (fmt) {
                case PxFormat::bgra8:
                    b8 = px[0]; g8 = px[1]; r8 = px[2]; a8 = px[3];
                    break;
                case PxFormat::rgba16: {
                    const quint16 r16 = qFromLittleEndian<quint16>(px);
                    const quint16 g16 = qFromLittleEndian<quint16>(px + 2);
                    const quint16 b16 = qFromLittleEndian<quint16>(px + 4);
                    const quint16 a16 = qFromLittleEndian<quint16>(px + 6);
                    r8 = r16 >> 8; g8 = g16 >> 8; b8 = b16 >> 8; a8 = a16 >> 8;
                    break;
                }
                case PxFormat::gray8:
                    r8 = g8 = b8 = px[0]; a8 = px[1];
                    break;
                default: { // gray16
                    const quint16 g16 = qFromLittleEndian<quint16>(px);
                    const quint16 a16 = qFromLittleEndian<quint16>(px + 2);
                    r8 = g8 = b8 = g16 >> 8; a8 = a16 >> 8;
                    break;
                }
                }
                dst[c] = qRgba(r8, g8, b8, a8);
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Blend mode mapping (Krita compositeop ids use SVG/CSS names, the same
// set the OCA importer maps - reuse that table with '_' normalized)
// ---------------------------------------------------------------------------

SkBlendMode blendModeFromKrita(const QString& mode)
{
    QString m = mode.toLower();
    m.replace(QLatin1Char('_'), QLatin1Char('-'));
    if (m == QLatin1String("multiply"))   { return SkBlendMode::kMultiply; }
    if (m == QLatin1String("screen"))     { return SkBlendMode::kScreen; }
    if (m == QLatin1String("overlay"))    { return SkBlendMode::kOverlay; }
    if (m == QLatin1String("darken"))     { return SkBlendMode::kDarken; }
    if (m == QLatin1String("lighten"))    { return SkBlendMode::kLighten; }
    if (m == QLatin1String("difference")) { return SkBlendMode::kDifference; }
    if (m == QLatin1String("exclusion"))  { return SkBlendMode::kExclusion; }
    if (m == QLatin1String("color-dodge") || m == QLatin1String("dodge"))
                                          { return SkBlendMode::kColorDodge; }
    if (m == QLatin1String("color-burn") || m == QLatin1String("burn"))
                                          { return SkBlendMode::kColorBurn; }
    if (m == QLatin1String("hard-light")) { return SkBlendMode::kHardLight; }
    if (m == QLatin1String("soft-light")) { return SkBlendMode::kSoftLight; }
    if (m == QLatin1String("hue"))        { return SkBlendMode::kHue; }
    if (m == QLatin1String("saturation")) { return SkBlendMode::kSaturation; }
    if (m == QLatin1String("color"))      { return SkBlendMode::kColor; }
    if (m == QLatin1String("luminosity") || m == QLatin1String("value"))
                                          { return SkBlendMode::kLuminosity; }
    if (m == QLatin1String("dissolve"))   { return SkBlendMode::kSrcOver; }
    if (m == QLatin1String("destination-in"))
                                          { return SkBlendMode::kDstIn; }
    if (m == QLatin1String("destination-out"))
                                          { return SkBlendMode::kDstOut; }
    if (m == QLatin1String("source-atop"))
                                          { return SkBlendMode::kSrcATop; }
    // "normal" and Krita-specific modes with no skia equivalent
    return SkBlendMode::kSrcOver;
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

struct KraKeyFrame {
    int mTime = 0;
    QString mFrameFile;
    int mOffX = 0;
    int mOffY = 0;
};

struct ImportState {
    const KraZip* mZip = nullptr;
    QString mLayersPrefix;
    QString mImageColorSpace;
    int mRangeTo = 0;
    ImportKRA::ProgressReporter mReport;
    int mTotalFrames = 0;
    int mDoneFrames = 0;
    QString mCacheDir;
    QHash<QString, QString> mFrameToCache;
    QStringList* mSkipped = nullptr;
};

QString cachePathForFrame(ImportState& st, const QString& frameFile,
                          const QImage& image)
{
    const auto it = st.mFrameToCache.constFind(frameFile);
    if (it != st.mFrameToCache.constEnd()) return it.value();
    if (!QDir().mkpath(st.mCacheDir)) {
        RuntimeThrow("kra: cannot create cache dir " + st.mCacheDir.toStdString());
    }
    QBuffer pngBuf;
    pngBuf.open(QIODevice::WriteOnly);
    if (!image.save(&pngBuf, "PNG")) {
        RuntimeThrow("kra: png encode failed");
    }
    // hash-named files: re-imports of edited documents never fight a
    // Windows file lock held by a previous loader task
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
                pngBuf.data(), QCryptographicHash::Md5).toHex().left(8));
    const QString base = frameFile;
    QString safeBase = base;
    safeBase.replace(QLatin1Char('/'), QLatin1Char('_'));
    const QString path = st.mCacheDir + QLatin1Char('/') + safeBase +
            QLatin1Char('_') + hash + QLatin1String(".png");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        RuntimeThrow("kra: cannot write cache file " + path.toStdString());
    }
    if (file.write(pngBuf.data()) != pngBuf.size() || !file.commit()) {
        RuntimeThrow("kra: cannot write cache file " + path.toStdString());
    }
    // drop stale versions of the same frame
    QDir cacheDir(st.mCacheDir);
    const auto entries = cacheDir.entryList({safeBase + QLatin1String("_*.png")},
                                            QDir::Files);
    for (const auto& entry : entries) {
        if (entry != QFileInfo(path).fileName()) {
            QFile::remove(st.mCacheDir + QLatin1Char('/') + entry);
        }
    }
    st.mFrameToCache.insert(frameFile, path);
    return path;
}

void noteSkipped(ImportState& st, const QString& name, const QString& reason)
{
    if (st.mSkipped) *st.mSkipped << name + QStringLiteral(" (") + reason + ')';
}

// apply shared layer properties; names are set AFTER insertion because
// makeNameUniqueForDescendants() strips non-ascii names
void applyLayerProps(BoundingBox* const box, const QDomElement& elem,
                     ContainerBox* const parent)
{
    parent->addContained(box->ref<BoundingBox>());
    const QString name = elem.attribute(QStringLiteral("name"));
    if (!name.isEmpty()) box->prp_setName(name);
    const int opacity = elem.attribute(QStringLiteral("opacity"), "255").toInt();
    if (opacity < 255) {
        box->getBoxTransformAnimator()->setOpacity(
                    qBound(0., opacity / 2.55, 100.));
    }
    if (elem.attribute(QStringLiteral("visible"), "1") != "1") box->hide();
    box->setBlendModeSk(blendModeFromKrita(
                elem.attribute(QStringLiteral("compositeop"), "normal")));
}

QVector<KraKeyFrame> parseKeyFrames(const QByteArray& xml)
{
    QDomDocument doc;
    QString errMsg;
    if (!doc.setContent(xml, &errMsg)) {
        RuntimeThrow("kra: bad keyframes xml: " + errMsg.toStdString());
    }
    QVector<KraKeyFrame> result;
    const QDomElement root = doc.documentElement();
    QDomElement channel = root.firstChildElement(QStringLiteral("channel"));
    while (!channel.isNull()) {
        if (channel.attribute(QStringLiteral("name")) ==
            QLatin1String("content")) break;
        channel = channel.nextSiblingElement(QStringLiteral("channel"));
    }
    if (channel.isNull()) return result;
    QDomElement kf = channel.firstChildElement(QStringLiteral("keyframe"));
    while (!kf.isNull()) {
        KraKeyFrame k;
        k.mTime = kf.attribute(QStringLiteral("time"), "0").toInt();
        k.mFrameFile = kf.attribute(QStringLiteral("frame"));
        const QDomElement off = kf.firstChildElement(QStringLiteral("offset"));
        if (!off.isNull()) {
            k.mOffX = off.attribute(QStringLiteral("x"), "0").toInt();
            k.mOffY = off.attribute(QStringLiteral("y"), "0").toInt();
        }
        if (!k.mFrameFile.isEmpty()) result << k;
        kf = kf.nextSiblingElement(QStringLiteral("keyframe"));
    }
    std::sort(result.begin(), result.end(),
              [](const KraKeyFrame& a, const KraKeyFrame& b)
              { return a.mTime < b.mTime; });
    return result;
}

qsptr<BoundingBox> buildPaintLayer(const QDomElement& elem,
                                   ContainerBox* const parent,
                                   ImportState& st)
{
    const QString fileName = elem.attribute(QStringLiteral("filename"),
                                            elem.attribute(QStringLiteral("name")));
    if (fileName.isEmpty()) return nullptr;
    const QString cs = elem.attribute(QStringLiteral("colorspacename"),
                                      st.mImageColorSpace);
    const QString keyframesAttr = elem.attribute(QStringLiteral("keyframes"));

    QVector<KraKeyFrame> keys;
    if (!keyframesAttr.isEmpty()) {
        keys = parseKeyFrames(st.mZip->read(st.mLayersPrefix + keyframesAttr));
    }

    // warn about masks (they are not imported; rendering may differ)
    const QDomElement masks = elem.firstChildElement(QStringLiteral("masks"));
    if (!masks.isNull() && masks.childNodes().count() > 0) {
        noteSkipped(st, elem.attribute(QStringLiteral("name"), fileName),
                    QObject::tr("masks are not imported"));
    }

    struct FrameBox {
        qsptr<ImageBox> mBox;
        int mTime = 0;
        int mOriginX = 0, mOriginY = 0, mOffX = 0, mOffY = 0;
    };
    QVector<FrameBox> frames;

    try {
        if (keys.isEmpty()) {
            // static layer: pixels live in the main file
            const auto raw = st.mZip->read(st.mLayersPrefix + fileName);
            const auto decoded = decodeTiled(raw, cs);
            if (decoded.mImage.isNull()) {
                noteSkipped(st, elem.attribute(QStringLiteral("name"), fileName),
                            QObject::tr("empty layer"));
                return nullptr;
            }
            st.mDoneFrames++;
            if (st.mReport) st.mReport(st.mDoneFrames, st.mTotalFrames);
            const QString cachePath = cachePathForFrame(st, fileName,
                                                        decoded.mImage);
            auto box = enve::make_shared<ImageBox>(cachePath);
            FrameBox fb;
            fb.mBox = box;
            fb.mOriginX = decoded.mOriginX;
            fb.mOriginY = decoded.mOriginY;
            fb.mOffX = elem.attribute(QStringLiteral("x"), "0").toInt();
            fb.mOffY = elem.attribute(QStringLiteral("y"), "0").toInt();
            frames << fb;
        } else {
            for (const auto& k : keys) {
                const auto raw = st.mZip->read(st.mLayersPrefix + k.mFrameFile);
                const auto decoded = decodeTiled(raw, cs);
                if (decoded.mImage.isNull()) {
                    noteSkipped(st, elem.attribute(QStringLiteral("name"), fileName),
                                QObject::tr("empty frame"));
                    continue;
                }
                st.mDoneFrames++;
                if (st.mReport) st.mReport(st.mDoneFrames, st.mTotalFrames);
                const QString cachePath = cachePathForFrame(st, k.mFrameFile,
                                                            decoded.mImage);
                auto box = enve::make_shared<ImageBox>(cachePath);
                FrameBox fb;
                fb.mBox = box;
                fb.mTime = k.mTime;
                fb.mOriginX = decoded.mOriginX;
                fb.mOriginY = decoded.mOriginY;
                fb.mOffX = k.mOffX;
                fb.mOffY = k.mOffY;
                frames << fb;
            }
        }
    } catch(const std::exception& e) {
        noteSkipped(st, elem.attribute(QStringLiteral("name"), fileName),
                    QString::fromUtf8(e.what()));
        return nullptr;
    }
    if (frames.isEmpty()) return nullptr;

    // exposure + position; box properties (name/opacity/blend/visible)
    // are applied to every frame box
    for (int i = 0; i < frames.count(); i++) {
        auto& fb = frames[i];
        fb.mBox->getBoxTransformAnimator()->getPosAnimator()->setBaseValue(
                    fb.mOriginX + fb.mOffX, fb.mOriginY + fb.mOffY);
        if (frames.count() > 1) {
            const int minFrame = fb.mTime;
            int maxFrame;
            if (i + 1 < frames.count()) {
                maxFrame = frames[i + 1].mTime - 1;
            } else {
                maxFrame = qMax(st.mRangeTo, fb.mTime);
            }
            if (maxFrame < minFrame) maxFrame = minFrame;
            const auto dur = enve::make_shared<DurationRectangle>(*fb.mBox.get());
            dur->setMinRelFrame(minFrame);
            dur->setMaxRelFrame(maxFrame);
            fb.mBox->setDurationRectangle(dur);
        }
        applyLayerProps(fb.mBox.get(), elem, parent);
    }
    return frames.first().mBox->ref<BoundingBox>();
}

qsptr<BoundingBox> buildNode(const QDomElement& elem,
                             ContainerBox* const parent,
                             ImportState& st);

void buildChildren(const QDomElement& layersElem,
                   ContainerBox* const parent,
                   ImportState& st)
{
    // Krita stores layers top-to-bottom (canvas order); addContained
    // PREPENDS, so iterate bottom-first to keep the stacking order
    QVector<QDomElement> children;
    QDomElement child = layersElem.firstChildElement(QStringLiteral("layer"));
    while (!child.isNull()) {
        children << child;
        child = child.nextSiblingElement(QStringLiteral("layer"));
    }
    for (int i = children.count() - 1; i >= 0; i--) {
        buildNode(children.at(i), parent, st);
    }
}

qsptr<BoundingBox> buildNode(const QDomElement& elem,
                             ContainerBox* const parent,
                             ImportState& st)
{
    const QString nodeType = elem.attribute(QStringLiteral("nodetype"),
                                            QStringLiteral("paintlayer"));
    if (nodeType == QLatin1String("grouplayer")) {
        const auto group = enve::make_shared<ContainerBox>(eBoxType::group);
        parent->addContained(group);
        const QString name = elem.attribute(QStringLiteral("name"));
        if (!name.isEmpty()) group->prp_setName(name);
        const int opacity = elem.attribute(QStringLiteral("opacity"), "255").toInt();
        if (opacity < 255) {
            group->getBoxTransformAnimator()->setOpacity(
                        qBound(0., opacity / 2.55, 100.));
        }
        if (elem.attribute(QStringLiteral("visible"), "1") != "1") group->hide();
        group->setBlendModeSk(blendModeFromKrita(
                    elem.attribute(QStringLiteral("compositeop"), "normal")));
        const QDomElement layers = elem.firstChildElement(QStringLiteral("layers"));
        if (!layers.isNull()) buildChildren(layers, group.get(), st);
        return group;
    }
    if (nodeType == QLatin1String("paintlayer")) {
        return buildPaintLayer(elem, parent, st);
    }
    QString reason;
    if (nodeType == QLatin1String("shapelayer") ||
        nodeType == QLatin1String("vectorlayer")) {
        reason = QObject::tr("vector layer");
    } else if (nodeType == QLatin1String("adjustmentlayer") ||
               nodeType == QLatin1String("filterlayer")) {
        reason = QObject::tr("filter layer");
    } else if (nodeType == QLatin1String("generatorlayer")) {
        reason = QObject::tr("generator layer");
    } else if (nodeType == QLatin1String("clonelayer")) {
        reason = QObject::tr("clone layer");
    } else if (nodeType == QLatin1String("filelayer")) {
        reason = QObject::tr("file layer");
    } else {
        reason = QObject::tr("unsupported layer type %1").arg(nodeType);
    }
    noteSkipped(st, elem.attribute(QStringLiteral("name"), nodeType), reason);
    return nullptr;
}

int countPaintFrames(const QDomElement& layersElem, const KraZip& zip,
                     const QString& prefix)
{
    // best-effort progress estimate: keyframes of animated paint layers
    // plus one for each static one (bad xml simply stops counting)
    int total = 0;
    QDomElement child = layersElem.firstChildElement(QStringLiteral("layer"));
    while (!child.isNull()) {
        const QString nodeType = child.attribute(QStringLiteral("nodetype"),
                                                 QStringLiteral("paintlayer"));
        if (nodeType == QLatin1String("paintlayer")) {
            const QString kf = child.attribute(QStringLiteral("keyframes"));
            if (!kf.isEmpty()) {
                try {
                    total += parseKeyFrames(zip.read(prefix + kf)).count();
                } catch(...) { total++; }
            } else {
                total++;
            }
        } else if (nodeType == QLatin1String("grouplayer")) {
            const QDomElement sub = child.firstChildElement(
                        QStringLiteral("layers"));
            if (!sub.isNull()) total += countPaintFrames(sub, zip, prefix);
        }
        child = child.nextSiblingElement(QStringLiteral("layer"));
    }
    return total;
}

struct AnimInfo {
    double mFps = 0;
    int mRangeFrom = 0;
    int mRangeTo = 0;
};

AnimInfo readAnimInfo(const KraZip& zip, const QDomElement& imageElem,
                      const QString& dir)
{
    AnimInfo result;
    // prefer <image>/animation/index.xml, fall back to maindoc <animation>
    const QString indexPath = dir + QStringLiteral("/animation/index.xml");
    QDomElement animElem;
    QDomDocument indexDoc;
    if (zip.has(indexPath)) {
        try {
            QString errMsg;
            if (indexDoc.setContent(zip.read(indexPath), &errMsg)) {
                animElem = indexDoc.documentElement();
            }
        } catch(...) {}
    }
    if (animElem.isNull()) {
        animElem = imageElem.firstChildElement(QStringLiteral("animation"));
    }
    if (animElem.isNull()) return result;
    QDomElement e = animElem.firstChildElement(QStringLiteral("framerate"));
    if (!e.isNull()) {
        result.mFps = e.attribute(QStringLiteral("value"), "0").toDouble();
    }
    e = animElem.firstChildElement(QStringLiteral("range"));
    if (!e.isNull()) {
        result.mRangeFrom = e.attribute(QStringLiteral("from"), "0").toInt();
        result.mRangeTo = e.attribute(QStringLiteral("to"), "0").toInt();
    }
    return result;
}

} // namespace

bool ImportKRA::looksLikeKRA(const QString& filePath)
{
    try {
        KraZip zip;
        if (!zip.load(filePath)) return false;
        if (!zip.has(QStringLiteral("mimetype"))) return false;
        return zip.read(QStringLiteral("mimetype")) ==
                QByteArrayLiteral("application/x-krita");
    } catch(...) { return false; }
}

qsptr<ContainerBox> ImportKRA::loadKRAFile(const QString& filePath,
                                           Canvas* const scene,
                                           const ProgressReporter& report,
                                           QStringList* skippedOut)
{
    KraZip zip;
    if (!zip.load(filePath)) {
        RuntimeThrow("Not a valid zip archive: " + filePath.toStdString());
    }
    if (!looksLikeKRA(filePath)) {
        RuntimeThrow("Not a Krita document (mimetype mismatch): " +
                     filePath.toStdString());
    }

    QDomDocument doc;
    {
        QString errMsg;
        const QByteArray maindoc = zip.read(QStringLiteral("maindoc.xml"));
        if (!doc.setContent(maindoc, &errMsg)) {
            RuntimeThrow("kra: bad maindoc.xml: " + errMsg.toStdString());
        }
    }
    const QDomElement docElem = doc.documentElement();
    if (docElem.tagName() != QLatin1String("DOC")) {
        RuntimeThrow("kra: maindoc.xml is not a Krita document");
    }
    // krita writes syntaxVersion as "2" or "2.0" depending on version -
    // compare as double so "2.0".toInt() failing to 0 does not reject
    // valid documents as ancient
    const double syntaxVersion = docElem.attribute(
                QStringLiteral("syntaxVersion"), "2").toDouble();
    if (syntaxVersion < 2.) {
        RuntimeThrow("kra: Krita 1.x documents are not supported - "
                     "re-save the file with a current Krita first");
    }
    if (syntaxVersion >= 3.) {
        RuntimeThrow("kra: document format is newer than supported");
    }

    const QDomElement imageElem = docElem.firstChildElement(
                QStringLiteral("IMAGE"));
    if (imageElem.isNull()) {
        RuntimeThrow("kra: maindoc.xml has no IMAGE element");
    }

    // image directory: "<name>/layers/" (fall back to scanning entries,
    // same trick Krita's loader uses for odd image names)
    const QString imageName = imageElem.attribute(QStringLiteral("name"));
    QString prefix;
    {
        const QString want = imageName + QLatin1String("/layers/");
        if (zip.has(want)) prefix = want;
        else {
            const QStringList candidates = zip.layersPrefixes();
            if (candidates.isEmpty()) {
                RuntimeThrow("kra: no layer data found in the archive");
            }
            prefix = candidates.first();
        }
    }

    const QString imageCs = imageElem.attribute(
                QStringLiteral("colorspacename"), QStringLiteral("RGBA"));
    // "<name>/layers/" minus "/layers/" (8 chars) = "<name>"
    const QString imageDir = prefix.left(prefix.count() - 8);
    const auto animInfo = readAnimInfo(zip, imageElem, imageDir);

    ImportState st;
    st.mZip = &zip;
    st.mLayersPrefix = prefix;
    st.mImageColorSpace = imageCs;
    st.mRangeTo = animInfo.mRangeTo;
    st.mReport = report;
    st.mSkipped = skippedOut;
    {
        const QString clean = QDir::cleanPath(filePath);
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(
                    clean.toUtf8(), QCryptographicHash::Md5).toHex().left(12));
        // configurable root (preferences); empty setting = cache default
        QString kraRoot = AppSupport::getSettings(
                    QStringLiteral("settings"),
                    QStringLiteral("KraCachePath")).toString();
        kraRoot = kraRoot.isEmpty() ?
                    AppSupport::getAppCachePath() +
                        QStringLiteral("/KRACache") :
                    QDir::cleanPath(kraRoot);
        st.mCacheDir = kraRoot + QLatin1Char('/') + hash;
    }

    // configure an empty scene with the document's canvas + framerate
    if (scene && scene->getContainedBoxesCount() == 0) {
        const int w = imageElem.attribute(QStringLiteral("width"), "0").toInt();
        const int h = imageElem.attribute(QStringLiteral("height"), "0").toInt();
        if (w > 0 && h > 0) scene->setCanvasSize(w, h);
        if (animInfo.mFps > 0) scene->setFps(animInfo.mFps);
    }

    const QDomElement layersElem = imageElem.firstChildElement(
                QStringLiteral("layers"));
    if (layersElem.isNull()) {
        RuntimeThrow("kra: document has no layers");
    }
    st.mTotalFrames = countPaintFrames(layersElem, zip, prefix);
    if (st.mReport) st.mReport(0, qMax(st.mTotalFrames, 1));

    const auto rootGroup = enve::make_shared<ContainerBox>(eBoxType::group);
    rootGroup->prp_setName(QFileInfo(filePath).completeBaseName());
    buildChildren(layersElem, rootGroup.get(), st);

    if (skippedOut && skippedOut->isEmpty() && rootGroup->getContainedBoxesCount() == 0) {
        RuntimeThrow("kra: no importable paint layers found");
    }
    return rootGroup;
}
