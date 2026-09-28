/*
# enve2d - https://github.com/enve2d
#
# Copyright (c) enve2d developers
# Copyright (c) 2016-2020 Maurycy Liebner
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
*/

#include "GUI/mainwindow.h"
#include "GUI/hangwatchdog.h"
#include "GUI/canvaswindow.h"
#include "GUI/lyricmotionengine.h"
#include "GUI/lyricmotionnative.h"
#include "Private/document.h"
#include "Boxes/boxrenderdata.h"
#include "Boxes/textbox.h"
#include "Boxes/rectangle.h"
#include "Boxes/containerbox.h"
#include "Animators/transformanimator.h"
#include <QJsonDocument>

#include <iostream>
#include <thread>
#include <chrono>
#include <QApplication>
#include <QSurfaceFormat>
#include <QElapsedTimer>
#include <QResizeEvent>
#include <QExposeEvent>
#include <QWindow>

#ifdef Q_OS_WIN
// in-process crash minidump: no admin rights needed, writes next to the
// portable exe so the faulting stack can be analyzed afterwards
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <cstring>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
static LONG WINAPI writeCrashMiniDump(EXCEPTION_POINTERS* const pep) {
    const QString dir = QCoreApplication::applicationDirPath() +
                        QStringLiteral("/crash_dumps");
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/") +
            QDateTime::currentDateTime().toString(
                QStringLiteral("yyyyMMdd_hhmmss")) +
            QStringLiteral(".dmp");
    const HANDLE hFile = CreateFileW(
                reinterpret_cast<const wchar_t*>(file.utf16()),
                GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
    if(hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = pep;
        mdei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile,
                          static_cast<MINIDUMP_TYPE>(
                              MiniDumpNormal |
                              MiniDumpWithIndirectlyReferencedMemory |
                              MiniDumpScanMemory),
                          &mdei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
#ifndef Q_OS_WIN
#include <unistd.h>
#endif
#include <QSplashScreen>

#include "hardwareinfo.h"
#include "Private/esettings.h"
#include "GUI/ewidgetsimpl.h"
#include "importhandler.h"
#include "effectsloader.h"
#include "memoryhandler.h"
#include "ShaderEffects/shadereffectprogram.h"
#include "videoencoder.h"
#include "appsupport.h"
#include "themesupport.h"
#include "wizards/quicksetup.h"

#ifdef Q_OS_WIN
#include "windowsincludes.h"
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#include <QWindowsWindowFunctions>
#endif
#endif

#include <QJSEngine>
#include <QTranslator>
#include <QLocale>
#include <QFile>
#include <QTextStream>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QPainter>
#include <QTimer>
#include <QToolTip>
#include <QToolButton>

namespace {

// Renders dock title-bar float/close buttons as single text glyphs:
// U+1F5D7 (overlap) for "pop out / float", U+1F5D9 (cancellation x)
// for "close". Everything else is forwarded to the base style.
class DockGlyphStyle : public QProxyStyle
{
public:
    explicit DockGlyphStyle(QStyle *base) : QProxyStyle(base) {}

    QIcon standardIcon(const StandardPixmap standardIcon,
                       const QStyleOption *option = nullptr,
                       const QWidget *widget = nullptr) const override
    {
        if (standardIcon == SP_TitleBarNormalButton) {
            return glyphIcon(0x1F5D7, true); // pop out (float)
        }
        if (standardIcon == SP_TitleBarCloseButton) {
            return glyphIcon(0x1F5D9, false); // close
        }
        return QProxyStyle::standardIcon(standardIcon, option, widget);
    }

private:
    static QIcon glyphIcon(const char32_t cp, const bool isFloat)
    {
        // intentionally leaked: avoids static QPixmap teardown issues
        static QIcon *floatIcon = new QIcon;
        static QIcon *closeIcon = new QIcon;
        QIcon *cache = isFloat ? floatIcon : closeIcon;
        if (!cache->isNull()) { return *cache; }

        const int size = 16;
        const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        QPixmap pm(QSize(size, size) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setPen(qApp->palette().color(QPalette::WindowText));
        QFont f(QStringLiteral("Segoe UI Symbol"));
        f.setPixelSize(13);
        p.setFont(f);
        const QString glyph = QString::fromUcs4(&cp, 1);
        p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, glyph);
        p.end();
        *cache = QIcon(pm);
        return *cache;
    }
};

} // namespace

#define GPU_NOT_COMPATIBLE gPrintException("Your GPU drivers do not seem to be compatible.")

void setDefaultFormat()
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

#ifdef USE_GLES
    QApplication::setAttribute(Qt::AA_UseOpenGLES);
#else
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif

    QSurfaceFormat format;

#ifdef USE_GLES
    format.setVersion(3, 0);
    format.setProfile(QSurfaceFormat::NoProfile);
#else
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
#endif

    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0);
    //format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    //format.setSwapInterval(0); // Disable vertical refresh syncing
    QSurfaceFormat::setDefaultFormat(format);
}

void generateAlphaMesh(QPixmap& alphaMesh,
                       const int dim)
{
    alphaMesh = QPixmap(2*dim, 2*dim);
    const QColor light = QColor::fromRgbF(0.2, 0.2, 0.2);
    const QColor dark = QColor::fromRgbF(0.4, 0.4, 0.4);
    QPainter p(&alphaMesh);
    p.fillRect(0, 0, dim, dim, light);
    p.fillRect(dim, 0, dim, dim, dark);
    p.fillRect(0, dim, dim, dim, dark);
    p.fillRect(dim, dim, dim, dim, light);
    p.end();
}

void setScaleFactor(const bool passThrough)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication::setHighDpiScaleFactorRoundingPolicy(passThrough ?
                                                          Qt::HighDpiScaleFactorRoundingPolicy::PassThrough :
                                                          Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);
}

void earlySettings(char *argv[],
                   bool *hdpiPassThrough)
{
    // we can't use AppSupport for this
    const QString key = "settings/interfaceScalingPassThrough";

#ifdef Q_OS_WIN
    // we need to consider portable mode on Windows
    const QString exePath = QString::fromLocal8Bit(argv[0]);
    const QString appDir = QFileInfo(exePath).absolutePath();
    const bool isPortable = QFile::exists(QString("%1/portable.txt").arg(appDir));
    const QString portableConfigPath = QString("%1/config/friction.conf").arg(appDir);

    if (QFile::exists(portableConfigPath) && isPortable) {
        QSettings settings(portableConfigPath,
                           QSettings::IniFormat);
        *hdpiPassThrough = settings.value(key, true).toBool();
        return;
    }
#else
    Q_UNUSED(argv)
#endif

    QSettings settings(AppSupport::getAppName(),
                       AppSupport::getAppOrg());
    *hdpiPassThrough = settings.value(key, true).toBool();
}

#ifdef Q_OS_WIN
// in-process VEH crash reporter: heap failures (0xc0000374) that bypass
// the unhandled-exception filter still raise a first-chance exception
// first; print every stack value landing inside a known module as
// "module+rva" so residual crashes can be symbolized against the map/pdb
struct CrashModInfo { HMODULE base; SIZE_T size; char name[MAX_PATH]; };
static CrashModInfo gCrashMods[256];
static int gCrashModCount = 0;
static void cacheCrashModules() {
    HMODULE mods[256]; DWORD needed = 0;
    const HANDLE proc = GetCurrentProcess();
    if(!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return;
    const int n = static_cast<int>(needed/sizeof(HMODULE));
    for(int i = 0; i < n && i < 256; i++) {
        MODULEINFO mi;
        if(!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
        if(mi.SizeOfImage == 0) continue;
        gCrashMods[gCrashModCount].base = mods[i];
        gCrashMods[gCrashModCount].size = mi.SizeOfImage;
        char path[MAX_PATH];
        if(!GetModuleFileNameA(mods[i], path, MAX_PATH)) path[0] = 0;
        const char* slash = strrchr(path, '\\');
        lstrcpynA(gCrashMods[gCrashModCount].name,
                  slash ? slash + 1 : path, MAX_PATH);
        gCrashModCount++;
    }
}
static void crashReportMod(HANDLE hFile, const void* addr,
                           const SIZE_T* off) {
    for(int i = 0; i < gCrashModCount; i++) {
        const char* b = reinterpret_cast<const char*>(gCrashMods[i].base);
        if(addr < b || addr >= b + gCrashMods[i].size) continue;
        char line[MAX_PATH + 64];
        const uintptr_t rva =
                reinterpret_cast<uintptr_t>(addr)
                - reinterpret_cast<uintptr_t>(gCrashMods[i].base);
        if(off) {
            _snprintf(line, sizeof(line)-1,
                      "  [rsp+0x%04zX] %s+0x%llX\r\n",
                      reinterpret_cast<size_t>(off),
                      gCrashMods[i].name,
                      static_cast<unsigned long long>(rva));
        } else {
            _snprintf(line, sizeof(line)-1,
                      "%s+0x%llX   <- RIP\r\n",
                      gCrashMods[i].name,
                      static_cast<unsigned long long>(rva));
        }
        line[sizeof(line)-1] = 0;
        DWORD written = 0;
        WriteFile(hFile, line, lstrlenA(line), &written, nullptr);
        return;
    }
}
static LONG WINAPI firstChanceCrashReporter(
        EXCEPTION_POINTERS* const pep) {
    const DWORD code = pep->ExceptionRecord->ExceptionCode;
    if(code != 0xC0000005 && code != 0xC0000374 && code != 0xC0000409) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const QString dir = QCoreApplication::applicationDirPath() +
                        QStringLiteral("/crash_dumps");
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/crash_report_") +
            QDateTime::currentDateTime().toString(
                QStringLiteral("yyyyMMdd_hhmmss")) +
            QStringLiteral(".txt");
    const HANDLE hFile = CreateFileW(
                reinterpret_cast<const wchar_t*>(file.utf16()),
                GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
    if(hFile == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    DWORD written = 0;
    char header[96];
    wsprintfA(header, "=== CRASH === code=0x%08lX\r\n", code);
    WriteFile(hFile, header, lstrlenA(header), &written, nullptr);
    const CONTEXT& ctx = *pep->ContextRecord;
    crashReportMod(hFile, reinterpret_cast<const void*>(ctx.Rip),
                   nullptr);
    MEMORY_BASIC_INFORMATION mbi;
    for(SIZE_T off = 0; off < 24 * 1024; off += sizeof(void*)) {
        const void* sp = reinterpret_cast<const char*>(ctx.Rsp) + off;
        if(VirtualQuery(sp, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        if(mbi.State != MEM_COMMIT ||
           (mbi.Protect & (PAGE_NOACCESS|PAGE_GUARD))) continue;
        crashReportMod(hFile,
                       *reinterpret_cast<const void* const*>(sp), &off);
    }
    CloseHandle(hFile);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(writeCrashMiniDump);
    cacheCrashModules();
    AddVectoredExceptionHandler(1, firstChanceCrashReporter);
#endif
    const bool isRenderer = false; // todo

    // Qt 6.11 wayland-egl regression: with widget RHI enabled the first
    // show of every popup surface (menus, combos) presents a wrong-sized
    // GL buffer instead of the rendered raster image, so the compositor
    // stretches garbage across the popup until it is reopened. Force the
    // raster path for widget windows on wayland unless the user overrides
    // it explicitly. The Skia GL canvas is not affected (QOpenGLWidget
    // keeps its own GL contexts). Repro/verdict: see MENUPROBE runs,
    // 2026-09-21, kwin 6.7.5 + mesa 26.2.3 + qt6-base 6.11.2-3.
    {
        const bool wayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
        if (wayland && qEnvironmentVariableIsEmpty("QT_WIDGETS_RHI")) {
            qputenv("QT_WIDGETS_RHI", "0");
        }
    }

    // capture debug output from the very beginning
    MainWindow::installDebugLogHandler();

    // get early settings
    bool hdpiPassThrough = true;
    earlySettings(argv, &hdpiPassThrough);
    qDebug() << "hdpiPassThrough" << hdpiPassThrough;

    // init env variables
    AppSupport::initEnv(isRenderer);

    // version info
    AppSupport::printVersion();

    // init app
    QApplication::setApplicationDisplayName(AppSupport::getAppDisplayName());
    QApplication::setApplicationName(AppSupport::getAppName());
    QApplication::setOrganizationName(AppSupport::getAppOrg());
    QApplication::setOrganizationDomain(AppSupport::getAppDomain());
    QApplication::setApplicationVersion(AppSupport::getAppVersion());

    // setup scaling
    setScaleFactor(hdpiPassThrough);

    // setup OpenGL
    setDefaultFormat();

    // setup app
    QApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");

    // dev-only menu probe: FRICTION_MENUPROBE=1 logs the lifecycle of
    // every QMenu/QMenuBar popup (polish/show/resize/expose order, size
    // vs sizeHint, window geometry) to diagnose the Wayland
    // first-popup stretched-render issue
    class MenuProbe : public QObject
    {
    public:
        QElapsedTimer t;
        explicit MenuProbe(QObject *parent) : QObject(parent) { t.start(); }
        bool eventFilter(QObject *obj, QEvent *e) override
        {
            const auto mo = obj->metaObject();
            if (!mo) { return QObject::eventFilter(obj, e); }
            const QByteArray cn = mo->className();
            const bool isMenu = cn == "QMenu" || cn == "QMenuBar"
                                || cn.endsWith("Menu");
            const bool isMenuWin = cn == "QWidgetWindow"
                                   && obj->objectName() == QLatin1String("menu popup window")
                                         ? true : false;
            if (isMenu) {
                const auto w = static_cast<QWidget*>(obj);
                const auto tag = QString(cn) + " '" + w->objectName() + "'"
                                 + " title '" + w->windowTitle() + "'";
                switch (e->type()) {
                case QEvent::Polish:
                    qWarning() << "[MENUPROBE]" << t.elapsed() << tag << "POLISH"
                               << "size" << w->size() << "hint" << w->sizeHint();
                    break;
                case QEvent::Show:
                    qWarning() << "[MENUPROBE]" << t.elapsed() << tag << "SHOW"
                               << "size" << w->size() << "hint" << w->sizeHint()
                               << "geo" << w->geometry()
                               << "winGeo" << (w->window() ? w->window()->geometry() : QRect());
                    break;
                case QEvent::Resize: {
                    const auto re = static_cast<QResizeEvent*>(e);
                    qWarning() << "[MENUPROBE]" << t.elapsed() << tag << "RESIZE"
                               << re->oldSize() << "->" << re->size()
                               << "hint" << w->sizeHint();
                    break; }
                case QEvent::Hide:
                    qWarning() << "[MENUPROBE]" << t.elapsed() << tag << "HIDE";
                    break;
                default: break;
                }
            } else if (isMenuWin || cn == "QWidgetWindow") {
                // log expose/update for any widget window whose title
                // matches a menu (QMenu windows are titled like the menu)
                switch (e->type()) {
                case QEvent::Expose: {
                    const auto we = static_cast<QExposeEvent*>(e);
                    const auto qw = qobject_cast<QWindow*>(obj);
                    qWarning() << "[MENUPROBE]" << t.elapsed() << "WINDOW"
                               << (qw ? qw->title() : QString()) << "EXPOSE"
                               << (we ? we->region().boundingRect() : QRect())
                               << "geo" << (qw ? qw->geometry() : QRect())
                               << "frame" << (qw ? qw->frameMargins() : QMargins());
                    break; }
                case QEvent::UpdateRequest: {
                    const auto qw = qobject_cast<QWindow*>(obj);
                    if (qw && !qw->title().isEmpty()) {
                        qWarning() << "[MENUPROBE]" << t.elapsed() << "WINDOW"
                                   << qw->title() << "UPDATEREQUEST geo" << qw->geometry();
                    }
                    break; }
                default: break;
                }
            }
            return QObject::eventFilter(obj, e);
        }
    };
    if (qEnvironmentVariableIsSet("FRICTION_MENUPROBE")) {
        static MenuProbe probe(&app);
        app.installEventFilter(&probe);
    }


    // load UI theme preference before any theme setup
    ThemeSupport::setThemeFromId(AppSupport::getSettings("ui",
                                                         "theme",
                                                         "friction").toString());

    // freeze watchdog: dumps all thread stacks to
    // %TEMP%/friction_hang_stack.txt while the UI is frozen.
    // Must run after QApplication so the heartbeat timer starts.
    HangWatchdog::start();

    // i18n: language from the preferences ("ui"/"language", default
    // Chinese regardless of the system locale); takes effect on
    // restart
    static QTranslator appTranslator;
    {
        const auto locale = QLocale::system();
        const QString lang = AppSupport::getSettings(
                    QStringLiteral("ui"), QStringLiteral("language"),
                    QStringLiteral("zh_CN")).toString();
        const bool isChinese = lang == QStringLiteral("zh_CN");
        const bool loaded = appTranslator.load(":/translations/friction_zh_CN.qm");
        if (isChinese && loaded) {
            QCoreApplication::installTranslator(&appTranslator);
        }
        // runtime i18n debug dump
        {
            QFile dbg(QCoreApplication::applicationDirPath() + "/i18n_debug.txt");
            if (dbg.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream s(&dbg);
                #if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                s.setEncoding(QStringConverter::Utf8);
#else
                s.setCodec("UTF-8");
#endif
                s << "qt: " << qVersion() << "\n";
                s << "locale: " << locale.name() << " chinese=" << isChinese
                  << " loaded=" << loaded << "\n";
                s << "Union+comment: [" << QCoreApplication::translate("MainWindow", "Union", "MenuBar_Path") << "]\n";
                s << "Union-nocomment: [" << QCoreApplication::translate("MainWindow", "Union") << "]\n";
                s << "Object+comment: [" << QCoreApplication::translate("MainWindow", "Object", "MenuBar") << "]\n";
                s << "Object-nocomment: [" << QCoreApplication::translate("MainWindow", "Object") << "]\n";
                s << "SceneProps: [" << QCoreApplication::translate("SceneSettingsDialog", "Scene Properties") << "]\n";
                s << "transform: [" << QCoreApplication::translate("BoxSingleWidget", "transform") << "]\n";
                s << "Loading: [" << QCoreApplication::translate("QObject", "Loading ...") << "]\n";
                s << "CurrentScene: [" << QCoreApplication::translate("TimelineWidget", "Current Scene") << "]\n";
            }
        }
    }

    // first run
    const bool firstRun = AppSupport::getSettings("settings",
                                                  "firstRun",
                                                  true).toBool();
    if (firstRun) {
        ThemeSupport::setupTheme();
        Friction::Ui::QuickSetup wizard;
        wizard.exec();
        AppSupport::setSettings("settings", "firstRun", false);
    }

    // handle XDG args
#ifdef Q_OS_LINUX
    const auto handleXDGActs = AppSupport::handleXDGArgs(isRenderer,
                                                         QApplication::arguments());
    if (handleXDGActs.first) { return handleXDGActs.second; }
    if (AppSupport::isWayland()) {
        QGuiApplication::setDesktopFileName(AppSupport::getAppID());
    }
#endif

    // init windows
#ifdef Q_OS_WIN
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    // https://bugreports.qt.io/browse/QTBUG-58610
    // https://github.com/musescore/MuseScore/pull/5820
    QApplication::setFont(QApplication::font("QMessageBox"));
#endif
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    if (!isRenderer) {
        // QWindowsWindowFunctions was removed in Qt6; Qt6 keeps the
        // window frame in fullscreen by default on Windows
        QWindowsWindowFunctions::setHasBorderInFullScreenDefault(true);
    }
#endif
#endif
    const bool showSplash = true;

    // init splash
    QSplashScreen splash(QPixmap(":/icons/friction-splash.png"));
    if (showSplash) {
        splash.show();
        splash.raise();
        splash.showMessage(QObject::tr("Loading ..."),
                           Qt::AlignRight | Qt::AlignBottom, Qt::white);
    }

    // init hardware
#ifndef Q_OS_DARWIN
    const bool threadedOpenGL = QOpenGLContext::supportsThreadedOpenGL();
    if (!threadedOpenGL) {
        gPrintException("Your GPU drivers do not support OpenGL "
                        "rendering outside the main thread");
    }
#endif

    try {
        HardwareInfo::sUpdateInfo();
    } catch(const std::exception& e) {
        GPU_NOT_COMPATIBLE;
        gPrintExceptionCritical(e);
    }

    // init settings
    eSettings settings(HardwareInfo::sCpuThreads(),
                       HardwareInfo::sRamKB());

    // setup UI scaling and theme
    OS_FONT = QApplication::font();
    eSizesUI::font.setEvaluator([&settings]() {
        const auto fm = QFontMetrics(OS_FONT);
        if (!settings.fDefaultInterfaceScaling) {
            const qreal scaling = qBound(0.5, settings.fInterfaceScaling, 1.5);
            return qRound(fm.height() * scaling);
        }
        return fm.height();
    });
    eSizesUI::widget.setEvaluator([]() {
        return eSizesUI::font.size()*4/3;
    });
    QObject::connect(&eSizesUI::font, &SizeSetter::sizeChanged,
                     &eSizesUI::widget, &SizeSetter::updateSize);
    eSizesUI::font.add(&app, [&app](const int size) {
        const auto fm = QFontMetrics(OS_FONT);
        const qreal mult = size/qreal(fm.height());
        QFont font = OS_FONT;
        if(OS_FONT.pixelSize() == -1) {
            font.setPointSizeF(mult*OS_FONT.pointSizeF());
        } else {
            font.setPixelSize(qRound(mult*OS_FONT.pixelSize()));
        }
        app.setFont(font);
    });

    eSizesUI::widget.add(&eSizesUI::button, [](const int size) {
        eSizesUI::button.set(qRound(size*1.1));
    });

    eSizesUI::widget.add([](const int size) {
        KEY_RECT_SIZE = size*3/5;
    });

    QPixmap alphaMesh;
    eSizesUI::widget.add([&alphaMesh](const int size) {
        generateAlphaMesh(alphaMesh, size/2);
    });
    ALPHA_MESH_PIX = &alphaMesh;

    ThemeSupport::setupTheme(eSizesUI::widget);

    // dock title-bar buttons use single glyphs (pop out / close)
    qApp->setStyle(new DockGlyphStyle(
        QStyleFactory::create(QStringLiteral("fusion"))));

    // check permissions
    AppSupport::checkPerms(isRenderer);

    // portable
    AppSupport::handlePortableFirstRun();

    // check XDG integration
#ifdef Q_OS_LINUX
    AppSupport::initXDGDesktop(isRenderer);
#endif

    if (showSplash) {
        splash.raise();
        splash.showMessage(QObject::tr("Initializing ..."),
                           Qt::AlignRight | Qt::AlignBottom, Qt::white);
    }

    // load settings
    try { settings.loadFromFile(); }
    catch(const std::exception& e) { gPrintExceptionCritical(e); }

    // init handlers
    eFilterSettings filterSettings;
    eWidgetsImpl widImpl;
    ImportHandler importHandler;
    MemoryHandler memoryHandler;
    TaskScheduler taskScheduler;

    QObject::connect(&memoryHandler, &MemoryHandler::enteredCriticalState,
                     &taskScheduler, &TaskScheduler::enterCriticalMemoryState);
    QObject::connect(&memoryHandler, &MemoryHandler::finishedCriticalState,
                     &taskScheduler, &TaskScheduler::finishCriticalMemoryState);

    Document document(taskScheduler);
    Actions actions(document);

    EffectsLoader effectsLoader;
    try {
        effectsLoader.initializeGpu();
        taskScheduler.initializeGpu();
    } catch(const std::exception& e) {
        GPU_NOT_COMPATIBLE;
        gPrintExceptionFatal(e);
    }

    // disabled for now
    //effectsLoader.iniCustomPathEffects();
    //std::cout << "Custom path effects initialized" << std::endl;

    // disabled for now
    //effectsLoader.iniCustomRasterEffects();
    //std::cout << "Custom raster effects initialized" << std::endl;

    if (showSplash) {
        splash.raise();
        splash.showMessage(QObject::tr("Loading Shaders ..."),
                           Qt::AlignRight | Qt::AlignBottom, Qt::white);
    }

    // init shaders
#ifndef USE_GLES
    try {
        effectsLoader.iniShaderEffects();
    } catch(const std::exception& e) {
        GPU_NOT_COMPATIBLE;
        gPrintExceptionCritical(e);
    }
    QObject::connect(&effectsLoader, &EffectsLoader::programChanged,
    [&document](ShaderEffectProgram * program) {
        for (const auto& scene : document.fScenes) {
            scene->updateIfUsesProgram(program);
        }
        document.actionFinished();
    });
#endif

    // disabled for now
    //effectsLoader.iniCustomBoxes();
    //std::cout << "Custom objects initialized" << std::endl;

    if (showSplash) {
        splash.raise();
        splash.showMessage(QObject::tr("Loading Audio ..."),
                           Qt::AlignRight | Qt::AlignBottom, Qt::white);
    }

    // init audio
    eSoundSettings soundSettings;
    AudioHandler audioHandler;

    if (!isRenderer) {
        try {
            audioHandler.initializeAudio(soundSettings.sData(),
                                         AppSupport::getSettings(QString::fromUtf8("audio"),
                                                                 QString::fromUtf8("output")).toString());
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
    }


    if (showSplash) {
        splash.raise();
        splash.showMessage(QObject::tr("Loading Encoder ..."),
                           Qt::AlignRight | Qt::AlignBottom, Qt::white);
    }

    // init encoder
    const auto videoEncoder = enve::make_shared<VideoEncoder>();
    RenderHandler renderHandler(document, audioHandler,
                                *videoEncoder, memoryHandler);

    // check for ffmpeg version
    AppSupport::checkFFmpeg(isRenderer);

    if (showSplash) {
        splash.raise();
        splash.showMessage(QObject::tr("Loading User Interface ..."),
                           Qt::AlignRight | Qt::AlignBottom, Qt::white);
    }

    // load UI
    const QString openProject = argc > 1 ? argv[1] : QString();
    MainWindow w(document,
                 actions,
                 audioHandler,
                 renderHandler,
                 openProject);
    w.show();

    // dev-only lyric apply probe: FRICTION_LYRICAPPLY=1 drives the real
    // GUI path the panel's 应用到场景 button uses (plan → native build
    // → playhead jump) against a fresh scene, waits for the render
    // tasks, grabs the canvas and exits - reproduces "applied but the
    // canvas shows no text" without touching the user's project
    if (qEnvironmentVariableIsSet("FRICTION_LYRICAPPLY")) {
        const bool probeGpuOff = qEnvironmentVariable("FRICTION_LYRICAPPLY")
                != QLatin1String("gpu");
        // dock-open latency probe: the lyric panel's preview request
        // used to run synchronously on this thread (a direct call on a
        // moveToThread'd worker executes in the caller); show the dock
        // early and heartbeat the event loop to prove the open is
        // stutter-free — stalls during the later apply/build are the
        // probe's own synchronous build, not the dock
        QTimer::singleShot(300, &w, [&w]() {
            auto *gap = new QElapsedTimer(); // probe-lifetime, no parent
            gap->start();
            auto *beat = new QTimer(&w);
            beat->setInterval(50);
            QObject::connect(beat, &QTimer::timeout, beat, [gap]() {
                const qint64 ms = gap->restart();
                if (ms > 120) {
                    qWarning() << "[LYRICAPPLY] GUI stall" << ms << "ms";
                }
            });
            beat->start();
            if (auto *dock = w.findChild<QDockWidget*>(
                        QStringLiteral("dockLyricMotion"))) {
                dock->show();
                dock->raise();
                qWarning() << "[LYRICAPPLY] lyric dock shown t+"
                           << gap->elapsed() << "ms";
            }
        });
        QTimer::singleShot(800, &w, [&w, &document, probeGpuOff]() {
            // the standalone smoke disables the GPU path before any
            // scene exists; mirror that unless FRICTION_LYRICAPPLY=gpu
            if (probeGpuOff && eSettings::sInstance) {
                eSettings::sInstance->fPathGpuAcc = false;
            }
            const auto scene = document.createNewScene(true);
            scene->setCanvasSize(1920, 1080);
            scene->setFps(24);
            qWarning() << "[LYRICAPPLY] scene created";
            // control: one PLAIN text box created the way a user
            // would - if this renders while the lyric texts do not,
            // the builder path is the problem, not text rendering
            {
                const auto ctl = enve::make_shared<TextBox>();
                scene->getCurrentGroup()->addContained(ctl);
                ctl->prp_setName(QStringLiteral("探针对照"));
                ctl->setCurrentValue(QStringLiteral("对照测试文字ABC"));
                ctl->setFontFamilyAndStyle(
                            QStringLiteral("Noto Sans CJK JP"),
                            SkFontStyle(700, SkFontStyle::kNormal_Width,
                                        SkFontStyle::kUpright_Slant));
                ctl->setFontSize(120);
                ctl->setTextHAlignment(Qt::AlignHCenter);
                ctl->setTextVAlignment(Qt::AlignVCenter);
                ctl->getFillSettings()->setPaintType(PaintType::FLATPAINT);
                ctl->getFillSettings()->setCurrentColor(Qt::white);
                ctl->getTransformAnimator()->getPosAnimator()->setBaseValue(
                            QPointF(960, 540));
                qWarning() << "[LYRICAPPLY] control text added";
            }
            LyricMotionEngine engine;
            QString err;
            if (!engine.ensureLoaded(&err)) {
                qWarning() << "[LYRICAPPLY] engine load failed:" << err;
                QApplication::exit(2);
                return;
            }
            LyricMotionEngine::Params p;
            // bisect knob: FRICTION_LYRICAPPLY=smoke uses the exact
            // params of the passing standalone smoke; =user (default)
            // uses the user's real panel params
            const bool smokeParams = qEnvironmentVariable(
                        "FRICTION_LYRICAPPLY") == QLatin1String("smoke");
            if (smokeParams) {
                p.lyrics = QStringLiteral(
                            "[00:01.00]夜明けの色を/覚えてる\n"
                            "[00:03.50]*文字* Motion 歌词动画\n"
                            "[00:05.50]两行歌词 第二句!\n"
                            "[間奏 2]\n"
                            "[00:09.50]ラストライン end");
                p.style = QStringLiteral("noir");
                p.seed = 7;
                p.density = 0.55;
                p.bpm = 120;
            } else {
                p.lyrics = QStringLiteral("你啊好哦啊 你是谁");
                p.style = QStringLiteral("transit");
                p.seed = 1;
                p.density = 0.36;
            }
            p.chroma = 0.7;
            // FRICTION_LYRICTRANS=mixed|<key> forces a transition
            // on every boundary through the planner override channel
            const QByteArray transEnv = qgetenv("FRICTION_LYRICTRANS");
            if (!transEnv.isEmpty()) {
                p.transMode = transEnv == "mixed" ? 1 : 2;
                p.transKey = QString::fromUtf8(transEnv);
            }
            const QString json = engine.planJson(p, &err);
            if (json.isEmpty()) {
                qWarning() << "[LYRICAPPLY] plan failed:" << err;
                QApplication::exit(2);
                return;
            }
            const auto doc = QJsonDocument::fromJson(json.toUtf8()).object();
            const auto plan = doc.value(QStringLiteral("plan")).toObject();
            const auto fonts = doc.value(QStringLiteral("fonts")).toObject();
            LyricMotionNative::Result result;
            QString buildErr;
            const bool ok = LyricMotionNative::build(
                        scene, plan, fonts, QString(), false,
                        &result, &buildErr, &p);
            qWarning() << "[LYRICAPPLY] build ok=" << ok
                       << "err=" << buildErr
                       << "cuts=" << result.cutsBuilt
                       << "notes=" << result.notes;
            if (!ok) { QApplication::exit(2); return; }
            // mirror applyToScene's playhead jump
            const auto firstCut = plan.value(QStringLiteral("cuts"))
                    .toArray().first().toObject();
            const qreal fStartS = firstCut.value(QStringLiteral("start")).toDouble();
            const qreal fEndS = firstCut.value(QStringLiteral("end")).toDouble();
            const qreal mid = (fStartS + fEndS) * 0.5;
            const int frame = qMax(0, qRound(mid * scene->getFps()));
            scene->anim_setAbsFrame(frame);
            qWarning() << "[LYRICAPPLY] jumped to frame" << frame
                       << "of" << scene->getFrameRange().fMax
                       << "cut" << fStartS << "-" << fEndS << "s"
                       << "top boxes" << scene->getContainedBoxes().size();
            for (const auto &b : scene->getContainedBoxes()) {
                const auto cont = dynamic_cast<ContainerBox*>(b);
                qWarning() << "[LYRICAPPLY] box" << b->prp_getName()
                           << "type" << int(b->getBoxType())
                           << "children"
                           << (cont ? cont->getContainedBoxes().size() : 0);
                // dump every cut group's text census so an invisible
                // run shows exactly which knob is off
                if (cont && b->prp_getName() ==
                            QStringLiteral("歌词动画")) {
                    for (const auto &cg : cont->getContainedBoxes()) {
                        const auto cutGroup = dynamic_cast<ContainerBox*>(cg);
                        if (!cutGroup) { continue; }
                        const auto dr = cutGroup->getDurationRectangle();
                        int nText = 0, nOther = 0;
                        qreal maxSize = 0;
                        QString sample;
                        for (const auto &tb : cutGroup->getContainedBoxes()) {
                            const auto txt = dynamic_cast<TextBox*>(tb);
                            if (txt) {
                                nText++;
                                if (txt->getFontSize() > maxSize) {
                                    maxSize = txt->getFontSize();
                                    sample = txt->getCurrentValue();
                                }
                            } else { nOther++; }
                        }
                        qWarning() << "[LYRICAPPLY]  cut"
                                   << cg->prp_getName()
                                   << "durRect"
                                   << (dr ? QString("%1+%2")
                                          .arg(dr->getMinAbsFrame())
                                          .arg(dr->getFrameDuration())
                                      : QStringLiteral("none"))
                                   << "texts" << nText << "others" << nOther
                                   << "maxSize" << maxSize
                                   << "sample" << sample;
                        // rect census (first 2 groups): the largest
                        // rects carry the background — a washed-out
                        // gray background means a semi-transparent or
                        // wrong-color rect (web composites bg fades
                        // over its own black page; friction has only
                        // whatever alpha the replay captured)
                        // raster-effect census: transitions attach
                        // WIPE/ZOOM_BLUR/... to the cut group - an
                        // empty list means the transition never landed
                        if (const auto rc = cutGroup
                                ->rasterEffectsCollection()) {
                            QStringList effs;
                            for (int ei = 0;
                                 ei < rc->ca_getNumberOfChildren();
                                 ei++) {
                                effs << rc->ca_getChildAt(ei)
                                        ->prp_getName();
                            }
                            qWarning() << "[LYRICAPPLY]  effects"
                                       << cg->prp_getName()
                                       << effs;
                        }
                        static int dumpedRects = 0;
                        if (dumpedRects < 2) {
                            dumpedRects++;
                            QList<QPair<qreal, QString>> rects;
                            for (const auto &tb :
                                 cutGroup->getContainedBoxes()) {
                                const auto r =
                                        dynamic_cast<RectangleBox*>(tb);
                                if (!r) { continue; }
                                const QPointF tl = r->getTopLeftAnimator()
                                        ->getBaseValue();
                                const QPointF br = r->getBottomRightAnimator()
                                        ->getBaseValue();
                                const qreal area = (br.x() - tl.x())
                                        * (br.y() - tl.y());
                                const QColor col = r->getFillSettings()
                                        ->getColor();
                                rects << qMakePair(area,
                                        QStringLiteral(
                                            "%1,%2 %3x%4 %5 a=%6")
                                        .arg(qRound(tl.x()))
                                        .arg(qRound(tl.y()))
                                        .arg(qRound(br.x() - tl.x()))
                                        .arg(qRound(br.y() - tl.y()))
                                        .arg(col.name(QColor::HexArgb))
                                        .arg(r->getBoxTransformAnimator()
                                             ->getOpacityAnimator()
                                             ->getCurrentBaseValue()));
                            }
                            std::sort(rects.begin(), rects.end());
                            qWarning() << "[LYRICAPPLY]  rects(top3)"
                                << (rects.size() >= 3
                                    ? rects.mid(rects.size() - 3)
                                    : rects);
                        }
                        // per-glyph animation census (first 2 cut
                        // groups only): each big glyph must show its
                        // own key window — a dump where every glyph
                        // shares the same first key frame is the
                        // "everything moves together" signature
                        static int dumpedCuts = 0;
                        if (dr && dumpedCuts < 2 && nText > 0
                                && maxSize > 0) {
                            dumpedCuts++;
                            const int f0 = dr->getMinAbsFrame() + 1;
                            int gi = 0;
                            for (const auto &tb :
                                 cutGroup->getContainedBoxes()) {
                                const auto txt =
                                        dynamic_cast<TextBox*>(tb);
                                if (!txt || txt->getFontSize()
                                        < maxSize * 0.55) { continue; }
                                const auto tr =
                                        txt->getTransformAnimator();
                                const auto opa = txt
                                        ->getBoxTransformAnimator()
                                        ->getOpacityAnimator();
                                QStringList alphaTrail;
                                for (int f = f0; f < f0 + 12; f += 2) {
                                    alphaTrail << QString::number(
                                        opa->getEffectiveValueAtAbsFrame(f),
                                        'f', 0);
                                }
                                const int nKeys =
                                        opa->anim_getKeys().count();
                                QStringList kv;
                                for (const auto *k : opa->anim_getKeys()) {
                                    kv << QStringLiteral("%1=%2")
                                        .arg(k->getAbsFrame())
                                        .arg(QString::number(
                                                 opa->getEffectiveValueAtAbsFrame(
                                                     k->getAbsFrame()),
                                                 'f', 0));
                                }
                                qWarning() << "[LYRICAPPLY]   glyph"
                                    << gi++ << txt->getCurrentValue()
                                    << "base" << opa->getCurrentBaseValue()
                                    << "opaKeys" << nKeys
                                    << kv.join(QLatin1Char(' '))
                                    << "alpha@f0+0,2..12"
                                    << alphaTrail.join(QLatin1Char(','))
                                    << "sclX@f0"
                                    << tr->getScaleAnimator()
                                       ->getXAnimator()
                                       ->getEffectiveValueAtAbsFrame(f0)
                                    << "posY@f0"
                                    << tr->getPosAnimator()
                                       ->getYAnimator()
                                       ->getEffectiveValueAtAbsFrame(f0);
                                if (gi >= 14) { break; }
                            }
                        }
                    }
                }
            }
            const int fStartF = qMax(0, qRound(fStartS * scene->getFps()));
            const int fEndF = qRound(fEndS * scene->getFps());
            const QList<int> probeFrames = {fStartF, fStartF + 3, frame,
                                            fEndF - 3, fEndF - 1,
                                            fEndF + 2, 0};
            QTimer::singleShot(1500, &w, [&w, scene, probeFrames]() {
                const auto cw = w.findChild<CanvasWindow*>();
                if (!cw) {
                    qWarning() << "[LYRICAPPLY] no CanvasWindow";
                    QApplication::exit(4);
                    return;
                }
                int anyBright = 0;
                for (int fi = 0; fi < probeFrames.size(); fi++) {
                    const int f = probeFrames.at(fi);
                    scene->anim_setAbsFrame(f);
                    QThread::msleep(350); // let render tasks settle
                    QCoreApplication::processEvents(
                                QEventLoop::AllEvents, 300);
                    const QImage grab = cw->grab().toImage();
                    grab.save(QStringLiteral("lyricapply_f%1.png").arg(fi));
                    int bright = 0;
                    for (int y = grab.height() / 5;
                         y < grab.height() * 4 / 5; y += 3) {
                        for (int x = grab.width() / 5;
                             x < grab.width() * 4 / 5; x += 3) {
                            const QRgb rgb = grab.pixel(x, y);
                            if (qAlpha(rgb) > 10 && qGray(rgb) > 160) {
                                bright++;
                            }
                        }
                    }
                    // offscreen cross-check on the same frame: the
                    // smoke test renders this way, the GUI canvas
                    // draws another way - a divergence pins the bug
                    const auto rd = scene->queExternalRender(f, false);
                    for (int w2 = 0; w2 < 3000; w2++) {
                        QCoreApplication::processEvents(
                                    QEventLoop::AllEvents, 20);
                        if (rd && rd->finished()) { break; }
                        QThread::msleep(10);
                    }
                    int obright = 0;
                    if (rd && rd->fRenderedImage) {
                        const auto raster = rd->fRenderedImage->makeRasterImage();
                        SkPixmap pm;
                        if (raster && raster->peekPixels(&pm)) {
                            const QImage oimg(
                                        static_cast<const uchar*>(pm.addr()),
                                        pm.width(), pm.height(),
                                        static_cast<qsizetype>(pm.rowBytes()),
                                        QImage::Format_ARGB32_Premultiplied);
                            oimg.save(QStringLiteral("lyricapply_o%1.png").arg(fi));
                            for (int y = oimg.height() / 5;
                                 y < oimg.height() * 4 / 5; y += 6) {
                                for (int x = oimg.width() / 5;
                                     x < oimg.width() * 4 / 5; x += 6) {
                                    const QRgb rgb = oimg.pixel(x, y);
                                    if (qAlpha(rgb) > 10 && qGray(rgb) > 160) {
                                        obright++;
                                    }
                                }
                            }
                        }
                    }
                    qWarning() << "[LYRICAPPLY] frame" << f
                               << "guiBright" << bright
                               << "offBright" << obright
                               << "rd" << (rd != nullptr)
                               << "img" << (rd && rd->fRenderedImage);
                    anyBright = qMax(anyBright, bright);
                }
                qWarning() << "[LYRICAPPLY] DONE anyBright" << anyBright;
                QApplication::exit(anyBright > 30 ? 0 : 5);
            });
        });
    }

    // dev-only tooltip probe: FRICTION_TIPPROBE=1 runs the real app,
    // hovers the first toolbox button, grabs the QTipLabel and dumps
    // metrics + png, then quits - reproduces the reported vertical
    // text mis-centering inside the real process
    if (qEnvironmentVariableIsSet("FRICTION_TIPPROBE")) {
        QTimer::singleShot(1500, &w, [&w]() {
            QToolButton* btn = nullptr;
            for (auto* wid : QApplication::allWidgets()) {
                const auto tb = qobject_cast<QToolButton*>(wid);
                if (tb && tb->objectName() == QStringLiteral("ToolBoxButton")
                    && !tb->toolTip().isEmpty()) {
                    btn = tb; break;
                }
            }
            if (!btn) {
                qWarning() << "[TIPPROBE] no toolbox button found";
                QApplication::exit(3);
                return;
            }
            qWarning() << "[TIPPROBE] button:" << btn->toolTip()
                       << "btnFont:" << btn->font().toString()
                       << "appFont:" << QApplication::font().toString()
                       << "dpr:" << qApp->devicePixelRatio();
            QToolTip::showText(btn->mapToGlobal(QPoint(4, 4)),
                               btn->toolTip(), btn, btn->rect());
            QTimer::singleShot(400, &w, [btn]() {
                QWidget* tip = nullptr;
                for (auto* wid : QApplication::allWidgets()) {
                    if (strcmp(wid->metaObject()->className(),
                               "QTipLabel") == 0) {
                        tip = wid; break;
                    }
                }
                if (!tip) {
                    qWarning() << "[TIPPROBE] no QTipLabel";
                    QApplication::exit(4);
                    return;
                }
                const auto m = tip->contentsMargins();
                qWarning() << "[TIPPROBE] tip size:" << tip->size()
                           << "sizeHint:" << tip->sizeHint()
                           << "font:" << tip->font().toString()
                           << "fmH:" << QFontMetrics(tip->font()).height()
                           << "margins:" << m
                           << "text:" << tip->property("text").toString();
                const QImage img = tip->grab().toImage();
                img.save(QStringLiteral("tipprobe.png"));
                qWarning() << "[TIPPROBE] saved tipprobe.png"
                           << img.size();
                QApplication::exit(0);
            });
        });
    }

    splash.finish(&w);

    try {
        const int exitCode = app.exec();
        // shutdown watchdog: user reports of the process staying
        // resident (high memory) after the window closed point at a
        // hanging destructor in the cleanup chain (worker threads, GL
        // contexts). Give normal cleanup 3 seconds, then hard-exit so
        // the process can never linger. The log line also tells us
        // whether the hang is real ("收尾超时") for a targeted fix later.
        std::thread([exitCode]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            qWarning() << "EXIT: 收尾超时（析构链挂起），强制退出进程";
#ifdef Q_OS_WIN
            ::ExitProcess(exitCode);
#else
            ::_exit(exitCode);
#endif
        }).detach();
        return exitCode;
    } catch(const std::exception& e) {
        gPrintExceptionFatal(e);
        return -1;
    }
}
