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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#ifndef IMAGEBOX_H
#define IMAGEBOX_H
#include "Boxes/boundingbox.h"
#include "skia/skiaincludes.h"
#include "FileCacheHandlers/imagecachehandler.h"
#include "imagerenderdata.h"
#include "FileCacheHandlers/filehandlerobjref.h"
#include "Boxes/skinmesh.h"

class BoxTargetProperty;
class Bone;
class SkinPin;
class SkinPinsProperty;

struct CORE_EXPORT ImageBoxRenderData : public ImageContainerRenderData {
    ImageBoxRenderData(ImageFileHandler * const cacheHandler,
                       BoundingBox * const parentBox) :
        ImageContainerRenderData(parentBox),
        fSrcCacheHandler(cacheHandler) {}

    void loadImageFromHandler();

    const qptr<ImageFileHandler> fSrcCacheHandler;
};

class CORE_EXPORT ImageBox : public BoundingBox {
    Q_OBJECT
    e_OBJECT
protected:
    ImageBox();
    ImageBox(const QString &filePath);
    // for subclasses with their own box type (e.g. PsdImageBox):
    // the type tag is serialized by writeIdentifier() and must match
    // the subclass, otherwise load creates the wrong class and the
    // stream desyncs on the subclass' extra fields
    ImageBox(const QString &name, const eBoxType type);

    void setFilePathNoRename(const QString &path);

    void prp_readPropertyXEV_impl(const QDomElement& ele, const XevImporter& imp);
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;
public:
    void setupCanvasMenu(PropertyMenu * const menu);

    void setupRenderData(const qreal relFrame, const QTransform& parentM,
                         BoxRenderData * const data,
                         Canvas * const scene);
    stdsptr<BoxRenderData> createRenderData();

    void writeBoundingBox(eWriteStream& dst) const;
    void readBoundingBox(eReadStream& src);

    void saveSVG(SvgExporter& exp, DomEleTask* const eleTask) const;

    void changeSourceFile();
    void setFilePath(const QString &path);

    const QString& filePath() const { return mPath; }

    // drop all skinning (pins + mesh) and render as a plain image
    void skinUnbind();
    bool hasSkinBind() const;

    // ---- puppet pins (direct mesh deformation, NO bones needed) ----
    // a pin is a degenerate skin driver: place it on the artwork,
    // drag it (standard animator undo/auto-key), the mesh follows
    // with a radial falloff - PS/AE puppet-warp workflow.
    // A pin can also RIDE a bone (rigid passenger + manual offset on
    // top), which turns a whole skeleton into mesh drivers
    SkinPin* addSkinPin(const QPointF& relPos);
    void removeSkinPin(SkinPin* const pin);
    void clearSkinPins();
    int skinPinCount() const;
    // one-click full-skeleton bind: pins along every scene bone, each
    // bound to its own bone
    void skinPinsBindSkeleton();
    // refresh the render cache after an out-of-animator pin change
    // (bone bind / unbind)
    void skinChangedNotify();
    // bone candidates for pin binding: the bone LAYER wrapping this
    // image (its own rig), or every scene bone as a fallback
    QList<Bone*> skinCandidateBones();
    // pixel-in-RAM state for diagnostics (blank canvas investigation):
    // false = pixels evicted/not loaded yet; the next render schedules
    // an async reload
    bool hasLoadedImage() const;

    // PS-style auto-select: sample the source bitmap alpha at the
    // mapped pixel; positions on transparent pixels are not a hit
    bool absPointInsideVisiblePixels(const QPointF &absPos) override;

    void reload();

    // derived PSD box guards its pixel cache against disk cleanup
protected:
    FileHandlerObjRef<ImageFileHandler> mFileHandler;

private:
    void fileHandlerConnector(ConnContext& conn, ImageFileHandler* obj);
    void fileHandlerAfterAssigned(ImageFileHandler* obj);

    // skin internals
    bool skinGenerateMesh(SkinBindData& skin);

    QString mPath;
    qsptr<SkinPinsProperty> mSkinPins;
    SkinBindData mSkin;
};

#endif // IMAGEBOX_H
