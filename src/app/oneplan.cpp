#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdio>
#include "GUI/lyricmotionengine.h"
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    LyricMotionEngine engine; QString err;
    engine.ensureLoaded(&err);
    LyricMotionEngine::Params p;
    p.lyrics = QStringLiteral("[00:01.00]夜明けの色を/覚えてる\n[00:03.50]*文字* Motion 歌词动画\n[00:05.50]两行歌词 第二句!\n[間奏 2]\n[00:09.50]ラストライン end");
    p.style = QStringLiteral("noir"); p.seed = 7; p.density = 0.55;
    p.bpm = 120; p.audioDuration = 11.5;
    const auto json = engine.planJson(p, &err);
    const auto plan = QJsonDocument::fromJson(json.toUtf8())
            .object().value(QStringLiteral("plan")).toObject();
    for(const auto& cv : plan.value(QStringLiteral("cuts")).toArray()) {
        const auto c = cv.toObject();
        printf("%5.1f-%5.1f layout=%-14s enter=%-12s exit=%-10s %s\n",
               c.value("start").toDouble(), c.value("end").toDouble(),
               c.value("layout").toString().toUtf8().constData(),
               c.value("enter").toString().toUtf8().constData(),
               c.value("exit").toString().toUtf8().constData(),
               c.value("text").toString().toUtf8().constData());
    }
    return 0;
}
