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

#include "timelinedockwidget.h"

#include <QKeyEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QPainter>
#include <QInputDialog>
#include <QDir>
#include <QFileInfo>
#include <cmath>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTime>
#include <QStatusBar>
#include <QTimer>
#include <QSvgRenderer>
#include <QFile>

#include <functional>

#include "Private/document.h"
#include "GUI/global.h"
#include "GUI/BoxesList/boxscrollwidget.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "GUI/keysview.h"
#include "Boxes/boundingbox.h"
#include "CacheHandlers/sceneframecontainer.h"
#include "skia/skiahelpers.h"
#include "Boxes/bone.h"
#include "Animators/transformanimator.h"
#include "Animators/complexanimator.h"
#include "Animators/animator.h"
#include "Animators/qrealanimator.h"
#include "Expressions/expression.h"
#include "Properties/property.h"
#include "swt_abstraction.h"

#include "mainwindow.h"
#include "canvaswindow.h"
#include "canvas.h"
#include "animationdockwidget.h"
#include "widgets/widgetstack.h"
#include "widgets/actionbutton.h"
#include "timelinewidget.h"
#include "widgets/framescrollbar.h"
#include "renderinstancesettings.h"
#include "layouthandler.h"
#include "memoryhandler.h"
#include "appsupport.h"

namespace {
// recursively gather every keyed QrealAnimator under prop (property
// channels, nested containers, child boxes included)
void collectKeyedQrealAnimators(Property* const prop,
                                QList<QrealAnimator*>& out) {
    const auto qa = dynamic_cast<QrealAnimator*>(prop);
    if (qa) {
        if (qa->anim_hasKeys()) out << qa;
        return;
    }
    const auto ca = dynamic_cast<ComplexAnimator*>(prop);
    if (ca) {
        const int n = ca->ca_getNumberOfChildren();
        for (int i = 0; i < n; i++) {
            collectKeyedQrealAnimators(ca->ca_getChildAt<Property>(i), out);
        }
    }
}

// user-supplied SVG loop glyphs rasterized via QSvgRenderer (the
// iconengines plugin is not deployed, a plain QIcon on .svg would
// not render)
QIcon svgLoopIcon(const QString& qrcPath)
{
    QIcon result;
    QFile f(qrcPath);
    if (!f.open(QIODevice::ReadOnly)) { return result; }
    QSvgRenderer renderer(f.readAll());
    if (!renderer.isValid()) { return result; }
    for (const int size : {64, 32, 24, 16}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p, QRectF(0, 0, size, size));
        p.end();
        result.addPixmap(pm);
    }
    return result;
}
}

TimelineDockWidget::TimelineDockWidget(Document& document,
                                       LayoutHandler * const layoutH,
                                       MainWindow * const parent)
    : QWidget(parent)
    , mDocument(document)
    , mMainWindow(parent)
    , mTimelineLayout(layoutH->timelineLayout())
    , mToolBar(nullptr)
    , mFrameStartSpin(nullptr)
    , mFrameEndSpin(nullptr)
    , mFrameRewindAct(nullptr)
    , mFrameFastForwardAct(nullptr)
    , mCurrentFrameSpinAct(nullptr)
    , mCurrentFrameSpin(nullptr)
    , mRenderProgressAct(nullptr)
    , mRenderProgress(nullptr)
    , mStepPreviewTimer(nullptr)
    , mPausedPreviewState({false, 0})
{
    connect(RenderHandler::sInstance, &RenderHandler::previewFinished,
            this, &TimelineDockWidget::previewFinished);
    connect(RenderHandler::sInstance, &RenderHandler::previewBeingPlayed,
            this, &TimelineDockWidget::previewBeingPlayed);
    connect(RenderHandler::sInstance, &RenderHandler::previewBeingRendered,
            this, &TimelineDockWidget::previewBeingRendered);
    connect(RenderHandler::sInstance, &RenderHandler::previewPaused,
            this, &TimelineDockWidget::previewPaused);

    connect(Document::sInstance, &Document::canvasModeSet,
            this, &TimelineDockWidget::updateButtonsVisibility);

    setFocusPolicy(Qt::NoFocus);

    mMainLayout = new QVBoxLayout(this);
    setLayout(mMainLayout);
    mMainLayout->setSpacing(0);
    mMainLayout->setMargin(0);

    mFrameRewindAct = new QAction(QIcon::fromTheme("rewind"),
                                  tr("Rewind"),
                                  this);
    mFrameRewindAct->setShortcut(QKeySequence(AppSupport::getSettings("shortcuts",
                                                                      "rewind",
                                                                      "Shift+Left").toString()));
    mFrameRewindAct->setData(tr("Go to First Frame"));
    connect(mFrameRewindAct, &QAction::triggered,
            this, [this]() {
        const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        const bool jumpFrame = (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::AltModifier)) == (Qt::ShiftModifier | Qt::AltModifier);
        if (jumpFrame) { // Go to previous scene quarter
            jumpToIntermediateFrame(false);
        } else { // Go to First Frame
            scene->anim_setAbsFrame(scene->getFrameRange().fMin);
            mDocument.actionFinished();
        }
    });

    mFrameFastForwardAct = new QAction(QIcon::fromTheme("fastforward"),
                                       tr("Fast Forward"),
                                       this);
    mFrameFastForwardAct->setShortcut(QKeySequence(AppSupport::getSettings("shortcuts",
                                                                           "fastForward",
                                                                           "Shift+Right").toString()));
    mFrameFastForwardAct->setData(tr("Go to Last Frame"));
    connect(mFrameFastForwardAct, &QAction::triggered,
            this, [this]() {
        const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        const bool jumpFrame = (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::AltModifier)) == (Qt::ShiftModifier | Qt::AltModifier);
        if (jumpFrame) { // Go to next scene quarter
            jumpToIntermediateFrame(true);
        } else { // Go to Last Frame
            scene->anim_setAbsFrame(scene->getFrameRange().fMax);
            mDocument.actionFinished();
        }
    });

    mPlayFromBeginningButton = new QAction(QIcon::fromTheme("preview"),
                                           tr("Play Preview From Start"),
                                           this);
    connect(mPlayFromBeginningButton, &QAction::triggered,
            this, [this]() {
        /*const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        scene->anim_setAbsFrame(scene->getFrameRange().fMin);
        renderPreview();*/
        const auto state = RenderHandler::sInstance->currentPreviewState();
        setPreviewFromStart(state);
    });

    mPlayButton = new QAction(QIcon::fromTheme("play"),
                              tr("Play Preview"),
                              this);

    mStopButton = new QAction(QIcon::fromTheme("stop"),
                              tr("Stop Preview"),
                              this);

    connect(mStopButton, &QAction::triggered,
            this, &TimelineDockWidget::interruptPreview);

    mLoopButton = new QAction(QIcon::fromTheme("preview_loop"),
                              tr("Loop Preview"),
                              this);
    mLoopButton->setCheckable(true);
    connect(mLoopButton, &QAction::triggered,
            this, &TimelineDockWidget::setLoop);

    // snapshot: quick PNG export of the current canvas frame
    {
        QPixmap pm(64, 64);
        pm.fill(Qt::transparent);
        QPainter g(&pm);
        g.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer renderer(
                    QStringLiteral(":/icons/camera_tool.svg"));
        renderer.render(&g, QRectF(0, 0, 64, 64));
        g.end();
        // white version (toolbar icon convention)
        QPainter w(&pm);
        w.setCompositionMode(QPainter::CompositionMode_SourceIn);
        w.fillRect(pm.rect(), Qt::white);
        w.end();
        mSnapshotButton = new QAction(pm, tr("Snapshot PNG"), this);
        mSnapshotButton->setToolTip(tr(
                "Export the current frame as a 100% resolution PNG "
                "(snapshot path is configurable in Preferences, "
                "default: Desktop)"));
        connect(mSnapshotButton, &QAction::triggered,
                this, &TimelineDockWidget::snapshotCurrentFrame);
    }

    // AE-style view toggles: action/title safe guides + transparency
    // checkerboard background (view only, never rendered/exported)
    {
        QPixmap sf(64, 64);
        sf.fill(Qt::transparent);
        QPainter p(&sf);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(255, 255, 255, 220));
        pen.setWidthF(4.);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawRect(QRectF(6, 14, 52, 36));
        pen.setColor(QColor(255, 220, 90, 220));
        pen.setWidthF(4.);
        p.setPen(pen);
        p.drawRect(QRectF(14, 21, 36, 22));
        p.end();
        mSafeFramesButton = new QAction(sf, tr("Safe Frames"), this);
        mSafeFramesButton->setCheckable(true);
        mSafeFramesButton->setToolTip(tr(
                "Show action/title safe frames (90%/80%)"));
        connect(mSafeFramesButton, &QAction::triggered,
                this, [this](const bool checked) {
            const auto scene = *mDocument.fActiveScene;
            if(scene) scene->setSafeFramesVisible(checked);
        });

        // mask everything outside the canvas - the same toggle as the
        // view menu "Clip to Scene" (shortcut C), surfaced as a button
        // next to the safe-frames toggle (user-supplied SVG icon)
        {
            QSvgRenderer renderer(
                        QStringLiteral(":/icons/clip_canvas.svg"));
            QPixmap cm(64, 64);
            cm.fill(Qt::transparent);
            if (renderer.isValid()) {
                QPainter cp(&cm);
                renderer.render(&cp, QRectF(4, 4, 56, 56));
                cp.end();
            }
            mClipCanvasButton = new QAction(cm, tr("遮蔽画布外"), this);
        }
        mClipCanvasButton->setCheckable(true);
        mClipCanvasButton->setToolTip(tr(
                "遮蔽掉画布之外的内容（同视图菜单 Clip to Scene，快捷键 C）"));
        connect(mClipCanvasButton, &QAction::triggered,
                this, [this](const bool checked) {
            const auto scene = *mDocument.fActiveScene;
            if(scene) scene->setClipToCanvas(checked);
        });

        // canvas rulers toggle (viewport overlay strips), user SVG icon
        QPixmap rl(64, 64);
        rl.fill(Qt::transparent);
        {
            QSvgRenderer rr(QStringLiteral(":/icons/canvas_rulers.svg"));
            if (rr.isValid()) {
                QPainter rp(&rl);
                rr.render(&rp, QRectF(6, 6, 52, 52));
                rp.end();
            }
        }
        mRulersButton = new QAction(rl, tr("画布标尺"), this);
        mRulersButton->setCheckable(true);
        mRulersButton->setChecked(AppSupport::getSettings(
                    QStringLiteral("view"), QStringLiteral("rulers"),
                    true).toBool());
        mRulersButton->setToolTip(tr("显示画布标尺（像素坐标，跟随缩放平移）"));
        connect(mRulersButton, &QAction::triggered,
                this, [this](const bool checked) {
            CanvasWindow::setRulersVisible(checked);
            const auto scene = *mDocument.fActiveScene;
            if (scene) { emit scene->requestUpdate(); }
        });

        QPixmap tg(64, 64);
        tg.fill(Qt::transparent);
        QPainter t(&tg);
        // clean 2x2 checkerboard (no rounded sub-patches - those left
        // antialiased seams that read as stray pixels), inset so the
        // icon matches the visual weight of the neighbouring icons
        const QRectF grid(14, 14, 36, 36);
        const qreal half = 18.;
        t.setPen(Qt::NoPen);
        t.setBrush(QColor(160, 160, 160));
        t.drawRect(grid);                       // gray base (TL + BR)
        t.setBrush(QColor(255, 255, 255));
        t.drawRect(QRectF(grid.left() + half, grid.top(),
                          half, half));         // white TR
        t.drawRect(QRectF(grid.left(), grid.top() + half,
                          half, half));         // white BL
        QPen border(QColor(255, 255, 255, 210));
        border.setWidthF(2.5);
        t.setPen(border);
        t.setBrush(Qt::NoBrush);
        t.drawRect(grid.adjusted(-1.25, -1.25, 1.25, 1.25));
        t.end();
        mTransparencyGridButton = new QAction(tg, tr("Transparency Grid"),
                                              this);
        mTransparencyGridButton->setCheckable(true);
        mTransparencyGridButton->setToolTip(tr(
                "Toggle the transparency grid background"));
        connect(mTransparencyGridButton, &QAction::triggered,
                this, [this](const bool checked) {
            const auto scene = *mDocument.fActiveScene;
            if(scene) scene->setTransparencyGrid(checked);
        });

        // bone auto-freeze pose: when on, every bone pose operation
        // keys ALL channels of the bone (Moho freeze-pose semantics)
        // instead of just the touched one - pins the pose so staggered
        // per-channel keys cannot drift
        QPixmap fp(64, 64);
        fp.fill(Qt::transparent);
        QPainter f(&fp);
        f.setRenderHint(QPainter::Antialiasing);
        // snowflake: three crossing arms + center dot
        QPen fpen(QColor(255, 255, 255, 230));
        fpen.setWidthF(3.5);
        fpen.setCapStyle(Qt::RoundCap);
        f.setPen(fpen);
        const QPointF c(32, 32);
        const qreal r = 20.;
        for(int k = 0; k < 3; k++) {
            const qreal a = qDegreesToRadians(30. + k*60.);
            f.drawLine(c + QPointF(qCos(a)*r, qSin(a)*r),
                       c - QPointF(qCos(a)*r, qSin(a)*r));
        }
        f.setBrush(QColor(255, 255, 255, 230));
        f.setPen(Qt::NoPen);
        f.drawEllipse(c, 4.5, 4.5);
        f.end();
        mFreezePoseButton = new QAction(fp, tr("Freeze Pose"), this);
        mFreezePoseButton->setCheckable(true);
        mFreezePoseButton->setToolTip(tr(
                "Auto freeze pose: key ALL bone channels on every pose "
                "edit (pinned poses, no drift)"));
        Bone::sAutoFreezePose = AppSupport::getSettings(
                    QStringLiteral("bones"),
                    QStringLiteral("autoFreezePose"),
                    false).toBool();
        mFreezePoseButton->setChecked(Bone::sAutoFreezePose);
        connect(mFreezePoseButton, &QAction::triggered,
                this, [this](const bool checked) {
            Bone::sAutoFreezePose = checked;
            AppSupport::setSettings(QStringLiteral("bones"),
                                    QStringLiteral("autoFreezePose"),
                                    checked);
        });

        // key loop modes: toggles right of the freeze-pose button;
        // enabling applies a loop-out expression (AE loopOut alike,
        // see Expression::parseLoopHeader) to every keyed animator of
        // the selected layers, disabling clears those expressions
        // again; the expression lives in the project, so it survives
        // saves and is per-property undoable.
        // applyLoopExpressions/clearLoopExpressions are MEMBER funcs:
        // constructor-local lambdas dangle after the ctor returns
        const auto setCheckedQuiet = [](QAction* const act,
                                        const bool checked) {
            act->blockSignals(true);
            act->setChecked(checked);
            act->blockSignals(false);
        };

        // user-supplied SVG glyphs: forward = single cycle arrow,
        // ping-pong = swap arrows, skip = loop with a jump bar
        mLoopPoseFwdButton = new QAction(
                    svgLoopIcon(QStringLiteral(":/icons/loop_fwd.svg")),
                    tr("Loop Keys"), this);
        mLoopPoseFwdButton->setCheckable(true);
        mLoopPoseFwdButton->setToolTip(tr(
                "Cycle keyframed animation forward after the last key "
                "(1,2,3 -> 1,2,3,1,...); applies a loop expression to "
                "every keyed property of the selected layers, bones "
                "included; click again to remove"));
        connect(mLoopPoseFwdButton, &QAction::triggered,
                this, [this, setCheckedQuiet](const bool checked) {
            if (checked) {
                setCheckedQuiet(mLoopPosePingPongButton, false);
                setCheckedQuiet(mLoopPoseSkipButton, false);
                applyLoopExpressions(QStringLiteral("//loop:cycle"));
            } else clearLoopExpressions();
        });

        // ping-pong: two opposing arrows
        mLoopPosePingPongButton = new QAction(
                    svgLoopIcon(QStringLiteral(":/icons/loop_pingpong.svg")),
                    tr("Ping-Pong Loop"), this);
        mLoopPosePingPongButton->setCheckable(true);
        mLoopPosePingPongButton->setToolTip(tr(
                "Bounce keyframed animation back and forth after the "
                "last key (1,2,3 -> 1,2,3,2,1,...); applies a loop "
                "expression to every keyed property of the selected "
                "layers, bones included; click again to remove"));
        connect(mLoopPosePingPongButton, &QAction::triggered,
                this, [this, setCheckedQuiet](const bool checked) {
            if (checked) {
                setCheckedQuiet(mLoopPoseFwdButton, false);
                setCheckedQuiet(mLoopPoseSkipButton, false);
                applyLoopExpressions(QStringLiteral("//loop:pingpong"));
            } else clearLoopExpressions();
        });

        // skip cycle: loop with a jump bar over the skipped keys
        mLoopPoseSkipButton = new QAction(
                    svgLoopIcon(QStringLiteral(":/icons/loop_skip.svg")),
                    tr("Skip Loop"), this);
        mLoopPoseSkipButton->setCheckable(true);
        mLoopPoseSkipButton->setToolTip(tr(
                "Cycle keyframed animation skipping the given number of "
                "leading keys (keys 1,2,3, skip 1 -> cycles 2,3); the "
                "amount is asked for when enabled; applies a loop "
                "expression to every keyed property of the selected "
                "layers, bones included; click again to remove"));
        connect(mLoopPoseSkipButton, &QAction::triggered,
                this, [this, setCheckedQuiet](const bool checked) {
            if (!checked) {
                clearLoopExpressions();
                return;
            }
            setCheckedQuiet(mLoopPoseFwdButton, false);
            setCheckedQuiet(mLoopPosePingPongButton, false);
            bool ok = false;
            const int skip = QInputDialog::getInt(
                        this, tr("Skip Loop"),
                        tr("Number of leading keys to skip:"),
                        1, 1, 999, 1, &ok);
            if (!ok) {
                setCheckedQuiet(mLoopPoseSkipButton, false);
                return;
            }
            applyLoopExpressions(
                        QStringLiteral("//loop:skip=%1").arg(skip));
        });
    }

    // match canvas: uniformly scale every selected layer so its width
    // or height matches the canvas, then center it (AE fit-to-comp
    // alike); one click = one undo step for the whole batch
    {
        // canvas frame + white double arrow along the matched axis
        const auto makeMatchIcon = [](const bool horizontal) {
            QPixmap pm(64, 64);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            QPen frame(QColor(255, 255, 255, 170));
            frame.setWidthF(3.);
            p.setBrush(Qt::NoBrush);
            p.setPen(frame);
            p.drawRoundedRect(QRectF(6, 6, 52, 52), 6, 6);
            QPen arrow(QColor(255, 255, 255, 240));
            arrow.setWidthF(4.5);
            arrow.setCapStyle(Qt::RoundCap);
            p.setPen(arrow);
            const QPointF head(7.5, 7.5);
            if (horizontal) {
                p.drawLine(QPointF(15, 32), QPointF(49, 32));
                p.drawLine(QPointF(15, 32), QPointF(22, 32) + QPointF(0, -head.y()));
                p.drawLine(QPointF(15, 32), QPointF(22, 32) + QPointF(0, head.y()));
                p.drawLine(QPointF(49, 32), QPointF(42, 32) + QPointF(0, -head.y()));
                p.drawLine(QPointF(49, 32), QPointF(42, 32) + QPointF(0, head.y()));
            } else {
                p.drawLine(QPointF(32, 15), QPointF(32, 49));
                p.drawLine(QPointF(32, 15), QPointF(32, 22) + QPointF(-head.x(), 0));
                p.drawLine(QPointF(32, 15), QPointF(32, 22) + QPointF(head.x(), 0));
                p.drawLine(QPointF(32, 49), QPointF(32, 42) + QPointF(-head.x(), 0));
                p.drawLine(QPointF(32, 49), QPointF(32, 42) + QPointF(head.x(), 0));
            }
            p.end();
            return pm;
        };
        mMatchCanvasWidthButton = new QAction(makeMatchIcon(true),
                                              tr("Match Canvas Width"),
                                              this);
        mMatchCanvasWidthButton->setToolTip(tr(
                "Uniformly scale each selected layer so its width "
                "matches the canvas width, then center it on the "
                "canvas (aspect ratio preserved)"));
        connect(mMatchCanvasWidthButton, &QAction::triggered,
                this, [this]() { matchSelectedToCanvas(true); });

        mMatchCanvasHeightButton = new QAction(makeMatchIcon(false),
                                               tr("Match Canvas Height"),
                                               this);
        mMatchCanvasHeightButton->setToolTip(tr(
                "Uniformly scale each selected layer so its height "
                "matches the canvas height, then center it on the "
                "canvas (aspect ratio preserved)"));
        connect(mMatchCanvasHeightButton, &QAction::triggered,
                this, [this]() { matchSelectedToCanvas(false); });
    }

    mStepPreviewTimer = new QTimer(this);

    mFrameStartSpin = new FrameSpinBox(this);
    mFrameStartSpin->setKeyboardTracking(false);
    mFrameStartSpin->setObjectName("LeftSpinBox");
    mFrameStartSpin->setAlignment(Qt::AlignHCenter);
    mFrameStartSpin->setFocusPolicy(Qt::ClickFocus);
    mFrameStartSpin->setToolTip(tr("Scene frame start"));
    mFrameStartSpin->setRange(0, INT_MAX);
    connect(mFrameStartSpin,
            &QSpinBox::editingFinished,
            this, [this]() {
            const auto scene = *mDocument.fActiveScene;
            if (!scene) { return; }
            auto range = scene->getFrameRange();
            int frame = mFrameStartSpin->value();
            if (range.fMin == frame) { return; }
            if (frame >= range.fMax) {
                mFrameStartSpin->setValue(range.fMin);
                return;
            }
            range.fMin = frame;
            scene->setFrameRange(range);
    });

    mFrameEndSpin = new FrameSpinBox(this);
    mFrameEndSpin->setKeyboardTracking(false);
    mFrameEndSpin->setAlignment(Qt::AlignHCenter);
    mFrameEndSpin->setFocusPolicy(Qt::ClickFocus);
    mFrameEndSpin->setToolTip(tr("Scene frame end"));
    mFrameEndSpin->setRange(1, INT_MAX);
    connect(mFrameEndSpin,
            &QSpinBox::editingFinished,
            this, [this]() {
            const auto scene = *mDocument.fActiveScene;
            if (!scene) { return; }
            auto range = scene->getFrameRange();
            int frame = mFrameEndSpin->value();
            if (range.fMax == frame) { return; }
            if (frame <= range.fMin) {
                mFrameEndSpin->setValue(range.fMax);
                return;
            }
            range.fMax = frame;
            scene->setFrameRange(range);
    });

    mCurrentFrameSpin = new FrameSpinBox(this);
    mCurrentFrameSpin->setKeyboardTracking(false);
    mCurrentFrameSpin->setAlignment(Qt::AlignHCenter);
    mCurrentFrameSpin->setObjectName(QString::fromUtf8("SpinBoxNoButtons"));
    mCurrentFrameSpin->setFocusPolicy(Qt::ClickFocus);
    mCurrentFrameSpin->setToolTip(tr("Current frame"));
    mCurrentFrameSpin->setRange(-INT_MAX, INT_MAX);
    connect(mCurrentFrameSpin,
            &QSpinBox::editingFinished,
            this, [this]() { gotoFrame(mCurrentFrameSpin->value()); });
    connect(mCurrentFrameSpin,
            &FrameSpinBox::wheelValueChanged,
            this, &TimelineDockWidget::gotoFrame);

    const auto mPrevKeyframeAct = new QAction(QIcon::fromTheme("prev_keyframe"),
                                              QString(),
                                              this);
    mPrevKeyframeAct->setToolTip(tr("Previous Keyframe"));
    mPrevKeyframeAct->setData(mPrevKeyframeAct->toolTip());
    connect(mPrevKeyframeAct, &QAction::triggered,
            this, [this]() {
        if (setPrevKeyframe()) {
            mDocument.actionFinished();
        }
    });

    const auto mNextKeyframeAct = new QAction(QIcon::fromTheme("next_keyframe"),
                                              QString(),
                                              this);
    mNextKeyframeAct->setToolTip(tr("Next Keyframe"));
    mNextKeyframeAct->setData(mNextKeyframeAct->toolTip());
    connect(mNextKeyframeAct, &QAction::triggered,
            this, [this]() {
        if (setNextKeyframe()) {
            mDocument.actionFinished();
        }
    });

    mSetInPointAct = new QAction(QIcon::fromTheme("range-in"),
                                  tr("Set Layer In Point (Alt+[)"),
                                  this);
    mSetInPointAct->setToolTip(tr("Set Layer In Point (Alt+[)"));
    mSetInPointAct->setData(mSetInPointAct->toolTip());
    connect(mSetInPointAct, &QAction::triggered, this, [this]() {
        const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        scene->setSelectedBoxesInPoint();
        mDocument.actionFinished();
    });

    mSetOutPointAct = new QAction(QIcon::fromTheme("range-out"),
                                   tr("Set Layer Out Point (Alt+])"),
                                   this);
    mSetOutPointAct->setToolTip(tr("Set Layer Out Point (Alt+])"));
    mSetOutPointAct->setData(mSetOutPointAct->toolTip());
    connect(mSetOutPointAct, &QAction::triggered, this, [this]() {
        const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        scene->setSelectedBoxesOutPoint();
        mDocument.actionFinished();
    });

    mSplitClipAct = new QAction(QIcon::fromTheme("cut"),
                                tr("Split Clip (Ctrl+Shift+D)"),
                                this);
    mSplitClipAct->setToolTip(tr("Split Clip at Current Frame"));
    mSplitClipAct->setData(mSplitClipAct->toolTip());
    connect(mSplitClipAct, &QAction::triggered, this, [this]() {
        splitClip();
        mDocument.actionFinished();
    });

    mToolBar = new QToolBar(this);
    mToolBar->setMovable(false);

    mRenderProgress = new QProgressBar(this);
    mRenderProgress->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Expanding);
    mRenderProgress->setFixedWidth(mCurrentFrameSpin->width());
    mRenderProgress->setFormat(tr("Cache %p%"));

    eSizesUI::widget.add(mToolBar, [this](const int size) {
        //mRenderProgress->setFixedHeight(eSizesUI::button);
        mToolBar->setIconSize(QSize(size, size));
    });

    // start layout
    mToolBar->addWidget(mFrameStartSpin);

    // timeline zoom slider: logarithmic map over the viewed frame span
    // (right = zoom in), acting on the current scene's timeline
    mZoomSlider = new QSlider(Qt::Horizontal, this);
    mZoomSlider->setRange(0, 100);
    mZoomSlider->setValue(50);
    mZoomSlider->setFixedWidth(110);
    mZoomSlider->setMaximumHeight(18);
    mZoomSlider->setToolTip(tr("时间轴缩放（右=放大，等价 Ctrl+滚轮）"));
    connect(mZoomSlider, &QSlider::valueChanged, this, [this](const int v) {
        const auto scene = *mDocument.fActiveScene;
        if (!scene) return;
        const int maxSpan = qMax(20, scene->getFrameRange().span());
        const int minSpan = 10;
        const qreal t = 1. - v/100.;
        const int span = qBound(minSpan,
                                qRound(minSpan*std::pow(1.*maxSpan/minSpan, t)),
                                maxSpan);
        const auto tw = mTimelineLayout->currentWidget() ?
                    mTimelineLayout->currentWidget()->findChild<TimelineWidget*>() :
                    nullptr;
        if (tw) tw->setTimelineZoomSpan(span);
    });
    mToolBar->addWidget(mZoomSlider);

    addSpacer();

    mToolBar->addAction(mFrameRewindAct);
    mToolBar->addAction(mPrevKeyframeAct);
    mToolBar->addAction(mNextKeyframeAct);
    mToolBar->addAction(mFrameFastForwardAct);

    mToolBar->addSeparator();
    mToolBar->addAction(mSetInPointAct);
    mToolBar->addAction(mSetOutPointAct);
    mToolBar->addAction(mSplitClipAct);
    mToolBar->addSeparator();

    mRenderProgressAct = mToolBar->addWidget(mRenderProgress);
    mCurrentFrameSpinAct = mToolBar->addWidget(mCurrentFrameSpin);

    mToolBar->addAction(mPlayFromBeginningButton);
    mToolBar->addAction(mPlayButton);
    mToolBar->addAction(mStopButton);
    mToolBar->addAction(mLoopButton);
    mToolBar->addAction(mSnapshotButton);
    mToolBar->addAction(mSafeFramesButton);
    mToolBar->addSeparator();
    mToolBar->addAction(mClipCanvasButton);
    mToolBar->addAction(mRulersButton);
    mToolBar->addAction(mTransparencyGridButton);
    mToolBar->addAction(mFreezePoseButton);
    mToolBar->addSeparator();
    mToolBar->addAction(mLoopPoseFwdButton);
    mToolBar->addAction(mLoopPosePingPongButton);
    mToolBar->addAction(mLoopPoseSkipButton);
    mToolBar->addSeparator();
    mToolBar->addAction(mMatchCanvasWidthButton);
    mToolBar->addAction(mMatchCanvasHeightButton);

    addSpacer();

    mToolBar->addWidget(mFrameEndSpin);
    // end layout

    mRenderProgressAct->setVisible(false);

    mMainWindow->cmdAddAction(mFrameRewindAct);
    mMainWindow->cmdAddAction(mPrevKeyframeAct);
    mMainWindow->cmdAddAction(mNextKeyframeAct);
    mMainWindow->cmdAddAction(mFrameFastForwardAct);
    mMainWindow->cmdAddAction(mSetInPointAct);
    mMainWindow->cmdAddAction(mSetOutPointAct);
    mMainWindow->cmdAddAction(mSplitClipAct);
    mMainWindow->cmdAddAction(mPlayFromBeginningButton);
    mMainWindow->cmdAddAction(mPlayButton);
    mMainWindow->cmdAddAction(mStopButton);
    mMainWindow->cmdAddAction(mLoopButton);

    mMainLayout->addWidget(mToolBar);
    mMainLayout->addSpacing(2);

    mPlayFromBeginningButton->setEnabled(false);
    mPlayButton->setEnabled(false);
    mStopButton->setEnabled(false);

    connect(&mDocument, &Document::activeSceneSet,
            this, [this](Canvas* const scene) {
        mPlayFromBeginningButton->setEnabled(scene);
        mPlayButton->setEnabled(scene);
        mStopButton->setEnabled(scene);
    });

    mMainLayout->addWidget(mTimelineLayout);

    previewFinished();

    connect(&mDocument, &Document::activeSceneSet,
            this, &TimelineDockWidget::updateSettingsForCurrentCanvas);

    connect(mStepPreviewTimer, &QTimer::timeout,
            this, &TimelineDockWidget::stepPreview);

    setupPropertyShortcuts();
}

void TimelineDockWidget::updateFrameRange(const FrameRange &range)
{
    mRenderProgress->setRange(range.fMin, range.fMax);
    if (range.fMin != mFrameStartSpin->value()) {
        mFrameStartSpin->blockSignals(true);
        mFrameStartSpin->setValue(range.fMin);
        mFrameStartSpin->blockSignals(false);
    }
    if (range.fMax != mFrameEndSpin->value()) {
        mFrameEndSpin->blockSignals(true);
        mFrameEndSpin->setValue(range.fMax);
        mFrameEndSpin->blockSignals(false);
    }
}

void TimelineDockWidget::handleCurrentFrameChanged(int frame)
{
    mCurrentFrameSpin->setValue(frame);
    if (mRenderProgress->isVisible()) { mRenderProgress->setValue(frame); }
}

void TimelineDockWidget::showRenderStatus(bool show)
{
    if (!show) { mRenderProgress->setValue(0); }
    mCurrentFrameSpinAct->setVisible(!show);
    mRenderProgressAct->setVisible(show);
}

void TimelineDockWidget::addSpacer()
{
    const auto spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding,
                          QSizePolicy::Minimum);
    mToolBar->addWidget(spacer);
}

void TimelineDockWidget::addBlankAction()
{
    const auto act = mToolBar->addAction(QString());
    act->setEnabled(false);
}

void TimelineDockWidget::setLoop(const bool loop)
{
    RenderHandler::sInstance->setLoop(loop);
}

// quick PNG export of the current frame at FULL (100%) resolution:
// when the preview runs at a lower resolution (default 50%), the
// scene resolution is bumped to 1.0, the fresh frame is awaited and
// the previous resolution restored. Destination: the snapshot path
// from the preferences (default: Desktop)
void TimelineDockWidget::snapshotCurrentFrame()
{
    const auto scene = *mDocument.fActiveScene;
    if(!scene) return;
    const int frame = scene->getCurrentFrame();
    const auto status = [this](const QString& msg) {
        mMainWindow->statusBar()->showMessage(msg, 5000);
    };
    // destination: preferences setting, fallback Desktop
    QString dir = AppSupport::getSettings(QStringLiteral("snapshots"),
                                          QStringLiteral("dir")).toString();
    if(dir.isEmpty() || !QDir(dir).exists()) {
        dir = QStandardPaths::writableLocation(
                    QStandardPaths::DesktopLocation);
    }
    static const QRegularExpression badChars(
                QStringLiteral("[\\\\/:*?\"<>|]"));
    QString sceneName = scene->prp_getName();
    sceneName.replace(badChars, QStringLiteral("_"));
    const QString name = QStringLiteral("%1_f%2_%3.png")
            .arg(sceneName)
            .arg(frame)
            .arg(QTime::currentTime().toString(QStringLiteral("HHmmss")));
    const QString path = dir + QStringLiteral("/") + name;

    const qreal savedRes = scene->getResolution();
    const auto saveAndReport = [status, path](const sk_sp<SkImage>& img) {
        SkiaHelpers::saveImage(path, img,
                               SkEncodedImageFormat::kPNG, 100);
        if(QFile::exists(path)) {
            status(tr("Snapshot saved: %1").arg(path));
        } else {
            status(tr("Failed to save snapshot: %1").arg(path));
        }
    };

    const auto contRaw = scene->getSceneFramesHandler().atFrame(frame);
    const auto frameCont = dynamic_cast<SceneFrameContainer*>(contRaw);
    const sk_sp<SkImage> img = frameCont ? frameCont->getImage() : nullptr;
    if(img && (savedRes > 0.999 || frameCont->fResolution > 0.999)) {
        // already at full resolution
        saveAndReport(img);
        return;
    }
    if(savedRes > 0.999) {
        status(tr("No rendered frame available yet - wait for the "
                  "preview to render this frame"));
        return;
    }
    // bump the scene to 100% and wait for the fresh full-res frame;
    // actionFinished() actually SCHEDULES the re-render (the video
    // export does the same setResolution + actionFinished dance)
    scene->setResolution(1.);
    mDocument.actionFinished();
    QPointer<Canvas> sceneQ(scene);
    const bool restoreRes = true;
    const qreal resToRestore = savedRes;
    auto* const timer = new QTimer(this);
    auto tries = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this,
            [this, timer, sceneQ, frame, path, restoreRes, resToRestore,
             tries, status, saveAndReport]() {
        if(!sceneQ) {
            timer->stop();
            timer->deleteLater();
            return;
        }
        const auto cont = sceneQ->getSceneFramesHandler().atFrame(frame);
        const auto fc = dynamic_cast<SceneFrameContainer*>(cont);
        const bool ready = fc && fc->getImage() &&
                           fc->fResolution > 0.999;
        if(!ready) {
            if(++(*tries) > 100) { // ~10s timeout
                timer->stop();
                timer->deleteLater();
                sceneQ->setResolution(resToRestore);
                mDocument.actionFinished();
                status(tr("Snapshot timed out - the frame did not "
                          "render in time"));
            }
            return;
        }
        timer->stop();
        timer->deleteLater();
        saveAndReport(fc->getImage());
        sceneQ->setResolution(resToRestore);
        mDocument.actionFinished();
    });
    timer->start(100);
    status(tr("Rendering snapshot at 100% resolution..."));
}

void TimelineDockWidget::spaceToggle()
{
    const auto state = RenderHandler::sInstance->currentPreviewState();
    // diagnostic: distinguishes "Space never reached this slot" from
    // "reached but wrong branch" when users report dead Space keys
    qWarning() << "[SPACE] spaceToggle state=" << int(state)
               << "stepTimer=" << mStepPreviewTimer->isActive();
    // Space = play <-> full stop: any preview activity (rendering,
    // playing, paused) stops the preview completely; the next press
    // starts playback again
    if (state == PreviewState::rendering ||
        state == PreviewState::playing ||
        state == PreviewState::paused) {
        interruptPreview();
    } else if (mStepPreviewTimer->isActive()) {
        pausePreview();
    } else {
        // AE-style start: when the range ahead is not fully cached,
        // warm the cache first (visible progress, auto-plays when
        // done); with everything cached play straight from memory
        bool started = false;
        if (eSettings::instance().fPreviewCache) {
            const auto scene = *mDocument.fActiveScene;
            bool warm = false;
            if (scene) {
                const int cur = scene->anim_getCurrentAbsFrame();
                const int max = scene->getFrameRange().fMax;
                warm = scene->getSceneFramesHandler()
                            .firstEmptyFrameAtOrAfter(cur) > max &&
                       scene->sceneFramesCacheIsFresh();
            }
            if (warm) {
                started = RenderHandler::sInstance->playPreview();
                if (!started) { renderPreview(); }
            } else {
                renderPreview();
                started = true;
            }
        } else {
            started = playPreview();
        }
        qWarning() << "[SPACE] start playPreview=" << started
                   << "activeScene="
                   << (*mDocument.fActiveScene ? "yes" : "null");
    }
}

bool TimelineDockWidget::processKeyPress(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const auto state = RenderHandler::sInstance->currentPreviewState();
    const bool jumpFrame = (mods & (Qt::ShiftModifier | Qt::AltModifier)) == (Qt::ShiftModifier | Qt::AltModifier);
    if (key == Qt::Key_Escape) { // stop playback
        if (state != PreviewState::stopped ||
            mStepPreviewTimer->isActive()) { interruptPreview(); }
        else { return false; }

    } else if (key == Qt::Key_Space && (mods & Qt::ShiftModifier)) { // play from first frame
        /*const auto scene = *mDocument.fActiveScene;
        if (!scene) { return false; }
        if (state != PreviewState::stopped) { interruptPreview(); }
        scene->anim_setAbsFrame(scene->getFrameRange().fMin);
        renderPreview();*/
        if (!setPreviewFromStart(state)) { return false; }
    } else if (key == Qt::Key_Space) { // play <-> full stop
        // keep both Space paths (window shortcut and timeline keys)
        // on the exact same behavior
        spaceToggle();
    } else if (key == Qt::Key_K && mods == Qt::NoModifier) { // split clip
        splitClip();
    } else if (key == Qt::Key_M) { // set marker
        setMarker();
    } else if (key == Qt::Key_I || key == Qt::Key_O) { // set frame in/out
        switch(key) {
            case Qt::Key_I: setIn(); break;
            case Qt::Key_O: setOut(); break;
            default:;
        }
    } else if (key == Qt::Key_Right && !(mods & Qt::ControlModifier)) {
        if (jumpFrame) { // jump to next scene quarter
            jumpToIntermediateFrame(true);
        } else { // next frame
            mDocument.incActiveSceneFrame();
        }
    } else if (key == Qt::Key_Left && !(mods & Qt::ControlModifier)) {
        if (jumpFrame) { // jump to previous scene quarter
            jumpToIntermediateFrame(false);
        } else { // previous frame
            mDocument.decActiveSceneFrame();
        }
    } else if (key == Qt::Key_Down && !(mods & Qt::ControlModifier)) { // previous keyframe
        /*const auto scene = *mDocument.fActiveScene;
        if (!scene) { return false; }
        int targetFrame;
        const int frame = mDocument.getActiveSceneFrame();
        if (scene->anim_prevRelFrameWithKey(frame, targetFrame)) {
            mDocument.setActiveSceneFrame(targetFrame);
        }*/
        if (!setPrevKeyframe()) { return false; }
    } else if (key == Qt::Key_Up && !(mods & Qt::ControlModifier)) { // next keyframe
        /*const auto scene = *mDocument.fActiveScene;
        if (!scene) { return false; }
        int targetFrame;
        const int frame = mDocument.getActiveSceneFrame();
        if (scene->anim_nextRelFrameWithKey(frame, targetFrame)) {
            mDocument.setActiveSceneFrame(targetFrame);
        }*/
        if (!setNextKeyframe()) { return false; }
    } else {
        return false;
    }
    return true;
}

void TimelineDockWidget::previewFinished()
{
    mPausedPreviewState.first = false;
    if (const auto scene = *mDocument.fActiveScene) {
        scene->setGizmosSuppressed(false);
    }
    //setPlaying(false);
    mFrameStartSpin->setEnabled(true);
    mFrameEndSpin->setEnabled(true);
    mCurrentFrameSpinAct->setEnabled(true);
    showRenderStatus(false);
    mPlayFromBeginningButton->setDisabled(false);
    mStopButton->setDisabled(true);
    mPlayButton->setIcon(QIcon::fromTheme("play"));
    mPlayButton->setText(tr("Play Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::renderPreview);
}

void TimelineDockWidget::previewBeingPlayed()
{
    if (const auto scene = *mDocument.fActiveScene) {
        scene->setGizmosSuppressed(true);
    }
    mFrameStartSpin->setEnabled(false);
    mFrameEndSpin->setEnabled(false);
    mCurrentFrameSpinAct->setEnabled(false);
    showRenderStatus(false);
    mPlayFromBeginningButton->setDisabled(true);
    mStopButton->setDisabled(false);
    mPlayButton->setIcon(QIcon::fromTheme("pause"));
    mPlayButton->setText(tr("Pause Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::pausePreview);
}

void TimelineDockWidget::previewBeingRendered()
{
    mFrameStartSpin->setEnabled(false);
    mFrameEndSpin->setEnabled(false);
    mCurrentFrameSpinAct->setEnabled(false);
    showRenderStatus(true);
    mPlayFromBeginningButton->setDisabled(true);
    mStopButton->setDisabled(false);
    mPlayButton->setIcon(QIcon::fromTheme("play"));
    mPlayButton->setText(tr("Play Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::playPreview);
}

void TimelineDockWidget::previewPaused()
{
    mPausedPreviewState = {true, mDocument.getActiveSceneFrame()};

    if (const auto scene = *mDocument.fActiveScene) {
        scene->setGizmosSuppressed(false);
    }
    mFrameStartSpin->setEnabled(true);
    mFrameEndSpin->setEnabled(true);
    mCurrentFrameSpinAct->setEnabled(true);
    showRenderStatus(false);
    mPlayFromBeginningButton->setDisabled(true);
    mStopButton->setDisabled(false);
    mPlayButton->setIcon(QIcon::fromTheme("play"));
    mPlayButton->setText(tr("Resume Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::resumePreview);
}

bool TimelineDockWidget::setPreviewFromStart(PreviewState state)
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    if (state != PreviewState::stopped) { interruptPreview(); }
    scene->anim_setAbsFrame(scene->getFrameRange().fMin);
    renderPreview();
    return true;
}

bool TimelineDockWidget::setNextKeyframe()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    int targetFrame;
    const int frame = mDocument.getActiveSceneFrame();
    if (scene->anim_nextRelFrameWithKey(frame, targetFrame)) {
        mDocument.setActiveSceneFrame(targetFrame);
    }
    return true;
}

bool TimelineDockWidget::setPrevKeyframe()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    int targetFrame;
    const int frame = mDocument.getActiveSceneFrame();
    if (scene->anim_prevRelFrameWithKey(frame, targetFrame)) {
        mDocument.setActiveSceneFrame(targetFrame);
    }
    return true;
}

void TimelineDockWidget::resumePreview()
{
    if (eSettings::instance().fPreviewCache) {
        if (mPausedPreviewState.first) {
            const int frame = mDocument.getActiveSceneFrame();
            if (mPausedPreviewState.second != frame) {
                qDebug() << "set new start frame for preview" << frame;
                RenderHandler::sInstance->setPreviewFrame(frame);
                mPausedPreviewState.first = false;
            }
        }
        RenderHandler::sInstance->resumePreview();
    } else { setStepPreviewStart(); }
}

void TimelineDockWidget::setStepPreviewStop(const bool pause)
{
    mStepPreviewTimer->stop();
    if (pause) { previewPaused(); }
    else { previewFinished(); }
}

void TimelineDockWidget::setStepPreviewStart()
{
    if (eSettings::instance().fPreviewCache) { return; }

    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }

    if (mStepPreviewTimer->isActive()) {
        mStepPreviewTimer->stop();
    }

    const auto state = RenderHandler::sInstance->currentPreviewState();
    if (state != PreviewState::stopped) {
        RenderHandler::sInstance->interruptPreview();
    }

    int fps = scene->getFps();
    mStepPreviewTimer->setInterval(1000 / fps);
    mStepPreviewTimer->start();
    previewBeingPlayed();
}

void TimelineDockWidget::gotoFrame(int frame)
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    scene->anim_setAbsFrame(frame);
    mDocument.actionFinished();
}

void TimelineDockWidget::updateButtonsVisibility(const CanvasMode mode)
{
    Q_UNUSED(mode)
}

void TimelineDockWidget::pausePreview()
{
    if (eSettings::instance().fPreviewCache) {
        RenderHandler::sInstance->pausePreview();
    } else { setStepPreviewStop(); }
}

bool TimelineDockWidget::playPreview()
{
    if (eSettings::instance().fPreviewCache) {
        return RenderHandler::sInstance->playPreview();
    }
    setStepPreviewStart();
    return true;
}

void TimelineDockWidget::renderPreview()
{
    if (eSettings::instance().fPreviewCache) {
        RenderHandler::sInstance->renderPreview();
    } else { setStepPreviewStart(); }
}

void TimelineDockWidget::interruptPreview()
{
    if (eSettings::instance().fPreviewCache) {
        RenderHandler::sInstance->interruptPreview();
    } else { setStepPreviewStop(); }
}

void TimelineDockWidget::updateSettingsForCurrentCanvas(Canvas* const canvas)
{
    if (!canvas) { return; }

    // keep the clip-to-canvas toggle in sync with the scene state (the
    // view-menu entry and the C shortcut can change it elsewhere)
    mClipCanvasButton->blockSignals(true);
    mClipCanvasButton->setChecked(canvas->clipToCanvas());
    mClipCanvasButton->blockSignals(false);

    const auto range = canvas->getFrameRange();
    updateFrameRange(range);
    handleCurrentFrameChanged(canvas->anim_getCurrentAbsFrame());

    mCurrentFrameSpin->setDisplayTimeCode(canvas->getDisplayTimecode());
    mFrameStartSpin->setDisplayTimeCode(canvas->getDisplayTimecode());
    mFrameEndSpin->setDisplayTimeCode(canvas->getDisplayTimecode());

    mCurrentFrameSpin->updateFps(canvas->getFps());
    mFrameStartSpin->updateFps(canvas->getFps());
    mFrameEndSpin->updateFps(canvas->getFps());

    connect(canvas, &Canvas::fpsChanged,
            this, [this](const qreal fps) {
        mCurrentFrameSpin->updateFps(fps);
        mFrameStartSpin->updateFps(fps);
        mFrameEndSpin->updateFps(fps);
        if (mStepPreviewTimer->isActive()) {
            mStepPreviewTimer->setInterval(1000 / fps);
        }
    });
    connect(canvas, &Canvas::displayTimeCodeChanged,
            this, [this](const bool enabled) {
        mCurrentFrameSpin->setDisplayTimeCode(enabled);
        mFrameStartSpin->setDisplayTimeCode(enabled);
        mFrameEndSpin->setDisplayTimeCode(enabled);
    });

    connect(canvas,
            &Canvas::newFrameRange,
            this, [this](const FrameRange range) {
            updateFrameRange(range);
    });
    connect(canvas, &Canvas::currentFrameChanged,
            this, &TimelineDockWidget::handleCurrentFrameChanged);

    update(); // needed for loaded markers
}

void TimelineDockWidget::stopPreview()
{
    const auto state = RenderHandler::sInstance->currentPreviewState();
    switch (state) {
    case PreviewState::paused:
        interruptPreview();
        break;
    case PreviewState::playing:
    case PreviewState::rendering:
        interruptPreview();
        renderPreview();
        break;
    default:;
    }
}

void TimelineDockWidget::setIn()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto frame = scene->getCurrentFrame();
    if (scene->getFrameOut().enabled) {
        if (frame >= scene->getFrameOut().frame) { return; }
    }
    bool apply = frame == 0 ? true : (scene->getFrameIn().frame != frame);
    scene->setFrameIn(apply, frame);
}

void TimelineDockWidget::setOut()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto frame = scene->getCurrentFrame();
    if (scene->getFrameIn().enabled) {
        if (frame <= scene->getFrameIn().frame) { return; }
    }
    bool apply = (scene->getFrameOut().frame != frame);
    scene->setFrameOut(apply, frame);
}

void TimelineDockWidget::setMarker()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto frame = scene->getCurrentFrame();
    scene->setMarker(frame);
}

void TimelineDockWidget::splitClip()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    scene->splitAction();
}

void TimelineDockWidget::jumpToIntermediateFrame(bool forward) {
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    
    const auto range = scene->getFrameRange();
    const int currentFrame = scene->anim_getCurrentAbsFrame();
    const int totalFrames = range.fMax - range.fMin;
    const int quarterFrame = range.fMin + qRound(totalFrames * 0.25);
    const int middleFrame = range.fMin + qRound(totalFrames * 0.5);
    const int threeQuarterFrame = range.fMin + qRound(totalFrames * 0.75);
    
    if (forward) {
        if (currentFrame < quarterFrame) {
            scene->anim_setAbsFrame(quarterFrame);
        } else if (currentFrame < middleFrame) {
            scene->anim_setAbsFrame(middleFrame);
        } else if (currentFrame < threeQuarterFrame) {
            scene->anim_setAbsFrame(threeQuarterFrame);
        } else {
            scene->anim_setAbsFrame(range.fMax);
        }
    } else {
        if (currentFrame > threeQuarterFrame) {
            scene->anim_setAbsFrame(threeQuarterFrame);
        } else if (currentFrame > middleFrame) {
            scene->anim_setAbsFrame(middleFrame);
        } else if (currentFrame > quarterFrame) {
            scene->anim_setAbsFrame(quarterFrame);
        } else {
            scene->anim_setAbsFrame(range.fMin);
        }
    }
    mDocument.actionFinished();
}

void TimelineDockWidget::stepPreview()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    int currentFrame = scene->anim_getCurrentAbsFrame();
    int nextFrame = currentFrame + 1;

    if (scene->getFrameIn().enabled && currentFrame < scene->getFrameIn().frame) {
        nextFrame = scene->getFrameIn().frame;
    }

    int frameOut = scene->getFrameRange().fMax;
    if (scene->getFrameOut().enabled) {
        frameOut = scene->getFrameOut().frame;
    }

    if (nextFrame > frameOut) {
        if (mLoopButton->isChecked()) {
            nextFrame = scene->getFrameRange().fMin;
            if (scene->getFrameIn().enabled) {
                nextFrame = scene->getFrameIn().frame;
            }
        } else {
            mStepPreviewTimer->stop();
            previewFinished();
            return;
        }
    }
    scene->anim_setAbsFrame(nextFrame);
    mDocument.actionFinished();
}

namespace {

// map which -> the transform sub-property (AE: A anchor/pivot,
// P position, S scale, R rotation, T opacity)
Property *transformSubProp(BoundingBox * const box, const int which)
{
    const auto trans = box->getBoxTransformAnimator();
    if (!trans) { return nullptr; }
    switch (which) {
    case 0: return trans->getPivotAnimator();
    case 1: return trans->getPosAnimator();
    case 2: return trans->getScaleAnimator();
    case 3: return trans->getRotAnimator();
    case 4: return trans->getOpacityAnimator();
    default: return nullptr;
    }
}

// make the property row visible in the timeline layer tree and
// expand every collapsed ancestor (transform group, box, groups)
void revealPropertyRow(TimelineWidget * const tw, Property * const prop)
{
    if (!tw || !prop) { return; }
    const auto list = tw->boxesListWidget();
    if (!list) { return; }
    const int widId = list->swtWidgetId();
    prop->SWT_show();
    const auto abs = prop->SWT_getAbstractionForWidget(widId);
    if (!abs) { return; }
    for (auto p = abs->getParent(); p; p = p->getParent()) {
        if (!p->contentVisible()) { p->setContentVisible(true); }
    }
}

// fold the transform group row so the solo property row hides
// inside it (second shortcut press = fold back)
void collapsePropertyRow(TimelineWidget * const tw, Property * const prop)
{
    if (!tw || !prop) { return; }
    const auto list = tw->boxesListWidget();
    if (!list) { return; }
    const auto abs = prop->SWT_getAbstractionForWidget(list->swtWidgetId());
    if (!abs) { return; }
    const auto parent = abs->getParent();
    if (parent) { parent->setContentVisible(false); }
}

// recursively collect properties that have animation keys
void collectAnimatedProps(ComplexAnimator * const ca,
                          QList<Property*> &out)
{
    const int n = ca->ca_getNumberOfChildren();
    for (int i = 0; i < n; i++) {
        const auto child = ca->ca_getChildAt(i);
        if (!child) { continue; }
        const auto anim = enve_cast<Animator*>(child);
        if (anim && anim->anim_hasKeys()) { out.append(child); }
        const auto complex = enve_cast<ComplexAnimator*>(child);
        if (complex) { collectAnimatedProps(complex, out); }
    }
}

} // namespace

void TimelineDockWidget::showTransformProperty(const int which)
{
    const auto scene = *mDocument.fActiveScene;
    // the stack holds one TimelineWrapperNode per scene; the actual
    // TimelineWidget is its central widget
    const auto tw = mTimelineLayout->currentWidget() ?
                mTimelineLayout->currentWidget()->findChild<TimelineWidget*>() :
                nullptr;
    if (!scene || !tw) { return; }
    // when keys are selected and the cursor is over the keys view,
    // S/G mean "move keys" in KeysView; let that win
    const auto kv = tw->keysView();
    if (kv && kv->hasSelectedKeysForShortcut() &&
            kv->underMouse()) { return; }
    const auto boxes = scene->getSelectedBoxesList();
    if (boxes.isEmpty()) { return; }
    for (const auto box : boxes) {
        const auto prop = transformSubProp(box, which);
        if (!prop) continue;
        if (box->swtSoloActiveProp() == prop) {
            // second press on the same key: fold back and restore
            // all rows hidden by the solo display
            box->swtRestoreSoloHidden();
            collapsePropertyRow(tw, prop);
        } else {
            // expand first (may trigger solo restore), then solo-hide
            revealPropertyRow(tw, prop);
            box->swtSoloHideAllExcept(prop);
        }
    }
    mDocument.actionFinished();
}

void TimelineDockWidget::showAnimatedProperties()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene || !mTimelineLayout->currentWidget()) { return; }
    const auto tw = mTimelineLayout->currentWidget()
            ->findChild<TimelineWidget*>();
    if (!tw) { return; }
    const auto kv = tw->keysView();
    if (kv && kv->hasSelectedKeysForShortcut() &&
            kv->underMouse()) { return; }
    const auto boxes = scene->getSelectedBoxesList();
    if (boxes.isEmpty()) { return; }
    for (const auto box : boxes) {
        // AE U behavior: show only properties with keyframes;
        // expand first (may trigger solo restore), then hide rest
        QList<Property*> animated;
        collectAnimatedProps(box, animated);
        for (const auto prop : animated) {
            revealPropertyRow(tw, prop);
        }
        box->swtHideWithoutKeys();
    }
    mDocument.actionFinished();
}

void TimelineDockWidget::applyLoopExpressions(const QString& header)
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) return;
    QList<QrealAnimator*> targets;
    const auto selected = scene->getSelectedBoxesList();
    for (const auto& box : selected) {
        collectKeyedQrealAnimators(box, targets);
    }
    int applied = 0;
    for (const auto& anim : targets) {
        const auto script = header + QStringLiteral("\nreturn value;");
        try {
            // the $frame binding is required for playback: it emits
            // currentValueChanged on every frame change, which is the
            // only thing that re-evaluates the expression for the
            // current-frame cache (the $value binding alone never
            // signals, the canvas would stay frozen)
            auto expr = Expression::sCreate(
                        QStringLiteral("value = $value;\nframe = $frame;\n"),
                        QString(),
                        script, anim,
                        Expression::sQrealAnimatorTester);
            anim->setExpressionAction(expr);
            applied++;
        } catch (const std::exception& e) {
            qWarning() << "[loop] expression failed for"
                       << anim->prp_getName()
                       << ":" << e.what();
        } catch (...) {
            qWarning() << "[loop] expression failed for"
                       << anim->prp_getName();
        }
    }
    qWarning() << "[loop] apply" << header
               << "selected boxes:" << selected.count()
               << "keyed animators:" << targets.count()
               << "applied:" << applied;
    Document::sInstance->actionFinished();
    scene->updateAllBoxes(UpdateReason::userChange);
}

void TimelineDockWidget::clearLoopExpressions()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) return;
    QList<QrealAnimator*> targets;
    for (const auto& box : scene->getSelectedBoxesList()) {
        collectKeyedQrealAnimators(box, targets);
    }
    for (const auto& anim : targets) {
        if (!anim->hasExpression()) continue;
        if (!anim->getExpressionScriptString()
                 .startsWith(QLatin1String("//loop:"))) continue;
        anim->clearExpressionAction();
    }
    Document::sInstance->actionFinished();
    scene->updateAllBoxes(UpdateReason::userChange);
}

void TimelineDockWidget::matchSelectedToCanvas(const bool byWidth)
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) return;
    if (scene->getSelectedBoxesList().isEmpty()) return;
    scene->scaleSelectedBoxesToCanvas(byWidth);
    Document::sInstance->actionFinished();
    scene->updateAllBoxes(UpdateReason::userChange);
}

void TimelineDockWidget::setupPropertyShortcuts()
{
    const auto makeShortcut = [this](const QString &id,
                                     const std::function<void()> &fn) {
        const auto seq = AppSupport::getSettings("shortcuts",
                                                 id, "").toString();
        if (seq.isEmpty()) { return; }
        const auto keySeq = QKeySequence(seq);
        // user-configured property shortcuts take priority over
        // hardcoded action shortcuts (e.g. View->Timeline uses T);
        // with two identical shortcuts Qt treats them as ambiguous
        // and neither fires, so clear the conflicting one
        const auto clearConflicts = [this, keySeq]() {
            if (!mMainWindow) return;
            const auto acts = mMainWindow->findChildren<QAction*>();
            for (const auto a : acts) {
                if (a && a->shortcut() == keySeq) {
                    a->setShortcut(QKeySequence());
                }
            }
        };
        clearConflicts();
        // toolbox/menu actions may be created after this dock, run
        // again once everything is built
        QTimer::singleShot(0, this, clearConflicts);
        const auto sc = new QShortcut(keySeq, this);
        connect(sc, &QShortcut::activated, this, fn);
    };
    makeShortcut("showAnchor",   [this]() { showTransformProperty(0); });
    makeShortcut("showPosition", [this]() { showTransformProperty(1); });
    makeShortcut("showScale",    [this]() { showTransformProperty(2); });
    makeShortcut("showRotation", [this]() { showTransformProperty(3); });
    makeShortcut("showOpacity",  [this]() { showTransformProperty(4); });
    makeShortcut("showAnimated", [this]() { showAnimatedProperties(); });
}
