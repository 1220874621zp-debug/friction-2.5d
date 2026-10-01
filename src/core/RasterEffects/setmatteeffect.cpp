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

#include "setmatteeffect.h"

#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "Properties/comboboxproperty.h"
#include "trackmattecaller.h"

// boxes whose set-matte sampling is currently on the call stack:
// queExternalRender runs the target's setup SYNCHRONOUSLY, so a
// set-matte cycle (A mattes B, B mattes A) would recurse forever -
// any revisit of a chained box degrades to "no matte" instead
namespace {
thread_local QVector<const BoundingBox*> sSampleChain;
}

SetMatteEffect::SetMatteEffect() :
    RasterEffect(QStringLiteral("设置遮罩 (Set Matte)"),
                 HardwareSupport::cpuOnly,
                 false,
                 RasterEffectType::SET_MATTE)
{
    // NOTE: Chinese names must use QStringLiteral (u16 literal, no
    // execution-charset conversion) -- plain tr() mangles them
    mMatteTarget = enve::make_shared<BoxTargetProperty>(
                QStringLiteral("遮罩图层")); // matte layer
    mMatteTarget->setComboPicker(true);
    connect(mMatteTarget.get(), &BoxTargetProperty::targetSet,
            this, [this](BoundingBox* const box) {
        // live follow: the matte layer moving/animating/changing shape
        // must invalidate the HOST's render cache (mirrors
        // BoundingBox::setTrackMatteSource)
        auto& conn = mFollowConn.assign(box);
        if(box) {
            conn << connect(box, &BoundingBox::prp_absFrameRangeChanged,
                            this, [this](const FrameRange&, const bool) {
                prp_afterWholeInfluenceRangeChanged();
            });
        }
        prp_afterWholeInfluenceRangeChanged();
    });
    ca_addChild(mMatteTarget);

    mMode = enve::make_shared<ComboBoxProperty>(
                QStringLiteral("遮罩通道"), QStringList() // matte channel
                << QStringLiteral("Alpha")
                << QStringLiteral("Alpha 反相")      // inverted
                << QStringLiteral("亮度 (Luma)")
                << QStringLiteral("亮度反相 (Luma Inv)"));
    ca_addChild(mMode);
}

stdsptr<RasterEffectCaller> SetMatteEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(influence)
    if(!data) return nullptr;
    const auto target = mMatteTarget ? mMatteTarget->getTarget() : nullptr;
    if(!target) return nullptr; // no matte layer picked: AE no-op
    const auto parentBox = data->fParentBox.data();
    // self, the own subtree and own ancestors would recurse through
    // the group's synchronous child setup - degrade to a no-op
    if(!parentBox || target == parentBox) return nullptr;
    if(parentBox->isAncestor(target)) return nullptr;
    if(target->isAncestor(parentBox)) return nullptr;
    if(sSampleChain.contains(target)) return nullptr;

    static const TrackMatteCaller::Mode sModes[4] = {
        TrackMatteCaller::Mode::alpha,
        TrackMatteCaller::Mode::alphaInv,
        TrackMatteCaller::Mode::luma,
        TrackMatteCaller::Mode::lumaInv
    };
    const int modeIdx = qBound(0, mMode ? mMode->getCurrentValue() : 0, 3);
    const auto mode = sModes[modeIdx];

    // queue the matte layer for an independent render; the dependency
    // delays this box's effects phase until the sample finishes (the
    // track-matte queExternalRender pattern)
    sSampleChain.append(parentBox);
    const auto guard = qScopeGuard([]() { sSampleChain.removeLast(); });
    // relFrame is the HOST's relative frame; the matte layer has its
    // own trim/start - convert through absolute scene frames or the
    // matte samples the wrong moment (a trimmed matte layer read an
    // out-of-range frame and hid the host entirely)
    const qreal absFrame = parentBox->prp_relFrameToAbsFrameF(relFrame);
    const qreal tRel = target->prp_absFrameToRelFrameF(absFrame);
    const auto sample = target->queExternalRender(tRel, true);
    if(sample) sample->addDependent(data);
    // a null sample reaches the caller as an empty matte: AE hides the
    // layer entirely (inverted modes show everything)
    return enve::make_shared<TrackMatteCaller>(sample, mode);
}

FrameRange SetMatteEffect::prp_getIdenticalRelRange(
        const int relFrame) const {
    const auto thisIdent = ComplexAnimator::prp_getIdenticalRelRange(relFrame);
    const auto target = mMatteTarget ? mMatteTarget->getTarget() : nullptr;
    if(!target) return thisIdent;
    // the host's rendered pixels depend on the matte layer's content:
    // a static host under an animated matte is NOT frame-identical
    // (same pattern as TargetTransformEffect); the chain guard breaks
    // set-matte cycles (A mattes B, B mattes A)
    static thread_local QVector<const BoundingBox*> sIdentChain;
    if(sIdentChain.contains(target)) return thisIdent;
    sIdentChain.append(target);
    const auto guard = qScopeGuard([]() { sIdentChain.removeLast(); });
    const int absFrame = prp_relFrameToAbsFrame(relFrame);
    const int tRelFrame = target->prp_absFrameToRelFrame(absFrame);
    const auto targetIdent = target->prp_getIdenticalRelRange(tRelFrame);
    const auto absTargetIdent = target->prp_relRangeToAbsRange(targetIdent);
    return thisIdent*prp_absRangeToRelRange(absTargetIdent);
}
