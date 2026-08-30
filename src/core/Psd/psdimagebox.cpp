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

#include "psdimagebox.h"
#include "fpsdpackage.h"
#include "psdfile.h"
#include "psdimporter.h"

#include <QCheckBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

#include "Animators/transformanimator.h"
#include "Boxes/containerbox.h"
#include "canvas.h"
#include "exceptions.h"
#include "Private/document.h"
#include "typemenu.h"

namespace {

const QString missingPrefix() { return QStringLiteral("[missing] "); }

void collectPsdImageBoxes(ContainerBox * const container,
                          const QString &packagePath,
                          QList<PsdImageBox*> &out)
{
    if (!container) { return; }
    for (auto *box : container->getContainedBoxes()) {
        if (const auto psdBox = dynamic_cast<PsdImageBox*>(box)) {
            if (psdBox->sourcePackage() == packagePath) { out << psdBox; }
        } else if (const auto group = dynamic_cast<ContainerBox*>(box)) {
            collectPsdImageBoxes(group, packagePath, out);
        }
    }
}

// ask the user which of the newly discovered layers to import;
// returns the selected indices into names, empty on cancel
QList<int> askNewLayers(const QStringList &names)
{
    QDialog dialog(nullptr);
    dialog.setWindowTitle(QObject::tr("New Layers in Source PSD"));
    auto * const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QObject::tr(
            "The source PSD contains new layers.\n"
            "Select the layers you want to import:")));

    QList<QCheckBox*> checkBoxes;
    for (const auto &name : names) {
        auto * const cb = new QCheckBox(name, &dialog);
        cb->setChecked(true);
        layout->addWidget(cb);
        checkBoxes << cb;
    }
    auto * const buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    // standard button captions come from QPlatformTheme ( untranslated
    // without qtbase qm) - set Chinese captions explicitly
    auto * const okBtn = buttons->button(QDialogButtonBox::Ok);
    if (okBtn) { okBtn->setText(QObject::tr("OK")); }
    auto * const cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    if (cancelBtn) { cancelBtn->setText(QObject::tr("Cancel")); }
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    QList<int> result;
    if (dialog.exec() == QDialog::Accepted) {
        for (int i = 0; i < checkBoxes.size(); i++) {
            if (checkBoxes[i]->isChecked()) { result << i; }
        }
    }
    return result;
}

} // namespace

PsdImageBox::PsdImageBox() :
    ImageBox(QStringLiteral("Image"), eBoxType::psdImage) {}

PsdImageBox::PsdImageBox(const QString &filePath,
                         const QString &sourcePackage,
                         const QString &sourceLayerKey) :
    ImageBox(QStringLiteral("Image"), eBoxType::psdImage),
    mSourcePackage(sourcePackage), mSourceLayerKey(sourceLayerKey)
{
    setFilePathNoRename(filePath);
}

void PsdImageBox::setPsdSource(const QString &sourcePackage,
                               const QString &sourceLayerKey)
{
    mSourcePackage = sourcePackage;
    mSourceLayerKey = sourceLayerKey;
}

void PsdImageBox::writeBoundingBox(eWriteStream &dst) const
{
    ImageBox::writeBoundingBox(dst);
    dst.writeFilePath(mSourcePackage);
    dst << mSourceLayerKey;
}

void PsdImageBox::readBoundingBox(eReadStream &src)
{
    ImageBox::readBoundingBox(src);
    // normalize separators so the cache-dir hash stays stable across
    // save/load cycles (readFilePath may return a mixed '/'+'\' path)
    mSourcePackage = QDir::cleanPath(src.readFilePath());
    src >> mSourceLayerKey;
    ensureCachedFile();
}

QDomElement PsdImageBox::prp_writePropertyXEV_impl(const XevExporter &exp) const
{
    auto result = ImageBox::prp_writePropertyXEV_impl(exp);
    result.setAttribute(QStringLiteral("psdSourcePackage"), mSourcePackage);
    result.setAttribute(QStringLiteral("psdSourceLayer"), mSourceLayerKey);
    return result;
}

void PsdImageBox::prp_readPropertyXEV_impl(const QDomElement &ele,
                                           const XevImporter &imp)
{
    ImageBox::prp_readPropertyXEV_impl(ele, imp);
    mSourcePackage = ele.attribute(QStringLiteral("psdSourcePackage"));
    mSourceLayerKey = ele.attribute(QStringLiteral("psdSourceLayer"));
    ensureCachedFile();
}

bool PsdImageBox::ensureCachedFile()
{
    if (mSourcePackage.isEmpty() || mSourceLayerKey.isEmpty()) { return false; }
    if (QFile::exists(filePath())) { return true; }

    // pixel cache was cleared: re-extract the layer from the package
    const auto entries = Fpsd::readPackage(mSourcePackage);
    const QByteArray png = entries.value(
                Fpsd::layerEntryName(mSourceLayerKey));
    if (png.isEmpty()) { return false; }

    const QString cachePath = Fpsd::writeLayerCacheFile(
                mSourcePackage, mSourceLayerKey, png);
    if (cachePath.isEmpty()) { return false; }
    setFilePathNoRename(cachePath);
    return true;
}

void PsdImageBox::setupRenderData(const qreal relFrame,
                                  const QMatrix& parentM,
                                  BoxRenderData * const data,
                                  Canvas* const scene)
{
    // runtime fallback: the pixel cache lives in the cache dir and can be
    // wiped by disk cleanup at any time (ensureCachedFile is otherwise only
    // called when the project is deserialized). Without this the layer
    // renders empty forever after the cache file disappears.
    if (mFileHandler && !mFileHandler->hasImage()) {
        ensureCachedFile();
    }
    ImageBox::setupRenderData(relFrame, parentM, data, scene);
}

void PsdImageBox::setupCanvasMenu(PropertyMenu * const menu)
{
    if (menu->hasActionsForType<PsdImageBox>()) { return; }
    menu->addedActionsForType<PsdImageBox>();

    const PropertyMenu::PlainSelectedOp<PsdImageBox> updateOp =
    [](PsdImageBox * box) {
        try {
            box->updateFromSource();
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
    };
    menu->addPlainAction(QIcon::fromTheme("loop"),
                         tr("Update Layer from Source PSD"), updateOp);

    const PropertyMenu::PlainSelectedOp<PsdImageBox> syncOp =
    [](PsdImageBox * box) {
        try {
            box->syncAllFromSource();
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
    };
    menu->addPlainAction(QIcon::fromTheme("linked"),
                         tr("Sync All Layers from Source PSD"), syncOp);

    menu->addSeparator();

    ImageBox::setupCanvasMenu(menu);
}

bool PsdImageBox::updateFromSource()
{
    if (mSourcePackage.isEmpty() || mSourceLayerKey.isEmpty()) {
        qWarning() << "PSD update: box is not bound to a source package";
        return false;
    }

    auto entries = Fpsd::readPackage(mSourcePackage);
    Fpsd::Meta meta;
    if (!Fpsd::metaFromJson(entries.value(QStringLiteral("meta.json")),
                            &meta)) {
        qWarning() << "PSD update: cannot read package meta"
                   << mSourcePackage;
        return false;
    }
    if (meta.sourcePsd.isEmpty() || !QFile::exists(meta.sourcePsd)) {
        qWarning() << "PSD update: source psd missing" << meta.sourcePsd;
        return false;
    }

    psd::PsdFile psd;
    QString error;
    if (!psd.load(meta.sourcePsd, &error)) {
        qWarning() << "PSD update: failed to parse source" << error;
        return false;
    }

    const int result = ImportPSD::PsdSync::updateLayerPixels(
                psd, meta, entries, this);
    if (result < 0) {
        const QString name = prp_getName();
        if (!name.startsWith(missingPrefix())) {
            prp_setName(missingPrefix() + name);
        }
        qWarning() << "PSD update: layer" << mSourceLayerKey
                   << "no longer exists in the source psd";
        QMessageBox::warning(nullptr, tr("PSD Update"),
                tr("Layer no longer exists in the source PSD.\n"
                   "It is kept (marked as missing) with all animation data."));
        return false;
    }
    // layer found again: restore the original name
    const QString name = prp_getName();
    if (name.startsWith(missingPrefix())) {
        prp_setName(name.mid(missingPrefix().size()));
    }

    entries.insert(QStringLiteral("meta.json"), Fpsd::metaToJson(meta));
    if (!Fpsd::writePackage(mSourcePackage, entries)) {
        qWarning() << "PSD update: failed to write package" << mSourcePackage;
        return false;
    }

    if (result > 0) {
        const QByteArray png = entries.value(
                    Fpsd::layerEntryName(mSourceLayerKey));
        const QString cachePath = Fpsd::writeLayerCacheFile(
                    mSourcePackage, mSourceLayerKey, png);
        if (!cachePath.isEmpty()) {
            // point to the fresh cache file: the new path forces a clean
            // handler assign + load, safer than reloading in place
            setFilePathNoRename(cachePath);
            // clear the render-data cache and schedule a canvas redraw,
            // otherwise the stale rendered image stays on screen
            planUpdate(UpdateReason::userChange);
        } else {
            qWarning() << "PSD update: failed to refresh cached pixels";
        }
        QMessageBox::information(nullptr, tr("PSD Update"),
                                  tr("Layer updated from source PSD."));
    } else {
        QMessageBox::information(nullptr, tr("PSD Update"),
                                  tr("Layer pixels unchanged."));
    }
    return true;
}

void PsdImageBox::syncAllFromSource()
{
    if (mSourcePackage.isEmpty()) { return; }

    auto entries = Fpsd::readPackage(mSourcePackage);
    Fpsd::Meta meta;
    if (!Fpsd::metaFromJson(entries.value(QStringLiteral("meta.json")),
                            &meta)) {
        qWarning() << "PSD sync: cannot read package meta" << mSourcePackage;
        return;
    }
    if (meta.sourcePsd.isEmpty() || !QFile::exists(meta.sourcePsd)) {
        QMessageBox::warning(nullptr, tr("PSD Sync"),
                              tr("Source PSD is missing:\n%1")
                              .arg(meta.sourcePsd));
        return;
    }

    psd::PsdFile psd;
    QString error;
    if (!psd.load(meta.sourcePsd, &error)) {
        QMessageBox::warning(nullptr, tr("PSD Sync"),
                              tr("Failed to parse source PSD:\n%1")
                              .arg(error));
        return;
    }

    // collect every box bound to this package in the current scene
    QList<PsdImageBox*> boxes;
    const auto scene = getParentScene();
    if (scene) { collectPsdImageBoxes(scene, mSourcePackage, boxes); }
    if (!boxes.contains(this)) { boxes.append(this); }

    // update existing layers
    int updatedCount = 0;
    int missingCount = 0;
    QSet<QString> seenKeys;
    QList<PsdImageBox*> changedBoxes;
    for (auto *box : boxes) {
        seenKeys.insert(box->sourceLayerKey());
        const int result = ImportPSD::PsdSync::updateLayerPixels(
                    psd, meta, entries, box);
        if (result > 0) {
            updatedCount++;
            changedBoxes << box;
        } else if (result < 0) {
            missingCount++;
            const QString name = box->prp_getName();
            if (!name.startsWith(missingPrefix())) {
                box->prp_setName(missingPrefix() + name);
            }
        } else {
            // unchanged; restore name if it was marked missing before
            const QString name = box->prp_getName();
            if (name.startsWith(missingPrefix())) {
                box->prp_setName(name.mid(missingPrefix().size()));
            }
        }
    }

    // layers present in the psd but not bound in the scene nor known
    // to the package
    QList<const psd::LayerRecord*> newRecords;
    if (!meta.composite) {
        for (const auto &rec : psd.layers()) {
            if (rec.divider != psd::Divider::None) { continue; }
            if (rec.rect.isEmpty()) { continue; }
            const QString key = ImportPSD::PsdSync::layerKeyForRecord(rec);
            if (seenKeys.contains(key)) { continue; }
            const auto known = std::any_of(meta.layers.begin(), meta.layers.end(),
                    [&key](const Fpsd::LayerMeta &l) { return l.key == key; });
            if (!known) { newRecords << &rec; }
        }
    }

    // new layers: extract pixels + meta entries right away, ask the
    // user which ones should be added to the scene
    int addedCount = 0;
    if (!newRecords.isEmpty()) {
        QStringList names;
        for (const auto rec : newRecords) { names << rec->name; }
        const QList<int> selected = askNewLayers(names);
        for (const int i : selected) {
            const auto rec = newRecords.at(i);
            const QString key = ImportPSD::PsdSync::layerKeyForRecord(*rec);
            const QByteArray rgba = psd.extractLayerRGBA(*rec, &error);
            const QByteArray png = Fpsd::rgbaToPng(
                        rgba, rec->rect.width(), rec->rect.height());
            const QString cachePath = Fpsd::writeLayerCacheFile(
                        mSourcePackage, key, png);
            if (png.isEmpty() || cachePath.isEmpty()) {
                qWarning() << "PSD sync: failed to cache new layer"
                           << rec->name;
                continue;
            }
            entries.insert(Fpsd::layerEntryName(key), png);

            Fpsd::LayerMeta lm;
            lm.key = key;
            lm.layerId = rec->layerId;
            lm.name = rec->name;
            lm.x = rec->rect.left();
            lm.y = rec->rect.top();
            lm.w = rec->rect.width();
            lm.h = rec->rect.height();
            lm.hash = Fpsd::pixelHash(rgba);
            lm.opacity = rec->opacity;
            lm.visible = rec->visible;
            lm.blendKey = rec->blendKey;
            meta.layers.append(lm);

            const auto box = ImportPSD::PsdSync::createLayerBox(
                        mSourcePackage, cachePath, lm);
            // add next to the other layers of this package
            const auto parent = getParentGroup() ? getParentGroup()
                                                 : static_cast<ContainerBox*>(scene);
            if (parent && box) {
                parent->addContained(box);
                box->prp_setName(rec->name); // restore unicode name
                box->planUpdate(UpdateReason::userChange);
                addedCount++;
            }
        }
    }

    // persist the package (updated pixels + meta)
    entries.insert(QStringLiteral("meta.json"), Fpsd::metaToJson(meta));
    if (!Fpsd::writePackage(mSourcePackage, entries)) {
        QMessageBox::warning(nullptr, tr("PSD Sync"),
                              tr("Failed to write package:\n%1")
                              .arg(mSourcePackage));
        return;
    }

    // refresh the pixel cache + textures of the changed layers
    for (auto *box : changedBoxes) {
        const QByteArray png = entries.value(
                    Fpsd::layerEntryName(box->sourceLayerKey()));
        const QString cachePath = Fpsd::writeLayerCacheFile(
                    mSourcePackage, box->sourceLayerKey(), png);
        if (!cachePath.isEmpty()) {
            box->setFilePathNoRename(cachePath);
            // clear stale render data so the new pixels reach the canvas
            box->planUpdate(UpdateReason::userChange);
        }
    }
    if (Document::sInstance) { Document::sInstance->actionFinished(); }

    if (updatedCount == 0 && missingCount == 0 && addedCount == 0) {
        QMessageBox::information(nullptr, tr("PSD Sync"),
                                  tr("All layers are up to date."));
    } else {
        QMessageBox::information(nullptr, tr("PSD Sync"),
                tr("Updated layers: %1\nMissing layers: %2\nAdded layers: %3")
                .arg(updatedCount).arg(missingCount).arg(addedCount));
    }
}
