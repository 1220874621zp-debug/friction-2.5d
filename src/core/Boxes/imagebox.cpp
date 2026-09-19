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
#include "Boxes/bone.h"
#include "Private/document.h"
#include "simpletask.h"
#include "ReadWrite/evformat.h"

#include <QtMath>

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
    // skin bind block (positional, version-gated on read)
    dst << (mSkin.hasBind() ? int(1) : int(0));
    if(!mSkin.hasBind()) return;
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

bool ImageBox::skinGenerateMesh(SkinBindData& skin) {
    const sk_sp<SkImage> img = mFileHandler ?
                mFileHandler->getImage() : nullptr;
    SkPixmap pm;
    if(img && img->peekPixels(&pm)) {
        // coarsen until the vertex count fits SkVertices' uint16
        // indices (a fully covered 8k image at 20px needs two steps)
        for(int cellPx = 20; cellPx < 400; cellPx *= 2) {
            if(SkinMeshGen::generate(pm, cellPx, skin.fMesh)) return true;
        }
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
    skinSetupFollowConns();
    prp_afterWholeInfluenceRangeChanged();
}

bool ImageBox::skinBindChain(Bone* const chainRoot) {
    if(!chainRoot) return false;
    const auto chain = SkinMeshGen::collectChain(chainRoot);
    if(chain.isEmpty()) return false;
    skinCaptureDefs(chain);
    if(!skinGenerateMesh(mSkin)) {
        mSkin.fDefs.clear();
        return false;
    }
    mSkinInternalSet = true;
    mSkinRoot->setTargetAction(chainRoot);
    mSkinInternalSet = false;
    skinFinishBind();
    return true;
}

void ImageBox::skinUnbind() {
    if(!mSkin.hasBind()) return;
    mSkin.fDefs.clear();
    mSkin.fMesh = SkinMesh();
    skinClearFollowConns();
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
    const auto chain = SkinMeshGen::collectChain(root);
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
    for(const auto bone : SkinMeshGen::collectChain(root)) {
        if(!bone) continue;
        mSkinFollowConns << connect(
                    bone, &BoundingBox::prp_absFrameRangeChanged,
                    this, [this](const FrameRange& abs) {
            SimpleTask::sScheduleContexted(this, [this, abs]() {
                prp_afterChangedAbsRange(abs);
            });
        });
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
    // consumes the assembled payload
    if (mSkin.hasBind()) {
        const auto root = enve_cast<Bone*>(mSkinRoot->getTarget());
        QVector<SkPoint> pos;
        const bool ok = SkinMeshGen::evaluate(mSkin, root, relFrame,
                                              data->fTotalTransform, pos);
        if (ok && !pos.isEmpty()) {
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
