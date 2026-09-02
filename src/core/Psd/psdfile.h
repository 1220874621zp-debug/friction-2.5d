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

#ifndef PSDFILE_H
#define PSDFILE_H

#include <QString>
#include <QStringList>
#include <QRect>
#include <QVector>
#include <QByteArray>

#include "core_global.h"

class QIODevice;
class QDataStream;

namespace psd {
    enum class ColorMode {
        Grayscale = 1,
        RGB = 3,
        CMYK = 4,
        Unsupported = -1
    };

    // 'lsct' section divider types
    enum class Divider {
        None = 0,
        OpenFolder = 1,
        ClosedFolder = 2,
        BoundingDivider = 3
    };

    struct ChannelInfo {
        int id = 0;           // 0=R 1=G 2=B -1=alpha -2=user mask
        qint64 length = 0;    // compressed data length (incl. compression field)
        qint64 dataOffset = 0;// absolute file offset of channel data
    };

    // Photoshop layer styles ('lfxp') actually consumed by the
    // importer; anything missing keeps the Photoshop defaults
    struct LayerStyles {
        bool hasAny = false;

        bool shadowEnabled = false;
        double shadowAngle = 120.0;    // light angle, degrees CCW
        double shadowDistance = 5.0;   // px
        double shadowSpread = 0.0;     // %
        double shadowSize = 5.0;       // px (blur extent)
        double shadowOpacity = 75.0;   // %
        quint8 shadowR = 0, shadowG = 0, shadowB = 0;

        bool glowEnabled = false;
        double glowSpread = 0.0;       // %
        double glowSize = 10.0;        // px
        double glowOpacity = 75.0;     // %
        quint8 glowR = 255, glowG = 255, glowB = 190;

        bool strokeEnabled = false;
        int strokePos = 0;             // 0 outside, 1 center, 2 inside
        double strokeSize = 3.0;       // px
        double strokeOpacity = 100.0;  // %
        quint8 strokeR = 255, strokeG = 0, strokeB = 0;
    };

    struct LayerRecord {
        QRect rect;
        QVector<ChannelInfo> channels;
        QString blendKey = QStringLiteral("norm");
        int opacity = 255;
        bool visible = true;
        QString name;
        Divider divider = Divider::None;
        QRect maskRect;
        bool hasMask = false;
        int index = 0;
        // Photoshop native layer id ('lyid' additional layer info).
        // Stable across rename / reorder / regroup; 0 when absent.
        qint32 layerId = 0;
        // Photoshop layer styles ('lfxp'/'lfx2'), one entry per
        // effect instance: the first shadow/glow/stroke merge into a
        // single entry, every additional instance (PS 2015+ allows
        // multiple per type, stored in '*Multi' lists) becomes its
        // own entry rendered as a stacked effect
        QVector<LayerStyles> stylesList;
        // lmfx (multi-instance store) is authoritative: once seen,
        // the lfxp/lfx2 mirror of the same layer is skipped
        bool stylesFromLmfx = false;
    };

    // CORE_EXPORT for the effects unit-test exe (module-internal
    // users don't need it)
    class CORE_EXPORT PsdFile {
    public:
        // Load the layer tree (records + channel offsets).
        bool load(const QString &path, QString *error = nullptr);

        int width() const { return mWidth; }
        int height() const { return mHeight; }
        int depth() const { return mDepth; }
        ColorMode colorMode() const { return mMode; }
        bool hasLayers() const { return !mLayers.isEmpty(); }

        // Records in file order (bottom to top).
        const QVector<LayerRecord>& layers() const { return mLayers; }

        // Extract a layer as straight (unpremultiplied) RGBA8, mask
        // applied to alpha. Size is 4 * w * h bytes.
        // const + thread-safe: extraction runs against the immutable
        // mData buffer with a private QBuffer per call.
        QByteArray extractLayerRGBA(const LayerRecord &layer,
                                    QString *error = nullptr) const;

        // Extract the flattened composite image as straight RGBA8.
        QByteArray extractCompositeRGBA(QString *error = nullptr) const;

        // MD5 over the raw (still compressed) channel byte ranges +
        // geometry. Cheap change detector for sync: identical file
        // bytes decode to identical pixels, so a matching hash lets
        // the caller skip the full decode + PNG re-encode entirely.
        QString rawLayerHash(const LayerRecord &rec) const;
        QString rawCompositeHash() const;

    private:
        bool readLayerSection(QDataStream &s,
                              qint64 sectionEnd,
                              QString *error);
        bool readLayerRecords(QDataStream &s,
                              qint64 layerInfoEnd,
                              QString *error);
        bool readChannelImageDataOffsets(QDataStream &s,
                                          qint64 layerInfoEnd,
                                          QString *error);

        // Read one channel, decompress and return a full plane
        // (rowWidth * rowHeight * depth/8 bytes).
        QByteArray channelPlane(QIODevice &dev,
                                const ChannelInfo &info,
                                int rowWidth,
                                int rowHeight,
                                QString *error) const;


        // Assemble RGBA8 from decompressed channel planes.
        QByteArray assembleRGBA(const QMap<int, QByteArray> &planes,
                                int w, int h) const;

        QVector<LayerRecord> mLayers;
        QString mPath;

        // The whole file kept in memory: parsing and pixel extraction
        // run against this buffer instead of issuing thousands of
        // tiny file reads (some filter drivers stall such patterns).
        QByteArray mData;
        int mWidth = 0;
        int mHeight = 0;
        int mDepth = 8;
        int mChannelCount = 3;
        ColorMode mMode = ColorMode::RGB;
        bool mIsPsb = false;

        // composite image data
        qint64 mCompositeOffset = 0;
    };
}

#endif // PSDFILE_H
