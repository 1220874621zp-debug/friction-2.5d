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
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>

namespace Friction
{
    namespace AI
    {
        static QWidget *findCanvasWidget(QMainWindow *mw)
        {
            if (!mw) return nullptr;
            QWidget *grabWidget = mw->findChild<QWidget*>(QStringLiteral("canvasWindow"));
            if (!grabWidget) {
                const auto allWidgets = mw->findChildren<QWidget*>();
                for (auto w : allWidgets) {
                    if (w && w->inherits("CanvasWindow")) {
                        grabWidget = w;
                        break;
                    }
                }
            }
            return grabWidget ? grabWidget : mw;
        }

        static QString findMarkupScriptPath()
        {
            const QString standardPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("friction/tools/mcp/friction_markup.py"));
            if (!standardPath.isEmpty() && QFileInfo::exists(standardPath)) {
                return standardPath;
            }

            const QStringList candidatePaths = {
                QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/mcp/friction_markup.py"),
                QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/mcp/friction_markup.py"),
                QCoreApplication::applicationDirPath() + QStringLiteral("/tools/mcp/friction_markup.py"),
                QCoreApplication::applicationDirPath() + QStringLiteral("/../share/friction/tools/mcp/friction_markup.py"),
                QDir::homePath() + QStringLiteral("/.local/share/friction/tools/mcp/friction_markup.py"),
                QStringLiteral("/usr/share/friction/tools/mcp/friction_markup.py"),
                QStringLiteral("/usr/local/share/friction/tools/mcp/friction_markup.py"),
                QDir::currentPath() + QStringLiteral("/tools/mcp/friction_markup.py")
            };

            for (const auto &path : candidatePaths) {
                if (QFileInfo::exists(path)) {
                    return path;
                }
            }
            return QString();
        }

        static QString escapeJsString(const QString &str)
        {
            QString res = str;
            res.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            res.replace(QLatin1Char('\''), QStringLiteral("\\'"));
            res.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            res.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
            return res;
        }

        static QString parseLayerRef(const QJsonObject &args, const QString &prefix = QString(), bool *ok = nullptr)
        {
            if (ok) *ok = true;
            const QString idxKey = prefix.isEmpty() ? QStringLiteral("index") : prefix + QStringLiteral("Index");
            const QString nameKey = prefix.isEmpty() ? QStringLiteral("name") : prefix + QStringLiteral("Name");
            const QString layerKey = prefix.isEmpty() ? QStringLiteral("layer") : prefix;
            const QString layerNameKey = prefix.isEmpty() ? QStringLiteral("layerName") : prefix + QStringLiteral("Layer");

            if (args.contains(idxKey)) {
                return QString::number(args.value(idxKey).toInt());
            }
            if (args.contains(nameKey)) {
                return QStringLiteral("'%1'").arg(escapeJsString(args.value(nameKey).toString()));
            }
            if (args.contains(layerKey)) {
                const auto val = args.value(layerKey);
                if (val.isDouble()) {
                    return QString::number(val.toInt());
                }
                if (val.isString() && !val.toString().isEmpty()) {
                    return QStringLiteral("'%1'").arg(escapeJsString(val.toString()));
                }
            }
            if (args.contains(layerNameKey)) {
                const auto val = args.value(layerNameKey);
                if (val.isDouble()) {
                    return QString::number(val.toInt());
                }
                if (val.isString() && !val.toString().isEmpty()) {
                    return QStringLiteral("'%1'").arg(escapeJsString(val.toString()));
                }
            }
            if (ok) *ok = false;
            return QStringLiteral("''");
        }

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
            } else if (toolName == QStringLiteral("friction_render_markup")) {
                return toolRenderMarkup(arguments);
            } else if (toolName == QStringLiteral("friction_update_layer")) {
                return toolUpdateLayer(arguments);
            } else if (toolName == QStringLiteral("friction_animate_layer")) {
                return toolAnimateLayer(arguments);
            } else if (toolName == QStringLiteral("friction_get_storyboard")) {
                return toolGetStoryboard(arguments);
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
                "  function __findLayer(ref) {\n"
                "    var s = app.activeScene;\n"
                "    if (!s) throw new Error('No active scene');\n"
                "    var l = s.layer(ref);\n"
                "    if (!l) {\n"
                "      var all = s.layers(), names = [];\n"
                "      for (var i = 0; i < all.length; i++) names.push(all[i].index + \": '\" + all[i].name + \"'\");\n"
                "      throw new Error('Layer not found: ' + ref + '. Available layers in scene: [' + names.join(', ') + ']');\n"
                "    }\n"
                "    return l;\n"
                "  }\n"
                "  function __findProp(lay, propName) {\n"
                "    var p = lay.property(propName);\n"
                "    if (!p && typeof lay[propName] === 'function') p = lay[propName]();\n"
                "    if (!p) {\n"
                "      throw new Error(\"Property '\" + propName + \"' not found on layer '\" + lay.name + \"'. Available properties: [position, scale, rotation, rotationX, rotationY, zPosition, perspective, opacity, anchorPoint, bounds, worldPosition]\");\n"
                "    }\n"
                "    return p;\n"
                "  }\n"
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

            QWidget *grabWidget = findCanvasWidget(mw);
            QPixmap pix = grabWidget ? grabWidget->grab() : mw->grab();
            if (pix.isNull()) {
                pix = mw->grab();
            }
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
                "    type: l.type,\n"
                "    text: l.text || undefined,\n"
                "    visible: l.visible,\n"
                "    selected: l.selected,\n"
                "    opacity: l.opacity,\n"
                "    is3D: l.is3DEnabled(),\n"
                "    inPoint: l.inPoint(),\n"
                "    outPoint: l.outPoint()\n"
                "  });\n"
                "}\n"
                "return out;"
            );
            return evalScript(script, QStringLiteral("AI List Layers"));
        }

        QJsonObject McpDispatcher::toolGetLayerProperties(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name: 'name', index: 1, or layer: 'name')");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString script = QStringLiteral(
                "var l = __findLayer(%1);\n"
                "return {\n"
                "  index: l.index,\n"
                "  name: l.name,\n"
                "  type: l.type,\n"
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
            const QString type = args.value(QStringLiteral("type")).toString(QStringLiteral("rect")).toLower().trimmed();
            const QString rawName = args.value(QStringLiteral("name")).toString();
            const QString name = escapeJsString(rawName);

            QString script;
            if (type == QStringLiteral("rect") || type == QStringLiteral("rectangle") || type == QStringLiteral("box") || type == QStringLiteral("solid")) {
                const qreal x = args.value(QStringLiteral("x")).toDouble(0);
                const qreal y = args.value(QStringLiteral("y")).toDouble(0);
                const qreal w = args.value(QStringLiteral("width")).toDouble(args.value(QStringLiteral("w")).toDouble(200));
                const qreal h = args.value(QStringLiteral("height")).toDouble(args.value(QStringLiteral("h")).toDouble(200));
                script = QStringLiteral("var l = app.activeScene.addRect('%1', %2, %3, %4, %5);\n")
                         .arg(name, QString::number(x), QString::number(y), QString::number(w), QString::number(h));
            } else if (type == QStringLiteral("ellipse") || type == QStringLiteral("circle") || type == QStringLiteral("ball")) {
                const qreal cx = args.value(QStringLiteral("cx")).toDouble(args.value(QStringLiteral("x")).toDouble(0));
                const qreal cy = args.value(QStringLiteral("cy")).toDouble(args.value(QStringLiteral("y")).toDouble(0));
                const qreal r = args.value(QStringLiteral("radius")).toDouble(args.value(QStringLiteral("r")).toDouble(100));
                script = QStringLiteral("var l = app.activeScene.addEllipse('%1', %2, %3, %4);\n")
                         .arg(name, QString::number(cx), QString::number(cy), QString::number(r));
            } else if (type == QStringLiteral("text")) {
                const QString text = escapeJsString(args.value(QStringLiteral("text")).toString(QStringLiteral("Text")));
                script = QStringLiteral("var l = app.activeScene.addText('%1', '%2');\n")
                         .arg(name, text);
            } else if (type == QStringLiteral("null")) {
                script = QStringLiteral("var l = app.activeScene.addNull('%1');\n").arg(name);
            } else if (type == QStringLiteral("group")) {
                script = QStringLiteral("var l = app.activeScene.addGroup('%1');\n").arg(name);
            } else if (type == QStringLiteral("layer") || type == QStringLiteral("container") || type == QStringLiteral("panel")) {
                script = QStringLiteral("var l = app.activeScene.addLayer('%1');\n").arg(name);
            } else {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Unknown layer type: %1. Valid: rect/rectangle, ellipse/circle, text, null, group, layer/container").arg(type);
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            // Optional styling & initial properties
            if (args.contains(QStringLiteral("fillColor")) || args.contains(QStringLiteral("fill")) || args.contains(QStringLiteral("color"))) {
                const QString col = escapeJsString(args.contains(QStringLiteral("fillColor")) ? args.value(QStringLiteral("fillColor")).toString() : (args.contains(QStringLiteral("fill")) ? args.value(QStringLiteral("fill")).toString() : args.value(QStringLiteral("color")).toString()));
                script += QStringLiteral("l.setFillColor('%1');\n").arg(col);
            }
            if (args.contains(QStringLiteral("strokeColor")) || args.contains(QStringLiteral("stroke"))) {
                const QString col = escapeJsString(args.contains(QStringLiteral("strokeColor")) ? args.value(QStringLiteral("strokeColor")).toString() : args.value(QStringLiteral("stroke")).toString());
                script += QStringLiteral("l.setStrokeColor('%1');\n").arg(col);
            }
            if (args.contains(QStringLiteral("strokeWidth"))) {
                script += QStringLiteral("l.setStrokeWidth(%1);\n").arg(args.value(QStringLiteral("strokeWidth")).toDouble());
            }
            if (args.contains(QStringLiteral("fontSize"))) {
                script += QStringLiteral("l.setFontSize(%1);\n").arg(args.value(QStringLiteral("fontSize")).toDouble());
            }
            if (args.contains(QStringLiteral("fontFamily")) || args.contains(QStringLiteral("font"))) {
                const QString f = escapeJsString(args.contains(QStringLiteral("fontFamily")) ? args.value(QStringLiteral("fontFamily")).toString() : args.value(QStringLiteral("font")).toString());
                script += QStringLiteral("l.setFontFamily('%1');\n").arg(f);
            }
            if (args.contains(QStringLiteral("opacity"))) {
                script += QStringLiteral("l.opacity = %1;\n").arg(args.value(QStringLiteral("opacity")).toDouble());
            }
            if (args.contains(QStringLiteral("is3D")) || args.contains(QStringLiteral("3d"))) {
                const bool is3d = args.contains(QStringLiteral("is3D")) ? args.value(QStringLiteral("is3D")).toBool() : args.value(QStringLiteral("3d")).toBool();
                script += QStringLiteral("l.set3DEnabled(%1);\n").arg(is3d ? QStringLiteral("true") : QStringLiteral("false"));
            }
            if (args.contains(QStringLiteral("parent"))) {
                bool pOk = false;
                const QString pRef = parseLayerRef(args, QStringLiteral("parent"), &pOk);
                if (pOk) {
                    script += QStringLiteral("var p = __findLayer(%1); if (p) app.activeScene.setParent(l, p);\n").arg(pRef);
                }
            }

            script += QStringLiteral("return { name: l.name, index: l.index, type: l.type, visible: l.visible };");
            return evalScript(script, QStringLiteral("AI Create Layer"));
        }

        QJsonObject McpDispatcher::toolDuplicateLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name: 'name', index: 1, or layer: 'name')");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "var dup = lay.duplicate(); return { name: dup.name, index: dup.index };"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Duplicate Layer"));
        }

        QJsonObject McpDispatcher::toolDeleteLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name: 'name', index: 1, or layer: 'name')");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "lay.remove(); return 'OK';"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Delete Layer"));
        }

        QJsonObject McpDispatcher::toolSetParentLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify child layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            bool parentOk = false;
            const QString parentRefStr = parseLayerRef(args, QStringLiteral("parent"), &parentOk);
            const QString parentRef = parentOk ? QStringLiteral("__findLayer(%1)").arg(parentRefStr) : QStringLiteral("null");

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "lay.setParentLayer(%2); return 'OK';"
            ).arg(layerRef, parentRef);

            return evalScript(script, QStringLiteral("AI Set Parent Layer"));
        }

        QJsonObject McpDispatcher::toolSet3DMode(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const bool enabled = args.value(QStringLiteral("enabled")).toBool(true);
            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "lay.set3DEnabled(%2); return 'OK';"
            ).arg(layerRef, enabled ? QStringLiteral("true") : QStringLiteral("false"));

            return evalScript(script, QStringLiteral("AI Set 3D Mode"));
        }

        QJsonObject McpDispatcher::toolSetProperty(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QJsonValue val = args.value(QStringLiteral("value"));
            const QString valStr = QString::fromUtf8(QJsonDocument(QJsonArray{val}).toJson(QJsonDocument::Compact).mid(1).chopped(1));

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "var p = __findProp(lay, '%2');\n"
                "p.setValue(%3); return 'OK';"
            ).arg(layerRef, prop, valStr);

            return evalScript(script, QStringLiteral("AI Set Property"));
        }

        QJsonObject McpDispatcher::toolSetKeyframe(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QJsonValue val = args.value(QStringLiteral("value"));
            const QString valStr = QString::fromUtf8(QJsonDocument(QJsonArray{val}).toJson(QJsonDocument::Compact).mid(1).chopped(1));
            const QString easing = args.value(QStringLiteral("easing")).toString().trimmed();

            QString setCall;
            if (args.contains(QStringLiteral("frame"))) {
                const int frame = args.value(QStringLiteral("frame")).toInt();
                if (!easing.isEmpty()) {
                    setCall = QStringLiteral("p.setValueAtFrameWithEasing(%1, %2, '%3');").arg(QString::number(frame), valStr, easing);
                } else {
                    setCall = QStringLiteral("p.setValueAtFrame(%1, %2);").arg(QString::number(frame), valStr);
                }
            } else {
                const qreal time = args.value(QStringLiteral("time")).toDouble(0);
                if (!easing.isEmpty()) {
                    setCall = QStringLiteral("p.setValueAtTimeWithEasing(%1, %2, '%3');").arg(QString::number(time), valStr, easing);
                } else {
                    setCall = QStringLiteral("p.setValueAtTime(%1, %2);").arg(QString::number(time), valStr);
                }
            }

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "var p = __findProp(lay, '%2');\n"
                "%3 return 'OK';"
            ).arg(layerRef, prop, setCall);

            return evalScript(script, QStringLiteral("AI Set Keyframe"));
        }

        QJsonObject McpDispatcher::toolRemoveKeyframe(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString prop = args.value(QStringLiteral("property")).toString();
            const int frame = args.contains(QStringLiteral("frame"))
                ? args.value(QStringLiteral("frame")).toInt()
                : qRound(args.value(QStringLiteral("time")).toDouble() * 60.);

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "var p = __findProp(lay, '%2');\n"
                "p.removeKeyAtFrame(%3); return 'OK';"
            ).arg(layerRef, prop, QString::number(frame));

            return evalScript(script, QStringLiteral("AI Remove Keyframe"));
        }

        QJsonObject McpDispatcher::toolClearKeyframes(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "var p = __findProp(lay, '%2');\n"
                "while (p.numKeys > 0) {\n"
                "  p.removeKeyAtFrame(p.keyFrame(1));\n"
                "}\n"
                "return 'OK';"
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
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QString setCalls;
            if (args.contains(QStringLiteral("inFrame"))) {
                setCalls += QStringLiteral("lay.setInPoint(%1);\n").arg(args.value(QStringLiteral("inFrame")).toInt());
            } else if (args.contains(QStringLiteral("inPoint"))) {
                setCalls += QStringLiteral("lay.setInPoint(%1);\n").arg(args.value(QStringLiteral("inPoint")).toInt());
            } else if (args.contains(QStringLiteral("inTime"))) {
                setCalls += QStringLiteral("lay.setInPoint(Math.round(%1 * app.activeScene.fps));\n").arg(args.value(QStringLiteral("inTime")).toDouble());
            }

            if (args.contains(QStringLiteral("outFrame"))) {
                setCalls += QStringLiteral("lay.setOutPoint(%1);\n").arg(args.value(QStringLiteral("outFrame")).toInt());
            } else if (args.contains(QStringLiteral("outPoint"))) {
                setCalls += QStringLiteral("lay.setOutPoint(%1);\n").arg(args.value(QStringLiteral("outPoint")).toInt());
            } else if (args.contains(QStringLiteral("outTime"))) {
                setCalls += QStringLiteral("lay.setOutPoint(Math.round(%1 * app.activeScene.fps));\n").arg(args.value(QStringLiteral("outTime")).toDouble());
            }

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "%2"
                "return { name: lay.name, inPoint: lay.inPoint(), outPoint: lay.outPoint() };"
            ).arg(layerRef, setCalls);

            return evalScript(script, QStringLiteral("AI Set In Out Point"));
        }

        QJsonObject McpDispatcher::toolSetTextProperties(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QString script = QStringLiteral("var lay = __findLayer(%1);\n").arg(layerRef);
            if (args.contains(QStringLiteral("text"))) {
                script += QStringLiteral("lay.setText('%1');\n").arg(escapeJsString(args.value(QStringLiteral("text")).toString()));
            }
            if (args.contains(QStringLiteral("fontSize"))) {
                script += QStringLiteral("lay.setFontSize(%1);\n").arg(args.value(QStringLiteral("fontSize")).toDouble());
            }
            if (args.contains(QStringLiteral("fontFamily")) || args.contains(QStringLiteral("font"))) {
                QString fn = args.contains(QStringLiteral("fontFamily")) ? args.value(QStringLiteral("fontFamily")).toString() : args.value(QStringLiteral("font")).toString();
                script += QStringLiteral("lay.setFontFamily('%1');\n").arg(escapeJsString(fn));
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
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QString script = QStringLiteral("var lay = __findLayer(%1);\n").arg(layerRef);
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

        QJsonObject McpDispatcher::toolSetLayerOrder(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString action = args.value(QStringLiteral("order")).toString(
                args.value(QStringLiteral("action")).toString(QStringLiteral("top"))).toLower();

            QString call;
            if (action == QStringLiteral("top") || action == QStringLiteral("bringtofront")) {
                call = QStringLiteral("lay.bringToFront();");
            } else if (action == QStringLiteral("bottom") || action == QStringLiteral("bringtoend")) {
                call = QStringLiteral("lay.bringToEnd();");
            } else if (action == QStringLiteral("up") || action == QStringLiteral("moveup")) {
                call = QStringLiteral("lay.moveUp();");
            } else if (action == QStringLiteral("down") || action == QStringLiteral("movedown")) {
                call = QStringLiteral("lay.moveDown();");
            } else {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Unknown order action: %1 (valid: top, bottom, up, down)").arg(action);
                err[QStringLiteral("success")] = false;
                return err;
            }

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "%2\n"
                "return { name: lay.name, index: lay.index, action: '%3' };"
            ).arg(layerRef, call, action);

            return evalScript(script, QStringLiteral("AI Set Layer Order"));
        }

        QJsonObject McpDispatcher::toolSetLayerVisibility(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const bool visible = args.value(QStringLiteral("visible")).toBool(true);
            const QString script = QStringLiteral("var lay = __findLayer(%1);\nlay.visible = %2;\nreturn { name: lay.name, visible: lay.visible };\n").arg(layerRef).arg(visible ? QStringLiteral("true") : QStringLiteral("false"));
            return evalScript(script, QStringLiteral("Set Layer Visibility"));
        }

        QJsonObject McpDispatcher::toolSetLayerLock(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const bool locked = args.value(QStringLiteral("locked")).toBool(true);
            const QString script = QStringLiteral("var lay = __findLayer(%1);\nlay.setLocked(%2);\nreturn { name: lay.name, locked: lay.isLocked() };\n").arg(layerRef).arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
            return evalScript(script, QStringLiteral("Set Layer Lock"));
        }

        QJsonObject McpDispatcher::toolSetKeyframeEasing(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QString easing = args.value(QStringLiteral("easing")).toString(QStringLiteral("easeOutCubic"));

            QString startF = QStringLiteral("-1");
            QString endF = QStringLiteral("-1");

            if (args.contains(QStringLiteral("startFrame"))) {
                startF = QString::number(args.value(QStringLiteral("startFrame")).toInt());
            } else if (args.contains(QStringLiteral("startTime"))) {
                startF = QStringLiteral("Math.round(%1 * app.activeScene.fps)").arg(args.value(QStringLiteral("startTime")).toDouble());
            }

            if (args.contains(QStringLiteral("endFrame"))) {
                endF = QString::number(args.value(QStringLiteral("endFrame")).toInt());
            } else if (args.contains(QStringLiteral("endTime"))) {
                endF = QStringLiteral("Math.round(%1 * app.activeScene.fps)").arg(args.value(QStringLiteral("endTime")).toDouble());
            }

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "var p = __findProp(lay, '%2');\n"
                "var ok = p.setEasing('%3', %4, %5);\n"
                "return { name: lay.name, property: '%2', easing: '%3', success: ok };"
            ).arg(layerRef, prop, easing, startF, endF);

            return evalScript(script, QStringLiteral("AI Set Keyframe Easing"));
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
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString effectType = args.value(QStringLiteral("effectType")).toString(QStringLiteral("glow"));
            const QString script = QStringLiteral("var lay = __findLayer(%1);\nvar ok = lay.addEffect('%2');\nreturn { name: lay.name, effect: '%2', success: ok };\n").arg(layerRef).arg(effectType);
            return evalScript(script, QStringLiteral("Add Raster Effect"));
        }

        QJsonObject McpDispatcher::toolRemoveRasterEffect(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const int effectIndex = args.value(QStringLiteral("effectIndex")).toInt(0);
            const QString script = QStringLiteral("var lay = __findLayer(%1);\nvar ok = lay.removeEffect(%2);\nreturn { name: lay.name, removedIndex: %2, success: ok };\n").arg(layerRef).arg(effectIndex);
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

        QJsonObject McpDispatcher::toolRenderMarkup(const QJsonObject &args)
        {
            QJsonObject resp;
            const QString markup = args.value(QStringLiteral("markup")).toString().trimmed();
            if (markup.isEmpty()) {
                resp[QStringLiteral("error")] = QStringLiteral("Markup is empty");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString mode = args.value(QStringLiteral("mode")).toString(QStringLiteral("replace")).toLower();
            const qreal timeOffset = args.value(QStringLiteral("timeOffset")).toDouble(0.0);

            const QString scriptPath = findMarkupScriptPath();
            if (scriptPath.isEmpty()) {
                resp[QStringLiteral("error")] = QStringLiteral("friction_markup.py compiler script not found");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QProcess proc;
            QStringList procArgs;
            procArgs << scriptPath << QStringLiteral("--compile-only")
                     << QStringLiteral("--mode") << mode
                     << QStringLiteral("--time-offset") << QString::number(timeOffset)
                     << QStringLiteral("-");

            QString pythonExe = QStandardPaths::findExecutable(QStringLiteral("python3"));
            if (pythonExe.isEmpty()) {
                pythonExe = QStandardPaths::findExecutable(QStringLiteral("python"));
            }
            if (pythonExe.isEmpty()) {
                pythonExe = QStringLiteral("python3");
            }

            proc.start(pythonExe, procArgs);
            if (!proc.waitForStarted(3000)) {
                resp[QStringLiteral("error")] = QStringLiteral("Failed to execute python: %1").arg(proc.errorString());
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            proc.write(markup.toUtf8());
            proc.closeWriteChannel();

            if (!proc.waitForFinished(10000)) {
                proc.kill();
                resp[QStringLiteral("error")] = QStringLiteral("friction_markup.py compilation timed out");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const int exitCode = proc.exitCode();
            const QString stdErr = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            const QString compiledJs = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();

            if (exitCode != 0 || compiledJs.isEmpty()) {
                resp[QStringLiteral("error")] = stdErr.isEmpty() ? QStringLiteral("Markup compilation failed (empty output)") : stdErr;
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString groupName = (mode == QStringLiteral("append"))
                ? QStringLiteral("AI Append Markup")
                : QStringLiteral("AI Render Markup");

            auto evalResult = evalScript(compiledJs, groupName);
            evalResult[QStringLiteral("mode")] = mode;
            evalResult[QStringLiteral("timeOffset")] = timeOffset;
            return evalResult;
        }

        QJsonObject McpDispatcher::toolUpdateLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                err[QStringLiteral("success")] = false;
                return err;
            }

            QStringList ops;
            if (args.contains(QStringLiteral("newName"))) {
                ops << QStringLiteral("lay.name = '%1';").arg(escapeJsString(args.value(QStringLiteral("newName")).toString()));
            }
            if (args.contains(QStringLiteral("text"))) {
                ops << QStringLiteral("lay.setText('%1');").arg(escapeJsString(args.value(QStringLiteral("text")).toString()));
            }
            if (args.contains(QStringLiteral("fontSize"))) {
                ops << QStringLiteral("lay.setFontSize(%1);").arg(args.value(QStringLiteral("fontSize")).toDouble());
            }
            if (args.contains(QStringLiteral("fontFamily")) || args.contains(QStringLiteral("font"))) {
                QString f = args.contains(QStringLiteral("fontFamily")) ? args.value(QStringLiteral("fontFamily")).toString() : args.value(QStringLiteral("font")).toString();
                ops << QStringLiteral("lay.setFontFamily('%1');").arg(escapeJsString(f));
            }
            if (args.contains(QStringLiteral("alignment"))) {
                ops << QStringLiteral("lay.setTextAlignment('%1');").arg(args.value(QStringLiteral("alignment")).toString());
            }
            if (args.contains(QStringLiteral("fillColor")) || args.contains(QStringLiteral("color"))) {
                const QString c = args.contains(QStringLiteral("fillColor")) ? args.value(QStringLiteral("fillColor")).toString() : args.value(QStringLiteral("color")).toString();
                ops << QStringLiteral("lay.setFillColor('%1');").arg(c);
            }
            if (args.contains(QStringLiteral("strokeColor"))) {
                ops << QStringLiteral("lay.setStrokeColor('%1');").arg(args.value(QStringLiteral("strokeColor")).toString());
            }
            if (args.contains(QStringLiteral("strokeWidth"))) {
                ops << QStringLiteral("lay.setStrokeWidth(%1);").arg(args.value(QStringLiteral("strokeWidth")).toDouble());
            }
            if (args.contains(QStringLiteral("blendMode"))) {
                ops << QStringLiteral("lay.setBlendMode('%1');").arg(args.value(QStringLiteral("blendMode")).toString());
            }
            if (args.contains(QStringLiteral("cornerRadius")) || args.contains(QStringLiteral("radius"))) {
                const double r = args.contains(QStringLiteral("cornerRadius")) ? args.value(QStringLiteral("cornerRadius")).toDouble() : args.value(QStringLiteral("radius")).toDouble();
                ops << QStringLiteral("lay.setCornerRadius(%1);").arg(r);
            }
            if (args.contains(QStringLiteral("opacity"))) {
                ops << QStringLiteral("lay.opacity = %1;").arg(args.value(QStringLiteral("opacity")).toDouble());
            }
            if (args.contains(QStringLiteral("visible"))) {
                ops << QStringLiteral("lay.visible = %1;").arg(args.value(QStringLiteral("visible")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            }
            if (args.contains(QStringLiteral("locked"))) {
                ops << QStringLiteral("lay.setLocked(%1);").arg(args.value(QStringLiteral("locked")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            }
            if (args.contains(QStringLiteral("is3D")) || args.contains(QStringLiteral("3d"))) {
                const bool is3d = args.contains(QStringLiteral("is3D")) ? args.value(QStringLiteral("is3D")).toBool() : args.value(QStringLiteral("3d")).toBool();
                ops << QStringLiteral("lay.set3DEnabled(%1);").arg(is3d ? QStringLiteral("true") : QStringLiteral("false"));
            }
            if (args.contains(QStringLiteral("parent"))) {
                bool pOk = false;
                const QString pRef = parseLayerRef(args, QStringLiteral("parent"), &pOk);
                if (pOk && pRef != QStringLiteral("''") && pRef != QStringLiteral("'null'") && pRef != QStringLiteral("'none'")) {
                    ops << QStringLiteral("var p = __findLayer(%1); if (p) app.activeScene.setParent(lay, p);").arg(pRef);
                } else {
                    ops << QStringLiteral("app.activeScene.setParent(lay, null);");
                }
            }
            if (args.contains(QStringLiteral("order"))) {
                const QString action = args.value(QStringLiteral("order")).toString().toLower();
                if (action == QStringLiteral("top") || action == QStringLiteral("bringtofront")) {
                    ops << QStringLiteral("lay.bringToFront();");
                } else if (action == QStringLiteral("bottom") || action == QStringLiteral("bringtoend")) {
                    ops << QStringLiteral("lay.bringToEnd();");
                } else if (action == QStringLiteral("up") || action == QStringLiteral("moveup")) {
                    ops << QStringLiteral("lay.moveUp();");
                } else if (action == QStringLiteral("down") || action == QStringLiteral("movedown")) {
                    ops << QStringLiteral("lay.moveDown();");
                }
            }
            if (args.contains(QStringLiteral("position"))) {
                const auto val = args.value(QStringLiteral("position"));
                if (val.isArray()) {
                    const auto arr = val.toArray();
                    const double px = arr.at(0).toDouble(0);
                    const double py = arr.at(1).toDouble(0);
                    ops << QStringLiteral("lay.position().setValue([%1, %2]);").arg(QString::number(px), QString::number(py));
                } else if (val.isObject()) {
                    const auto obj = val.toObject();
                    const double px = obj.value(QStringLiteral("x")).toDouble(0);
                    const double py = obj.value(QStringLiteral("y")).toDouble(0);
                    ops << QStringLiteral("lay.position().setValue([%1, %2]);").arg(QString::number(px), QString::number(py));
                } else if (val.isString()) {
                    const QString str = val.toString().trimmed();
                    const auto parts = str.split(QLatin1Char(','));
                    if (parts.size() >= 2) {
                        ops << QStringLiteral("lay.position().setValue([%1, %2]);").arg(QString::number(parts[0].trimmed().toDouble()), QString::number(parts[1].trimmed().toDouble()));
                    }
                }
            } else if (args.contains(QStringLiteral("x")) || args.contains(QStringLiteral("y"))) {
                if (args.contains(QStringLiteral("x")) && args.contains(QStringLiteral("y"))) {
                    const double px = args.value(QStringLiteral("x")).toDouble();
                    const double py = args.value(QStringLiteral("y")).toDouble();
                    ops << QStringLiteral("lay.position().setValue([%1, %2]);").arg(QString::number(px), QString::number(py));
                } else if (args.contains(QStringLiteral("x"))) {
                    const double px = args.value(QStringLiteral("x")).toDouble();
                    ops << QStringLiteral("var curP = lay.position().value; lay.position().setValue([%1, (curP ? curP[1] : 0)]);").arg(QString::number(px));
                } else {
                    const double py = args.value(QStringLiteral("y")).toDouble();
                    ops << QStringLiteral("var curP = lay.position().value; lay.position().setValue([(curP ? curP[0] : 0), %1]);").arg(QString::number(py));
                }
            }
            if (args.contains(QStringLiteral("width")) || args.contains(QStringLiteral("height")) ||
                args.contains(QStringLiteral("w")) || args.contains(QStringLiteral("h"))) {
                const double w = args.contains(QStringLiteral("width")) ? args.value(QStringLiteral("width")).toDouble() : args.value(QStringLiteral("w")).toDouble(100.0);
                const double h = args.contains(QStringLiteral("height")) ? args.value(QStringLiteral("height")).toDouble() : args.value(QStringLiteral("h")).toDouble(100.0);
                ops << QStringLiteral("lay.setSize(%1, %2);").arg(QString::number(w), QString::number(h));
            }
            if (args.contains(QStringLiteral("scale"))) {
                const auto val = args.value(QStringLiteral("scale"));
                if (val.isArray()) {
                    const auto arr = val.toArray();
                    double sx = arr.at(0).toDouble(1.0);
                    double sy = arr.at(1).toDouble(1.0);
                    if (sx > 10.0) sx /= 100.0;
                    if (sy > 10.0) sy /= 100.0;
                    ops << QStringLiteral("lay.scale().setValue([%1, %2]);").arg(QString::number(sx), QString::number(sy));
                } else {
                    double s = val.toDouble(1.0);
                    if (s > 10.0) s /= 100.0;
                    ops << QStringLiteral("lay.scale().setValue([%1, %1]);").arg(QString::number(s));
                }
            }
            if (args.contains(QStringLiteral("rotation"))) {
                ops << QStringLiteral("lay.rotation().setValue(%1);").arg(args.value(QStringLiteral("rotation")).toDouble());
            }
            if (args.contains(QStringLiteral("rotationX"))) {
                ops << QStringLiteral("lay.set3DEnabled(true); lay.rotationX().setValue(%1);").arg(args.value(QStringLiteral("rotationX")).toDouble());
            }
            if (args.contains(QStringLiteral("rotationY"))) {
                ops << QStringLiteral("lay.set3DEnabled(true); lay.rotationY().setValue(%1);").arg(args.value(QStringLiteral("rotationY")).toDouble());
            }
            if (args.contains(QStringLiteral("zPosition"))) {
                ops << QStringLiteral("lay.set3DEnabled(true); lay.zPosition().setValue(%1);").arg(args.value(QStringLiteral("zPosition")).toDouble());
            }
            if (args.contains(QStringLiteral("inPoint")) || args.contains(QStringLiteral("inFrame"))) {
                const int inF = args.contains(QStringLiteral("inPoint")) ? args.value(QStringLiteral("inPoint")).toInt() : args.value(QStringLiteral("inFrame")).toInt();
                ops << QStringLiteral("lay.setInPoint(%1);").arg(inF);
            }
            if (args.contains(QStringLiteral("outPoint")) || args.contains(QStringLiteral("outFrame"))) {
                const int outF = args.contains(QStringLiteral("outPoint")) ? args.value(QStringLiteral("outPoint")).toInt() : args.value(QStringLiteral("outFrame")).toInt();
                ops << QStringLiteral("lay.setOutPoint(%1);").arg(outF);
            }

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "%2\n"
                "return { success: true, name: lay.name, index: lay.index, visible: lay.visible, opacity: lay.opacity };"
            ).arg(layerRef, ops.join(QStringLiteral("\n")));

            return evalScript(script, QStringLiteral("AI Update Layer"));
        }

        QJsonObject McpDispatcher::toolAnimateLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = parseLayerRef(args, QString(), &ok);
            if (!ok) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const QString preset = args.value(QStringLiteral("preset")).toString(
                args.value(QStringLiteral("type")).toString()
            ).toLower().trimmed();

            if (preset.isEmpty()) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Preset is required (e.g. pop, slide_up, slide_down, slide_left, slide_right, zoom, fade, flip_y, flip_x, rotate, spin, pop_fade, slide_up_fade)");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const int startFrame = args.value(QStringLiteral("startFrame")).toInt(-1);
            const double startTimeSec = args.value(QStringLiteral("startTime")).toDouble(-1.0);
            const int durationFrames = args.value(QStringLiteral("durationFrames")).toInt(-1);
            const double durationSec = args.value(QStringLiteral("duration")).toDouble(-1.0);
            const QString userEasing = args.value(QStringLiteral("easing")).toString();
            const double distance = args.value(QStringLiteral("distance")).toDouble(120.0);
            const bool isOut = args.value(QStringLiteral("isOut")).toBool(false);

            const QString script = QStringLiteral(
                "var lay = __findLayer(%1);\n"
                "var fps = (app.activeScene && app.activeScene.fps) ? app.activeScene.fps : 60;\n"
                "var startF = (%2 >= 0) ? %2 : ((%3 >= 0) ? Math.round(%3 * fps) : 0);\n"
                "var dur = (%4 > 0) ? %4 : ((%5 > 0) ? Math.round(%5 * fps) : 25);\n"
                "var endF = startF + dur;\n"
                "var isOut = %6;\n"
                "var userEasing = '%7';\n"
                "var dist = %8;\n"
                "var preset = '%9';\n"
                "\n"
                "if (preset.indexOf('fade') !== -1 || preset === 'fade') {\n"
                "  var curOp = (lay.opacity !== undefined && lay.opacity > 0) ? lay.opacity : 100;\n"
                "  if (!isOut) {\n"
                "    lay.opacityProp().setValueAtFrame(startF, 0);\n"
                "    lay.opacityProp().setValueAtFrameWithEasing(endF, curOp, userEasing || 'easeInOutCubic');\n"
                "  } else {\n"
                "    lay.opacityProp().setValueAtFrame(startF, curOp);\n"
                "    lay.opacityProp().setValueAtFrameWithEasing(endF, 0, userEasing || 'easeInOutCubic');\n"
                "  }\n"
                "}\n"
                "\n"
                "if (preset.indexOf('pop') !== -1) {\n"
                "  var curScale = lay.scale().value;\n"
                "  var sx = (curScale && curScale[0] !== undefined) ? curScale[0] : 1.0;\n"
                "  var sy = (curScale && curScale[1] !== undefined) ? curScale[1] : 1.0;\n"
                "  if (sx > 10.0) sx /= 100.0;\n"
                "  if (sy > 10.0) sy /= 100.0;\n"
                "  if (!isOut) {\n"
                "    lay.scale().setValueAtFrame(startF, [0, 0]);\n"
                "    lay.scale().setValueAtFrameWithEasing(endF, [sx, sy], userEasing || 'easeOutBack');\n"
                "  } else {\n"
                "    lay.scale().setValueAtFrame(startF, [sx, sy]);\n"
                "    lay.scale().setValueAtFrameWithEasing(endF, [0, 0], userEasing || 'easeInBack');\n"
                "  }\n"
                "} else if (preset.indexOf('slide_up') !== -1) {\n"
                "  var curPos = lay.position().value;\n"
                "  var px = (curPos && curPos[0] !== undefined) ? curPos[0] : 0;\n"
                "  var py = (curPos && curPos[1] !== undefined) ? curPos[1] : 0;\n"
                "  if (!isOut) {\n"
                "    lay.position().setValueAtFrame(startF, [px, py + dist]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px, py], userEasing || 'easeOutCubic');\n"
                "  } else {\n"
                "    lay.position().setValueAtFrame(startF, [px, py]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px, py - dist], userEasing || 'easeInCubic');\n"
                "  }\n"
                "} else if (preset.indexOf('slide_down') !== -1) {\n"
                "  var curPos = lay.position().value;\n"
                "  var px = (curPos && curPos[0] !== undefined) ? curPos[0] : 0;\n"
                "  var py = (curPos && curPos[1] !== undefined) ? curPos[1] : 0;\n"
                "  if (!isOut) {\n"
                "    lay.position().setValueAtFrame(startF, [px, py - dist]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px, py], userEasing || 'easeOutCubic');\n"
                "  } else {\n"
                "    lay.position().setValueAtFrame(startF, [px, py]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px, py + dist], userEasing || 'easeInCubic');\n"
                "  }\n"
                "} else if (preset.indexOf('slide_left') !== -1) {\n"
                "  var curPos = lay.position().value;\n"
                "  var px = (curPos && curPos[0] !== undefined) ? curPos[0] : 0;\n"
                "  var py = (curPos && curPos[1] !== undefined) ? curPos[1] : 0;\n"
                "  if (!isOut) {\n"
                "    lay.position().setValueAtFrame(startF, [px + dist, py]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px, py], userEasing || 'easeOutCubic');\n"
                "  } else {\n"
                "    lay.position().setValueAtFrame(startF, [px, py]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px - dist, py], userEasing || 'easeInCubic');\n"
                "  }\n"
                "} else if (preset.indexOf('slide_right') !== -1) {\n"
                "  var curPos = lay.position().value;\n"
                "  var px = (curPos && curPos[0] !== undefined) ? curPos[0] : 0;\n"
                "  var py = (curPos && curPos[1] !== undefined) ? curPos[1] : 0;\n"
                "  if (!isOut) {\n"
                "    lay.position().setValueAtFrame(startF, [px - dist, py]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px, py], userEasing || 'easeOutCubic');\n"
                "  } else {\n"
                "    lay.position().setValueAtFrame(startF, [px, py]);\n"
                "    lay.position().setValueAtFrameWithEasing(endF, [px + dist, py], userEasing || 'easeInCubic');\n"
                "  }\n"
                "} else if (preset.indexOf('zoom') !== -1) {\n"
                "  var curScale = lay.scale().value;\n"
                "  var sx = (curScale && curScale[0] !== undefined) ? curScale[0] : 1.0;\n"
                "  var sy = (curScale && curScale[1] !== undefined) ? curScale[1] : 1.0;\n"
                "  if (sx > 10.0) sx /= 100.0;\n"
                "  if (sy > 10.0) sy /= 100.0;\n"
                "  if (!isOut) {\n"
                "    lay.scale().setValueAtFrame(startF, [sx * 0.2, sy * 0.2]);\n"
                "    lay.scale().setValueAtFrameWithEasing(endF, [sx, sy], userEasing || 'easeOutCubic');\n"
                "  } else {\n"
                "    lay.scale().setValueAtFrame(startF, [sx, sy]);\n"
                "    lay.scale().setValueAtFrameWithEasing(endF, [sx * 2.0, sy * 2.0], userEasing || 'easeInCubic');\n"
                "  }\n"
                "} else if (preset.indexOf('rotate') !== -1 || preset.indexOf('spin') !== -1) {\n"
                "  if (!isOut) {\n"
                "    lay.rotation().setValueAtFrame(startF, -180);\n"
                "    lay.rotation().setValueAtFrameWithEasing(endF, 0, userEasing || 'easeOutBack');\n"
                "  } else {\n"
                "    lay.rotation().setValueAtFrame(startF, 0);\n"
                "    lay.rotation().setValueAtFrameWithEasing(endF, 180, userEasing || 'easeInBack');\n"
                "  }\n"
                "} else if (preset.indexOf('flip_y') !== -1) {\n"
                "  lay.set3DEnabled(true);\n"
                "  if (!isOut) {\n"
                "    lay.rotationY().setValueAtFrame(startF, -90);\n"
                "    lay.rotationY().setValueAtFrameWithEasing(endF, 0, userEasing || 'easeOutCubic');\n"
                "  } else {\n"
                "    lay.rotationY().setValueAtFrame(startF, 0);\n"
                "    lay.rotationY().setValueAtFrameWithEasing(endF, 90, userEasing || 'easeInCubic');\n"
                "  }\n"
                "} else if (preset.indexOf('flip_x') !== -1) {\n"
                "  lay.set3DEnabled(true);\n"
                "  if (!isOut) {\n"
                "    lay.rotationX().setValueAtFrame(startF, 90);\n"
                "    lay.rotationX().setValueAtFrameWithEasing(endF, 0, userEasing || 'easeOutCubic');\n"
                "  } else {\n"
                "    lay.rotationX().setValueAtFrame(startF, 0);\n"
                "    lay.rotationX().setValueAtFrameWithEasing(endF, -90, userEasing || 'easeInCubic');\n"
                "  }\n"
                "} else if (preset === 'fade') {\n"
                "  // Handled by opacity block above\n"
                "} else {\n"
                "  throw new Error('Unknown animate preset: ' + preset + '. Supported: pop, slide_up, slide_down, slide_left, slide_right, zoom, fade, flip_y, flip_x, rotate, spin, pop_fade, slide_up_fade');\n"
                "}\n"
                "return { success: true, layer: lay.name, preset: preset, startFrame: startF, endFrame: endF };"
            ).arg(layerRef,
                 QString::number(startFrame),
                 QString::number(startTimeSec),
                 QString::number(durationFrames),
                 QString::number(durationSec),
                 isOut ? QStringLiteral("true") : QStringLiteral("false"),
                 userEasing, QString::number(distance), preset);

            return evalScript(script, QStringLiteral("AI Animate Layer"));
        }

        QJsonObject McpDispatcher::toolGetStoryboard(const QJsonObject &args)
        {
            QJsonObject resp;
            auto mw = mainWindow();
            if (!mw) {
                resp[QStringLiteral("error")] = QStringLiteral("MainWindow unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            auto scene = activeScene();
            if (!scene || !Document::sInstance) {
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const int targetWidth = args.value(QStringLiteral("width")).toInt(480);
            const QString format = args.value(QStringLiteral("format")).toString(QStringLiteral("jpeg")).toLower();
            const int quality = args.value(QStringLiteral("quality")).toInt(80);

            const int origFrame = scene->getCurrentFrame();
            const auto frameRange = scene->getFrameRange();
            const int fMin = frameRange.fMin;
            const int fMax = frameRange.fMax;

            const qreal fps = scene->getFps() > 0 ? scene->getFps() : 60.0;
            int sampleStart = fMin;
            int sampleEnd = fMax;
            if (args.contains(QStringLiteral("startFrame"))) {
                sampleStart = args.value(QStringLiteral("startFrame")).toInt();
            } else if (args.contains(QStringLiteral("startTime"))) {
                sampleStart = qRound(args.value(QStringLiteral("startTime")).toDouble() * fps);
            }
            if (args.contains(QStringLiteral("endFrame"))) {
                sampleEnd = args.value(QStringLiteral("endFrame")).toInt();
            } else if (args.contains(QStringLiteral("endTime"))) {
                sampleEnd = qRound(args.value(QStringLiteral("endTime")).toDouble() * fps);
            }
            if (sampleEnd < sampleStart) {
                std::swap(sampleStart, sampleEnd);
            }

            QVector<int> sampleFrames;
            if (args.contains(QStringLiteral("frames"))) {
                const auto arr = args.value(QStringLiteral("frames")).toArray();
                for (const auto &v : arr) {
                    sampleFrames.append(v.toInt());
                }
            } else {
                int count = args.value(QStringLiteral("numFrames")).toInt(4);
                if (count < 2) count = 2;
                if (count > 8) count = 8;
                const int span = qMax(1, sampleEnd - sampleStart);
                for (int i = 0; i < count; ++i) {
                    int f = sampleStart + qRound((qreal)i / (count - 1) * span);
                    sampleFrames.append(f);
                }
            }

            QWidget *grabWidget = findCanvasWidget(mw);
            QJsonArray storyboard;

            for (int f : sampleFrames) {
                Document::sInstance->setActiveSceneFrame(f);
                QApplication::processEvents();

                QPixmap pix = grabWidget->grab();
                if (pix.isNull()) {
                    pix = mw->grab();
                }

                if (!pix.isNull() && targetWidth > 0 && pix.width() > targetWidth) {
                    pix = pix.scaledToWidth(targetWidth, Qt::SmoothTransformation);
                }

                QByteArray bytes;
                QBuffer buf(&bytes);
                buf.open(QIODevice::WriteOnly);
                pix.save(&buf, (format == QStringLiteral("png")) ? "PNG" : "JPEG", quality);

                QJsonObject frameObj;
                frameObj[QStringLiteral("frame")] = f;
                frameObj[QStringLiteral("time")] = (qreal)f / fps;
                frameObj[QStringLiteral("width")] = pix.width();
                frameObj[QStringLiteral("height")] = pix.height();
                frameObj[QStringLiteral("data")] = QString::fromLatin1(bytes.toBase64());
                storyboard.append(frameObj);
            }

            // Restore original frame
            Document::sInstance->setActiveSceneFrame(origFrame);
            QApplication::processEvents();

            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("format")] = format;
            resp[QStringLiteral("storyboard")] = storyboard;
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
                props[QStringLiteral("easing")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Optional easing preset from previous key (e.g. easeOutCubic, easeInOutQuad, easeOutBack, bounce, elastic)")}};
                tools.append(makeTool(QStringLiteral("friction_set_keyframe"),
                                      QStringLiteral("Set a keyframe on a layer property at a specific frame or time, with optional easing curve"),
                                      props, QJsonArray{QStringLiteral("property"), QStringLiteral("value")}));
            }

            // 11.1 set_keyframe_easing
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Property name to ease (position, scale, rotation, opacity, etc.)")}};
                props[QStringLiteral("easing")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Easing preset ID (easeOutCubic, easeInOutQuad, easeOutBack, easeOutBounce, easeOutElastic, etc.)")}};
                props[QStringLiteral("startFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Start keyframe index (optional, defaults to first key)")}};
                props[QStringLiteral("endFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("End keyframe index (optional, defaults to last key)")}};
                props[QStringLiteral("startTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Start time in seconds")}};
                props[QStringLiteral("endTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("End time in seconds")}};
                tools.append(makeTool(QStringLiteral("friction_set_keyframe_easing"),
                                      QStringLiteral("Apply a smooth mathematical easing curve (Robert Penner equations) between keyframes on a property"),
                                      props, QJsonArray{QStringLiteral("property"), QStringLiteral("easing")}));
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

            // 19.1 set_layer_order
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("order")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Z-order action: 'top' (bring to front), 'bottom' (send to back), 'up' (raise one level), 'down' (lower one level)")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("up"), QStringLiteral("down")}}};
                tools.append(makeTool(QStringLiteral("friction_set_layer_order"),
                                      QStringLiteral("Reorder a layer in the layer stack (top, bottom, up, down)"),
                                      props, QJsonArray{QStringLiteral("order")}));
            }

            // 19.2 set_in_out_point
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("inFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("In-point start frame")}};
                props[QStringLiteral("outFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Out-point end frame")}};
                props[QStringLiteral("inTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("In-point start time in seconds")}};
                props[QStringLiteral("outTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Out-point end time in seconds")}};
                tools.append(makeTool(QStringLiteral("friction_set_in_out_point"),
                                      QStringLiteral("Set layer in-point and out-point clip boundaries in frames or seconds"),
                                      props));
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

            // 23. render_markup
            {
                QJsonObject props;
                props[QStringLiteral("markup")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("XML markup string defining the entire animation scene or clips")}};
                props[QStringLiteral("mode")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("replace"), QStringLiteral("append")}}, {QStringLiteral("description"), QStringLiteral("replace (default): clears scene first; append: keeps existing layers and appends new content")}};
                props[QStringLiteral("timeOffset")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Time offset in seconds for all generated layers and keyframes")}};
                tools.append(makeTool(QStringLiteral("friction_render_markup"),
                                      QStringLiteral("Declaratively compile and render vector animations, typography, and motion graphics via Friction XML Markup with replace or append modes"),
                                      props, QJsonArray{QStringLiteral("markup")}));
            }

            // 24. update_layer
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Layer index")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name")}};
                props[QStringLiteral("newName")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("New layer name")}};
                props[QStringLiteral("text")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Text content")}};
                props[QStringLiteral("fontSize")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Font size")}};
                props[QStringLiteral("fontFamily")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Font family")}};
                props[QStringLiteral("alignment")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}}};
                props[QStringLiteral("fillColor")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Fill color (#RRGGBB or #RRGGBBAA)")}};
                props[QStringLiteral("strokeColor")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Stroke color")}};
                props[QStringLiteral("strokeWidth")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Stroke width")}};
                props[QStringLiteral("blendMode")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Blend mode")}};
                props[QStringLiteral("cornerRadius")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Corner radius")}};
                props[QStringLiteral("opacity")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Opacity (0-100)")}};
                props[QStringLiteral("visible")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Layer visibility")}};
                props[QStringLiteral("locked")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Layer lock state")}};
                props[QStringLiteral("is3D")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Enable 2.5D spatial mode")}};
                props[QStringLiteral("position")] = QJsonObject{{QStringLiteral("description"), QStringLiteral("Position [x, y], {x, y} or 'x, y'")}};
                props[QStringLiteral("x")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("X position")}};
                props[QStringLiteral("y")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Y position")}};
                props[QStringLiteral("width")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Width in pixels (rect/circle)")}};
                props[QStringLiteral("height")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Height in pixels (rect/circle)")}};
                props[QStringLiteral("scale")] = QJsonObject{{QStringLiteral("description"), QStringLiteral("Scale [sx, sy] or uniform scalar")}};
                props[QStringLiteral("rotation")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Z rotation in degrees")}};
                props[QStringLiteral("rotationX")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("X rotation in degrees")}};
                props[QStringLiteral("rotationY")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Y rotation in degrees")}};
                props[QStringLiteral("zPosition")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Z depth")}};
                props[QStringLiteral("inPoint")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("In-point frame")}};
                props[QStringLiteral("outPoint")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Out-point frame")}};
                tools.append(makeTool(QStringLiteral("friction_update_layer"),
                                      QStringLiteral("Non-destructively update an existing layer in-place (text, colors, position, size, scale, 2.5D, visibility, in/out points)"),
                                      props));
            }

            // 25. animate_layer
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("preset")] = QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("enum"), QJsonArray{
                        QStringLiteral("pop"), QStringLiteral("slide_up"), QStringLiteral("slide_down"),
                        QStringLiteral("slide_left"), QStringLiteral("slide_right"), QStringLiteral("zoom"),
                        QStringLiteral("fade"), QStringLiteral("flip_y"), QStringLiteral("flip_x")
                    }},
                    {QStringLiteral("description"), QStringLiteral("High-level animation macro preset")}
                };
                props[QStringLiteral("startFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Start frame index (default: 0)")}};
                props[QStringLiteral("durationFrames")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Duration in frames (default: 25)")}};
                props[QStringLiteral("easing")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Easing curve preset name")}};
                props[QStringLiteral("distance")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Slide distance in pixels (default: 120)")}};
                props[QStringLiteral("isOut")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("If true, animates out instead of in (default: false)")}};
                tools.append(makeTool(QStringLiteral("friction_animate_layer"),
                                      QStringLiteral("Apply high-level animation macro to a layer (pop, slide, zoom, fade, 2.5D flip) with auto-configured easing curves"),
                                      props, QJsonArray{QStringLiteral("preset")}));
            }

            // 26. get_storyboard
            {
                QJsonObject props;
                props[QStringLiteral("numFrames")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Number of visual frames to sample across timeline (default: 4, max: 8)")}};
                props[QStringLiteral("startTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Start sampling time in seconds")}};
                props[QStringLiteral("endTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("End sampling time in seconds")}};
                props[QStringLiteral("startFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Start sampling frame index")}};
                props[QStringLiteral("endFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("End sampling frame index")}};
                props[QStringLiteral("frames")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}, {QStringLiteral("description"), QStringLiteral("Explicit list of frame indices to capture")}};
                props[QStringLiteral("width")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Thumbnail image width in pixels (default: 480)")}};
                props[QStringLiteral("format")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("jpeg"), QStringLiteral("png")}}};
                props[QStringLiteral("quality")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Compression quality (default: 80)")}};
                tools.append(makeTool(QStringLiteral("friction_get_storyboard"),
                                      QStringLiteral("Sample multiple visual frames across the timeline and return Base64 storyboard strips for AI vision review"),
                                      props));
            }

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
