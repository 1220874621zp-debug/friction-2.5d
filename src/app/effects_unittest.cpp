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

#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <memory>
#include <QCryptographicHash>
#include <QtConcurrent/QtConcurrentMap>
#include <QTranslator>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <iostream>
#include <cassert>
#include <cstring>

#include "RasterEffects/rastereffectsinclude.h"
#include "RasterEffects/rastereffectcollection.h"
#include "Boxes/rectangle.h"
#include "Properties/emimedata.h"
#include "swt_abstraction.h"
#include "Private/document.h"
#include "Private/Tasks/taskscheduler.h"
#include "Private/esettings.h"
#include "hardwareinfo.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "GUI/BoxesList/levelseffectdialog.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "GUI/BoxesList/boxscroller.h"
#include "optimalscrollarena/scrollwidget.h"
#include "RasterEffects/effectpreview.h"
#include "Properties/comboboxproperty.h"
#include "Psd/psdfile.h"
#include "include/core/SkBitmap.h"

#include "themesupport.h"
#include "AI/mcpserver.h"
#include "AI/mcpdispatcher.h"
#include "textanimpresets.h"
#include "layeranimpresets.h"
#include "GUI/mainwindow.h"
#include "GUI/canvaswindow.h"
#include "GUI/timelinedockwidget.h"
#include "GUI/RenderWidgets/renderwidget.h"
#include "GUI/RenderWidgets/renderinstancewidget.h"
#include "renderinstancesettings.h"
#include "renderhandler.h"

// Headless test stubs for McpDispatcher GUI references
RenderHandler *RenderHandler::sInstance = nullptr;
TimelineDockWidget *MainWindow::getTimeLineWidget() { return nullptr; }
void TimelineDockWidget::spaceToggle() {}
Canvas *CanvasWindow::getCurrentCanvas() { return nullptr; }
void CanvasWindow::fitCanvasToSize(const bool&) {}
void CanvasWindow::setRulersVisible(bool) {}
RenderInstanceWidget *RenderWidget::addCanvasRenderInstance(Canvas *) { return nullptr; }
void RenderWidget::renderOnly(RenderInstanceWidget *) {}
int RenderWidget::count() { return 0; }
RenderWidget *MainWindow::renderWidget() const { return nullptr; }
// never actually dereferenced: addCanvasRenderInstance returns nullptr,
// so the dispatcher render path bails before touching the settings
RenderInstanceSettings &RenderInstanceWidget::getSettings() {
    // alignas must precede the decl-specifiers: gcc/clang reject it
    // between `static` and the type (MSVC silently tolerates)
    alignas(alignof(RenderInstanceSettings)) static char buf[sizeof(RenderInstanceSettings)];
    return reinterpret_cast<RenderInstanceSettings&>(buf);
}
// real-row coverage stubs: BoxSingleWidget's full dependency web
// (keys view, presets panel, leaf widgets) is not linked into this
// test binary - only the symbols the row actually touches at
// assignment time are stubbed
#include "GUI/keysview.h"
#include "GUI/effectspresetspanel.h"
#include "GUI/BoxesList/boolpropertywidget.h"
#include "GUI/BoxesList/boxtargetwidget.h"
#include "GUI/timelinehighlightwidget.h"
#include "Private/esettings.h"
void KeysView::clearKeySelection() {}
void KeysView::graphAddViewedAnimator(GraphAnimator*) {}
int KeysView::graphGetAnimatorId(GraphAnimator*) { return -1; }
bool KeysView::graphIsSelected(GraphAnimator*) { return false; }
void KeysView::graphRemoveViewedAnimator(GraphAnimator*) {}
QColor KeysView::sGetAnimatorColor(int) { return QColor(); }
const QString& EffectsPresetsPanel::sMimeFormat()
{ static const QString f; return f; }
EffectsPresetsPanel::EffectApplyFn EffectsPresetsPanel::takeEffectDrag(
        const QByteArray&) { return nullptr; }
// the levels dialog parents itself to the main window when one
// exists; headless there is none
MainWindow *MainWindow::sInstance = nullptr;
MainWindow *MainWindow::sGetInstance() { return sInstance; }
void MainWindow::toggleTopViewWindow() {}
bool MainWindow::isTopViewVisible() const { return false; }
const QMetaObject MainWindow::staticMetaObject = QMainWindow::staticMetaObject;
const QMetaObject CanvasWindow::staticMetaObject = GLWindow::staticMetaObject;

// frame-sequence hash for the parallel determinism test
static const auto gHashFrames = [](const QList<QImage>& frames) -> QByteArray {
    QCryptographicHash h(QCryptographicHash::Md5);
    for (const auto& f : frames) {
        const QImage c = f.convertToFormat(QImage::Format_ARGB32);
        h.addData(reinterpret_cast<const char*>(c.constBits()),
                  c.sizeInBytes());
    }
    return h.result();
};

// FRICTION_CANCEL_PROBE body: rapid edits must cancel the in-flight
// stale renders through the REAL scheduler - canceled tasks must not
// resurrect, the pool must drain (no hang), and the surviving render
// must carry the current state id. Needs a QGuiApplication instance
// (document/scene machinery), so it runs re-exec'd offscreen.
static void cancelStormBody()
{
    // eSettings singleton: Document's grid and the task-que hardware
    // preference read it (Test 10 does the same before any scheduling);
    // BoxRenderData's ctor reads eFilterSettings::sRender() (harness
    // does the same before queuing renders)
    static eSettings settingsCS(HardwareInfo::sCpuThreads(),
                                HardwareInfo::sRamKB());
    Q_UNUSED(settingsCS)
    eFilterSettings filterSettingsCS;
    TaskScheduler sched;
    Document doc(sched);
    const auto scene = doc.createNewScene(false);
    const auto box = enve::make_shared<RectangleBox>();
    // big canvas + big rect = heavy rounds, so renders are still in
    // flight when the next edit lands (the cancel path always runs)
    scene->setCanvasSize(2000, 2000);
    box->setTopLeftPos(QPointF(-800, -800));
    box->setBottomRightPos(QPointF(800, 800));
    scene->addContained(box);
    const auto coll = box->rasterEffectsCollection();
    coll->addChild(enve::make_shared<ThresholdEffect>());
    coll->addChild(enve::make_shared<SimpleChokerEffect>());
    coll->addChild(enve::make_shared<DesaturateEffect>());
    // push the choker off zero so its caller exists (heavy: blur)
    const auto choker = enve_cast<SimpleChokerEffect*>(coll->getChild(1));
    if (choker) {
        for (int i = 0; i < choker->ca_getNumberOfChildren(); i++) {
            const auto qa = enve_cast<QrealAnimator*>(choker->ca_getChildAt(i));
            if (qa) { qa->setCurrentBaseValue(35.0); break; }
        }
    }

    const auto pump = [&](const int maxMs) {
        QElapsedTimer t; t.start();
        while (!TaskScheduler::sAllTasksFinished()) {
            QCoreApplication::processEvents(
                        QEventLoop::AllEvents, 5);
            if (t.elapsed() > maxMs) {
                throw std::runtime_error("task pool never drained (hang)");
            }
        }
    };

    // rapid-edit storm: re-queue while the previous round's renders
    // are still in flight; each bump must cancel the stale ones
    for (int round = 0; round < 12; round++) {
        const auto thr = enve_cast<ThresholdEffect*>(coll->getChild(0));
        if (thr) {
            for (int i = 0; i < thr->ca_getNumberOfChildren(); i++) {
                const auto qa = enve_cast<QrealAnimator*>(thr->ca_getChildAt(i));
                if (qa) { qa->setCurrentBaseValue(20.0 + round); break; }
            }
        }
        box->planUpdate(UpdateReason::userChange);
        box->queTasks();
        // brief window so the round actually starts (and gets
        // canceled by the next bump)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
    pump(60000);

    // the LAST round must land: current render data exists and
    // carries the box's current state id
    const auto finalData = box->getCurrentRenderData(
                box->anim_getCurrentRelFrame());
    if (!finalData) {
        throw std::runtime_error("no current render data after storm");
    }
    if (finalData->fBoxStateId != box->getBoxStateId()) {
        throw std::runtime_error("surviving render is stale");
    }
    if (!finalData->fRenderedImage) {
        throw std::runtime_error("surviving render has no image");
    }
    std::cout << "(storm x12 drained, final state "
              << box->getBoxStateId() << ") ";
}

int main(int argc, char *argv[])
{
    // SWT_dropInto queries keyboard modifiers (Ctrl = duplicate drop),
    // which requires a GUI application instance; only the reorder probe
    // needs it - plain tests (ThemeSupport etc.) stay on QCoreApplication
    // which survives headless
    // the reorder probe only touches SWT data (QGuiApplication is
    // enough); the levels dialog probe builds real widgets; the cancel
    // probe needs the document/scene machinery (also QGuiApplication)
    QCoreApplication* appInstance = nullptr;
    if (qEnvironmentVariableIsSet("FRICTION_LEVELS_DIALOG_PROBE")) {
        appInstance = new QApplication(argc, argv);
    } else if (qEnvironmentVariableIsSet("FRICTION_REORDER_PROBE") ||
               qEnvironmentVariableIsSet("FRICTION_CANCEL_PROBE")) {
        appInstance = new QGuiApplication(argc, argv);
    } else {
        appInstance = new QCoreApplication(argc, argv);
    }
    const std::unique_ptr<QCoreApplication> app(appInstance);
    // surface qWarning from core (psd parser diagnostics) on stderr:
    // the default Windows handler drops them when no real console
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&,
                              const QString& msg) {
        fprintf(stderr, "[QT%d] %s\n", int(type), qPrintable(msg));
        fflush(stderr);
    });
    int passed = 0;
    int failed = 0;

    // cancel-probe mode: run ONLY the scheduler storm (ThemeSupport and
    // other tests crash or misbehave under a GUI instance)
    if (qEnvironmentVariableIsSet("FRICTION_CANCEL_PROBE")) {
        std::cout << "[RUNNING] Test 11: stale render cancellation (real scheduler) ... " << std::flush;
        try {
            cancelStormBody();
            std::cout << "PASSED" << std::endl;
            passed++;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
            failed++;
        } catch (...) {
            std::cout << "FAILED (unknown exception)" << std::endl;
            failed++;
        }
        return (failed == 0) ? 0 : 1;
    }

    const auto runTest = [&](const char* name, auto func) {
        std::cout << "[RUNNING] " << name << " ... ";
        try {
            func();
            std::cout << "PASSED" << std::endl;
            passed++;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
            failed++;
        } catch (...) {
            std::cout << "FAILED (unknown exception)" << std::endl;
            failed++;
        }
    };

    // Levels dialog probe: build a real effect under a real box,
    // open the PS-style editor, drag the input slider with synthetic
    // mouse events, double-click-reset, and screenshot the result
    if (qEnvironmentVariableIsSet("FRICTION_LEVELS_DIALOG_PROBE")) {
        static eSettings probeSettings(HardwareInfo::sCpuThreads(),
                                       HardwareInfo::sRamKB());
        Q_UNUSED(probeSettings)
        runTest("PROBE: Levels dialog open + drag + reset", [&]() {
            TaskScheduler probeSched;
            Document probeDoc(probeSched);
            const auto scene = probeDoc.createNewScene(false);
            const auto box = enve::make_shared<RectangleBox>();
            scene->addContained(box);
            const auto coll = box->rasterEffectsCollection();
            const auto eff = enve::make_shared<LevelsEffect>();
            coll->addChild(eff);
            if (coll->ca_getNumberOfChildren() != 1) {
                throw std::runtime_error("effect setup failed");
            }

            // restructured children: channel / input wrapper(3) /
            // output wrapper(2); the wrappers drive the panel sliders
            if (coll->ca_getNumberOfChildren() != 1 ||
                eff->ca_getNumberOfChildren() != 3) {
                throw std::runtime_error("unexpected levels children");
            }
            const auto inWrap = enve_cast<LevelsInputAnimator*>(
                        eff->ca_getChildAt(1));
            const auto outWrap = enve_cast<LevelsOutputAnimator*>(
                        eff->ca_getChildAt(2));
            if (!inWrap || inWrap->ca_getNumberOfChildren() != 3 ||
                !outWrap || outWrap->ca_getNumberOfChildren() != 2) {
                throw std::runtime_error("wrapper structure is off");
            }
            if (enve_cast<QrealAnimator*>(inWrap->ca_getChildAt(
                        LevelsInputAnimator::Black)) !=
                eff->getInBlackAnimator()) {
                throw std::runtime_error("input black child mismatch");
            }
            if (enve_cast<QrealAnimator*>(outWrap->ca_getChildAt(
                        LevelsOutputAnimator::White)) !=
                eff->getOutWhiteAnimator()) {
                throw std::runtime_error("output white child mismatch");
            }

            // the embedded panel-row slider: compact drag wiring with
            // the same index mapping BoxSingleWidget uses
            {
                auto row = std::make_unique<LevelsSlider>(
                            LevelsSlider::Input);
                row->setCompact(true);
                row->resize(300, 20);
                row->show();
                QPointer<QrealAnimator> dragAnim;
                const auto handleAnim = [&inWrap](const int idx) {
                    return enve_cast<QrealAnimator*>(
                                inWrap->ca_getChildAt(idx));
                };
                QObject::connect(row.get(), &LevelsSlider::handlePressed,
                                 &*row, [&](const int idx) {
                    dragAnim = handleAnim(idx);
                    dragAnim->prp_startTransform();
                });
                QObject::connect(row.get(), &LevelsSlider::valuesChanged,
                                 &*row, [&](const int idx, const qreal b,
                                            const qreal g, const qreal w) {
                    if (!dragAnim) { return; }
                    const qreal v = idx == LevelsSlider::Black ? b :
                                    idx == LevelsSlider::White ? w : g;
                    dragAnim->setCurrentBaseValue(
                                idx == LevelsSlider::Gamma ?
                                    qBound(LevelsEffect::sMinGamma, v,
                                           LevelsEffect::sMaxGamma) :
                                    qBound(0., v, 253.));
                });
                QObject::connect(row.get(), &LevelsSlider::handleReleased,
                                 &*row, [&](const int) {
                    if (dragAnim) {
                        dragAnim->prp_finishTransform();
                        dragAnim.clear();
                    }
                });
                row->setValues(0., 1., 255.);
                for (int i = 0; i < 10; i++) {
                    QCoreApplication::processEvents();
                }
                const int y = row->height() / 2;
                const qreal dragX = 4.5 + 0.4 * (row->width() - 11);
                const auto sendMouse2 = [&row](const QEvent::Type type,
                                               const QPointF& pos) {
                    QMouseEvent ev(type, pos, row->mapToGlobal(pos),
                                   Qt::LeftButton, Qt::LeftButton,
                                   Qt::NoModifier);
                    QApplication::sendEvent(&*row, &ev);
                };
                sendMouse2(QEvent::MouseButtonPress, QPointF(5, y));
                sendMouse2(QEvent::MouseMove, QPointF(dragX, y));
                sendMouse2(QEvent::MouseButtonRelease, QPointF(dragX, y));
                for (int i = 0; i < 10; i++) {
                    QCoreApplication::processEvents();
                }
                const qreal rowDragged = eff->getInBlackAnimator()
                        ->getCurrentBaseValue();
                if (rowDragged < 96. || rowDragged > 108.) {
                    throw std::runtime_error(
                            "panel slider drag is off: " +
                            std::to_string(rowDragged));
                }
                sendMouse2(QEvent::MouseButtonDblClick, QPointF(dragX, y));
                for (int i = 0; i < 10; i++) {
                    QCoreApplication::processEvents();
                }
                if (!qFuzzyIsNull(eff->getInBlackAnimator()
                                  ->getCurrentBaseValue())) {
                    throw std::runtime_error("panel slider reset is off");
                }
                // strip screenshot for eyeballing the compact rows
                QFrame strip;
                strip.setStyleSheet(
                            "QFrame{background:#2b2b2b;}");
                const auto lay = new QVBoxLayout(&strip);
                lay->setContentsMargins(8, 6, 8, 6);
                const auto mkRow = [&](const char* label,
                                       LevelsSlider* const sl) {
                    const auto h = new QHBoxLayout();
                    h->addWidget(new QLabel(QString::fromUtf8(label)));
                    sl->setCompact(true);
                    h->addWidget(sl, 1);
                    lay->addLayout(h);
                };
                mkRow("输入色阶", new LevelsSlider(LevelsSlider::Input));
                mkRow("输出色阶", new LevelsSlider(LevelsSlider::Output));
                static_cast<LevelsSlider*>(
                            strip.findChildren<LevelsSlider*>().at(0))
                        ->setValues(64., 1.6, 200.);
                static_cast<LevelsSlider*>(
                            strip.findChildren<LevelsSlider*>().at(1))
                        ->setValues(40., 1., 230.);
                strip.resize(320, 76);
                strip.show();
                for (int i = 0; i < 10; i++) {
                    QCoreApplication::processEvents();
                }
                strip.grab().save(QString::fromUtf8(
                            qgetenv("FRICTION_LEVELS_ROW_SHOT")));
                row->hide();
            }

            // REAL panel row: a genuine BoxSingleWidget fed the
            // wrapper abstraction - exactly the assignment that
            // crashed on the out-of-range child access
            {
                UpdateFuncs funcs;
                funcs.fContentUpdateIfIsCurrentRule = [](SWT_BoxRule){};
                funcs.fContentUpdateIfIsCurrentTarget = [](SingleWidgetTarget*, SWT_Target){};
                funcs.fContentUpdateIfSearchNotEmpty = [](){};
                funcs.fUpdateParentHeight = [](){};
                funcs.fUpdateVisibleWidgetsContent = [](){};
                const int pid = 7777;
                box->SWT_createAbstraction(funcs, pid);
                const auto inAbs = inWrap->SWT_getAbstractionForWidget(pid);
                const auto outAbs = outWrap->SWT_getAbstractionForWidget(pid);
                if (!inAbs || !outAbs) {
                    throw std::runtime_error("wrapper abstractions "
                                             "were not created");
                }

                // heap + explicit teardown order: ScrollWidget
                // adopts the scroller, so a stack scroller would be
                // destroyed twice
                auto scroller = std::unique_ptr<BoxScroller>(
                            new BoxScroller(nullptr));
                ScrollWidget* const sw = new ScrollWidget(
                            scroller.get(), nullptr);
                // NOTE: the scroller paint path needs a fully set-up
                // panel, so the parent chain stays unshown - the row
                // is driven and rendered directly instead
                auto row = std::make_unique<BoxSingleWidget>(
                            scroller.get());
                // wide enough that the name column + row buttons
                // still leave the slider a real span
                row->resize(640, 20);
                row->show();

                // input row: assignment must sync without throwing
                row->setTargetAbstraction(inAbs);
                for (int i = 0; i < 10; i++) {
                    QCoreApplication::processEvents();
                }
                LevelsSlider* inRow = row->findChild<LevelsSlider*>(
                            QStringLiteral("levelsInputRow"));
                if (!inRow || inRow->isHidden() ||
                    !qFuzzyIsNull(inRow->black()) ||
                    !qFuzzyCompare(inRow->white(), 255.)) {
                    throw std::runtime_error("input row not live-synced");
                }
                // drag its black handle to ~40% via the real row widget
                {
                    const int y = inRow->height() / 2;
                    const qreal dragX = 4.5 + 0.4 * (inRow->width() - 11);
                    const auto sendMouse3 = [inRow](const QEvent::Type type,
                                                    const QPointF& pos) {
                        QMouseEvent ev(type, pos, inRow->mapToGlobal(pos),
                                       Qt::LeftButton, Qt::LeftButton,
                                       Qt::NoModifier);
                        QApplication::sendEvent(inRow, &ev);
                    };
                    sendMouse3(QEvent::MouseButtonPress, QPointF(5, y));
                    sendMouse3(QEvent::MouseMove, QPointF(dragX, y));
                    sendMouse3(QEvent::MouseButtonRelease, QPointF(dragX, y));
                    for (int i = 0; i < 10; i++) {
                        QCoreApplication::processEvents();
                    }
                    const qreal v = eff->getInBlackAnimator()
                            ->getCurrentBaseValue();
                    if (v < 96. || v > 108.) {
                        throw std::runtime_error(
                                "real row drag is off: " +
                                std::to_string(v) + " w=" +
                                std::to_string(inRow->width()) +
                                " x=" + std::to_string(dragX));
                    }
                    sendMouse3(QEvent::MouseButtonDblClick,
                               QPointF(dragX, y));
                    for (int i = 0; i < 10; i++) {
                        QCoreApplication::processEvents();
                    }
                    if (!qFuzzyIsNull(eff->getInBlackAnimator()
                                      ->getCurrentBaseValue())) {
                        throw std::runtime_error("real row reset is off");
                    }
                }

                // output row: the two-child wrapper - the crash case
                row->setTargetAbstraction(outAbs);
                for (int i = 0; i < 10; i++) {
                    QCoreApplication::processEvents();
                }
                LevelsSlider* outRow = row->findChild<LevelsSlider*>(
                            QStringLiteral("levelsOutputRow"));
                if (!outRow || outRow->isHidden() ||
                    !qFuzzyIsNull(outRow->black()) ||
                    !qFuzzyCompare(outRow->white(), 255.)) {
                    throw std::runtime_error("output row not live-synced "
                                             "(original crash case)");
                }
                const QString shot = QString::fromUtf8(
                            qgetenv("FRICTION_LEVELS_PANEL_SHOT"));
                if (!shot.isEmpty()) {
                    row->grab().save(shot);
                }
                row->hide();
                row.reset();
                delete sw; // deletes the scroller it adopted
                scroller.release();
            }

            LevelsEffectDialog::openFor(eff.get());
            // the dialog is parented to MainWindow::sGetInstance()
            // which is null here - find it as a top-level instead
            LevelsEffectDialog* dlg = nullptr;
            for (const auto w : QApplication::topLevelWidgets()) {
                dlg = qobject_cast<LevelsEffectDialog*>(w);
                if (dlg) { break; }
            }
            if (!dlg) { throw std::runtime_error("dialog not created"); }

            dlg->resize(360, 420);
            dlg->show();
            for (int i = 0; i < 20; i++) { QCoreApplication::processEvents(); }

            const auto sliders = dlg->findChildren<LevelsSlider*>();
            if (sliders.count() < 2) {
                throw std::runtime_error("sliders not found");
            }
            auto* input = sliders.at(0);

            // drag the black handle (leftmost) to ~40% - kept off
            // the exact center so it never overlaps the gamma handle
            const int y = input->height() / 2;
            const qreal dragX = 6.5 + 0.4 * (input->width() - 13);
            const auto sendMouse = [input](const QEvent::Type type,
                                           const QPointF& pos) {
                QMouseEvent ev(type, pos, input->mapToGlobal(pos),
                               Qt::LeftButton, Qt::LeftButton,
                               Qt::NoModifier);
                QApplication::sendEvent(input, &ev);
            };
            sendMouse(QEvent::MouseButtonPress, QPointF(8, y));
            sendMouse(QEvent::MouseMove, QPointF(dragX, y));
            sendMouse(QEvent::MouseButtonRelease, QPointF(dragX, y));
            for (int i = 0; i < 10; i++) { QCoreApplication::processEvents(); }
            const qreal dragged = eff->getInBlackAnimator()
                    ->getCurrentBaseValue();
            if (dragged < 96. || dragged > 108.) {
                throw std::runtime_error("black handle drag did not land "
                                         "near 40 percent: " +
                                         std::to_string(dragged));
            }

            // set an asymmetric state then double-click the black
            // handle (sitting at the 40 percent mark after the drag):
            // back to 0, one clean undo step
            sendMouse(QEvent::MouseButtonDblClick, QPointF(dragX, y));
            for (int i = 0; i < 10; i++) { QCoreApplication::processEvents(); }
            if (!qFuzzyIsNull(eff->getInBlackAnimator()
                              ->getCurrentBaseValue())) {
                throw std::runtime_error("double-click did not reset");
            }

            // screenshot for eyeballing
            const auto shot = dlg->grab();
            const QString outPath = QString::fromUtf8(
                        qgetenv("FRICTION_LEVELS_DIALOG_SHOT"));
            if (!outPath.isEmpty() && !shot.save(outPath)) {
                throw std::runtime_error("screenshot save failed");
            }

            dlg->close();
            for (int i = 0; i < 10; i++) { QCoreApplication::processEvents(); }
        });

        std::cout << "ALL DONE" << std::endl;
        std::cout << passed << " passed, " << failed << " failed" << std::endl;
        return failed == 0 ? 0 : 1;
    }

    // isolated reorder-crash probe: runs on a clean heap before any
    // other test can corrupt it
    if (qEnvironmentVariableIsSet("FRICTION_REORDER_PROBE")) {
        // PathBox ctor reads eSettings (last used stroke width)
        static eSettings probeSettings(HardwareInfo::sCpuThreads(),
                                       HardwareInfo::sRamKB());
        Q_UNUSED(probeSettings)
        runTest("PROBE: Effect reorder drop (SWT_dropInto)", [&]() {
            const auto box = enve::make_shared<RectangleBox>();
            const auto coll = box->rasterEffectsCollection();
            coll->addChild(enve::make_shared<BlurEffect>());
            coll->addChild(enve::make_shared<ThresholdEffect>());
            coll->addChild(enve::make_shared<ShadowEffect>());
            if (coll->ca_getNumberOfChildren() != 3) {
                throw std::runtime_error("effect setup failed");
            }
            // SWT abstractions like the properties panel creates
            UpdateFuncs funcs;
            funcs.fContentUpdateIfIsCurrentRule = [](SWT_BoxRule){};
            funcs.fContentUpdateIfIsCurrentTarget = [](SingleWidgetTarget*, SWT_Target){};
            funcs.fContentUpdateIfSearchNotEmpty = [](){};
            funcs.fUpdateParentHeight = [](){};
            funcs.fUpdateVisibleWidgetsContent = [](){};
            box->SWT_createAbstraction(funcs, 4242);
            const auto collAbs = coll->SWT_getAbstractionForWidget(4242);
            if (!collAbs) throw std::runtime_error("no collection abstraction");
            std::cout << "abs children " << collAbs->childrenCount()
                      << " model children " << coll->ca_getNumberOfChildren() << " | ";
            for (int round = 0; round < 6; round++) {
                const int n = coll->ca_getNumberOfChildren();
                RasterEffect* dragged = coll->getChild(round % 2 ? 0 : n - 1);
                // round%4==3: drop BELOW the last row = index n (the UI
                // sends exactly this for move-to-bottom)
                const int dropId = round % 4 == 3 ? n
                                 : round % 2 ? n - 1 : 0;
                const eMimeData mime(QList<RasterEffect*>{ dragged });
                coll->SWT_dropInto(dropId, &mime);
                // move-to-bottom must land the dragged effect LAST
                if (round % 4 == 3 &&
                        coll->getChild(coll->ca_getNumberOfChildren() - 1) != dragged) {
                    throw std::runtime_error("move-to-bottom landed at wrong index");
                }
                std::cout << "round " << round
                          << " abs " << collAbs->childrenCount()
                          << " model " << coll->ca_getNumberOfChildren() << " ; " << std::flush;
                if (collAbs->childrenCount() != coll->ca_getNumberOfChildren()) {
                    throw std::runtime_error("abstraction/model desync after reorder");
                }
            }
            std::cout << "PROBE SURVIVED (3-effect minimal) | " << std::flush;

            // ---- full-context storm: real document/scene, ALL effect
            // types on one layer, reorder + undo/redo interleaved
            TaskScheduler probeSched;
            Document probeDoc(probeSched);
            const auto scene = probeDoc.createNewScene(false);
            const auto box2 = enve::make_shared<RectangleBox>();
            scene->addContained(box2);
            const auto coll2 = box2->rasterEffectsCollection();
            const RasterEffectType typesAll[] = {
            RasterEffectType::BLUR,
            RasterEffectType::SHADOW,
            RasterEffectType::MOTION_BLUR,
            RasterEffectType::WIPE,
            RasterEffectType::NOISE_FADE,
            RasterEffectType::COLORIZE,
            RasterEffectType::BRIGHTNESS_CONTRAST,
            RasterEffectType::VIGNETTE,
            RasterEffectType::CHROMATIC_ABERRATION,
            RasterEffectType::LETTERBOX,
            RasterEffectType::SCANLINES,
            RasterEffectType::GLOW,
            RasterEffectType::DIRECTIONAL_BLUR,
            RasterEffectType::RADIAL_BLUR,
            RasterEffectType::WAVE_WARP,
            RasterEffectType::RAIN,
            RasterEffectType::EDGE_DETECT,
            RasterEffectType::INVERT,
            RasterEffectType::TINT,
            RasterEffectType::PIXELATE,
            RasterEffectType::NOISE,
            RasterEffectType::MIRROR,
            RasterEffectType::GLITCH,
            RasterEffectType::POSTERIZE,
            RasterEffectType::TWIRL,
            RasterEffectType::CHANNEL_BLUR,
            RasterEffectType::HALFTONE,
            RasterEffectType::SHAKE,
            RasterEffectType::DROP_SHADOW,
            RasterEffectType::ZOOM_BLUR,
            RasterEffectType::COLOR_GRADING,
            RasterEffectType::STRIPE,
            RasterEffectType::MOTION_TILE,
            RasterEffectType::FRACTAL_NOISE,
            RasterEffectType::LIGHT_SWEEP,
            RasterEffectType::DISPLACEMENT_WARP,
            RasterEffectType::FILM_GRAIN,
            RasterEffectType::BLACK_WHITE_FLASH,
            RasterEffectType::LIQUID_GLASS,
            RasterEffectType::PIXEL_ART,
            RasterEffectType::CHROMA_KEY,
            RasterEffectType::LAYER_STYLES,
            RasterEffectType::PAGE_CURL,
            RasterEffectType::THRESHOLD,
            RasterEffectType::SIMPLE_CHOKER,
            RasterEffectType::DESATURATE,
            RasterEffectType::LEVELS
        };
            for (const auto t : typesAll) {
                const auto eff = createRasterEffectForNonCustomType(t);
                if (eff) coll2->addChild(eff);
            }
            const int nAll = coll2->ca_getNumberOfChildren();
            std::cout << "all-effects children " << nAll << " | " << std::flush;
            UpdateFuncs funcs2;
            funcs2.fContentUpdateIfIsCurrentRule = [](SWT_BoxRule){};
            funcs2.fContentUpdateIfIsCurrentTarget = [](SingleWidgetTarget*, SWT_Target){};
            funcs2.fContentUpdateIfSearchNotEmpty = [](){};
            funcs2.fUpdateParentHeight = [](){};
            funcs2.fUpdateVisibleWidgetsContent = [](){};
            scene->SWT_createAbstraction(funcs2, 4243);
            box2->SWT_createAbstraction(funcs2, 4243);
            for (int round = 0; round < 12; round++) {
                const int n = coll2->ca_getNumberOfChildren();
                if (n == 0) throw std::runtime_error("effects vanished");
                RasterEffect* dragged = coll2->getChild(round % 3 == 0 ? 0 :
                                              round % 3 == 1 ? n - 1 : n / 2);
                const eMimeData mime(QList<RasterEffect*>{ dragged });
                coll2->SWT_dropInto(round % 3 == 2 ? n : round % 2 ? 0 : n - 1, &mime);
                std::cout << "r" << round << " " << std::flush;
            }
            // undo the last few steps, redo them back
            for (int u = 0; u < 6; u++) {
                if (!scene->undoRedoStack()->canUndo()) break;
                scene->undo();
                std::cout << "u" << u << " " << std::flush;
            }
            for (int r = 0; r < 6; r++) {
                if (!scene->undoRedoStack()->canRedo()) break;
                scene->redo();
                std::cout << "d" << r << " " << std::flush;
            }
            std::cout << "| PROBE2 SURVIVED" << std::endl;
        });
        std::cout << "Summary: " << passed << " passed, " << failed << " failed." << std::endl;
        return (failed == 0) ? 0 : 1;
    }

    // Test 1: Factory instantiation for all RasterEffectTypes
    runTest("Test 1: createRasterEffectForNonCustomType", [&]() {
        const RasterEffectType types[] = {
            RasterEffectType::BLUR,
            RasterEffectType::SHADOW,
            RasterEffectType::MOTION_BLUR,
            RasterEffectType::WIPE,
            RasterEffectType::NOISE_FADE,
            RasterEffectType::COLORIZE,
            RasterEffectType::BRIGHTNESS_CONTRAST,
            RasterEffectType::VIGNETTE,
            RasterEffectType::CHROMATIC_ABERRATION,
            RasterEffectType::LETTERBOX,
            RasterEffectType::SCANLINES,
            RasterEffectType::GLOW,
            RasterEffectType::DIRECTIONAL_BLUR,
            RasterEffectType::RADIAL_BLUR,
            RasterEffectType::WAVE_WARP,
            RasterEffectType::RAIN,
            RasterEffectType::EDGE_DETECT,
            RasterEffectType::INVERT,
            RasterEffectType::TINT,
            RasterEffectType::PIXELATE,
            RasterEffectType::NOISE,
            RasterEffectType::MIRROR,
            RasterEffectType::GLITCH,
            RasterEffectType::POSTERIZE,
            RasterEffectType::TWIRL,
            RasterEffectType::CHANNEL_BLUR,
            RasterEffectType::HALFTONE,
            RasterEffectType::SHAKE,
            RasterEffectType::DROP_SHADOW,
            RasterEffectType::ZOOM_BLUR,
            RasterEffectType::COLOR_GRADING,
            RasterEffectType::STRIPE,
            RasterEffectType::MOTION_TILE,
            RasterEffectType::FRACTAL_NOISE,
            RasterEffectType::LIGHT_SWEEP,
            RasterEffectType::DISPLACEMENT_WARP,
            RasterEffectType::FILM_GRAIN,
            RasterEffectType::BLACK_WHITE_FLASH,
            RasterEffectType::LIQUID_GLASS,
            RasterEffectType::PIXEL_ART,
            RasterEffectType::CHROMA_KEY,
            RasterEffectType::LAYER_STYLES,
            RasterEffectType::PAGE_CURL,
            RasterEffectType::THRESHOLD,
            RasterEffectType::SIMPLE_CHOKER,
            RasterEffectType::DESATURATE,
            RasterEffectType::LEVELS
        };

        for (const auto t : types) {
            auto eff = createRasterEffectForNonCustomType(t);
            if (!eff) {
                throw std::runtime_error(std::string("Factory returned null for type ") + std::to_string(int(t)));
            }
            if (eff->getEffectType() != t) {
                throw std::runtime_error("Effect type mismatch");
            }
            if (eff->prp_getName().isEmpty()) {
                throw std::runtime_error("Effect name is empty");
            }
        }
    });

    // Test 2: Caller generation and CPU render execution
    runTest("Test 2: CPU Tile Processing", [&]() {
        const RasterEffectType types[] = {
            RasterEffectType::BRIGHTNESS_CONTRAST,
            RasterEffectType::COLORIZE,
            RasterEffectType::VIGNETTE,
            RasterEffectType::CHROMATIC_ABERRATION,
            RasterEffectType::LETTERBOX,
            RasterEffectType::SCANLINES,
            RasterEffectType::GLOW,
            RasterEffectType::DIRECTIONAL_BLUR,
            RasterEffectType::RADIAL_BLUR,
            RasterEffectType::WAVE_WARP,
            RasterEffectType::RAIN,
            RasterEffectType::EDGE_DETECT,
            RasterEffectType::INVERT,
            RasterEffectType::TINT,
            RasterEffectType::PIXELATE,
            RasterEffectType::NOISE,
            RasterEffectType::MIRROR,
            RasterEffectType::GLITCH,
            RasterEffectType::POSTERIZE,
            RasterEffectType::TWIRL,
            RasterEffectType::CHANNEL_BLUR,
            RasterEffectType::HALFTONE,
            RasterEffectType::SHAKE,
            RasterEffectType::DROP_SHADOW,
            RasterEffectType::ZOOM_BLUR,
            RasterEffectType::COLOR_GRADING,
            RasterEffectType::STRIPE,
            RasterEffectType::MOTION_TILE,
            RasterEffectType::FRACTAL_NOISE,
            RasterEffectType::LIGHT_SWEEP,
            RasterEffectType::DISPLACEMENT_WARP,
            RasterEffectType::FILM_GRAIN,
            RasterEffectType::BLACK_WHITE_FLASH,
            RasterEffectType::LIQUID_GLASS,
            RasterEffectType::PIXEL_ART,
            RasterEffectType::PAGE_CURL,
            RasterEffectType::THRESHOLD,
            RasterEffectType::DESATURATE
        };

        SkBitmap srcBtmp;
        srcBtmp.allocN32Pixels(64, 64);
        srcBtmp.eraseARGB(255, 128, 64, 200);

        SkBitmap dstBtmp;
        dstBtmp.allocN32Pixels(64, 64);
        dstBtmp.eraseARGB(0, 0, 0, 0);

        for (const auto t : types) {
            auto eff = createRasterEffectForNonCustomType(t);
            auto caller = eff->getEffectCaller(0.0, 1.0, 1.0, nullptr);
            if (!caller) {
                throw std::runtime_error("Caller is null for type " + std::to_string(int(t)));
            }

            CpuRenderTools tools{srcBtmp, dstBtmp};
            CpuRenderData data;
            data.fTexTile = SkIRect::MakeXYWH(0, 0, 64, 64);

            caller->processCpu(tools, data);
        }
    });

    // Test 2b: Layer Styles effect needs at least one style enabled
    // before a caller exists (default state is all-off = null caller).
    // Mirrors EffectSubTaskSpawner: full-size dst, per-tile subsets,
    // then pixel assertions (styles must land outside the silhouette)
    runTest("Test 2b: Layer Styles CPU render", [&]() {
        const auto eff = createRasterEffectForNonCustomType(
                RasterEffectType::LAYER_STYLES);
        if (!eff) { throw std::runtime_error("Factory returned null"); }
        const auto styles = enve_cast<LayerStylesEffect*>(eff.get());
        if (!styles) { throw std::runtime_error("Not a LayerStylesEffect"); }
        styles->shadowEnabled()->setCurrentBoolValue(true);
        styles->glowEnabled()->setCurrentBoolValue(true);
        styles->strokeEnabled()->setCurrentBoolValue(true);
        // angle 0 -> light from the right, shadow falls left
        styles->setShadow(true, 0.0, 10.0, 0.0, 5.0, 100.0, QColor(0, 0, 0));
        const auto caller = styles->getEffectCaller(0.0, 1.0, 1.0, nullptr);
        if (!caller) { throw std::runtime_error("Layer styles caller is null"); }

        SkBitmap srcBtmp;
        srcBtmp.allocN32Pixels(64, 64);
        srcBtmp.eraseARGB(0, 0, 0, 0);
        {
            SkCanvas c(srcBtmp);
            SkPaint p;
            p.setColor(SkColorSetARGB(255, 128, 64, 200));
            c.drawRect(SkRect::MakeXYWH(16, 16, 32, 32), p);
        }

        SkBitmap dstBtmp;
        dstBtmp.allocN32Pixels(srcBtmp.width(), srcBtmp.height());
        dstBtmp.eraseARGB(0, 0, 0, 0);

        // two vertical tiles, exactly like the spawner subsets them
        const SkIRect tiles[] = { SkIRect::MakeXYWH(0, 0, 32, 64),
                                  SkIRect::MakeXYWH(32, 0, 32, 64) };
        for (const auto& tile : tiles) {
            SkBitmap tileDst;
            if (!dstBtmp.extractSubset(&tileDst, tile)) {
                throw std::runtime_error("extractSubset failed");
            }
            CpuRenderTools tools{srcBtmp, tileDst};
            CpuRenderData data;
            data.fTexTile = tile;
            caller->processCpu(tools, data);
        }

        // silhouette core survives with the original color
        const auto core = static_cast<const uint32_t*>(dstBtmp.getAddr(32, 32));
        if (SkColorGetA(*core) < 250) {
            throw std::runtime_error("layer core lost alpha");
        }
        // stroke ring outside the right edge of the square (x=50)
        const auto rightRing = static_cast<const uint32_t*>(dstBtmp.getAddr(50, 32));
        if (SkColorGetA(*rightRing) < 100
                || SkColorGetR(*rightRing) < 200) {
            throw std::runtime_error("no red stroke ring on the right");
        }
        // shadow left of the square: square spans x[16,48], distance 10
        // -> shadow spans x[6,38]; sample inside it
        const auto shadowPx = static_cast<const uint32_t*>(dstBtmp.getAddr(7, 32));
        if (SkColorGetA(*shadowPx) < 30) {
            throw std::runtime_error("no shadow to the left");
        }

        // --- round 2: with spread/choke (the PSD-import parameter
        // shape that failed on the user's GPU render) ---
        dstBtmp.eraseARGB(0, 0, 0, 0);
        styles->setShadow(true, 90.0, 10.0, 56.0, 7.0, 40.0, QColor(0, 0, 0));
        styles->setGlow(true, 42.0, 54.0, 23.0, QColor(0, 255, 24));
        const auto caller2 = styles->getEffectCaller(0.0, 1.0, 1.0, nullptr);
        if (!caller2) { throw std::runtime_error("caller2 is null"); }
        for (const auto& tile : tiles) {
            SkBitmap tileDst;
            if (!dstBtmp.extractSubset(&tileDst, tile)) {
                throw std::runtime_error("extractSubset failed");
            }
            CpuRenderTools tools{srcBtmp, tileDst};
            CpuRenderData data;
            data.fTexTile = tile;
            caller2->processCpu(tools, data);
        }
        // angle 90 with the PS dial mapping -> shadow straight UP
        // (-10): spans y[6,38]; sample above the square (y=12);
        // shadow is black: tint must dominate, not the layer's purple
        const auto shadowPx2 = static_cast<const uint32_t*>(dstBtmp.getAddr(20, 12));
        if (SkColorGetA(*shadowPx2) < 20
                || SkColorGetB(*shadowPx2) > SkColorGetR(*shadowPx2) + 10) {
            throw std::runtime_error("no choked black shadow above");
        }
        // glow: the user's real PSD parameters (spread 42, size 54,
        // opacity 23%) must stay visible - spread is ignored for glow
        // and the rim is lifted x2, otherwise 0.23*0.5*choke leaves
        // a handful of alpha units invisible to the eye
        styles->setGlow(true, 42.0, 54.0, 23.0, QColor(0, 255, 24));
        const auto caller3 = styles->getEffectCaller(0.0, 1.0, 1.0, nullptr);
        if (!caller3) { throw std::runtime_error("caller3 is null"); }
        dstBtmp.eraseARGB(0, 0, 0, 0);
        for (const auto& tile : tiles) {
            SkBitmap tileDst;
            if (!dstBtmp.extractSubset(&tileDst, tile)) {
                throw std::runtime_error("extractSubset failed");
            }
            CpuRenderTools tools{srcBtmp, tileDst};
            CpuRenderData data;
            data.fTexTile = tile;
            caller3->processCpu(tools, data);
        }
        // (11,32) is 5px left of the square edge: ~48/255 green
        const auto glowPx = static_cast<const uint32_t*>(dstBtmp.getAddr(11, 32));
        if (SkColorGetA(*glowPx) < 30
                || SkColorGetG(*glowPx) < SkColorGetR(*glowPx)) {
            std::cout << " [glow debug:";
            for (int x = 8; x <= 20; x += 2) {
                const auto px = static_cast<const uint32_t*>(dstBtmp.getAddr(x, 32));
                std::cout << " x" << x << "=" << SkColorGetA(*px)
                          << "/g" << SkColorGetG(*px);
            }
            std::cout << "] ";
            throw std::runtime_error("no visible green glow around");
        }
    });

    // Test 2c: visual effect-preview frames for every core raster
    // effect (the card gallery renderer); optionally dumps PNGs to
    // argv[2] for eyeballing. Asserts the CPU offscreen path returns
    // a full frame sequence with non-blank content.
    runTest("Test 2c: EffectPreview frames", [&]() {
        const RasterEffectType types[] = {
            RasterEffectType::BLUR,
            RasterEffectType::SHADOW,
            RasterEffectType::MOTION_BLUR,
            RasterEffectType::WIPE,
            RasterEffectType::NOISE_FADE,
            RasterEffectType::COLORIZE,
            RasterEffectType::BRIGHTNESS_CONTRAST,
            RasterEffectType::CHROMA_KEY,
            RasterEffectType::LIQUID_GLASS,
            RasterEffectType::VIGNETTE,
            RasterEffectType::CHROMATIC_ABERRATION,
            RasterEffectType::LETTERBOX,
            RasterEffectType::SCANLINES,
            RasterEffectType::GLOW,
            RasterEffectType::DIRECTIONAL_BLUR,
            RasterEffectType::RADIAL_BLUR,
            RasterEffectType::WAVE_WARP,
            RasterEffectType::RAIN,
            RasterEffectType::EDGE_DETECT,
            RasterEffectType::INVERT,
            RasterEffectType::TINT,
            RasterEffectType::PIXELATE,
            RasterEffectType::NOISE,
            RasterEffectType::MIRROR,
            RasterEffectType::GLITCH,
            RasterEffectType::POSTERIZE,
            RasterEffectType::TWIRL,
            RasterEffectType::CHANNEL_BLUR,
            RasterEffectType::HALFTONE,
            RasterEffectType::SHAKE,
            RasterEffectType::DROP_SHADOW,
            RasterEffectType::ZOOM_BLUR,
            RasterEffectType::COLOR_GRADING,
            RasterEffectType::STRIPE,
            RasterEffectType::MOTION_TILE,
            RasterEffectType::FRACTAL_NOISE,
            RasterEffectType::LIGHT_SWEEP,
            RasterEffectType::DISPLACEMENT_WARP,
            RasterEffectType::FILM_GRAIN,
            RasterEffectType::BLACK_WHITE_FLASH,
            RasterEffectType::PIXEL_ART,
            RasterEffectType::LAYER_STYLES,
            RasterEffectType::SHATTER,
            RasterEffectType::SMEAR,
            RasterEffectType::ROUGHEN_EDGES,
            RasterEffectType::PARTICLE,
            RasterEffectType::PAGE_CURL,
            RasterEffectType::LATTICE_WARP,
            RasterEffectType::CEL_VOLUME,
            RasterEffectType::THRESHOLD,
            RasterEffectType::SIMPLE_CHOKER,
            RasterEffectType::DESATURATE,
            RasterEffectType::LEVELS
        };
        QString dumpDir;
        if (argc >= 3) {
            dumpDir = QString::fromLocal8Bit(argv[2]);
            QDir().mkpath(dumpDir);
        }
        int rendered = 0;
        int blank = 0;
        for (const auto t : types) {
            if (!EffectPreview::canPreview(t)) { continue; }
            const auto frames = EffectPreview::renderEffectFrames(
                        t, 16, QSize(160, 160));
            if (frames.count() != 16) {
                throw std::runtime_error("frame count mismatch for type "
                                         + std::to_string(int(t)));
            }
            // at least one frame must carry visible pixels
            bool anyOpaque = false;
            for (const auto& f : frames) {
                if (f.isNull()) { continue; }
                for (int y = 0; y < f.height() && !anyOpaque; y += 8) {
                    for (int x = 0; x < f.width(); x += 8) {
                        if (qAlpha(f.pixel(x, y)) > 8) {
                            anyOpaque = true;
                            break;
                        }
                    }
                }
                if (anyOpaque) { break; }
            }
            if (anyOpaque) { rendered++; }
            else {
                blank++;
                std::cout << " [blank: type " << int(t) << "] ";
            }
            if (!dumpDir.isEmpty() && !frames.isEmpty()) {
                const int idx = frames.count() / 2;
                frames.at(idx).save(dumpDir + "/" +
                                    QString::number(int(t)) + ".png");
                frames.at(0).save(dumpDir + "/f0_" +
                                  QString::number(int(t)) + ".png");
                frames.at(qMax(1, frames.count() / 4)).save(
                            dumpDir + "/q_" +
                            QString::number(int(t)) + ".png");
            }
        }
        std::cout << " (" << rendered << " rendered, "
                  << blank << " blank) ";
        if (rendered < 20) {
            throw std::runtime_error("too few effects produced visible frames");
        }
    });

    // Test 2d: concurrency determinism - the visual panel renders all
    // tiles in parallel on QtConcurrent threads; if any effect's CPU
    // path mutates shared/static state, parallel output differs from
    // serial. Hash both and compare per effect type.
    runTest("Test 2d: EffectPreview parallel determinism", [&]() {
        const RasterEffectType probeTypes[] = {
            RasterEffectType::BLUR,
            RasterEffectType::MIRROR,
            RasterEffectType::TWIRL,
            RasterEffectType::SHAKE,
            RasterEffectType::GLITCH,
            RasterEffectType::FRACTAL_NOISE,
            RasterEffectType::RAIN,
            RasterEffectType::FILM_GRAIN,
            RasterEffectType::NOISE,
            RasterEffectType::WAVE_WARP,
            RasterEffectType::SHATTER,
            RasterEffectType::SMEAR,
            RasterEffectType::GLOW,
            RasterEffectType::DISPLACEMENT_WARP
        };
        const auto hashFrames = gHashFrames;
        // run the same batch in parallel several times; any mismatch
        // against the serial baseline or between rounds is a race
        QVector<QPair<RasterEffectType, int>> jobs;
        for (const auto t : probeTypes) {
            if (!EffectPreview::canPreview(t)) { continue; }
            jobs << qMakePair(t, 0);
            jobs << qMakePair(t, 1); // each effect twice concurrently
        }
        QVector<QByteArray> serial;
        for (const auto& j : jobs) {
            serial << hashFrames(EffectPreview::renderEffectFrames(
                        j.first, 6, QSize(64, 64)));
        }
        const auto runBatch = [jobs]() {
            return QtConcurrent::blockingMapped<QVector<QByteArray>>(
                        jobs, [](const QPair<RasterEffectType, int>& j) -> QByteArray {
                return gHashFrames(EffectPreview::renderEffectFrames(
                            j.first, 6, QSize(64, 64)));
            });
        };
        const int rounds = 6;
        int mismatches = 0;
        for (int r = 0; r < rounds; r++) {
            const auto par = runBatch();
            const int n = qMin(par.size(), serial.size());
            for (int i = 0; i < n; i++) {
                if (par.at(i) != serial.at(i)) {
                    mismatches++;
                    std::cout << " [RACE: type " << int(jobs.at(i).first)
                              << " job " << i << "] ";
                }
            }
        }
        std::cout << " (" << rounds << " rounds, "
                  << mismatches << " mismatches) ";
        if (mismatches > 0) {
            throw std::runtime_error("parallel rendering is not deterministic");
        }
    });

    // Test 2e: AE semantics of Threshold (luminance binarize, alpha
    // preserved), Simple Choker (positive chokes the matte inward,
    // negative spreads it outward; choke 0 = no caller = passthrough)
    // and Desaturate (Rec.601 grayscale, alpha preserved, amount
    // blends toward the original colors)
    runTest("Test 2e: Threshold + Simple Choker + Desaturate semantics", [&]() {
        const auto findParam = [](RasterEffect* eff,
                                  const char* name) -> QrealAnimator* {
            const int n = eff->ca_getNumberOfChildren();
            for (int i = 0; i < n; i++) {
                auto* qa = enve_cast<QrealAnimator*>(
                            eff->ca_getChildAt(i));
                if (qa && qa->prp_getName().contains(
                            QString::fromLatin1(name))) {
                    return qa;
                }
            }
            return nullptr;
        };
        const auto renderTiles = [](RasterEffect* eff,
                                    const SkBitmap& srcBtmp,
                                    SkBitmap& dstBtmp) {
            const auto caller = eff->getEffectCaller(0.0, 1.0, 1.0, nullptr);
            if (!caller) { return false; }
            const SkIRect tiles[] = { SkIRect::MakeXYWH(0, 0, 32, 64),
                                      SkIRect::MakeXYWH(32, 0, 32, 64) };
            for (const auto& tile : tiles) {
                SkBitmap tileDst;
                if (!dstBtmp.extractSubset(&tileDst, tile)) {
                    throw std::runtime_error("extractSubset failed");
                }
                CpuRenderTools tools{srcBtmp, tileDst};
                CpuRenderData data;
                data.fTexTile = tile;
                caller->processCpu(tools, data);
            }
            return true;
        };
        const auto px = [](const SkBitmap& b, const int x, const int y) {
            return *static_cast<const uint32_t*>(b.getAddr(x, y));
        };

        // --- threshold: Rec.601 luminance vs level, alpha untouched ---
        {
            const auto eff = createRasterEffectForNonCustomType(
                        RasterEffectType::THRESHOLD);
            auto* level = findParam(eff.get(), "level");
            if (!level) { throw std::runtime_error("no level param"); }
            level->setCurrentBaseValue(50.);

            SkBitmap src;
            src.allocN32Pixels(64, 64);
            src.eraseARGB(0, 0, 0, 0);
            {
                SkCanvas c(src);
                SkPaint p;
                // lum = 0.299*200+0.587*40+0.114*40 = 87.8 < 127.5 -> black
                p.setColor(SkColorSetARGB(255, 200, 40, 40));
                c.drawRect(SkRect::MakeXYWH(16, 16, 32, 32), p);
                // lum = 240 >= 127.5 -> white, alpha 180 must survive
                p.setColor(SkColorSetARGB(180, 240, 240, 240));
                c.drawRect(SkRect::MakeXYWH(0, 0, 16, 64), p);
            }
            SkBitmap dst;
            dst.allocN32Pixels(64, 64);
            dst.eraseARGB(0, 0, 0, 0);
            if (!renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("threshold caller is null");
            }
            const auto darkPx = px(dst, 32, 32);
            if (SkColorGetR(darkPx) != 0 || SkColorGetG(darkPx) != 0 ||
                SkColorGetB(darkPx) != 0 || SkColorGetA(darkPx) != 255) {
                throw std::runtime_error("below-level pixel not black");
            }
            const auto lightPx = px(dst, 8, 32);
            if (SkColorGetR(lightPx) != 255 || SkColorGetG(lightPx) != 255 ||
                SkColorGetB(lightPx) != 255 || SkColorGetA(lightPx) != 180) {
                throw std::runtime_error("above-level pixel not white "
                                         "with preserved alpha");
            }
        }

        // --- simple choker: opaque 32x32 square on transparency ---
        {
            const auto eff = createRasterEffectForNonCustomType(
                        RasterEffectType::SIMPLE_CHOKER);
            auto* choke = findParam(eff.get(), "choke matte");
            if (!choke) { throw std::runtime_error("no choke param"); }

            SkBitmap src;
            src.allocN32Pixels(64, 64);
            src.eraseARGB(0, 0, 0, 0);
            {
                SkCanvas c(src);
                SkPaint p;
                p.setAntiAlias(false);
                p.setColor(SkColorSetARGB(255, 255, 255, 255));
                // square spans x[16,48) y[16,48), hard un-antialiased edge
                c.drawRect(SkRect::MakeXYWH(16, 16, 32, 32), p);
            }
            SkBitmap dst;
            dst.allocN32Pixels(64, 64);

            // choke 0 must be a no-op (null caller, AE passthrough)
            choke->setCurrentBaseValue(0.0);
            dst.eraseARGB(0, 0, 0, 0);
            if (renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("choke 0 produced a caller");
            }

            // choke +4 shrinks the matte ~4px inward
            choke->setCurrentBaseValue(4.0);
            dst.eraseARGB(0, 0, 0, 0);
            if (!renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("choke 4 caller is null");
            }
            if (SkColorGetA(px(dst, 32, 32)) < 250) {
                throw std::runtime_error("choked matte lost its core");
            }
            // 2px inside the old edge is past the ~4.8px shrink: gone
            if (SkColorGetA(px(dst, 18, 32)) > 100) {
                throw std::runtime_error("choke did not eat the rim");
            }
            // outside the old edge must stay empty
            if (SkColorGetA(px(dst, 14, 32)) != 0) {
                throw std::runtime_error("choke leaked outside the matte");
            }

            // choke -4 spreads the matte ~4px outward
            choke->setCurrentBaseValue(-4.0);
            dst.eraseARGB(0, 0, 0, 0);
            if (!renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("choke -4 caller is null");
            }
            if (SkColorGetA(px(dst, 13, 32)) < 100) {
                throw std::runtime_error("spread did not grow the matte");
            }
            if (SkColorGetA(px(dst, 32, 32)) < 250) {
                throw std::runtime_error("spread lost the matte core");
            }
            if (SkColorGetA(px(dst, 8, 32)) > 30) {
                throw std::runtime_error("spread bled too far out");
            }
        }

        // --- desaturate: Rec.601 gray, alpha untouched, amount blend ---
        {
            const auto eff = createRasterEffectForNonCustomType(
                        RasterEffectType::DESATURATE);
            auto* amount = findParam(eff.get(), "amount");
            if (!amount) { throw std::runtime_error("no amount param"); }

            SkBitmap src;
            src.allocN32Pixels(64, 64);
            src.eraseARGB(0, 0, 0, 0);
            {
                SkCanvas c(src);
                SkPaint p;
                // lum = 0.299*200+0.587*40+0.114*40 = 87.8 -> 88
                p.setColor(SkColorSetARGB(255, 200, 40, 40));
                c.drawRect(SkRect::MakeXYWH(16, 16, 32, 32), p);
                // lum = 240
                p.setColor(SkColorSetARGB(180, 240, 240, 240));
                c.drawRect(SkRect::MakeXYWH(0, 0, 16, 64), p);
            }
            SkBitmap dst;
            dst.allocN32Pixels(64, 64);

            // amount 100 (default): pure gray, channels equal, alpha kept
            dst.eraseARGB(0, 0, 0, 0);
            if (!renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("desaturate caller is null");
            }
            const auto grayPx = px(dst, 32, 32);
            if (std::abs(int(SkColorGetR(grayPx)) - 88) > 1 ||
                SkColorGetR(grayPx) != SkColorGetG(grayPx) ||
                SkColorGetG(grayPx) != SkColorGetB(grayPx) ||
                SkColorGetA(grayPx) != 255) {
                throw std::runtime_error("amount 100 not uniform Rec.601 gray");
            }
            if (SkColorGetA(px(dst, 8, 32)) != 180) {
                throw std::runtime_error("desaturate lost alpha");
            }

            // amount 50: halfway between source and gray
            amount->setCurrentBaseValue(50.0);
            dst.eraseARGB(0, 0, 0, 0);
            if (!renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("desaturate 50 caller is null");
            }
            const auto halfPx = px(dst, 32, 32);
            // r: 200*0.5+88*0.5 = 144, g/b: 40*0.5+88*0.5 = 64
            if (std::abs(int(SkColorGetR(halfPx)) - 144) > 1 ||
                std::abs(int(SkColorGetG(halfPx)) - 64) > 1) {
                throw std::runtime_error("amount 50 blend is off");
            }

            // invert: black/white reversed desaturation - the gray
            // target becomes its negative (255 - gray)
            const auto findBool = [&eff]() -> BoolAnimator* {
                const int n = eff->ca_getNumberOfChildren();
                for (int i = 0; i < n; i++) {
                    auto* ba = enve_cast<BoolAnimator*>(
                                eff->ca_getChildAt(i));
                    if (ba && ba->prp_getName().contains(
                                QStringLiteral("invert"))) {
                        return ba;
                    }
                }
                return nullptr;
            };
            auto* invert = findBool();
            if (!invert) { throw std::runtime_error("no invert param"); }

            invert->setCurrentBoolValue(true);
            amount->setCurrentBaseValue(100.0);
            dst.eraseARGB(0, 0, 0, 0);
            if (!renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("invert caller is null");
            }
            // (200,40,40): gray 87.8 -> negative 167, alpha kept
            const auto negPx = px(dst, 32, 32);
            if (std::abs(int(SkColorGetR(negPx)) - 167) > 1 ||
                SkColorGetR(negPx) != SkColorGetG(negPx) ||
                SkColorGetG(negPx) != SkColorGetB(negPx) ||
                SkColorGetA(negPx) != 255) {
                throw std::runtime_error("invert not negative gray");
            }
            // light gray input: stored premultiplied as
            // round(240*180/255) = 169 -> negative 86, alpha kept
            const auto lightPx = px(dst, 8, 32);
            if (std::abs(int(SkColorGetR(lightPx)) - 86) > 1 ||
                SkColorGetA(lightPx) != 180) {
                throw std::runtime_error("invert wrong on light input");
            }

            // invert + amount 50: halfway between source and negative
            amount->setCurrentBaseValue(50.0);
            dst.eraseARGB(0, 0, 0, 0);
            if (!renderTiles(eff.get(), src, dst)) {
                throw std::runtime_error("invert 50 caller is null");
            }
            const auto halfInvPx = px(dst, 32, 32);
            // r: 200*0.5+167*0.5 = 184, g/b: 40*0.5+167*0.5 = 104
            if (std::abs(int(SkColorGetR(halfInvPx)) - 184) > 1 ||
                std::abs(int(SkColorGetG(halfInvPx)) - 104) > 1) {
                throw std::runtime_error("invert amount 50 blend is off");
            }
        }
    });

    // Test 2f: PS Levels semantics - input black/white remap, gamma
    // curve, output range, per-channel targeting, alpha preserved,
    // defaults = identity = null caller (passthrough), black/white
    // points can never cross
    runTest("Test 2f: Levels semantics", [&]() {
        const auto eff = createRasterEffectForNonCustomType(
                    RasterEffectType::LEVELS);
        if (!eff) { throw std::runtime_error("Factory returned null"); }
        const auto levels = enve_cast<LevelsEffect*>(eff.get());
        if (!levels) { throw std::runtime_error("Not a LevelsEffect"); }

        const auto renderTiles = [](RasterEffect* eff,
                                    const SkBitmap& srcBtmp,
                                    SkBitmap& dstBtmp) {
            const auto caller = eff->getEffectCaller(0.0, 1.0, 1.0, nullptr);
            if (!caller) { return false; }
            const SkIRect tiles[] = { SkIRect::MakeXYWH(0, 0, 32, 64),
                                      SkIRect::MakeXYWH(32, 0, 32, 64) };
            for (const auto& tile : tiles) {
                SkBitmap tileDst;
                if (!dstBtmp.extractSubset(&tileDst, tile)) {
                    throw std::runtime_error("extractSubset failed");
                }
                CpuRenderTools tools{srcBtmp, tileDst};
                CpuRenderData data;
                data.fTexTile = tile;
                caller->processCpu(tools, data);
            }
            return true;
        };
        const auto px = [](const SkBitmap& b, const int x, const int y) {
            return *static_cast<const uint32_t*>(b.getAddr(x, y));
        };

        SkBitmap src;
        src.allocN32Pixels(64, 64);
        src.eraseARGB(0, 0, 0, 0);
        {
            SkCanvas c(src);
            SkPaint p;
            // translucent alpha probe first, on the empty canvas (an
            // SrcOver on an opaque base would stay opaque)
            p.setColor(SkColorSetARGB(180, 200, 128, 32));
            c.drawRect(SkRect::MakeXYWH(0, 0, 16, 64), p);
            // opaque body carrying the test ramp 32 / 128 / 200
            p.setColor(SkColorSetARGB(255, 200, 128, 32));
            c.drawRect(SkRect::MakeXYWH(16, 0, 48, 64), p);
        }
        SkBitmap dst;
        dst.allocN32Pixels(64, 64);

        // defaults: 0 / 1.0 / 255 / 0 / 255 = identity = no caller
        dst.eraseARGB(0, 0, 0, 0);
        if (renderTiles(eff.get(), src, dst)) {
            throw std::runtime_error("identity levels produced a caller");
        }

        // input remap: inBlack=64 inWhite=192, gamma=1, out default:
        // v<=64 -> 0, v>=192 -> 255, v=128 -> (128-64)/128*255 = 127.5
        levels->getInBlackAnimator()->setCurrentBaseValue(64.);
        levels->getInWhiteAnimator()->setCurrentBaseValue(192.);
        dst.eraseARGB(0, 0, 0, 0);
        if (!renderTiles(eff.get(), src, dst)) {
            throw std::runtime_error("levels caller is null");
        }
        {
            const auto out = px(dst, 32, 32);
            if (SkColorGetR(out) != 255) {
                throw std::runtime_error("200 should clip to 255");
            }
            if (std::abs(int(SkColorGetG(out)) - 128) > 1) {
                throw std::runtime_error("128 remap is off");
            }
            if (SkColorGetB(out) != 0) {
                throw std::runtime_error("32 should crush to 0");
            }
            if (SkColorGetA(out) != 255) {
                throw std::runtime_error("levels lost alpha");
            }
        }

        // gamma 2 on a fresh input range (0..255): v=128 ->
        // (128/255)^(1/2) * 255 = 180.3 -> 180
        levels->getInBlackAnimator()->setCurrentBaseValue(0.);
        levels->getInWhiteAnimator()->setCurrentBaseValue(255.);
        levels->getGammaAnimator()->setCurrentBaseValue(2.);
        dst.eraseARGB(0, 0, 0, 0);
        if (!renderTiles(eff.get(), src, dst)) {
            throw std::runtime_error("gamma caller is null");
        }
        {
            const auto out = px(dst, 32, 32);
            if (std::abs(int(SkColorGetG(out)) - 180) > 1) {
                throw std::runtime_error("gamma 2 midtone is off");
            }
            // alpha probe: 180-alpha area keeps its alpha, rgb premul
            // input read straight off the source bytes
            if (SkColorGetA(px(dst, 8, 32)) != 180) {
                throw std::runtime_error("gamma lost alpha");
            }
        }

        // output range: outBlack=51 outWhite=204 remaps into the
        // narrowed span: 32 -> 51+32/255*153 = 70, 200 -> 51+120 = 171
        levels->getGammaAnimator()->setCurrentBaseValue(1.);
        levels->getOutBlackAnimator()->setCurrentBaseValue(51.);
        levels->getOutWhiteAnimator()->setCurrentBaseValue(204.);
        dst.eraseARGB(0, 0, 0, 0);
        if (!renderTiles(eff.get(), src, dst)) {
            throw std::runtime_error("output levels caller is null");
        }
        {
            const auto out = px(dst, 32, 32);
            if (std::abs(int(SkColorGetB(out)) - 70) > 1) {
                throw std::runtime_error("output black not applied");
            }
            if (std::abs(int(SkColorGetR(out)) - 171) > 1) {
                throw std::runtime_error("output white not applied");
            }
        }

        // channel targeting: Red only - r remapped, g/b untouched;
        // inBlack=64: r=200 -> (200-64)/191... use full 0..255 range:
        // inWhite default 255 -> t=(200-64)/255 -> 135 (floor+0.5)
        levels->getOutBlackAnimator()->setCurrentBaseValue(0.);
        levels->getOutWhiteAnimator()->setCurrentBaseValue(255.);
        levels->getInBlackAnimator()->setCurrentBaseValue(64.);
        levels->getChannelProperty()->setCurrentValue(LevelsEffect::Red);
        dst.eraseARGB(0, 0, 0, 0);
        if (!renderTiles(eff.get(), src, dst)) {
            throw std::runtime_error("red channel caller is null");
        }
        {
            const auto out = px(dst, 32, 32);
            // PS normalizes by the input span: (200-64)/(255-64)*255
            const int expectR = int((200. - 64.) / (255. - 64.) * 255. + 0.5);
            if (std::abs(int(SkColorGetR(out)) - expectR) > 1) {
                throw std::runtime_error("red channel remap is off");
            }
            if (SkColorGetG(out) != 128 || SkColorGetB(out) != 32) {
                throw std::runtime_error("red mode touched g/b");
            }
        }

        // white point may never cross below the black point:
        // inWhite 10 < inBlack 64 clamps back to 65 (channel back to
        // RGB so the crossed-points curve touches every channel)
        levels->getChannelProperty()->setCurrentValue(LevelsEffect::RGB);
        levels->getInWhiteAnimator()->setCurrentBaseValue(10.);
        dst.eraseARGB(0, 0, 0, 0);
        if (!renderTiles(eff.get(), src, dst)) {
            throw std::runtime_error("crossed points caller is null");
        }
        {
            const auto out = px(dst, 32, 32);
            // with inBlack=64, inWhite=65: only 64 and 65 survive as
            // non-white; 32 -> 0, 128/200 -> 255
            if (SkColorGetB(out) != 0 || SkColorGetR(out) != 255) {
                throw std::runtime_error("crossed points not clamped");
            }
        }
    });

    // Test 5b: PSD layer-styles parsing on a real file passed as
    // argv[1] (skipped when no argument) - offline repro for imports
    runTest("Test 5b: PSD layer styles (real file)", [&]() {
        if (argc < 2) { std::cout << " (skipped, no file) "; return; }
        psd::PsdFile psd;
        QString err;
        if (!psd.load(QString::fromLocal8Bit(argv[1]), &err)) {
            throw std::runtime_error(("load failed: " + err).toStdString());
        }
        int styled = 0;
        int effects = 0;
        for (const auto& rec : psd.layers()) {
            if (!rec.stylesList.isEmpty()) { styled++; }
            effects += rec.stylesList.size();
        }
        std::cout << " layers=" << psd.layers().size()
                  << " styled=" << styled
                  << " effects=" << effects << " ";
        if (styled < 1) {
            throw std::runtime_error("expected at least one styled layer");
        }
    });

    // Test 5c: synthetic multi-instance PSD (2 shadows + 2 glows +
    // 2 strokes in lmfx, plus an lfx2 mirror that must be ignored).
    // Hand-built bytes: locks the '*Multi' key spellings, the lmfx
    // authority rule and the instance-to-effect assembly end to end
    runTest("Test 5c: synthetic multi-instance PSD", [&]() {
        struct Dw {
            QByteArray b;
            void u8v(const quint8 v) { b.append(char(v)); }
            void u16v(const quint16 v) {
                u8v(quint8(v >> 8)); u8v(quint8(v));
            }
            void u32v(const quint32 v) {
                for (int i = 3; i >= 0; i--) { u8v(quint8(v >> (8 * i))); }
            }
            void i32v(const qint32 v) { u32v(quint32(v)); }
            void i16v(const qint16 v) { u16v(quint16(v)); }
            void f64v(const double v) {
                quint64 bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                for (int i = 7; i >= 0; i--) { u8v(quint8(bits >> (8 * i))); }
            }
            void raw(const char* const s, const int n) { b.append(s, n); }
            void id(const char* const s) {
                const int n = int(qstrlen(s));
                if (n == 4) { i32v(0); raw(s, 4); }
                else { i32v(n); raw(s, n); }
            }
            void unit(const char* const unitKey, const double v) {
                raw("UntF", 4); raw(unitKey, 4); f64v(v);
            }
            void boolean(const bool v) { raw("bool", 4); u8v(v ? 1 : 0); }
            void enumV(const char* const type, const char* const value) {
                raw("enum", 4); id(type); id(value);
            }
            void color(const double r, const double g, const double bl) {
                raw("Objc", 4); i32v(0); id("RGBC"); i32v(3);
                id("Rd  "); raw("doub", 4); f64v(r);
                id("Grn "); raw("doub", 4); f64v(g);
                id("Bl  "); raw("doub", 4); f64v(bl);
            }
        };

        const auto shadowObj = [&](const double dist) {
            Dw w;
            w.raw("Objc", 4); w.i32v(0); w.id("DrSh"); w.i32v(7);
            w.id("enab"); w.boolean(true);
            w.id("Opct"); w.unit("#Prc", 40.0);
            w.id("lagl"); w.unit("#Ang", 90.0);
            w.id("Dstn"); w.unit("#Pxl", dist);
            w.id("Ckmt"); w.unit("#Prc", 0.0);
            w.id("blur"); w.unit("#Pxl", 5.0);
            w.id("Clr "); w.color(61.0, 27.0, 5.0);
            return w.b;
        };
        const auto glowObj = [&](const double size) {
            Dw w;
            w.raw("Objc", 4); w.i32v(0); w.id("OrGl"); w.i32v(5);
            w.id("enab"); w.boolean(true);
            w.id("Opct"); w.unit("#Prc", 30.0);
            w.id("Ckmt"); w.unit("#Prc", 0.0);
            w.id("blur"); w.unit("#Pxl", size);
            w.id("Clr "); w.color(0.0, 255.0, 24.0);
            return w.b;
        };
        const auto strokeObj = [&](const double size) {
            Dw w;
            w.raw("Objc", 4); w.i32v(0); w.id("FrFX"); w.i32v(6);
            w.id("enab"); w.boolean(true);
            w.id("Opct"); w.unit("#Prc", 100.0);
            w.id("Sz  "); w.unit("#Pxl", size);
            w.id("Styl"); w.enumV("FTst", "FStF");
            w.id("Md  "); w.enumV("BlnM", "Nrml");
            w.id("Clr "); w.color(255.0, 0.0, 0.0);
            return w.b;
        };

        // lmfx: authoritative, two instances of each type
        Dw lmfxW;
        lmfxW.i32v(0); lmfxW.id("null"); lmfxW.i32v(5);
        lmfxW.id("Scl "); lmfxW.unit("#Prc", 100.0);
        lmfxW.id("masterFXSwitch"); lmfxW.boolean(true);
        lmfxW.id("dropShadowMulti");
        lmfxW.raw("VlLs", 4); lmfxW.i32v(2);
        lmfxW.b.append(shadowObj(20.0)); lmfxW.b.append(shadowObj(50.0));
        lmfxW.id("outerGlowMulti");
        lmfxW.raw("VlLs", 4); lmfxW.i32v(2);
        lmfxW.b.append(glowObj(18.0)); lmfxW.b.append(glowObj(36.0));
        lmfxW.id("frameFXMulti");
        lmfxW.raw("VlLs", 4); lmfxW.i32v(2);
        lmfxW.b.append(strokeObj(3.0)); lmfxW.b.append(strokeObj(8.0));

        // lfx2 mirror: single shadow with a DIFFERENT distance - the
        // assertion on dist proves the mirror was overridden
        Dw lfx2W;
        lfx2W.i32v(0); lfx2W.id("null"); lfx2W.i32v(3);
        lfx2W.id("Scl "); lfx2W.unit("#Prc", 100.0);
        lfx2W.id("masterFXSwitch"); lfx2W.boolean(true);
        lfx2W.id("DrSh"); lfx2W.b.append(shadowObj(5.0));

        Dw lmfxBlock;
        lmfxBlock.raw("8BIM", 4); lmfxBlock.raw("lmfx", 4);
        lmfxBlock.u32v(lmfxW.b.size() + 8);
        lmfxBlock.i32v(0); lmfxBlock.i32v(16);
        lmfxBlock.b.append(lmfxW.b);

        Dw lfx2Block;
        lfx2Block.raw("8BIM", 4); lfx2Block.raw("lfx2", 4);
        lfx2Block.u32v(lfx2W.b.size() + 8);
        lfx2Block.i32v(0); lfx2Block.i32v(16);
        lfx2Block.b.append(lfx2W.b);

        // minimal 1-layer PSD carrying both blocks
        Dw psd;
        psd.raw("8BPS", 4); psd.u16v(1);
        for (int i = 0; i < 6; i++) { psd.u8v(0); }
        psd.u16v(3);                    // channels
        psd.i32v(1); psd.i32v(1);       // h, w
        psd.u16v(8); psd.u16v(3);       // depth, RGB
        psd.u32v(0);                    // color mode data
        psd.u32v(0);                    // image resources
        Dw li;
        li.i16v(1);                     // layer count
        li.i32v(0); li.i32v(0); li.i32v(1); li.i32v(1);  // rect
        li.u16v(1);                     // channel count
        li.i16v(0); li.u32v(2);         // channel id 0 (i16), len 2
        li.raw("8BIM", 4); li.raw("norm", 4);
        li.u8v(255); li.u8v(0); li.u8v(0); li.u8v(0);
        Dw extra;
        extra.u32v(0);                  // mask size
        extra.u32v(0);                  // blending ranges
        extra.u8v(1); extra.raw("a", 1);
        extra.u8v(0); extra.u8v(0);
        extra.b.append(lfx2Block.b);
        extra.b.append(lmfxBlock.b);
        li.u32v(extra.b.size()); li.b.append(extra.b);
        if (li.b.size() & 1) { li.u8v(0); }
        Dw lm;
        lm.u32v(li.b.size() + 4); lm.u32v(li.b.size());
        lm.b.append(li.b);
        psd.b.append(lm.b);
        psd.u16v(1); psd.u16v(0);       // channel data stub
        psd.u16v(1); psd.u16v(0);       // image data stub

        const QString tmpPath = QDir::temp().absoluteFilePath(
                    QStringLiteral("friction_lfx_test.psd"));
        {
            QFile f(tmpPath);
            if (!f.open(QIODevice::WriteOnly)) {
                throw std::runtime_error("cannot write temp psd");
            }
            f.write(psd.b);
        }

        psd::PsdFile psdFile;
        QString err;
        if (!psdFile.load(tmpPath, &err)) {
            throw std::runtime_error(("load failed: " + err).toStdString());
        }
        if (psdFile.layers().isEmpty()) {
            throw std::runtime_error("no layers parsed");
        }
        const auto& rec = psdFile.layers().first();
        std::cout << " effects=" << rec.stylesList.size() << " ";
        if (rec.stylesList.size() != 4) {
            throw std::runtime_error("expected 4 style effects (main + 3 extras)");
        }
        if (!rec.stylesFromLmfx) {
            throw std::runtime_error("lmfx authority flag not set");
        }
        const auto& main = rec.stylesList.first();
        if (!main.shadowEnabled || !main.glowEnabled || !main.strokeEnabled) {
            throw std::runtime_error("main effect missing a style");
        }
        // the mirror said dist 5, lmfx says 20
        if (qAbs(main.shadowDistance - 20.0) > 0.01) {
            throw std::runtime_error("lfx2 mirror was not overridden by lmfx");
        }
        for (int i = 1; i < rec.stylesList.size(); i++) {
            const auto& e = rec.stylesList.at(i);
            const int on = int(e.shadowEnabled) + int(e.glowEnabled)
                           + int(e.strokeEnabled);
            if (on != 1) {
                throw std::runtime_error("extra effect is not solo");
            }
        }
    });

    // Test 3: Menu registry coverage
    runTest("Test 3: RasterEffectMenuCreator coverage", [&]() {
        int count = 0;
        RasterEffectMenuCreator::forEveryEffectCore(
            [&](const QString& name, const QString& cat,
                const RasterEffectMenuCreator::EffectCreator& creator) {
                Q_UNUSED(cat)
                auto eff = creator();
                if (!eff) {
                    throw std::runtime_error("Menu creator produced null effect: " + name.toStdString());
                }
                count++;
            });

        if (count < 20) {
            throw std::runtime_error("Expected at least 20 core effects in menu, found " + std::to_string(count));
        }
    });

    // Test 4: Chinese translation resource load test
    runTest("Test 4: Chinese (zh_CN) Translation Loading", [&]() {
        QTranslator translator;
        const bool loaded = translator.load(":/translations/friction_zh_CN.qm");
        if (!loaded) {
            throw std::runtime_error("Failed to load :/translations/friction_zh_CN.qm resource");
        }
    });

    // Test 7: Page Curl CPU math (identity at zero progress, curl at mid)
    runTest("Test 7: Page Curl CPU math", [&]() {
        const auto eff = createRasterEffectForNonCustomType(
                RasterEffectType::PAGE_CURL);
        if (!eff) { throw std::runtime_error("Factory returned null"); }
        // the effect defaults to wave mode; pin curl mode for these checks
        eff->ca_getChildAt<ComboBoxProperty>(0)->setCurrentValue(0);

        SkBitmap src;
        src.allocN32Pixels(128, 128);
        src.eraseARGB(255, 200, 100, 50);

        const auto render = [&](SkBitmap& dst) {
            dst.allocN32Pixels(128, 128);
            dst.eraseARGB(0, 0, 0, 0);
            const auto caller = eff->getEffectCaller(0.0, 1.0, 1.0, nullptr);
            if (!caller) { throw std::runtime_error("null caller"); }
            CpuRenderTools tools{src, dst};
            CpuRenderData data;
            data.fTexTile = SkIRect::MakeXYWH(0, 0, 128, 128);
            caller->processCpu(tools, data);
        };

        // identity: progress 0 must be an exact passthrough
        {
            SkBitmap dst;
            render(dst);
            for (int y = 0; y < 128; y += 5) {
                for (int x = 0; x < 128; x += 5) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) != 255 ||
                        qAbs(int(SkColorGetR(c)) - 200) > 2 ||
                        qAbs(int(SkColorGetG(c)) - 100) > 2 ||
                        qAbs(int(SkColorGetB(c)) - 50) > 2) {
                        throw std::runtime_error("progress 0 is not identity");
                    }
                }
            }
        }

        // mid progress, default direction (right edge rolls leftward):
        // the consumed far-right side empties, the kept left side stays
        // opaque and shaded, and the back face shows near the tube
        const auto prog = eff->ca_getChildAt<QrealAnimator>(1);
        if (!prog) { throw std::runtime_error("no progress animator"); }
        prog->setCurrentBaseValue(50.0);
        {
            SkBitmap dst;
            render(dst);
            // far right: page has left
            if (SkColorGetA(dst.getColor(126, 64)) != 0) {
                throw std::runtime_error("consumed side is not transparent");
            }
            // far left: still opaque, shaded darker than the source
            const SkColor c = dst.getColor(6, 64);
            if (SkColorGetA(c) != 255) {
                throw std::runtime_error("kept side lost opacity");
            }
            if (SkColorGetR(c) >= 200 || SkColorGetR(c) < 60) {
                throw std::runtime_error("kept side not lit/shaded");
            }
            // near the tube the flipped back face (gray) must show:
            // some pixel there has blue >= red, unlike the orange front
            bool sawBack = false;
            for (int y = 20; y < 108 && !sawBack; y += 4) {
                for (int x = 30; x < 66; x += 2) {
                    const SkColor b = dst.getColor(x, y);
                    if (SkColorGetA(b) > 200 &&
                        SkColorGetB(b) >= SkColorGetR(b)) {
                        sawBack = true;
                        break;
                    }
                }
            }
            if (!sawBack) {
                throw std::runtime_error("back face never visible");
            }
        }
        prog->setCurrentBaseValue(0.0);

        // wave mode: the image must stay fully visible - amplitude 0 is
        // an exact passthrough, amplitude 8 keeps every pixel opaque and
        // shaded (no transparency anywhere)
        const auto mode = eff->ca_getChildAt<ComboBoxProperty>(0);
        const auto amp = eff->ca_getChildAt<QrealAnimator>(10);
        if (!mode || !amp) { throw std::runtime_error("no mode/amp animator"); }
        mode->setCurrentValue(1);
        amp->setCurrentBaseValue(0.0);
        {
            SkBitmap dst;
            render(dst);
            const SkColor c = dst.getColor(64, 64);
            if (SkColorGetA(c) != 255 ||
                qAbs(int(SkColorGetR(c)) - 200) > 2 ||
                qAbs(int(SkColorGetG(c)) - 100) > 2 ||
                qAbs(int(SkColorGetB(c)) - 50) > 2) {
                throw std::runtime_error("wave amp 0 is not identity");
            }
        }
        amp->setCurrentBaseValue(8.0);
        {
            SkBitmap dst;
            render(dst);
            for (int y = 0; y < 128; y += 5) {
                for (int x = 0; x < 128; x += 5) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) != 255) {
                        throw std::runtime_error("wave mode lost opacity");
                    }
                }
            }
            const SkColor a = dst.getColor(10, 64);
            const SkColor b = dst.getColor(118, 64);
            if (a == b) {
                throw std::runtime_error("wave shading has no variation");
            }
        }
        // crossed wave keeps everything opaque too
        eff->ca_getChildAt<QrealAnimator>(16)->setCurrentBaseValue(50.0);
        {
            SkBitmap dst;
            render(dst);
            const SkColor c = dst.getColor(64, 10);
            if (SkColorGetA(c) != 255) {
                throw std::runtime_error("crossed wave lost opacity");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(16)->setCurrentBaseValue(0.0);
        mode->setCurrentValue(0);
        amp->setCurrentBaseValue(0.0);

        // page turn (mode 2): mid progress shows the flipped back and
        // keeps a large opaque area; full progress lies flat mirrored
        mode->setCurrentValue(2);
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(50.0); // progress
        {
            SkBitmap dst;
            render(dst);
            int opaque = 0;
            int backish = 0;
            for (int y = 0; y < 128; y += 6) {
                for (int x = 0; x < 128; x += 6) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) == 255) opaque++;
                    if (SkColorGetA(c) == 255 &&
                        qAbs(int(SkColorGetB(c)) - int(SkColorGetR(c))) < 12 &&
                        SkColorGetR(c) > 100) backish++;
                }
            }
            if (opaque < 100) {
                throw std::runtime_error("page turn lost the image");
            }
            if (backish < 8) {
                throw std::runtime_error("page turn back never visible");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(100.0);
        {
            SkBitmap dst;
            render(dst);
            int opaque = 0;
            for (int y = 0; y < 128; y += 6) {
                for (int x = 0; x < 128; x += 6) {
                    if (SkColorGetA(dst.getColor(x, y)) == 255) opaque++;
                }
            }
            if (opaque < 300) {
                throw std::runtime_error("full turn does not lie flat");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(0.0);

        // slant + perspective + spiral on curl mode: no crash, output
        // stays bounded and the far side still empties
        mode->setCurrentValue(0);
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(50.0);
        eff->ca_getChildAt<QrealAnimator>(13)->setCurrentBaseValue(60.0); // slant
        eff->ca_getChildAt<QrealAnimator>(14)->setCurrentBaseValue(40.0); // perspective
        eff->ca_getChildAt<QrealAnimator>(15)->setCurrentBaseValue(30.0); // spiral
        {
            SkBitmap dst;
            render(dst);
            int opaque = 0;
            for (int y = 0; y < 128; y += 6) {
                for (int x = 0; x < 128; x += 6) {
                    const SkColor c = dst.getColor(x, y);
                    if (SkColorGetA(c) == 255 &&
                        (SkColorGetR(c) > 250 || SkColorGetG(c) > 250)) {
                        throw std::runtime_error("slant/perspective/spline produced unclamped output");
                    }
                    if (SkColorGetA(c) == 255) opaque++;
                }
            }
            if (opaque < 80) {
                throw std::runtime_error("slant/perspective destroyed the image");
            }
        }
        eff->ca_getChildAt<QrealAnimator>(13)->setCurrentBaseValue(0.0);
        eff->ca_getChildAt<QrealAnimator>(14)->setCurrentBaseValue(0.0);
        eff->ca_getChildAt<QrealAnimator>(15)->setCurrentBaseValue(0.0);
        eff->ca_getChildAt<QrealAnimator>(1)->setCurrentBaseValue(0.0);

    });

    // Test 5: ThemeSupport presets, accents and style generation test
    runTest("Test 5: ThemeSupport presets and styling", [&]() {
        const auto &presets = ThemeSupport::themePresetList();
        if (presets.size() < 10) {
            throw std::runtime_error("Expected at least 10 theme presets, got " + std::to_string(presets.size()));
        }

        const auto &accents = ThemeSupport::accentPresetList();
        if (accents.size() < 10) {
            throw std::runtime_error("Expected at least 10 accent presets, got " + std::to_string(accents.size()));
        }

        // Test theme switching
        ThemeSupport::setThemeFromId(QStringLiteral("morandi_dark"));
        if (ThemeSupport::themeId() != QStringLiteral("morandi_dark")) {
            throw std::runtime_error("Theme ID mismatch for morandi_dark");
        }
        if (!ThemeSupport::getThemeBaseColor().isValid()) {
            throw std::runtime_error("Invalid base color for morandi_dark");
        }
        if (!ThemeSupport::getThemeHighlightColor().isValid()) {
            throw std::runtime_error("Invalid highlight color for morandi_dark");
        }

        // Test custom radius and scrollbar
        ThemeSupport::setBorderRadius(8);
        if (ThemeSupport::borderRadius() != 8) {
            throw std::runtime_error("Failed to set border radius to 8");
        }
        ThemeSupport::setScrollbarWidth(6);
        if (ThemeSupport::scrollbarWidth() != 6) {
            throw std::runtime_error("Failed to set scrollbar width to 6");
        }

        const QString style = ThemeSupport::getThemeStyle(20);
        if (style.isEmpty()) {
            throw std::runtime_error("Generated theme style string is empty");
        }

        // Restore friction theme
        ThemeSupport::setThemeFromId(QStringLiteral("friction"));
    });

    // Test 6: AI MCP Tool Dispatcher & Schema validation
    runTest("Test 6: AI MCP Tool Dispatcher & Schema validation", [&]() {
        Friction::AI::McpDispatcher dispatcher;
        const auto schema = dispatcher.getToolsSchema();
        if (schema.isEmpty()) {
            throw std::runtime_error("McpDispatcher tools schema is empty");
        }

        bool hasSceneInfo = false;
        bool hasCreateLayer = false;
        bool hasSetKeyframe = false;
        bool hasSetKeyframeEasing = false;
        bool hasSetInOutPoint = false;
        bool hasSetLayerOrder = false;
        bool hasEvalScript = false;
        bool hasCapture = false;
        bool hasKeyframeEasingParam = false;
        bool hasRenderMarkup = false;
        bool hasUpdateLayer = false;
        bool hasAnimateLayer = false;
        bool hasGetStoryboard = false;

        for (const auto &val : schema) {
            const auto obj = val.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString();
            if (name == QStringLiteral("friction_get_scene_info")) hasSceneInfo = true;
            if (name == QStringLiteral("friction_create_layer")) hasCreateLayer = true;
            if (name == QStringLiteral("friction_set_keyframe")) {
                hasSetKeyframe = true;
                const auto inputSchema = obj.value(QStringLiteral("inputSchema")).toObject();
                const auto props = inputSchema.value(QStringLiteral("properties")).toObject();
                if (props.contains(QStringLiteral("easing"))) hasKeyframeEasingParam = true;
            }
            if (name == QStringLiteral("friction_set_keyframe_easing")) hasSetKeyframeEasing = true;
            if (name == QStringLiteral("friction_set_in_out_point")) hasSetInOutPoint = true;
            if (name == QStringLiteral("friction_set_layer_order")) hasSetLayerOrder = true;
            if (name == QStringLiteral("friction_eval_script")) hasEvalScript = true;
            if (name == QStringLiteral("friction_capture_viewport")) hasCapture = true;
            if (name == QStringLiteral("friction_render_markup")) hasRenderMarkup = true;
            if (name == QStringLiteral("friction_update_layer")) hasUpdateLayer = true;
            if (name == QStringLiteral("friction_animate_layer")) hasAnimateLayer = true;
            if (name == QStringLiteral("friction_get_storyboard")) hasGetStoryboard = true;
        }

        if (!hasSceneInfo || !hasCreateLayer || !hasSetKeyframe || !hasEvalScript || !hasCapture) {
            throw std::runtime_error("Required MCP tools missing from schema");
        }
        if (!hasSetKeyframeEasing || !hasSetInOutPoint || !hasSetLayerOrder) {
            throw std::runtime_error("Newly added MCP animation tools missing from schema");
        }
        if (!hasKeyframeEasingParam) {
            throw std::runtime_error("friction_set_keyframe schema missing 'easing' parameter");
        }
        if (!hasRenderMarkup || !hasUpdateLayer || !hasAnimateLayer || !hasGetStoryboard) {
            throw std::runtime_error("Advanced AI orchestration tools (markup, update, animate, storyboard) missing from schema");
        }

        // Test tool dispatcher error handling / execution path for newly registered tools
        QJsonObject dummyArgs;
        dummyArgs[QStringLiteral("index")] = 1;
        const auto respEasing = dispatcher.dispatchTool(QStringLiteral("friction_set_keyframe_easing"), dummyArgs);
        if (!respEasing.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_set_keyframe_easing dispatch response malformed");
        }

        const auto respInOut = dispatcher.dispatchTool(QStringLiteral("friction_set_in_out_point"), dummyArgs);
        if (!respInOut.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_set_in_out_point dispatch response malformed");
        }

        dummyArgs[QStringLiteral("order")] = QStringLiteral("top");
        const auto respOrder = dispatcher.dispatchTool(QStringLiteral("friction_set_layer_order"), dummyArgs);
        if (!respOrder.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_set_layer_order dispatch response malformed");
        }

        // Test update_layer dispatch
        QJsonObject updateArgs;
        updateArgs[QStringLiteral("name")] = QStringLiteral("NonExistentLayer");
        updateArgs[QStringLiteral("opacity")] = 50.0;
        const auto respUpdate = dispatcher.dispatchTool(QStringLiteral("friction_update_layer"), updateArgs);
        if (!respUpdate.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_update_layer dispatch response malformed");
        }

        // Test animate_layer dispatch
        QJsonObject animArgs;
        animArgs[QStringLiteral("name")] = QStringLiteral("NonExistentLayer");
        animArgs[QStringLiteral("preset")] = QStringLiteral("pop");
        const auto respAnim = dispatcher.dispatchTool(QStringLiteral("friction_animate_layer"), animArgs);
        if (!respAnim.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_animate_layer dispatch response malformed");
        }

        // Test render_markup dispatch validation (empty markup returns error)
        QJsonObject markupArgs;
        markupArgs[QStringLiteral("markup")] = QStringLiteral("");
        const auto respMarkup = dispatcher.dispatchTool(QStringLiteral("friction_render_markup"), markupArgs);
        if (respMarkup.value(QStringLiteral("success")).toBool() != false) {
            throw std::runtime_error("friction_render_markup should fail gracefully on empty markup");
        }

        // Test storyboard dispatch (handled without crash when window absent in test harness)
        QJsonObject sbArgs;
        sbArgs[QStringLiteral("numFrames")] = 3;
        const auto respSb = dispatcher.dispatchTool(QStringLiteral("friction_get_storyboard"), sbArgs);
        if (!respSb.contains(QStringLiteral("success"))) {
            throw std::runtime_error("friction_get_storyboard dispatch response malformed");
        }
    });

    // Test 7: AI MCP Server JSON-RPC Protocol Parser
    runTest("Test 7: AI MCP Server JSON-RPC Protocol Parser", [&]() {
        Friction::AI::McpServer server;

        // Test initialize
        QJsonObject initReq;
        initReq[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        initReq[QStringLiteral("id")] = 1;
        initReq[QStringLiteral("method")] = QStringLiteral("initialize");

        const auto initResp = server.processJsonRpc(initReq);
        if (initResp.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
            throw std::runtime_error("Invalid jsonrpc version in response");
        }
        if (initResp.value(QStringLiteral("id")).toInt() != 1) {
            throw std::runtime_error("Mismatch response id in initialize");
        }
        const auto resObj = initResp.value(QStringLiteral("result")).toObject();
        if (!resObj.contains(QStringLiteral("serverInfo"))) {
            throw std::runtime_error("serverInfo missing in initialize result");
        }

        // Test ping
        QJsonObject pingReq;
        pingReq[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        pingReq[QStringLiteral("id")] = 2;
        pingReq[QStringLiteral("method")] = QStringLiteral("ping");

        const auto pingResp = server.processJsonRpc(pingReq);
        if (pingResp.value(QStringLiteral("id")).toInt() != 2) {
            throw std::runtime_error("Mismatch response id in ping");
        }

        // Test tools/list
        QJsonObject listReq;
        listReq[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        listReq[QStringLiteral("id")] = 3;
        listReq[QStringLiteral("method")] = QStringLiteral("tools/list");

        const auto listResp = server.processJsonRpc(listReq);
        const auto listResult = listResp.value(QStringLiteral("result")).toObject();
        if (!listResult.contains(QStringLiteral("tools")) || !listResult.value(QStringLiteral("tools")).isArray()) {
            throw std::runtime_error("Invalid tools array in tools/list result");
        }
    });

    // Test 8: Kinetic Text & Layer Animation Presets
    runTest("Test 8: Kinetic Text & Layer Animation Presets", [&]() {
        const auto& textPresets = TextAnimPresets::all();
        if (textPresets.size() < 160) {
            throw std::runtime_error(QString("Text presets count too low: %1 (expected >= 160)").arg(textPresets.size()).toStdString());
        }

        const auto& layerPresets = LayerAnimPresets::all();
        if (layerPresets.size() < 60) {
            throw std::runtime_error(QString("Layer presets count too low: %1 (expected >= 60)").arg(layerPresets.size()).toStdString());
        }

        const int totalPresets = textPresets.size() + layerPresets.size();
        if (totalPresets < 220) {
            throw std::runtime_error(QString("Total presets count too low: %1 (expected >= 220)").arg(totalPresets).toStdString());
        }

        // Verify key text presets from each archetype exist and have valid fields
        const QStringList keyTextIds = {
            "sharp-snap-rise", "sharp-elastic-pop", "sharp-blade-cut",
            "sharp-double-bounce", "sharp-jelly-squash", "sharp-trampoline",
            "smooth-float-rise", "smooth-cinematic-fade", "smooth-aurora",
            "smooth-par-float", "smooth-bloom-slow",
            "prop-pos-x-left", "prop-scale-uniform", "prop-rot-full-360", "prop-shear-slash-x",
            "prop-scale-wide-8x",
            "3d-flip-y-cw", "3d-corkscrew", "3d-barrel-roll", "3d-door-swing-left",
            "tech-typewriter-std", "tech-number-roll", "tech-matrix-rain",
            "tech-binary-matrix", "tech-crt-scan",
            "loop-sine-wave", "loop-breathe-soft", "loop-heartbeat", "loop-rainbow-wave"
        };
        for (const auto& id : keyTextIds) {
            const auto p = TextAnimPresets::byId(id);
            if (!p) {
                throw std::runtime_error(QString("Missing key text preset: %1").arg(id).toStdString());
            }
            if (p->name.isEmpty() || p->duration <= 0.0 || p->tag.isEmpty()) {
                throw std::runtime_error(QString("Invalid data in text preset: %1").arg(id).toStdString());
            }
        }

        // Verify key text presets have diverse and distinct physical easings
        const auto snapPreset = TextAnimPresets::byId("sharp-snap-rise");
        if (!snapPreset || snapPreset->easing != TextEasing::sharpSnap) {
            throw std::runtime_error("sharp-snap-rise missing sharpSnap easing");
        }
        const auto bouncePreset = TextAnimPresets::byId("sharp-overshoot-down");
        if (!bouncePreset || bouncePreset->easing != TextEasing::bounce) {
            throw std::runtime_error("sharp-overshoot-down missing bounce easing");
        }
        const auto elasticPreset = TextAnimPresets::byId("sharp-elastic-pop");
        if (!elasticPreset || elasticPreset->easing != TextEasing::elastic) {
            throw std::runtime_error("sharp-elastic-pop missing elastic easing");
        }
        const auto anticipatePreset = TextAnimPresets::byId("3d-corkscrew");
        if (!anticipatePreset || anticipatePreset->easing != TextEasing::anticipate) {
            throw std::runtime_error("3d-corkscrew missing anticipate easing");
        }
        const auto steppedPreset = TextAnimPresets::byId("tech-typewriter-std");
        if (!steppedPreset || steppedPreset->easing != TextEasing::stepped) {
            throw std::runtime_error("tech-typewriter-std missing stepped easing");
        }

        // Verify TextEffect setups physics correctly
        const auto effect = enve::make_shared<TextEffect>();
        effect->setupFromPreset(*elasticPreset, 200.0, 48.0, 0, 30.0, 1.0);
        if (!effect->hasCustomPhysics()) {
            throw std::runtime_error("TextEffect failed to initialize custom physics");
        }
        if (effect->getEasing() != TextEasing::elastic) {
            throw std::runtime_error("TextEffect easing mismatch");
        }

        // Verify key layer presets exist and have valid generators
        const QStringList keyLayerIds = {
            "l-fade", "l-pop", "l-drop", "l-flip-x",
            "l-swing", "l-skew-slide", "l-elastic-scale", "l-orbit-3d",
            "l-door-open-l", "l-dive-3d", "l-jelly-wobble", "l-heavy-stamp-jitter",
            "l-sheet-slide-up", "l-glitch-shake", "l-heartbeat-layer"
        };
        for (const auto& id : keyLayerIds) {
            const auto p = LayerAnimPresets::byId(id);
            if (!p) {
                throw std::runtime_error(QString("Missing key layer preset: %1").arg(id).toStdString());
            }
            if (p->name.isEmpty() || p->duration <= 0.0 || (!p->gen && !p->outGen)) {
                throw std::runtime_error(QString("Invalid data or missing generator in layer preset: %1").arg(id).toStdString());
            }
        }
    });

    // Test 9: effect reorder drop (properties-panel drag path, was SIGSEGV)
    runTest("Test 9: Effect reorder drop (SWT_dropInto)", [&]() {
        // PathBox ctor reads eSettings (last used stroke width)
        static eSettings probeSettings(HardwareInfo::sCpuThreads(),
                                       HardwareInfo::sRamKB());
        Q_UNUSED(probeSettings)
        const auto box = enve::make_shared<RectangleBox>();
        const auto coll = box->rasterEffectsCollection();
        coll->addChild(enve::make_shared<BlurEffect>());
        coll->addChild(enve::make_shared<ThresholdEffect>());
        coll->addChild(enve::make_shared<ShadowEffect>());
        if (coll->ca_getNumberOfChildren() != 3) {
            throw std::runtime_error("effect setup failed");
        }
        const QStringList namesBefore = [&]() {
            QStringList n;
            for (int i = 0; i < coll->ca_getNumberOfChildren(); i++) {
                n << coll->getChild(i)->prp_getName();
            }
            return n;
        }();

        // UI drop path: drag the row of the last effect, drop above the
        // first (index 0); then drag the first to the bottom, etc.
        for (int round = 0; round < 6; round++) {
            const int n = coll->ca_getNumberOfChildren();
            RasterEffect* dragged = coll->getChild(round % 2 ? 0 : n - 1);
            // round%4==3 reproduces the move-to-bottom drop (index n,
            // the pre-removal count) that used to corrupt the heap
            const int dropId = round % 4 == 3 ? n : round % 2 ? n - 1 : 0;
            const eMimeData mime(QList<RasterEffect*>{ dragged });
            coll->SWT_dropInto(dropId, &mime);
        }

        const int nAfter = coll->ca_getNumberOfChildren();
        if (nAfter != 3) {
            throw std::runtime_error(QString("children count changed: %1").arg(nAfter).toStdString());
        }
        // same set of effects, just reordered
        for (int i = 0; i < nAfter; i++) {
            if (!namesBefore.contains(coll->getChild(i)->prp_getName())) {
                throw std::runtime_error("effect set changed after reorder");
            }
        }
        std::cout << "order now:";
        for (int i = 0; i < nAfter; i++) {
            std::cout << " [" << coll->getChild(i)->prp_getName().toStdString() << "]";
        }
        std::cout << " ";
    });


    // Test 10: stacked effects must compose sequentially (AE order
    // semantics): both effects participate and order matters
    runTest("Test 10: Effect stacking order semantics (CPU chain)", [&]() {
        static eSettings settings10(HardwareInfo::sCpuThreads(),
                                    HardwareInfo::sRamKB());
        Q_UNUSED(settings10)
        // gradient source with luma AND alpha variance
        SkBitmap src0;
        src0.allocN32Pixels(64, 64);
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                const int luma = (x * 4 + y * 2) % 256;
                const int alpha = (x < 32) ? 255 : 128;
                src0.eraseArea(SkIRect::MakeXYWH(x, y, 1, 1),
                               SkColorSetARGB(alpha, luma, luma / 2, 255 - luma));
            }
        }

        // two CPU-capable effects: threshold (luma binarize) + choker
        // (alpha choke) - composition must depend on order
        const auto mkThreshold = []() {
            const auto e = enve::make_shared<ThresholdEffect>();
            return e;
        };
        const auto mkChoker = []() {
            const auto e = enve::make_shared<SimpleChokerEffect>();
            // push the first real param (choke matte) off zero so the
            // effect actually transforms the image
            for (int i = 0; i < e->ca_getNumberOfChildren(); i++) {
                const auto qa = enve_cast<QrealAnimator*>(e->ca_getChildAt(i));
                if (qa) { qa->setCurrentBaseValue(35.0); break; }
            }
            return e;
        };

        // pipeline-equivalent serial application: each effect consumes
        // the previous effect's output (EffectSubTaskSpawner pattern)
        const auto copyBtmp = [](const SkBitmap& src) {
            SkBitmap dst;
            dst.allocPixels(src.info());
            dst.eraseARGB(0, 0, 0, 0);
            for (int y = 0; y < src.height(); y++) {
                memcpy(dst.getAddr32(0, y), src.getAddr32(0, y),
                       src.width() * 4);
            }
            return dst;
        };
        const auto applyChain = [&](const QList<RasterEffect*>& effs) {
            SkBitmap cur = copyBtmp(src0);
            for (const auto e : effs) {
                const auto caller = e->getEffectCaller(0.0, 1.0, 1.0, nullptr);
                if (!caller) throw std::runtime_error("null caller in chain");
                SkBitmap src = copyBtmp(cur);
                SkBitmap dst;
                dst.allocPixels(src.info());
                dst.eraseARGB(0, 0, 0, 0);
                CpuRenderTools tools{src, dst};
                CpuRenderData data;
                data.fTexTile = src.bounds();
                data.fWidth = static_cast<uint>(src.width());
                data.fHeight = static_cast<uint>(src.height());
                caller->processCpu(tools, data);
                if (caller->srcDstSeparation()) cur = dst; else cur = src;
            }
            return cur;
        };

        const auto hashBtmp = [](const SkBitmap& b) {
            QCryptographicHash h(QCryptographicHash::Md5);
            for (int y = 0; y < b.height(); y++) {
                h.addData(reinterpret_cast<const char*>(b.getAddr32(0, y)),
                          b.width() * 4);
            }
            return h.result();
        };

        const auto thr1 = mkThreshold();
        const auto chk1 = mkChoker();
        const auto chainTC = applyChain({thr1.data(), chk1.data()});

        const auto thr2 = mkThreshold();
        const auto chk2 = mkChoker();
        const auto chainCT = applyChain({chk2.data(), thr2.data()});

        const auto thr3 = mkThreshold();
        const auto onlyT = applyChain({thr3.data()});
        const auto chk3 = mkChoker();
        const auto onlyC = applyChain({chk3.data()});

        const auto hTC = hashBtmp(chainTC);
        const auto hCT = hashBtmp(chainCT);
        const auto hT = hashBtmp(onlyT);
        const auto hC = hashBtmp(onlyC);
        const auto hSrc = hashBtmp(src0);

        // sanity: the effects actually do something
        if (hT == hSrc || hC == hSrc) {
            throw std::runtime_error("effect is a no-op on the source");
        }
        // order matters (AE-style sequential composition)
        if (hTC == hCT) {
            throw std::runtime_error("chain(T,C) == chain(C,T): order ignored");
        }
        // both effects participate: result is neither single effect alone
        if (hTC == hT || hTC == hC || hCT == hT || hCT == hC) {
            throw std::runtime_error("stacked result equals a single effect: not chained");
        }
        std::cout << "(2-effect chain order-sensitive, both applied) ";
    });

    // Test 11: rapid edits must cancel the in-flight stale renders
    // through the REAL scheduler - canceled tasks must not resurrect,
    // the pool must drain (no hang), and the surviving render must
    // carry the current state id (regression guard for the
    // stale-render-cancellation machinery).
    // The document/scene machinery needs a QGuiApplication while the
    // ThemeSupport tests crash under one (getIconSize headless) - so
    // the storm runs re-exec'd in FRICTION_CANCEL_PROBE mode (offscreen
    // QGuiApplication, this test only) and the exit code is asserted
    runTest("Test 11: stale render cancellation (real scheduler)", [&]() {
        if (!qEnvironmentVariableIsSet("FRICTION_CANCEL_PROBE")) {
            QProcessEnvironment env =
                    QProcessEnvironment::systemEnvironment();
            env.insert("FRICTION_CANCEL_PROBE", "1");
            env.insert("QT_QPA_PLATFORM", "offscreen");
            QProcess proc;
            proc.setProcessEnvironment(env);
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.start(QCoreApplication::applicationFilePath());
            if (!proc.waitForStarted(5000)) {
                throw std::runtime_error("probe re-exec failed to start");
            }
            QElapsedTimer guard; guard.start();
            while (!proc.waitForFinished(100)) {
                std::cout << proc.readAllStandardOutput().toStdString()
                          << std::flush;
                if (proc.state() != QProcess::Running) break;
                if (guard.elapsed() > 180000) {
                    proc.kill();
                    throw std::runtime_error("probe timed out (hang)");
                }
            }
            std::cout << proc.readAllStandardOutput().toStdString()
                      << std::flush;
            if (proc.exitStatus() != QProcess::NormalExit ||
                proc.exitCode() != 0) {
                throw std::runtime_error("probe crashed or failed (exit " +
                                         std::to_string(proc.exitCode()) + ")");
            }
            return;
        }
        cancelStormBody();
    });

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Unit Test Summary: " << passed << " passed, " << failed << " failed." << std::endl;
    std::cout << "=========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}
