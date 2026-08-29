// headless reproduction harness for the project-switch heap corruption:
//   friction_harness.exe -platform offscreen A.friction B.friction
// mirrors openFile(A) -> clearAll() -> openFile(B) using only the core
// loading path (no MainWindow / layout / render widget), with breadcrumb
// checkpoints on stderr so the last printed line brackets the phase
// that corrupts the heap.
#include <windows.h>
#include <dbghelp.h>
#include <cstring>
#include <QApplication>
#include <QFile>
#include <cstdio>
#include "ReadWrite/filefooter.h"
#include "ReadWrite/ewritestream.h"
#include "ReadWrite/evformat.h"
#include "ReadWrite/filefooter.h"
#include "ReadWrite/ewritestream.h"
#include "ReadWrite/evformat.h"
#include "Private/document.h"
#include "Private/esettings.h"
#include "Private/Tasks/taskscheduler.h"
#include "hardwareinfo.h"
#include "efiltersettings.h"
#include "actions.h"
#include "importhandler.h"
#include "Sound/esoundsettings.h"
#include "canvas.h"
#include "Boxes/rectangle.h"
#include "Boxes/bonelayer.h"
#include "Boxes/bone.h"
#include "Boxes/imagebox.h"
#include "RasterEffects/rastereffectcollection.h"
#include "fileshandler.h"
#include "memoryhandler.h"
#include <QImage>
#include <QDir>
#include "Animators/motionpathhandler.h"

#define NOMINMAX
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// VEH crash reporter: on an access violation print every return address
// found near the faulting RIP / on the raw stack that falls inside one of
// our modules, as "module+rva". Combined with the linker /MAP file this
// substitutes for a debugger.
struct ModInfo { HMODULE base; SIZE_T size; char name[MAX_PATH]; };
static ModInfo gMods[256];
static int    gModCount = 0;
static void cacheModules() {
    HMODULE mods[256]; DWORD needed = 0;
    HANDLE proc = GetCurrentProcess();
    if(!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return;
    const int n = sizeof(mods)/sizeof(HMODULE);
    for(int i = 0; i < n && i < (int)(needed/sizeof(HMODULE)); i++) {
        MODULEINFO mi;
        if(!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
        if(mi.SizeOfImage == 0) continue;
        gMods[gModCount].base = mods[i];
        gMods[gModCount].size = mi.SizeOfImage;
        char path[MAX_PATH];
        if(!GetModuleFileNameA(mods[i], path, MAX_PATH)) path[0] = 0;
        const char* slash = strrchr(path, '\\');
        lstrcpynA(gMods[gModCount].name,
                  slash ? slash + 1 : path, MAX_PATH);
        gModCount++;
    }
}
static const ModInfo* findMod(const void* addr) {
    for(int i = 0; i < gModCount; i++) {
        const char* b = reinterpret_cast<const char*>(gMods[i].base);
        if(addr >= b && addr < b + gMods[i].size) return &gMods[i];
    }
    return nullptr;
}
static bool modOf(const void* addr, char* out, int cap) {
    const auto m = findMod(addr);
    if(!m) { out[0] = 0; return false; }
    const uintptr_t rva = reinterpret_cast<uintptr_t>(addr)
                        - reinterpret_cast<uintptr_t>(m->base);
    _snprintf(out, cap - 1, "%s+0x%llX", m->name,
              static_cast<unsigned long long>(rva));
    out[cap-1] = 0;
    return true;
}
static LONG WINAPI crashReporter(EXCEPTION_POINTERS* const pep) {
    const DWORD code = pep->ExceptionRecord->ExceptionCode;
    if(code != 0xC0000005 && code != 0xC0000374 &&
       code != 0xC0000409 && code != 0x80000003) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    HANDLE hFile = CreateFileA("harness_crash_report.txt",
                               GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    auto out = [&](const char* s) {
        DWORD written = 0;
        WriteFile(hFile, s, lstrlenA(s), &written, nullptr);
        fprintf(stderr, "%s", s);
        fflush(stderr);
    };
    if(hFile == INVALID_HANDLE_VALUE) hFile = GetStdHandle(STD_ERROR_HANDLE);

    char line[128];
    _snprintf(line, sizeof(line)-1,
              "\n=== HARNESS CRASH === code=0x%08lX\n", code);
    line[sizeof(line)-1] = 0; out(line);

    const CONTEXT& ctx = *pep->ContextRecord;
    modOf(reinterpret_cast<const void*>(ctx.Rip), line, sizeof(line));
    out(line); out("   <- RIP\n");

    // scan the raw stack window for values pointing into known modules:
    // likely return addresses (no proper unwind available)
    MEMORY_BASIC_INFORMATION mbi;
    const SIZE_T span = 64 * 1024;
    for(SIZE_T off = 0; off < span; off += sizeof(void*)) {
        const void* sp = reinterpret_cast<const char*>(ctx.Rsp) + off;
        if(VirtualQuery(sp, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        if(mbi.State != MEM_COMMIT ||
           (mbi.Protect & (PAGE_NOACCESS|PAGE_GUARD))) { continue; }
        const void* val = *reinterpret_cast<const void* const*>(sp);
        char sym[128];
        if(modOf(val, sym, sizeof(sym))) {
            char l2[192];
            _snprintf(l2, sizeof(l2)-1,
                      "  [rsp+0x%04IX] %s\n", off, sym);
            l2[sizeof(l2)-1] = 0;
            out(l2);
            if(off > 24 * 1024) break; // enough breadcrumbs
        }
    }
    out("=== END ===\n");
    FlushFileBuffers(hFile);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void ckpt(const char* what) {
    fprintf(stderr, "[ckpt] %s\n", what);
    fflush(stderr);
}

// same in-process dump hook as main.cpp (helps when the failure turns
// out to be an ordinary AV instead of an uncatchable fast-fail)
static LONG WINAPI writeHarnessDump(EXCEPTION_POINTERS* const pep) {
    const QString dir = QCoreApplication::applicationDirPath() +
                        QStringLiteral("/crash_dumps");
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/harness_") +
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

// core part of MainWindow::loadEVFile minus dialogs/layout/render widget
static bool loadProject(Document& document, TaskScheduler& tasks,
                        const QString& path) {
    QFile file(path);
    if(!file.exists() || !file.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "[harness] cannot open %s\n", qPrintable(path));
        fflush(stderr);
        return false;
    }
    const int evVersion = FileFooter::sReadEvFileVersion(&file);
    if(evVersion <= 0 || evVersion > EvFormat::version) {
        fprintf(stderr, "[harness] bad version %d for %s\n",
                evVersion, qPrintable(path));
        fflush(stderr);
        return false;
    }
    eReadStream readStream(evVersion, &file);
    readStream.setPath(path);

    const qint64 savedPos = file.pos();
    const qint64 pos = file.size() - FileFooter::sSize(evVersion) -
            qint64(sizeof(int));
    file.seek(pos);
    readStream.readFutureTable();
    file.seek(savedPos);
    // NOTE: this harness intentionally skips the layout section that
    // the real app reads here - file positions after this point are
    // NOT trustworthy; expect a checkpoint throw when that matters
    readStream.readCheckpoint("File beginning pos mismatch");
    if(evVersion >= EvFormat::betterSWTAbsReadWrite) {
        int nScenes; readStream >> nScenes;
        for(int i = 0; i < nScenes; i++) {
            const bool beforeContent =
                    (evVersion >= EvFormat::readSceneSettingsBeforeContent);
            ckpt("scene created");
            const auto scene = document.createNewScene(!beforeContent);
            if(beforeContent) {
                try {
                    scene->readSettings(readStream);
                    document.sceneCreated(scene);
                } catch(const std::exception& e) {
                    fprintf(stderr, "[harness] scene %d settings threw: %s\n",
                            i, e.what()); fflush(stderr); throw;
                }
            }
        }
        // NOTE: layout section skipped (app-only)
    }
    ckpt("readScenes begin");
    try {
        document.readScenes(readStream);
    } catch(const std::exception& e) {
        fprintf(stderr, "[harness] readScenes threw at pos %lld: %s\n",
                file.pos(), e.what());
        fflush(stderr);
        // treat as a hard failure of this run (matches old behavior)
        throw;
    }
    ckpt("readScenes end");
    file.close();
    Document::sInstance->actionFinished();
    Q_UNUSED(tasks)
    return true;
}

// core part of MainWindow::clearAll
static void clearAllCore(Document& document, TaskScheduler& tasks) {
    tasks.clearTasks();
    document.clear();
}

// bind repro: ImageBox -> bind into a bone (exactly the UI flow), then
// inspect the pixel/render state - blank-canvas investigation
static int runBindTest(Document& document, TaskScheduler& tasks) {
    Q_UNUSED(tasks)
    auto pump = [&document]() {
        for(int i = 0; i < 300; i++) QApplication::processEvents();
    };
    // solid-red test png
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString png = QDir::tempPath() + "/bindtest_red.png";
    if(!img.save(png)) {
        fprintf(stderr, "[harness] BINDTEST FAIL: cannot write png\n");
        return 10;
    }
    const auto scene = document.createNewScene(false);
    const auto bl = enve::make_shared<BoneLayer>();
    scene->addContained(bl);
    const auto bone = enve::make_shared<Bone>();
    bl->addContained(bone);
    const auto box = enve::make_shared<ImageBox>();
    box->setFilePath(png);
    scene->addContained(box);
    box->planUpdate(UpdateReason::userChange);
    // the canvas render chain is not driven headlessly - force the
    // box's render task directly (the export/offscreen path)
    box->queExternalRender(0, false);
    pump();

    const auto report = [&](const char* tag) {
        const auto rd = box->getCurrentRenderData(
                    box->anim_getCurrentRelFrame());
        const auto t = box->getTotalTransform();
        fprintf(stderr, "[harness] %s: loaded=%d name='%s' parent=%s "
                "renderData=%d bounds=%dx%d det=%.3f\n",
                tag, int(box->hasLoadedImage()),
                box->prp_getName().toLocal8Bit().constData(),
                box->getParentGroup() ?
                    box->getParentGroup()->prp_getName()
                        .toLocal8Bit().constData() : "(null)",
                int(rd != nullptr),
                rd ? int(rd->fRelBoundingRect.width()) : -1,
                rd ? int(rd->fRelBoundingRect.height()) : -1,
                t.determinant());
    };
    report("before");
    if(!box->hasLoadedImage()) {
        fprintf(stderr, "[harness] BINDTEST FAIL: image not loaded\n");
        return 11;
    }
    // REAL bind path: selection + bindSelectedLayers, like the UI tool
    scene->clearBoxesSelection();
    scene->addBoxToSelection(box.data());
    bone->bindSelectedLayers();
    box->queExternalRender(0, false);
    pump();
    report("after ");
    const auto rd = box->getCurrentRenderData(
                box->anim_getCurrentRelFrame());
    const bool ok = box->hasLoadedImage() && rd &&
            !rd->fRelBoundingRect.isEmpty();
    fprintf(stderr, "[harness] BINDTEST %s\n", ok ? "PASS" : "FAIL");
    fflush(stderr);
    return ok ? 0 : 12;
}

// synthetic differential test: every layer creates a MotionPathHandler
// in prp_updateCanvasProps(); repeated create/destroy cycles used to be
// lethal with the double-shared PointsHandler ownership
static int runSynthetic(Document& document, TaskScheduler& tasks,
                        const int cycles) {
    for(int c = 0; c < cycles; c++) {
        const auto scene = document.createNewScene(false);
        ckpt("synthetic scene created");
        char buf[64];
        for(int b = 0; b < 6; b++) {
            const auto rect = enve::make_shared<RectangleBox>();
            scene->addContained(rect);
        }
        sprintf(buf, "cycle %d: boxes populated", c);
        ckpt(buf);
        if(c + 1 < cycles) document.removeScene(0);
        else clearAllCore(document, tasks);
        sprintf(buf, "cycle %d: cleared", c);
        ckpt(buf);
    }
    fprintf(stderr, "[harness] SYNTHETIC PASS (%d cycles)\n", cycles);
    fflush(stderr);
    return 0;
}

int main(int argc, char *argv[]) {
    SetUnhandledExceptionFilter(writeHarnessDump);
    cacheModules();
    AddVectoredExceptionHandler(1, crashReporter);
    QApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");

    QStringList args;
    for(int i2 = 1; i2 < argc; i2++) args << QString(argv[i2]);

    if(!args.isEmpty() && args.first() == "--bindtest") {
        eSettings settings(HardwareInfo::sCpuThreads(),
                           HardwareInfo::sRamKB());
        ImportHandler importHandler;
        TaskScheduler taskScheduler;
        Document document(taskScheduler);
        // ImageBox file handlers need the process-wide registry (the
        // app builds it in MainWindow; without it assign() crashes),
        // and the image loader is an eHddTask -> needs MemoryHandler
        FilesHandler filesHandler;
        MemoryHandler memoryHandler;
        return runBindTest(document, taskScheduler);
    }
    if(!args.isEmpty() && args.first() == "--synthetic") {
        const int cycles = args.count() > 1 ? args.at(1).toInt() : 5;
        // Document needs the process-wide settings singleton
        eSettings settings(HardwareInfo::sCpuThreads(),
                           HardwareInfo::sRamKB());
        ImportHandler importHandler;
        TaskScheduler taskScheduler;
        Document document(taskScheduler);
        return runSynthetic(document, taskScheduler, cycles);
    }
    if(args.count() < 2) {
        fprintf(stderr, "usage: friction_harness A.friction B.friction\n");
        return 2;
    }

    eFilterSettings filterSettings;
    ckpt("eFilterSettings");
    ImportHandler importHandler;
    ckpt("ImportHandler");
    // settings singleton: main() stack-allocates it before Document
    eSettings settings(HardwareInfo::sCpuThreads(),
                       HardwareInfo::sRamKB());
    ckpt("eSettings");
    TaskScheduler taskScheduler;
    ckpt("TaskScheduler");

    Document document(taskScheduler);
    ckpt("Document");
    Actions actions(document);
    ckpt("Actions");

    // NOTE: GPU / shader / audio / memory-watchdog init intentionally
    // skipped - the pure core read+clear path is what we need to reproduce

    ckpt("init done");

    if(!loadProject(document, taskScheduler, args.at(0))) return 3;
    ckpt("project A loaded");

    clearAllCore(document, taskScheduler);
    ckpt("cleared after A");

    if(!loadProject(document, taskScheduler, args.at(1))) return 4;
    ckpt("project B loaded");

    clearAllCore(document, taskScheduler);
    ckpt("cleared after B - survived");

    fprintf(stderr, "[harness] PASS: no crash across switch cycles\n");
    fflush(stderr);
    return 0;
}
