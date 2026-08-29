/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
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

#include "toolbox.h"
#include <QSvgRenderer>
#include "themesupport.h"

#include <QToolButton>
#include <QPainter>
#include <QApplication>

using namespace Friction::Ui;

ToolBox::ToolBox(Actions &actions,
                 Document &document,
                 QWidget *parent)
    : QObject{parent}
    , mActions(actions)
    , mDocument(document)
    , mMain(nullptr)
    , mControls(nullptr)
    , mExtra(nullptr)
    , mInteract(nullptr)
    , mGroupMain(nullptr)
    , mGroupNodes(nullptr)
    , mGroupDraw(nullptr)
    , mGroupColorPicker(nullptr)
    , mDrawPathMaxError(nullptr)
    , mDrawPathSmooth(nullptr)
    , mLocalPivot(nullptr)
    , mColorPickerButton(nullptr)
    , mColorPickerLabel(nullptr)
{
    setupToolBox(parent);
}

QToolBar *ToolBox::getToolBar(const Type &type)
{
    switch (type) {
    case Type::Main:
        return mMain;
    case Type::Controls:
        return mControls;
    case Type::Extra:
        return mExtra;
    case Type::Interact:
        return mInteract;
    default:
        return nullptr;
    }
}

const QList<QAction*> ToolBox::getMainActions()
{
    return mGroupMain->actions();
}

const QList<QAction*> ToolBox::getNodeActions()
{
    return mGroupNodes->actions();
}

void ToolBox::setMovable(const bool movable)
{
    mMain->setMovable(movable);
    mControls->setMovable(movable);
    if (mExtra) { mExtra->setMovable(movable); }
}

void ToolBox::setupToolBox(QWidget *parent)
{
    if (!parent) { return; }

    mMain = new ToolBar(tr("ToolBox"),
                        "ToolBoxMain",
                        parent,
                        true);
    mControls = new ToolControls(parent);
    mInteract = new ToolInteract(parent);
    // disable for now
    /*mExtra = new ToolboxToolBar(tr("Extra Tools"),
                                "ToolBoxExtra",
                                parent);*/

    mGroupMain = new QActionGroup(this);
    mGroupNodes = new QActionGroup(this);
    mGroupDraw = new QActionGroup(this);
    mGroupColorPicker = new QActionGroup(this);

    setupDocument();
    setupMainActions();
    setupNodesActions();
    setupDrawActions();
    setupColorPickerActions();
}

void ToolBox::setupDocument()
{
    connect(&mDocument, &Document::activeSceneSet,
            this, &ToolBox::setCurrentCanvas);
    connect(&mDocument, &Document::canvasModeSet,
            this, &ToolBox::setCanvasMode);
    connect(&mDocument, &Document::currentPixelColor,
            this, &ToolBox::updateColorPicker);
}

void ToolBox::setupMainAction(const QIcon &icon,
                              const QString &title,
                              const QKeySequence &shortcut,
                              const QList<CanvasMode> &modes,
                              const bool checked)
{
    if (modes.isEmpty() ||
        icon.isNull() ||
        title.isEmpty()) { return; }

    // white variant for the checked state (matches the svg tools)
    QIcon twoState = icon;
    const int icSize = ThemeSupport::getIconSize(64).width();
    QPixmap pm = icon.pixmap(QSize(icSize, icSize));
    if(!pm.isNull()) {
        QPixmap pmOn = pm;
        QPainter pOn(&pmOn);
        pOn.setCompositionMode(QPainter::CompositionMode_SourceIn);
        pOn.fillRect(pmOn.rect(), Qt::white);
        pOn.end();
        twoState.addPixmap(pm, QIcon::Normal, QIcon::Off);
        twoState.addPixmap(pmOn, QIcon::Normal, QIcon::On);
    }

    const auto act = new QAction(twoState,
                                 title,
                                 mMain);
    act->setCheckable(true);
    act->setChecked(checked);
    act->setShortcut(shortcut);
    mGroupMain->addAction(act);

    connect(act,
            &QAction::triggered,
            this,
            [this, modes](bool checked) {
        if (!checked) { return; }
        switch (modes.at(0)) {
        case CanvasMode::boxTransform:
            mActions.setMovePathMode();
            break;
        case CanvasMode::pointTransform:
            mActions.setMovePointMode();
            break;
        case CanvasMode::pathCreate:
            mActions.setAddPointMode();
            break;
        case CanvasMode::drawPath:
            mActions.setDrawPathMode();
            break;
        case CanvasMode::circleCreate:
            mActions.setCircleMode();
            break;
        case CanvasMode::rectCreate:
            mActions.setRectangleMode();
            break;
        case CanvasMode::textCreate:
            mActions.setTextMode();
            break;
        case CanvasMode::nullCreate:
            mActions.setNullMode();
            break;
        case CanvasMode::boneCreate:
            mActions.setBoneCreateMode();
            break;
        case CanvasMode::bonePose:
            mActions.setBonePoseMode();
            break;
        case CanvasMode::boneBind:
            mActions.setBoneBindMode();
            break;
        case CanvasMode::boneParent:
            mActions.setBoneParentMode();
            break;
        case CanvasMode::boneSelect:
            mActions.setBoneSelectMode();
            break;
        case CanvasMode::camera:
            mActions.setCameraMode();
            break;
        case CanvasMode::pickFillStroke:
            mActions.setPickPaintSettingsMode();
            break;
        default:;
        }
    });
    connect(&mDocument,
            &Document::canvasModeSet,
            this,
            [act, modes](CanvasMode mode) {
        if (modes.contains(mode)) { act->setChecked(true); }
    });
}


// the bone tool icon: rasterized from the bundled bone_icon.svg via
// QSvgRenderer (the iconengines plugin is not deployed, so QIcon on an
// svg file would not work)
static QIcon svgToolIcon(const QString& qrcPath, const int size) {
    static QHash<QString, QIcon> cache;
    const auto hit = cache.constFind(qrcPath);
    if(hit != cache.constEnd()) return hit.value();
    QFile f(qrcPath);
    if(!f.open(QIODevice::ReadOnly)) return QIcon();
    const QByteArray data = f.readAll();
    QSvgRenderer renderer(data);
    if(!renderer.isValid()) return QIcon();
    // keep the artwork aspect; WIDE artwork (like the bone-select
    // glyph) is rendered fill-height into a wider temp pixmap and the
    // center square is cropped out, so it matches the visual weight of
    // the square icons (Qt5 QSvgRenderer has no source-rect overload)
    const QSizeF def = renderer.defaultSize();
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    if(def.width() > 0 && def.height() > 0) {
        const double ratio = def.width()/def.height();
        if(ratio > 1.15) {
            QPixmap tmp(qRound(size*ratio), size);
            tmp.fill(Qt::transparent);
            QPainter pt(&tmp);
            renderer.render(&pt, QRectF(0, 0, tmp.width(), tmp.height()));
            pt.end();
            pm = tmp.copy((tmp.width() - size)/2, 0, size, size);
        } else if(ratio < 1/1.15) {
            QPixmap tmp(size, qRound(size/ratio));
            tmp.fill(Qt::transparent);
            QPainter pt(&tmp);
            renderer.render(&pt, QRectF(0, 0, tmp.width(), tmp.height()));
            pt.end();
            pm = tmp.copy(0, (tmp.height() - size)/2, size, size);
        } else {
            QPainter p(&pm);
            renderer.render(&p, QRectF(0, 0, size, size));
            p.end();
        }
    } else {
        QPainter p(&pm);
        renderer.render(&p, QRectF(0, 0, size, size));
        p.end();
    }
    // checked-state variant: white tint of the same artwork (tool
    // buttons highlight their icon when the mode is active)
    QPixmap pmOn = pm;
    QPainter pOn(&pmOn);
    pOn.setCompositionMode(QPainter::CompositionMode_SourceIn);
    pOn.fillRect(pmOn.rect(), Qt::white);
    pOn.end();
    QIcon ic;
    ic.addPixmap(pm, QIcon::Normal, QIcon::Off);
    ic.addPixmap(pmOn, QIcon::Normal, QIcon::On);
    cache.insert(qrcPath, ic);
    return ic;
}

static QIcon boneSvgIcon(const int size) {
    return svgToolIcon(QStringLiteral(":/icons/bone.svg"), size);
}

// monochrome tool glyph rendered with a symbol font (follows the
// theme color, never a colored emoji)
static QIcon glyphToolIcon(const QString& glyph, const int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(QStringLiteral("Segoe UI Symbol"));
    f.setPixelSize(qRound(size*0.82));
    p.setFont(f);
    p.setPen(ThemeSupport::getThemeColorBlue());
    p.drawText(pm.rect(), Qt::AlignCenter, glyph);
    p.end();
    return QIcon(pm);
}

void ToolBox::setupMainActions()
{
    setupMainAction(ThemeSupport::themedToolIcon("boxTransform",
                                                 ThemeSupport::getThemeColorBlue(),
                                                 64),
                    tr("Object Mode"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "boxTransform",
                                                         "F1").toString()),
                    {CanvasMode::boxTransform},
                    true);
    setupMainAction(QIcon::fromTheme("pointTransform"),
                    tr("Point Mode"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "pointTransform",
                                                         "F2").toString()),
                    {CanvasMode::pointTransform},
                    false);
    setupMainAction(svgToolIcon(QStringLiteral(":/icons/pen_tool.svg"),
                              ThemeSupport::getIconSize(64).width()),
                    tr("Add Path"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "pathCreate",
                                                         "F3").toString()),
                    {CanvasMode::pathCreate},
                    false);
    setupMainAction(QIcon::fromTheme("drawPath"),
                    tr("Draw Path"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "drawPath",
                                                         "F4").toString()),
                    {CanvasMode::drawPath},
                    false);
    setupMainAction(QIcon::fromTheme("circleCreate"),
                    tr("Add Circle"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "circleMode",
                                                         "F5").toString()),
                    {CanvasMode::circleCreate},
                    false);
    setupMainAction(QIcon::fromTheme("rectCreate"),
                    tr("Add Rectangle"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "rectMode",
                                                         "F6").toString()),
                    {CanvasMode::rectCreate},
                    false);
    setupMainAction(QIcon::fromTheme("textCreate"),
                    tr("Add Text"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "textMode",
                                                         "F7").toString()),
                    {CanvasMode::textCreate},
                    false);
    setupMainAction(QIcon::fromTheme("nullCreate"),
                    tr("Add Null Object"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "nullMode",
                                                         "F8").toString()),
                    {CanvasMode::nullCreate},
                    false);
    setupMainAction(boneSvgIcon(ThemeSupport::getIconSize(64).width()),
                    tr("Add Bone"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "boneMode",
                                                         "B").toString()),
                    {CanvasMode::boneCreate},
                    false);
    setupMainAction(svgToolIcon(QStringLiteral(":/icons/bone_pose.svg"),
                              ThemeSupport::getIconSize(64).width()),
                    tr("Bone Pose"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "bonePose",
                                                         "P").toString()),
                    {CanvasMode::bonePose},
                    false);
    setupMainAction(svgToolIcon(QStringLiteral(":/icons/bone_bind.svg"),
                              ThemeSupport::getIconSize(64).width()),
                    tr("Bone Bind"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "boneBind",
                                                         "J").toString()),
                    {CanvasMode::boneBind},
                    false);
    setupMainAction(svgToolIcon(QStringLiteral(":/icons/bone_parent.svg"),
                              ThemeSupport::getIconSize(64).width()),
                    tr("Bone Parent"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "boneParent",
                                                         "L").toString()),
                    {CanvasMode::boneParent},
                    false);
    setupMainAction(svgToolIcon(QStringLiteral(":/icons/bone_select.svg"),
                              ThemeSupport::getIconSize(64).width()),
                    tr("Bone Select"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "boneSelect",
                                                         "K").toString()),
                    {CanvasMode::boneSelect},
                    false);
    // scene camera (AE-like): orbit/pan/zoom the whole composition -
    // LMB orbit, Shift+LMB pan, Ctrl+LMB zoom
    setupMainAction(svgToolIcon(QStringLiteral(":/icons/camera_tool.svg"),
                              ThemeSupport::getIconSize(64).width()),
                    tr("Camera"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "camera",
                                                         "C").toString()),
                    {CanvasMode::camera},
                    false);
    setupMainAction(ThemeSupport::themedToolIcon("pick",
                                                 ThemeSupport::getThemeColorRed(),
                                                 64),
                    tr("Color Pick Mode"),
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         "pickMode",
                                                         "F9").toString()),
                    {CanvasMode::pickFillStroke,
                     CanvasMode::pickFillStrokeEvent},
                    false);

    // local pivot
    mLocalPivot = new QAction(mDocument.fLocalPivot ?
                                  QIcon::fromTheme("pivotLocal") :
                                  QIcon::fromTheme("pivotGlobal"),
                              tr("Pivot Global / Local"),
                              mMain);
    mLocalPivot->setShortcut(QKeySequence(AppSupport::getSettings("shortcuts",
                                                                  "localPivot",
                                                                  "P").toString()));
    connect(mLocalPivot, &QAction::triggered,
            this, [this]() {
        mDocument.fLocalPivot = !mDocument.fLocalPivot;
        for (const auto& scene : mDocument.fScenes) { scene->updatePivot(); }
        Document::sInstance->actionFinished();
        mLocalPivot->setIcon(mDocument.fLocalPivot ?
                         QIcon::fromTheme("pivotLocal") :
                         QIcon::fromTheme("pivotGlobal"));
    });
    mGroupMain->addAction(mLocalPivot);

    // mask pen: draw DstIn mask shapes right above bitmap layers
    // (AE-style clipping); glyph U+29EA for the icon
    {
        QPixmap pm(64, 64);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont f = qApp->font();
        f.setPixelSize(46);
        f.setBold(true);
        p.setFont(f);
        p.setPen(qApp->palette().color(QPalette::WindowText));
        p.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, QChar(0x29EA));
        p.end();

        mMaskPen = new QAction(QIcon(pm),
                               tr("Mask Pen (draw shapes that clip layers below)"),
                               mMain);
        mMaskPen->setCheckable(true);
        connect(mMaskPen, &QAction::toggled,
                this, [this](const bool checked) {
            mDocument.fMaskPenActive = checked;
            if (checked) { mActions.setAddPointMode(); }
        });
        connect(&mDocument, &Document::canvasModeSet,
                this, [this](const CanvasMode mode) {
            if (mode != CanvasMode::pathCreate && mMaskPen->isChecked()) {
                mMaskPen->setChecked(false);
            }
        });
    }

    mMain->addActions(mGroupMain->actions());
    if (mMaskPen) { mMain->addAction(mMaskPen); }
}

void ToolBox::setupNodesAction(const QIcon &icon,
                               const QString &title,
                               const Node &node)
{

    const auto act = new QAction(icon, title, this);
    connect(act, &QAction::triggered,
            this, [this, node]() {
        switch (node) {
        case NodeConnect:
            mActions.connectPointsSlot();
            break;
        case NodeDisconnect:
            mActions.disconnectPointsSlot();
            break;
        case NodeMerge:
            mActions.mergePointsSlot();
            break;
        case NodeSplit:
            mActions.splitPointsSlot();
            break;
        case NodeFirst:
            mActions.makeSelectedNodeFirstSlot();
            break;
        case NodeReverse:
            mActions.reverseNodesOrderSlot();
            break;
        case NodeNew:
            mActions.subdivideSegments();
            break;
        case NodeRemove:
            if (mActions.deleteAction) {
                (*mActions.deleteAction)();
            }
            break;
        case NodeRemoveApprox:
            mActions.removePointsApprox();
            break;
        case NodeSymmetric:
            mActions.makePointCtrlsSymmetric();
            break;
        case NodeSmooth:
            mActions.makePointCtrlsSmooth();
            break;
        case NodeCorner:
            mActions.makePointCtrlsCorner();
            break;
        case NodeSegmentLine:
            mActions.makeSegmentLine();
            break;
        case NodeSegmentCurve:
            mActions.makeSegmentCurve();
            break;
        default:;
        }
    });
    mControls->addAction(mGroupNodes->addAction(act));
    ThemeSupport::setToolbarButtonStyle("ToolBoxButton", mControls, act);
}

void ToolBox::setupNodesActions()
{
    mGroupNodes->addAction(mControls->addSpacer(true, true));
    mControls->addAction(mGroupNodes->addAction(QIcon::fromTheme("pointTransform"),
                                                tr("Nodes")));

    setupNodesAction(QIcon::fromTheme("nodeNew"),
                     tr("New Node"), NodeNew);
    setupNodesAction(QIcon::fromTheme("nodeRemove"),
                     tr("Remove Node"), NodeRemove);
    setupNodesAction(QIcon::fromTheme("nodeRemoveApprox"),
                     tr("Remove Node Approx."), NodeRemoveApprox);
    setupNodesAction(QIcon::fromTheme("nodeConnect"),
                     tr("Connect Nodes"), NodeConnect);
    setupNodesAction(QIcon::fromTheme("nodeDisconnect"),
                     tr("Disconnect Nodes"), NodeDisconnect);
    setupNodesAction(QIcon::fromTheme("nodeMerge"),
                     tr("Merge Nodes"), NodeMerge);
    setupNodesAction(QIcon::fromTheme("nodeSplit"),
                     tr("Split Nodes"), NodeSplit);
    setupNodesAction(QIcon::fromTheme("nodeReverse"),
                     tr("Reverse Nodes Order"), NodeReverse);
    setupNodesAction(QIcon::fromTheme("nodeFirst"),
                     tr("Set First Node"), NodeFirst);
    setupNodesAction(QIcon::fromTheme("nodeSymmetric"),
                     tr("Symmetric Nodes"), NodeSymmetric);
    setupNodesAction(QIcon::fromTheme("nodeSmooth"),
                     tr("Smooth Nodes"), NodeSmooth);
    setupNodesAction(QIcon::fromTheme("nodeCorner"),
                     tr("Corner Nodes"), NodeCorner);
    setupNodesAction(QIcon::fromTheme("segmentLine"),
                     tr("Make Segment Line"), NodeSegmentLine);
    setupNodesAction(QIcon::fromTheme("segmentCurve"),
                     tr("Make Segment Curve"), NodeSegmentCurve);

    {
        // node visibility tool button
        const auto button = new QToolButton(mControls);
        button->setObjectName("ToolBoxButton");
        button->setPopupMode(QToolButton::InstantPopup);
        button->setFocusPolicy(Qt::NoFocus);
        const auto act1 = new QAction(QIcon::fromTheme("dissolvedAndNormalNodes"),
                                      tr("Dissolved and normal nodes"),
                                      this);
        act1->setData(0);
        const auto act2 = new QAction(QIcon::fromTheme("dissolvedNodesOnly"),
                                      tr("Dissolved nodes only"),
                                      this);
        act2->setData(1);
        const auto act3 = new QAction(QIcon::fromTheme("normalNodesOnly"),
                                      tr("Normal nodes only"),
                                      this);
        act3->setData(2);
        button->addAction(act1);
        button->addAction(act2);
        button->addAction(act3);
        button->setDefaultAction(act1);
        connect(button, &QToolButton::triggered,
                this, [this, button](QAction *act) {
            button->setDefaultAction(act);
            mDocument.fNodeVisibility = static_cast<NodeVisiblity>(act->data().toInt());
            Document::sInstance->actionFinished();
        });
        mGroupNodes->addAction(mControls->addWidget(button));
    }

    mGroupNodes->setEnabled(false);
    mGroupNodes->setVisible(false);
}

void ToolBox::setupDrawActions()
{
    mDrawPathMaxError = new QDoubleSlider(1, 200, 1, mControls, false);
    mDrawPathMaxError->setNumberDecimals(0);
    mDrawPathMaxError->setMinimumWidth(50);
    mDrawPathMaxError->setDisplayedValue(mDocument.fDrawPathMaxError);
    connect(mDrawPathMaxError, &QDoubleSlider::valueEdited,
            this, [this](const qreal value) {
        mDocument.fDrawPathMaxError = qFloor(value);
    });

    mDrawPathSmooth = new QDoubleSlider(1, 200, 1, mControls, false);
    mDrawPathSmooth->setNumberDecimals(0);
    mDrawPathSmooth->setMinimumWidth(50);
    mDrawPathSmooth->setDisplayedValue(mDocument.fDrawPathSmooth);
    connect(mDrawPathSmooth, &QDoubleSlider::valueEdited,
            this, [this](const qreal value) {
        mDocument.fDrawPathSmooth = qFloor(value);
    });

    const auto labelMax = new QLabel(tr("Max Error"), mControls);
    const auto labelSmooth = new QLabel(tr("Smooth"), mControls);

    mGroupDraw->addAction(mControls->addSpacer(true, true));
    mGroupDraw->addAction(mControls->addAction(QIcon::fromTheme("drawPath"),
                                               QString()));
    mGroupDraw->addAction(mControls->addWidget(labelMax));
    mGroupDraw->addAction(mControls->addSeparator());
    mGroupDraw->addAction(mControls->addWidget(mDrawPathMaxError));

    mGroupDraw->addAction(mControls->addSpacer(true, true));

    mGroupDraw->addAction(mControls->addAction(QIcon::fromTheme("drawPath"),
                                               QString()));
    mGroupDraw->addAction(mControls->addWidget(labelSmooth));
    mGroupDraw->addAction(mControls->addSeparator());
    mGroupDraw->addAction(mControls->addWidget(mDrawPathSmooth));
    mGroupDraw->addAction(mControls->addSpacer(true, true));

    {
        const auto act = new QAction(mDocument.fDrawPathManual ?
                                         QIcon::fromTheme("drawPathAutoUnchecked") :
                                         QIcon::fromTheme("drawPathAutoChecked"),
                                     tr("Automatic/Manual Fitting"),
                                     this);
        connect(act, &QAction::triggered,
                this, [this, act]() {
            mDocument.fDrawPathManual = !mDocument.fDrawPathManual;
            mDrawPathMaxError->setDisabled(mDocument.fDrawPathManual);
            act->setIcon(mDocument.fDrawPathManual ?
                             QIcon::fromTheme("drawPathAutoUnchecked") :
                             QIcon::fromTheme("drawPathAutoChecked"));
        });
        mControls->addAction(act);
        mGroupDraw->addAction(act);
        ThemeSupport::setToolbarButtonStyle("ToolBoxButton", mControls, act);
    }

    mGroupDraw->setEnabled(false);
    mGroupDraw->setVisible(false);
}

void ToolBox::setupColorPickerActions()
{
    mColorPickerButton = new QToolButton(mControls);
    mColorPickerButton->setObjectName("FlatButton");
    mColorPickerButton->setIcon(QIcon::fromTheme("pick"));
    mColorPickerLabel = new QLabel(mControls);

    mGroupColorPicker->addAction(mControls->addSpacer(true, true));
    mGroupColorPicker->addAction(mControls->addSeparator());
    mGroupColorPicker->addAction(mControls->addWidget(mColorPickerButton));
    mGroupColorPicker->addAction(mControls->addWidget(mColorPickerLabel));

    mGroupColorPicker->setVisible(false);
}

void ToolBox::setCurrentCanvas(Canvas * const target)
{
    mControls->setCurrentCanvas(target);
    if (mExtra) { mExtra->setCurrentCanvas(target); }
}

void ToolBox::setCanvasMode(const CanvasMode &mode)
{
    const bool boxMode = mode == CanvasMode::boxTransform;
    const bool pointMode = mode == CanvasMode::pointTransform;
    const bool drawMode = mode == CanvasMode::drawPath;
    const bool pickMode = mode == CanvasMode::pickFillStroke ||
                          mode == CanvasMode::pickFillStrokeEvent;

    mGroupNodes->setEnabled(pointMode);
    mGroupNodes->setVisible(pointMode);

    mGroupDraw->setEnabled(drawMode);
    mGroupDraw->setVisible(drawMode);

    mLocalPivot->setEnabled(boxMode || pointMode);

    if (mExtra) { mExtra->setCanvasMode(mode); }

    mGroupColorPicker->setVisible(pickMode);
    if (pickMode) { updateColorPicker(Qt::black); }
}

void ToolBox::updateColorPicker(const QColor &color)
{
    if (!mColorPickerButton || !mColorPickerLabel) { return; }
    mColorPickerButton->setStyleSheet(QString("background-color: %1;").arg(color.isValid() ?
                                                                               color.name() :
                                                                               "black"));
    mColorPickerLabel->setText(QString("&nbsp;"
                                       "<b>R:</b> %1 "
                                       "<b>G:</b> %2 "
                                       "<b>B:</b> %3")
                                   .arg(QString::number(color.isValid() ? color.redF() : 0., 'f', 3),
                                        QString::number(color.isValid() ? color.greenF() : 0., 'f', 3),
                                        QString::number(color.isValid() ? color.blueF() : 0., 'f', 3)));
}
