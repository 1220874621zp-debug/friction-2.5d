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

#include "eindependentsound.h"

#include <QInputDialog>

#include "ReadWrite/evformat.h"
#include "typemenu.h"
#include "Timeline/fixedlenanimationrect.h"
#include "fileshandler.h"
#include "Animators/qrealanimator.h"
#include "Boxes/nullobject.h"
#include "canvas.h"

SoundFileHandler* soundFileHandlerGetter(const QString& path)
{
    return FilesHandler::sInstance->getFileHandler<SoundFileHandler>(path);
}

qsptr<FixedLenAnimationRect> createIndependentSoundDur(eIndependentSound* const sound)
{
    const auto result = enve::make_shared<FixedLenAnimationRect>(*sound, true);
    return result;
}

eIndependentSound::eIndependentSound()
    : eSoundObjectBase(createIndependentSoundDur(this))
    , mFileHandler(this,
                   [](const QString& path) {
                       return soundFileHandlerGetter(path);
                   },
                   [this](SoundFileHandler* obj) {
                       fileHandlerAfterAssigned(obj);
                   },
                   [this](ConnContext& conn, SoundFileHandler* obj) {
                       fileHandlerConnector(conn, obj);
                   })
{}

void eIndependentSound::fileHandlerConnector(ConnContext &conn,
                                             SoundFileHandler *obj)
{
    conn << connect(obj, &SoundFileHandler::pathChanged,
                    this, &eSoundObjectBase::prp_afterWholeInfluenceRangeChanged);
    conn << connect(obj, &SoundFileHandler::reloaded,
                    this, &eSoundObjectBase::prp_afterWholeInfluenceRangeChanged);
}

void eIndependentSound::fileHandlerAfterAssigned(SoundFileHandler *obj)
{
    if(obj) {
        const auto newDataHandler = FileDataCacheHandler::sGetDataHandler<SoundDataHandler>(obj->path());
        setSoundDataHandler(newDataHandler);
    } else {
        setSoundDataHandler(nullptr);
    }
}


void eIndependentSound::prp_setupTreeViewMenu(PropertyMenu * const menu)
{
    if (menu->hasActionsForType<eIndependentSound>()) { return; }
    menu->addedActionsForType<eIndependentSound>();
    eSoundObjectBase::prp_setupTreeViewMenu(menu);

    const PropertyMenu::PlainTriggeredOp stretchOp = [this]() {
        bool ok = false;
        const int stretch = QInputDialog::getInt(nullptr,
                                                 tr("Stretch"),
                                                 tr("Stretch"),
                                                 qRound(getStretch() * 100),
                                                 -1000,
                                                 1000,
                                                 1,
                                                 &ok);
        if (!ok) { return; }
        setStretch(stretch * 0.01);
    };
    menu->addPlainAction(QIcon::fromTheme("width"),
                         tr("Stretch"),
                         stretchOp);

    const PropertyMenu::PlainTriggeredOp audioKeysOp = [this]() {
        convertAudioToKeyframesAction();
    };
    menu->addPlainAction(QIcon::fromTheme("audio-volume-high"),
                         tr("音频转关键帧"),
                         audioKeysOp);

    const PropertyMenu::PlainTriggeredOp deleteOp = [this]() {
        removeFromParent_k();
    };
    menu->addPlainAction(QIcon::fromTheme("trash"),
                         tr("Delete"),
                         deleteOp);
}

void eIndependentSound::convertAudioToKeyframesAction()
{
    const auto scene = getParentScene();
    if (!scene) { return; }
    const qreal fps = getCanvasFPS();
    if (fps <= 0.) { return; }
    const auto dur = getDurationRectangle();
    if (!dur) { return; }
    const int nFrames = dur->getFrameDuration();
    if (nFrames <= 0) { return; }
    const int startAbsFrame = dur->getMinAbsFrame();

    // per-frame mixed-channel peak (0..1) across the sound's duration;
    // stretched sounds sample every rel second overlapping each frame
    QVector<qreal> peaks(nFrames, 0.);
    for (int frame = 0; frame < nFrames; frame++) {
        const qreal absSec = qreal(frame)/fps;
        const int absSecond = int(absSec);
        const auto relSecs = absSecondToRelSeconds(absSecond);
        if (!relSecs.isValid()) { continue; }
        const qreal fracInSecond = absSec - absSecond;
        for (int relSec = relSecs.fMin; relSec <= relSecs.fMax; relSec++) {
            const auto samples = getSamplesForSecond(relSec);
            if (!samples) { continue; }
            if (samples->fSampleSize != 4) { continue; } // float only
            const int sr = samples->fSampleRate;
            if (sr <= 0) { continue; }
            const int nSamples = int(samples->fSampleRange.span());
            const int i0 = qBound(0, qFloor(fracInSecond*sr), nSamples);
            const int i1 = qBound(i0, qFloor((fracInSecond + 1./fps)*sr), nSamples);
            for (int i = i0; i < i1; i++) {
                for (uint ch = 0; ch < samples->fNChannels; ch++) {
                    float v;
                    if (samples->fPlanar) {
                        v = reinterpret_cast<const float*>(
                                    samples->fData[ch])[i];
                    } else {
                        v = reinterpret_cast<const float*>(
                                    samples->fData[0])[i*samples->fNChannels + ch];
                    }
                    const qreal a = qAbs(qreal(v));
                    if (a > peaks[frame]) { peaks[frame] = a; }
                }
            }
        }
    }

    // deliver the curve on a null layer ("sound name 振幅") so any
    // property/expression can bind to it by name
    const auto amp = enve::make_shared<NullObject>();
    amp->prp_setName(prp_getName() + QStringLiteral(" 振幅"));
    const auto anim = enve::make_shared<QrealAnimator>(
                0., 0., 1., 0.01, tr("振幅"));
    amp->ca_addChild(anim);
    scene->getCurrentGroup()->addContained(amp);
    for (int frame = 0; frame < nFrames; frame++) {
        anim->saveValueToKey(startAbsFrame + frame, peaks[frame]);
    }
    anim->prp_afterWholeInfluenceRangeChanged();
}

bool eIndependentSound::SWT_shouldBeVisible(const SWT_RulesCollection &rules,
                                            const bool parentSatisfies,
                                            const bool parentMainTarget) const
{
    Q_UNUSED(parentMainTarget);
    if (rules.fRule == SWT_BoxRule::visible && !isVisible()) { return false; }
    if (rules.fRule == SWT_BoxRule::selected && !isSelected()) { return false; }
    if (rules.fType == SWT_Type::sound) { return true; }
    if (rules.fType == SWT_Type::graphics) { return false; }
    return parentSatisfies;
}

void eIndependentSound::setFilePathNoRename(const QString &path)
{
    mFileHandler.assign(path);
}

void eIndependentSound::setFilePath(const QString &path)
{
    setFilePathNoRename(path);
    rename(QFileInfo(path).completeBaseName());
}

void eIndependentSound::updateDurationRectLength()
{
    if (cacheHandler() && getParentScene()) {
        const qreal secs = durationSeconds();
        const qreal fps = getCanvasFPS();
        const int frames = qCeil(qAbs(secs * fps * getStretch()));
        const auto flaRect = static_cast<FixedLenAnimationRect*>(getDurationRectangle());
        flaRect->setAnimationFrameDuration(frames);
    }
}

void eIndependentSound::prp_writeProperty_impl(eWriteStream& dst) const
{
    eBoxOrSound::prp_writeProperty_impl(dst);
    dst.writeFilePath(mFileHandler.path());
    dst << getStretch();
}

void eIndependentSound::prp_readProperty_impl(eReadStream& src)
{
    eBoxOrSound::prp_readProperty_impl(src);
    const QString filePath = src.readFilePath();
    if (!filePath.isEmpty()) { setFilePathNoRename(filePath); }
    if (src.evFileVersion() >= EvFormat::avStretch) {
        qreal stretch;
        src >> stretch;
        setStretch(stretch);
    }
}

QDomElement eIndependentSound::prp_writePropertyXEV_impl(const XevExporter& exp) const
{
    auto result = eBoxOrSound::prp_writePropertyXEV_impl(exp);
    const QString& absSrc = mFileHandler.path();
    XevExportHelpers::setAbsAndRelFileSrc(absSrc, result, exp);
    return result;
}

void eIndependentSound::prp_readPropertyXEV_impl(const QDomElement& ele,
                                                 const XevImporter& imp)
{
    eBoxOrSound::prp_readPropertyXEV_impl(ele, imp);
    const QString absSrc = XevExportHelpers::getAbsAndRelFileSrc(ele, imp);
    if (!absSrc.isEmpty()) { setFilePathNoRename(absSrc); }
}
