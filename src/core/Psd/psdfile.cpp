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

// PSD/PSB file reader, architecture inspired by PhotoshopAPI
// https://github.com/EmilDohne/PhotoshopAPI (BSD-3-Clause)
//
// Supports: layer tree (groups/dividers), unicode names, opacity,
// visibility, blend mode keys, RLE/Raw compression, 8/16/32-bit depth,
// RGB and Grayscale color modes, layer masks and flattened composites.

#include "psdfile.h"

#include <QFile>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QElapsedTimer>

#include <cstring>
#include <limits>

namespace {

bool readSignature(QDataStream &s, const char *sig)
{
    char buf[4];
    if (s.readRawData(buf, 4) != 4) { return false; }
    return qstrncmp(buf, sig, 4) == 0;
}

QString readFourCC(QDataStream &s, bool *ok = nullptr)
{
    char buf[5] = {0, 0, 0, 0, 0};
    const int read = s.readRawData(buf, 4);
    if (ok) { *ok = (read == 4); }
    return QString::fromLatin1(buf);
}

// Pascal string padded to 4-byte boundary (layer names)
QString readPascalString(QDataStream &s)
{
    quint8 len = 0;
    s >> len;
    const int padded = (int(len) + 4) & ~3;
    const int count = padded - 1;
    if (count <= 0) {
        for (int i = 0; i < qMax(count, 0); i++) { quint8 b; s >> b; }
        return QString();
    }
    QByteArray bytes(count, Qt::Uninitialized);
    s.readRawData(bytes.data(), count);
    // Photoshop stores the pascal name in the system codepage
    // (GBK on Chinese Windows), not Latin-1
    return QString::fromLocal8Bit(bytes.constData(), int(len));
}

// 'luni' unicode string: uint32 char count followed by count
// UTF-16BE chars (verified against real files; there is no inner
// length field - the block size already covers count + chars)
QString readUnicodeString(QDataStream &s, qint64 blockEnd)
{
    const qint64 avail = blockEnd - s.device()->pos();
    if (avail < 4) { return QString(); }
    quint32 chars = 0;
    s >> chars;
    // clamp with 64-bit math: casting chars to int wraps negative when
    // chars >= 0x80000000, which would silently skip the clamp and make
    // the loop below spin for billions of iterations
    const qint64 maxChars = (avail - 4) / 2;
    if (qint64(chars) > maxChars) { chars = quint32(qMax<qint64>(maxChars, 0)); }
    QString result;
    result.reserve(int(chars));
    for (quint32 i = 0; i < chars && s.status() == QDataStream::Ok; i++) {
        quint16 ch = 0;
        s >> ch;
        if (ch != 0) { result.append(QChar(ch)); }
    }
    return result;
}

// ---- AMT descriptor reader (lfxp layer effects) ----
// Minimal read-only walker for Photoshop's layer-styles block: it
// covers every value type Photoshop is known to write there so the
// stream can be walked past uninteresting keys; anything exotic
// aborts the whole block and styles fall back to their defaults.

namespace {

struct DescError {
    DescError(const char *why) : reason(why) {}
    const char *reason = "";
};

struct DescValue {
    QByteArray tag;      // OSType, e.g. "doub", "enum"
    bool boolV = false;
    double doubleV = 0;
    QString textV;
    QByteArray enumV;
    QHash<QByteArray, DescValue> objV; // Objc/GlbO: key -> value
    QVector<DescValue> listV;          // VlLs items ('*Multi' lists)
};

class DescReader {
public:
    DescReader(QDataStream &s, const qint64 end) : mS(s), mEnd(end) {}

    DescValue readDescriptorBody() {
        DescValue v;
        v.tag = QByteArrayLiteral("Objc");
        // unicode name: int32 char count, 0 = no name
        qint32 nameChars = 0;
        mS >> nameChars;
        if (nameChars > 0) {
            const qint64 maxChars = qMax<qint64>(0, (mEnd - mS.device()->pos()) / 2);
            const qint32 lim = qint32(qMin<qint64>(nameChars, maxChars));
            for (qint32 i = 0; i < lim; i++) { quint16 ch = 0; mS >> ch; }
        }
        readId(); // class ID, unused
        qint32 count = 0;
        mS >> count;
        check();
        if (count < 0 || count > 8192) {
            qWarning() << "PSD lfx2: bad descriptor count" << count;
            throw DescError("desccount");
        }
        for (qint32 i = 0; i < count; i++) {
            const QByteArray key = readId();
            v.objV.insert(key, readValue());
        }
        return v;
    }
private:
    QDataStream &mS;
    const qint64 mEnd;

    void check() {
        if (mS.status() != QDataStream::Ok) {
            qWarning() << "PSD lfx2: stream status bad";
            throw DescError("stream");
        }
        if (mS.device()->pos() > mEnd) {
            qWarning() << "PSD lfx2: bounds exceeded pos"
                       << mS.device()->pos() << "end" << mEnd;
            throw DescError("bounds");
        }
    }

    double readDouble() {
        // the shared stream is in SinglePrecision mode (channel data),
        // operator>>(double&) would consume only 4 bytes; descriptor
        // doubles are always 8-byte big-endian
        char buf[8];
        if (mS.readRawData(buf, 8) != 8) {
            qWarning() << "PSD lfx2: short read on double";
            throw DescError("dbl");
        }
        quint64 bits = 0;
        for (int i = 0; i < 8; i++) { bits = (bits << 8) | quint8(buf[i]); }
        double d = 0;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    QByteArray readFourCCRaw() {
        char buf[4];
        if (mS.readRawData(buf, 4) != 4) {
            qWarning() << "PSD lfx2: short read on 4cc at" << mS.device()->pos();
            throw DescError("4cc");
        }
        return QByteArray(buf, 4);
    }

    QByteArray readId() {
        qint32 len = 0;
        mS >> len;
        check();
        if (len == 0) { len = 4; }
        else if (len < 0 || len > 8192) {
            qWarning() << "PSD lfx2: bad id length" << len;
            throw DescError("idlen");
        }
        QByteArray bytes(len, Qt::Uninitialized);
        if (mS.readRawData(bytes.data(), len) != len) {
            qWarning() << "PSD lfx2: short read on id" << len;
            throw DescError("idread");
        }
        return bytes;
    }

    QString readUnicode() {
        qint32 chars = 0;
        mS >> chars;
        check();
        QString out;
        if (chars <= 0) { return out; }
        if (chars > (1 << 20)) {
            qWarning() << "PSD lfx2: bad text length" << chars;
            throw DescError("textlen");
        }
        out.reserve(chars);
        for (qint32 i = 0; i < chars; i++) {
            quint16 ch = 0;
            mS >> ch;
            if (ch) { out.append(QChar(ch)); }
        }
        return out;
    }

    void skipBytes(const qint64 n) {
        qint64 skip = n;
        while (skip > 0 && mS.status() == QDataStream::Ok) {
            const int chunk = int(qMin<qint64>(skip, 1 << 24));
            if (mS.skipRawData(chunk) < 0) { break; }
            skip -= chunk;
        }
        check();
    }

    void readReference() {
        qint32 count = 0;
        mS >> count;
        check();
        if (count < 0 || count > 4096) {
            qWarning() << "PSD lfx2: bad reference count" << count;
            throw DescError("refcount");
        }
        for (qint32 i = 0; i < count; i++) {
            const QByteArray form = readFourCCRaw();
            readId(); // class ID
            if (form == "prop") { readId(); }
            else if (form == "Enmr") { readId(); readId(); }
            else if (form == "rele" || form == "Idnt" || form == "indx") {
                qint32 dummy = 0;
                mS >> dummy;
            } else if (form == "name") { readUnicode(); }
            // "Clss" carries nothing beyond the class ID
            check();
        }
    }

    DescValue readValue() {
        const QByteArray tag = readFourCCRaw();
        DescValue v;
        v.tag = tag;
        if (tag == "obj ") {
            readReference();
        } else if (tag == "Objc" || tag == "GlbO") {
            return readDescriptorBody();
        } else if (tag == "VlLs") {
            qint32 count = 0;
            mS >> count;
            check();
            if (count < 0) { count = 0; }
            if (count > 65536) {
                qWarning() << "PSD lfx2: bad list count" << count;
                throw DescError("listcount");
            }
            for (qint32 i = 0; i < count; i++) {
                v.listV.append(readValue());
            }
        } else if (tag == "doub") {
            v.doubleV = readDouble();
        } else if (tag == "UntF") {
            readFourCCRaw(); // unit, e.g. "#Prc"
            v.doubleV = readDouble();
        } else if (tag == "TEXT") {
            v.textV = readUnicode();
        } else if (tag == "enum") {
            readId(); // enum type
            v.enumV = readId();
        } else if (tag == "long") {
            qint32 i = 0;
            mS >> i;
            v.doubleV = i;
        } else if (tag == "bool") {
            quint8 b = 0;
            mS >> b;
            v.boolV = b;
        } else if (tag == "type" || tag == "GlbC") {
            readId();
        } else if (tag == "tdta") {
            qint32 len = 0;
            mS >> len;
            check();
            if (len < 0) {
                qWarning() << "PSD lfx2: bad tdta length" << len;
                throw DescError("tdta");
            }
            skipBytes((qint64(len) + 3) & ~qint64(3));
        } else {
            qWarning() << "PSD lfx2: unhandled type" << tag
                       << "at" << mS.device()->pos();
            throw DescError("type");
        }
        check();
        return v;
    }
};

const DescValue* objChild(const DescValue &obj, const QByteArray &key)
{
    const auto it = obj.objV.constFind(key);
    if (it == obj.objV.constEnd()) { return nullptr; }
    return &it.value();
}

bool styleEnabled(const DescValue &obj)
{
    const auto e = objChild(obj, QByteArrayLiteral("enab"));
    return e ? e->boolV : true;
}

double styleNum(const DescValue &obj, const QByteArray &key, const double def)
{
    const auto v = objChild(obj, key);
    return v ? v->doubleV : def;
}

bool styleColor(const DescValue &obj, const QByteArray &key,
                quint8 &r, quint8 &g, quint8 &b)
{
    const auto c = objChild(obj, key);
    if (!c) { return false; }
    const auto rd = objChild(*c, QByteArrayLiteral("Rd  "));
    const auto gr = objChild(*c, QByteArrayLiteral("Grn "));
    const auto bl = objChild(*c, QByteArrayLiteral("Bl  "));
    if (!rd || !gr || !bl) { return false; }
    // PS6-era lfx2 stores color channels in 0..255, the CS2+ lfxp
    // in 0..1: scale per component (> 1 means already 8-bit).
    // User-verified against a real file: shadow #3d1b05 reads 61/27/4.5
    const auto to8 = [](const double v) {
        return quint8(qBound(0.0, v > 1.0 ? v : v * 255.0, 255.0));
    };
    r = to8(rd->doubleV);
    g = to8(gr->doubleV);
    b = to8(bl->doubleV);
    return true;
}

void fillShadow(psd::LayerStyles &st, const DescValue &obj)
{
    st.hasAny = true;
    st.shadowEnabled = true;
    st.shadowOpacity = styleNum(obj, QByteArrayLiteral("Opct"), st.shadowOpacity);
    st.shadowAngle = styleNum(obj, QByteArrayLiteral("lagl"), st.shadowAngle);
    st.shadowDistance = styleNum(obj, QByteArrayLiteral("Dstn"), st.shadowDistance);
    // lfxp stores choke as #Prc percent; some lfx2 writers emit #Pxl
    // - clamp either way, percent semantics
    st.shadowSpread = qBound(0.0, styleNum(obj, QByteArrayLiteral("Ckmt"),
                                            st.shadowSpread), 100.0);
    st.shadowSize = styleNum(obj, QByteArrayLiteral("blur"), st.shadowSize);
    styleColor(obj, QByteArrayLiteral("Clr "),
               st.shadowR, st.shadowG, st.shadowB);
}

void fillGlow(psd::LayerStyles &st, const DescValue &obj)
{
    st.hasAny = true;
    st.glowEnabled = true;
    st.glowOpacity = styleNum(obj, QByteArrayLiteral("Opct"), st.glowOpacity);
    st.glowSpread = qBound(0.0, styleNum(obj, QByteArrayLiteral("Ckmt"),
                                          st.glowSpread), 100.0);
    st.glowSize = styleNum(obj, QByteArrayLiteral("blur"), st.glowSize);
    styleColor(obj, QByteArrayLiteral("Clr "),
               st.glowR, st.glowG, st.glowB);
}

void fillStroke(psd::LayerStyles &st, const DescValue &obj)
{
    st.hasAny = true;
    st.strokeEnabled = true;
    st.strokeOpacity = styleNum(obj, QByteArrayLiteral("Opct"), st.strokeOpacity);
    st.strokeSize = styleNum(obj, QByteArrayLiteral("Sz  "), st.strokeSize);
    const auto styl = objChild(obj, QByteArrayLiteral("Styl"));
    if (styl) {
        if (styl->enumV == QByteArrayLiteral("FStF")) { st.strokePos = 1; }
        else if (styl->enumV == QByteArrayLiteral("InsF")) { st.strokePos = 2; }
        else { st.strokePos = 0; } // "OutF"
    }
    styleColor(obj, QByteArrayLiteral("Clr "),
               st.strokeR, st.strokeG, st.strokeB);
}

void applyLfxpStyles(const DescValue &root, psd::LayerRecord &rec)
{
    // 'lfxp' (CS2+) and 'lfx2' (PS 6) use different effect key
    // spellings; PS 2015+ stores additional instances of a type in
    // '<effect>Multi' lists (dropShadowMulti etc), with the single
    // legacy key holding the first instance
    QVector<const DescValue*> shadows, glows, strokes;
    const auto collect = [&root](const QByteArray& singleA,
                                 const QByteArray& singleB,
                                 const QByteArray& multi,
                                 QVector<const DescValue*>& out) {
        const DescValue* single = objChild(root, singleA);
        if (!single) { single = objChild(root, singleB); }
        if (single && styleEnabled(*single)) { out.append(single); }
        const auto m = objChild(root, multi);
        if (m) {
            for (const auto& e : m->listV) {
                if (e.tag == QByteArrayLiteral("Objc") && styleEnabled(e)) {
                    out.append(&e);
                }
            }
        }
    };
    collect(QByteArrayLiteral("dropShadow"), QByteArrayLiteral("DrSh"),
            QByteArrayLiteral("dropShadowMulti"), shadows);
    collect(QByteArrayLiteral("outerGlow"), QByteArrayLiteral("OrGl"),
            QByteArrayLiteral("outerGlowMulti"), glows);
    collect(QByteArrayLiteral("frameFX"), QByteArrayLiteral("FrFX"),
            QByteArrayLiteral("frameFXMulti"), strokes);
    if (shadows.isEmpty() && glows.isEmpty() && strokes.isEmpty()) { return; }

    // first instance of each type merges into one effect; every
    // additional instance becomes its own entry (stacked effects)
    psd::LayerStyles main;
    if (!shadows.isEmpty()) { fillShadow(main, *shadows.first()); }
    if (!glows.isEmpty()) { fillGlow(main, *glows.first()); }
    if (!strokes.isEmpty()) { fillStroke(main, *strokes.first()); }
    for (int i = 1; i < shadows.size(); i++) {
        rec.stylesList.append(psd::LayerStyles());
        fillShadow(rec.stylesList.last(), *shadows.at(i));
    }
    for (int i = 1; i < glows.size(); i++) {
        rec.stylesList.append(psd::LayerStyles());
        fillGlow(rec.stylesList.last(), *glows.at(i));
    }
    for (int i = 1; i < strokes.size(); i++) {
        rec.stylesList.append(psd::LayerStyles());
        fillStroke(rec.stylesList.last(), *strokes.at(i));
    }
    rec.stylesList.prepend(main);

    qWarning() << "PSD layer styles:" << rec.stylesList.size()
               << "effect(s): shadow x" << shadows.size()
               << "glow x" << glows.size()
               << "stroke x" << strokes.size();
}

} // namespace

void readLfxpStyles(QDataStream &s, const qint64 blockEnd, psd::LayerRecord &rec)
{
    try {
        // version (0 or 2), descriptor version (16), then descriptor
        qint32 version = 0;
        qint32 descVersion = 0;
        s >> version >> descVersion;
        DescReader reader(s, blockEnd);
        const DescValue root = reader.readDescriptorBody();
        applyLfxpStyles(root, rec);
    } catch (...) {
        // malformed or unexpected descriptor: keep defaults, the
        // caller skips the rest of the block regardless
        qWarning() << "PSD layer styles: descriptor parse failed,"
                      " keeping defaults";
    }
}

// PackBits decompression straight into the destination buffer.
// Returns the number of bytes written (< dstSize on truncation).
int uncompressRLETo(const char *srcPtr, const int srcSize,
                    char *dstPtr, const int dstSize)
{
    int srcPos = 0;
    int dstPos = 0;
    while (srcPos < srcSize && dstPos < dstSize) {
        const qint8 n = qint8(srcPtr[srcPos++]);
        if (n >= 0) {
            const int count = qMin(int(n) + 1, dstSize - dstPos);
            const int available = srcSize - srcPos;
            const int toCopy = qMin(count, available);
            if (toCopy > 0) {
                memcpy(dstPtr + dstPos, srcPtr + srcPos, size_t(toCopy));
                srcPos += toCopy;
                dstPos += toCopy;
            }
            if (toCopy < count) { break; }
        } else if (n > -128) {
            const int count = qMin(1 - int(n), dstSize - dstPos);
            if (srcPos >= srcSize) { break; }
            const char value = srcPtr[srcPos++];
            memset(dstPtr + dstPos, value, size_t(count));
            dstPos += count;
        } // -128: no-op
    }
    return dstPos;
}

inline quint8 depthTo8Raw(const uchar *data, int channelSize)
{
    if (channelSize == 1) {
        return *data;
    } else if (channelSize == 2) {
        const quint32 v = quint32((data[0] << 8) | data[1]);
        // integer (v*255 + 32767)/65535 - same rounding as
        // qRound(v * 255.0 / 65535.0) without the float ops
        return quint8((v * 255u + 32767u) / 65535u);
    } else { // 4-byte float
        // memcpy instead of reinterpret_cast: the channel data is
        // not guaranteed to be aligned for a float load (UB)
        const quint32 raw = (quint32(data[0]) << 24)
                          | (quint32(data[1]) << 16)
                          | (quint32(data[2]) << 8)
                          | quint32(data[3]);
        float f;
        memcpy(&f, &raw, sizeof(float));
        const float c = qBound(0.0f, f, 1.0f);
        return quint8(qRound(c * 255.0f));
    }
}

quint8 depthTo8(const QByteArray &data, int pos, int channelSize)
{
    if (pos + channelSize > data.size()) { return 0; }
    return depthTo8Raw(reinterpret_cast<const uchar*>(data.constData()) + pos, channelSize);
}

} // namespace

namespace psd {

bool PsdFile::load(const QString &path, QString *error)
{
    mLayers.clear();
    mPath = path;
    mWidth = mHeight = 0;
    mDepth = 8;
    mChannelCount = 3;
    mMode = ColorMode::RGB;
    mIsPsb = false;
    mCompositeOffset = 0;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) { *error = QStringLiteral("Cannot open file %1").arg(path); }
        return false;
    }

    // Read the whole file in one pass and parse from memory. Thousands
    // of tiny buffered reads + seeks on the live file can stall in
    // filter drivers (AV / cloud-sync sandbox injection).
    {
        QElapsedTimer timer;
        timer.start();
        mData = file.readAll();
        qWarning() << "PSD: loaded" << mData.size() << "bytes into memory in"
                   << timer.elapsed() << "ms";
    }
    file.close();
    if (mData.size() < 26) {
        if (error) { *error = QStringLiteral("File too small to be a PSD"); }
        return false;
    }

    QBuffer buffer(&mData);
    buffer.open(QIODevice::ReadOnly);
    QDataStream s(&buffer);
    s.setByteOrder(QDataStream::BigEndian);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);

    // ---- Header ----
    if (!readSignature(s, "8BPS")) {
        if (error) { *error = QStringLiteral("Not a Photoshop file (bad signature)"); }
        return false;
    }
    quint16 version = 0;
    s >> version;
    if (version != 1 && version != 2) {
        if (error) { *error = QStringLiteral("Unsupported PSD version %1").arg(version); }
        return false;
    }
    mIsPsb = (version == 2);
    s.skipRawData(6); // reserved
    quint16 channels = 0;
    qint32 height = 0;
    qint32 width = 0;
    quint16 depth = 0;
    quint16 colorMode = 0;
    s >> channels >> height >> width >> depth >> colorMode;
    if (s.status() != QDataStream::Ok) {
        if (error) { *error = QStringLiteral("Corrupted PSD header"); }
        return false;
    }
    mWidth = int(width);
    mHeight = int(height);
    mChannelCount = int(channels);
    mDepth = int(depth);
    switch (colorMode) {
    case 1: mMode = ColorMode::Grayscale; break;
    case 3: mMode = ColorMode::RGB; break;
    case 4: mMode = ColorMode::CMYK; break;
    default: mMode = ColorMode::Unsupported; break;
    }
    if (mMode == ColorMode::Unsupported) {
        if (error) { *error = QStringLiteral("Unsupported PSD color mode %1").arg(colorMode); }
        return false;
    }
    if (mDepth != 8 && mDepth != 16 && mDepth != 32) {
        if (error) { *error = QStringLiteral("Unsupported PSD depth %1").arg(mDepth); }
        return false;
    }

    // ---- Color mode data ----
    quint32 cmdLength = 0;
    s >> cmdLength;
    s.skipRawData(int(cmdLength));

    // ---- Image resources ----
    quint32 resLength = 0;
    s >> resLength;
    s.skipRawData(int(resLength));

    // ---- Layer and mask info ----
    qint64 layerMaskLength;
    if (mIsPsb) { quint64 v = 0; s >> v; layerMaskLength = qint64(v); }
    else { quint32 v = 0; s >> v; layerMaskLength = qint64(v); }
    if (s.status() != QDataStream::Ok) {
        if (error) { *error = QStringLiteral("Corrupted PSD (layer section)"); }
        return false;
    }
    const qint64 sectionEnd = s.device()->pos() + layerMaskLength;
    qWarning() << "PSD header ok:" << mWidth << "x" << mHeight
            << "depth" << mDepth << "channels" << mChannelCount
            << "psb" << mIsPsb << "layerSectionSize" << layerMaskLength;
    if (!readLayerSection(s, sectionEnd, error)) { return false; }
    qWarning() << "PSD parsed, layer records:" << mLayers.size();

    // composite image follows the layer & mask section
    mCompositeOffset = sectionEnd;
    return true;
}

bool PsdFile::readLayerSection(QDataStream &s, qint64 sectionEnd,
                               QString *error)
{
    if (s.device()->pos() >= sectionEnd) {
        // no layer info at all (flattened only)
        return true;
    }
    qint64 layerInfoLength;
    if (mIsPsb) { quint64 v = 0; s >> v; layerInfoLength = qint64(v); }
    else { quint32 v = 0; s >> v; layerInfoLength = qint64(v); }
    if (layerInfoLength < 0
            || s.device()->pos() + layerInfoLength > sectionEnd) {
        // clamp out-of-range declared length to the remaining section
        layerInfoLength = sectionEnd - s.device()->pos();
    }
    const qint64 layerInfoEnd = s.device()->pos() + layerInfoLength;

    qWarning() << "PSD: reading layer records, infoLength" << layerInfoLength;
    if (!readLayerRecords(s, layerInfoEnd, error)) { return false; }
    qWarning() << "PSD: layer records done," << mLayers.size() << "layer(s)";
    if (!readChannelImageDataOffsets(s, layerInfoEnd, error)) { return false; }
    qWarning() << "PSD: channel offsets done";

    // 16/32-bit PSD files store layer info in Lr16/Lr32 additional
    // blocks; the main layer info section is empty in that case.
    if (mLayers.isEmpty() && layerInfoLength == 0
            && layerInfoEnd < sectionEnd) {
        s.device()->seek(layerInfoEnd);
        qint64 lastPos = -1;
        while (s.device()->pos() + 12 <= sectionEnd) {
            const qint64 loopStart = s.device()->pos();
            // bail out if the cursor did not advance (corrupt data)
            if (loopStart == lastPos || s.status() != QDataStream::Ok) { break; }
            lastPos = loopStart;
            char sigBuf[4];
            if (s.readRawData(sigBuf, 4) != 4) { break; }
            const bool isLarge = qstrncmp(sigBuf, "8B64", 4) == 0;
            if (qstrncmp(sigBuf, "8BIM", 4) != 0 && !isLarge) { break; }
            const QString key = readFourCC(s);
            qint64 blockSize;
            if (isLarge || mIsPsb) { quint64 v = 0; s >> v; blockSize = qint64(v); }
            else { quint32 v = 0; s >> v; blockSize = qint64(v); }
            if (blockSize < 0 || blockSize > (qint64(1) << 30)) { break; }
            const qint64 blockEnd = s.device()->pos() + blockSize;
            if (blockEnd > sectionEnd) { break; }

            if (key == QLatin1String("Lr16")
                    || key == QLatin1String("Lr32")) {
                // payload mirrors the layer info section: length + records
                qint64 subLen;
                if (mIsPsb || key == QLatin1String("Lr32")) {
                    quint64 v = 0; s >> v; subLen = qint64(v);
                } else { quint32 v = 0; s >> v; subLen = qint64(v); }
                const qint64 subEnd = s.device()->pos() + subLen;
                if (subEnd > 0 && subEnd <= blockEnd) {
                    if (!readLayerRecords(s, subEnd, error)) { return false; }
                    if (!readChannelImageDataOffsets(s, subEnd, error)) { return false; }
                    qWarning() << "PSD: found" << (key == QLatin1String("Lr16") ? 16 : 32)
                            << "bit layer section with" << mLayers.size() << "layer(s)";
                    return true;
                }
                s.device()->seek(blockEnd);
                continue;
            }
            // skip other blocks in chunks to avoid int overflow
            qint64 skip = blockEnd - s.device()->pos();
            if ((mIsPsb || isLarge) && (blockSize % 4) != 0) { skip += 4 - (blockSize % 4); }
            while (skip > 0 && s.status() == QDataStream::Ok) {
                const int chunk = int(qMin(skip, qint64(1 << 24)));
                if (s.skipRawData(chunk) < 0) { break; }
                skip -= chunk;
            }
            if (s.device()->pos() <= loopStart) { break; }
        }
    }
    return true;
}

bool PsdFile::readLayerRecords(QDataStream &s, qint64 layerInfoEnd,
                               QString *error)
{
    if (s.device()->pos() >= layerInfoEnd) { return true; }

    qint16 layerCount = 0;
    s >> layerCount;
    int count = qAbs(int(layerCount));
    mLayers.clear();
    mLayers.reserve(count);
    qWarning() << "PSD: layer count" << layerCount << "->" << count;

    for (int i = 0; i < count; i++) {
        qWarning() << "PSD: parsing layer record" << i;
        LayerRecord rec;
        rec.index = i;
        qint32 top = 0, left = 0, bottom = 0, right = 0;
        quint16 nChannels = 0;
        s >> top >> left >> bottom >> right >> nChannels;
        if (s.status() != QDataStream::Ok) {
            if (error) { *error = QStringLiteral("Corrupted layer record"); }
            return false;
        }
        rec.rect = QRect(left, top, right - left, bottom - top);

        for (int c = 0; c < nChannels; c++) {
            ChannelInfo info;
            qint16 id = 0;
            s >> id;
            info.id = int(id);
            if (mIsPsb) { quint64 v = 0; s >> v; info.length = qint64(v); }
            else { quint32 v = 0; s >> v; info.length = qint64(v); }
            if (id == -2) { rec.hasMask = true; }
            rec.channels.append(info);
        }

        if (!readSignature(s, "8BIM")) {
            if (error) { *error = QStringLiteral("Corrupted blend mode signature"); }
            return false;
        }
        rec.blendKey = readFourCC(s);
        quint8 opacity = 0, clipping = 0, flags = 0, filler = 0;
        s >> opacity >> clipping >> flags >> filler;
        Q_UNUSED(clipping)
        Q_UNUSED(filler)
        rec.opacity = int(opacity);
        rec.visible = !(flags & 2);

        // ---- extra data ----
        quint32 extraLength = 0;
        s >> extraLength;
        const qint64 extraStart = s.device()->pos();
        const qint64 extraEnd = extraStart + qint64(extraLength);

        // layer mask data
        quint32 maskLength = 0;
        s >> maskLength;
        if (maskLength > 0) {
            const qint64 maskBlockEnd = s.device()->pos() + qint64(maskLength);
            if (maskLength >= 20) {
                qint32 mTop = 0, mLeft = 0, mBottom = 0, mRight = 0;
                quint8 defaultColor = 0, mFlags = 0;
                s >> mTop >> mLeft >> mBottom >> mRight >> defaultColor >> mFlags;
                rec.maskRect = QRect(mLeft, mTop, mRight - mLeft, mBottom - mTop);
                rec.hasMask = rec.hasMask && !(mFlags & 2); // disabled
            }
            s.device()->seek(maskBlockEnd);
        }

        // blending ranges
        quint32 blendingLength = 0;
        s >> blendingLength;
        if (blendingLength > 0) { s.skipRawData(int(blendingLength)); }

        // pascal name
        rec.name = readPascalString(s);

        // additional layer info blocks
        // guard against infinite loops: bail out when the cursor
        // stops advancing or the stream gets corrupted
        qint64 lastPos = -1;
        while (s.device()->pos() + 12 <= extraEnd) {
            const qint64 loopStart = s.device()->pos();
            if (loopStart == lastPos || s.status() != QDataStream::Ok) { break; }
            lastPos = loopStart;
            char sigBuf[4];
            if (s.readRawData(sigBuf, 4) != 4) { break; }
            const bool isImageResource = qstrncmp(sigBuf, "8BIM", 4) == 0;
            const bool isLarge = qstrncmp(sigBuf, "8B64", 4) == 0;
            if (!isImageResource && !isLarge) { break; }
            const QString key = readFourCC(s);
            qint64 blockSize;
            if (isLarge || mIsPsb) { quint64 v = 0; s >> v; blockSize = qint64(v); }
            else { quint32 v = 0; s >> v; blockSize = qint64(v); }
            if (blockSize < 0 || blockSize > (qint64(1) << 30)) { break; }
            const qint64 blockStart = s.device()->pos();
            const qint64 blockEnd = blockStart + blockSize;
            if (blockEnd > extraEnd) { break; }

            if (key == QLatin1String("luni")) {
                const QString uni = readUnicodeString(s, blockEnd);
                if (!uni.isEmpty()) { rec.name = uni; }
            } else if (key == QLatin1String("lyid")) {
                // native layer id: stable across rename / move / regroup
                quint32 id = 0;
                s >> id;
                rec.layerId = qint32(id);
            } else if (key == QLatin1String("lfxp")
                       || key == QLatin1String("lfx2")
                       || key == QLatin1String("lmfx")) {
                // Photoshop layer styles (descriptor-based); 'lfxp' is
                // the CS2+ spelling, 'lfx2' the Photoshop 6-era one with
                // short effect keys (DrSh/OrGl/FrFX), 'lmfx' (layer
                // multi fx) is where current Photoshop stores styles
                // when a type has multiple instances - the legacy
                // 'lrFX' block only mirrors the first instance and is
                // skipped. All three share the same body layout.
                readLfxpStyles(s, blockEnd, rec);
            } else if (key == QLatin1String("lsct")
                       || key == QLatin1String("lsdk")) {
                quint32 dividerType = 0;
                s >> dividerType;
                switch (dividerType) {
                case 1: rec.divider = Divider::OpenFolder; break;
                case 2: rec.divider = Divider::ClosedFolder; break;
                case 3: rec.divider = Divider::BoundingDivider; break;
                default: rec.divider = Divider::None; break;
                }
            }

            qint64 skip = blockEnd - s.device()->pos();
            // PSB pads additional blocks to 4-byte boundary
            if ((mIsPsb || isLarge) && (blockSize % 4) != 0) { skip += 4 - (blockSize % 4); }
            // skip in chunks: a single call may fail or exceed int range
            while (skip > 0 && s.status() == QDataStream::Ok) {
                const int chunk = int(qMin(skip, qint64(1 << 24)));
                if (s.skipRawData(chunk) < 0) { break; }
                skip -= chunk;
            }
            if (s.device()->pos() <= loopStart) { break; }
            if (s.device()->pos() > extraEnd) { break; }
        }

        if (s.device()->pos() > extraEnd
                || s.status() != QDataStream::Ok) {
            // extraEnd may be past EOF, seek to a safe position instead
            s.resetStatus();
            s.device()->seek(qMin(extraEnd, s.device()->size()));
        } else {
            s.device()->seek(extraEnd);
        }
        mLayers.append(rec);
    }
    return true;
}

bool PsdFile::readChannelImageDataOffsets(QDataStream &s, qint64 layerInfoEnd,
                                           QString *error)
{
    Q_UNUSED(error)
    qint64 pos = s.device()->pos();
    for (auto &rec : mLayers) {
        for (auto &ch : rec.channels) {
            ch.dataOffset = pos;
            pos += ch.length;
        }
    }
    if (pos > layerInfoEnd) {
        // channel data overflows the declared section, clamp offsets
        for (auto &rec : mLayers) {
            for (auto &ch : rec.channels) {
                if (ch.dataOffset > layerInfoEnd) { ch.dataOffset = 0; ch.length = 0; }
            }
        }
    }
    return true;
}

QByteArray PsdFile::channelPlane(QIODevice &dev,
                                const ChannelInfo &info,
                                const int rowWidth,
                                const int rowHeight,
                                QString *error) const
{
    const int channelSize = mDepth / 8;
    const int rowBytes = rowWidth * channelSize;
    const int planeSize = rowBytes * rowHeight;
    if (rowWidth <= 0 || rowHeight <= 0) { return QByteArray(); }
    if (info.length <= 0 || info.dataOffset <= 0) {
        return QByteArray(planeSize, 0);
    }

    dev.seek(info.dataOffset);
    QDataStream s(&dev);
    s.setByteOrder(QDataStream::BigEndian);
    quint16 compression = 0;
    s >> compression;

    if (compression == 0) { // raw
        QByteArray raw = dev.read(planeSize);
        if (raw.size() < planeSize) { raw.resize(planeSize); }
        return raw;
    } else if (compression == 1) { // RLE
        // per-row compressed length table
        QVector<qint64> rowLengths(rowHeight);
        qint64 totalCompressed = 0;
        for (int y = 0; y < rowHeight; y++) {
            if (mIsPsb) { quint32 v = 0; s >> v; rowLengths[y] = qint64(v); }
            else { quint16 v = 0; s >> v; rowLengths[y] = qint64(v); }
            totalCompressed += rowLengths[y];
        }
        // one bulk read of all compressed rows, then decode each row
        // straight into its plane slot - no per-row temporaries
        const QByteArray packed = dev.read(qint64(qMin(totalCompressed,
                qint64(std::numeric_limits<int>::max()))));
        QByteArray plane(planeSize, Qt::Uninitialized);
        qint64 srcPos = 0;
        for (int y = 0; y < rowHeight; y++) {
            const qint64 avail = qMin(rowLengths[y],
                                      qint64(packed.size()) - srcPos);
            if (avail <= 0) { break; }
            uncompressRLETo(packed.constData() + srcPos, int(avail),
                            plane.data() + y * rowBytes, rowBytes);
            srcPos += rowLengths[y];
        }
        return plane;
    }

    if (error) {
        *error = QStringLiteral("Unsupported compression mode %1").arg(compression);
    }
    return QByteArray();
}

QByteArray PsdFile::assembleRGBA(const QMap<int, QByteArray> &planes,
                                 const int w, const int h) const
{
    const int channelSize = mDepth / 8;
    const int numPixels = w * h;
    if (numPixels <= 0 || channelSize <= 0) { return QByteArray(); }
    const int expectedPlaneBytes = numPixels * channelSize;
    QByteArray rgba(4 * numPixels, 255);
    const bool gray = (mMode == ColorMode::Grayscale);
    const bool cmyk = (mMode == ColorMode::CMYK);

    const auto p0It = planes.find(0);
    const auto p1It = planes.find(1);
    const auto p2It = planes.find(2);
    const auto p3It = planes.find(3);
    const auto paIt = planes.find(-1);

    if (gray) {
        if (p0It == planes.end() || p0It.value().size() < expectedPlaneBytes) {
            return QByteArray();
        }
    } else if (cmyk) {
        if (p0It == planes.end() || p0It.value().size() < expectedPlaneBytes ||
            p1It == planes.end() || p1It.value().size() < expectedPlaneBytes ||
            p2It == planes.end() || p2It.value().size() < expectedPlaneBytes ||
            p3It == planes.end() || p3It.value().size() < expectedPlaneBytes) {
            return QByteArray();
        }
    } else {
        if (p0It == planes.end() || p0It.value().size() < expectedPlaneBytes ||
            p1It == planes.end() || p1It.value().size() < expectedPlaneBytes ||
            p2It == planes.end() || p2It.value().size() < expectedPlaneBytes) {
            return QByteArray();
        }
    }

    const uchar *p0 = reinterpret_cast<const uchar*>(p0It.value().constData());
    const uchar *p1 = (!gray && p1It != planes.end()) ? reinterpret_cast<const uchar*>(p1It.value().constData()) : nullptr;
    const uchar *p2 = (!gray && p2It != planes.end()) ? reinterpret_cast<const uchar*>(p2It.value().constData()) : nullptr;
    const uchar *p3 = (cmyk && p3It != planes.end()) ? reinterpret_cast<const uchar*>(p3It.value().constData()) : nullptr;
    const bool hasAlpha = (paIt != planes.end() && paIt.value().size() >= expectedPlaneBytes);
    const uchar *pa = hasAlpha ? reinterpret_cast<const uchar*>(paIt.value().constData()) : nullptr;

    uchar *dst = reinterpret_cast<uchar*>(rgba.data());

    for (int i = 0; i < numPixels; i++) {
        const int offset = i * channelSize;
        if (gray) {
            const quint8 val = depthTo8Raw(p0 + offset, channelSize);
            dst[0] = val;
            dst[1] = val;
            dst[2] = val;
        } else if (cmyk) {
            // PSD stores CMYK channels inverted (255 = no ink);
            // flip to ink amounts, then rgb = 255 - min(255, ink + k)
            const int c = 255 - depthTo8Raw(p0 + offset, channelSize);
            const int m = 255 - depthTo8Raw(p1 + offset, channelSize);
            const int y = 255 - depthTo8Raw(p2 + offset, channelSize);
            const int k = 255 - depthTo8Raw(p3 + offset, channelSize);
            dst[0] = quint8(255 - qMin(255, c + k));
            dst[1] = quint8(255 - qMin(255, m + k));
            dst[2] = quint8(255 - qMin(255, y + k));
        } else {
            dst[0] = depthTo8Raw(p0 + offset, channelSize);
            dst[1] = depthTo8Raw(p1 + offset, channelSize);
            dst[2] = depthTo8Raw(p2 + offset, channelSize);
        }
        dst[3] = pa ? depthTo8Raw(pa + offset, channelSize) : 255;
        dst += 4;
    }
    return rgba;
}

QByteArray PsdFile::extractLayerRGBA(const LayerRecord &layer,
                                     QString *error) const
{
    const int w = layer.rect.width();
    const int h = layer.rect.height();
    if (w <= 0 || h <= 0) { return QByteArray(); }

    // const_cast: QBuffer wants a non-const QByteArray*, but opened
    // ReadOnly it never writes; constness keeps extraction thread-safe
    QBuffer buffer(const_cast<QByteArray*>(&mData));
    buffer.open(QIODevice::ReadOnly);

    QMap<int, QByteArray> planes;
    QByteArray maskPlane;
    for (const auto &info : layer.channels) {
        if (info.id == -2) {
            if (!layer.maskRect.isEmpty()) {
                maskPlane = channelPlane(buffer, info,
                                         layer.maskRect.width(),
                                         layer.maskRect.height(), error);
            }
        } else if (info.id >= -1 && info.id <= 3) {
            // id 3 is the K channel in CMYK files (extra alpha in RGB
            // files; assembleRGBA simply ignores it there)
            planes.insert(info.id,
                          channelPlane(buffer, info, w, h, error));
        }
    }

    QByteArray rgba = assembleRGBA(planes, w, h);
    if (!rgba.isEmpty() && !maskPlane.isEmpty()) {
        const int mw = layer.maskRect.width();
        const int mh = layer.maskRect.height();
        const int channelSize = mDepth / 8;
        const uchar *maskPtr = reinterpret_cast<const uchar*>(maskPlane.constData());
        const int maskPlaneSize = maskPlane.size();
        uchar *px = reinterpret_cast<uchar*>(rgba.data());
        for (int y = 0; y < h; y++) {
            const int my = y + layer.rect.top() - layer.maskRect.top();
            const bool yValid = (my >= 0 && my < mh);
            for (int x = 0; x < w; x++) {
                const int mx = x + layer.rect.left() - layer.maskRect.left();
                if (yValid && mx >= 0 && mx < mw) {
                    const int maskOffset = (my * mw + mx) * channelSize;
                    if (maskOffset + channelSize <= maskPlaneSize) {
                        const quint8 m = depthTo8Raw(maskPtr + maskOffset, channelSize);
                        px[3] = quint8(int(px[3]) * int(m) / 255);
                    }
                }
                px += 4;
            }
        }
    }
    return rgba;
}

QByteArray PsdFile::extractCompositeRGBA(QString *error) const
{
    if (mCompositeOffset <= 0 || mWidth <= 0 || mHeight <= 0) { return QByteArray(); }

    QBuffer buffer(const_cast<QByteArray*>(&mData));
    if (!buffer.open(QIODevice::ReadOnly)
            || !buffer.seek(mCompositeOffset)) {
        if (error) { *error = QStringLiteral("Cannot seek composite data"); }
        return QByteArray();
    }

    QDataStream s(&buffer);
    s.setByteOrder(QDataStream::BigEndian);
    quint16 compression = 0;
    s >> compression;

    const int channelSize = mDepth / 8;
    const int rowBytes = mWidth * channelSize;
    const int planeSize = rowBytes * mHeight;
    const int nChannels = qMin(mChannelCount, 4);

    // planes of straight channel data, top-to-bottom row order
    QMap<int, QByteArray> planes;

    if (compression == 0) { // raw, planar order, rows bottom to top
        for (int c = 0; c < nChannels; c++) {
            planes.insert(c, QByteArray(planeSize, 0));
        }
        // one bulk read per channel instead of per-row reads
        for (int c = 0; c < nChannels; c++) {
            const QByteArray chanData = buffer.read(planeSize);
            const int rows = qMin(mHeight, chanData.size() / rowBytes);
            char *dst = planes[c].data();
            const char *src = chanData.constData();
            for (int row = 0; row < rows; row++) {
                memcpy(dst + (mHeight - 1 - row) * rowBytes,
                       src + row * rowBytes, size_t(rowBytes));
            }
        }
    } else if (compression == 1) { // RLE, planar order, rows bottom to top
        // first the per-row length tables for ALL channels,
        // then the compressed rows channel by channel
        QVector<QVector<qint64>> tables(nChannels);
        QVector<qint64> totals(nChannels, 0);
        for (int c = 0; c < nChannels; c++) {
            tables[c].resize(mHeight);
            for (int y = 0; y < mHeight; y++) {
                if (mIsPsb) { quint32 v = 0; s >> v; tables[c][y] = qint64(v); }
                else { quint16 v = 0; s >> v; tables[c][y] = qint64(v); }
                totals[c] += tables[c][y];
            }
        }
        for (int c = 0; c < nChannels; c++) {
            planes.insert(c, QByteArray(planeSize, 0));
            // bulk read the whole channel, decode rows in place
            const QByteArray packed = buffer.read(qMin(totals[c],
                    qint64(std::numeric_limits<int>::max())));
            char *dst = planes[c].data();
            qint64 srcPos = 0;
            for (int row = 0; row < mHeight; row++) {
                const qint64 avail = qMin(tables[c][row],
                                          qint64(packed.size()) - srcPos);
                if (avail <= 0) { break; }
                uncompressRLETo(packed.constData() + srcPos, int(avail),
                                dst + (mHeight - 1 - row) * rowBytes,
                                rowBytes);
                srcPos += tables[c][row];
            }
        }
    } else {
        if (error) {
            *error = QStringLiteral("Unsupported composite compression %1").arg(compression);
        }
        return QByteArray();
    }

    return assembleRGBA(planes, mWidth, mHeight);
}

QString PsdFile::rawLayerHash(const LayerRecord &rec) const
{
    QCryptographicHash md5(QCryptographicHash::Md5);
    // geometry participates so pure moves/resizes register as changes
    const qint32 geo[4] = { rec.rect.x(), rec.rect.y(),
                            rec.rect.width(), rec.rect.height() };
    md5.addData(reinterpret_cast<const char*>(geo), sizeof(geo));
    for (const auto &ch : rec.channels) {
        if (ch.id < -2 || ch.id > 3) { continue; }
        md5.addData(reinterpret_cast<const char*>(&ch.id), sizeof(ch.id));
        if (ch.length <= 0 || ch.dataOffset <= 0
                || ch.dataOffset + ch.length > mData.size()) {
            continue;
        }
        md5.addData(mData.constData() + ch.dataOffset, int(ch.length));
    }
    return QString::fromLatin1(md5.result().toHex());
}

QString PsdFile::rawCompositeHash() const
{
    if (mCompositeOffset <= 0 || mCompositeOffset >= mData.size()) {
        return QString();
    }
    return QString::fromLatin1(QCryptographicHash::hash(
            mData.mid(int(mCompositeOffset)),
            QCryptographicHash::Md5).toHex());
}

} // namespace psd
