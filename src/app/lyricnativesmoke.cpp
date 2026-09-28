// Headless smoke test for the native lyric builder (development tool
// only, never installed): plans a mixed zh/ja LRC through the vendored
// JIZURA planner, builds the native (editable) scene, then renders
// probe frames offscreen and checks structure + coverage.

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <cstdio>
#include <clocale>
#include <functional>

#include "Private/document.h"
#include "Private/esettings.h"
#include "Private/Tasks/taskscheduler.h"
#include "hardwareinfo.h"
#include "efiltersettings.h"
#include "Private/Tasks/offscreenqgl33c.h"
#include "actions.h"
#include "importhandler.h"
#include "canvas.h"
#include "Boxes/containerbox.h"
#include "Boxes/textbox.h"
#include "Boxes/rectangle.h"
#include "Boxes/circle.h"
#include "GUI/lyricmotionengine.h"
#include "GUI/lyricmotionnative.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&,
                              const QString& msg) {
        fprintf(stderr, "[qt] %s\n", msg.toLocal8Bit().constData());
        fflush(stderr);
    });

    eSettings settings(HardwareInfo::sCpuThreads(), HardwareInfo::sRamKB());
    ImportHandler importHandler;
    TaskScheduler taskScheduler;
    Document document(taskScheduler);
    eFilterSettings filterSettings;
    if(eSettings::sInstance) eSettings::sInstance->fPathGpuAcc = false;
    // Canvas::queTasks dereferences Actions::sInstance
    Actions actions(document);

    auto pump = []() {
        for(int j = 0; j < 30; j++) QApplication::processEvents();
    };

    const auto scene = document.createNewScene(false);
    scene->setCanvasSize(480, 270);
    scene->setFps(24);
    pump();

    // --gputest: minimal rect + GLITCH effect render, isolates the
    // software-GL effect pipeline from the lyric builder itself
    // --noanim: skip TextEffect presets (isolates preset expressions
    // from the plain text/shape/keyframe path)
    const bool noAnim = argc > 1 && QString(argv[1]) == "--noanim";
    if(argc > 1 && QString(argv[1]) == "--gputest") {
        const auto rect = enve::make_shared<RectangleBox>();
        rect->setTopLeftPos(QPointF(10, 10));
        rect->setBottomRightPos(QPointF(200, 200));
        scene->addContained(rect);
        const auto eff = createRasterEffectForNonCustomType(
                    RasterEffectType::GLITCH);
        rect->addRasterEffect(eff);
        pump();
        const auto rd = rect->queExternalRender(0, true);
        for(int w = 0; w < 3000; w++) {
            pump();
            taskScheduler.queTasks();
            if(rd && rd->finished()) break;
        }
        fprintf(stderr, "[gputest] finished=%d state=%d img=%d\n",
                int(rd && rd->finished()), rd ? int(rd->getState()) : -1,
                int(rd && rd->fRenderedImage != nullptr));
        fflush(stderr);
        return (rd && rd->finished()) ? 0 : 1;
    }

    LyricMotionEngine engine;
    QString err;
    if(!engine.ensureLoaded(&err)) {
        fprintf(stderr, "[smoke] lyric: engine load failed: %s\n",
                err.toUtf8().constData());
        return 1;
    }
    LyricMotionEngine::Params p;
    p.lyrics = QStringLiteral(
        "[00:01.00]夜明けの色を/覚えてる\n"
        "[00:03.50]*文字* Motion 歌词动画\n"
        "[00:05.50]两行歌词 第二句!\n"
        "[間奏 2]\n"
        "[00:09.50]ラストライン end");
    p.style = QStringLiteral("noir");
    p.seed = 7;
    p.density = 0.55;
    p.chroma = 0.7;
    p.bpm = 120; // beat grid, no audio file
    p.audioDuration = 11.5;
    // --stress: rebuild into the SAME scene for 40 seeds — the
    // apply-again-apply-again flow users hit (old group teardown incl.
    // matte links, then a fresh build every time)
    if(argc > 1 && QString(argv[1]) == "--stress") {
        int fails = 0;
        for(int seed = 1; seed <= 40; seed++) {
            LyricMotionEngine::Params p;
            p.lyrics = QStringLiteral(
                "[00:01.00]夜明けの色を/覚えてる\n"
                "[00:03.50]*文字* Motion 歌词动画\n"
                "[00:05.50]两行歌词 第二句!\n"
                "[間奏 2]\n"
                "[00:09.50]ラストライン end");
            p.style = QStringLiteral("noir");
            p.seed = seed;
            p.density = 0.55;
            p.chroma = 0.7;
            p.bpm = 120;
            p.audioDuration = 11.5;
            const auto json = engine.planJson(p, &err);
            if(json.isEmpty()) { fprintf(stderr, "[stress] seed %d plan fail\n", seed); fails++; continue; }
            const auto doc2 = QJsonDocument::fromJson(json.toUtf8());
            auto plan2 = doc2.object().value(QStringLiteral("plan")).toObject();
            // same headless trim as the main flow
            {
                auto fx2 = plan2.value(QStringLiteral("fx")).toObject();
                fx2.insert(QStringLiteral("chroma"), 0.0);
                fx2.insert(QStringLiteral("texture"), 0.0);
                plan2.insert(QStringLiteral("fx"), fx2);
                plan2.insert(QStringLiteral("events"), QJsonArray());
                auto cuts2 = plan2.value(QStringLiteral("cuts")).toArray();
                for(int i2 = 0; i2 < cuts2.count(); i2++) {
                    auto cut2 = cuts2.at(i2).toObject();
                    if(cut2.value(QStringLiteral("enter")).toString() == QStringLiteral("blur"))
                        cut2.insert(QStringLiteral("enter"), QStringLiteral("wipe"));
                    if(cut2.value(QStringLiteral("exit")).toString() == QStringLiteral("blur"))
                        cut2.insert(QStringLiteral("exit"), QStringLiteral("wipe"));
                    cuts2.replace(i2, cut2);
                }
                plan2.insert(QStringLiteral("cuts"), cuts2);
            }
            LyricMotionNative::Result r2;
            QString e2;
            const bool ok = LyricMotionNative::build(scene, plan2,
                    doc2.object().value(QStringLiteral("fonts")).toObject(),
                    QString(), false, &r2, &e2);
            fprintf(stderr, "[stress] seed %2d ok=%d cuts=%d subs=%d\n",
                    seed, int(ok), r2.cutsBuilt, r2.substitutions);
            fflush(stderr);
            if(!ok) fails++;
            pump();
            // select a deep child (a lyric TextBox) like a canvas click
            // would — the next iteration tears the group down with the
            // canvas selection still pointing at it (crash repro)
            BoundingBox* deepSel = nullptr;
            for(const auto& top : scene->getContainedBoxes()) {
                if(!top->prp_getName().startsWith(
                            QStringLiteral("歌词动画"))) continue;
                if(const auto g = enve::cast<ContainerBox*>(top)) {
                    for(const auto& cut : g->getContainedBoxes()) {
                        if(const auto cg =
                                enve::cast<ContainerBox*>(cut)) {
                            for(const auto& leaf :
                                    cg->getContainedBoxes()) {
                                if(enve::cast<TextBox*>(leaf)) {
                                    deepSel = leaf; break;
                                }
                            }
                        }
                        if(deepSel) break;
                    }
                }
                if(deepSel) break;
            }
            if(deepSel) scene->addBoxToSelection(deepSel);
        }
        fprintf(stderr, "[stress] %s (fails=%d)\n", fails ? "FAIL" : "PASS", fails);
        return fails ? 1 : 0;
    }

    const auto planJson = engine.planJson(p, &err);
    if(planJson.isEmpty()) {
        fprintf(stderr, "[smoke] lyric: plan failed: %s\n",
                err.toUtf8().constData());
        return 1;
    }
    const auto doc = QJsonDocument::fromJson(planJson.toUtf8());
    auto plan = doc.object().value(QStringLiteral("plan")).toObject();
    const auto fonts = doc.object().value(QStringLiteral("fonts")).toObject();
    // headless sanitize: no GL context here, shader-effect tasks would
    // park in the GPU queue forever (see --gputest). The GUI with a
    // real context runs the full effect set; this smoke trims the plan
    // to the CPU-renderable subset: no screen events, no chroma/texture
    // finishing, and part keys whose recipe attaches a raster effect
    // (blur enter/exit, wipe-family transitions, handheld cam) rerouted
    // to effect-free equivalents.
    {
        auto fx = plan.value(QStringLiteral("fx")).toObject();
        fx.insert(QStringLiteral("chroma"), 0.0);
        fx.insert(QStringLiteral("texture"), 0.0);
        plan.insert(QStringLiteral("fx"), fx);
        plan.insert(QStringLiteral("events"), QJsonArray());
        auto cuts = plan.value(QStringLiteral("cuts")).toArray();
        for(int i = 0; i < cuts.count(); i++) {
            auto cut = cuts.at(i).toObject();
            if(cut.value(QStringLiteral("enter")).toString()
                    == QStringLiteral("blur")) {
                cut.insert(QStringLiteral("enter"),
                           QStringLiteral("wipe"));
            }
            if(cut.value(QStringLiteral("exit")).toString()
                    == QStringLiteral("blur")) {
                cut.insert(QStringLiteral("exit"),
                           QStringLiteral("wipe"));
            }
            const QString cam = cut.value(QStringLiteral("cam")).toString();
            if(cam != QStringLiteral("push") && !cam.isEmpty()) {
                cut.insert(QStringLiteral("cam"),
                           QStringLiteral("push"));
            }
            const QString trans = cut.value(QStringLiteral("trans")).toString();
            if(!trans.isEmpty() && trans != QStringLiteral("none")
                    && trans != QStringLiteral("push")
                    && trans != QStringLiteral("cover")) {
                cut.insert(QStringLiteral("trans"),
                           QStringLiteral("push"));
            }
            cuts.replace(i, cut);
        }
        plan.insert(QStringLiteral("cuts"), cuts);
    }
    const int cutCount = plan.value(QStringLiteral("cuts")).toArray().count();
    fprintf(stderr, "[smoke] lyric: plan %d cuts\n", cutCount);
    fflush(stderr);

    LyricMotionNative::Result result;
    QString buildErr;
    if(!LyricMotionNative::build(scene, plan, fonts, QString(), false,
                                 &result, &buildErr)) {
        fprintf(stderr, "[smoke] lyric: build failed: %s\n",
                buildErr.toUtf8().constData());
        return 1;
    }
    if(noAnim) {
        const std::function<void(ContainerBox* const)> strip =
                [&](ContainerBox* const box) {
            for(const auto& child : box->getContainedBoxes()) {
                if(auto* const tb = enve::cast<TextBox*>(child)) {
                    tb->getTextEffects()->clear();
                }
                if(const auto cont = enve::cast<ContainerBox*>(child)) {
                    strip(cont);
                }
            }
        };
        strip(scene->getCurrentGroup());
    }
    fprintf(stderr, "[smoke] lyric: built %d cuts, %d substitutions\n",
            result.cutsBuilt, result.substitutions);
    for(const QString& note : result.notes) {
        fprintf(stderr, "[smoke] lyric note: %s\n",
                note.toUtf8().constData());
    }
    fflush(stderr);

    if(result.cutsBuilt < 3) {
        fprintf(stderr, "[smoke] lyric: FAIL too few cuts built\n");
        return 1;
    }

    int groups = 0, texts = 0, shapes = 0;
    const std::function<void(ContainerBox* const)> walk =
            [&](ContainerBox* const box) {
        for(const auto& child : box->getContainedBoxes()) {
            if(enve::cast<TextBox*>(child)) texts++;
            else if(enve::cast<RectangleBox*>(child)) shapes++;
            else if(enve::cast<Circle*>(child)) shapes++;
            if(const auto cont = enve::cast<ContainerBox*>(child)) {
                groups++;
                walk(cont);
            }
        }
    };
    walk(scene->getCurrentGroup());
    fprintf(stderr, "[smoke] lyric: groups=%d texts=%d shapes=%d "
            "range=%d..%d\n", groups, texts, shapes,
            scene->getFrameRange().fMin, scene->getFrameRange().fMax);
    fflush(stderr);
    if(texts < 3 || groups < 4) {
        fprintf(stderr, "[smoke] lyric: FAIL scene structure too thin\n");
        return 1;
    }

    const auto coverage = [&](const int frame) {
        // render the lyric root group directly: a scene composite does
        // not produce fRenderedImage, a force-rasterized group does
        BoundingBox* rootBox = nullptr;
        for(const auto& box : scene->getContainedBoxes()) {
            if(box->prp_getName().startsWith(QStringLiteral("歌词动画"))) {
                rootBox = box;
                break;
            }
        }
        if(!rootBox) return -1;
        const auto rd = rootBox->queExternalRender(frame, true);
        for(int w = 0; w < 4000; w++) {
            pump();
            taskScheduler.queTasks();
            QApplication::processEvents();
            if(TaskScheduler::sAllQuedCpuTasksFinished() &&
               rd && rd->finished()) break;
        }
        if(!rd || !rd->finished() || !rd->fRenderedImage) {
            fprintf(stderr, "[smoke] lyric: frame %d rd=%d finished=%d img=%d "
                    "state=%d busyCpu=%d\n",
                    frame, int(rd != nullptr), int(rd && rd->finished()),
                    int(rd && rd->fRenderedImage != nullptr),
                    rd ? int(rd->getState()) : -1,
                    int(taskScheduler.busyCpuThreads()));
            return -1;
        }
        const auto raster = rd->fRenderedImage->makeRasterImage();
        SkPixmap pm;
        if(!raster || !raster->peekPixels(&pm)) return -1;
        int count = 0;
        int minX = pm.width(), minY = pm.height(), maxX = -1, maxY = -1;
        const int n = pm.width() * pm.height();
        const auto px = static_cast<const uint32_t*>(pm.addr32());
        for(int i = 0; i < n; i++) {
            if((px[i] >> 24) > 32) {
                count++;
                const int x = i % pm.width();
                const int y = i / pm.width();
                if(x < minX) minX = x;
                if(x > maxX) maxX = x;
                if(y < minY) minY = y;
                if(y > maxY) maxY = y;
            }
        }
        if(count > 0) {
            // content must stay inside the frame with a small margin on
            // every edge (catches the left/top-origin text offset)
            const int mx = qMax(1, pm.width() / 50);
            const int my = qMax(1, pm.height() / 50);
            fprintf(stderr, "[smoke] lyric: frame %d bbox=[%d..%d x %d..%d] "
                    "of %dx%d\n", frame, minX, maxX, minY, maxY,
                    pm.width(), pm.height());
            fflush(stderr);
            if(minX < mx || minY < my || maxX >= pm.width() - mx
                    || maxY >= pm.height() - my) {
                return -2; // content clipped at an edge
            }
        }
        return count;
    };
    int fails = 0;
    for(const int frame : {30, 90, 140, 215}) {
        const int cov = coverage(frame);
        fprintf(stderr, "[smoke] lyric: frame %d coverage=%d\n", frame, cov);
        fflush(stderr);
        if(cov <= 60) fails++; // a lone kinetic word is alive
        if(cov == -2) fails++; // clipped at an edge
    }
    if(fails > 0) {
        fprintf(stderr, "[smoke] lyric: FAIL %d empty probe frames\n",
                fails);
        return 1;
    }
    fprintf(stderr, "[smoke] lyric: PASS\n");
    fflush(stderr);
    return 0;
}
