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





#include "boxscroller.h"


#include "boxsinglewidget.h"


#include <QPainter>


#include "Animators/qrealanimator.h"


#include "boxscrollwidget.h"


#include <QTimer>


#include <QMimeData>


#include <QDataStream>


#include <QCursor>


#include <QStatusBar>


#include "Boxes/boundingbox.h"


#include "canvas.h"


#include "Boxes/bonelayer.h"


#include "Properties/emimedata.h"


#include "Private/document.h"


#include "Boxes/containerbox.h"


#include "GUI/mainwindow.h"


#include "GUI/global.h"


#include "swt_abstraction.h"


#include "GUI/keysview.h"


#include "RasterEffects/rastereffectcollection.h"


#include "GUI/timelinehighlightwidget.h"





// the layer/sound rows carried by a layer drag&drop


static QList<eBoxOrSound *> draggedLayers(const QMimeData * const mime) {


    if(!mime || !eMimeData::sHasType<eBoxOrSound>(mime)) return QList<eBoxOrSound*>();


    return static_cast<const eMimeData*>(mime)->getObjects<eBoxOrSound>();


}





bool  BoxScroller::sPlActive = false;


QPoint BoxScroller::sPlSrcGlobal;





BoxScroller::BoxScroller(ScrollWidget * const parent) :


    ScrollWidgetVisiblePart(parent) {


    setAcceptDrops(true);


    mScrollTimer = new QTimer(this);


}





QWidget *BoxScroller::createNewSingleWidget() {


    return new BoxSingleWidget(this);


}





void BoxScroller::plDragStarted(const QPoint& srcGlobalCenter) {


    sPlActive = true;


    sPlSrcGlobal = srcGlobalCenter;


}





void BoxScroller::plDragEnded() {


    sPlActive = false;


}





void BoxScroller::paintEvent(QPaintEvent *) {


    QPainter p(this);





    int currY = eSizesUI::widget;


    p.setPen(QPen(ThemeSupport::getThemeButtonBorderColor(), 1));


    const auto parent = static_cast<BoxScrollWidget*>(parentWidget());


    const int parentContHeight = parent->getContentHeight() - eSizesUI::widget;


    while(currY < parentContHeight) {


        p.drawLine(0, currY, width(), currY);


        currY += eSizesUI::widget;


    }





    // AE-style connector line during a parent-link drag: the line runs


    // from the source row's link button to the live cursor position


    if(sPlActive) {


        p.setRenderHint(QPainter::Antialiasing, true);


        const QColor link = ThemeSupport::getThemeHighlightColor();


        const QPoint src = mapFromGlobal(sPlSrcGlobal);


        const QPoint dst = mapFromGlobal(QCursor::pos());


        p.setPen(QPen(link, 2));


        p.drawLine(src, dst);


        p.setPen(Qt::NoPen);


        p.setBrush(link);


        p.drawEllipse(src, 4, 4);


        p.drawEllipse(dst, 5, 5);


        p.setBrush(Qt::NoBrush);


        p.setRenderHint(QPainter::Antialiasing, false);


    }





    if(mDropTarget.isValid()) {


        p.setPen(QPen(Qt::white, 2));


        if(mDropIsCombine) {


            // track-combine preview: translucent fill over the whole row,


            // clearly distinct from the thin reorder insert line


            p.setBrush(QColor(255, 255, 255, 35));


        }


        p.drawRect(mCurrentDragRect);


        if(mDropIsCombine) p.setBrush(Qt::NoBrush);


    }





    p.end();


}





TimelineHighlightWidget *BoxScroller::requestHighlighter() {


    if(!mHighlighter) {


        mHighlighter = new TimelineHighlightWidget(true, this, true);


        mHighlighter->resize(size());


    }


    return mHighlighter;


}





void BoxScroller::resizeEvent(QResizeEvent *e) {


    if(mHighlighter) mHighlighter->resize(e->size());


    ScrollWidgetVisiblePart::resizeEvent(e);


}





bool BoxScroller::tryDropIntoAbs(SWT_Abstraction* const abs,


                                 const int idInAbs,


                                 DropTarget& dropTarget) {


    if(!abs) return false;


    const auto target = abs->getTarget();


    const int id = qBound(0, idInAbs, abs->childrenCount());


    if(!target->SWT_dropIntoSupport(id, mCurrentMimeData)) return false;


    dropTarget = DropTarget{abs, id, DropType::into};


    return true;


}





// editing-app rule: audio rows only merge onto audio rows, visual rows


// (graphics/bitmap/video/text - every BoundingBox kind) only onto other


// visual rows; group rows keep their existing drop-into behavior


bool BoxScroller::dragCombineTarget(const SingleWidgetTarget *target) const {


    const auto dragged = draggedLayers(mCurrentMimeData);


    if(dragged.isEmpty() || !target) return false;


    if(enve_cast<const ContainerBox*>(target)) return false;


    const auto bos = enve_cast<const eBoxOrSound*>(target);


    if(!bos) return false;


    const bool audio = bos->isAudioKind();


    // the whole batch has to match, so mixed selections never merge


    for(const auto obj : dragged) {


        if(!obj || obj->isAudioKind() != audio) return false;


    }


    return true;


}





BoxScroller::DropTarget BoxScroller::getClosestDropTarget(const int yPos) {


    const auto mainAbs = getMainAbstration();


    if(!mainAbs) return DropTarget();


    const int idAtPos = yPos / eSizesUI::widget;


    DropTarget target;


    mDropIsCombine = false;


    const auto& wids = widgets();


    const int nWidgets = wids.count();


    if(idAtPos >= 0 && idAtPos < nWidgets) {


        const auto bsw = static_cast<BoxSingleWidget*>(wids.at(idAtPos));


        if(bsw->isHidden()) {


            const int nChildren = mainAbs->childrenCount();


            if(tryDropIntoAbs(mainAbs, nChildren, target)) {


                mCurrentDragRect = QRect(0, visibleCount()*eSizesUI::widget, width(), 1);


                return target;


            }


        } else if(bsw->getTargetAbstraction()) {


            const auto abs = bsw->getTargetAbstraction();


            const bool above = yPos % eSizesUI::widget < eSizesUI::widget*0.5;


            bool dropOn = false;


            {


                const qreal posFrac = qreal(yPos)/eSizesUI::widget;


                if(qAbs(qRound(posFrac) - posFrac) > 0.333) dropOn = true;


                if(!above && abs->contentVisible() &&


                   abs->childrenCount() > 0) dropOn = true;


                // wider drop-onto zone for track combining: the middle


                // 60% of a plain same-kind row accepts layer drops


                // (mismatched kinds keep showing a reorder line)


                if(!dropOn && !altDetachActive() &&


                   dragCombineTarget(abs->getTarget())) {


                    dropOn = qAbs(qRound(posFrac) - posFrac) > 0.2;


                }


            }


            for(const bool iDropOn : {dropOn, !dropOn}) {


                if(iDropOn) {


                    const auto dropTarget = abs->getTarget();


                    // dropping a layer row onto a plain row of the same


                    // kind combines both into one track; group rows keep


                    // their gather-inside behavior via SWT_dropSupport


                    const bool combineOnLayer =


                            !altDetachActive() &&


                            dragCombineTarget(dropTarget);


                    if(combineOnLayer ||


                       dropTarget->SWT_dropSupport(mCurrentMimeData)) {


                        mCurrentDragRect = bsw->rect().translated(bsw->pos());


                        mDropIsCombine = combineOnLayer;


                        return {abs, 0, DropType::on};


                    }


                } else {


                    const auto parentAbs = abs->getParent();


                    if(parentAbs) {


                        const int id = abs->getIdInParent() + (above ? 0 : 1);


                        if(tryDropIntoAbs(parentAbs, id, target)) {


                            const int y = bsw->y() + (above ? 0 : abs->getHeight());


                            mCurrentDragRect = QRect(bsw->x(), y,  width(), 1);


                            return target;


                        }


                    }


                }


            }


        }


    }


    for(int i = idAtPos - 1; i >= 0; i--) {


        const auto bsw = static_cast<BoxSingleWidget*>(wids.at(i));


        if(!bsw->isHidden() && bsw->getTargetAbstraction()) {


            const auto abs = bsw->getTargetAbstraction();


            if(abs->getTarget()->SWT_dropSupport(mCurrentMimeData)) {


                mCurrentDragRect = bsw->rect().translated(bsw->pos());


                return {abs, 0, DropType::on};


            }


            const auto parentAbs = abs->getParent();


            if(parentAbs) {


                const int id = abs->getIdInParent() + 1;


                if(tryDropIntoAbs(parentAbs, id, target)) {


                    const int y = bsw->y() + abs->getHeight();


                    mCurrentDragRect = QRect(bsw->x(), y, width(), 1);


                    return target;


                }


            }


        }


    }





    for(int i = idAtPos + 1; i < wids.count(); i++) {


        const auto bsw = static_cast<BoxSingleWidget*>(wids.at(i));


        if(!bsw->isHidden() && bsw->getTargetAbstraction()) {


            const auto abs = bsw->getTargetAbstraction();


            if(tryDropIntoAbs(abs, 0, target)) {


                mCurrentDragRect = QRect(bsw->x() + eSizesUI::widget,


                                         bsw->y() + eSizesUI::widget,


                                         width(), 1);


                return target;


            }


        }


    }


    return DropTarget();


}





void BoxScroller::stopScrolling() {


    if(mScrollTimer->isActive()) {


        mScrollTimer->disconnect();


        mScrollTimer->stop();


    }


}





void BoxScroller::dropEvent(QDropEvent *event) {


    stopScrolling();


    mCurrentMimeData = event->mimeData();


    mLastDragMoveY = event->pos().y();


    mDragModifiers = event->keyboardModifiers();


    // node-link parenting drag: link the source layer to the layer row


    // under the cursor (handled before the reorder/reparent logic)


    if(event->mimeData()->hasFormat(BoxSingleWidget::parentLinkMimeType())) {


        plDragEnded();


        quintptr srcPtr = 0;


        QDataStream ds(event->mimeData()->data(


                           BoxSingleWidget::parentLinkMimeType()));


        ds >> srcPtr;


        auto source = reinterpret_cast<BoundingBox*>(srcPtr);


        const auto& wids = widgets();


        const int idAtPos = event->pos().y() / eSizesUI::widget;


        if(source && idAtPos >= 0 && idAtPos < wids.count()) {


            const auto bsw = static_cast<BoxSingleWidget*>(wids.at(idAtPos));


            // the row may have lost its target while dragging


            const auto dropAbs = bsw->getTargetAbstraction();


            const auto targetBox = dropAbs ?


                        enve_cast<BoundingBox*>(dropAbs->getTarget()) :


                        nullptr;


            const auto scene = source->getParentScene();


            if(targetBox && scene) {


                scene->linkParentLevel(source, targetBox);


                event->accept();


            }


        }


        mCurrentMimeData = nullptr;


        mDropTarget.reset();


        return;


    }


    updateDropTarget();


    if(mDropTarget.isValid()) {


        const auto targetAbs = mDropTarget.fTargetParent;


        const auto target = targetAbs->getTarget();


        const auto draggedBos = draggedLayers(mCurrentMimeData);


        if(altDetachActive() && !draggedBos.isEmpty() &&


           mDropTarget.fDropType == DropType::on) {


            // Alt+release over a plain row: detach every dragged track


            // member instead of combining (no reorder takes place)


            for(const auto obj : draggedBos) {


                if(obj && obj->isInTrack()) obj->setTrackId(-1);


            }


        } else if(mDropTarget.fDropType == DropType::on) {


            // layer rows dropped onto a plain same-kind row combine into


            // one timeline track; mismatched kinds never reach this spot


            // - while hovering they only ever show a reorder line.


            // Dropping onto a CONTAINER row moves the layers INTO that


            // container (Moho-style: drop artwork onto the bone-layer


            // row); SWT_drop carries its own cycle guards


            if(!draggedBos.isEmpty()) {


                bool combined = false;


                if(dragCombineTarget(target)) {


                    combined = currentScene()->combineIntoTrack(


                                enve_cast<eBoxOrSound*>(target), draggedBos);


                }


                if(!combined) {


                    if(const auto bl = enve_cast<BoneLayer*>(target)) {


                        // Moho-style: dropping onto a bone-layer row


                        // FLATTENS plain groups (children move directly


                        // under the bone layer, shell removed) -


                        // nesting would isolate blend-mode layers from


                        // their backdrop and shift colors


                        bl->absorbDroppedBoxes(draggedBos);


                    } else if(enve_cast<ContainerBox*>(target)) {


                        target->SWT_drop(mCurrentMimeData);


                    } else {


                        MainWindow::sGetInstance()->statusBar()->showMessage(


                                    QObject::tr("Cannot merge into this track"), 4000);


                    }


                }


            } else {


                target->SWT_drop(mCurrentMimeData);


            }


        } else if(mDropTarget.fDropType == DropType::into) {


            target->SWT_dropInto(mDropTarget.fTargetId, mCurrentMimeData);


            // dropping a track member between rows: when either new


            // neighbor belongs to the same track it is an in-track


            // reorder (the whole track follows); otherwise the member


            // LEAVES the track and becomes an independent layer again.


            // Holding Alt forces the leave regardless of neighbors


            for(const auto obj : draggedBos) {


                if(!obj || !obj->isInTrack()) continue;


                if(altDetachActive()) {


                    obj->setTrackId(-1); // forced by the user


                    continue;


                }


                const auto scene = obj->getParentScene();


                if(!scene) continue;


                const auto parent = obj->getParentGroup();


                bool neighborInTrack = false;


                if(parent) {


                    const int idx = parent->getContainedIndex(obj);


                    const auto& cs = parent->getContained();


                    if(idx > 0 && cs.at(idx - 1) &&


                       cs.at(idx - 1)->trackId() == obj->trackId()) {


                        neighborInTrack = true;


                    }


                    if(idx >= 0 && idx + 1 < cs.count() && cs.at(idx + 1) &&


                       cs.at(idx + 1)->trackId() == obj->trackId()) {


                        neighborInTrack = true;


                    }


                }


                if(neighborInTrack) {


                    scene->gatherTrack(obj);


                } else {


                    obj->setTrackId(-1); // left the track


                }


            }


        }


        planScheduleUpdateVisibleWidgetsContent();


        Document::sInstance->actionFinished();


    }


    mCurrentMimeData = nullptr;


    mDropTarget.reset();


    mDragModifiers = Qt::NoModifier;


    mDropIsCombine = false;


}





void BoxScroller::dragEnterEvent(QDragEnterEvent *event) {


    const auto mimeData = event->mimeData();


    mLastDragMoveY = event->pos().y();


    mCurrentMimeData = mimeData;


    mDragModifiers = event->keyboardModifiers();


    updateDropTarget();


    //mDragging = true;


    if(mCurrentMimeData) event->acceptProposedAction();


    update();


}





void BoxScroller::dragLeaveEvent(QDragLeaveEvent *event) {


    mCurrentMimeData = nullptr;


    mDropTarget.reset();


    mDragModifiers = Qt::NoModifier;


    mDropIsCombine = false;


    plDragEnded();


    stopScrolling();


    event->accept();


    update();


}





void BoxScroller::dragMoveEvent(QDragMoveEvent *event) {


    event->acceptProposedAction();


    const int yPos = event->pos().y();


    mDragModifiers = event->keyboardModifiers();





    if(yPos < 30) {


        if(!mScrollTimer->isActive()) {


            connect(mScrollTimer, &QTimer::timeout,


                    this, &BoxScroller::scrollUp);


            mScrollTimer->start(300);


        }


    } else if(yPos > height() - 30) {


        if(!mScrollTimer->isActive()) {


            connect(mScrollTimer, &QTimer::timeout,


                    this, &BoxScroller::scrollDown);


            mScrollTimer->start(300);


        }


    } else {


        mScrollTimer->disconnect();


        mScrollTimer->stop();


    }


    mLastDragMoveY = yPos;





    updateDropTarget();


    update();


}





void BoxScroller::updateDropTarget() {


    mDropTarget = getClosestDropTarget(mLastDragMoveY);


}





void BoxScroller::scrollUp() {


    parentWidget()->scrollParentAreaBy(-eSizesUI::widget);


    updateDropTarget();


    update();


}





void BoxScroller::scrollDown() {


    parentWidget()->scrollParentAreaBy(eSizesUI::widget);


    updateDropTarget();


    update();


}


