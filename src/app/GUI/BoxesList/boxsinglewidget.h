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

#ifndef BOXSINGLEWIDGET_H
#define BOXSINGLEWIDGET_H

#include "GUI/boxeslistactionbutton.h"
#include "optimalscrollarena/singlewidget.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMimeData>
#include <QComboBox>
#include <QPointer>
#include "skia/skiaincludes.h"
#include "smartPointers/ememory.h"
#include "framerange.h"
#include "Animators/SmartPath/smartpathanimator.h"
class QrealAnimatorValueSlider;
class QPlainTextEdit;
class TimelineMovable;
class eBoxOrSound;
class Key;
class BoxTargetWidget;
class BoolPropertyWidget;
class ComboBoxProperty;
class ColorAnimator;
class ColorAnimatorButton;
class BoxScroller;
class eComboBox;

class BoxSingleWidget : public SingleWidget {
    Q_OBJECT
public:
    explicit BoxSingleWidget(BoxScroller * const parent);

    void setTargetAbstraction(SWT_Abstraction *abs);

    // node-link parenting drag mime format (source layer pointer)
    static const char* parentLinkMimeType()
    { return "application/x-friction-parent-link"; }

    static QPixmap* VISIBLE_ICON;
    static QPixmap* INVISIBLE_ICON;
    static QPixmap* BOX_CHILDREN_VISIBLE_ICON;
    static QPixmap* BOX_CHILDREN_HIDDEN_ICON;
    static QPixmap* ANIMATOR_CHILDREN_VISIBLE_ICON;
    static QPixmap* ANIMATOR_CHILDREN_HIDDEN_ICON;
    static QPixmap* LOCKED_ICON;
    static QPixmap* UNLOCKED_ICON;
    static QPixmap* MUTED_ICON;
    static QPixmap* UNMUTED_ICON;
    static QPixmap* ANIMATOR_RECORDING_ICON;
    static QPixmap* ANIMATOR_NOT_RECORDING_ICON;
    static QPixmap* ANIMATOR_DESCENDANT_RECORDING_ICON;
    static QPixmap* C_ICON;
    static QPixmap* G_ICON;
    static QPixmap* CG_ICON;
    static QPixmap* GRAPH_PROPERTY_ICON;
    static QPixmap* PROMOTE_TO_LAYER_ICON;
    static QPixmap* ICON_3D_ON;
    static QPixmap* ICON_3D_OFF;
    static QPixmap* ICON_RESET;

    // AE-style layer switch glyphs (text characters rasterized)
    static QPixmap* ICON_SOLO_ON;
    static QPixmap* ICON_SOLO_OFF;
    static QPixmap* ICON_SHY_ON;
    static QPixmap* ICON_SHY_OFF;
    static QPixmap* ICON_FX_ON;
    static QPixmap* ICON_FX_OFF;
    static QPixmap* ICON_MB_ON;
    static QPixmap* ICON_MB_OFF;
    static QPixmap* ICON_T_ON;
    static QPixmap* ICON_T_OFF;
    static QPixmap* ICON_LINKNODE_ON;
    static QPixmap* ICON_LINKNODE_OFF;
    // scale X/Y proportional link: the bone parent-link chain glyph
    // (bone_parent.svg rasterized, bright = linked)
    static QPixmap* ICON_SCALE_LINK_ON;
    static QPixmap* ICON_SCALE_LINK_OFF;
    // track matte mode glyphs: alpha / alphaInv / luma / lumaInv
    static QPixmap* ICON_TM_ALPHA;
    static QPixmap* ICON_TM_ALPHAINV;
    static QPixmap* ICON_TM_LUMA;
    static QPixmap* ICON_TM_LUMAINV;
    static QPixmap* ICON_TM_OFF;

    static QPixmap* BOX_PATH;
    static QPixmap* BOX_CIRCLE;
    static QPixmap* BOX_RECT;
    static QPixmap* BOX_TEXT;
    static QPixmap* BOX_NULL;
    static QPixmap* BOX_IMAGE;
    static QPixmap* BOX_VIDEO;
    static QPixmap* BOX_SOUND;
    static QPixmap* BOX_BONE;
    static QPixmap* BOX_BONELAYER;
    static QPixmap* BOX_SOLID;
    static QPixmap* BOX_CAMERA;
    static QPixmap* BOX_GROUP;
    static QPixmap* BOX_LINK;
    static QPixmap* BOX_SEQ;

    static bool sStaticPixmapsLoaded;
    static void loadStaticPixmaps(int iconSize);
    static void clearStaticPixmaps();

    void prp_drawTimelineControls(QPainter * const p,
                  const qreal pixelsPerFrame,
                  const FrameRange &viewedFrames);
    Key *getKeyAtPos(const int pressX,
                     const qreal pixelsPerFrame,
                     const int minViewedFrame);
    void getKeysInRect(const QRectF &selectionRect,
                       const qreal pixelsPerFrame,
                       QList<Key *> &listKeys);
    TimelineMovable *getRectangleMovableAtPos(
                        const int pressX,
                        const qreal pixelsPerFrame,
                        const int minViewedFrame);
    // the inactive track member whose clip contains the x position
    eBoxOrSound *getTrackClipAtPos(
                        const int pressX,
                        const qreal pixelsPerFrame,
                        const int minViewedFrame);

    void setSelected(const bool selected) {
        mSelected = selected;
        update();
        selOverlayUpdate();
    }
    void selOverlayUpdate();

    // true when x falls outside the interactive band (mFillWidget) -
    // the left indent and the blank right side of the row; natively
    // gesture-dead, used as the rubber-band selection start zone
    bool inRowBlankZone(const int x) const {
        return !mFillWidget || x < mFillWidget->x() ||
               x > mFillWidget->x() + mFillWidget->width();
    }
protected:
    bool mSelected = false;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void mousePressEvent(QMouseEvent *e);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);

    void enterEvent(QEvent *);
    void leaveEvent(QEvent *);

#ifdef Q_OS_MAC
    void keyPressEvent(QKeyEvent *event);
#endif

    void paintEvent(QPaintEvent *);
    void mouseDoubleClickEvent(QMouseEvent *e);
    void resizeEvent(QResizeEvent *);

    bool mBlendModeVisible = false;
    bool mPathBlendModeVisible = false;
    bool mFillTypeVisible = false;

    void updatePathCompositionBoxVisible();
    void updateCompositionBoxVisible();
    void updateFillTypeBoxVisible();

    void clearAndHideValueAnimators();
    void updateValueSlidersForQPointFAnimator();
private:
    ContainerBox *getPromoteTargetGroup();

    // AE-style Shift range selection: select every layer row between
    // the anchor (last plain-clicked row) and the clicked row
    void selectRowRange(BoxSingleWidget* const rowA,
                        BoxSingleWidget* const rowB);
    static QPointer<BoxSingleWidget> sLastClickedRow;

    void clearSelected() { setSelected(false); }
    void switchContentVisibleAction();
    void switchRecordingAction();
    void switchBoxLockedAction();
    void resetPropertyAction();

    void switchBoxVisibleAction();
    void setCompositionMode(const int id);
    void setPathCompositionMode(const int id);
    void setFillType(const int id);
    ColorAnimator* getColorTarget() const;

    void setComboProperty(ComboBoxProperty * const combo);

    void handlePropertySelectedChanged(const Property *prop);

    BoxScroller* const mParent;

    bool mDragPressPos = false;
    QPoint mDragStartPos;

    bool mHover = false;

    PixmapActionButton *mBoxButton;
    PixmapActionButton *mRecordButton;
    PixmapActionButton *mContentButton;
    PixmapActionButton *mVisibleButton;
    PixmapActionButton *mLockedButton;
    PixmapActionButton *mSoloButton;
    PixmapActionButton *mShyButton;
    PixmapActionButton *m3DButton;
    PixmapActionButton *mFxButton;
    PixmapActionButton *mMbButton;
    PixmapActionButton *mTButton;
    class ParentLinkButton* mParentLinkButton;
    PixmapActionButton *mHwSupportButton;
    ColorAnimatorButton *mColorButton;
    BoxTargetWidget *mBoxTargetWidget;

    QWidget *mFillWidget;
    BoolPropertyWidget *mCheckBox;
    QHBoxLayout *mMainLayout;
    QrealAnimatorValueSlider *mValueSlider;
    QrealAnimatorValueSlider *mSecondValueSlider;
    // scale X/Y proportional link toggle (sits between the x/y sliders
    // on collapsed scale rows); state = "linkedScale" dynamic property
    // on the scale QPointFAnimator
    PixmapActionButton *mScaleLinkButton = nullptr;
    PixmapActionButton *mResetButton;

    // AE-style inline expression editor: fx button on animator rows
    // toggles an ExpressionRow child (script editor line) under the
    // property; commit on focus-out / Ctrl+Return
    PixmapActionButton *mExprButton = nullptr;
    QPlainTextEdit *mExprEdit = nullptr;
    PixmapActionButton *mExprClearButton = nullptr;
    bool mExprEditLoading = false;

    void toggleExpressionRow();
    void commitExpressionEdit();
    void collapseOwnExpressionRow();

    PixmapActionButton *mPromoteToLayerButton;
    eComboBox *mPropertyComboBox;
    eComboBox *mBlendModeCombo;
    eComboBox *mPathBlendModeCombo;
    eComboBox *mFillTypeCombo;
    // shows and switches the node-link parent of this layer
    eComboBox *mParentLinkCombo;
    bool mParentLinkComboBuilding = false;
    // AE-style track matte: single matte-layer pick + mode cycle button
    eComboBox *mTrkMatLayerCombo;
    PixmapActionButton *mTrkMatModeButton;
    bool mTrkMatBuilding = false;
    void rebuildTrkMatLayerCandidates();

    // layer-picker dropdown for BoxTargetProperty rows flagged with
    // comboPicker() (e.g. the liquid-glass background layer)
    eComboBox *mBgLayerCombo = nullptr;
    bool mBgLayerBuilding = false;
    class BoxTargetProperty* mBgTargetProp = nullptr;
    void rebuildBgLayerCandidates();

    // topmost translucent overlay painting the selection highlight
    // above every child widget of the row
    class RowHighlightOverlay* mSelOverlay = nullptr;
public:
    bool isSelectedRow() const { return mSelected; }
protected:

    // node-link parenting UI
    void showParentLinkMenu();
    void startParentLinkDrag();
    void refreshParentLinkCombo();
    void rebuildParentLinkCandidates();
    BoundingBox* currentLinkedBox();

    ConnContext mTargetConn;
};

QString translatePropertyName(const QString& name);

#endif // BOXSINGLEWIDGET_H
