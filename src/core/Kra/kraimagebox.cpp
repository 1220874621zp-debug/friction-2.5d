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

#include "kraimagebox.h"
#include "kraimporter.h"

#include <QDir>
#include <QFile>
#include <QMessageBox>

#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"
#include "Boxes/containerbox.h"
#include "canvas.h"
#include "exceptions.h"
#include "Private/document.h"
#include "typemenu.h"

namespace {

// 中文提示前缀通过运行时构造，避免 MSVC C2001（GBK 源文件含非 ASCII）
const QString missingPrefix() { return QStringLiteral("[missing] "); }

void collectKraImageBoxes(ContainerBox * const container,
                          const QString &sourceKra,
                          QList<KraImageBox*> &out)
{
    if (!container) { return; }
    for (auto *box : container->getContainedBoxes()) {
        if (const auto kraBox = dynamic_cast<KraImageBox*>(box)) {
            if (kraBox->sourceKra() == sourceKra) { out << kraBox; }
        } else if (const auto group = dynamic_cast<ContainerBox*>(box)) {
            collectKraImageBoxes(group, sourceKra, out);
        }
    }
}

} // namespace

KraImageBox::KraImageBox() :
    ImageBox(QStringLiteral("Image"), eBoxType::kraImage) {}

KraImageBox::KraImageBox(const QString &filePath,
                         const QString &sourceKra,
                         const QString &layerUuid,
                         const QString &frameFile,
                         const quint32 crc) :
    ImageBox(QStringLiteral("Image"), eBoxType::kraImage),
    mSourceKra(sourceKra), mLayerUuid(layerUuid),
    mFrameFile(frameFile), mCrc(crc)
{
    setFilePathNoRename(filePath);
}

void KraImageBox::writeBoundingBox(eWriteStream &dst) const
{
    ImageBox::writeBoundingBox(dst);
    dst.writeFilePath(mSourceKra);
    dst << mLayerUuid;
    dst << mFrameFile;
    dst << mCrc;
}

void KraImageBox::readBoundingBox(eReadStream &src)
{
    ImageBox::readBoundingBox(src);
    // normalize separators so the cache-dir hash stays stable across
    // save/load cycles (readFilePath may return a mixed '/'+'\' path)
    mSourceKra = QDir::cleanPath(src.readFilePath());
    src >> mLayerUuid;
    src >> mFrameFile;
    src >> mCrc;
    ensureCachedFile();
}

QDomElement KraImageBox::prp_writePropertyXEV_impl(const XevExporter &exp) const
{
    auto result = ImageBox::prp_writePropertyXEV_impl(exp);
    result.setAttribute(QStringLiteral("kraSourceFile"), mSourceKra);
    result.setAttribute(QStringLiteral("kraLayerUuid"), mLayerUuid);
    result.setAttribute(QStringLiteral("kraFrameFile"), mFrameFile);
    result.setAttribute(QStringLiteral("kraCrc"),
                        QString::number(mCrc));
    return result;
}

void KraImageBox::prp_readPropertyXEV_impl(const QDomElement &ele,
                                           const XevImporter &imp)
{
    ImageBox::prp_readPropertyXEV_impl(ele, imp);
    mSourceKra = QDir::cleanPath(ele.attribute(
                QStringLiteral("kraSourceFile")));
    mLayerUuid = ele.attribute(QStringLiteral("kraLayerUuid"));
    mFrameFile = ele.attribute(QStringLiteral("kraFrameFile"));
    mCrc = ele.attribute(QStringLiteral("kraCrc"), "0").toUInt();
    ensureCachedFile();
}

bool KraImageBox::ensureCachedFile()
{
    if (mSourceKra.isEmpty() || mFrameFile.isEmpty()) { return false; }
    if (QFile::exists(filePath())) { return true; }

    // pixel cache was cleared: re-extract the layer from the source
    const auto state = ImportKRA::checkLayerUpdate(
                mSourceKra, mLayerUuid, mFrameFile, 0);
    if (!state.found || !state.pixelsChanged) { return false; }
    const QString cachePath = kraCachePng(mSourceKra, mFrameFile,
                                          state.image);
    if (cachePath.isEmpty()) { return false; }
    mCrc = state.crc;
    setFilePathNoRename(cachePath);
    return true;
}

void KraImageBox::setupCanvasMenu(PropertyMenu * const menu)
{
    if (menu->hasActionsForType<KraImageBox>()) { return; }
    menu->addedActionsForType<KraImageBox>();

    const PropertyMenu::PlainSelectedOp<KraImageBox> updateOp =
    [](KraImageBox * box) {
        try {
            box->updateFromSource();
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
    };
    menu->addPlainAction(QIcon::fromTheme("loop"),
                         tr("Update Layer from Source KRA"), updateOp);

    const PropertyMenu::PlainSelectedOp<KraImageBox> syncOp =
    [](KraImageBox * box) {
        try {
            box->syncAllFromSource();
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
    };
    menu->addPlainAction(QIcon::fromTheme("linked"),
                         tr("Sync All Layers from Source KRA"), syncOp);

    menu->addSeparator();

    ImageBox::setupCanvasMenu(menu);
}

bool KraImageBox::updateFromSource()
{
    if (mSourceKra.isEmpty() || mLayerUuid.isEmpty()) {
        qWarning() << "KRA update: box is not bound to a source kra";
        return false;
    }
    if (!QFile::exists(mSourceKra)) {
        QMessageBox::warning(nullptr, tr("KRA Update"),
                tr("Source KRA is missing:\n%1").arg(mSourceKra));
        return false;
    }

    const auto state = ImportKRA::checkLayerUpdate(
                mSourceKra, mLayerUuid, mFrameFile, mCrc);
    if (!state.found) {
        const QString name = prp_getName();
        if (!name.startsWith(missingPrefix())) {
            prp_setName(missingPrefix() + name);
        }
        QMessageBox::warning(nullptr, tr("KRA Update"),
                tr("Layer no longer exists in the source KRA.\n"
                   "It is kept (marked as missing) with all animation data."));
        return false;
    }
    // layer found: restore the original name if marked missing
    const QString name = prp_getName();
    if (name.startsWith(missingPrefix())) {
        prp_setName(name.mid(missingPrefix().size()));
    }

    // refresh non-pixel attributes
    const auto trans = getBoxTransformAnimator();
    trans->setOpacity(qBound(0., state.opacity / 2.55, 100.));
    if (state.visible) { show(); } else { hide(); }
    setBlendModeSk(ImportKRA::blendModeFromKrita(state.blendMode));

    if (!state.pixelsChanged) {
        QMessageBox::information(nullptr, tr("KRA Update"),
                                  tr("Layer pixels unchanged."));
        return true;
    }

    // position compensation: the frame moved in the source document,
    // shift the whole transform (base value + every keyframe) so the
    // animation data is preserved. (Only meaningful when pixels were
    // re-decoded: posX/posY come from the tile extent.)
    const QPointF oldPos = trans->getPosAnimator()->getBaseValue();
    const qreal dx = state.posX - oldPos.x();
    const qreal dy = state.posY - oldPos.y();
    if (dx != 0 || dy != 0) { trans->translate(dx, dy); }

    const QString cachePath = kraCachePng(mSourceKra, mFrameFile,
                                          state.image);
    if (cachePath.isEmpty()) {
        qWarning() << "KRA update: failed to refresh cached pixels";
        return false;
    }
    mCrc = state.crc;
    // point to the fresh cache file: the new path forces a clean
    // handler assign + load, safer than reloading in place
    setFilePathNoRename(cachePath);
    // clear the render-data cache and schedule a canvas redraw,
    // otherwise the stale rendered image stays on screen
    planUpdate(UpdateReason::userChange);
    QMessageBox::information(nullptr, tr("KRA Update"),
                              tr("Layer updated from source KRA."));
    return true;
}

void KraImageBox::syncAllFromSource()
{
    if (mSourceKra.isEmpty()) { return; }
    if (!QFile::exists(mSourceKra)) {
        QMessageBox::warning(nullptr, tr("KRA Sync"),
                tr("Source KRA is missing:\n%1").arg(mSourceKra));
        return;
    }

    QList<KraImageBox*> boxes;
    const auto scene = getParentScene();
    if (scene) { collectKraImageBoxes(scene, mSourceKra, boxes); }
    if (!boxes.contains(this)) { boxes.append(this); }

    int updatedCount = 0;
    int missingCount = 0;
    for (auto *box : boxes) {
        const auto state = ImportKRA::checkLayerUpdate(
                    mSourceKra, box->layerUuid(), box->frameFile(),
                    box->sourceCrc());
        if (!state.found) {
            missingCount++;
            const QString name = box->prp_getName();
            if (!name.startsWith(missingPrefix())) {
                box->prp_setName(missingPrefix() + name);
            }
            continue;
        }
        const QString name = box->prp_getName();
        if (name.startsWith(missingPrefix())) {
            box->prp_setName(name.mid(missingPrefix().size()));
        }
        // non-pixel attributes follow the source document
        const auto boxTrans = box->getBoxTransformAnimator();
        boxTrans->setOpacity(qBound(0., state.opacity / 2.55, 100.));
        if (state.visible) { box->show(); } else { box->hide(); }
        box->setBlendModeSk(ImportKRA::blendModeFromKrita(state.blendMode));
        if (!state.pixelsChanged) { continue; }

        // same position compensation as updateFromSource: shift the
        // whole transform (base value + every keyframe) so animation
        // data survives a layer move in the source document
        const auto trans = box->getBoxTransformAnimator();
        const QPointF oldPos = trans->getPosAnimator()->getBaseValue();
        const qreal dx = state.posX - oldPos.x();
        const qreal dy = state.posY - oldPos.y();
        if (dx != 0 || dy != 0) { trans->translate(dx, dy); }

        const QString cachePath = kraCachePng(mSourceKra,
                                              box->frameFile(),
                                              state.image);
        if (cachePath.isEmpty()) {
            qWarning() << "KRA sync: failed to cache layer" << state.name;
            continue;
        }
        box->mCrc = state.crc;
        box->setFilePathNoRename(cachePath);
        box->planUpdate(UpdateReason::userChange);
        updatedCount++;
    }
    if (Document::sInstance) { Document::sInstance->actionFinished(); }

    if (updatedCount == 0 && missingCount == 0) {
        QMessageBox::information(nullptr, tr("KRA Sync"),
                                  tr("All layers are up to date."));
    } else {
        QMessageBox::information(nullptr, tr("KRA Sync"),
                tr("Updated layers: %1\nMissing layers: %2")
                .arg(updatedCount).arg(missingCount));
    }
}
