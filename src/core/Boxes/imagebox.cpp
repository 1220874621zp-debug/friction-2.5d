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

#include "Boxes/imagebox.h"

#include <QMenu>

#include "FileCacheHandlers/imagecachehandler.h"
#include "fileshandler.h"
#include "filesourcescache.h"
#include "typemenu.h"
//#include "paintbox.h"
#include "svgexporter.h"
#include "svgexporthelpers.h"
#include "appsupport.h"
#include "Properties/boxtargetproperty.h"
#include "canvas.h"
#include "Boxes/bone.h"
#include "Private/document.h"
#include "simpletask.h"
#include "ReadWrite/evformat.h"
#include "Animators/animator.h"
#include "Animators/complexanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/transformanimator.h"
#include "MovablePoints/movablepoint.h"
#include "MovablePoints/pointshandler.h"

#include <QtMath>
#include <QDebug>

// ---------------------------------------------------------------------------
// puppet pins: direct mesh deformation, no bones required. A pin is a
// degenerate skin driver (a point with an influence radius) whose x/y
// are standard keyable animators, so dragging rides the regular undo /
// auto-key / interpolation pipeline

class SkinPinPoint : public MovablePoint {
    e_OBJECT
public:
    SkinPinPoint(BasicTransformAnimator * const trans,
                 SkinPin * const pin)
        : MovablePoint(trans, TYPE_PIVOT_POINT), mPin(pin) {
        setRadius(6);
    }

    QPointF getRelativePos() const override;
    void setRelativePos(const QPointF &relPos) override;

    void startTransform() override;
    void finishTransform() override;

    void canvasContextMenu(PointTypeMenu * const menu) override;
private:
    SkinPin * const mPin;
};

class SkinPin : public ComplexAnimator {
public:
    SkinPin(const QString& name, ImageBox * const box)
        : ComplexAnimator(name), mBox(box) {
        mX = enve::make_shared<QrealAnimator>(
                    0, -100000, 100000, 1, QStringLiteral("x"));
        mY = enve::make_shared<QrealAnimator>(
                    0, -100000, 100000, 1, QStringLiteral("y"));
        mRadius = enve::make_shared<QrealAnimator>(
                    150, 1, 100000, 1,
                    QStringLiteral("\u5F71\u54CD\u534A\u5F84"));
        ca_addChild(mX);
        ca_addChild(mY);
        ca_addChild(mRadius);
        setPointsHandler(enve::make_shared<PointsHandler>());
        getPointsHandler()->appendPt(enve::make_shared<SkinPinPoint>(
                    mBox ? mBox->getBoxTransformAnimator() : nullptr,
                    this));
        prp_enabledDrawingOnCanvas();
    }

    QPointF getRelPos() const {
        return QPointF(mX->getEffectiveValue(),
                       mY->getEffectiveValue());
    }

    void setRelPos(const QPointF& p) {
        mX->setCurrentBaseValue(p.x());
        mY->setCurrentBaseValue(p.y());
    }

    qreal getRadius() const { return mRadius->getEffectiveValue(); }
    void setRadiusValue(const qreal r) {
        mRadius->setCurrentBaseValue(r);
    }

    QPointF bindRel() const { return mBindRel; }
    void setBindRel(const QPointF& p) { mBindRel = p; }

    void startTransform() {
        mPosAtStart = getRelPos();
        mX->prp_startTransform();
        mY->prp_startTransform();
    }

    void finishTransform() {
        mX->prp_finishTransform();
        mY->prp_finishTransform();
        // Moho-style auto-keyframing (BoneTailPoint pattern): a real
        // drag records a key at the current frame
        if (QLineF(mPosAtStart, getRelPos()).length() > 0.01) {
            mX->anim_saveCurrentValueAsKey();
            mY->anim_saveCurrentValueAsKey();
        }
    }

    void removeFromPins() {
        if (mBox) mBox->removeSkinPin(this);
    }

    // serialization: the three child animators positionally (created
    // by the ctor, StaticComplexAnimator pattern), then the bind pos
    void prp_writeProperty_impl(eWriteStream& dst) const {
        for (const auto& prop : ca_getChildren()) {
            prop->prp_writeProperty(dst);
        }
        dst << mBindRel;
    }

    void prp_readProperty_impl(eReadStream& src) {
        for (const auto& prop : ca_getChildren()) {
            prop->prp_readProperty(src);
        }
        src >> mBindRel;
    }

    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const {
        auto ele = exp.createElement(QStringLiteral("SkinPin"));
        ele.setAttribute(QStringLiteral("x"), mX->getEffectiveValue());
        ele.setAttribute(QStringLiteral("y"), mY->getEffectiveValue());
        ele.setAttribute(QStringLiteral("radius"),
                         mRadius->getEffectiveValue());
        ele.setAttribute(QStringLiteral("bindX"), mBindRel.x());
        ele.setAttribute(QStringLiteral("bindY"), mBindRel.y());
        return ele;
    }

    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp) {
        Q_UNUSED(imp)
        setRelPos(QPointF(ele.attribute(QStringLiteral("x")).toDouble(),
                          ele.attribute(QStringLiteral("y")).toDouble()));
        mBindRel = QPointF(ele.attribute(QStringLiteral("bindX")).toDouble(),
                           ele.attribute(QStringLiteral("bindY")).toDouble());
        setRadiusValue(ele.attribute(QStringLiteral("radius")).toDouble());
    }

private:
    qptr<ImageBox> mBox;
    qsptr<QrealAnimator> mX;
    qsptr<QrealAnimator> mY;
    qsptr<QrealAnimator> mRadius;
    QPointF mBindRel;
    QPointF mPosAtStart;
};

QPointF SkinPinPoint::getRelativePos() const { return mPin->getRelPos(); }

void SkinPinPoint::setRelativePos(const QPointF &relPos) {
    mPin->setRelPos(relPos);
}

void SkinPinPoint::startTransform() {
    // the BASE startTransform records mSavedRelPos - the anchor the
    // generic drag pipeline (moveByAbs) positions the point from;
    // skipping it teleports the pin to the image origin + drag delta
    // on the first mouse move
    MovablePoint::startTransform();
    mPin->startTransform();
}

void SkinPinPoint::finishTransform() {
    mPin->finishTransform();
    MovablePoint::finishTransform();
}

void SkinPinPoint::canvasContextMenu(PointTypeMenu * const menu) {
    if (menu->hasActionsForType<SkinPinPoint>()) return;
    menu->addedActionsForType<SkinPinPoint>();
    const PointTypeMenu::PlainSelectedOp<SkinPinPoint> delOp =
            [pin = mPin](SkinPinPoint *) { pin->removeFromPins(); };
    menu->addPlainAction(QIcon::fromTheme("trash"),
                         QStringLiteral("\u5220\u9664\u6B64\u9489"), delOp);
}

class SkinPinsProperty : public ComplexAnimator {
public:
    SkinPinsProperty(ImageBox * const box)
        : ComplexAnimator(QStringLiteral("\u8499\u76AE\u9489")), mBox(box) {}

    int pinCount() const { return ca_getChildren().count(); }

    SkinPin* pinAt(const int i) {
        return static_cast<SkinPin*>(ca_getChildren().at(i).data());
    }

    SkinPin* addPin(const QPointF& relPos, const qreal radius) {
        const auto pin = enve::make_shared<SkinPin>(
                    QStringLiteral("\u9489 %1").arg(pinCount() + 1),
                    mBox.data());
        pin->setRelPos(relPos);
        pin->setBindRel(relPos);
        pin->setRadiusValue(radius);
        ca_addChild(pin);
        return pin.get();
    }

    void removePin(SkinPin * const pin) {
        for (const auto& c : ca_getChildren()) {
            if (c.data() == pin) { ca_removeChild(c); return; }
        }
    }

    void prp_writeProperty_impl(eWriteStream& dst) const {
        const auto& children = ca_getChildren();
        dst << int(children.count());
        for (const auto& prop : children) {
            prop->prp_writeProperty(dst);
        }
    }

    void prp_readProperty_impl(eReadStream& src) {
        int count; src >> count;
        for (int i = 0; i < count && i < 512; ++i) {
            const auto pin = enve::make_shared<SkinPin>(
                        QStringLiteral("\u9489 %1").arg(i + 1),
                        mBox.data());
            ca_addChild(pin);
            pin->prp_readProperty(src);
        }
    }

    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const {
        auto ele = exp.createElement(QStringLiteral("SkinPins"));
        for (const auto& prop : ca_getChildren()) {
            if (const auto pin = static_cast<SkinPin*>(prop.data())) {
                ele.appendChild(pin->prp_writePropertyXEV_impl(exp));
            }
        }
        return ele;
    }

    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp) {
        Q_UNUSED(imp)
        const auto pins = ele.elementsByTagName(
                    QStringLiteral("SkinPin"));
        for (int i = 0; i < pins.count(); ++i) {
            const auto pin = addPin(QPointF(), 150.);
            pin->prp_readPropertyXEV_impl(pins.at(i).toElement(), imp);
        }
    }

private:
    qptr<ImageBox> mBox;
};

ImageFileHandler* imageFileHandlerGetter(const QString& path) {
    return FilesHandler::sInstance->getFileHandler<ImageFileHandler>(path);
}

ImageBox::ImageBox(const QString &name, const eBoxType type) :
    BoundingBox(name, type),
    mFileHandler(this,
                 [](const QString& path) {
                     return imageFileHandlerGetter(path);
                 },
                 [this](ImageFileHandler* obj) {
                     return fileHandlerAfterAssigned(obj);
                 },
                 [this](ConnContext& conn, ImageFileHandler* obj) {
                     fileHandlerConnector(conn, obj);
                 }) {
    // skin bones chain root (mesh deformation driver); always present
    // so the serialized children block stays positional
    mSkinRoot = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("\u8499\u76AE\u9AA8\u9ABC\u94FE"));
    mSkinRoot->setValidator<Bone>();
    ca_addChild(mSkinRoot);
    // manual retarget from the property panel: only rewire the live
    // follow connections; use the layer menu to re-capture the pose
    connect(mSkinRoot.data(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox*) {
        if(mSkinInternalSet) return;
        skinSetupFollowConns();
        if(mSkin.hasBind()) prp_afterWholeInfluenceRangeChanged();
    });
    // puppet pins container (direct deformation, no bones); always a
    // child so the serialized property tree stays positional
    mSkinPins = enve::make_shared<SkinPinsProperty>(this);
    ca_addChild(mSkinPins);
}

ImageBox::ImageBox() : ImageBox(QStringLiteral("Image"), eBoxType::image) {
}

ImageBox::ImageBox(const QString &filePath) : ImageBox() {
    setFilePath(filePath);
}

void ImageBox::fileHandlerConnector(ConnContext &conn, ImageFileHandler *obj) {
    conn << connect(obj, &ImageFileHandler::pathChanged,
                    this, &ImageBox::prp_afterWholeInfluenceRangeChanged);
    conn << connect(obj, &ImageFileHandler::reloaded,
                    this, &ImageBox::prp_afterWholeInfluenceRangeChanged);
}

void ImageBox::fileHandlerAfterAssigned(ImageFileHandler *obj) {
    Q_UNUSED(obj);
}

void ImageBox::writeBoundingBox(eWriteStream& dst) const {
    BoundingBox::writeBoundingBox(dst);
    dst.writeFilePath(mFileHandler->path());
    // skin bind block (positional, version-gated on read); the gate
    // covers both drivers - puppet-pin-only layers carry the mesh here
    // while the pins themselves serialize as animator children
    dst << (hasSkinBind() ? int(1) : int(0));
    if(!hasSkinBind()) return;
    dst << int(mSkin.fDefs.count());
    for(const auto& def : mSkin.fDefs) {
        dst << def.fName;
        dst << def.fBindTotal;
        dst << def.fBindHead;
        dst << def.fBindTail;
        dst << def.fBindAngle;
        dst << def.fRadius;
    }
    dst << mSkin.fBindBoxTotal;
    dst << int(mSkin.fMesh.fCellPx)
        << int(mSkin.fMesh.fImgW) << int(mSkin.fMesh.fImgH);
    const int posCount = mSkin.fMesh.fPos.count();
    dst << posCount;
    dst.write(mSkin.fMesh.fPos.constData(),
              qint64(posCount) * qint64(sizeof(SkPoint)));
    dst << int(mSkin.fMesh.fIndices.count());
    dst.write(mSkin.fMesh.fIndices.constData(),
              qint64(mSkin.fMesh.fIndices.count()) * qint64(sizeof(uint16_t)));
    for(const auto& vw : mSkin.fMesh.fW) {
        dst << int(vw.fCount);
        for(int k = 0; k < vw.fCount; ++k) {
            dst << int(vw.fIdx[k]);
            dst << qreal(vw.fW[k]);
        }
    }
}

void ImageBox::readBoundingBox(eReadStream& src) {
    BoundingBox::readBoundingBox(src);
    const QString path = src.readFilePath();
    setFilePathNoRename(path);
    if(src.evFileVersion() < EvFormat::imageBoxSkinBind) return;
    int hasSkin; src >> hasSkin;
    if(!hasSkin) return;
    int defCount; src >> defCount;
    if(defCount < 0 || defCount > 512) return;
    mSkin.fDefs.clear();
    for(int i = 0; i < defCount; ++i) {
        SkinBoneDef def;
        src >> def.fName;
        src >> def.fBindTotal;
        src >> def.fBindHead;
        src >> def.fBindTail;
        src >> def.fBindAngle;
        src >> def.fRadius;
        mSkin.fDefs.append(def);
    }
    src >> mSkin.fBindBoxTotal;
    int cellPx, imgW, imgH;
    src >> cellPx >> imgW >> imgH;
    int posCount; src >> posCount;
    if(posCount < 3 || posCount >= 65536) {
        mSkin.fDefs.clear();
        return;
    }
    mSkin.fMesh.fPos.resize(posCount);
    src.read(mSkin.fMesh.fPos.data(),
             qint64(posCount) * qint64(sizeof(SkPoint)));
    int idxCount; src >> idxCount;
    if(idxCount < 3 || idxCount > posCount * 8) {
        mSkin.fDefs.clear();
        return;
    }
    mSkin.fMesh.fIndices.resize(idxCount);
    src.read(mSkin.fMesh.fIndices.data(),
             qint64(idxCount) * qint64(sizeof(uint16_t)));
    mSkin.fMesh.fW.resize(posCount);
    for(int i = 0; i < posCount; ++i) {
        auto& vw = mSkin.fMesh.fW[i];
        int cnt; src >> cnt;
        cnt = qBound(0, cnt, 4);
        vw.fCount = cnt;
        for(int k = 0; k < cnt; ++k) {
            int idx; src >> idx;
            qreal w; src >> w;
            if(idx < 0 || idx >= defCount) continue;
            vw.fIdx[k] = idx;
            vw.fW[k] = float(w);
        }
    }
    mSkin.fMesh.fCellPx = cellPx;
    mSkin.fMesh.fImgW = imgW;
    mSkin.fMesh.fImgH = imgH;
    // wire the live-follow connections once the event loop settles
    // (the chain root may still be resolving from its write id)
    SimpleTask::sScheduleContexted(this, [this]() {
        skinSetupFollowConns();
    });
}

QDomElement ImageBox::prp_writePropertyXEV_impl(const XevExporter& exp) const {
    auto result = BoundingBox::prp_writePropertyXEV_impl(exp);
    const QString& absSrc = mFileHandler.path();
    XevExportHelpers::setAbsAndRelFileSrc(absSrc, result, exp);
    return result;
}

void ImageBox::prp_readPropertyXEV_impl(const QDomElement& ele, const XevImporter& imp) {
    BoundingBox::prp_readPropertyXEV_impl(ele, imp);
    const QString absSrc = XevExportHelpers::getAbsAndRelFileSrc(ele, imp);
    setFilePathNoRename(absSrc);
}

void ImageBox::setFilePathNoRename(const QString &path) {
    mPath = path;
    mFileHandler.assign(path);
    prp_afterWholeInfluenceRangeChanged();
}

void ImageBox::setFilePath(const QString &path) {
    setFilePathNoRename(path);
    rename(QFileInfo(path).completeBaseName());
}

void ImageBox::reload() {
    if(mFileHandler) mFileHandler->reloadAction();
}

bool ImageBox::hasLoadedImage() const {
    return mFileHandler && mFileHandler->hasImage();
}

bool ImageBox::absPointInsideVisiblePixels(const QPointF &absPos) {
    // map the scene position into the source bitmap (image rel space
    // is pixel space, origin top-left, size = image dimensions) and
    // require a non-transparent pixel; fall back to the bounding
    // rectangle when the pixels are not in RAM (evicted)
    if(!mFileHandler || !mFileHandler->hasImage()) {
        return absPointInsidePath(absPos);
    }
    const sk_sp<SkImage> img = mFileHandler->getImage();
    if(!img) { return absPointInsidePath(absPos); }
    const QPointF rel = mapAbsPosToRel(absPos);
    const int px = qRound(rel.x());
    const int py = qRound(rel.y());
    if(px < 0 || py < 0 || px >= img->width() || py >= img->height()) {
        return false;
    }
    SkPixmap pixmap;
    if(!img->peekPixels(&pixmap)) {
        return absPointInsidePath(absPos);
    }
    return SkColorGetA(pixmap.getColor(px, py)) > 0;
}

// ---------------------------------------------------------------------------
// bone skin bind (AnimeEffects-style mesh deformation)

bool ImageBox::hasSkinBind() const {
    return mSkin.hasBind() || (mSkinPins && mSkinPins->pinCount() > 0);
}

int ImageBox::skinPinCount() const {
    return mSkinPins ? mSkinPins->pinCount() : 0;
}

SkinPin* ImageBox::addSkinPin(const QPointF& relPos) {
    // first driver on this layer: generate the mesh now
    if (!mSkin.fMesh.isValid()) {
        mSkin.fBindBoxTotal = getTotalTransform();
        skinGenerateMesh(mSkin);
    }
    const sk_sp<SkImage> img = mFileHandler ?
                mFileHandler->getImage() : nullptr;
    const qreal diag = img ? QLineF(QPointF(), QPointF(img->width(),
                                                       img->height())).length()
                           : 500.;
    const qreal radius = qBound(40., 0.3 * diag, 800.);
    const auto pin = mSkinPins->addPin(relPos, radius);
    qDebug() << "[SKIN]" << prp_getName() << "pin added at"
             << relPos << "radius" << radius
             << "mesh verts=" << mSkin.fMesh.fPos.count();
    prp_updateCanvasProps();
    prp_afterWholeInfluenceRangeChanged();
    if (Document::sInstance) Document::sInstance->actionFinished();
    return pin;
}

void ImageBox::removeSkinPin(SkinPin * const pin) {
    if (!mSkinPins || !pin) return;
    mSkinPins->removePin(pin);
    prp_updateCanvasProps();
    prp_afterWholeInfluenceRangeChanged();
    if (Document::sInstance) Document::sInstance->actionFinished();
}

void ImageBox::clearSkinPins() {
    if (!mSkinPins || mSkinPins->pinCount() == 0) return;
    while (mSkinPins->pinCount() > 0) {
        mSkinPins->removePin(mSkinPins->pinAt(0));
    }
    prp_updateCanvasProps();
    prp_afterWholeInfluenceRangeChanged();
    if (Document::sInstance) Document::sInstance->actionFinished();
}

bool ImageBox::skinGenerateMesh(SkinBindData& skin) {
    sk_sp<SkImage> img = mFileHandler ?
                mFileHandler->getImage() : nullptr;
    if (img) {
        // lazy/codec-backed images do not expose pixels: peekPixels
        // fails on them and every real-world image silently degraded
        // to the coarse uniform fallback grid (625 verts = 24x24) -
        // materialize a raster copy first
        img = img->makeRasterImage();
    }
    SkPixmap pm;
    if (img && img->peekPixels(&pm)) {
        // coarsen until the vertex count fits SkVertices' uint16
        // indices (a fully covered 8k image at 20px needs two steps)
        for (int cellPx = 20; cellPx < 400; cellPx *= 2) {
            if (SkinMeshGen::generate(pm, cellPx, skin.fMesh)) {
                return true;
            }
        }
        qWarning() << "[SKIN]" << prp_getName()
                   << "alpha lattice failed at every cell size -"
                      " falling back to the uniform grid";
    } else if (img) {
        qWarning() << "[SKIN]" << prp_getName()
                   << "image pixels unavailable (peek failed) -"
                      " falling back to the uniform grid";
    }
    const int w = img ? img->width() : 640;
    const int h = img ? img->height() : 480;
    SkinMeshGen::generateUniform(w, h, skin.fMesh);
    return true;
}

void ImageBox::skinCaptureDefs(const QList<Bone*>& chain) {
    mSkin.fBindBoxTotal = getTotalTransform();
    mSkin.fDefs.clear();
    for(const auto bone : chain) {
        if(!bone) continue;
        SkinBoneDef def;
        def.fName = bone->prp_getName();
        def.fBindTotal = bone->getTotalTransform();
        def.fBindHead = def.fBindTotal.map(QPointF(0., 0.));
        def.fBindTail = def.fBindTotal.map(
                    QPointF(bone->getLength(), 0.));
        def.fBindAngle = std::atan2(def.fBindTail.y() - def.fBindHead.y(),
                                    def.fBindTail.x() - def.fBindHead.x());
        def.fRadius = bone->skinInfluenceRadius();
        mSkin.fDefs.append(def);
    }
}

void ImageBox::skinFinishBind() {
    SkinMeshGen::computeWeights(mSkin);
    mSkinWarnedNoBones = false;
    skinSetupFollowConns();
    prp_afterWholeInfluenceRangeChanged();
}

bool ImageBox::skinBindChain(Bone* const chainRoot) {
    if(!chainRoot) return false;
    const auto chain = Bone::chain(chainRoot);
    if(chain.isEmpty()) return false;
    // A layer that still lives INSIDE a bone chain (from an earlier
    // rigid "Bind Selected Layers to This Bone") makes the skin
    // matrices collapse to identity: the parenting already delivers
    // the bone motion, and R = L_cur^-1 * M_b * L_bind cancels it
    // exactly. Skin bind REPLACES rigid bind - move the layer out to
    // the nearest non-bone ancestor first, keeping its world pose.
    bool movedOutOfBone = false;
    if(const auto parentBone = enve_cast<Bone*>(getParentGroup())) {
        // bone-side unbind: moves the layer out of the chain to the
        // nearest non-bone ancestor, keeping its world appearance
        parentBone->unbindLayer(this);
        movedOutOfBone = true;
    }
    skinCaptureDefs(chain);
    if(!skinGenerateMesh(mSkin)) {
        mSkin.fDefs.clear();
        qWarning() << "[SKIN]" << prp_getName()
                   << "mesh generation failed - bind aborted";
        return false;
    }
    mSkinInternalSet = true;
    mSkinRoot->setTargetAction(chainRoot);
    mSkinInternalSet = false;
    skinFinishBind();
    qDebug() << "[SKIN]" << prp_getName() << "bound:"
             << "bones=" << mSkin.fDefs.count()
             << "verts=" << mSkin.fMesh.fPos.count()
             << "tris=" << mSkin.fMesh.fIndices.count() / 3
             << "cellPx=" << mSkin.fMesh.fCellPx
             << "uniformFallback=" << (mSkin.fMesh.fCellPx < 0)
             << "movedOutOfBone=" << movedOutOfBone;
    return true;
}

void ImageBox::skinUnbind() {
    if(!hasSkinBind()) return;
    mSkin.fDefs.clear();
    mSkin.fMesh = SkinMesh();
    skinClearFollowConns();
    clearSkinPins();
    mSkinInternalSet = true;
    mSkinRoot->setTargetAction(nullptr);
    mSkinInternalSet = false;
    prp_afterWholeInfluenceRangeChanged();
    if(Document::sInstance) Document::sInstance->actionFinished();
}

void ImageBox::skinRebindPose() {
    if(!mSkin.hasBind()) return;
    const auto root = enve_cast<Bone*>(mSkinRoot->getTarget());
    if(!root) return;
    const auto chain = Bone::chain(root);
    if(chain.isEmpty()) return;
    // regenerate the mesh when the image changed since the bind
    if(mSkin.fMesh.fImgW > 0 && mFileHandler && mFileHandler->hasImage()) {
        const auto img = mFileHandler->getImage();
        if(img && (img->width() != mSkin.fMesh.fImgW ||
                   img->height() != mSkin.fMesh.fImgH)) {
            skinGenerateMesh(mSkin);
        }
    }
    skinCaptureDefs(chain);
    skinFinishBind();
    if(Document::sInstance) Document::sInstance->actionFinished();
}

void ImageBox::skinSetupFollowConns() {
    // live follow: any bound bone change re-renders this layer
    // (coalesced through SimpleTask - the proven BoneWarp pattern)
    skinClearFollowConns();
    const auto root = enve_cast<Bone*>(mSkinRoot->getTarget());
    if(!root) return;
    for(const auto bone : Bone::chain(root)) {
        if(!bone) continue;
        mSkinFollowConns << connect(
                    bone, &BoundingBox::prp_absFrameRangeChanged,
                    this, [this](const FrameRange& abs) {
            SimpleTask::sScheduleContexted(this, [this, abs]() {
                prp_afterChangedAbsRange(abs);
            });
        });
        // live influence radius: the slider captures into the bind
        // defs, so a later edit re-reads the bone's radius, rebuilds
        // the weights and re-renders (no manual re-bind needed)
        const auto radiusAnim = bone->skinRadiusAnimator();
        if(radiusAnim) {
            const qptr<Bone> bonePtr = bone;
            mSkinFollowConns << connect(
                        radiusAnim, &Animator::prp_absFrameRangeChanged,
                        this, [this, bonePtr](const FrameRange&) {
                SimpleTask::sScheduleContexted(this, [this, bonePtr]() {
                    if(!bonePtr || !mSkin.hasBind()) return;
                    bool changed = false;
                    const QString name = bonePtr->prp_getName();
                    const qreal radius = bonePtr->skinInfluenceRadius();
                    for(auto& def : mSkin.fDefs) {
                        if(def.fName != name) continue;
                        if(qAbs(def.fRadius - radius) > 0.01) {
                            def.fRadius = radius;
                            changed = true;
                        }
                    }
                    if(changed) {
                        SkinMeshGen::computeWeights(mSkin);
                        prp_afterWholeInfluenceRangeChanged();
                    }
                });
            });
        }
    }
}

void ImageBox::skinClearFollowConns() {
    for(const auto& c : mSkinFollowConns) QObject::disconnect(c);
    mSkinFollowConns.clear();
}

void ImageBox::setupCanvasMenu(PropertyMenu * const menu)
{
    if (menu->hasActionsForType<ImageBox>()) { return; }
    menu->addedActionsForType<ImageBox>();

    const PropertyMenu::PlainSelectedOp<ImageBox> addPinOp =
    [](ImageBox * box) {
        // place the pin where the context menu was opened (scene ->
        // image rel space); no bones involved anywhere
        const auto scene = box->getParentScene();
        if (!scene) return;
        const QPointF rel = box->mapAbsPosToRel(
                    scene->getLastContextMenuAbsPos());
        box->addSkinPin(rel);
    };
    menu->addPlainAction(QIcon::fromTheme("newVectorLayer"),
                         QStringLiteral("\u5728\u6B64\u5904\u6DFB\u52A0\u8499\u76AE\u9489\uFF08\u65E0\u9700\u9AA8\u9ABC\uFF0C\u62D6\u9489\u53D8\u5F62\uFF09"),
                         addPinOp);

    const PropertyMenu::PlainSelectedOp<ImageBox> clearPinsOp =
    [](ImageBox * box) { box->clearSkinPins(); };
    menu->addPlainAction(QIcon::fromTheme("trash"),
                         QStringLiteral("\u6E05\u9664\u5168\u90E8\u8499\u76AE\u9489"),
                         clearPinsOp);

    const PropertyMenu::PlainSelectedOp<ImageBox> skinRebindOp =
    [](ImageBox * box) { box->skinRebindPose(); };
    menu->addPlainAction(QIcon::fromTheme("loop"),
                         QStringLiteral("\u8499\u76AE\u91CD\u7ED1\u59FF\u6001\uFF08\u4EE5\u5F53\u524D\u9AA8\u9ABC\u59FF\u6001\u4E3A\u57FA\u51C6\uFF09"),
                         skinRebindOp);

    const PropertyMenu::PlainSelectedOp<ImageBox> skinUnbindOp =
    [](ImageBox * box) { box->skinUnbind(); };
    menu->addPlainAction(QIcon::fromTheme("edit-clear"),
                         QStringLiteral("\u89E3\u9664\u8499\u76AE\u7ED1\u5B9A"),
                         skinUnbindOp);

    const PropertyMenu::PlainSelectedOp<ImageBox> reloadOp =
    [](ImageBox * box) { box->reload(); };
    menu->addPlainAction(QIcon::fromTheme("loop"), tr("Reload"), reloadOp);

    const PropertyMenu::PlainSelectedOp<ImageBox> setSrcOp =
    [](ImageBox * box) { box->changeSourceFile(); };
    menu->addPlainAction(QIcon::fromTheme("document-new"), tr("Set Source File"), setSrcOp);

    menu->addSeparator();

    BoundingBox::setupCanvasMenu(menu);
}

void ImageBox::changeSourceFile()
{
    const QString filters = FileExtensions::imageFilters();
    QString importPath = AppSupport::getOpenFile(nullptr,
                                                 tr("Change Source"),
                                                 mFileHandler.path(),
                                                 tr("Image Files (%1)").arg(filters));
    if (!importPath.isEmpty()) { setFilePath(importPath); }
}

void ImageBox::setupRenderData(const qreal relFrame, const QTransform& parentM,
                               BoxRenderData * const data,
                               Canvas* const scene)
{
    if (!mFileHandler) { mFileHandler.assign(mPath); }
    BoundingBox::setupRenderData(relFrame, parentM, data, scene);
    const auto imgData = static_cast<ImageBoxRenderData*>(data);
    if (mFileHandler->hasImage()) {
        imgData->setContainer(mFileHandler->getImageContainer());
        if (!imgData->hasLoadedImage()) {
            // the container was evicted to tmp between renders: wait
            // for its reload (same dependency pattern as the no-image
            // branch) - compositing anyway baked imageless frames
            // into the cache that showed up as layer color shifts
            // and flicker until a restart
            const auto cont = mFileHandler->getImageContainer();
            const auto tmpLoader = cont ?
                        cont->scheduleLoadFromTmpFile() : nullptr;
            if (tmpLoader) {
                tmpLoader->addDependent(imgData);
                // a finished/canceled waiter does NOT hold the render
                // task (addDependent no-ops on finished, cancels on
                // canceled) - log it, the frame would render imageless
                const auto st = tmpLoader->getState();
                if(st == eTaskState::finished || st == eTaskState::canceled) {
                    qWarning() << "IMGWAIT-DEAD:" << prp_getName()
                               << "tmpLoader state=" << int(st)
                               << "contInMem=" << cont->storesDataInMemory();
                }
            } else {
                const auto loader = mFileHandler->scheduleLoad();
                if (loader) {
                    loader->addDependent(imgData);
                    const auto st = loader->getState();
                    if(st == eTaskState::finished || st == eTaskState::canceled) {
                        qWarning() << "IMGWAIT-DEAD:" << prp_getName()
                                   << "srcLoader state=" << int(st);
                    }
                } else {
                    qWarning() << "IMGWAIT-NONE:" << prp_getName()
                               << "no loader, frame renders imageless"
                               << "contInMem="
                               << (cont ? cont->storesDataInMemory() : false)
                               << "contTmp=" << (cont && cont->getTmpFile());
                }
            }
        }
    } else {
        const auto loader = mFileHandler->scheduleLoad();
        if (loader) { loader->addDependent(imgData); }
    }

    // skin bind: evaluate the deformed mesh HERE (GUI thread - bone
    // animator reads are unsafe off-thread); the raster path then only
    // consumes the assembled payload. Two driver families share the
    // mesh: bones (normalized LBS over the palette) and puppet pins
    // (additive offset blending, see below)
    if (hasSkinBind()) {
        QVector<SkinDriverPose> poses(mSkin.fDefs.count());
        const auto root = enve_cast<Bone*>(mSkinRoot->getTarget());
        if (root) {
            const auto chain = Bone::chain(root);
            for (int i = 0; i < mSkin.fDefs.count(); ++i) {
                const Bone* bone = nullptr;
                for (const auto b : chain) {
                    if (b && b->prp_getName() == mSkin.fDefs[i].fName) {
                        bone = b;
                        break;
                    }
                }
                if (!bone) continue;
                const QTransform cur =
                        bone->getTotalTransformAtFrame(relFrame);
                auto& pose = poses[i];
                pose.fHead = cur.map(QPointF(0., 0.));
                const QPointF tail = cur.map(
                            QPointF(bone->getLength(), 0.));
                pose.fAngle = std::atan2(tail.y() - pose.fHead.y(),
                                         tail.x() - pose.fHead.x());
                pose.fLen = bone->getLength();
                pose.fValid = true;
            }
        }
        const int pinCount = skinPinCount();
        QVector<SkPoint> pos;
        bool ok = false;
        if (mSkin.fDefs.count() > 0) {
            ok = SkinMeshGen::evaluate(mSkin, poses,
                                       data->fTotalTransform, pos);
        }
        if (!ok) pos = mSkin.fMesh.fPos;
        // puppet pins: ADDITIVE offset blending (not the normalized
        // bone LBS): the hold zone around a pin tracks it exactly, the
        // cubic falloff decays to zero, overlapping pins sum. This is
        // what keeps the grabbed artwork glued to the cursor
        for (int pi = 0; pi < pinCount; ++pi) {
            const auto pin = mSkinPins->pinAt(pi);
            if (!pin) continue;
            const QPointF bind = pin->bindRel();
            const QPointF delta = pin->getRelPos() - bind;
            if (QLineF(QPointF(), delta).length() < 0.01) continue;
            const qreal radius = pin->getRadius();
            for (int vi = 0; vi < pos.count(); ++vi) {
                const auto& rest = mSkin.fMesh.fPos[vi];
                const float w = SkinMeshGen::pinWeight(
                            QLineF(bind, QPointF(rest.x(), rest.y())).length(),
                            radius);
                if (w <= 0.f) continue;
                pos[vi] += SkPoint::Make(float(delta.x()) * w,
                                         float(delta.y()) * w);
            }
        }
        ok = ok || pinCount > 0;
        if (ok && !pos.isEmpty()) {
            imgData->fSkinned = true;
            mSkinWarnedNoBones = false;
            SkRect bounds;
            bounds.setBoundsCheck(pos.constData(), pos.count());
            bounds.outset(2.f, 2.f);
            imgData->fSkinBounds = bounds;
            imgData->fSkinVertices = SkVertices::MakeCopy(
                        SkVertices::kTriangles_VertexMode,
                        pos.count(),
                        pos.constData(),
                        // texcoords: the UNdeformed positions, the image
                        // is sampled where each vertex originally sat
                        mSkin.fMesh.fPos.constData(),
                        nullptr,
                        mSkin.fMesh.fIndices.count(),
                        mSkin.fMesh.fIndices.constData());
        } else if(!mSkinWarnedNoBones) {
            mSkinWarnedNoBones = true;
            qWarning() << "[SKIN]" << prp_getName()
                       << "evaluate: no live bone matched (chain root"
                          " unset or bone names changed) - drawing"
                          " undeformed";
        }
    }
}

stdsptr<BoxRenderData> ImageBox::createRenderData()
{
    if (!mFileHandler) { mFileHandler.assign(mPath); }
    return enve::make_shared<ImageBoxRenderData>(mFileHandler, this);
}

void ImageBox::saveSVG(SvgExporter& exp, DomEleTask* const eleTask) const {
    const QString imageId = SvgExportHelpers::ptrToStr(mFileHandler.data());
    const auto expPtr = &exp;
    const auto generate = [expPtr, eleTask, imageId](const sk_sp<SkImage>& image) {
        if(!image) return;
        SvgExportHelpers::defImage(*expPtr, image, imageId);
        auto& use = eleTask->initialize("use");
        use.setAttribute("href", "#" + imageId);
    };
    if(mFileHandler->hasImage()) {
        const auto image = mFileHandler->getImage();
        generate(image);
    } else {
        const auto task = mFileHandler->scheduleLoad();
        if(!task) return;
        const qptr<const ImageBox> thisPtr = this;
        const stdptr<DomEleTask> eleTaskPtr = eleTask;
        task->addDependent(
        {[thisPtr, eleTaskPtr, imageId, generate]() {
             if(!eleTaskPtr || !thisPtr) return;
             const auto image = thisPtr->mFileHandler->getImage();
             generate(image);
         }, nullptr});
        task->addDependent(eleTask);
    }
}

void ImageBoxRenderData::loadImageFromHandler() {
    if(fSrcCacheHandler) {
        setContainer(fSrcCacheHandler->getImageContainer());
    }
}
