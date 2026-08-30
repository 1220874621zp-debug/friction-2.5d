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

#include "mainwindow.h"
#include "GUI/Expressions/expressiondialog.h"
#include "canvas.h"
#include <QKeyEvent>
#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QUrl>
#include <QStatusBar>
#include <QToolBar>
#include <QMenuBar>
#include <QMessageBox>
#include <QAudioOutput>
#include <QSpacerItem>
#include <QMargins>
#include <iostream>
#include <QClipboard>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QScrollBar>
#include <QMenu>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QKeySequence>
#include <QTabBar>
#include <QGroupBox>
#include <QTimer>
#include <QStandardPaths>
#include <QTextStream>
#include <QInputDialog>

#include "dialogs/applyexpressiondialog.h"
#include "dialogs/markereditordialog.h"
#include "timelinedockwidget.h"
#include "easingpresetspanel.h"
#include "effectspresetspanel.h"
#include "quickeffectsearchdialog.h"
#include "scriptmanager.h"
#include "scriptconsole.h"
#include "GUI/keysview.h"
#include "canvaswindow.h"
#include "aepropertiesinspector.h"
#include "GUI/BoxesList/boxscrollwidget.h"
#include "clipboardcontainer.h"
#include "optimalscrollarena/scrollarea.h"
#include "GUI/BoxesList/boxscroller.h"
#include "GUI/RenderWidgets/renderwidget.h"
#include "GUI/global.h"
#include "filesourcescache.h"
#include "widgets/fillstrokesettings.h"

#include "Sound/soundcomposition.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "memoryhandler.h"
#include "dialogs/scenesettingsdialog.h"
#include "importhandler.h"
#include "eimporters.h"
#include "dialogs/exportsvgdialog.h"
#include "widgets/alignwidget.h"
#include "widgets/welcomedialog.h"
#include "Boxes/textbox.h"
#include "misc/noshortcutaction.h"
#include "efiltersettings.h"
#include "Settings/settingsdialog.h"
#include "appsupport.h"
#include "themesupport.h"

#include "widgets/assetswidget.h"
#include "dialogs/adjustscenedialog.h"
#include "dialogs/commandpalette.h"
#include "wizards/installpresets.h"

using namespace Friction;

MainWindow *MainWindow::sInstance = nullptr;

namespace {
// In-memory debug log buffer, filled by the Qt message handler and by
// the user-action logger below. Bounded so it can never grow without limit.
QMutex gDebugLogMutex;
QStringList gDebugLogLines;
const int gDebugLogMaxLines = 3000;
QtMessageHandler gDefaultMessageHandler = nullptr;

// Mirror of the in-memory log kept on disk, so the log can still be
// inspected with an external editor if the UI freezes or crashes.
class DebugLogFile {
public:
    DebugLogFile() {
        mPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                + QStringLiteral("/friction_debug.log");
        mFile.setFileName(mPath);
        if (mFile.open(QIODevice::WriteOnly | QIODevice::Text
                       | QIODevice::Truncate)) {
            mStream.setDevice(&mFile);
            mStream.setCodec("UTF-8");
        }
    }
    void append(const QString &line) {
        if (!mStream.device()) { return; }
        mStream << line << "\n";
        mStream.flush();
    }
    const QString &path() const { return mPath; }
private:
    QFile mFile;
    QTextStream mStream;
    QString mPath;
};
Q_GLOBAL_STATIC(DebugLogFile, gDebugLogFile)

void debugLogAppendLine(const QString &typeName,
                        const QString &msg,
                        const QString &suffix = QString())
{
    QString line = QStringLiteral("[%1] %2: %3")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")),
                 typeName, msg);
    if (!suffix.isEmpty()) { line += suffix; }
    {
        // the file append must be inside the same mutex: concurrent
        // writes from multiple threads would corrupt the shared
        // QTextStream and can deadlock the emitting thread
        QMutexLocker lock(&gDebugLogMutex);
        gDebugLogLines.append(line);
        while (gDebugLogLines.size() > gDebugLogMaxLines) {
            gDebugLogLines.removeFirst();
        }
        if (gDebugLogFile) { gDebugLogFile->append(line); }
    }
}

void debugLogMessageHandler(const QtMsgType type,
                            const QMessageLogContext &context,
                            const QString &msg)
{
    QString typeName;
    switch (type) {
        case QtDebugMsg:    typeName = QStringLiteral("DEBUG"); break;
        case QtWarningMsg:  typeName = QStringLiteral("WARN");  break;
        case QtCriticalMsg: typeName = QStringLiteral("CRIT");  break;
        case QtFatalMsg:    typeName = QStringLiteral("FATAL"); break;
        case QtInfoMsg:     typeName = QStringLiteral("INFO");  break;
    }
    QString suffix;
    if (context.file) {
        suffix = QStringLiteral(" (%1:%2)")
                .arg(QString::fromLatin1(context.file))
                .arg(context.line);
    }
    debugLogAppendLine(typeName, msg, suffix);
    if (gDefaultMessageHandler) {
        gDefaultMessageHandler(type, context, msg);
    }
}

// --- user action logging ----------------------------------------------

QString debugLogClip(const QString &s,
                     const int maxLen = 40)
{
    const auto t = s.simplified();
    return t.size() > maxLen ? t.left(maxLen) + QStringLiteral("...") : t;
}

QString debugLogDescribeWidget(const QWidget *w)
{
    QString desc = QString::fromLatin1(w->metaObject()->className());
    QString label;
    const auto btn = qobject_cast<const QAbstractButton*>(w);
    const auto lbl = qobject_cast<const QLabel*>(w);
    const auto gb = qobject_cast<const QGroupBox*>(w);
    const auto tab = qobject_cast<const QTabBar*>(w);
    if (btn) { label = btn->text(); }
    else if (lbl) { label = lbl->text(); }
    else if (gb) { label = gb->title(); }
    else if (tab) {
        label = tab->tabText(tab->tabAt(tab->mapFromGlobal(QCursor::pos())));
    }
    if (label.simplified().isEmpty()) { label = w->objectName(); }
    if (!label.simplified().isEmpty()) {
        desc += QStringLiteral(" \"%1\"").arg(debugLogClip(label));
    }
    const auto win = w->window();
    if (win && win != w) {
        const auto wt = win->windowTitle().simplified();
        if (!wt.isEmpty()) {
            desc += QStringLiteral(" @ %1").arg(debugLogClip(wt, 30));
        } else if (!win->objectName().isEmpty()) {
            desc += QStringLiteral(" @ %1").arg(debugLogClip(win->objectName(), 30));
        }
    }
    return desc;
}

bool debugLogIsTextEntry(const QWidget *w)
{
    return qobject_cast<const QLineEdit*>(w)
        || qobject_cast<const QTextEdit*>(w)
        || qobject_cast<const QPlainTextEdit*>(w);
}

bool debugLogIsModifierKey(const int key)
{
    switch (key) {
        case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Meta:
        case Qt::Key_Alt: case Qt::Key_AltGr: case Qt::Key_CapsLock:
        case Qt::Key_NumLock: case Qt::Key_ScrollLock:
            return true;
        default:
            return false;
    }
}

// Records every user interaction (mouse clicks, menu items, key presses)
// into the debug log buffer.
class DebugEventLogger : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const auto type = event->type();
        if (type == QEvent::MouseButtonPress) {
            const auto w = qobject_cast<QWidget*>(watched);
            // QMenu clicks are logged via MouseButtonRelease + actionAt
            if (w && !qobject_cast<QMenu*>(w)) {
                debugLogAppendLine(QStringLiteral("USER"),
                    QStringLiteral("%1 %2").arg(
                        tr("Click"), // click
                        debugLogDescribeWidget(w)));
            }
        } else if (type == QEvent::MouseButtonRelease) {
            const auto menu = qobject_cast<QMenu*>(watched);
            if (menu) {
                const auto me = static_cast<QMouseEvent*>(event);
                const auto action = menu->actionAt(me->pos());
                if (action && !action->isSeparator()) {
                    debugLogAppendLine(QStringLiteral("USER"),
                        QStringLiteral("%1 \"%2\" @ %3").arg(
                            tr("Menu"), // menu
                            debugLogClip(action->text()),
                            debugLogClip(menu->windowTitle().isEmpty()
                                            ? QString::fromLatin1("MenuBar")
                                            : menu->windowTitle(), 30)));
                }
            }
        } else if (type == QEvent::KeyPress) {
            const auto w = qobject_cast<QWidget*>(watched);
            if (w && !debugLogIsTextEntry(w)) {
                const auto ke = static_cast<QKeyEvent*>(event);
                if (!debugLogIsModifierKey(ke->key())) {
                    const auto keyStr = QKeySequence(ke->key()).toString();
                    debugLogAppendLine(QStringLiteral("USER"),
                        QStringLiteral("%1 %2 @ %3").arg(
                            tr("Key Press"), // key press
                            keyStr,
                            QString::fromLatin1(w->metaObject()->className())));
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

DebugEventLogger gDebugEventLogger;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    processKeyEvent(event);
}

MainWindow::MainWindow(Document& document,
                       Actions& actions,
                       AudioHandler& audioHandler,
                       RenderHandler& renderHandler,
                       const QString &openProject,
                       QWidget * const parent)
    : QMainWindow(parent)
    , mShutdown(false)
    , mWelcomeDialog(nullptr)
    , mStackWidget(nullptr)
    , mTabProperties(nullptr)
    , mTimeline(nullptr)
    , mRenderWidget(nullptr)
    , mToolbar(nullptr)
    , mToolBox(nullptr)
    , mSaveAct(nullptr)
    , mSaveAsAct(nullptr)
    , mSaveBackAct(nullptr)
    , mPreviewSVGAct(nullptr)
    , mExportSVGAct(nullptr)
    , mRenderVideoAct(nullptr)
    , mCloseProjectAct(nullptr)
    , mLinkedAct(nullptr)
    , mImportAct(nullptr)
    , mImportSeqAct(nullptr)
    , mRevertAct(nullptr)
    , mSelectAllAct(nullptr)
    , mInvertSelAct(nullptr)
    , mClearSelAct(nullptr)
    , mAddKeyAct(nullptr)
    , mAddToQueAct(nullptr)
    , mViewFullScreenAct(nullptr)
    , mFontWidget(nullptr)
    , mFontWidgetAct(nullptr)
    , mDocument(document)
    , mActions(actions)
    , mAudioHandler(audioHandler)
    , mRenderHandler(renderHandler)
    , mLayoutHandler(nullptr)
    , mFillStrokeSettings(nullptr)
    , mChangedSinceSaving(false)
    , mEventFilterDisabled(false)
    , mGrayOutWidget(nullptr)
    , mDisplayedFillStrokeSettingsUpdateNeeded(false)
    , mObjectSettingsWidget(nullptr)
    , mObjectSettingsScrollArea(nullptr)
    , mStackIndexScene(0)
    , mStackIndexWelcome(0)
    , mTabColorIndex(0)
    , mTabTextIndex(0)
    , mTabPropertiesIndex(0)
    , mTabAssetsIndex(0)
    , mTabQueueIndex(0)
    , mColorToolBar(nullptr)
    , mCanvasToolBar(nullptr)
    , mBackupOnSave(false)
    , mAutoSave(false)
    , mAutoSaveTimeout(0)
    , mAutoSaveTimer(nullptr)
    , mAboutWidget(nullptr)
    , mAboutWindow(nullptr)
    , mViewTimelineAct(nullptr)
    , mTimelineWindow(nullptr)
    , mTimelineWindowAct(nullptr)
    , mViewFillStrokeAct(nullptr)
    , mRenderWindow(nullptr)
    , mRenderWindowAct(nullptr)
    , mToolBarMainAct(nullptr)
    , mToolBarColorAct(nullptr)
{
    Q_ASSERT(!sInstance);
    sInstance = this;

    setWindowIcon(QIcon::fromTheme(AppSupport::getAppID()));
    setContextMenuPolicy(Qt::NoContextMenu);

    setupImporters();
    setupDocument();
    setupAutoSave();

    setupMainWidgets();
    setupMemoryWidgets();
    setupPropertiesWidgets();

    setupToolBar();
    setupMenuBar();

    setupStackWidgets();

    readRecentFiles();
    updateRecentMenu();

    installEventFilter(this);

    setupLayout();
    setupDebugLog();
    readSettings(openProject);
}

MainWindow::~MainWindow()
{
    disconnect();
    mShutdown = true;
    if (mAutoSaveTimer->isActive()) { mAutoSaveTimer->stop(); }
    writeSettings();
    sInstance = nullptr;
}

BoundingBox *MainWindow::getCurrentBox()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return nullptr; }

    const auto box = scene->getCurrentBox();
    if (!box) { return nullptr; }

    return box;
}

void MainWindow::checkAutoSaveTimer()
{
    if (mShutdown) { return; }

    if (mAutoSave &&
        mChangedSinceSaving &&
        !mDocument.fEvFile.isEmpty())
    {
        const int projectVersion = AppSupport::getProjectVersion(mDocument.fEvFile);
        const int newProjectVersion = AppSupport::getProjectVersion();
        if (newProjectVersion > projectVersion && projectVersion > 0) {
            QMessageBox::warning(this,
                                 tr("Auto Save canceled"),
                                 tr("Auto Save is not allowed to break"
                                    " project format compatibility (%1 vs. %2)."
                                    " Please save the project to confirm"
                                    " project format changes.").arg(QString::number(newProjectVersion),
                                                                    QString::number(projectVersion)));
            return;
        }
        saveFile(mDocument.fEvFile);
    }
}

void MainWindow::openAboutWindow()
{
    if (!mAboutWidget) {
        mAboutWidget = new AboutWidget(this);
    }
    if (!mAboutWindow) {
        mAboutWindow = new Window(this,
                                  mAboutWidget,
                                  tr("About"),
                                  QString("AboutWindow"),
                                  false,
                                  false);
        mAboutWindow->setMinimumSize(640, 480);
    }
    mAboutWindow->focusWindow();
}

void MainWindow::openTimelineWindow()
{
    AppSupport::setSettings("ui",
                            "TimelineWindow",
                            true);
    if (!mTimelineWindow) {
        mTimelineWindow = new Window(this,
                                     mTimeline,
                                     tr("Timeline"),
                                     QString("TimelineWindow"),
                                     true,
                                     true,
                                     true);
        connect(mTimelineWindow, &Window::closed,
                this, [this]() { closedTimelineWindow(); });
    } else {
        mTimelineWindow->addWidget(mTimeline);
    }
    mTimelineWindowAct->setChecked(true);
    if (mTimelineDock) {
        mTimelineDock->setWidget(nullptr);
        mTimelineDock->hide();
    }
    mTimelineWindow->focusWindow();
}

void MainWindow::closedTimelineWindow()
{
    if (mShutdown) { return; }
    AppSupport::setSettings("ui",
                            "TimelineWindow",
                            false);
    mTimelineWindowAct->setChecked(false);
    if (mTimelineDock) {
        mTimelineDock->setWidget(mTimeline);
        mTimelineDock->show();
        mTimeline->show();
        if (mViewTimelineAct) { mViewTimelineAct->setChecked(true); }
    }
}

void MainWindow::openRenderQueueWindow()
{
    AppSupport::setSettings("ui",
                            "RenderWindow",
                            true);
    mRenderWindowAct->setChecked(true);
    mTabProperties->removeTab(mTabQueueIndex);
    mRenderWidget->setVisible(true);
    if (!mRenderWindow) {
        mRenderWindow = new Window(this,
                                   mRenderWidget,
                                   tr("Renderer"),
                                   QString("RenderWindow"),
                                   true,
                                   true,
                                   false);
        connect(mRenderWindow, &Window::closed,
                this, [this]() { closedRenderQueueWindow(); });
    } else {
        mRenderWindow->addWidget(mRenderWidget);
    }
    mRenderWindow->focusWindow();
}

void MainWindow::closedRenderQueueWindow()
{
    if (mShutdown) { return; }
    AppSupport::setSettings("ui",
                            "RenderWindow",
                            false);
    mRenderWindowAct->setChecked(false);
    mTabQueueIndex = mTabProperties->addTab(mRenderWidget,
                                            QIcon::fromTheme("render_animation"),
                                            tr("Queue"));
}

void MainWindow::askInstallDefaultPresets()
{
    Ui::InstallPresets dialog(this);
    dialog.exec();
}

void MainWindow::askRestoreFillStrokeDefault()
{
    const auto result = QMessageBox::question(this,
                                              tr("Restore default fill and stroke?"),
                                              tr("Are you sure you want to restore fill and stroke defaults?"));
    if (result != QMessageBox::Yes) { return; }

    auto settings = eSettings::sInstance;
    settings->fLastFillFlatEnabled = false;
    settings->fLastStrokeFlatEnabled = true;
    settings->fLastUsedFillColor = Qt::white;
    settings->fLastUsedStrokeColor = ThemeSupport::getThemeObjectColor();
    settings->fLastUsedStrokeWidth = 10.;
}

void MainWindow::askRestoreDefaultUi()
{
    const auto result = QMessageBox::question(this,
                                              tr("Restore default user interface?"),
                                              tr("Are you sure you want to restore default user interface? "
                                                 "You must restart Friction to apply."));
    if (result != QMessageBox::Yes) { return; }
    eSettings::sInstance->fRestoreDefaultUi = true;
}

void MainWindow::askRunQuickSetup()
{
    const auto result = QMessageBox::question(this,
                                              tr("Run Quick Setup on startup?"),
                                              tr("Are you sure you want to run Quick Setup the next time you start Friction?"));
    if (result != QMessageBox::Yes) { return; }
    AppSupport::setSettings("settings", "firstRun", true);
}

void MainWindow::installDebugLogHandler()
{
    // Installed as early as possible from main() so that startup
    // debug output is captured as well.
    if (gDefaultMessageHandler) { return; }
    gDefaultMessageHandler = qInstallMessageHandler(debugLogMessageHandler);
}

void MainWindow::setupDebugLog()
{
    // record every user interaction (clicks, menu items, key presses)
    qApp->installEventFilter(&gDebugEventLogger);

    // Floating debug log button in the bottom-left corner
    mDebugLogButton = new QPushButton(QString(QChar(0x2315)), this);
    mDebugLogButton->setObjectName("DebugLogButton");
    mDebugLogButton->setFixedSize(30, 30);
    mDebugLogButton->setToolTip(tr("Debug Log"));
    mDebugLogButton->setCursor(Qt::PointingHandCursor);
    mDebugLogButton->setFocusPolicy(Qt::NoFocus);
    mDebugLogButton->setFlat(true);
    connect(mDebugLogButton, &QPushButton::clicked,
            this, &MainWindow::openDebugLogDialog);
    updateDebugLogButtonPos();
}

void MainWindow::updateDebugLogButtonPos()
{
    if (!mDebugLogButton) { return; }
    // anchor to the window bottom-left corner (above the status bar)
    int bottomOffset = 8;
    if (statusBar() && statusBar()->isVisible()) {
        bottomOffset += statusBar()->height();
    }
    mDebugLogButton->move(8,
                          height() - mDebugLogButton->height() - bottomOffset);
    mDebugLogButton->raise();
}

void MainWindow::openDebugLogDialog()
{
    // Non-modal dialog: importing or rendering keeps running while the
    // log is open, so the user can always copy its contents.
    if (!mDebugLogDialog) {
        mDebugLogDialog = new QDialog(this);
        mDebugLogDialog->setWindowTitle(tr("Debug Log"));
        mDebugLogDialog->resize(560, 440);
        mDebugLogDialog->setSizeGripEnabled(true);

        const auto layout = new QVBoxLayout(mDebugLogDialog);

        const auto pathLabel = new QLabel(mDebugLogDialog);
        pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLabel->setText(tr("Log file: %1")
                           .arg(gDebugLogFile ? gDebugLogFile->path()
                                              : QStringLiteral("-")));
        pathLabel->setToolTip(tr("Even if the window freezes, "
                                 "this file can be opened in Notepad "
                                 "to read the log"));

        mDebugLogView = new QPlainTextEdit(mDebugLogDialog);
        mDebugLogView->setReadOnly(true);
        mDebugLogView->setLineWrapMode(QPlainTextEdit::NoWrap);

        const auto btnLayout = new QHBoxLayout();
        const auto copyBtn = new QPushButton(tr("Copy Log"), mDebugLogDialog);
        const auto clearBtn = new QPushButton(tr("Clear Log"), mDebugLogDialog);
        const auto closeBtn = new QPushButton(tr("Close"), mDebugLogDialog);

        const auto viewPtr = mDebugLogView;
        connect(copyBtn, &QPushButton::clicked, copyBtn, [copyBtn, viewPtr]() {
            QApplication::clipboard()->setText(viewPtr->toPlainText());
            const auto oldText = copyBtn->text();
            copyBtn->setText(tr("Copied"));
            QTimer::singleShot(1500, copyBtn, [copyBtn, oldText]() {
                copyBtn->setText(oldText);
            });
        });
        connect(clearBtn, &QPushButton::clicked, this, [this, viewPtr]() {
            {
                QMutexLocker lock(&gDebugLogMutex);
                gDebugLogLines.clear();
            }
            mDebugLogShownLines = 0;
            viewPtr->clear();
        });
        connect(closeBtn, &QPushButton::clicked,
                mDebugLogDialog, &QDialog::close);

        btnLayout->addWidget(copyBtn);
        btnLayout->addWidget(clearBtn);
        btnLayout->addStretch();
        btnLayout->addWidget(closeBtn);

        layout->addWidget(pathLabel);
        layout->addWidget(mDebugLogView);
        layout->addLayout(btnLayout);

        mDebugLogTimer = new QTimer(this);
        connect(mDebugLogTimer, &QTimer::timeout,
                this, &MainWindow::refreshDebugLogView);
        connect(mDebugLogDialog, &QDialog::finished, this, [this]() {
            if (mDebugLogTimer) { mDebugLogTimer->stop(); }
        });
    }

    // full reload on (re)open
    mDebugLogShownLines = 0;
    mDebugLogView->clear();
    refreshDebugLogView();
    mDebugLogDialog->show();
    mDebugLogDialog->raise();
    mDebugLogDialog->activateWindow();
    mDebugLogTimer->start(400);
}

void MainWindow::refreshDebugLogView()
{
    if (!mDebugLogView) { return; }
    QStringList newLines;
    {
        QMutexLocker lock(&gDebugLogMutex);
        // buffer was trimmed (or cleared), rebuild from scratch
        if (gDebugLogLines.size() < mDebugLogShownLines) {
            mDebugLogShownLines = 0;
            mDebugLogView->clear();
        }
        if (mDebugLogShownLines < gDebugLogLines.size()) {
            newLines = gDebugLogLines.mid(mDebugLogShownLines);
            mDebugLogShownLines = gDebugLogLines.size();
        }
    }
    if (newLines.isEmpty()) { return; }
    const auto bar = mDebugLogView->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    mDebugLogView->appendPlainText(newLines.join(QStringLiteral("\n")));
    if (atBottom) { bar->setValue(bar->maximum()); }
}

void MainWindow::openWelcomeDialog()
{
    mStackWidget->setCurrentIndex(mStackIndexWelcome);
}

void MainWindow::closeWelcomeDialog()
{
    mStackWidget->setCurrentIndex(mStackIndexScene);
}

void MainWindow::addCanvasToRenderQue()
{
    if (!mDocument.fActiveScene) { return; }
    if (mRenderWindowAct->isChecked()) { openRenderQueueWindow(); }
    else { mTabProperties->setCurrentIndex(mTabQueueIndex); }
    mRenderWidget->createNewRenderInstanceWidgetForCanvas(mDocument.fActiveScene);
}

void MainWindow::updateSettingsForCurrentCanvas(Canvas* const scene)
{
    if (mColorToolBar) { mColorToolBar->setCurrentCanvas(scene); }
    if (mCanvasToolBar) { mCanvasToolBar->setCurrentCanvas(scene); }

    mObjectSettingsWidget->setCurrentScene(scene);
    if (mPropertiesInspector) { mPropertiesInspector->setCurrentScene(scene); }

    if (mPreviewSVGAct) { mPreviewSVGAct->setEnabled(scene); }
    if (mExportSVGAct) { mExportSVGAct->setEnabled(scene); }
    if (mSaveAct) { mSaveAct->setEnabled(scene); }
    if (mSaveAsAct) { mSaveAsAct->setEnabled(scene); }
    if (mSaveBackAct) { mSaveBackAct->setEnabled(scene); }
    if (mAddToQueAct) { mAddToQueAct->setEnabled(scene); }
    if (mRenderVideoAct) { mRenderVideoAct->setEnabled(scene); }
    if (mCloseProjectAct) { mCloseProjectAct->setEnabled(scene); }
    if (mLinkedAct) { mLinkedAct->setEnabled(scene); }
    if (mImportAct) { mImportAct->setEnabled(scene); }
    if (mImportSeqAct) { mImportSeqAct->setEnabled(scene); }
    if (mImportOCAAct) { mImportOCAAct->setEnabled(scene); }
    if (mRevertAct) { mRevertAct->setEnabled(scene); }
    if (mSelectAllAct) { mSelectAllAct->setEnabled(scene); }
    if (mInvertSelAct) { mInvertSelAct->setEnabled(scene); }
    if (mClearSelAct) { mClearSelAct->setEnabled(scene); }
    if (mAddKeyAct) { mAddKeyAct->setEnabled(scene); }
    if (mEffectsMenu) { mEffectsMenu->setEnabled(scene); }

    if (!scene) {
        mObjectSettingsWidget->setMainTarget(nullptr);
        mTimeline->updateSettingsForCurrentCanvas(nullptr);
        return;
    }

    mClipViewToCanvas->blockSignals(true);
    mClipViewToCanvas->setChecked(scene->clipToCanvas());
    mClipViewToCanvas->blockSignals(false);

    mRasterEffectsVisible->blockSignals(true);
    mRasterEffectsVisible->setChecked(scene->getRasterEffectsVisible());
    mRasterEffectsVisible->blockSignals(false);

    mPathEffectsVisible->blockSignals(true);
    mPathEffectsVisible->setChecked(scene->getPathEffectsVisible());
    mPathEffectsVisible->blockSignals(false);

    mTimeline->updateSettingsForCurrentCanvas(scene);
    mObjectSettingsWidget->setMainTarget(scene->getCurrentGroup());
}

void MainWindow::setupToolBar()
{
    mToolBox = new Ui::ToolBox(mActions, mDocument, this);
    mToolbar = new Ui::ToolBar(tr("Toolbar"),
                               "MainToolBar",
                               this);
    addToolBar(Qt::TopToolBarArea, mToolbar);

    mColorToolBar = new Ui::ColorToolBar(mDocument, this);
    connect(mColorToolBar, &Ui::ColorToolBar::message,
            this, [this](const QString &msg){ statusBar()->showMessage(msg, 500); });
    addToolBar(mColorToolBar);

    {
        const auto toolbar = mToolBox->getToolBar(Ui::ToolBox::Main);
        if (toolbar) { addToolBar(Qt::LeftToolBarArea, toolbar); }
    }
    {
        const auto toolbar = mToolBox->getToolBar(Ui::ToolBox::Controls);
        if (toolbar) { addToolBar(Qt::TopToolBarArea, toolbar); }
    }

    mCanvasToolBar = new Ui::CanvasToolBar(this);
    installNumericFilter(mCanvasToolBar->getResolutionComboBox());

    mCanvasToolBar->addSeparator();
    mCanvasToolBar->addAction(QIcon::fromTheme("workspace"),
                              tr("Layout"));
    const auto workspaceLayoutCombo = mLayoutHandler->comboWidget();
    workspaceLayoutCombo->setMaximumWidth(150);
    mCanvasToolBar->addWidget(workspaceLayoutCombo);

    statusBar()->addPermanentWidget(mCanvasToolBar);

    {
        const auto toolbar = mToolBox->getToolBar(Ui::ToolBox::Interact);
        if (toolbar) { statusBar()->addPermanentWidget(toolbar); }
    }

    connect(&mAudioHandler, &AudioHandler::deviceChanged,
            this, [this]() {
        statusBar()->showMessage(tr("Changed audio output: %1").arg(mAudioHandler.getDeviceName()),
                                 10000);
    });
}

MainWindow *MainWindow::sGetInstance()
{
    return sInstance;
}

void MainWindow::setupDocument()
{
    // setup connections
    connect(&mDocument, &Document::evFilePathChanged,
            this, &MainWindow::updateTitle);
    connect(&mDocument, &Document::activeSceneSet,
            this, &MainWindow::updateSettingsForCurrentCanvas);
    connect(&mDocument, &Document::currentBoxChanged,
            this, &MainWindow::setCurrentBox);
    connect(&mDocument, &Document::canvasModeSet,
            this, &MainWindow::updateCanvasModeButtonsChecked);
    connect(&mDocument, &Document::sceneCreated,
            this, &MainWindow::closeWelcomeDialog);
    connect(&mDocument, &Document::openTextEditor,
            this, [this] () { focusFontWidget(true); });
    connect(&mDocument, &Document::openMarkerEditor,
            this, &MainWindow::openMarkerEditor);
    connect(&mDocument, &Document::openExpressionDialog,
            this, &MainWindow::openExpressionDialog);
    connect(&mDocument, &Document::openApplyExpressionDialog,
            this, &MainWindow::openApplyExpressionDialog);
    connect(&mDocument, &Document::newVideo,
            this, &MainWindow::handleNewVideoClip);
    connect(&mDocument, &Document::documentChanged,
            this, [this]() {
        setFileChangedSinceSaving(true);
        if (mTimeline) { mTimeline->stopPreview(); }
    });

#ifdef Q_OS_MAC
    // https://github.com/friction2d/friction/pull/736
    connect(&mDocument, &Document::fitCanvasToSize, this, []() {
        const auto target = KeyFocusTarget::KFT_getCurrentTarget();
        const auto cwTarget = dynamic_cast<CanvasWindow*>(target);
        if (cwTarget) { cwTarget->fitCanvasToSize(); }
    });
#endif

    // set defaults
    mDocument.setPath("");
    mDocument.fDrawPathManual = false;
    mDocument.setCanvasMode(CanvasMode::boxTransform);
}

void MainWindow::setupImporters()
{
    //ImportHandler::sInstance->addImporter<eXevImporter>();
    ImportHandler::sInstance->addImporter<evImporter>();

    ImportHandler::sInstance->addImporter<eSvgImporter>();
    ImportHandler::sInstance->addImporter<ePsdImporter>();
    ImportHandler::sInstance->addImporter<eKraImporter>();
    //ImportHandler::sInstance->addImporter<eOraImporter>();
}

void MainWindow::setupAutoSave()
{
    mAutoSaveTimer = new QTimer(this);
    connect (mAutoSaveTimer, &QTimer::timeout,
            this, &MainWindow::checkAutoSaveTimer);
}

void MainWindow::updateCanvasModeButtonsChecked()
{
    // const CanvasMode mode = mDocument.fCanvasMode;
    // keep around in case we need to trigger something
}

void MainWindow::setResolutionValue(const qreal value)
{
    if (!mDocument.fActiveScene) { return; }
    mDocument.fActiveScene->setResolution(value);
    mDocument.actionFinished();
}

void MainWindow::setFileChangedSinceSaving(const bool changed)
{
    if (changed == mChangedSinceSaving) { return; }
    mChangedSinceSaving = changed;
    updateTitle();
}

SimpleBrushWrapper *MainWindow::getCurrentBrush() const
{
    return nullptr; //mBrushSelectionWidget->getCurrentBrush();
}

void MainWindow::setCurrentBox(BoundingBox *box)
{
    mColorToolBar->setCurrentBox(box);
    mFillStrokeSettings->setCurrentBox(box);
    mFontWidget->setCurrentBox(box);
    setCurrentBoxFocus(box);
}

void MainWindow::setCurrentBoxFocus(BoundingBox *box)
{
    if (!box) { return; }
    if (enve_cast<TextBox*>(box)) {
        focusFontWidget(mDocument.fCanvasMode == CanvasMode::textCreate);
    } else {
        focusColorWidget();
    }
}

FillStrokeSettingsWidget *MainWindow::getFillStrokeSettings()
{
    return mFillStrokeSettings;
}

bool MainWindow::askForSaving() {
    if (mChangedSinceSaving) {
        const QString title = tr("Save", "AskSaveDialog_Title");
        QFileInfo info(mDocument.fEvFile);
        QString file = info.baseName();
        if (file.isEmpty()) { file = tr("Untitled"); }

        const QString question = tr("Save changes to document \"%1\"?",
                                    "AskSaveDialog_Question");
        const QString questionWithTarget = question.arg(file);
        const QString closeNoSave =  tr("Close without saving",
                                        "AskSaveDialog_Button");
        const QString cancel = tr("Cancel", "AskSaveDialog_Button");
        const QString save = tr("Save", "AskSaveDialog_Button");
        const int buttonId = QMessageBox::question(
                    this, title, questionWithTarget,
                    closeNoSave, cancel, save);
        if (buttonId == 1) {
            return false;
        } else if (buttonId == 2) {
            saveFile();
            return true;
        }
    }
    return true;
}

BoxScrollWidget *MainWindow::getObjectSettingsList()
{
    return mObjectSettingsWidget;
}

void MainWindow::disableEventFilter()
{
    mEventFilterDisabled = true;
}

void MainWindow::enableEventFilter()
{
    mEventFilterDisabled = false;
}

void MainWindow::disable()
{
    disableEventFilter();
    mGrayOutWidget = new QWidget(this);
    mGrayOutWidget->setFixedSize(size());
   // mGrayOutWidget->setStyleSheet(
     //           "QWidget { background-color: rgba(0, 0, 0, 125) }");
    mGrayOutWidget->show();
    mGrayOutWidget->update();
}

void MainWindow::enable()
{
    if (!mGrayOutWidget) { return; }
    enableEventFilter();
    delete mGrayOutWidget;
    mGrayOutWidget = nullptr;
    mDocument.actionFinished();
}

void MainWindow::newFile()
{
    if (mChangedSinceSaving || !mDocument.fEvFile.isEmpty()) {
        const int ask = QMessageBox::question(this,
                                              tr("New Project"),
                                              tr("Are you sure you want to create a new project?"));
        if (ask == QMessageBox::No) { return; }
    }
    if (closeProject()) {
        SceneSettingsDialog::sNewSceneDialog(mDocument, this);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (mLock) { if (dynamic_cast<QInputEvent*>(e)) { return true; } }
    if (mEventFilterDisabled) { return QMainWindow::eventFilter(obj, e); }
    const auto type = e->type();
    const auto focusWidget = QApplication::focusWidget();
    if (type == QEvent::KeyPress) {
        const auto keyEvent = static_cast<QKeyEvent*>(e);
        if (keyEvent->key() == Qt::Key_Delete && focusWidget) {
            mEventFilterDisabled = true;
            const bool widHandled =
                    QCoreApplication::sendEvent(focusWidget, keyEvent);
            mEventFilterDisabled = false;
            if (widHandled) { return false; }
        }
        return processKeyEvent(keyEvent);
    } else if (type == QEvent::ShortcutOverride) {
        const auto keyEvent = static_cast<QKeyEvent*>(e);
        const int key = keyEvent->key();
        if (key == Qt::Key_Tab) {
            if (enve_cast<QLineEdit*>(focusWidget)) { return true; }
            KeyFocusTarget::KFT_sTab();
            return true;
        }
        //if (handleCanvasModeKeyPress(mDocument, key)) { return true; }
        if (keyEvent->modifiers() & Qt::SHIFT && key == Qt::Key_D) {
            return processKeyEvent(keyEvent);
        }
        if (keyEvent->modifiers() & Qt::CTRL &&
            (key == Qt::Key_C || key == Qt::Key_V ||
             key == Qt::Key_X || key == Qt::Key_D)) {
            return processKeyEvent(keyEvent);
        } else if (key == Qt::Key_A || key == Qt::Key_I ||
                   key == Qt::Key_Delete) {
              return processKeyEvent(keyEvent);
        }
    } else if (type == QEvent::KeyRelease) {
        const auto keyEvent = static_cast<QKeyEvent*>(e);
        if (processKeyEvent(keyEvent)) { return true; }
        //finishUndoRedoSet();
    } else if (type == QEvent::MouseButtonRelease) {
        //finishUndoRedoSet();
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!closeProject()) { e->ignore(); }
    else { mShutdown = true; }
}

bool MainWindow::processKeyEvent(QKeyEvent *event)
{
    if (isActiveWindow() || (mTimelineWindow && mTimelineWindow->isActiveWindow())) {
        bool returnBool = false;
        if (event->type() == QEvent::KeyPress &&
            mTimeline->processKeyPress(event))
        {
            returnBool = true;
        } else {
            returnBool = KeyFocusTarget::KFT_handleKeyEvent(event);
        }
        mDocument.actionFinished();
        return returnBool;
    }
    return false;
}

#ifdef Q_OS_MAC
bool MainWindow::processBoxesListKeyEvent(QKeyEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) { return false; }
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    if (ctrl && event->key() == Qt::Key_V) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.pasteAction)();
    } else if (ctrl && event->key() == Qt::Key_C) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.copyAction)();
    } else if (ctrl && event->key() == Qt::Key_D) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.duplicateAction)();
    } else if (ctrl && event->key() == Qt::Key_X) {
        if (event->isAutoRepeat()) { return false; }
        (*mActions.cutAction)();
    } else if (event->key() == Qt::Key_Delete) {
        (*mActions.deleteAction)();
    } else { return false; }
    return true;
}
#endif

static QString workspaceStateKey(const QString &name)
{
    // base64url keeps the settings key ASCII-safe for any (also CJK) name
    return QStringLiteral("state_") +
           QString::fromLatin1(name.toUtf8().toBase64(
                                   QByteArray::Base64UrlEncoding |
                                   QByteArray::OmitTrailingEquals));
}

void MainWindow::readSettings(const QString &openProject)
{
    restoreGeometry(AppSupport::getSettings("ui",
                                            "geometry").toByteArray());

    // Restore the last used panel layout (docks/toolbars). When the user
    // asked to restore the default UI, the saved state was cleared and
    // the factory layout is used instead. The state version guards
    // against restoring layouts saved by an incompatible panel system.
    const int stateVersion = AppSupport::getSettings("ui",
                                                     "stateVersion",
                                                     1).toInt();
    qWarning() << "WORKSPACE: stateVersion" << stateVersion
               << "restoreDefaultUi" << eSettings::instance().fRestoreDefaultUi;
    if (!eSettings::instance().fRestoreDefaultUi && stateVersion == 2) {
        // A custom workspace that the user applied/saved takes priority
        // over the ad-hoc last session layout, so it is re-applied on
        // every startup.
        const QString activeWorkspace = AppSupport::getSettings("workspaces",
                                                                 "active").toString();
        QByteArray stateToRestore;
        if (!activeWorkspace.isEmpty()) {
            stateToRestore = AppSupport::getSettings("workspaces",
                                                     workspaceStateKey(activeWorkspace)).toByteArray();
        }
        qWarning() << "WORKSPACE: active" << activeWorkspace
                   << "state bytes" << stateToRestore.size();
        if (stateToRestore.isEmpty()) {
            stateToRestore = AppSupport::getSettings("ui",
                                                     "state").toByteArray();
            qWarning() << "WORKSPACE: fallback to ui/state bytes"
                       << stateToRestore.size();
        }
        if (!stateToRestore.isEmpty()) {
            // Do NOT call restoreState() here: the window is not shown
            // yet and its final size arrives asynchronously (maximize),
            // so the dock layout would be clamped to the dock minimums
            // and the saved dock sizes lost. The state is applied by
            // applyPendingStateRestore() once the window geometry is
            // stable (debounced in showEvent/resizeEvent).
            mPendingStateRestore = stateToRestore;
            qWarning() << "WORKSPACE: state restore deferred until shown,"
                       << stateToRestore.size() << "bytes";
        } else {
            qWarning() << "WORKSPACE: no state to restore, using default layout";
        }
    }

    bool isMax = AppSupport::getSettings("ui",
                                         "maximized",
                                         false).toBool();
    bool isFull = AppSupport::getSettings("ui",
                                          "fullScreen",
                                          false).toBool();

    mToolBarMainAct->setChecked(true);
    mToolBarColorAct->setChecked(true);

    // sync menu actions with the restored dock visibility
    mViewTimelineAct->blockSignals(true);
    mViewTimelineAct->setChecked(!mTimelineDock->isHidden());
    mViewTimelineAct->blockSignals(false);
    mViewFillStrokeAct->blockSignals(true);
    mViewFillStrokeAct->setChecked(!mFillStrokeDock->isHidden());
    mViewFillStrokeAct->blockSignals(false);

#ifdef Q_OS_LINUX
    if (AppSupport::isWayland()) { // Disable fullscreen on wayland
        isFull = false;
        mViewFullScreenAct->setEnabled(false);
    }
#endif

    mViewFullScreenAct->blockSignals(true);
    mViewFullScreenAct->setChecked(isFull);
    mViewFullScreenAct->blockSignals(false);

    mTimelineWindowAct->blockSignals(true);
    mTimelineWindowAct->setChecked(false);
    mTimelineWindowAct->blockSignals(false);

    mRenderWindowAct->blockSignals(true);
    mRenderWindowAct->setChecked(false);
    mRenderWindowAct->blockSignals(false);

    {
        // force tool controls to own row
        const auto toolbar = mToolBox->getToolBar(Ui::ToolBox::Controls);
        if (toolbar) { insertToolBarBreak(toolbar); }
    }

    if (isFull) { showFullScreen(); }
    else if (isMax) { showMaximized(); }

    updateAutoSaveBackupState();

    if (!openProject.isEmpty()) {
        QTimer::singleShot(10,
                           this,
                           [this,
                           openProject]() { openFile(openProject); });
    } else { openWelcomeDialog(); }
}

void MainWindow::writeSettings()
{
    if (eSettings::instance().fRestoreDefaultUi) {
        AppSupport::clearSettings("ui");
        // also drop the auto-applied workspace so the default UI sticks
        AppSupport::setSettings("workspaces", "active", QVariant());
    } else {
        AppSupport::setSettings("ui", "geometry", saveGeometry());
        AppSupport::setSettings("ui", "maximized", isMaximized());
        AppSupport::setSettings("ui", "fullScreen", isFullScreen());
        // persist the current panel layout so it is restored on startup
        AppSupport::setSettings("ui", "state", saveState());
        AppSupport::setSettings("ui", "stateVersion", 2);
    }

    AppSupport::setSettings("FillStroke", "LastStrokeColor",
                            eSettings::instance().fLastUsedStrokeColor);
    AppSupport::setSettings("FillStroke", "LastStrokeWidth",
                            eSettings::instance().fLastUsedStrokeWidth);
    AppSupport::setSettings("FillStroke", "LastStrokeFlat",
                            eSettings::instance().fLastStrokeFlatEnabled);

    AppSupport::setSettings("FillStroke", "LastFillColor",
                            eSettings::instance().fLastUsedFillColor);
    AppSupport::setSettings("FillStroke", "LastFillFlat",
                            eSettings::instance().fLastFillFlatEnabled);
}

bool MainWindow::isEnabled()
{
    return !mGrayOutWidget;
}

QStringList MainWindow::savedWorkspaceNames() const
{
    return AppSupport::getSettings("workspaces",
                                   "names",
                                   QStringList()).toStringList();
}

void MainWindow::saveCurrentWorkspaceAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               tr("Save Workspace"),
                                               tr("Workspace name:"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok).trimmed();
    if (!ok || name.isEmpty()) { return; }

    QStringList names = savedWorkspaceNames();
    if (names.contains(name)) {
        const auto ret = QMessageBox::question(
                             this,
                             tr("Overwrite Workspace"),
                             tr("Workspace '%1' already exists. "
                                "Overwrite it?").arg(name));
        if (ret != QMessageBox::Yes) { return; }
    } else {
        names.append(name);
        AppSupport::setSettings("workspaces", "names", names);
    }
    AppSupport::setSettings("workspaces", workspaceStateKey(name), saveState());
    // the saved workspace becomes the one re-applied on startup
    AppSupport::setSettings("workspaces", "active", name);
}

void MainWindow::applyWorkspace(const QString &name)
{
    const QByteArray state = AppSupport::getSettings("workspaces",
                                                     workspaceStateKey(name)).toByteArray();
    if (state.isEmpty()) { return; }
    restoreState(state);
    // remember the applied workspace so it is restored on startup
    AppSupport::setSettings("workspaces", "active", name);
}

void MainWindow::deleteWorkspace(const QString &name)
{
    QStringList names = savedWorkspaceNames();
    if (names.removeAll(name) == 0) { return; }
    AppSupport::setSettings("workspaces", "names", names);
    // an invalid QVariant removes the key from QSettings
    AppSupport::setSettings("workspaces", workspaceStateKey(name), QVariant());
    if (AppSupport::getSettings("workspaces", "active").toString() == name) {
        AppSupport::setSettings("workspaces", "active", QVariant());
    }
}

void MainWindow::applyDefaultWorkspace()
{
    // the default layout is not a saved workspace: stop auto-applying
    // the previous custom workspace on startup
    AppSupport::setSettings("workspaces", "active", QVariant());

    const QList<QDockWidget*> docks = {mTimelineDock,
                                       mFillStrokeDock,
                                       mPropertiesDock,
                                       mEasingDock};
    for (const auto dock : docks) {
        if (!dock) { continue; }
        dock->setFloating(false);
        dock->setVisible(true);
    }

    addDockWidget(Qt::RightDockWidgetArea, mFillStrokeDock);
    addDockWidget(Qt::RightDockWidgetArea, mPropertiesDock);
    addDockWidget(Qt::RightDockWidgetArea, mEasingDock);
    addDockWidget(Qt::BottomDockWidgetArea, mTimelineDock);

    if (mEasingDock) { mEasingDock->hide(); }

    const int w = width();
    const int h = height();
    if (w > 0 && h > 0) {
        resizeDocks({mFillStrokeDock, mPropertiesDock},
                    {int(w * 0.3), int(w * 0.3)}, Qt::Horizontal);
        resizeDocks({mFillStrokeDock, mPropertiesDock},
                    {int(h * 0.3), int(h * 0.35)}, Qt::Vertical);
        resizeDocks({mTimelineDock}, {int(h * 0.3)}, Qt::Vertical);
    }
}

void MainWindow::rebuildWorkspaceMenu()
{
    if (!mWorkspaceMenu) { return; }
    mWorkspaceMenu->clear();

    mWorkspaceMenu->addAction(tr("Reset to Default Layout"),
                              this, &MainWindow::applyDefaultWorkspace);
    mWorkspaceMenu->addAction(tr("Save Current Workspace..."),
                              this, &MainWindow::saveCurrentWorkspaceAs);

    const QStringList names = savedWorkspaceNames();
    if (!names.isEmpty()) {
        mWorkspaceMenu->addSeparator();
        for (const auto &name : names) {
            mWorkspaceMenu->addAction(name, this, [this, name]() {
                applyWorkspace(name);
            });
        }
        const auto deleteMenu = mWorkspaceMenu->addMenu(tr("Delete Workspace"));
        for (const auto &name : names) {
            deleteMenu->addAction(name, this, [this, name]() {
                deleteWorkspace(name);
            });
        }
    }

    mWorkspaceMenu->addSeparator();
    const auto panelsMenu = mWorkspaceMenu->addMenu(tr("Panels"));
    panelsMenu->addAction(mTimelineDock->toggleViewAction());
    panelsMenu->addAction(mFillStrokeDock->toggleViewAction());
    panelsMenu->addAction(mPropertiesDock->toggleViewAction());
    panelsMenu->addAction(mEasingDock->toggleViewAction());
    if (mScriptManager && mScriptManager->console()) {
        panelsMenu->addAction(mScriptManager->console()->toggleViewAction());
    }
}

void MainWindow::setupMainWidgets()
{
    BoxSingleWidget::loadStaticPixmaps(eSizesUI::widget);

    mFillStrokeSettings = new FillStrokeSettingsWidget(mDocument, this);

    mLayoutHandler = new LayoutHandler(mDocument,
                                       mAudioHandler,
                                       this);
    mTimeline = new TimelineDockWidget(mDocument,
                                       mLayoutHandler,
                                       this);
    mRenderWidget = new RenderWidget(this);
}

void MainWindow::setupStackWidgets()
{
    mWelcomeDialog = new WelcomeDialog(mRecentMenu,
                                       [this]() { SceneSettingsDialog::sNewSceneDialog(mDocument, this); },
                                       []() { MainWindow::sGetInstance()->openFile(); },
                                       this);

    mStackWidget = new QStackedWidget(this);
    mStackIndexScene = mStackWidget->addWidget(mLayoutHandler->sceneLayout());
    mStackIndexWelcome = mStackWidget->addWidget(mWelcomeDialog);
}

void MainWindow::setupMemoryWidgets()
{
    const auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout,
            this, [this]() {
        if (mShutdown || !mCanvasToolBar) { return; }
        mCanvasToolBar->setMemoryUsage(mMemoryUsed);
    });
    timer->start(5000);

    const auto handler = MemoryHandler::sInstance;
    connect(handler, &MemoryHandler::memoryUsed,
            this, [this](intMB used) { mMemoryUsed = used; });
}

void MainWindow::setupPropertiesWidgets()
{
    mObjectSettingsScrollArea = new ScrollArea(this);
    mObjectSettingsScrollArea->setSizePolicy(QSizePolicy::Expanding,
                                             QSizePolicy::Expanding);
    mObjectSettingsScrollArea->setAutoFillBackground(true);
    mObjectSettingsScrollArea->setPalette(ThemeSupport::getDarkPalette());

    mObjectSettingsWidget = new BoxScrollWidget(mDocument,
                                                mObjectSettingsScrollArea);
    mObjectSettingsScrollArea->setWidget(mObjectSettingsWidget);
    mObjectSettingsWidget->setAlwaysShowChildren(false);
    mObjectSettingsWidget->setCurrentRule(SWT_BoxRule::selected);
    mObjectSettingsWidget->setCurrentTarget(nullptr, SWT_Target::canvas);

    // font widget
    mFontWidget = new Ui::FontsWidget(this);
    mFontWidget->setMaximumHeight(150);

    // align widget
    const auto alignWidget = new Ui::AlignWidget(this);

    // assets widget
    const auto assets = new AssetsWidget(this);

    mTabProperties = new QTabWidget(this);
    mTabProperties->setObjectName("TabWidgetWide");
    mTabProperties->tabBar()->setFocusPolicy(Qt::NoFocus);
    mTabProperties->setContentsMargins(0, 0, 0, 0);
    mTabProperties->setTabPosition(QTabWidget::South);
    eSizesUI::widget.add(mTabProperties, [this](const int size) {
        mTabProperties->setIconSize(QSize(size, size));
    });

    const auto tabButtons = mTabProperties->findChildren<QToolButton*>();
    for (const auto &button : tabButtons) {
        button->setFocusPolicy(Qt::NoFocus); // don't allow buttons to take focus
    }

    const auto propertiesWidget = new QWidget(this);
    const auto propertiesLayout = new QVBoxLayout(propertiesWidget);
    propertiesLayout->setContentsMargins(0, 0, 0, 0);
    propertiesLayout->setSpacing(0);

    mPropertiesInspector = new AEPropertiesInspector(mDocument, this);
    propertiesLayout->addWidget(mPropertiesInspector);
    propertiesLayout->addWidget(mFontWidget);
    propertiesLayout->addWidget(alignWidget);

    mTabPropertiesIndex = mTabProperties->addTab(propertiesWidget,
                                                 ThemeSupport::themedToolIcon("drawPathAutoChecked",
                                                                              ThemeSupport::getThemeColorBlue(), 64),
                                                 tr("Properties"));
    const auto effectsPanel = new EffectsPresetsPanel(this, this);
    mEffectsPresetsPanel = effectsPanel;
    mTabEffectsIndex = mTabProperties->addTab(effectsPanel,
                                              ThemeSupport::themedToolIcon("effect",
                                                                           ThemeSupport::getThemeColorOrange(), 64),
                                              tr("Effects"));
    mTabAssetsIndex = mTabProperties->addTab(assets,
                                             ThemeSupport::themedToolIcon("asset_manager",
                                                                          ThemeSupport::getThemeColorGreen(), 64),
                                             tr("Assets"));
    mTabQueueIndex = mTabProperties->addTab(mRenderWidget,
                                            ThemeSupport::themedToolIcon("render_animation",
                                                                         ThemeSupport::getThemeColorRed(), 64),
                                            tr("Queue"));

    connect(mObjectSettingsScrollArea->verticalScrollBar(),
            &QScrollBar::valueChanged,
            mObjectSettingsWidget, &BoxScrollWidget::changeVisibleTop);
    connect(mObjectSettingsScrollArea, &ScrollArea::heightChanged,
            mObjectSettingsWidget, &BoxScrollWidget::changeVisibleHeight);
    connect(mObjectSettingsScrollArea, &ScrollArea::widthChanged,
            mObjectSettingsWidget, &BoxScrollWidget::setWidth);
}

void MainWindow::setupLayout()
{
    // AE-like panel system: every side panel is a QDockWidget that can be
    // popped out (floating), re-docked into any area or tabbed with
    // other panels. The whole layout is serializable with saveState().
    // The canvas stays the central widget so the dock areas always have
    // an elastic neighbor and can be resized freely.
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    const auto makeDock = [this](const QString &title,
                                 const QString &objectName,
                                 QWidget *widget) {
        const auto dock = new QDockWidget(title, this);
        dock->setObjectName(objectName); // required by saveState()
        dock->setWidget(widget);
        dock->setFeatures(QDockWidget::DockWidgetClosable |
                          QDockWidget::DockWidgetMovable |
                          QDockWidget::DockWidgetFloatable);
        return dock;
    };

    mTimelineDock = makeDock(tr("Timeline"), QStringLiteral("dockTimeline"),
                             mTimeline);
    mFillStrokeDock = makeDock(tr("Fill and Stroke"), QStringLiteral("dockFillStroke"),
                               mFillStrokeSettings);
    mPropertiesDock = makeDock(tr("Properties"), QStringLiteral("dockProperties"),
                               mTabProperties);

    // easing presets panel (AE-like curve picker)
    const auto easingPresets = new EasingPresetsWidget(this);
    easingPresets->setKeysViewGetter([this]() -> KeysView* {
        if (!mLayoutHandler) { return nullptr; }
        const auto timeline = mLayoutHandler->timelineLayout()->currentWidget();
        return timeline ? timeline->findChild<KeysView*>() : nullptr;
    });
    mEasingDock = makeDock(tr("Easing Presets"), QStringLiteral("dockEasingPresets"),
                           easingPresets);

    setCentralWidget(mStackWidget);
    addDockWidget(Qt::RightDockWidgetArea, mFillStrokeDock);
    addDockWidget(Qt::RightDockWidgetArea, mPropertiesDock);
    addDockWidget(Qt::RightDockWidgetArea, mEasingDock);
    addDockWidget(Qt::BottomDockWidgetArea, mTimelineDock);

    mEasingDock->hide();

    // JS plugin system: Scripts menu + script console dock
    setupScripting();

    // keep view menu actions in sync when docks are closed directly
    connect(mTimelineDock, &QDockWidget::visibilityChanged,
            this, [this](const bool visible) {
        if (!mViewTimelineAct) { return; }
        if (mTimelineWindowAct && mTimelineWindowAct->isChecked()) { return; }
        mViewTimelineAct->blockSignals(true);
        mViewTimelineAct->setChecked(visible);
        mViewTimelineAct->blockSignals(false);
    });
    connect(mFillStrokeDock, &QDockWidget::visibilityChanged,
            this, [this](const bool visible) {
        if (!mViewFillStrokeAct) { return; }
        mViewFillStrokeAct->blockSignals(true);
        mViewFillStrokeAct->setChecked(visible);
        mViewFillStrokeAct->blockSignals(false);
    });
}

void MainWindow::setupScripting()
{
    // the manager creates the console dock and loads plugins from
    // the user scripts folder
    mScriptManager = new ScriptManager(this);
    mMenuBar->addMenu(mScriptManager->menu());
    addDockWidget(Qt::BottomDockWidgetArea, mScriptManager->console());
    mScriptManager->console()->hide();
}

void MainWindow::clearAll()
{
    TaskScheduler::instance()->clearTasks();
    setFileChangedSinceSaving(false);
    mObjectSettingsWidget->setMainTarget(nullptr);

    mRenderWidget->clearRenderQueue();
    mFillStrokeSettings->clearAll();
    mFontWidget->clearAll();
    mDocument.clear();
    mLayoutHandler->clear();
    FilesHandler::sInstance->clear();

    mActions.setMovePathMode();

    openWelcomeDialog();
}

void MainWindow::updateTitle()
{
    QString unsaved = mChangedSinceSaving ? " *" : "";
    QFileInfo info(mDocument.fEvFile);
    QString file = info.baseName();
    if (file.isEmpty()) { file = tr("Untitled"); }
    setWindowTitle(QString("%1%2").arg(file, unsaved));
    if (mSaveAct) {
        mSaveAct->setText(mChangedSinceSaving ? tr("Save *") : tr("Save"));
    }
}

void MainWindow::openFile()
{
    if (askForSaving()) {
        disable();
        const QString defPath = mDocument.fEvFile.isEmpty() ? getLastOpenDir() : mDocument.fEvFile;
        const QString title = tr("Open File", "OpenDialog_Title");
        const QString files = tr("Friction Files %1", "OpenDialog_FileType");
        const QString openPath = AppSupport::getOpenFile(this,
                                                         title,
                                                         defPath,
                                                         files.arg("(*.friction)"));
        if (!openPath.isEmpty()) { openFile(openPath); }
        enable();
    }
}

void MainWindow::openFile(const QString& openPath)
{
    clearAll();
    try {
        QFileInfo fi(openPath);
        const QString suffix = fi.suffix();
        if (suffix == "friction") {
            loadEVFile(openPath);
        } /*else if (suffix == "xev") {
            loadXevFile(openPath);
        }*/ else { RuntimeThrow("Unrecognized file extension " + suffix); }
        mDocument.setPath(openPath);
        setFileChangedSinceSaving(false);
        updateLastOpenDir(openPath);
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
    mDocument.actionFinished();
}

void MainWindow::saveFile()
{
    if (mDocument.fEvFile.isEmpty()) { saveFileAs(true); }
    else {
        const int projectVersion = AppSupport::getProjectVersion(mDocument.fEvFile);
        const int newProjectVersion = AppSupport::getProjectVersion();
        if (newProjectVersion > projectVersion && projectVersion > 0) {
            const auto result = QMessageBox::question(this,
                                                      tr("Project version"),
                                                      tr("Saving this project file will change the project"
                                                         " format from version %1 to version %2."
                                                         " This breaks compatibility with older versions of Friction."
                                                         "\n\nAre you sure you want"
                                                         " to save this project file?").arg(QString::number(projectVersion),
                                                                                            QString::number(newProjectVersion)));
            if (result != QMessageBox::Yes) { return; }
        }
        saveFile(mDocument.fEvFile);
    }
}

void MainWindow::saveFile(const QString& path,
                          const bool setPath)
{
    try {
        QFileInfo fi(path);
        const QString suffix = fi.suffix();
        if (suffix == "friction") {
            saveToFile(path);
        } /*else if (suffix == "xev") {
            saveToFileXEV(path);
            const auto& inst = DialogsInterface::instance();
            inst.displayMessageToUser("Please note that the XEV format is still in the testing phase.");
        }*/ else { RuntimeThrow("Unrecognized file extension " + suffix); }
        if (setPath) mDocument.setPath(path);
        setFileChangedSinceSaving(false);
        updateLastSaveDir(path);
        if (mBackupOnSave) {
            qDebug() << "auto backup";
            saveBackup();
        }
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
}

void MainWindow::saveFileAs(const bool setPath)
{
    disableEventFilter();
    const QString defPath = mDocument.fEvFile.isEmpty() ? getLastSaveDir() : mDocument.fEvFile;

    const QString title = tr("Save File", "SaveDialog_Title");
    const QString fileType = tr("Friction Files %1", "SaveDialog_FileType");
    QString saveAs = AppSupport::getSaveFile(this,
                                             title,
                                             defPath,
                                             fileType.arg("(*.friction)"),
                                             "friction");
    enableEventFilter();
    if (!saveAs.isEmpty()) { saveFile(saveAs, setPath); }
}

void MainWindow::saveBackup()
{
    const QString defPath = mDocument.fEvFile;
    QFileInfo defInfo(defPath);
    if (defPath.isEmpty() || defInfo.isDir())  { return; }
    const QString backupPath = defPath + "_backup/backup_%1.friction";
    int id = 1;
    QFile backupFile(backupPath.arg(id));
    while (backupFile.exists()) {
        id++;
        backupFile.setFileName(backupPath.arg(id) );
    }
    try {
        saveToFile(backupPath.arg(id), false);
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
}

const QString MainWindow::checkBeforeExportSVG()
{
    QStringList result;
    for (const auto& scene : mDocument.fScenes) {
        const auto warnings = scene->checkForUnsupportedSVG();
        if (!warnings.isEmpty()) { result.append(warnings); }
    }
    return result.join("");
}

void MainWindow::exportSVG(const bool &preview)
{
    const auto dialog = new ExportSvgDialog(this,
                                            preview ? QString() : checkBeforeExportSVG());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (!preview) {
        dialog->show();
    } else {
        dialog->showPreview(true /* close when done */);
    }
}

void MainWindow::updateLastOpenDir(const QString &path)
{
    if (path.isEmpty()) { return; }
    QFileInfo i(path);
    AppSupport::setSettings("files",
                            "lastOpenDir",
                            i.absoluteDir().absolutePath());
}

void MainWindow::updateLastSaveDir(const QString &path)
{
    if (path.isEmpty()) { return; }
    QFileInfo i(path);
    AppSupport::setSettings("files",
                            "lastSaveDir",
                            i.absoluteDir().absolutePath());
}

const QString MainWindow::getLastOpenDir()
{
    return AppSupport::getSettings("files",
                                   "lastOpenDir",
                                   QDir::homePath()).toString();
}

const QString MainWindow::getLastSaveDir()
{
    return AppSupport::getSettings("files",
                                   "lastSaveDir",
                                   QDir::homePath()).toString();
}

bool MainWindow::closeProject()
{
    if (askForSaving()) {
        clearAll();
        return true;
    }
    return false;
}

void MainWindow::importFile()
{
    disableEventFilter();

    const auto recentDir = AppSupport::getSettings("files",
                                                   "recentImportDir",
                                                   QDir::homePath()).toString();
    QString defPath = QDir::homePath();
    switch (eSettings::instance().fImportFileDirOpt) {
    case eSettings::ImportFileDirRecent:
        defPath = recentDir;
        break;
    case eSettings::ImportFileDirProject:
        defPath = mDocument.fEvFile.isEmpty() ? recentDir : mDocument.fEvFile;
        break;
    default:;
    }

    const QString title = tr("Import File(s)", "ImportDialog_Title");
    const QString fileType = tr("Files %1", "ImportDialog_FileTypes");
    const QString fileTypes = "(*.friction *.svg *.psd *.psb *.kra " +
            FileExtensions::videoFilters() +
            FileExtensions::imageFilters() +
            FileExtensions::soundFilters() + ")";
    const auto importPaths = AppSupport::getOpenFiles(this,
                                                      title,
                                                      defPath,
                                                      fileType.arg(fileTypes));
    enableEventFilter();
    if (!importPaths.isEmpty()) {
        for(const QString &path : importPaths) {
            if (path.isEmpty()) { continue; }
            try {
                mActions.importFile(path);
            } catch(const std::exception& e) {
                gPrintExceptionCritical(e);
            }
        }
    }
}

void MainWindow::linkFile()
{
    disableEventFilter();
    const QString defPath = mDocument.fEvFile.isEmpty() ?
                QDir::homePath() : mDocument.fEvFile;
    const QString title = tr("Link File", "LinkDialog_Title");
    const QString fileType = tr("Files %1", "LinkDialog_FileType");
    const auto importPaths = AppSupport::getOpenFiles(this,
                                                      title,
                                                      defPath,
                                                      fileType.arg("(*.svg)"));
    enableEventFilter();
    if (!importPaths.isEmpty()) {
        for (const QString &path : importPaths) {
            if (path.isEmpty()) { continue; }
            try {
                mActions.linkFile(path);
            } catch(const std::exception& e) {
                gPrintExceptionCritical(e);
            }
        }
    }
}

void MainWindow::importImageSequence()
{
    disableEventFilter();
    const QString defPath = mDocument.fEvFile.isEmpty() ?
                QDir::homePath() : mDocument.fEvFile;
    const QString title = tr("Import Image Sequence",
                             "ImportSequenceDialog_Title");
    const auto folder = AppSupport::getOpenDirectory(this,
                                                     title,
                                                     defPath);
    enableEventFilter();
    if (!folder.isEmpty()) { mActions.importFile(folder); }
}

void MainWindow::importOCA()
{
    disableEventFilter();
    const QString defPath = mDocument.fEvFile.isEmpty() ?
                QDir::homePath() : mDocument.fEvFile;
    // DuIO/AE-style: the user picks the MANIFEST FILE (.oca/.json)
    // and the import starts right away; a directory-only picker hid
    // the manifest file entirely and read as "clicking does nothing"
    const QString title = tr("Select OCA Manifest", "ImportOCADialog_Title");
    const QString fileType = tr("OCA manifest %1", "ImportOCADialog_FileTypes");
    const auto importPaths = AppSupport::getOpenFiles(this,
                                                      title,
                                                      defPath,
                                                      fileType.arg("(*.oca *.json)"));
    enableEventFilter();
    for (const QString &path : importPaths) {
        if (path.isEmpty()) { continue; }
        try {
            mActions.importFile(path);
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
    }
}

void MainWindow::revert()
{
    const int ask = QMessageBox::question(this,
                                          tr("Confirm revert"),
                                          tr("Are you sure you want to revert current project?"
                                             "<p><b>Any changes will be lost.</b></p>"));
    if (ask == QMessageBox::No) { return; }
    const QString path = mDocument.fEvFile;
    openFile(path);
}

void MainWindow::updateAutoSaveBackupState()
{
    if (mShutdown) { return; }

    mBackupOnSave = AppSupport::isFlatpak() ? false :
                        AppSupport::getSettings("files",
                                                "BackupOnSave",
                                                false).toBool();
    mAutoSave = AppSupport::getSettings("files",
                                        "AutoSave",
                                        false).toBool();
    int lastTimeout = mAutoSaveTimeout;
    mAutoSaveTimeout = AppSupport::getSettings("files",
                                               "AutoSaveTimeout",
                                               300000).toInt();
    qDebug() << "update auto save/backup state" << mBackupOnSave << mAutoSave << mAutoSaveTimeout;
    if (mAutoSave && !mAutoSaveTimer->isActive()) {
        mAutoSaveTimer->start(mAutoSaveTimeout);
    } else if (!mAutoSave && mAutoSaveTimer->isActive()) {
        mAutoSaveTimer->stop();
    }
    if (mAutoSave &&
        lastTimeout > 0 &&
        lastTimeout != mAutoSaveTimeout) {
        if (mAutoSaveTimer->isActive()) { mAutoSaveTimer->stop(); }
        mAutoSaveTimer->start(mAutoSaveTimeout);
    }
}

void MainWindow::openRendererWindow()
{
    if (mRenderWidget->count() < 1) {
        addCanvasToRenderQue();
    } else {
        if (mRenderWindowAct->isChecked()) { openRenderQueueWindow(); }
        else { mTabProperties->setCurrentIndex(mTabQueueIndex); }
    }
}

void MainWindow::cmdAddAction(QAction *act)
{
    if (!act || eSettings::instance().fCommandPalette.contains(act)) { return; }
    eSettings::sInstance->fCommandPalette.append(act);
}

LayoutHandler *MainWindow::getLayoutHandler()
{
    return mLayoutHandler;
}

TimelineDockWidget *MainWindow::getTimeLineWidget()
{
    return mTimeline;
}

void MainWindow::focusFontWidget(const bool focus)
{
    if (mTabProperties->currentIndex() != mTabPropertiesIndex) {
        mTabProperties->setCurrentIndex(mTabPropertiesIndex);
    }
    if (focus) { mFontWidget->setTextFocus(); }
}

void MainWindow::focusColorWidget()
{
    // TODO when we support window mode for color widget
}

void MainWindow::openMarkerEditor()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto dialog = new Ui::MarkerEditorDialog(scene, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openExpressionDialog(QrealAnimator * const target)
{
    if (!target) { return; }
    const auto dialog = new ExpressionDialog(target, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openApplyExpressionDialog(QrealAnimator * const target)
{
    if (!target) { return; }
    const auto dialog = new ApplyExpressionDialog(target, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

stdsptr<void> MainWindow::lock()
{
    if (mLock) { return mLock->ref<Lock>(); }
    setEnabled(false);
    const auto newLock = enve::make_shared<Lock>(this);
    mLock = newLock.get();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    return newLock;
}

void MainWindow::lockFinished()
{
    if (mLock) {
        gPrintException(false, "Lock finished before lock object deleted");
    } else {
        QApplication::restoreOverrideCursor();
        setEnabled(true);
    }
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    //if (statusBar()) { statusBar()->setMaximumWidth(width()); }
    QMainWindow::resizeEvent(e);
    updateDebugLogButtonPos();
    // restart the debounce timer: apply the saved dock layout only
    // after the window size has settled
    armPendingStateRestore();
}

void MainWindow::showEvent(QShowEvent *e)
{
    //if (statusBar()) { statusBar()->setMaximumWidth(width()); }
    QMainWindow::showEvent(e);
    updateDebugLogButtonPos();
    armPendingStateRestore();
}

void MainWindow::armPendingStateRestore()
{
    if (mPendingStateRestore.isEmpty()) { return; }
    if (!mStateRestoreTimer) {
        mStateRestoreTimer = new QTimer(this);
        mStateRestoreTimer->setSingleShot(true);
        mStateRestoreTimer->setInterval(60);
        connect(mStateRestoreTimer, &QTimer::timeout,
                this, &MainWindow::applyPendingStateRestore);
    }
    // debounce: every show/resize restarts the countdown so the state
    // is restored only once the final window geometry is in place
    mStateRestoreTimer->start();
}

void MainWindow::applyPendingStateRestore()
{
    if (mPendingStateRestore.isEmpty()) { return; }
    const QByteArray state = mPendingStateRestore;
    mPendingStateRestore.clear();
    const bool restored = restoreState(state);
    qWarning() << "WORKSPACE: stable-geometry restoreState returned"
               << restored << "window" << width() << "x" << height();

    // keep view menu actions in sync with the restored docks
    mViewTimelineAct->blockSignals(true);
    mViewTimelineAct->setChecked(!mTimelineDock->isHidden());
    mViewTimelineAct->blockSignals(false);
    mViewFillStrokeAct->blockSignals(true);
    mViewFillStrokeAct->setChecked(!mFillStrokeDock->isHidden());
    mViewFillStrokeAct->blockSignals(false);
}

void MainWindow::updateRecentMenu()
{
    mRecentMenu->clear();
    for (const auto &path : mRecentFiles) {
        QFileInfo info(path);
        if (!info.exists()) { continue; }
        mRecentMenu->addAction(QIcon::fromTheme(ThemeSupport::getAppIconName(true)), info.baseName(), [path, this]() {
            openFile(path);
        });
    }
}

void MainWindow::addRecentFile(const QString &recent)
{
    if (mRecentFiles.contains(recent)) {
        mRecentFiles.removeOne(recent);
    }
    while (mRecentFiles.count() >= 11) {
        mRecentFiles.removeLast();
    }
    mRecentFiles.prepend(recent);
    updateRecentMenu();
    writeRecentFiles();
}

void MainWindow::readRecentFiles()
{
    const auto files = AppSupport::getSettings("files",
                                               "recentSaved").toStringList();
    for (const auto &file : files) { mRecentFiles.append(file); }
}

void MainWindow::writeRecentFiles()
{
    QStringList files;
    for (const auto &file : mRecentFiles) { files.append(file); }
    AppSupport::setSettings("files", "recentSaved", files);
}

void MainWindow::handleNewVideoClip(const VideoBox::VideoSpecs &specs)
{
    int act = eSettings::instance().fAdjustSceneFromFirstClip;

    // never apply or bad specs?
    if (act == eSettings::AdjustSceneNever ||
        specs.fps < 1 ||
        specs.dim.height() < 1 ||
        specs.dim.width() < 1) { return; }

    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }

    // only continue if this is the only clip
    if (scene->getContainedBoxes().count() != 1) { return; }

    // is identical?
    if (scene->getCanvasSize() == specs.dim &&
        scene->getFps() == specs.fps &&
        scene->getFrameRange().fMax == specs.range.fMax) { return; }

    // always apply?
    if (act == eSettings::AdjustSceneAlways) {
        scene->setCanvasSize(specs.dim.width(),
                             specs.dim.height());
        scene->setFps(specs.fps);
        scene->setFrameRange(specs.range);
        return;
    }

    // open dialog if ask
    AdjustSceneDialog dialog(scene, specs, this);
    dialog.exec();
}
