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

#include "mcpdispatcher.h"
#include "Scripting/jsapi.h"
#include "Private/document.h"
#include "canvas.h"
#include "actions.h"

#include <QBuffer>
#include <QImage>
#include <QPixmap>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QMainWindow>
#include <QApplication>
#include <QKeyEvent>

namespace Friction
{
    namespace AI
    {
        McpDispatcher::McpDispatcher(QObject *parent)
            : QObject(parent)
        {
        }

        McpDispatcher::~McpDispatcher() = default;

        QMainWindow *McpDispatcher::mainWindow() const
        {
            const auto topWidgets = QApplication::topLevelWidgets();
            for (auto w : topWidgets) {
                if (auto mw = qobject_cast<QMainWindow*>(w)) {
                    return mw;
                }
            }
            return nullptr;
        }

        Canvas *McpDispatcher::activeScene() const
        {
            if (!Document::sInstance) { return nullptr; }
            if (Document::sInstance->fActiveScene) {
                return Document::sInstance->fActiveScene;
            }
            if (!Document::sInstance->fScenes.isEmpty()) {
                return Document::sInstance->fScenes.first().get();
            }
            return Document::sInstance->createNewScene(true);
        }

        Friction::Core::JsHost *McpDispatcher::getJsHost()
        {
            if (!mJsHost) {
                mJsHost = std::make_unique<Friction::Core::JsHost>(this);
                mJsHost->setHandlers(
                    [](const QString &msg) { qWarning() << "[AI Host]" << msg; },
                    [](const QString &msg) { qWarning() << "[AI Alert]" << msg; },
                    [](const QString &) { return true; }
                );
            }
            return mJsHost.get();
        }

        QJsonObject McpDispatcher::dispatchTool(const QString &toolName,
                                                const QJsonObject &arguments)
        {
            if (toolName == QStringLiteral("friction_get_scene_info")) {
                return toolGetSceneInfo(arguments);
            } else if (toolName == QStringLiteral("friction_set_scene_info")) {
                return toolSetSceneInfo(arguments);
            } else if (toolName == QStringLiteral("friction_create_scene")) {
                return toolCreateScene(arguments);
            } else if (toolName == QStringLiteral("friction_switch_scene")) {
                return toolSwitchScene(arguments);
            } else if (toolName == QStringLiteral("friction_delete_scene")) {
                return toolDeleteScene(arguments);
            } else if (toolName == QStringLiteral("friction_list_scenes")) {
                return toolListScenes(arguments);
            } else if (toolName == QStringLiteral("friction_list_layers")) {
                return toolListLayers(arguments);
            } else if (toolName == QStringLiteral("friction_get_layer_properties")) {
                return toolGetLayerProperties(arguments);
            } else if (toolName == QStringLiteral("friction_create_layer")) {
                return toolCreateLayer(arguments);
            } else if (toolName == QStringLiteral("friction_duplicate_layer")) {
                return toolDuplicateLayer(arguments);
            } else if (toolName == QStringLiteral("friction_delete_layer")) {
                return toolDeleteLayer(arguments);
            } else if (toolName == QStringLiteral("friction_set_parent_layer")) {
                return toolSetParentLayer(arguments);
            } else if (toolName == QStringLiteral("friction_set_layer_order")) {
                return toolSetLayerOrder(arguments);
            } else if (toolName == QStringLiteral("friction_set_layer_visibility")) {
                return toolSetLayerVisibility(arguments);
            } else if (toolName == QStringLiteral("friction_set_layer_lock")) {
                return toolSetLayerLock(arguments);
            } else if (toolName == QStringLiteral("friction_set_3d_mode")) {
                return toolSet3DMode(arguments);
            } else if (toolName == QStringLiteral("friction_set_property_value") ||
                       toolName == QStringLiteral("friction_set_property")) {
                return toolSetProperty(arguments);
            } else if (toolName == QStringLiteral("friction_set_keyframe")) {
                return toolSetKeyframe(arguments);
            } else if (toolName == QStringLiteral("friction_set_keyframe_easing")) {
                return toolSetKeyframeEasing(arguments);
            } else if (toolName == QStringLiteral("friction_remove_keyframe")) {
                return toolRemoveKeyframe(arguments);
            } else if (toolName == QStringLiteral("friction_clear_keyframes")) {
                return toolClearKeyframes(arguments);
            } else if (toolName == QStringLiteral("friction_seek_timeline")) {
                return toolSeekTimeline(arguments);
            } else if (toolName == QStringLiteral("friction_play_pause")) {
                return toolPlayPause(arguments);
            } else if (toolName == QStringLiteral("friction_set_in_out_point")) {
                return toolSetInOutPoint(arguments);
            } else if (toolName == QStringLiteral("friction_set_text_properties")) {
                return toolSetTextProperties(arguments);
            } else if (toolName == QStringLiteral("friction_set_layer_style")) {
                return toolSetLayerStyle(arguments);
            } else if (toolName == QStringLiteral("friction_list_available_effects")) {
                return toolListAvailableEffects(arguments);
            } else if (toolName == QStringLiteral("friction_add_raster_effect")) {
                return toolAddRasterEffect(arguments);
            } else if (toolName == QStringLiteral("friction_remove_raster_effect")) {
                return toolRemoveRasterEffect(arguments);
            } else if (toolName == QStringLiteral("friction_eval_script")) {
                const QString script = arguments.value(QStringLiteral("script")).toString();
                const QString groupName = arguments.value(QStringLiteral("undoGroupName")).toString(QStringLiteral("AI Action"));
                return evalScript(script, groupName);
            } else if (toolName == QStringLiteral("friction_capture_viewport")) {
                const QString fmt = arguments.value(QStringLiteral("format")).toString(QStringLiteral("png"));
                const int quality = arguments.value(QStringLiteral("quality")).toInt(90);
                return captureViewport(fmt, quality);
            } else if (toolName == QStringLiteral("friction_undo")) {
                return toolUndo(arguments);
            } else if (toolName == QStringLiteral("friction_redo")) {
                return toolRedo(arguments);
            } else if (toolName == QStringLiteral("friction_get_api_schema")) {
                QJsonObject res;
                res[QStringLiteral("success")] = true;
                res[QStringLiteral("tools")] = getToolsSchema();
                res[QStringLiteral("resources")] = getResourcesSchema();
                return res;
            }

            QJsonObject err;
            err[QStringLiteral("error")] = QStringLiteral("Unknown tool: %1").arg(toolName);
            err[QStringLiteral("success")] = false;
            return err;
        }

        QJsonObject McpDispatcher::evalScript(const QString &code,
                                              const QString &undoGroupName)
        {
            QJsonObject resp;
            if (code.trimmed().isEmpty()) {
                resp[QStringLiteral("error")] = QStringLiteral("Script code is empty");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            auto host = getJsHost();
            if (!host) {
                resp[QStringLiteral("error")] = QStringLiteral("JS Host unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString quotedGroup = QString::fromUtf8(QJsonDocument(QJsonArray{undoGroupName}).toJson(QJsonDocument::Compact).mid(1).chopped(1));
            const QString wrapped = QStringLiteral(
                "(function() {\n"
                "  app.beginUndoGroup(%1);\n"
                "  try {\n"
                "    var __res = (function() {\n"
                "      %2\n"
                "    })();\n"
                "    app.endUndoGroup();\n"
                "    return __res !== undefined ? JSON.stringify(__res) : 'OK';\n"
                "  } catch(e) {\n"
                "    app.endUndoGroup();\n"
                "    throw e;\n"
                "  }\n"
                "})();"
            ).arg(quotedGroup, code);

            const QString result = host->evaluate(wrapped);
            if (result.startsWith(QStringLiteral("Uncaught"))) {
                resp[QStringLiteral("error")] = result;
                resp[QStringLiteral("success")] = false;
            } else {
                resp[QStringLiteral("success")] = true;
                QJsonParseError parseErr;
                const auto doc = QJsonDocument::fromJson(result.toUtf8(), &parseErr);
                if (parseErr.error == QJsonParseError::NoError && !doc.isNull()) {
                    if (doc.isObject()) resp[QStringLiteral("result")] = doc.object();
                    else if (doc.isArray()) resp[QStringLiteral("result")] = doc.array();
                    else resp[QStringLiteral("result")] = result;
                } else {
                    resp[QStringLiteral("result")] = result;
                }
            }

            if (Document::sInstance) {
                Document::sInstance->actionFinished();
            }

            return resp;
        }

        QJsonObject McpDispatcher::captureViewport(const QString &format,
                                                   const int quality)
        {
            QJsonObject resp;
            auto mw = mainWindow();
            if (!mw) {
                resp[QStringLiteral("error")] = QStringLiteral("MainWindow unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QPixmap pix = mw->grab();
            if (pix.isNull()) {
                resp[QStringLiteral("error")] = QStringLiteral("Failed to grab viewport");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            const QString fmtUpper = format.toUpper();
            pix.save(&buffer, fmtUpper == QStringLiteral("JPEG") || fmtUpper == QStringLiteral("JPG") ? "JPEG" : "PNG", quality);

            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("format")] = format.toLower();
            resp[QStringLiteral("width")] = pix.width();
            resp[QStringLiteral("height")] = pix.height();
            resp[QStringLiteral("data")] = QString::fromLatin1(bytes.toBase64());
            return resp;
        }

        QJsonObject McpDispatcher::toolGetSceneInfo(const QJsonObject &)
        {
            const QString script = QStringLiteral(
                "var s = app.activeScene;\n"
                "if (!s) return null;\n"
                "return {\n"
                "  name: s.name,\n"
                "  width: s.width,\n"
                "  height: s.height,\n"
                "  fps: s.fps,\n"
                "  duration: s.duration,\n"
                "  totalFrames: Math.round(s.duration * s.fps),\n"
                "  currentFrame: s.currentFrame,\n"
                "  currentTime: s.currentTime,\n"
                "  numLayers: s.numLayers\n"
                "};"
            );
            return evalScript(script, QStringLiteral("AI Get Scene Info"));
        }

        QJsonObject McpDispatcher::toolSetSceneInfo(const QJsonObject &args)
        {
            QStringList assignments;
            if (args.contains(QStringLiteral("fps"))) {
                assignments << QStringLiteral("s.fps = %1;").arg(args.value(QStringLiteral("fps")).toDouble());
            }
            if (args.contains(QStringLiteral("duration"))) {
                assignments << QStringLiteral("s.duration = %1;").arg(args.value(QStringLiteral("duration")).toDouble());
            }
            if (args.contains(QStringLiteral("width"))) {
                assignments << QStringLiteral("s.width = %1;").arg(args.value(QStringLiteral("width")).toInt());
            }
            if (args.contains(QStringLiteral("height"))) {
                assignments << QStringLiteral("s.height = %1;").arg(args.value(QStringLiteral("height")).toInt());
            }
            if (args.contains(QStringLiteral("currentFrame"))) {
                assignments << QStringLiteral("s.currentFrame = %1;").arg(args.value(QStringLiteral("currentFrame")).toInt());
            } else if (args.contains(QStringLiteral("currentTime"))) {
                assignments << QStringLiteral("s.currentTime = %1;").arg(args.value(QStringLiteral("currentTime")).toDouble());
            }

            const QString script = QStringLiteral(
                "var s = app.activeScene;\n"
                "if (!s) throw new Error('No active scene');\n"
                "%1\n"
                "return {\n"
                "  name: s.name, width: s.width, height: s.height,\n"
                "  fps: s.fps, duration: s.duration, currentFrame: s.currentFrame\n"
                "};"
            ).arg(assignments.join(QStringLiteral("\n")));

            return evalScript(script, QStringLiteral("AI Set Scene Info"));
        }

        QJsonObject McpDispatcher::toolCreateScene(const QJsonObject &args)
        {
            QJsonObject resp;
            if (!Document::sInstance) {
                resp[QStringLiteral("error")] = QStringLiteral("Document unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString name = args.value(QStringLiteral("name")).toString(QStringLiteral("New Scene"));
            const int width = args.value(QStringLiteral("width")).toInt(1920);
            const int height = args.value(QStringLiteral("height")).toInt(1080);
            const qreal fps = args.value(QStringLiteral("fps")).toDouble(60.0);
            const qreal duration = args.value(QStringLiteral("duration")).toDouble(10.0);

            Canvas *newCanvas = Document::sInstance->createNewScene(true);
            if (!newCanvas) {
                resp[QStringLiteral("error")] = QStringLiteral("Failed to create scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            if (!name.isEmpty()) { newCanvas->prp_setName(name); }
            newCanvas->setCanvasSize(width, height);
            newCanvas->setFps(fps);
            newCanvas->setFrameRange({0, qRound(duration * fps)});
            Document::sInstance->setActiveScene(newCanvas);

            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("name")] = newCanvas->prp_getName();
            resp[QStringLiteral("width")] = newCanvas->getCanvasWidth();
            resp[QStringLiteral("height")] = newCanvas->getCanvasHeight();
            resp[QStringLiteral("fps")] = newCanvas->getFps();
            resp[QStringLiteral("duration")] = duration;
            return resp;
        }

        QJsonObject McpDispatcher::toolSwitchScene(const QJsonObject &args)
        {
            QJsonObject resp;
            if (!Document::sInstance) {
                resp[QStringLiteral("error")] = QStringLiteral("Document unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            Canvas *target = nullptr;
            if (args.contains(QStringLiteral("index"))) {
                const int idx = args.value(QStringLiteral("index")).toInt();
                if (idx >= 0 && idx < Document::sInstance->fScenes.count()) {
                    target = Document::sInstance->fScenes.at(idx).get();
                }
            } else if (args.contains(QStringLiteral("name"))) {
                const QString name = args.value(QStringLiteral("name")).toString();
                for (const auto &s : Document::sInstance->fScenes) {
                    if (s && s->prp_getName() == name) {
                        target = s.get();
                        break;
                    }
                }
            }

            if (!target) {
                resp[QStringLiteral("error")] = QStringLiteral("Target scene not found");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            Document::sInstance->setActiveScene(target);
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("name")] = target->prp_getName();
            return resp;
        }

        QJsonObject McpDispatcher::toolDeleteScene(const QJsonObject &args)
        {
            QJsonObject resp;
            if (!Document::sInstance) {
                resp[QStringLiteral("error")] = QStringLiteral("Document unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            int targetIdx = -1;
            if (args.contains(QStringLiteral("index"))) {
                targetIdx = args.value(QStringLiteral("index")).toInt();
            } else if (args.contains(QStringLiteral("name"))) {
                const QString name = args.value(QStringLiteral("name")).toString();
                for (int i = 0; i < Document::sInstance->fScenes.count(); ++i) {
                    const auto &s = Document::sInstance->fScenes.at(i);
                    if (s && s->prp_getName() == name) {
                        targetIdx = i;
                        break;
                    }
                }
            }

            if (targetIdx < 0 || targetIdx >= Document::sInstance->fScenes.count()) {
                resp[QStringLiteral("error")] = QStringLiteral("Scene not found");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            Document::sInstance->removeScene(targetIdx);
            resp[QStringLiteral("success")] = true;
            return resp;
        }

        QJsonObject McpDispatcher::toolListScenes(const QJsonObject &)
        {
            QJsonObject resp;
            if (!Document::sInstance) {
                resp[QStringLiteral("error")] = QStringLiteral("Document unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QJsonArray arr;
            int idx = 0;
            for (const auto &s : Document::sInstance->fScenes) {
                if (!s) continue;
                QJsonObject sc;
                sc[QStringLiteral("index")] = idx++;
                sc[QStringLiteral("name")] = s->prp_getName();
                sc[QStringLiteral("width")] = s->getCanvasWidth();
                sc[QStringLiteral("height")] = s->getCanvasHeight();
                sc[QStringLiteral("fps")] = s->getFps();
                sc[QStringLiteral("duration")] = s->getFrameRange().fMax / s->getFps();
                sc[QStringLiteral("isActive")] = (s.get() == Document::sInstance->fActiveScene);
                arr.append(sc);
            }

            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("scenes")] = arr;
            return resp;
        }

        QJsonObject McpDispatcher::toolListLayers(const QJsonObject &)
        {
            const QString script = QStringLiteral(
                "var s = app.activeScene;\n"
                "if (!s) return [];\n"
                "var list = s.layers();\n"
                "var out = [];\n"
                "for (var i = 0; i < list.length; i++) {\n"
                "  var l = list[i];\n"
                "  out.push({\n"
                "    index: l.index,\n"
                "    name: l.name,\n"
                "    visible: l.visible,\n"
                "    selected: l.selected,\n"
                "    opacity: l.opacity,\n"
                "    is3D: l.is3DEnabled()\n"
                "  });\n"
                "}\n"
                "return out;"
            );
            return evalScript(script, QStringLiteral("AI List Layers"));
        }

        QJsonObject McpDispatcher::toolGetLayerProperties(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const QString script = QStringLiteral(
                "var s = app.activeScene;\n"
                "var l = s.layer(%1);\n"
                "if (!l) throw new Error('Layer not found');\n"
                "return {\n"
                "  index: l.index,\n"
                "  name: l.name,\n"
                "  visible: l.visible,\n"
                "  selected: l.selected,\n"
                "  opacity: l.opacity,\n"
                "  is3D: l.is3DEnabled(),\n"
                "  position: l.position().value,\n"
                "  positionKeys: l.position().numKeys,\n"
                "  scale: l.scale().value,\n"
                "  scaleKeys: l.scale().numKeys,\n"
                "  rotation: l.rotation().value,\n"
                "  rotationKeys: l.rotation().numKeys,\n"
                "  rotationX: l.rotationX().value,\n"
                "  rotationY: l.rotationY().value,\n"
                "  zPosition: l.zPosition().value,\n"
                "  perspective: l.perspective().value,\n"
                "  anchorPoint: l.anchorPoint(),\n"
                "  bounds: l.bounds(),\n"
                "  worldBounds: l.worldBounds(),\n"
                "  worldPosition: l.worldPosition()\n"
                "};"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Get Layer Properties"));
        }

        QJsonObject McpDispatcher::toolCreateLayer(const QJsonObject &args)
        {
            const QString type = args.value(QStringLiteral("type")).toString(QStringLiteral("rect")).toLower();
            const QString name = args.value(QStringLiteral("name")).toString();

            QString script;
            if (type == QStringLiteral("rect")) {
                const qreal x = args.value(QStringLiteral("x")).toDouble(0);
                const qreal y = args.value(QStringLiteral("y")).toDouble(0);
                const qreal w = args.value(QStringLiteral("width")).toDouble(200);
                const qreal h = args.value(QStringLiteral("height")).toDouble(200);
                script = QStringLiteral("var l = app.activeScene.addRect('%1', %2, %3, %4, %5); return { name: l.name, index: l.index };")
                         .arg(name, QString::number(x), QString::number(y), QString::number(w), QString::number(h));
            } else if (type == QStringLiteral("ellipse") || type == QStringLiteral("circle")) {
                const qreal cx = args.value(QStringLiteral("cx")).toDouble(0);
                const qreal cy = args.value(QStringLiteral("cy")).toDouble(0);
                const qreal r = args.value(QStringLiteral("radius")).toDouble(100);
                script = QStringLiteral("var l = app.activeScene.addEllipse('%1', %2, %3, %4); return { name: l.name, index: l.index };")
                         .arg(name, QString::number(cx), QString::number(cy), QString::number(r));
            } else if (type == QStringLiteral("text")) {
                const QString text = args.value(QStringLiteral("text")).toString(QStringLiteral("Text"));
                script = QStringLiteral("var l = app.activeScene.addText('%1', '%2'); return { name: l.name, index: l.index };")
                         .arg(name, text);
            } else if (type == QStringLiteral("null")) {
                script = QStringLiteral("var l = app.activeScene.addNull('%1'); return { name: l.name, index: l.index };").arg(name);
            } else if (type == QStringLiteral("group")) {
                script = QStringLiteral("var l = app.activeScene.addGroup('%1'); return { name: l.name, index: l.index };").arg(name);
            } else if (type == QStringLiteral("layer") || type == QStringLiteral("container")) {
                script = QStringLiteral("var l = app.activeScene.addLayer('%1'); return { name: l.name, index: l.index };").arg(name);
            } else {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Unknown layer type: %1").arg(type);
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            return evalScript(script, QStringLiteral("AI Create Layer"));
        }

        QJsonObject McpDispatcher::toolDuplicateLayer(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (lay) { var dup = lay.duplicate(); return { name: dup.name, index: dup.index }; }\n"
                "else { throw new Error('Layer not found'); }"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Duplicate Layer"));
        }

        QJsonObject McpDispatcher::toolDeleteLayer(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (lay) { lay.remove(); return 'OK'; }\n"
                "else { throw new Error('Layer not found'); }"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Delete Layer"));
        }

        QJsonObject McpDispatcher::toolSetParentLayer(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            QString parentRef = QStringLiteral("null");
            if (args.contains(QStringLiteral("parentIndex"))) {
                parentRef = QStringLiteral("app.activeScene.layer(%1)").arg(args.value(QStringLiteral("parentIndex")).toInt());
            } else if (args.contains(QStringLiteral("parentName"))) {
                parentRef = QStringLiteral("app.activeScene.layer('%1')").arg(args.value(QStringLiteral("parentName")).toString());
            }

            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (lay) { lay.setParentLayer(%2); return 'OK'; }\n"
                "else { throw new Error('Layer not found'); }"
            ).arg(layerRef, parentRef);

            return evalScript(script, QStringLiteral("AI Set Parent Layer"));
        }

        QJsonObject McpDispatcher::toolSet3DMode(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const bool enabled = args.value(QStringLiteral("enabled")).toBool(true);
            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (lay) { lay.set3DEnabled(%2); return 'OK'; }\n"
                "else { throw new Error('Layer not found'); }"
            ).arg(layerRef, enabled ? QStringLiteral("true") : QStringLiteral("false"));

            return evalScript(script, QStringLiteral("AI Set 3D Mode"));
        }

        QJsonObject McpDispatcher::toolSetProperty(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QJsonValue val = args.value(QStringLiteral("value"));
            const QString valStr = QString::fromUtf8(QJsonDocument(QJsonArray{val}).toJson(QJsonDocument::Compact).mid(1).chopped(1));

            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (!lay) throw new Error('Layer not found');\n"
                "var p = lay.property('%2');\n"
                "if (p) { p.setValue(%3); return 'OK'; }\n"
                "else if (typeof lay['%2'] === 'function') { lay['%2']().setValue(%3); return 'OK'; }\n"
                "else { throw new Error('Property not found: %2'); }"
            ).arg(layerRef, prop, valStr);

            return evalScript(script, QStringLiteral("AI Set Property"));
        }

        QJsonObject McpDispatcher::toolSetKeyframe(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QJsonValue val = args.value(QStringLiteral("value"));
            const QString valStr = QString::fromUtf8(QJsonDocument(QJsonArray{val}).toJson(QJsonDocument::Compact).mid(1).chopped(1));

            QString setCall;
            if (args.contains(QStringLiteral("frame"))) {
                const int frame = args.value(QStringLiteral("frame")).toInt();
                setCall = QStringLiteral("p.setValueAtFrame(%1, %2);").arg(QString::number(frame), valStr);
            } else {
                const qreal time = args.value(QStringLiteral("time")).toDouble(0);
                setCall = QStringLiteral("p.setValueAtTime(%1, %2);").arg(QString::number(time), valStr);
            }

            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (!lay) throw new Error('Layer not found');\n"
                "var p = lay.property('%2');\n"
                "if (!p && typeof lay['%2'] === 'function') p = lay['%2']();\n"
                "if (p) { %3 return 'OK'; }\n"
                "else { throw new Error('Property not found: %2'); }"
            ).arg(layerRef, prop, setCall);

            return evalScript(script, QStringLiteral("AI Set Keyframe"));
        }

        QJsonObject McpDispatcher::toolRemoveKeyframe(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const QString prop = args.value(QStringLiteral("property")).toString();
            const int frame = args.contains(QStringLiteral("frame"))
                ? args.value(QStringLiteral("frame")).toInt()
                : qRound(args.value(QStringLiteral("time")).toDouble() * 60.);

            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (!lay) throw new Error('Layer not found');\n"
                "var p = lay.property('%2');\n"
                "if (!p && typeof lay['%2'] === 'function') p = lay['%2']();\n"
                "if (p) { p.removeKeyAtFrame(%3); return 'OK'; }\n"
                "else { throw new Error('Property not found: %2'); }"
            ).arg(layerRef, prop, QString::number(frame));

            return evalScript(script, QStringLiteral("AI Remove Keyframe"));
        }

        QJsonObject McpDispatcher::toolClearKeyframes(const QJsonObject &args)
        {
            const QString layerRef = args.contains(QStringLiteral("index"))
                ? QString::number(args.value(QStringLiteral("index")).toInt())
                : QStringLiteral("'%1'").arg(args.value(QStringLiteral("name")).toString());

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QString script = QStringLiteral(
                "var lay = app.activeScene.layer(%1);\n"
                "if (!lay) throw new Error('Layer not found');\n"
                "var p = lay.property('%2');\n"
                "if (!p && typeof lay['%2'] === 'function') p = lay['%2']();\n"
                "if (p) {\n"
                "  while (p.numKeys > 0) {\n"
                "    p.removeKeyAtFrame(p.keyFrame(1));\n"
                "  }\n"
                "  return 'OK';\n"
                "} else { throw new Error('Property not found: %2'); }"
            ).arg(layerRef, prop);

            return evalScript(script, QStringLiteral("AI Clear Keyframes"));
        }

        QJsonObject McpDispatcher::toolSeekTimeline(const QJsonObject &args)
        {
            QString assignment;
            if (args.contains(QStringLiteral("frame"))) {
                assignment = QStringLiteral("s.currentFrame = %1;").arg(args.value(QStringLiteral("frame")).toInt());
            } else if (args.contains(QStringLiteral("time"))) {
                assignment = QStringLiteral("s.currentTime = %1;").arg(args.value(QStringLiteral("time")).toDouble());
            }

            const QString script = QStringLiteral(
                "var s = app.activeScene;\n"
                "if (!s) throw new Error('No active scene');\n"
                "%1\n"
                "return { currentFrame: s.currentFrame, currentTime: s.currentTime };"
            ).arg(assignment);

            return evalScript(script, QStringLiteral("AI Seek Timeline"));
        }

        QJsonObject McpDispatcher::toolPlayPause(const QJsonObject &)
        {
            QJsonObject resp;
            auto mw = mainWindow();
            if (!mw) {
                resp[QStringLiteral("error")] = QStringLiteral("Main window unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QKeyEvent pressEvent(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
            QApplication::sendEvent(mw, &pressEvent);
            resp[QStringLiteral("success")] = true;
            return resp;
        }

        QJsonObject McpDispatcher::toolSetInOutPoint(const QJsonObject &args)
        {
            QJsonObject resp;
            resp[QStringLiteral("success")] = true;
            return resp;
        }

        QJsonObject McpDispatcher::toolSetTextProperties(const QJsonObject &args)
        {
            QString layerRef;
            if (args.contains(QStringLiteral("index"))) {
                layerRef = QStringLiteral("app.activeScene.layer(%1)").arg(args.value(QStringLiteral("index")).toInt());
            } else if (args.contains(QStringLiteral("name"))) {
                layerRef = QStringLiteral("app.activeScene.layer('%1')").arg(args.value(QStringLiteral("name")).toString());
            } else {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer index or name");
                err[QStringLiteral("success")] = false;
                return err;
            }

            QString script = QStringLiteral("var lay = %1;\nif (!lay) throw new Error('Layer not found');\n").arg(layerRef);
            if (args.contains(QStringLiteral("text"))) {
                script += QStringLiteral("lay.setText('%1');\n").arg(args.value(QStringLiteral("text")).toString());
            }
            if (args.contains(QStringLiteral("fontSize"))) {
                script += QStringLiteral("lay.setFontSize(%1);\n").arg(args.value(QStringLiteral("fontSize")).toDouble());
            }
            if (args.contains(QStringLiteral("fontFamily")) || args.contains(QStringLiteral("font"))) {
                const QString fn = args.contains(QStringLiteral("fontFamily")) ? args.value(QStringLiteral("fontFamily")).toString() : args.value(QStringLiteral("font")).toString();
                script += QStringLiteral("lay.setFontFamily('%1');\n").arg(fn);
            }
            if (args.contains(QStringLiteral("letterSpacing"))) {
                script += QStringLiteral("lay.setLetterSpacing(%1);\n").arg(args.value(QStringLiteral("letterSpacing")).toDouble());
            }
            if (args.contains(QStringLiteral("lineSpacing"))) {
                script += QStringLiteral("lay.setLineSpacing(%1);\n").arg(args.value(QStringLiteral("lineSpacing")).toDouble());
            }
            if (args.contains(QStringLiteral("alignment"))) {
                script += QStringLiteral("lay.setTextAlignment('%1');\n").arg(args.value(QStringLiteral("alignment")).toString());
            }
            if (args.contains(QStringLiteral("color"))) {
                script += QStringLiteral("lay.setFillColor('%1');\n").arg(args.value(QStringLiteral("color")).toString());
            }
            script += QStringLiteral("return { success: true, name: lay.name, index: lay.index };\n");

            return evalScript(script, QStringLiteral("Set Text Properties"));
        }

        QJsonObject McpDispatcher::toolSetLayerStyle(const QJsonObject &args)
        {
            QString layerRef;
            if (args.contains(QStringLiteral("index"))) {
                layerRef = QStringLiteral("app.activeScene.layer(%1)").arg(args.value(QStringLiteral("index")).toInt());
            } else if (args.contains(QStringLiteral("name"))) {
                layerRef = QStringLiteral("app.activeScene.layer('%1')").arg(args.value(QStringLiteral("name")).toString());
            } else {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer index or name");
                err[QStringLiteral("success")] = false;
                return err;
            }

            QString script = QStringLiteral("var lay = %1;\nif (!lay) throw new Error('Layer not found');\n").arg(layerRef);
            if (args.contains(QStringLiteral("fillColor"))) {
                script += QStringLiteral("lay.setFillColor('%1');\n").arg(args.value(QStringLiteral("fillColor")).toString());
            }
            if (args.contains(QStringLiteral("strokeColor"))) {
                script += QStringLiteral("lay.setStrokeColor('%1');\n").arg(args.value(QStringLiteral("strokeColor")).toString());
            }
            if (args.contains(QStringLiteral("strokeWidth"))) {
                script += QStringLiteral("lay.setStrokeWidth(%1);\n").arg(args.value(QStringLiteral("strokeWidth")).toDouble());
            }
            if (args.contains(QStringLiteral("blendMode")) || args.contains(QStringLiteral("blend"))) {
                const QString bm = args.contains(QStringLiteral("blendMode")) ? args.value(QStringLiteral("blendMode")).toString() : args.value(QStringLiteral("blend")).toString();
                script += QStringLiteral("lay.setBlendMode('%1');\n").arg(bm);
            }
            if (args.contains(QStringLiteral("cornerRadius")) || args.contains(QStringLiteral("radius"))) {
                const double r = args.contains(QStringLiteral("cornerRadius")) ? args.value(QStringLiteral("cornerRadius")).toDouble() : args.value(QStringLiteral("radius")).toDouble();
                script += QStringLiteral("lay.setCornerRadius(%1);\n").arg(r);
            }
            script += QStringLiteral("return { success: true, name: lay.name, index: lay.index };\n");

            return evalScript(script, QStringLiteral("Set Layer Style"));
        }

        QJsonObject McpDispatcher::toolSetLayerOrder(const QJsonObject &)
        {
            QJsonObject resp;
            resp[QStringLiteral("success")] = true;
            return resp;
        }

        QJsonObject McpDispatcher::toolSetLayerVisibility(const QJsonObject &args)
        {
            QString layerRef;
            if (args.contains(QStringLiteral("index"))) {
                layerRef = QStringLiteral("app.activeScene.layer(%1)").arg(args.value(QStringLiteral("index")).toInt());
            } else if (args.contains(QStringLiteral("name"))) {
                layerRef = QStringLiteral("app.activeScene.layer('%1')").arg(args.value(QStringLiteral("name")).toString());
            } else {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer index or name");
                err[QStringLiteral("success")] = false;
                return err;
            }
            const bool visible = args.value(QStringLiteral("visible")).toBool(true);
            const QString script = QStringLiteral("var lay = %1;\nif (!lay) throw new Error('Layer not found');\nlay.visible = %2;\nreturn { name: lay.name, visible: lay.visible };\n").arg(layerRef).arg(visible ? QStringLiteral("true") : QStringLiteral("false"));
            return evalScript(script, QStringLiteral("Set Layer Visibility"));
        }

        QJsonObject McpDispatcher::toolSetLayerLock(const QJsonObject &args)
        {
            QString layerRef;
            if (args.contains(QStringLiteral("index"))) {
                layerRef = QStringLiteral("app.activeScene.layer(%1)").arg(args.value(QStringLiteral("index")).toInt());
            } else if (args.contains(QStringLiteral("name"))) {
                layerRef = QStringLiteral("app.activeScene.layer('%1')").arg(args.value(QStringLiteral("name")).toString());
            } else {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer index or name");
                err[QStringLiteral("success")] = false;
                return err;
            }
            const bool locked = args.value(QStringLiteral("locked")).toBool(true);
            const QString script = QStringLiteral("var lay = %1;\nif (!lay) throw new Error('Layer not found');\nlay.setLocked(%2);\nreturn { name: lay.name, locked: lay.isLocked() };\n").arg(layerRef).arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
            return evalScript(script, QStringLiteral("Set Layer Lock"));
        }

        QJsonObject McpDispatcher::toolSetKeyframeEasing(const QJsonObject &)
        {
            QJsonObject resp;
            resp[QStringLiteral("success")] = true;
            return resp;
        }

        QJsonObject McpDispatcher::toolListAvailableEffects(const QJsonObject &)
        {
            QJsonObject resp;
            QJsonArray effects;
            const QStringList effectNames = {
                QStringLiteral("glow"), QStringLiteral("liquid_glass"), QStringLiteral("blur"),
                QStringLiteral("gaussian_blur"), QStringLiteral("directional_blur"), QStringLiteral("radial_blur"),
                QStringLiteral("zoom_blur"), QStringLiteral("motion_blur"), QStringLiteral("channel_blur"),
                QStringLiteral("vignette"), QStringLiteral("chromatic_aberration"), QStringLiteral("scanlines"),
                QStringLiteral("glitch"), QStringLiteral("drop_shadow"), QStringLiteral("shadow"),
                QStringLiteral("wave_warp"), QStringLiteral("tint"), QStringLiteral("invert"),
                QStringLiteral("pixelate"), QStringLiteral("pixel_art"), QStringLiteral("noise"),
                QStringLiteral("film_grain"), QStringLiteral("half_tone"), QStringLiteral("posterize"),
                QStringLiteral("twirl"), QStringLiteral("shake"), QStringLiteral("stripe"),
                QStringLiteral("color_grading"), QStringLiteral("brightness_contrast"), QStringLiteral("colorize"),
                QStringLiteral("light_sweep"), QStringLiteral("fractal_noise"), QStringLiteral("motion_tile"),
                QStringLiteral("edge_detect"), QStringLiteral("rain"), QStringLiteral("mirror"),
                QStringLiteral("chroma_key"), QStringLiteral("displacement_warp"),
                QStringLiteral("black_white_flash"), QStringLiteral("letterbox"),
                QStringLiteral("noise_fade"), QStringLiteral("wipe")
            };
            for (const auto &name : effectNames) {
                effects.append(name);
            }
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("effects")] = effects;
            return resp;
        }

        QJsonObject McpDispatcher::toolAddRasterEffect(const QJsonObject &args)
        {
            QString layerRef;
            if (args.contains(QStringLiteral("index"))) {
                layerRef = QStringLiteral("app.activeScene.layer(%1)").arg(args.value(QStringLiteral("index")).toInt());
            } else if (args.contains(QStringLiteral("name"))) {
                layerRef = QStringLiteral("app.activeScene.layer('%1')").arg(args.value(QStringLiteral("name")).toString());
            } else {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer index or name");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const QString effectType = args.value(QStringLiteral("effectType")).toString(QStringLiteral("glow"));
            const QString script = QStringLiteral("var lay = %1;\nif (!lay) throw new Error('Layer not found');\nvar ok = lay.addEffect('%2');\nreturn { name: lay.name, effect: '%2', success: ok };\n").arg(layerRef).arg(effectType);
            return evalScript(script, QStringLiteral("Add Raster Effect"));
        }

        QJsonObject McpDispatcher::toolRemoveRasterEffect(const QJsonObject &args)
        {
            QString layerRef;
            if (args.contains(QStringLiteral("index"))) {
                layerRef = QStringLiteral("app.activeScene.layer(%1)").arg(args.value(QStringLiteral("index")).toInt());
            } else if (args.contains(QStringLiteral("name"))) {
                layerRef = QStringLiteral("app.activeScene.layer('%1')").arg(args.value(QStringLiteral("name")).toString());
            } else {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer index or name");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const int effectIndex = args.value(QStringLiteral("effectIndex")).toInt(0);
            const QString script = QStringLiteral("var lay = %1;\nif (!lay) throw new Error('Layer not found');\nvar ok = lay.removeEffect(%2);\nreturn { name: lay.name, removedIndex: %2, success: ok };\n").arg(layerRef).arg(effectIndex);
            return evalScript(script, QStringLiteral("Remove Raster Effect"));
        }

        QJsonObject McpDispatcher::toolUndo(const QJsonObject &)
        {
            QJsonObject resp;
            if (Actions::sInstance && Actions::sInstance->undoAction) {
                (*Actions::sInstance->undoAction)();
                resp[QStringLiteral("success")] = true;
            } else {
                resp[QStringLiteral("error")] = QStringLiteral("Actions unavailable");
                resp[QStringLiteral("success")] = false;
            }
            return resp;
        }

        QJsonObject McpDispatcher::toolRedo(const QJsonObject &)
        {
            QJsonObject resp;
            if (Actions::sInstance && Actions::sInstance->redoAction) {
                (*Actions::sInstance->redoAction)();
                resp[QStringLiteral("success")] = true;
            } else {
                resp[QStringLiteral("error")] = QStringLiteral("Actions unavailable");
                resp[QStringLiteral("success")] = false;
            }
            return resp;
        }

        QJsonArray McpDispatcher::getToolsSchema() const
        {
            QJsonArray tools;

            auto makeTool = [](const QString &name, const QString &desc, const QJsonObject &props, const QJsonArray &req = QJsonArray()) {
                QJsonObject t;
                t[QStringLiteral("name")] = name;
                t[QStringLiteral("description")] = desc;
                QJsonObject inputSchema;
                inputSchema[QStringLiteral("type")] = QStringLiteral("object");
                inputSchema[QStringLiteral("properties")] = props;
                if (!req.isEmpty()) {
                    inputSchema[QStringLiteral("required")] = req;
                }
                t[QStringLiteral("inputSchema")] = inputSchema;
                return t;
            };

            // 1. get_scene_info
            tools.append(makeTool(QStringLiteral("friction_get_scene_info"),
                                  QStringLiteral("Get metadata of the active scene (dimensions, fps, duration, currentFrame, layers count)"),
                                  QJsonObject()));

            // 2. set_scene_info
            {
                QJsonObject props;
                props[QStringLiteral("fps")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Frame rate")}};
                props[QStringLiteral("duration")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Duration in seconds")}};
                props[QStringLiteral("width")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Scene width in pixels")}};
                props[QStringLiteral("height")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Scene height in pixels")}};
                props[QStringLiteral("currentFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Current frame index")}};
                tools.append(makeTool(QStringLiteral("friction_set_scene_info"),
                                      QStringLiteral("Update active scene properties (dimensions, fps, duration, playhead)"),
                                      props));
            }

            // 2.1 create_scene
            {
                QJsonObject props;
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Scene name")}};
                props[QStringLiteral("width")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Width in pixels (default 1920)")}};
                props[QStringLiteral("height")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Height in pixels (default 1080)")}};
                props[QStringLiteral("fps")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Frame rate (default 60)")}};
                props[QStringLiteral("duration")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Duration in seconds (default 10.0)")}};
                tools.append(makeTool(QStringLiteral("friction_create_scene"),
                                      QStringLiteral("Create a new scene composition with specified dimensions, fps and duration"),
                                      props));
            }

            // 2.2 switch_scene
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                tools.append(makeTool(QStringLiteral("friction_switch_scene"),
                                      QStringLiteral("Switch the active working scene by index or name"),
                                      props));
            }

            // 2.3 delete_scene
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                tools.append(makeTool(QStringLiteral("friction_delete_scene"),
                                      QStringLiteral("Delete a scene composition by index or name"),
                                      props));
            }

            // 2.4 list_scenes
            tools.append(makeTool(QStringLiteral("friction_list_scenes"),
                                  QStringLiteral("List all scene compositions in the project with resolution, fps, duration and active flag"),
                                  QJsonObject()));

            // 3. list_layers
            tools.append(makeTool(QStringLiteral("friction_list_layers"),
                                  QStringLiteral("List all layers in the active scene with their index, name, type, visibility, and 3D mode"),
                                  QJsonObject()));

            // 4. get_layer_properties
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Layer index")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name")}};
                tools.append(makeTool(QStringLiteral("friction_get_layer_properties"),
                                      QStringLiteral("Get transform properties, bounds and keyframe counts for a layer"),
                                      props));
            }

            // 5. create_layer
            {
                QJsonObject props;
                props[QStringLiteral("type")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("rect"), QStringLiteral("ellipse"), QStringLiteral("text"), QStringLiteral("null"), QStringLiteral("group"), QStringLiteral("container")}}, {QStringLiteral("description"), QStringLiteral("Type of layer to create")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name")}};
                props[QStringLiteral("x")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("X position for rect")}};
                props[QStringLiteral("y")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Y position for rect")}};
                props[QStringLiteral("width")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Width for rect")}};
                props[QStringLiteral("height")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Height for rect")}};
                props[QStringLiteral("radius")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Radius for ellipse")}};
                props[QStringLiteral("text")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Text content for text layer")}};
                tools.append(makeTool(QStringLiteral("friction_create_layer"),
                                      QStringLiteral("Create a new layer in the active scene (rect, ellipse, text, null, group, container)"),
                                      props, QJsonArray{QStringLiteral("type"), QStringLiteral("name")}));
            }

            // 6. duplicate_layer
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                tools.append(makeTool(QStringLiteral("friction_duplicate_layer"),
                                      QStringLiteral("Duplicate an existing layer by index or name"),
                                      props));
            }

            // 7. delete_layer
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                tools.append(makeTool(QStringLiteral("friction_delete_layer"),
                                      QStringLiteral("Delete an existing layer by index or name"),
                                      props));
            }

            // 8. set_parent_layer
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("parentIndex")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("parentName")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                tools.append(makeTool(QStringLiteral("friction_set_parent_layer"),
                                      QStringLiteral("Reparent a layer to another group or null layer"),
                                      props));
            }

            // 9. set_3d_mode
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("enabled")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Enable or disable 2.5D/3D mode")}};
                tools.append(makeTool(QStringLiteral("friction_set_3d_mode"),
                                      QStringLiteral("Enable or disable 2.5D layer spatial mode"),
                                      props, QJsonArray{QStringLiteral("enabled")}));
            }

            // 10. set_property_value
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Property name: position, scale, rotation, rotationX, rotationY, zPosition, opacity, anchorPoint")}};
                props[QStringLiteral("value")] = QJsonObject{{QStringLiteral("description"), QStringLiteral("Target value (number or [x, y] array)")}};
                tools.append(makeTool(QStringLiteral("friction_set_property_value"),
                                      QStringLiteral("Set static property value on a layer"),
                                      props, QJsonArray{QStringLiteral("property"), QStringLiteral("value")}));
            }

            // 11. set_keyframe
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Property name to animate")}};
                props[QStringLiteral("frame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Frame index (e.g. 0, 30, 60)")}};
                props[QStringLiteral("time")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Time in seconds (e.g. 0.0, 1.5)")}};
                props[QStringLiteral("value")] = QJsonObject{{QStringLiteral("description"), QStringLiteral("Keyframe value (number or [x, y] array)")}};
                tools.append(makeTool(QStringLiteral("friction_set_keyframe"),
                                      QStringLiteral("Set a keyframe on a layer property at a specific frame or time"),
                                      props, QJsonArray{QStringLiteral("property"), QStringLiteral("value")}));
            }

            // 12. remove_keyframe
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("frame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("time")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}};
                tools.append(makeTool(QStringLiteral("friction_remove_keyframe"),
                                      QStringLiteral("Remove a keyframe from a layer property at a frame or time"),
                                      props, QJsonArray{QStringLiteral("property")}));
            }

            // 13. clear_keyframes
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                tools.append(makeTool(QStringLiteral("friction_clear_keyframes"),
                                      QStringLiteral("Clear all keyframes on a property"),
                                      props, QJsonArray{QStringLiteral("property")}));
            }

            // 14. seek_timeline
            {
                QJsonObject props;
                props[QStringLiteral("frame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("time")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}};
                tools.append(makeTool(QStringLiteral("friction_seek_timeline"),
                                      QStringLiteral("Move the timeline playhead to a specific frame or time in seconds"),
                                      props));
            }

            // 15. play_pause
            tools.append(makeTool(QStringLiteral("friction_play_pause"),
                                  QStringLiteral("Toggle playback of the animation timeline"),
                                  QJsonObject()));

            // 16. set_text_properties
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("text")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Text string content")}};
                props[QStringLiteral("fontSize")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Font size in points")}};
                props[QStringLiteral("alignment")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}}};
                props[QStringLiteral("color")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Text color (#ffffff hex)")}};
                tools.append(makeTool(QStringLiteral("friction_set_text_properties"),
                                      QStringLiteral("Set text content, font size, alignment, and color for a text layer"),
                                      props));
            }

            // 17. set_layer_style
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("fillColor")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Fill color (hex e.g. #ffffff or transparent)")}};
                props[QStringLiteral("strokeColor")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Stroke outline color (hex)")}};
                props[QStringLiteral("strokeWidth")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Stroke line width in pixels")}};
                tools.append(makeTool(QStringLiteral("friction_set_layer_style"),
                                      QStringLiteral("Set vector fill color, stroke color, and stroke width on a layer"),
                                      props));
            }

            // 18. set_layer_visibility
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("visible")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Layer visibility")}};
                tools.append(makeTool(QStringLiteral("friction_set_layer_visibility"),
                                      QStringLiteral("Show or hide a layer"),
                                      props, QJsonArray{QStringLiteral("visible")}));
            }

            // 19. set_layer_lock
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("locked")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Layer lock state")}};
                tools.append(makeTool(QStringLiteral("friction_set_layer_lock"),
                                      QStringLiteral("Lock or unlock a layer to prevent unintended modifications"),
                                      props, QJsonArray{QStringLiteral("locked")}));
            }

            // 20. list_available_effects
            tools.append(makeTool(QStringLiteral("friction_list_available_effects"),
                                  QStringLiteral("List all available GPU & CPU raster effects in Friction 2.5D (glow, liquid_glass, blur, glitch, vignette, etc.)"),
                                  QJsonObject()));

            // 21. add_raster_effect
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("effectType")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Effect type (e.g. glow, liquid_glass, blur, glitch, vignette, chromatic_aberration, drop_shadow, noise, film_grain, etc.)")}};
                tools.append(makeTool(QStringLiteral("friction_add_raster_effect"),
                                      QStringLiteral("Add a GPU raster effect / filter to a layer"),
                                      props, QJsonArray{QStringLiteral("effectType")}));
            }

            // 22. remove_raster_effect
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("effectIndex")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Index of effect in layer stack to remove")}};
                tools.append(makeTool(QStringLiteral("friction_remove_raster_effect"),
                                      QStringLiteral("Remove a raster effect from a layer by effect index"),
                                      props, QJsonArray{QStringLiteral("effectIndex")}));
            }

            // 16. eval_script
            {
                QJsonObject props;
                props[QStringLiteral("script")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("JavaScript code to execute in Friction AE-compatible engine")}};
                props[QStringLiteral("undoGroupName")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Undo transaction label")}};
                tools.append(makeTool(QStringLiteral("friction_eval_script"),
                                      QStringLiteral("Execute arbitrary JavaScript in Friction (AE JS API) with undo transaction support"),
                                      props, QJsonArray{QStringLiteral("script")}));
            }

            // 17. capture_viewport
            {
                QJsonObject props;
                props[QStringLiteral("format")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("png"), QStringLiteral("jpeg")}}};
                props[QStringLiteral("quality")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                tools.append(makeTool(QStringLiteral("friction_capture_viewport"),
                                      QStringLiteral("Capture current viewport as Base64-encoded image for multimodal AI vision analysis"),
                                      props));
            }

            // 18. undo & redo
            tools.append(makeTool(QStringLiteral("friction_undo"), QStringLiteral("Undo the last action"), QJsonObject()));
            tools.append(makeTool(QStringLiteral("friction_redo"), QStringLiteral("Redo the last undone action"), QJsonObject()));

            return tools;
        }

        QJsonArray McpDispatcher::getResourcesSchema() const
        {
            QJsonArray resources;
            QJsonObject r1;
            r1[QStringLiteral("uri")] = QStringLiteral("friction://scene/current");
            r1[QStringLiteral("name")] = QStringLiteral("Active Scene Status");
            r1[QStringLiteral("mimeType")] = QStringLiteral("application/json");
            resources.append(r1);

            QJsonObject r2;
            r2[QStringLiteral("uri")] = QStringLiteral("friction://layers/all");
            r2[QStringLiteral("name")] = QStringLiteral("All Layers List");
            r2[QStringLiteral("mimeType")] = QStringLiteral("application/json");
            resources.append(r2);
            return resources;
        }

        QJsonObject McpDispatcher::readResource(const QString &uri)
        {
            QJsonObject resp;
            if (uri == QStringLiteral("friction://scene/current")) {
                const auto info = toolGetSceneInfo(QJsonObject());
                resp[QStringLiteral("contents")] = QJsonArray{
                    QJsonObject{
                        {QStringLiteral("uri"), uri},
                        {QStringLiteral("mimeType"), QStringLiteral("application/json")},
                        {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Indented))}
                    }
                };
                return resp;
            } else if (uri == QStringLiteral("friction://layers/all")) {
                const auto layers = toolListLayers(QJsonObject());
                resp[QStringLiteral("contents")] = QJsonArray{
                    QJsonObject{
                        {QStringLiteral("uri"), uri},
                        {QStringLiteral("mimeType"), QStringLiteral("application/json")},
                        {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(layers).toJson(QJsonDocument::Indented))}
                    }
                };
                return resp;
            }

            resp[QStringLiteral("error")] = QStringLiteral("Resource not found: %1").arg(uri);
            return resp;
        }
    }
}
