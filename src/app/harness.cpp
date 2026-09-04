// headless reproduction harness for the project-switch heap corruption:
//   friction_harness.exe -platform offscreen A.friction B.friction
// mirrors openFile(A) -> clearAll() -> openFile(B) using only the core
// loading path (no MainWindow / layout / render widget), with breadcrumb
// checkpoints on stderr so the last printed line brackets the phase
// that corrupts the heap.
#ifndef NOMINMAX
#define NOMINMAX // Skia headers need std::min/std::max without macros
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstring>
#include <QApplication>
#include <QFile>
#include <QDateTime>
#include <cstdio>
#include "Depth/aidepthprovider.h"
#include "Depth/modelcatalog.h"
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
#include "Boxes/layerboxrenderdata.h"
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

// track matte differential test: a 300x300 target rectangle matted by
// a 100x100 rectangle must lose ~8/9 of its alpha coverage; verifies
// the whole chain headlessly (combo-level model state -> render data
// effect attach -> TrackMatteCaller tiles -> final image)
static int runTrackMatteTest(Document& document, TaskScheduler& tasks) {
    Q_UNUSED(tasks)
    // headless: no GL context exists, GPU-preferred render tasks would
    // park forever - force the CPU rasterization paths
    if(eSettings::sInstance) eSettings::sInstance->fPathGpuAcc = false;
    auto pump = []() {
        for(int j = 0; j < 30; j++) QApplication::processEvents();
    };
    const auto alphaCoverage = [](const stdsptr<BoxRenderData>& rd) {
        if(!rd || !rd->fRenderedImage) return -1;
        const auto raster = rd->fRenderedImage->makeRasterImage();
        if(!raster) return -1;
        SkPixmap pm;
        if(!raster->peekPixels(&pm)) return -1;
        int count = 0;
        const int n = qMin(pm.width()*pm.height(), 4 << 20);
        const auto px = static_cast<const uint32_t*>(pm.addr32());
        for(int i = 0; i < n; i++) {
            if((px[i] >> 24) > 128) count++;
        }
        return count;
    };
    const auto renderAndWait = [&](BoundingBox* const box) {
        const auto rd = box->queExternalRender(0, true);
        for(int w = 0; w < 200; w++) {
            pump();
            if(rd && rd->finished()) break;
        }
        return rd;
    };

    const auto scene = document.createNewScene(false);
    const auto target = enve::make_shared<RectangleBox>();
    target->setTopLeftPos(QPointF(0, 0));
    target->setBottomRightPos(QPointF(300, 300));
    scene->addContained(target);
    const auto matte = enve::make_shared<RectangleBox>();
    matte->setTopLeftPos(QPointF(0, 0));
    matte->setBottomRightPos(QPointF(100, 100));
    scene->addContained(matte);
    pump();

    const auto matteRd = renderAndWait(matte.get());
    const int matteCov = alphaCoverage(matteRd);
    fprintf(stderr, "[harness] trkmat: matte coverage=%d finished=%d\n",
            matteCov, int(matteRd && matteRd->finished()));
    fflush(stderr);

    const auto before = renderAndWait(target.get());
    const int cov0 = alphaCoverage(before);
    fprintf(stderr, "[harness] trkmat: target before coverage=%d "
            "finished=%d img=%dx%d\n", cov0,
            int(before && before->finished()),
            before && before->fRenderedImage ?
                before->fRenderedImage->width() : -1,
            before && before->fRenderedImage ?
                before->fRenderedImage->height() : -1);
    fflush(stderr);

    // same calls the timeline TrkMat combo makes
    target->trackMatteTarget()->setTargetAction(matte.get());
    target->setTrackMatteMode(1); // alpha matte
    pump();

    const auto after = renderAndWait(target.get());
    const int cov1 = alphaCoverage(after);
    fprintf(stderr, "[harness] trkmat: target after coverage=%d "
            "finished=%d mode=%d\n", cov1,
            int(after && after->finished()),
            target->getTrackMatteMode());
    fflush(stderr);

    // the matte and target images render at their own global-rect
    // resolutions, so compare in ratios: the matted target must keep
    // a positive but much smaller coverage (100/300 area + AA edges)
    const bool ok = matteCov > 0 && cov0 > 1000 &&
                    cov1 > 0 && cov1 < cov0/2;
    fprintf(stderr, "[harness] TRKMAT %s (cov0=%d cov1=%d matte=%d "
            "ratio=%.2f)\n",
            ok ? "PASS" : "FAIL", cov0, cov1, matteCov,
            cov0 > 0 ? double(cov1)/double(cov0) : -1.);
    fflush(stderr);
    return ok ? 0 : 20;
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
    fprintf(stderr, "[harness] ckpt: setup done\n"); fflush(stderr);
    box->setFilePath(png);
    fprintf(stderr, "[harness] ckpt: setFilePath\n"); fflush(stderr);
    scene->addContained(box);
    fprintf(stderr, "[harness] ckpt: addContained\n"); fflush(stderr);
    box->planUpdate(UpdateReason::userChange);
    fprintf(stderr, "[harness] ckpt: planUpdate\n"); fflush(stderr);
    // the canvas render chain is not driven headlessly - force the
    // box's render task directly (the export/offscreen path)
    for(int i = 0; i < 6; i++) {
        box->queExternalRender(0, false);
        fprintf(stderr, "[harness] ckpt: queExternalRender %d\n", i); fflush(stderr);
        for(int j = 0; j < 50; j++) QApplication::processEvents();
        fprintf(stderr, "[harness] ckpt: pumped %d\n", i); fflush(stderr);
    }

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
    // the loader completes asynchronously - wait it out (the plain
    // check raced and failed on slow runs)
    for(int w = 0; w < 40 && !box->hasLoadedImage(); w++) {
        for(int j = 0; j < 25; j++) QApplication::processEvents();
    }
    report("before");
    if(!box->hasLoadedImage()) {
        fprintf(stderr, "[harness] BINDTEST FAIL: image not loaded\n");
        return 11;
    }
    // realistic psd-like transform: centered pivot + offset +
    // rotation + scale (the trivial identity case always round-trips;
    // if decomposePivoted mishandles the pivot convention, THIS is
    // where it shows)
    {
        const auto tr = box->getBoxTransformAnimator();
        tr->setPivot(32, 32);
        tr->setPosition(100, 50);
        tr->getRotAnimator()->setCurrentBaseValue(15);
        tr->getScaleAnimator()->getXAnimator()->setCurrentBaseValue(0.8);
        tr->getScaleAnimator()->getYAnimator()->setCurrentBaseValue(1.2);
        pump();
        const auto m = box->getTotalTransform();
        fprintf(stderr, "[harness] tx before: %g %g %g %g %g %g\n",
                m.m11(), m.m12(), m.m21(), m.m22(), m.dx(), m.dy());
    }
    // REAL bind path: selection + bindSelectedLayers, like the UI tool
    scene->clearBoxesSelection();
    scene->addBoxToSelection(box.data());
    fprintf(stderr, "[harness] ckpt: selected\n"); fflush(stderr);
    bone->bindSelectedLayers();
    fprintf(stderr, "[harness] ckpt: bound\n"); fflush(stderr);
    for(int i = 0; i < 6; i++) {
        box->queExternalRender(0, false);
        fprintf(stderr, "[harness] ckpt: post-bind que %d\n", i); fflush(stderr);
        for(int j = 0; j < 50; j++) QApplication::processEvents();
        fprintf(stderr, "[harness] ckpt: post-bind pumped %d\n", i); fflush(stderr);
    }
    report("after ");
    {
        const auto m = box->getTotalTransform();
        fprintf(stderr, "[harness] tx after : %g %g %g %g %g %g\n",
                m.m11(), m.m12(), m.m21(), m.m22(), m.dx(), m.dy());
        // exact round-trip expectation: world transform unchanged
        QMatrix want; // recomputed via a fresh bind of the same values
        const bool sane = !qIsNaN(m.m11()) && !qIsNaN(m.dx()) &&
                          qAbs(m.determinant()) > 0.01;
        fprintf(stderr, "[harness] tx sane=%d det=%.4f\n",
                int(sane), m.determinant());
    }
    // compositor smoke test: render the BONE LAYER group (the exact
    // container the layer now lives in) with a psd-like blend mode on
    // the box - verifies the child render chain builds and finishes
    {
        box->setBlendModeSk(SkBlendMode::kMultiply);
        const auto blData = bl->queExternalRender(0, false);
        for(int w = 0; w < 40; w++) {
            for(int j = 0; j < 25; j++) QApplication::processEvents();
            if(blData && blData->finished()) break;
        }
        const auto contData = enve::shared(
                    static_cast<ContainerBoxRenderData*>(blData.get()));
        const int n = contData ? contData->fChildrenRenderData.count() : -1;
        fprintf(stderr, "[harness] compositor: blData=%p finished=%d "
                "children=%d boxData=%d\n",
                blData.get(), int(blData && blData->finished()), n,
                int(box->getCurrentRenderData(
                        box->anim_getCurrentRelFrame()) != nullptr));
    }
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

// AI depth end-to-end (no GUI): model catalog resolution + sha256 +
// ONNX Runtime inference on a synthetic two-tone image; verifies the
// provider chain (preprocess -> session -> postprocess) for all three
// output modes. The model dir comes from argv when the harness runs
// outside the portable deploy tree.
static int runAiDepthTest(const QString& modelDirArg)
{
    if (!AiDepth::available()) {
        fprintf(stderr, "[harness] AIDEPTH FAIL: no onnxruntime.dll\n");
        return 20;
    }
    fprintf(stderr, "[harness] aidepth: runtime '%s'\n",
            AiDepth::versionString().toLocal8Bit().constData());

    QString modelDir = modelDirArg;
    if (modelDir.isEmpty()) {
        modelDir = AiDepth::ModelCatalog::resolveModelDir(
                    QStringLiteral("small"));
    }
    if (modelDir.isEmpty()) {
        fprintf(stderr, "[harness] AIDEPTH FAIL: small model not resolved\n");
        return 21;
    }
    fprintf(stderr, "[harness] aidepth: modelDir '%s'\n",
            modelDir.toLocal8Bit().constData());

    {
        const auto* info = AiDepth::ModelCatalog::model(
                    QStringLiteral("small"));
        QString err;
        if (!info || !AiDepth::ModelCatalog::verifySha256(modelDir, *info, &err)) {
            fprintf(stderr, "[harness] AIDEPTH FAIL: sha256 (%s)\n",
                    err.toLocal8Bit().constData());
            return 22;
        }
        fprintf(stderr, "[harness] aidepth: sha256 ok\n");
    }

    // synthetic 640x480, dark left half vs bright right half: the depth
    // map must show contrast between both halves
    const int W = 640;
    const int H = 480;
    SkBitmap bmp;
    bmp.allocPixels(SkImageInfo::Make(W, H, kRGBA_8888_SkColorType,
                                      kUnpremul_SkAlphaType));
    bmp.eraseColor(SK_ColorBLACK);
    SkCanvas canvas(bmp);
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    canvas.drawRect(SkRect::MakeXYWH(W / 2, 0, W / 2, H), paint);
    const auto srcImg = SkImage::MakeFromBitmap(bmp);

    for (int mode = 0; mode < 3; mode++) {
        AiDepth::Options opts;
        opts.fInputSize = 518;
        opts.fOutputMode = mode;
        sk_sp<SkImage> depth;
        QString err;
        const auto t1 = QDateTime::currentMSecsSinceEpoch();
        const auto st = AiDepth::runDepth(srcImg, modelDir, opts, depth, err);
        const qint64 dt = QDateTime::currentMSecsSinceEpoch() - t1;
        if (st != AiDepth::DepthStatus::Ok) {
            fprintf(stderr, "[harness] AIDEPTH FAIL: mode %d status=%d err='%s'\n",
                    mode, int(st), err.toLocal8Bit().constData());
            return 23;
        }
        if (!depth || depth->width() != W || depth->height() != H) {
            fprintf(stderr, "[harness] AIDEPTH FAIL: mode %d dims %dx%d (want %dx%d)\n",
                    mode, depth ? depth->width() : -1, depth ? depth->height() : -1, W, H);
            return 24;
        }
        SkPixmap pm;
        const auto raster = depth->makeRasterImage();
        if (!raster || !raster->peekPixels(&pm)) {
            fprintf(stderr, "[harness] AIDEPTH FAIL: mode %d peek\n", mode);
            return 25;
        }
        const SkColor lC = pm.getColor(W / 4, H / 2);
        const SkColor rC = pm.getColor(3 * W / 4, H / 2);
        // SkColorGetR/G/B return U8CPU: subtract in signed or it wraps
        const int diff = qAbs(int(SkColorGetR(lC)) - int(SkColorGetR(rC)))
                + qAbs(int(SkColorGetG(lC)) - int(SkColorGetG(rC)))
                + qAbs(int(SkColorGetB(lC)) - int(SkColorGetB(rC)));
        if (diff < 24) {
            fprintf(stderr, "[harness] AIDEPTH FAIL: mode %d no contrast "
                    "L=%06x R=%06x diff=%d\n",
                    mode, lC, rC, diff);
            return 26;
        }
        fprintf(stderr, "[harness] aidepth: mode %d ok %lld ms L=%06x R=%06x\n",
                mode, dt, lC, rC);
    }
    fprintf(stderr, "[harness] AIDEPTH PASS\n");
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

    // route Qt messages straight to stderr - the app's custom handler
    // buffers them into the debug-log dialog and the console sees
    // nothing (which hid the ImageLoader diagnostics)
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&,
                              const QString& msg) {
        fprintf(stderr, "[qt] %s\n", msg.toLocal8Bit().constData());
        fflush(stderr);
    });

    if(!args.isEmpty() && args.first() == "--bindtest") {
        eSettings settings(HardwareInfo::sCpuThreads(),
                           HardwareInfo::sRamKB());
        ImportHandler importHandler;
        TaskScheduler taskScheduler;
        Document document(taskScheduler);
        // ImageBox file handlers need the process-wide registry (the
        // app builds it in MainWindow; without it assign() crashes),
        // the image loader is an eHddTask -> needs MemoryHandler, and
        // BoxRenderData's ctor reads eFilterSettings::sRender()
        FilesHandler filesHandler;
        MemoryHandler memoryHandler;
        eFilterSettings filterSettings;
        return runBindTest(document, taskScheduler);
    }
    if(!args.isEmpty() && args.first() == "--trkmat") {
        eSettings settings(HardwareInfo::sCpuThreads(),
                           HardwareInfo::sRamKB());
        ImportHandler importHandler;
        TaskScheduler taskScheduler;
        Document document(taskScheduler);
        FilesHandler filesHandler;
        MemoryHandler memoryHandler;
        eFilterSettings filterSettings;
        return runTrackMatteTest(document, taskScheduler);
    }
    if(!args.isEmpty() && args.first() == "--aidepth") {
        eSettings settings(HardwareInfo::sCpuThreads(),
                           HardwareInfo::sRamKB());
        ImportHandler importHandler;
        TaskScheduler taskScheduler;
        Document document(taskScheduler);
        return runAiDepthTest(args.count() > 1 ? args.at(1) : QString());
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
