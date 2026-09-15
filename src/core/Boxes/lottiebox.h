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

#ifndef LOTTIEBOX_H
#define LOTTIEBOX_H

#include "Boxes/boundingbox.h"
#include "Boxes/imagerenderdata.h"
#include "Timeline/fixedlenanimationrect.h"

#include <QScopedPointer>

class QTemporaryDir;

// Renders the requested Lottie frame on the CPU render thread: the
// skottie animation is seeked and drawn into a raster surface, then
// composited like any other image layer (transparent background, so
// the result overlays correctly).
struct LottieBoxRenderData : public ImageRenderData {
    LottieBoxRenderData(BoundingBox* const parentBox);
    ~LottieBoxRenderData();

    void loadImageFromHandler();

    // provider-owned animation reference, released in the destructor
    void* fHandle = nullptr;
    double fTime = 0; // animation time in seconds
};

class CORE_EXPORT LottieBox : public BoundingBox {
    Q_OBJECT
    e_OBJECT
protected:
    LottieBox();
public:
    // content sniff for .json files: true when they look like a
    // bodymovin/Lottie document (used by the import dispatcher)
    static bool looksLikeLottie(const QString& path);

    void setupCanvasMenu(PropertyMenu * const menu);

    void setupRenderData(const qreal relFrame, const QTransform& parentM,
                         BoxRenderData * const data,
                         Canvas * const scene);
    stdsptr<BoxRenderData> createRenderData();

    void writeBoundingBox(eWriteStream& dst) const;
    void readBoundingBox(eReadStream& src);

    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp);
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;

    // the animation state lives inside the lottie file, not in
    // animated properties: frame changes must schedule re-renders,
    // and every in-range frame is its own cache identity
    void anim_setAbsFrame(const int frame);
    FrameRange prp_getIdenticalRelRange(const int relFrame) const;
    bool shouldScheduleUpdate();

    void setFilePath(const QString& path);
    void setFilePathNoRename(const QString& path);
    const QString& filePath() const { return mPath; }

    void reload();
    void changeSourceFile();

    // maps a parent-relative frame to the animation frame range
    // [0, frameCount)
    int getAnimationFrameForRelFrame(const qreal relFrame);

    bool hasAnimation() const { return mHandle != nullptr; }
    qreal getLottieFps() const { return mFps; }
protected:
    FixedLenAnimationRect* getAnimationDurationRect() const;
private:
    void loadAnimation();
    void updateDurationRange();
    bool extractLottieZip(const QString& zipPath, QString& jsonPathOut);

    // original file path (.json or .lottie); what gets serialized
    QString mPath;
    // effective animation json (extracted copy for .lottie zips)
    QString mJsonPath;
    // directory for relative resources (extracted dir / json dir)
    QString mBaseDir;
    // extraction target for .lottie zip containers
    QScopedPointer<QTemporaryDir> mExtractDir;

    void* mHandle = nullptr;
    // fps used for frame <-> time mapping (from the file, scene fps
    // is not authoritative for lottie timing)
    qreal mFps = 30;
    int mFrameCount = 0;
};

#endif // LOTTIEBOX_H
