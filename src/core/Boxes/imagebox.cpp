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
#include "canvas.h"
#include "Boxes/bone.h"
#include "Boxes/bonelayer.h"
#include "Private/document.h"
#include "simpletask.h"
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

    // pins stay visible in the object/point modes AND in the skin pin
    // tool itself - placing more pins while seeing the existing ones
    // is the whole point of the tool
    bool isVisible(const CanvasMode mode) const override;

    void startTransform() override;
    void finishTransform() override;

    void canvasContextMenu(PointTypeMenu * const menu) override;
private:
    SkinPin * const mPin;
};

// scene-space distance from a point to a bone's head-tail segment
static qreal distPointToBone(const Bone* const b, const QPointF& p) {
    const QPointF h = b->getHeadAbsPos();
    const QPointF t = b->getTailAbsPos();
    const QPointF d = t - h;
    const qreal len2 = d.x() * d.x() + d.y() * d.y();
    const qreal u = len2 > 0. ?
                qBound(0., ((p.x() - h.x()) * d.x() +
                            (p.y() - h.y()) * d.y()) / len2, 1.) : 0.;
    return QLineF(p, h + u * d).length();
}

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
        mSoft = enve::make_shared<QrealAnimator>(
                    0.5, 0, 1, 0.01,
                    QStringLiteral("\u67D4\u548C\u5EA6"));
        ca_addChild(mX);
        ca_addChild(mY);
        ca_addChild(mRadius);
        ca_addChild(mSoft);
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

    qreal getSoftness() const { return mSoft->getEffectiveValue(); }

    QPointF bindRel() const { return mBindRel; }
    void setBindRel(const QPointF& p) { mBindRel = p; }

    // ---- bone attachment ----
    // a bound pin rides its bone as a rigid passenger: the bone's
    // current-vs-bind rigid transform carries the pin's bind scene
    // position; the x/y animators stay a MANUAL offset on top (bone
    // animates the gross motion, pin keys add local accents)
    bool hasBone() const { return !mBoneName.isEmpty(); }
    const QString& boneName() const { return mBoneName; }
    const QTransform& boneBindTotal() const { return mBoneBindTotal; }
    const QPointF& bindScene() const { return mBindScene; }

    void attachBoneFollow(Bone * const bone) {
        if (mBoneFollowConn) QObject::disconnect(mBoneFollowConn);
        mBoneFollowConn = QMetaObject::Connection();
        if (!bone) return;
        // context = this pin: the connection dies with it
        mBoneFollowConn = connect(
                    bone, &Animator::prp_absFrameRangeChanged,
                    this, [this](const FrameRange& abs) {
            SimpleTask::sScheduleContexted(this, [this, abs]() {
                if (mBox) mBox->prp_afterChangedAbsRange(abs);
            });
        });
    }

    // re-establish the follow connection for a pin restored from a
    // file (the bone object only resolves after the whole load)
    void reconnectBoneFollow() {
        if (mBoneName.isEmpty() || !mBox) return;
        for (const auto b : mBox->skinCandidateBones()) {
            if (b && b->prp_getName() == mBoneName) {
                attachBoneFollow(b);
                break;
            }
        }
    }

    void bindToBone(Bone * const bone) {
        if (!bone || !mBox) return;
        mBoneName = bone->prp_getName();
        mBoneBindTotal = bone->getTotalTransform();
        mBindScene = mBox->getTotalTransform().map(getRelPos());
        attachBoneFollow(bone);
        qDebug() << "[SKIN] pin bound to bone" << mBoneName;
    }

    void unbindBone() {
        if (mBoneName.isEmpty()) return;
        mBoneName.clear();
        if (mBoneFollowConn) {
            QObject::disconnect(mBoneFollowConn);
            mBoneFollowConn = QMetaObject::Connection();
        }
        qDebug() << "[SKIN] pin unbound";
    }

    // bind to the nearest bone in the scene (within reach); returns
    // true when bound
    // bind to the nearest bone among the candidates; enforceReach
    // caps the distance (placement-time adoption) while the explicit
    // bulk action binds to the nearest bone however far; returns true
    // when bound
    bool tryBindNearestBone(const bool enforceReach = true) {
        if (!mBox) return false;
        const QPointF pScene =
                mBox->getTotalTransform().map(effectiveRelPos());
        Bone* best = nullptr;
        qreal bestD = 1e12;
        for (const auto b : mBox->skinCandidateBones()) {
            if (!b) continue;
            const qreal d = distPointToBone(b, pScene);
            if (d < bestD) { bestD = d; best = b; }
        }
        if (best && (!enforceReach ||
                     bestD <= qMax(200., 1.5 * best->getLength()))) {
            bindToBone(best);
            return true;
        }
        return false;
    }

    // bone-driven part of the position, in image rel space
    QPointF drivenRelPos() const {
        if (mBoneName.isEmpty() || !mBox) return QPointF();
        Bone* bone = nullptr;
        for (const auto b : mBox->skinCandidateBones()) {
            if (b && b->prp_getName() == mBoneName) { bone = b; break; }
        }
        if (!bone) return QPointF();
        const QTransform cur = bone->getTotalTransform();
        // NOTE Qt row-vector convention: A*B applies A FIRST, so the
        // driven transform is bindInv*cur (undo the bind pose, then
        // apply the current one) - writing cur*bindInv swaps axes
        // whenever the bone carries rotation+translation
        const QPointF drivenScene =
                (mBoneBindTotal.inverted() * cur).map(mBindScene);
        return mBox->getTotalTransform().inverted().map(drivenScene);
    }

    // where the pin visually sits: bone-driven position plus the
    // manual (x/y) offset relative to the bind
    QPointF effectiveRelPos() const {
        if (mBoneName.isEmpty()) return getRelPos();
        return drivenRelPos() + (getRelPos() - mBindRel);
    }

    // drag target: solve x/y so the EFFECTIVE position equals p
    void setEffectiveRelPos(const QPointF& p) {
        if (mBoneName.isEmpty()) { setRelPos(p); return; }
        setRelPos(p - drivenRelPos() + mBindRel);
    }

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

    // refresh the render cache after an out-of-animator change
    // (bone bind / unbind)
    void notifySkinChanged() {
        if (mBox) mBox->skinChangedNotify();
    }

    // serialization: the child animators positionally (created by the
    // ctor; the softness child is version-gated - older files carry
    // only x/y/radius), then the bind pos and the bone attachment
    void prp_writeProperty_impl(eWriteStream& dst) const {
        for (const auto& prop : ca_getChildren()) {
            prop->prp_writeProperty(dst);
        }
        dst << mBindRel;
        dst << mBoneName << mBoneBindTotal << mBindScene;
    }

    void prp_readProperty_impl(eReadStream& src) {
        for (const auto& prop : ca_getChildren()) {
            prop->prp_readProperty(src);
        }
        src >> mBindRel;
        src >> mBoneName >> mBoneBindTotal >> mBindScene;
    }

    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const {
        auto ele = exp.createElement(QStringLiteral("SkinPin"));
        ele.setAttribute(QStringLiteral("x"), mX->getEffectiveValue());
        ele.setAttribute(QStringLiteral("y"), mY->getEffectiveValue());
        ele.setAttribute(QStringLiteral("radius"),
                         mRadius->getEffectiveValue());
        ele.setAttribute(QStringLiteral("softness"),
                         mSoft->getEffectiveValue());
        ele.setAttribute(QStringLiteral("bindX"), mBindRel.x());
        ele.setAttribute(QStringLiteral("bindY"), mBindRel.y());
        if (!mBoneName.isEmpty()) {
            ele.setAttribute(QStringLiteral("bone"), mBoneName);
        }
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
        mSoft->setCurrentBaseValue(
                    ele.attribute(QStringLiteral("softness"),
                                  QStringLiteral("0.5")).toDouble());
        mBoneName = ele.attribute(QStringLiteral("bone"));
    }

private:
    qptr<ImageBox> mBox;
    qsptr<QrealAnimator> mX;
    qsptr<QrealAnimator> mY;
    qsptr<QrealAnimator> mRadius;
    qsptr<QrealAnimator> mSoft;
    QPointF mBindRel;
    QPointF mPosAtStart;
    // bone attachment (empty name = free pin)
    QString mBoneName;
    QTransform mBoneBindTotal;
    QPointF mBindScene;
    // bone-motion follow: the pin x/y animators do NOT change when
    // the bone moves, so without this connection the skinned image
    // never re-renders on bone animation
    QMetaObject::Connection mBoneFollowConn;
};

QPointF SkinPinPoint::getRelativePos() const { return mPin->effectiveRelPos(); }

bool SkinPinPoint::isVisible(const CanvasMode mode) const {
    return mode == CanvasMode::pointTransform ||
           mode == CanvasMode::boxTransform ||
           mode == CanvasMode::skinPin;
}

void SkinPinPoint::setRelativePos(const QPointF &relPos) {
    mPin->setEffectiveRelPos(relPos);
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
    menu->addSeparator();
    const PointTypeMenu::PlainSelectedOp<SkinPinPoint> bindOp =
            [pin = mPin](SkinPinPoint *) {
        if (pin->tryBindNearestBone()) pin->notifySkinChanged();
        else qWarning() << "[SKIN] no bone near enough to bind";
    };
    menu->addPlainAction(QIcon::fromTheme("group"),
                         QStringLiteral("\u7ED1\u5B9A\u5230\u6700\u8FD1\u9AA8\u9ABC"),
                         bindOp);
    const PointTypeMenu::PlainSelectedOp<SkinPinPoint> unbindOp =
            [pin = mPin](SkinPinPoint *) {
        pin->unbindBone();
        pin->notifySkinChanged();
    };
    menu->addPlainAction(QIcon::fromTheme("edit-clear"),
                         QStringLiteral("\u89E3\u9664\u9AA8\u9ABC\u7ED1\u5B9A"),
                         unbindOp);
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
    // skin mesh block (positional): bind transform + lattice
    dst << (hasSkinBind() ? int(1) : int(0));
    if(!hasSkinBind()) return;
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
}

void ImageBox::readBoundingBox(eReadStream& src) {
    BoundingBox::readBoundingBox(src);
    const QString path = src.readFilePath();
    setFilePathNoRename(path);
    int hasSkin; src >> hasSkin;
    if(!hasSkin) return;
    src >> mSkin.fBindBoxTotal;
    int cellPx, imgW, imgH;
    src >> cellPx >> imgW >> imgH;
    int posCount; src >> posCount;
    if(posCount < 3 || posCount >= 65536) return;
    mSkin.fMesh.fPos.resize(posCount);
    src.read(mSkin.fMesh.fPos.data(),
             qint64(posCount) * qint64(sizeof(SkPoint)));
    int idxCount; src >> idxCount;
    if(idxCount < 3 || idxCount > posCount * 8) return;
    mSkin.fMesh.fIndices.resize(idxCount);
    src.read(mSkin.fMesh.fIndices.data(),
             qint64(idxCount) * qint64(sizeof(uint16_t)));
    mSkin.fMesh.fCellPx = cellPx;
    mSkin.fMesh.fImgW = imgW;
    mSkin.fMesh.fImgH = imgH;
    // re-establish the bone follow connections once the event loop
    // settles (bones may load after this image)
    SimpleTask::sScheduleContexted(this, [this]() {
        for (int i = 0; i < skinPinCount(); ++i) {
            if (const auto pin = mSkinPins->pinAt(i)) {
                pin->reconnectBoneFollow();
            }
        }
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
    return mSkinPins && mSkinPins->pinCount() > 0;
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
    // auto-adopt: with a skeleton in the scene a fresh pin binds to
    // its nearest bone right away (a free pin when nothing is close)
    pin->tryBindNearestBone();
    qDebug() << "[SKIN]" << prp_getName() << "pin added at"
             << relPos << "radius" << radius
             << "bone=" << (pin->hasBone() ? pin->boneName()
                                           : QStringLiteral("(free)"))
             << "mesh verts=" << mSkin.fMesh.fPos.count();
    prp_updateCanvasProps();
    prp_afterWholeInfluenceRangeChanged();
    if (Document::sInstance) Document::sInstance->actionFinished();
    return pin;
}

void ImageBox::skinChangedNotify() {
    prp_afterWholeInfluenceRangeChanged();
    if (Document::sInstance) Document::sInstance->actionFinished();
}

// one-click full-skeleton bind: place pins along every bone in the
// scene (head/middle/tail, deduplicated at the joints) and bind each
// to its own bone - the whole rig drives the mesh through the pin
// layer, no chain/layer semantics needed
void ImageBox::skinPinsBindSkeleton() {
    const auto scene = getParentScene();
    if (!scene) return;
    const auto bones = skinCandidateBones();
    if (bones.isEmpty()) {
        qWarning() << "[SKIN] skinPinsBindSkeleton: no bones in the scene";
        return;
    }
    if (!mSkin.fMesh.isValid()) {
        mSkin.fBindBoxTotal = getTotalTransform();
        skinGenerateMesh(mSkin);
    }
    const QTransform invL = getTotalTransform().inverted();
    QList<QPointF> placed;   // joint dedup (chain heads == parent tails)
    int added = 0;
    int skipped = 0;
    for (const auto bone : bones) {
        if (!bone) continue;
        const QTransform bt = bone->getTotalTransform();
        const QPointF headS = bt.map(QPointF(0., 0.));
        const QPointF tailS = bt.map(QPointF(bone->getLength(), 0.));
        const QPointF midS((headS.x() + tailS.x()) * 0.5,
                           (headS.y() + tailS.y()) * 0.5);
        const QPointF headR = invL.map(headS);
        const QPointF tailR = invL.map(tailS);
        const qreal lenR = QLineF(headR, tailR).length();
        const qreal radius = qBound(40., 0.75 * lenR, 2000.);
        for (const auto& ps : { headS, midS, tailS }) {
            const QPointF rel = invL.map(ps);
            bool dup = false;
            for (const auto& q : placed) {
                if (QLineF(q, rel).length() < 2.) { dup = true; break; }
            }
            if (dup) { skipped++; continue; }
            placed.append(rel);
            const auto pin = mSkinPins->addPin(rel, radius);
            pin->bindToBone(bone);
            added++;
        }
    }
    qDebug() << "[SKIN]" << prp_getName()
             << "skeleton bind: bones=" << bones.count()
             << "pins=" << added << "jointDups=" << skipped
             << "mesh verts=" << mSkin.fMesh.fPos.count();
    prp_updateCanvasProps();
    prp_afterWholeInfluenceRangeChanged();
    if (Document::sInstance) Document::sInstance->actionFinished();
}

// bind every FREE pin to its nearest bone in one action (the
// "pins first, rig later" workflow: placement-time adoption found no
// bones because the skeleton did not exist yet)
void ImageBox::skinPinsAutoBindBones() {
    maybeAutoBindFreePins(false);
}

// automatic binding hook: fires at the natural moments (first bone
// pose, image dropped into a bone group that has bones) - an image
// carrying pins inside a bone group MEANS bind, per the user model.
// No-op without free pins or without bones in scope, so callers can
// invoke it liberally
void ImageBox::maybeAutoBindFreePins(const bool quiet) {
    // only inside a bone-layer scope (an image outside any rig must
    // not grab scene bones on its own)
    bool inBoneLayer = false;
    for (auto p = getParentGroup(); p; p = p->getParentGroup()) {
        if (enve_cast<BoneLayer*>(p)) { inBoneLayer = true; break; }
    }
    if (!inBoneLayer || skinCandidateBones().isEmpty()) return;
    int free = 0;
    for (int i = 0; i < skinPinCount(); ++i) {
        const auto pin = mSkinPins->pinAt(i);
        if (pin && !pin->hasBone()) { free++; break; }
    }
    if (free == 0) return;
    int bound = 0;
    for (int i = 0; i < skinPinCount(); ++i) {
        const auto pin = mSkinPins->pinAt(i);
        if (!pin || pin->hasBone()) continue;
        if (pin->tryBindNearestBone(false)) bound++;
    }
    qDebug() << "[SKIN]" << prp_getName()
             << "auto-bind: bound=" << bound << "pins";
    if (bound > 0) prp_afterWholeInfluenceRangeChanged();
    if (!quiet && Document::sInstance) {
        Document::sInstance->actionFinished();
    }
}

// sweep every image under a container (e.g. a bone layer) and
// auto-bind their free pins
void ImageBox::autoBindFreePinsUnder(ContainerBox * const root) {
    if (!root) return;
    for (const auto& c : root->getContained()) {
        if (const auto bone = enve_cast<Bone*>(c.data())) {
            autoBindFreePinsUnder(bone);
        } else if (const auto img = enve_cast<ImageBox*>(c.data())) {
            img->maybeAutoBindFreePins(true);
        } else if (const auto group =
                   enve_cast<ContainerBox*>(c.data())) {
            autoBindFreePinsUnder(group);
        }
    }
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

// bones under a container, recursively (bones nest inside bones and
// live inside bone layers)
static void collectBones(ContainerBox* const c, QList<Bone*>& out) {
    if (!c) return;
    for (const auto& child : c->getContained()) {
        if (const auto bone = enve_cast<Bone*>(child.data())) {
            out.append(bone);
            collectBones(bone, out);
        } else if (const auto group =
                   enve_cast<ContainerBox*>(child.data())) {
            collectBones(group, out);
        }
    }
}

QList<Bone*> ImageBox::skinCandidateBones() {
    // the rig that owns this image: the nearest bone LAYER wrapping
    // it (a character lives inside one). Bones under that layer are
    // the only bind candidates - both for nearest-bone adoption and
    // for name resolution, so multi-character scenes never cross rigs
    // and duplicate bone names cannot resolve to the wrong skeleton.
    // Fallback (image not inside any bone layer): all scene bones.
    for (auto p = getParentGroup(); p; p = p->getParentGroup()) {
        if (enve_cast<BoneLayer*>(p)) {
            QList<Bone*> bones;
            collectBones(p, bones);
            return bones;
        }
    }
    const auto scene = getParentScene();
    return scene ? scene->getBones() : QList<Bone*>();
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

void ImageBox::skinUnbind() {
    if(!hasSkinBind()) return;
    mSkin.fMesh = SkinMesh();
    clearSkinPins();
    prp_afterWholeInfluenceRangeChanged();
    if(Document::sInstance) Document::sInstance->actionFinished();
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

    const PropertyMenu::PlainSelectedOp<ImageBox> bindSkelOp =
    [](ImageBox * box) { box->skinPinsBindSkeleton(); };
    menu->addPlainAction(QIcon::fromTheme("group"),
                         QStringLiteral("\u8499\u76AE\u9489\u7ED1\u5B9A\u9AA8\u67B6\uFF08\u6CBF\u9AA8\u9ABC\u81EA\u52A8\u5E03\u9489\uFF09"),
                         bindSkelOp);

    const PropertyMenu::PlainSelectedOp<ImageBox> autoBindOp =
    [](ImageBox * box) { box->skinPinsAutoBindBones(); };
    menu->addPlainAction(QIcon::fromTheme("bone"),
                         QStringLiteral("\u56FE\u9489\u81EA\u52A8\u7ED1\u5B9A\u9AA8\u9ABC\uFF08\u6BCF\u9489\u8BA4\u9886\u6700\u8FD1\u9AA8\u9ABC\uFF09"),
                         autoBindOp);

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
    // mesh: puppet pins (additive offset blending). Bone-bound pins
    // contribute their EFFECTIVE position: the bone's rigid motion
    // (evaluated at this frame) plus the pin's manual x/y offset
    if (hasSkinBind()) {
        QVector<SkPoint> pos = mSkin.fMesh.fPos;
        const int pinCount = skinPinCount();
        for (int pi = 0; pi < pinCount; ++pi) {
            const auto pin = mSkinPins->pinAt(pi);
            if (!pin) continue;
            const QPointF bind = pin->bindRel();
            QPointF eff = pin->getRelPos();
            if (pin->hasBone()) {
                const Bone* bone = nullptr;
                for (const auto b : skinCandidateBones()) {
                    if (b && b->prp_getName() == pin->boneName()) {
                        bone = b;
                        break;
                    }
                }
                if (bone) {
                    const QTransform curB =
                            bone->getTotalTransformAtFrame(relFrame);
                    // Qt row-vector convention: bindInv first, then
                    // cur (see SkinPin::drivenRelPos)
                    const QPointF drivenScene =
                            (pin->boneBindTotal().inverted() * curB)
                                .map(pin->bindScene());
                    eff = data->fTotalTransform.inverted().map(drivenScene)
                            + (pin->getRelPos() - bind);
                }
            }
            const QPointF delta = eff - bind;
            if (QLineF(QPointF(), delta).length() < 0.01) continue;
            const qreal radius = pin->getRadius();
            const qreal softness = pin->getSoftness();
            for (int vi = 0; vi < pos.count(); ++vi) {
                const auto& rest = mSkin.fMesh.fPos[vi];
                const float w = SkinMeshGen::pinWeight(
                            QLineF(bind, QPointF(rest.x(), rest.y())).length(),
                            radius, softness);
                if (w <= 0.f) continue;
                pos[vi] += SkPoint::Make(float(delta.x()) * w,
                                         float(delta.y()) * w);
            }
        }
        if (!pos.isEmpty()) {
            imgData->fSkinned = true;
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
