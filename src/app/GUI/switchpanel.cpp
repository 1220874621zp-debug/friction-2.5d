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

#include "switchpanel.h"

#include <QApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <functional>

#include "Private/document.h"
#include "canvas.h"
#include "Boxes/containerbox.h"
#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "Animators/boolanimator.h"
#include "Animators/qrealkey.h"

// one undo entry restoring exactly what a switch commit touched:
// with auto-key: the visibility key at the commit frame (-1 = none)
// without auto-key: the base value (-2 marker, oldBase carries it)
namespace {
struct SwitchKeyRec {
    QPointer<BoolAnimator> va;
    int oldKey;
    int oldBase;
    int newVal;
};
}

// ------------------------------ SwitchPreview ------------------------------

SwitchPreview::SwitchPreview(QWidget* const parent)
    : QWidget(parent)
{
    setMinimumSize(140, 140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SwitchPreview::setImage(const QImage& img, const QString& name)
{
    mImage = img;
    mName = name;
    mPlaceholder.clear();
    update();
}

void SwitchPreview::setPlaceholder(const QString& text)
{
    mPlaceholder = text;
    mImage = QImage();
    mName.clear();
    update();
}

void SwitchPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // square, rounded, black frame
    const int side = qMin(width(), height());
    QRectF box((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const qreal radius = 10.0;
    const int inset = 6;
    p.setPen(QPen(QColor(58, 58, 62), 1.0));
    p.setBrush(QColor(8, 8, 10));
    p.drawRoundedRect(box, radius, radius);

    if(!mImage.isNull()) {
        const QRectF inner = box.adjusted(inset, inset, -inset,
                                          -inset - 20);
        p.save();
        p.setClipPath([&]() {
            QPainterPath clip;
            clip.addRoundedRect(inner, radius * 0.7, radius * 0.7);
            return clip;
        }());
        const auto scaled = mImage.scaled(
                    qMax(1, qRound(inner.width())),
                    qMax(1, qRound(inner.height())),
                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage(QPointF(inner.center().x() - scaled.width() / 2.0,
                            inner.center().y() - scaled.height() / 2.0),
                    scaled);
        p.restore();
    } else {
        p.setPen(QColor(130, 130, 134));
        p.drawText(box.adjusted(12, 12, -12, -24),
                   Qt::AlignCenter | Qt::TextWordWrap,
                   mPlaceholder.isEmpty() ? tr("无预览") : mPlaceholder);
    }

    // layer name strip along the bottom of the frame
    if(!mName.isEmpty()) {
        const QRectF strip = box.adjusted(inset, 0, -inset, 0);
        const qreal stripTop = box.bottom() - 6 - 18;
        QPainterPath stripPath;
        stripPath.addRoundedRect(QRectF(strip.left(), stripTop,
                                        strip.width(), 18),
                                 6, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 168));
        p.drawPath(stripPath);
        const QFontMetrics fm(font());
        const QString name = fm.elidedText(mName, Qt::ElideRight,
                                           qRound(strip.width()) - 12);
        p.setPen(QColor(235, 235, 238));
        p.drawText(QRectF(strip.left() + 6, stripTop,
                          strip.width() - 12, 18),
                   Qt::AlignVCenter | Qt::AlignLeft, name);
    }
}

// ------------------------------ SwitchRuler ------------------------------

SwitchRuler::SwitchRuler(QWidget* const parent)
    : QWidget(parent)
{
    setMinimumHeight(52);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

// fixed horizontal padding of the slider groove
static qreal rulerMargin() { return 10.0; }

void SwitchRuler::setItems(const QStringList& names)
{
    mNames = names;
    if(mActive >= mNames.count()) mActive = mNames.count() - 1;
    update();
}
void SwitchRuler::setActiveIndex(const int index)
{
    if(mActive == index) return;
    mActive = index;
    update();
}

qreal SwitchRuler::tickX(const int index) const
{
    const int n = mNames.count();
    if(n < 1) return 0;
    const qreal x0 = rulerMargin();
    const qreal x1 = width() - rulerMargin();
    const qreal spacing = (x1 - x0) / n;
    return x0 + spacing * (index + 0.5);
}

int SwitchRuler::indexAt(const int x) const
{
    const int n = mNames.count();
    if(n < 1) return -1;
    const qreal x0 = rulerMargin();
    const qreal x1 = width() - rulerMargin();
    const qreal spacing = (x1 - x0) / n;
    const int index = qRound((x - x0) / spacing - 0.5);
    if(index < 0) return 0;
    if(index >= n) return n - 1;
    return index;
}

void SwitchRuler::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int n = mNames.count();
    const int grooveY = height() - 22;
    const QRectF groove(rulerMargin(), grooveY,
                       width() - 2 * rulerMargin(), 6);

    p.fillRect(rect(), QColor(37, 37, 40));

    if(n < 1) {
        p.setPen(QColor(120, 120, 124));
        p.drawText(rect(), Qt::AlignCenter, tr("切换组没有子图层"));
        return;
    }

    // groove
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(62, 62, 67));
    p.drawRoundedRect(groove, 3, 3);

    const int shown = mDrag >= 0 ? mDrag : mActive;

    // one tick per child; the active/drag tick is highlighted
    for(int i = 0; i < n; i++) {
        const bool isActive = (i == shown);
        p.setPen(isActive ? QColor(72, 145, 220) : QColor(105, 105, 110));
        p.drawLine(QLineF(tickX(i), grooveY - 9, tickX(i), grooveY - 2));
    }

    // the handle: a rounded block snapped onto the current tick
    if(shown >= 0 && shown < n) {
        const qreal spacing = (width() - 2 * rulerMargin()) / n;
        const qreal hw = qMin(spacing * 0.6, 34.0);
        const QRectF handle(tickX(shown) - hw / 2, grooveY - 11,
                            hw, 18);
        p.setPen(QPen(QColor(26, 26, 28), 1));
        p.setBrush(mDrag >= 0 ? QColor(47, 158, 68) :
                                QColor(72, 145, 220));
        p.drawRoundedRect(handle, 4, 4);
        // grip lines
        p.setPen(QColor(26, 26, 28));
        for(int g = -2; g <= 2; g++) {
            const qreal gx = handle.center().x() + g * 3;
            p.drawLine(QLineF(gx, handle.top() + 4,
                              gx, handle.bottom() - 4));
        }
    }
}

void SwitchRuler::mousePressEvent(QMouseEvent* const e)
{
    if(mNames.isEmpty()) return;
    if(e->button() != Qt::LeftButton) return;
    const int index = indexAt(e->pos().x());
    if(index < 0) return;
    mDrag = index;
    emit interactionStarted();
    emit livePreview(index);
    update();
}

void SwitchRuler::mouseMoveEvent(QMouseEvent* const e)
{
    if(mDrag < 0) {
        const int hover = indexAt(e->pos().x());
        if(hover != mHover) {
            mHover = hover;
            setToolTip(hover >= 0 && hover < mNames.count() ?
                           mNames.at(hover) : QString());
        }
        return;
    }
    // discrete: the handle jumps tick to tick, never in between
    const int index = indexAt(e->pos().x());
    if(index >= 0 && index != mDrag) {
        mDrag = index;
        emit livePreview(index);
        update();
    }
}

void SwitchRuler::mouseReleaseEvent(QMouseEvent* const e)
{
    if(mDrag < 0) return;
    if(e->button() != Qt::LeftButton) return;
    const int index = mDrag;
    mDrag = -1;
    update();
    emit committed(index);
}

void SwitchRuler::leaveEvent(QEvent*)
{
    mHover = -1;
    setToolTip(QString());
}

void SwitchRuler::wheelEvent(QWheelEvent* const e)
{
    if(mNames.isEmpty() || mActive < 0) return;
    int next = mActive + (e->angleDelta().y() > 0 ? 1 : -1);
    if(next < 0) next = 0;
    if(next >= mNames.count()) next = mNames.count() - 1;
    if(next == mActive) return;
    emit interactionStarted();
    emit committed(next);
    setActiveIndex(next);
}

// ------------------------------ SwitchPanel ------------------------------

SwitchPanel::SwitchPanel(Document& doc, QWidget* const parent)
    : QWidget(parent)
    , mDocument(doc)
{
    const auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    const auto topRow = new QHBoxLayout();
    const auto comboLabel = new QLabel(tr("切换组"), this);
    comboLabel->setStyleSheet(QStringLiteral(
                "font-size:13px; font-weight:bold; color:#ffffff;"));
    mGroupCombo = new QComboBox(this);
    mGroupCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    topRow->addWidget(comboLabel);
    topRow->addWidget(mGroupCombo);
    lay->addLayout(topRow);

    mPreview = new SwitchPreview(this);
    lay->addWidget(mPreview, 1);

    mRuler = new SwitchRuler(this);
    lay->addWidget(mRuler);

    mAutoKey = new QCheckBox(tr("自动打关键帧"), this);
    mAutoKey->setChecked(true);
    mAutoKey->setStyleSheet(QStringLiteral("font-size:12px; color:#ffffff;"));
    lay->addWidget(mAutoKey);

    mSelectionDebounce = new QTimer(this);
    mSelectionDebounce->setSingleShot(true);
    mSelectionDebounce->setInterval(80);
    connect(mSelectionDebounce, &QTimer::timeout,
            this, &SwitchPanel::checkSelection);

    mChildrenDebounce = new QTimer(this);
    mChildrenDebounce->setSingleShot(true);
    mChildrenDebounce->setInterval(60);
    connect(mChildrenDebounce, &QTimer::timeout,
            this, &SwitchPanel::refreshChildren);

    mRescanDebounce = new QTimer(this);
    mRescanDebounce->setSingleShot(true);
    mRescanDebounce->setInterval(300);
    connect(mRescanDebounce, &QTimer::timeout,
            this, &SwitchPanel::rescanGroups);

    connect(mGroupCombo, qOverload<int>(&QComboBox::activated), this,
            [this](const int index) {
        if(mComboGuard || index < 0) return;
        if(index < 0 || index >= mComboGroups.count()) return;
        const auto group = mComboGroups.at(index);
        if(group) bindGroup(group.data());
    });

    connect(mRuler, &SwitchRuler::interactionStarted, this, [this]() {
        mInteracting = true;
    });
    connect(mRuler, &SwitchRuler::livePreview, this, [this](const int index) {
        const auto box = childAt(index);
        if(box) requestPreview(box);
    });
    connect(mRuler, &SwitchRuler::committed, this, [this](const int index) {
        commitSwitch(index);
        mInteracting = false;
        updateActive();
    });

    // document-level wiring stays connected for the panel's lifetime
    // (cheap, no scene walking); scene/group connections are gated by
    // the dock visibility through setListeningEnabled
    connect(&mDocument, &Document::activeSceneSet, this,
            [this](Canvas* const scene) {
        if(mListening) setCurrentScene(scene);
    });
    connect(&mDocument, &Document::documentChanged, this, [this]() {
        // coarse fallback: conversions, undos, remote edits
        if(mListening) scheduleGroupRescan();
    });

    mPreview->setPlaceholder(tr("选择切换组中的图层\n或从上方下拉选择切换组"));
}

void SwitchPanel::setListeningEnabled(const bool enabled)
{
    if(mListening == enabled) return;
    mListening = enabled;
    if(enabled) {
        setCurrentScene(mDocument.fActiveScene.data());
    } else {
        clearBinding();
        if(mScene) disconnect(mScene, nullptr, this, nullptr);
        mScene = nullptr;
        mSelectionDebounce->stop();
        mChildrenDebounce->stop();
        mRescanDebounce->stop();
    }
}

void SwitchPanel::setCurrentScene(Canvas* const scene)
{
    if(mScene == scene) return;
    if(mScene) disconnect(mScene, nullptr, this, nullptr);
    clearBinding();
    mScene = scene;
    if(mScene) {
        connect(mScene, &Canvas::objectSelectionChanged, this,
                [this]() { scheduleSelectionCheck(); });
        connect(mScene, &Canvas::currentFrameChanged, this,
                [this](int) { if(!mInteracting) updateActive(); });
        connect(mScene, &ContainerBox::insertedObject, this,
                [this](int, eBoxOrSound*) { scheduleGroupRescan(); });
        connect(mScene, &ContainerBox::removedObject, this,
                [this](int, eBoxOrSound*) { scheduleGroupRescan(); });
    }
    rescanGroups();
    updateActive();
}

void SwitchPanel::scheduleGroupRescan()
{
    if(!mListening) return;
    mRescanDebounce->start();
}

void SwitchPanel::rescanGroups()
{
    if(!mScene) return;
    QList<ContainerBox*> groups;
    std::function<void(ContainerBox*)> walk =
            [&groups, &walk](ContainerBox* const cont) {
        for(const auto& b : cont->getContainedBoxes()) {
            const auto g = enve_cast<ContainerBox*>(b);
            if(!g) continue;
            if(!g->isLink() && g->isSwitchLayer()) groups << g;
            walk(g);
        }
    };
    walk(mScene.data());

    mComboGuard = true;
    mGroupCombo->clear();
    mComboGroups.clear();
    for(const auto& g : groups) {
        mGroupCombo->addItem(g->prp_getName());
        mComboGroups << QPointer<ContainerBox>(g);
    }
    mComboGuard = false;

    if(mGroup && groups.contains(mGroup.data())) {
        mComboGuard = true;
        mGroupCombo->setCurrentIndex(groups.indexOf(mGroup.data()));
        mComboGuard = false;
    } else if(!groups.isEmpty()) {
        if(!mGroup) bindGroup(groups.first());
    } else if(!mGroup) {
        mComboGuard = true;
        mGroupCombo->addItem(tr("（场景中无切换组）"));
        mGroupCombo->setCurrentIndex(0);
        mComboGuard = false;
    }
}

void SwitchPanel::bindGroup(ContainerBox* const group)
{
    if(mGroup == group) {
        refreshChildren();
        return;
    }
    clearBinding();
    if(!group) return;
    mGroup = group;

    mGroupConns << connect(mGroup, &ContainerBox::insertedObject, this,
            [this](int, eBoxOrSound*) { scheduleChildrenRefresh(); });
    mGroupConns << connect(mGroup, &ContainerBox::removedObject, this,
            [this](int, eBoxOrSound* obj) {
        // prune immediately: the child may be freed before the
        // debounced rebuild runs
        for(int i = mChildren.count() - 1; i >= 0; i--) {
            if(mChildren.at(i) == obj) mChildren.removeAt(i);
        }
        scheduleChildrenRefresh();
    });
    mGroupConns << connect(mGroup, &ContainerBox::movedObject, this,
            [this](int, int, eBoxOrSound*) { refreshChildren(); });

    refreshChildren();
    scheduleGroupRescan();
}

void SwitchPanel::clearBinding()
{
    for(const auto& conn : mGroupConns) disconnect(conn);
    mGroupConns.clear();
    mGroup = nullptr;
    mChildren.clear();
    mActiveIndex = -1;
    mRuler->setItems(QStringList());
    mRuler->setActiveIndex(-1);
    mPreview->setPlaceholder(tr("选择切换组中的图层\n或从上方下拉选择切换组"));
}

void SwitchPanel::scheduleChildrenRefresh()
{
    if(!mGroup) return;
    mChildrenDebounce->start();
}

void SwitchPanel::refreshChildren()
{
    if(!mGroup) return;
    for(const auto& conn : mGroupConns) disconnect(conn);
    mGroupConns.clear();

    mChildren.clear();
    for(const auto& b : mGroup->getContainedBoxes()) {
        mChildren << QPointer<BoundingBox>(b);
    }

    QStringList names;
    for(const auto& b : mChildren) {
        names << (b ? b->prp_getName() : QString());
        if(b) {
            // trailing QPrivateSignal argument is omitted on purpose:
            // private-signal connections drop it
            mGroupConns << connect(b, &Property::prp_nameChanged, this,
                    [this](const QString&) {
                scheduleChildrenRefresh();
            });
        }
    }
    mRuler->setItems(names);

    mGroupConns << connect(mGroup, &ContainerBox::insertedObject, this,
            [this](int, eBoxOrSound*) { scheduleChildrenRefresh(); });
    mGroupConns << connect(mGroup, &ContainerBox::removedObject, this,
            [this](int, eBoxOrSound* obj) {
        for(int i = mChildren.count() - 1; i >= 0; i--) {
            if(mChildren.at(i) == obj) mChildren.removeAt(i);
        }
        scheduleChildrenRefresh();
    });
    mGroupConns << connect(mGroup, &ContainerBox::movedObject, this,
            [this](int, int, eBoxOrSound*) { refreshChildren(); });

    updateActive();
}

BoundingBox* SwitchPanel::childAt(const int index) const
{
    if(index < 0 || index >= mChildren.count()) return nullptr;
    const auto& b = mChildren.at(index);
    return b ? b.data() : nullptr;
}

void SwitchPanel::updateActive()
{
    if(!mGroup) return;
    int active = -1;
    for(int i = 0; i < mChildren.count(); i++) {
        const auto b = childAt(i);
        if(!b) continue;
        const auto va = b->getVisibleAnim();
        if(b->isVisible() && va && va->getEffectiveIntValue() == 1) {
            active = i;
            break;
        }
    }
    mActiveIndex = active;
    mRuler->setActiveIndex(active);
    const auto box = childAt(active);
    if(active >= 0 && box) {
        requestPreview(box);
    } else {
        mPreview->setPlaceholder(tr("无激活图层（可见性全关）"));
    }
}

void SwitchPanel::scheduleSelectionCheck()
{
    if(!mListening || mInteracting) return;
    mSelectionDebounce->start();
}

void SwitchPanel::checkSelection()
{
    if(!mScene || mInteracting) return;
    const auto sel = mScene->getSelectedBoxesList();
    for(int i = sel.count() - 1; i >= 0; i--) {
        const auto box = sel.at(i);
        if(!box) continue;
        const auto cont = enve_cast<ContainerBox*>(box);
        if(cont && cont->isSwitchLayer()) {
            if(cont != mGroup.data()) bindGroup(cont);
            return;
        }
        auto p = box->getParentGroup();
        while(p) {
            if(p->isSwitchLayer()) {
                if(p != mGroup.data()) bindGroup(p);
                return;
            }
            p = p->getParentGroup();
        }
    }
}

void SwitchPanel::commitSwitch(const int index)
{
    if(!mGroup || !mScene) return;
    const auto target = childAt(index);
    if(!target) return;
    const int absFrame = mScene->anim_getCurrentAbsFrame();
    const bool autoKey = mAutoKey->isChecked();

    QList<SwitchKeyRec> recs;
    for(int i = 0; i < mChildren.count(); i++) {
        const auto box = childAt(i);
        if(!box) continue;
        const auto va = box->getVisibleAnim();
        if(!va) continue;
        const int want = (box == target) ? 1 : 0;
        if(autoKey) {
            const int relF = box->prp_absFrameToRelFrame(absFrame);
            if(va->getEffectiveIntValue(relF) == want) continue;
            const auto key = va->anim_getKeyAtAbsFrame<QrealKey>(absFrame);
            SwitchKeyRec rec;
            rec.va = va;
            rec.oldKey = key ? qRound(key->getValue()) : -1;
            rec.oldBase = 0;
            rec.newVal = want;
            recs << rec;
        } else {
            if(va->getCurrentIntValue() == want) continue;
            SwitchKeyRec rec;
            rec.va = va;
            rec.oldKey = -2;
            rec.oldBase = va->getCurrentIntValue();
            rec.newVal = want;
            recs << rec;
        }
    }
    if(recs.isEmpty()) return;

    const auto apply = [absFrame](const QList<SwitchKeyRec>& rs,
                                  const bool forward) {
        for(const auto& r : rs) {
            if(!r.va) continue;
            if(r.oldKey == -2) {
                r.va->setCurrentIntValue(forward ? r.newVal : r.oldBase);
            } else {
                r.va->saveValueToKey(absFrame,
                                     forward ? r.newVal : r.oldKey);
            }
        }
    };
    apply(recs, true);

    if(recs.first().va) {
        recs.first().va->prp_pushUndoRedoName(tr("切换图层"));
        UndoRedo ur;
        ur.fUndo = [recs, absFrame, apply]() { apply(recs, false); };
        ur.fRedo = [recs, absFrame, apply]() { apply(recs, true); };
        recs.first().va->prp_addUndoRedo(ur);
    }

    Document::sInstance->actionFinished();
}

void SwitchPanel::insertPreviewCache(const QString& key, const QImage& img)
{
    if(mPreviewCache.contains(key)) {
        mPreviewOrder.removeAll(key);
    } else if(mPreviewOrder.count() >= 16) {
        mPreviewCache.remove(mPreviewOrder.takeFirst());
    }
    mPreviewCache.insert(key, img);
    mPreviewOrder << key;
}

void SwitchPanel::requestPreview(BoundingBox* const box)
{
    if(!box || !mScene) {
        mPreview->setPlaceholder(tr("无预览"));
        return;
    }
    const int absFrame = mScene->anim_getCurrentAbsFrame();
    const qreal relFrame = box->prp_absFrameToRelFrameF(absFrame);
    const QString key = QStringLiteral("%1:%2").arg(
                reinterpret_cast<qulonglong>(box), 0, 16).arg(relFrame);
    const auto it = mPreviewCache.constFind(key);
    if(it != mPreviewCache.constEnd()) {
        mPreview->setImage(it.value(), box->prp_getName());
        return;
    }

    // async offscreen render of the single layer (hidden layers bypass
    // the parent composition gate on this path, like track mattes);
    // the result is marshalled back through a queued invocation on
    // this panel so the task thread never touches the UI directly
    auto task = box->queExternalRender(relFrame, true);
    if(!task) {
        mPreview->setPlaceholder(tr("无法渲染预览"));
        return;
    }
    mPreviewTask = task;
    mPreviewToken++;
    const int token = mPreviewToken;
    const std::weak_ptr<BoxRenderData> weak = task;
    const QPointer<BoundingBox> boxQ = box;
    const QPointer<SwitchPanel> self = this;
    task->addDependent({[self, weak, boxQ, key, token]() {
        if(!self || token != self->mPreviewToken) return;
        const auto task = weak.lock();
        if(!task) return;
        QImage img;
        const auto sk = task->fRenderedImage;
        SkPixmap pm;
        if(sk && sk->peekPixels(&pm)) {
            QImage::Format fmt = QImage::Format_Invalid;
            if(pm.colorType() == kBGRA_8888_SkColorType) {
                fmt = QImage::Format_ARGB32_Premultiplied;
            } else if(pm.colorType() == kRGBA_8888_SkColorType) {
                fmt = QImage::Format_RGBA8888_Premultiplied;
            }
            if(fmt != QImage::Format_Invalid) {
                img = QImage(reinterpret_cast<const uchar*>(pm.addr()),
                             pm.width(), pm.height(),
                             int(pm.rowBytes()), fmt).copy();
            }
        }
        if(img.isNull()) {
            QMetaObject::invokeMethod(self, [self]() {
                if(self) self->mPreview->setPlaceholder(
                            self->tr("无法渲染预览"));
            }, Qt::QueuedConnection);
            return;
        }
        if(img.width() > 512 || img.height() > 512) {
            img = img.scaled(512, 512, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
        }
        const QString name = boxQ ? boxQ->prp_getName() : QString();
        QMetaObject::invokeMethod(self, [self, key, img, name]() {
            if(!self || !self->mPreview) return;
            self->insertPreviewCache(key, img);
            self->mPreview->setImage(img, name);
        }, Qt::QueuedConnection);
    }, [](){}});
}
