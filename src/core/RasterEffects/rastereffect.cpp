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

#include "rastereffect.h"
#include "Animators/dynamiccomplexanimator.h"
#include "typemenu.h"
#include "rastereffectcollection.h"
#include "ReadWrite/ewritestream.h"
#include "ReadWrite/ereadstream.h"
#include "undoredo.h"
#include "exceptions.h"
#include "Private/document.h"
#include <QBuffer>

namespace {
QByteArray serializePropertyBlock(const Property* const prop) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    eWriteStream writeStream(&buffer);
    prop->prp_writeProperty(writeStream);
    buffer.close();
    return data;
}

void deserializePropertyBlock(Property* const prop, const QByteArray& data) {
    QByteArray buf = data;
    QBuffer buffer(&buf);
    buffer.open(QIODevice::ReadOnly);
    eReadStream readStream(&buffer);
    prop->prp_readProperty(readStream);
    buffer.close();
}
}

// AE-style per-effect reset: rebuild a factory-default instance of
// the same effect type and copy its parameter values over; undoable
// via before/after snapshots of every child property
void RasterEffect::resetToDefault() {
    QByteArray idData;
    {
        QBuffer buffer(&idData);
        buffer.open(QIODevice::WriteOnly);
        eWriteStream writeStream(&buffer);
        writeIdentifier(writeStream);
        buffer.close();
    }
    qsptr<RasterEffect> fresh;
    {
        QByteArray buf = idData;
        QBuffer buffer(&buf);
        buffer.open(QIODevice::ReadOnly);
        eReadStream readStream(&buffer);
        try { fresh = readIdCreateRasterEffect(readStream); }
        catch (const std::exception& e) { gPrintExceptionCritical(e); }
    }
    if (!fresh) { return; }
    const auto oldChildren = ca_getChildren();
    const auto newChildren = fresh->ca_getChildren();
    if (oldChildren.count() != newChildren.count() ||
        oldChildren.isEmpty()) { return; }

    QList<QPair<QPointer<Property>, QByteArray>> before;
    for (const auto& child : oldChildren) {
        before.append(qMakePair(QPointer<Property>(child.get()),
                                serializePropertyBlock(child.get())));
    }
    for (int i = 0; i < oldChildren.count(); i++) {
        const auto srcBlock = serializePropertyBlock(newChildren.at(i).get());
        deserializePropertyBlock(oldChildren.at(i).get(), srcBlock);
    }
    QList<QPair<QPointer<Property>, QByteArray>> after;
    for (const auto& child : oldChildren) {
        after.append(qMakePair(QPointer<Property>(child.get()),
                               serializePropertyBlock(child.get())));
    }

    const QPointer<RasterEffect> effQ(this);
    const auto restore = [effQ](const QList<QPair<QPointer<Property>,
                                               QByteArray>>& blocks) {
        for (const auto& block : blocks) {
            if (block.first) { deserializePropertyBlock(block.first, block.second); }
        }
        if (effQ) { effQ->prp_afterWholeInfluenceRangeChanged(); }
        if (Document::sInstance) { Document::sInstance->actionFinished(); }
    };

    prp_pushUndoRedoName(QObject::tr("重置特效"));
    {
        UndoRedo ur;
        ur.fUndo = [restore, before]() { restore(before); };
        ur.fRedo = [restore, after]() { restore(after); };
        prp_addUndoRedo(ur);
    }
    prp_afterWholeInfluenceRangeChanged();
    if (Document::sInstance) { Document::sInstance->actionFinished(); }
}

RasterEffect::RasterEffect(const QString &name,
                           const HardwareSupport hwSupport,
                           const bool hwInterchangeable,
                           const RasterEffectType type) :
    eEffect(name), mType(type),
    mTypeHwSupport(hwSupport),
    mHwInterchangeable(hwInterchangeable) {
    if(hwInterchangeable ||
       hwSupport == HardwareSupport::cpuOnly ||
       hwSupport == HardwareSupport::gpuOnly) {
        mInstHwSupport = hwSupport;
    } else if(hwSupport == HardwareSupport::cpuPreffered) {
        mInstHwSupport = HardwareSupport::cpuOnly;
    } else if(hwSupport == HardwareSupport::gpuPreffered) {
        mInstHwSupport = HardwareSupport::gpuOnly;
    } else Q_ASSERT(false);
}

void RasterEffect::writeIdentifier(eWriteStream &dst) const {
    dst.write(&mType, sizeof(RasterEffectType));
}

void RasterEffect::writeIdentifierXEV(QDomElement& ele) const {
    ele.setAttribute("type", static_cast<int>(mType));
}

void RasterEffect::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    eEffect::prp_setupTreeViewMenu(menu);
    const PropertyMenu::PlainSelectedOp<RasterEffect> rOp =
    [](RasterEffect* const eff) {
        eff->resetToDefault();
    };
    menu->addPlainAction(QIcon::fromTheme("reload"), tr("重置特效"), rOp);
    const PropertyMenu::PlainSelectedOp<RasterEffect> dOp =
    [](RasterEffect* const eff) {
        const auto parent = eff->getParent<DynamicComplexAnimatorBase<RasterEffect>>();
        parent->removeChild(eff->ref<RasterEffect>());
    };
    menu->addPlainAction(QIcon::fromTheme("trash"), tr("Delete Effect(s)"), dOp);
}

QMimeData *RasterEffect::SWT_createMimeData() {
    return new eMimeData(QList<RasterEffect*>() << this);
}

void RasterEffect::switchInstanceHwSupport() {
    if(mTypeHwSupport == HardwareSupport::cpuOnly) return;
    if(mTypeHwSupport == HardwareSupport::gpuOnly) return;
    if(mInstHwSupport == HardwareSupport::cpuOnly) {
        if(mHwInterchangeable) mInstHwSupport = mTypeHwSupport;
        else mInstHwSupport = HardwareSupport::gpuOnly;
    } else if(mInstHwSupport == HardwareSupport::gpuOnly) {
        mInstHwSupport = HardwareSupport::cpuOnly;
    } else mInstHwSupport = HardwareSupport::gpuOnly;
    if(!mHwInterchangeable) prp_afterWholeInfluenceRangeChanged();
    emit hardwareSupportChanged();
}
