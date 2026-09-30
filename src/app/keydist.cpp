// 统计多种子下 plan 的 layout/enter/exit 键分布（复用引擎）
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdio>
#include "GUI/lyricmotionengine.h"
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    LyricMotionEngine engine;
    QString err;
    if(!engine.ensureLoaded(&err)) { fprintf(stderr, "load fail %s\n", err.toUtf8().constData()); return 1; }
    QHash<QString,int> layouts, enters, exits;
    for(int seed = 1; seed <= 40; seed++) {
        LyricMotionEngine::Params p;
        p.lyrics = QStringLiteral("[00:01.00]夜明けの色を/覚えてる\n[00:03.50]*文字* Motion 歌词动画\n[00:05.50]两行歌词 第二句!\n[間奏 2]\n[00:09.50]ラストライン end");
        p.style = QStringLiteral("noir");
        p.seed = seed;
        p.density = 0.55;
        p.bpm = 120;
        p.audioDuration = 11.5;
        const auto json = engine.planJson(p, &err);
        if(json.isEmpty()) continue;
        const auto plan = QJsonDocument::fromJson(json.toUtf8())
                .object().value(QStringLiteral("plan")).toObject();
        for(const auto& cv : plan.value(QStringLiteral("cuts")).toArray()) {
            const auto c = cv.toObject();
            layouts[c.value(QStringLiteral("layout")).toString()]++;
            enters[c.value(QStringLiteral("enter")).toString()]++;
            exits[c.value(QStringLiteral("exit")).toString()]++;
        }
    }
    auto dump = [](const char* t, QHash<QString,int>& h) {
        fprintf(stderr, "=== %s (%d keys) ===\n", t, h.size());
        QList<QPair<int,QString>> v;
        for(auto it = h.begin(); it != h.end(); ++it) v << qMakePair(it.value(), it.key());
        std::sort(v.begin(), v.end(), [](auto&a, auto&b){return a.first>b.first;});
        for(auto& p : v) fprintf(stderr, "  %3d %s\n", p.first, p.second.toUtf8().constData());
    };
    dump("layout", layouts); dump("enter", enters); dump("exit", exits);
    return 0;
}
