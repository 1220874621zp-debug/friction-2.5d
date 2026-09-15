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
#include "Private/esettings.h"
#include "Expressions/expressionpresets.h"
#include "canvas.h"
#include "actions.h"
#include "appsupport.h"
#include "Boxes/boundingbox.h"
#include "Boxes/containerbox.h"
#include "Boxes/textbox.h"
#include "Boxes/lottiebox.h"
#include "lottieprovider.h"
#include "layeranimpresets.h"
#include "textanimpresets.h"
#include "GUI/mainwindow.h"
#include "GUI/canvaswindow.h"
#include "GUI/timelinedockwidget.h"
#include "renderhandler.h"

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
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QCoreApplication>
#include <QStandardPaths>

namespace Friction
{
    namespace AI
    {
        namespace
        {
            // watchdog budget for a single AI-issued script; generous
            // enough for large compiled markups, short enough that a
            // runaway loop cannot wedge the app for good
            constexpr int kEvalTimeoutMs = 30000;

            // JSON-quote a string into a valid JS string literal.
            // Used for EVERY string interpolated into generated JS so
            // hostile or merely odd input (quotes, newlines, unicode)
            // can never break out into code
            QString jsStr(const QString &str)
            {
                return QString::fromUtf8(QJsonDocument(QJsonArray{str})
                            .toJson(QJsonDocument::Compact).mid(1).chopped(1));
            }

            // engine opacity is 0..100 end-to-end (animator range,
            // render multiplies by 2.55); accept AE-style percent and
            // also fractions in (0,1] which agents commonly send
            double normOpacity(const double v)
            {
                double x = v;
                if (x > 0.0 && x <= 1.0) { x *= 100.0; }
                return qBound(0.0, x, 100.0);
            }

            // let scheduled canvas renders settle before grabbing a
            // frame, bounded so the loop cannot spin forever
            void settleCanvas(const int ms = 60)
            {
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms) {
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                }
            }
        }

        // RAII: hides selection outlines/handles and rulers so widget
        // grabs produce a clean frame; restores everything after
        struct CleanCanvasGrab
        {
            Canvas *canvas = nullptr;
            QList<BoundingBox *> selected;
            bool rulersWere = true;
            bool active = false;

            void begin(Canvas * const c)
            {
                canvas = c;
                if (canvas) {
                    selected = canvas->getSelectedBoxesList();
                    if (!selected.isEmpty()) { canvas->clearBoxesSelection(); }
                }
                rulersWere = AppSupport::getSettings(QStringLiteral("view"),
                                                     QStringLiteral("rulers"),
                                                     true).toBool();
                if (rulersWere) { CanvasWindow::setRulersVisible(false); }
                active = true;
            }

            void end()
            {
                if (!active) { return; }
                if (canvas) {
                    for (auto *box : selected) {
                        if (box) { canvas->addBoxToSelection(box); }
                    }
                }
                if (rulersWere) { CanvasWindow::setRulersVisible(true); }
                active = false;
            }

            ~CleanCanvasGrab() { end(); }
        };

        static QWidget *findCanvasWidget(QMainWindow *mw)
        {
            if (!mw) return nullptr;
            CanvasWindow *bestCanvas = nullptr;
            int bestArea = 0;
            const auto canvases = mw->findChildren<CanvasWindow*>();
            for (auto *c : canvases) {
                if (c && c->isVisible()) {
                    int area = c->width() * c->height();
                    if (area > bestArea) {
                        bestArea = area;
                        bestCanvas = c;
                    }
                }
            }
            if (bestCanvas) return bestCanvas;
            if (!canvases.isEmpty()) return canvases.first();

            const auto named = mw->findChildren<QWidget*>(QStringLiteral("canvasWindow"));
            for (auto w : named) {
                if (w && w->isVisible()) { return w; }
            }
            if (!named.isEmpty()) { return named.first(); }
            return mw;
        }

        static QString findMarkupScriptPath()
        {
            const QString standardPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("friction/tools/mcp/friction_markup.py"));
            if (!standardPath.isEmpty() && QFileInfo::exists(standardPath)) {
                return standardPath;
            }

            const QStringList candidatePaths = {
                // portable install: tools/ deployed next to the binary
                QCoreApplication::applicationDirPath() + QStringLiteral("/tools/mcp/friction_markup.py"),
                // build tree: binary in build/src/app/Release, sources at repo root
                QCoreApplication::applicationDirPath() + QStringLiteral("/../../../../tools/mcp/friction_markup.py"),
                QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/mcp/friction_markup.py"),
                QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/mcp/friction_markup.py"),
                QCoreApplication::applicationDirPath() + QStringLiteral("/../../../tools/mcp/friction_markup.py"),
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

        // parses a row index or group path argument; every segment
        // is a 1-based row number within its container (the number
        // the timeline shows). Group-internal rows and top-level
        // rows repeat numbers by design - only the full path is
        // unambiguous ("2/1" = top-level row 2, its child row 1)
        static bool parseLayerPathValue(const QJsonValue &v, QList<int> &path)
        {
            if (v.isDouble()) {
                const int n = v.toInt();
                if (n < 1) { return false; }
                path.append(n);
                return true;
            }
            if (v.isArray()) {
                const auto arr = v.toArray();
                if (arr.isEmpty()) { return false; }
                for (const auto &seg : arr) {
                    if (!seg.isDouble() || seg.toInt() < 1) { return false; }
                    path.append(seg.toInt());
                }
                return true;
            }
            if (v.isString()) {
                const QString s = v.toString().trimmed();
                if (s.isEmpty()) { return false; }
                const auto parts = s.split(QLatin1Char('/'), Qt::SkipEmptyParts);
                for (const auto &part : parts) {
                    bool okNum = false;
                    const int n = part.trimmed().toInt(&okNum);
                    if (!okNum || n < 1) { return false; }
                    path.append(n);
                }
                return !path.isEmpty();
            }
            return false;
        }

        static QString pathListLiteral(const QList<int> &path)
        {
            QStringList nums;
            for (const int n : path) { nums << QString::number(n); }
            return QStringLiteral("[%1]").arg(nums.join(QStringLiteral(", ")));
        }

        // Builds a JS expression resolving to the addressed layer.
        // Priority: row index / group path (unambiguous, primary),
        // then name (recursive lookup, ambiguous when duplicated).
        // Accepted forms for "index", "path" or "layer":
        //   3        top-level row 3
        //   "3"      same
        //   "2/1"    group path: top-level row 2, child row 1
        //   [2,1]    path array, same as "2/1"
        // "name"/"layerName" (and a non-numeric "layer" string) fall
        // back to name lookup.
        static QString layerRefExpr(const QJsonObject &args,
                                    const QString &prefix = QString(),
                                    bool *ok = nullptr)
        {
            if (ok) { *ok = true; }
            const QString idxKey = prefix.isEmpty() ? QStringLiteral("index") : prefix + QStringLiteral("Index");
            const QString pathKey = prefix.isEmpty() ? QStringLiteral("path") : prefix + QStringLiteral("Path");
            const QString nameKey = prefix.isEmpty() ? QStringLiteral("name") : prefix + QStringLiteral("Name");
            const QString layerKey = prefix.isEmpty() ? QStringLiteral("layer") : prefix;
            const QString layerNameKey = prefix.isEmpty() ? QStringLiteral("layerName") : prefix + QStringLiteral("Layer");

            // primary: row index or group path
            const QString refKeys[3] = { idxKey, pathKey, layerKey };
            for (const auto &key : refKeys) {
                if (!args.contains(key)) { continue; }
                QList<int> path;
                if (parseLayerPathValue(args.value(key), path)) {
                    return path.count() == 1
                            ? QStringLiteral("__findLayer(%1)").arg(path.first())
                            : QStringLiteral("__findLayerPath(%1)").arg(pathListLiteral(path));
                }
                // a non-numeric string under the generic "layer" key
                // is a name, not a path
                const auto val = args.value(key);
                if (key == layerKey && val.isString() && !val.toString().isEmpty()) {
                    return QStringLiteral("__findLayer(%1)").arg(jsStr(val.toString()));
                }
            }
            // fallback: name (rows are the recommended addressing)
            const QString nameKeys[2] = { nameKey, layerNameKey };
            for (const auto &key : nameKeys) {
                if (!args.contains(key)) { continue; }
                const auto val = args.value(key);
                if (val.isDouble() && val.toInt() >= 1) {
                    return QStringLiteral("__findLayer(%1)").arg(val.toInt());
                }
                if (val.isString() && !val.toString().isEmpty()) {
                    return QStringLiteral("__findLayer(%1)").arg(jsStr(val.toString()));
                }
            }
            if (ok) { *ok = false; }
            return QStringLiteral("null");
        }

        // C++ mirror of layerRefExpr for tools that operate on raw
        // BoundingBox pointers (no JS roundtrip)
        static BoundingBox *resolveLayerRefCxx(const QJsonObject &args,
                                              ContainerBox * const root,
                                              const QString &prefix = QString(),
                                              bool *ok = nullptr)
        {
            if (ok) { *ok = true; }
            if (!root) { if (ok) { *ok = false; } return nullptr; }
            const QString idxKey = prefix.isEmpty() ? QStringLiteral("index") : prefix + QStringLiteral("Index");
            const QString pathKey = prefix.isEmpty() ? QStringLiteral("path") : prefix + QStringLiteral("Path");
            const QString nameKey = prefix.isEmpty() ? QStringLiteral("name") : prefix + QStringLiteral("Name");
            const QString layerKey = prefix.isEmpty() ? QStringLiteral("layer") : prefix;
            const QString layerNameKey = prefix.isEmpty() ? QStringLiteral("layerName") : prefix + QStringLiteral("Layer");

            auto containedBoxes = [](ContainerBox * const c) {
                QList<BoundingBox*> out;
                if (!c) { return out; }
                const auto &contained = c->getContained();
                for (const auto &child : contained) {
                    const auto box = enve_cast<BoundingBox*>(child.get());
                    if (box) { out.append(box); }
                }
                return out;
            };

            QList<int> path;
            const QString pathKeys[3] = { idxKey, pathKey, layerKey };
            for (const auto &key : pathKeys) {
                if (args.contains(key) && parseLayerPathValue(args.value(key), path)) { break; }
                path.clear();
            }

            if (!path.isEmpty()) {
                ContainerBox *cursor = root;
                BoundingBox *result = nullptr;
                for (int depth = 0; depth < path.count(); ++depth) {
                    const auto siblings = containedBoxes(cursor);
                    const int row = path.at(depth);
                    if (row < 1 || row > siblings.count()) { if (ok) { *ok = false; } return nullptr; }
                    result = siblings.at(row - 1);
                    cursor = enve_cast<ContainerBox*>(result);
                }
                return result;
            }

            // name fallback: direct children first, then recursive
            const QString nameKeys[2] = { nameKey, layerNameKey };
            QString name;
            for (const auto &key : nameKeys) {
                if (args.contains(key) && args.value(key).isString()) {
                    name = args.value(key).toString();
                    if (!name.isEmpty()) { break; }
                }
            }
            if (name.isEmpty()) {
                const auto layerVal = args.value(layerKey);
                if (layerVal.isString() && !layerVal.toString().isEmpty()
                        && !parseLayerPathValue(layerVal, path)) {
                    name = layerVal.toString();
                }
            }
            if (!name.isEmpty()) {
                for (const auto box : containedBoxes(root)) {
                    if (box->prp_getName() == name) { return box; }
                }
                std::function<BoundingBox*(ContainerBox*)> search =
                        [&](ContainerBox * const c) -> BoundingBox* {
                    for (const auto box : containedBoxes(c)) {
                        if (box->prp_getName() == name) { return box; }
                        const auto grp = enve_cast<ContainerBox*>(box);
                        if (grp) {
                            if (const auto hit = search(grp)) { return hit; }
                        }
                    }
                    return nullptr;
                };
                return search(root);
            }

            if (ok) { *ok = false; }
            return nullptr;
        }

        // whole-layer timeline shift ("drag the layer later on the
        // timeline"): with a duration fragment the fragment position
        // moves and the keys follow through the rel->abs frame
        // mapping; without one every keyframe is shifted directly -
        // either way the layer AND its animation land later in time,
        // as one undoable transform
        static void shiftBoxInTime(BoundingBox * const box, const int delta)
        {
            if (!box || delta == 0) { return; }
            box->startShiftAllTransform();
            box->moveShiftAllBy(delta);
            box->finishShiftAllTransform();
        }

        // stagger targets by row order: first layer stays put, every
        // following layer shifts by slot*stagger frames on the
        // timeline; call inside a undo batch for a single undo step
        static QJsonArray staggerTargetsInTime(const QVector<BoundingBox*> &targets,
                                               const int stagger,
                                               const QString &order)
        {
            QJsonArray shifted;
            const int n = targets.count();
            for (int i = 0; i < n; ++i) {
                const int slot = (order == QStringLiteral("bottom")) ?
                            (n - 1 - i) : i;
                const int delta = slot * stagger;
                shiftBoxInTime(targets.at(i), delta);
                QJsonObject o;
                o[QStringLiteral("row")] = i + 1;
                o[QStringLiteral("name")] = targets.at(i)->prp_getName();
                o[QStringLiteral("shiftedFrames")] = delta;
                shifted.append(o);
            }
            return shifted;
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
            // strictly a getter: never creates a scene as a side
            // effect of a read-only tool call
            if (!Document::sInstance) { return nullptr; }
            if (Document::sInstance->fActiveScene) {
                return Document::sInstance->fActiveScene;
            }
            if (!Document::sInstance->fScenes.isEmpty()) {
                return Document::sInstance->fScenes.first().get();
            }
            return nullptr;
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
            // re-entrancy guard: tools that pump the event loop
            // (storyboard sampling) or long-running scripts must not
            // be interleaved with a second tool mutating the same
            // document mid-flight
            if (mDispatching) {
                QJsonObject busy;
                busy[QStringLiteral("success")] = false;
                busy[QStringLiteral("error")] = QStringLiteral(
                            "Server busy: another tool call is still executing; retry when it completes");
                return busy;
            }
            mDispatching = true;
            const QJsonObject result = dispatchToolImpl(toolName, arguments);
            mDispatching = false;
            return result;
        }

        void McpDispatcher::dispatchToolAsync(const QString &toolName,
                                              const QJsonObject &arguments,
                                              const std::function<void(const QJsonObject&)> &callback)
        {
            if (toolName == QStringLiteral("friction_render_markup")) {
                toolRenderMarkupAsync(arguments, callback);
                return;
            }
            callback(dispatchTool(toolName, arguments));
        }

        QJsonObject McpDispatcher::dispatchToolImpl(const QString &toolName,
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
            } else if (toolName == QStringLiteral("friction_create_lottie_layer")) {
                return toolCreateLottieLayer(arguments);
            } else if (toolName == QStringLiteral("friction_update_lottie_layer")) {
                return toolUpdateLottieLayer(arguments);
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
                // served exclusively through dispatchToolAsync: the
                // python compiler runs out-of-line so neither the GUI
                // nor the calling connection may block on it
                QJsonObject resp;
                resp[QStringLiteral("success")] = false;
                resp[QStringLiteral("error")] = QStringLiteral(
                            "friction_render_markup is asynchronous and must be called through the async dispatcher");
                return resp;
            } else if (toolName == QStringLiteral("friction_update_layer")) {
                return toolUpdateLayer(arguments);
            } else if (toolName == QStringLiteral("friction_animate_layer")) {
                return toolAnimateLayer(arguments);
            } else if (toolName == QStringLiteral("friction_get_storyboard")) {
                return toolGetStoryboard(arguments);
            } else if (toolName == QStringLiteral("friction_get_keyframes")) {
                return toolGetKeyframes(arguments);
            } else if (toolName == QStringLiteral("friction_set_expression")) {
                return toolSetExpression(arguments);
            } else if (toolName == QStringLiteral("friction_list_anim_presets")) {
                return toolListAnimPresets(arguments);
            } else if (toolName == QStringLiteral("friction_apply_anim_preset")) {
                return toolApplyAnimPreset(arguments);
            } else if (toolName == QStringLiteral("friction_stagger_layers")) {
                return toolStaggerLayers(arguments);
            } else if (toolName == QStringLiteral("friction_list_easing_presets")) {
                return toolListEasingPresets(arguments);
            } else if (toolName == QStringLiteral("friction_capture_viewport")) {
                const QString fmt = arguments.value(QStringLiteral("format")).toString(QStringLiteral("png"));
                const int quality = arguments.value(QStringLiteral("quality")).toInt(90);
                return captureViewport(fmt, quality);
            } else if (toolName == QStringLiteral("friction_undo")) {
                return toolUndo(arguments);
            } else if (toolName == QStringLiteral("friction_redo")) {
                return toolRedo(arguments);
            } else if (toolName == QStringLiteral("friction_import_file")) {
                return toolImportFile(arguments);
            } else if (toolName == QStringLiteral("friction_set_marker")) {
                return toolSetMarker(arguments);
            } else if (toolName == QStringLiteral("friction_clear_markers")) {
                return toolClearMarkers(arguments);
            } else if (toolName == QStringLiteral("friction_list_markers")) {
                return toolListMarkers(arguments);
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
                "  function __findLayerPath(path) {\n"
                "    var s = app.activeScene;\n"
                "    if (!s) throw new Error('No active scene');\n"
                "    if (!path || !path.length) throw new Error('Empty layer path');\n"
                "    var l = s.layer(path[0]);\n"
                "    if (!l) {\n"
                "      var tops = s.layers(), names = [];\n"
                "      for (var i = 0; i < tops.length; i++) names.push((i + 1) + \": '\" + tops[i].name + \"'\");\n"
                "      throw new Error('Layer path segment 1 (row ' + path[0] + ') not found. Top-level rows: [' + names.join(', ') + ']');\n"
                "    }\n"
                "    for (var i = 1; i < path.length; i++) {\n"
                "      var kids = (typeof l.layers === 'function') ? l.layers() : [];\n"
                "      var next = (typeof l.layer === 'function') ? l.layer(path[i]) : null;\n"
                "      if (!next) {\n"
                "        var kn = [];\n"
                "        for (var k = 0; k < kids.length; k++) kn.push((k + 1) + \": '\" + kids[k].name + \"'\");\n"
                "        throw new Error('Layer path segment ' + (i + 1) + ' (row ' + path[i] + ') not found inside group \"' + l.name + '\". Child rows: [' + kn.join(', ') + ']');\n"
                "      }\n"
                "      l = next;\n"
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

            const QString result = host->evaluate(wrapped, kEvalTimeoutMs);
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

            // clean grab: selection outlines, transform handles and
            // rulers are hidden while the pixels are taken so the
            // image reflects the scene, not the editing chrome
            QRect cropRect;
            CleanCanvasGrab clean;
            if (auto *cw = qobject_cast<CanvasWindow*>(grabWidget)) {
                clean.begin(cw->getCurrentCanvas());
                cw->fitCanvasToSize();
                cw->repaint();
                if (auto *c = cw->getCurrentCanvas()) {
                    const QSize cSize = c->getCanvasSize();
                    cropRect = cw->getViewTransform().mapRect(QRect(0, 0, cSize.width(), cSize.height()));
                }
            }
            QPixmap pix;
            if (auto *glw = qobject_cast<QOpenGLWidget*>(grabWidget)) {
                pix = QPixmap::fromImage(glw->grabFramebuffer());
            } else if (grabWidget) {
                pix = grabWidget->grab();
            } else {
                pix = mw->grab();
            }
            clean.end();
            if (!cropRect.isEmpty() && cropRect.isValid() && !pix.isNull()) {
                const QRect safeCrop = cropRect.intersected(pix.rect());
                if (!safeCrop.isEmpty()) {
                    pix = pix.copy(safeCrop);
                }
            }
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
            // recursive tree: every layer reports its 1-based row
            // within its container plus the full path ("2/1" =
            // top-level row 2, child row 1) so group-internal rows
            // can never be confused with top-level rows
            const QString script = QStringLiteral(
                "var __out = [];\n"
                "function __walk(list, parentPath, depth) {\n"
                "  for (var i = 0; i < list.length; i++) {\n"
                "    var l = list[i];\n"
                "    var row = i + 1;\n"
                "    var path = parentPath.concat([row]);\n"
                "    var kids = [];\n"
                "    if (depth < 6 && typeof l.layers === 'function') {\n"
                "      try { kids = l.layers() || []; } catch (e) { kids = []; }\n"
                "    }\n"
                "    var o = {\n"
                "      index: row,\n"
                "      row: row,\n"
                "      path: path.join('/'),\n"
                "      pathArray: path,\n"
                "      name: l.name,\n"
                "      type: l.type,\n"
                "      visible: l.visible,\n"
                "      selected: l.selected,\n"
                "      opacity: l.opacity,\n"
                "      is3D: l.is3DEnabled(),\n"
                "      inPoint: l.inPoint(),\n"
                "      outPoint: l.outPoint(),\n"
                "      depth: depth,\n"
                "      isGroup: kids.length > 0,\n"
                "      numChildren: kids.length\n"
                "    };\n"
                "    if (l.text !== undefined) { o.text = l.text; }\n"
                "    __out.push(o);\n"
                "    if (__out.length > 400) { return; }\n"
                "    if (kids.length > 0) { __walk(kids, path, depth + 1); }\n"
                "  }\n"
                "}\n"
                "var s = app.activeScene;\n"
                "if (!s) return [];\n"
                "__walk(s.layers(), [], 0);\n"
                "return __out;"
            );
            return evalScript(script, QStringLiteral("AI List Layers"));
        }

        QJsonObject McpDispatcher::toolGetLayerProperties(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name: 'name', index: 1, or layer: 'name')");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString script = QStringLiteral(
                "var l = %1;\n"
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
                "  bounds: l.bounds()\n"
                "};"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Get Layer Properties"));
        }

        QJsonObject McpDispatcher::toolCreateLayer(const QJsonObject &args)
        {
            const QString type = args.value(QStringLiteral("type")).toString(QStringLiteral("rect")).toLower().trimmed();
            const QString name = jsStr(args.value(QStringLiteral("name")).toString());

            QString script;
            if (type == QStringLiteral("rect") || type == QStringLiteral("rectangle") || type == QStringLiteral("box") || type == QStringLiteral("solid")) {
                const qreal x = args.value(QStringLiteral("x")).toDouble(0);
                const qreal y = args.value(QStringLiteral("y")).toDouble(0);
                const qreal w = args.value(QStringLiteral("width")).toDouble(args.value(QStringLiteral("w")).toDouble(200));
                const qreal h = args.value(QStringLiteral("height")).toDouble(args.value(QStringLiteral("h")).toDouble(200));
                script = QStringLiteral("var l = app.activeScene.addRect(%1, %2, %3, %4, %5);\n")
                         .arg(name, QString::number(x), QString::number(y), QString::number(w), QString::number(h));
            } else if (type == QStringLiteral("ellipse") || type == QStringLiteral("circle") || type == QStringLiteral("ball")) {
                const qreal cx = args.value(QStringLiteral("cx")).toDouble(args.value(QStringLiteral("x")).toDouble(0));
                const qreal cy = args.value(QStringLiteral("cy")).toDouble(args.value(QStringLiteral("y")).toDouble(0));
                const qreal r = args.value(QStringLiteral("radius")).toDouble(args.value(QStringLiteral("r")).toDouble(100));
                script = QStringLiteral("var l = app.activeScene.addEllipse(%1, %2, %3, %4);\n")
                         .arg(name, QString::number(cx), QString::number(cy), QString::number(r));
            } else if (type == QStringLiteral("text")) {
                const QString text = jsStr(args.value(QStringLiteral("text")).toString(QStringLiteral("Text")));
                script = QStringLiteral("var l = app.activeScene.addText(%1, %2);\n")
                         .arg(name, text);
            } else if (type == QStringLiteral("null")) {
                script = QStringLiteral("var l = app.activeScene.addNull(%1);\n").arg(name);
            } else if (type == QStringLiteral("group")) {
                script = QStringLiteral("var l = app.activeScene.addGroup(%1);\n").arg(name);
            } else if (type == QStringLiteral("layer") || type == QStringLiteral("container") || type == QStringLiteral("panel")) {
                script = QStringLiteral("var l = app.activeScene.addLayer(%1);\n").arg(name);
            } else if (type == QStringLiteral("sound") || type == QStringLiteral("audio")) {
                const QString path = jsStr(args.value(QStringLiteral("path")).toString(args.value(QStringLiteral("filePath")).toString()));
                script = QStringLiteral("var l = app.activeScene.addSound(%1, %2);\n").arg(path, name);
            } else {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Unknown layer type: %1. Valid: rect/rectangle, ellipse/circle, text, null, group, layer/container, sound/audio").arg(type);
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            // Optional styling & initial properties
            if (args.contains(QStringLiteral("fillColor")) || args.contains(QStringLiteral("fill")) || args.contains(QStringLiteral("color"))) {
                const QString col = jsStr(args.contains(QStringLiteral("fillColor")) ? args.value(QStringLiteral("fillColor")).toString() : (args.contains(QStringLiteral("fill")) ? args.value(QStringLiteral("fill")).toString() : args.value(QStringLiteral("color")).toString()));
                script += QStringLiteral("l.setFillColor(%1);\n").arg(col);
            }
            if (args.contains(QStringLiteral("strokeColor")) || args.contains(QStringLiteral("stroke"))) {
                const QString col = jsStr(args.contains(QStringLiteral("strokeColor")) ? args.value(QStringLiteral("strokeColor")).toString() : args.value(QStringLiteral("stroke")).toString());
                script += QStringLiteral("l.setStrokeColor(%1);\n").arg(col);
            }
            if (args.contains(QStringLiteral("strokeWidth"))) {
                script += QStringLiteral("l.setStrokeWidth(%1);\n").arg(args.value(QStringLiteral("strokeWidth")).toDouble());
            }
            if (args.contains(QStringLiteral("fontSize"))) {
                script += QStringLiteral("l.setFontSize(%1);\n").arg(args.value(QStringLiteral("fontSize")).toDouble());
            }
            if (args.contains(QStringLiteral("fontFamily")) || args.contains(QStringLiteral("font"))) {
                const QString f = jsStr(args.contains(QStringLiteral("fontFamily")) ? args.value(QStringLiteral("fontFamily")).toString() : args.value(QStringLiteral("font")).toString());
                script += QStringLiteral("l.setFontFamily(%1);\n").arg(f);
            }
            if (args.contains(QStringLiteral("opacity"))) {
                script += QStringLiteral("l.opacity = %1;\n").arg(normOpacity(args.value(QStringLiteral("opacity")).toDouble()));
            }
            if (args.contains(QStringLiteral("is3D")) || args.contains(QStringLiteral("3d"))) {
                const bool is3d = args.contains(QStringLiteral("is3D")) ? args.value(QStringLiteral("is3D")).toBool() : args.value(QStringLiteral("3d")).toBool();
                script += QStringLiteral("l.set3DEnabled(%1);\n").arg(is3d ? QStringLiteral("true") : QStringLiteral("false"));
            }
            if (args.contains(QStringLiteral("parent"))) {
                bool pOk = false;
                const QString pRef = layerRefExpr(args, QStringLiteral("parent"), &pOk);
                if (pOk) {
                    // JsLayerProxy::setParentLayer is the real API;
                    // the scene proxy has no setParent method
                    script += QStringLiteral("l.setParentLayer(%1);\n").arg(pRef);
                }
            }

            script += QStringLiteral("return { name: l.name, index: l.index, type: l.type, visible: l.visible };");
            return evalScript(script, QStringLiteral("AI Create Layer"));
        }

        QJsonObject McpDispatcher::toolDuplicateLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name: 'name', index: 1, or layer: 'name')");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "var dup = lay.duplicate(); return { name: dup.name, index: dup.index };"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Duplicate Layer"));
        }

        QJsonObject McpDispatcher::toolDeleteLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name: 'name', index: 1, or layer: 'name')");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "lay.remove(); return 'OK';"
            ).arg(layerRef);

            return evalScript(script, QStringLiteral("AI Delete Layer"));
        }

        QJsonObject McpDispatcher::toolSetParentLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify child layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            bool parentOk = false;
            const QString parentRef = layerRefExpr(args, QStringLiteral("parent"), &parentOk);

            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "lay.setParentLayer(%2); return 'OK';"
            ).arg(layerRef, parentRef);

            return evalScript(script, QStringLiteral("AI Set Parent Layer"));
        }

        QJsonObject McpDispatcher::toolSet3DMode(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const bool enabled = args.value(QStringLiteral("enabled")).toBool(true);
            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "lay.set3DEnabled(%2); return 'OK';"
            ).arg(layerRef, enabled ? QStringLiteral("true") : QStringLiteral("false"));

            return evalScript(script, QStringLiteral("AI Set 3D Mode"));
        }

        QJsonObject McpDispatcher::toolSetProperty(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
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
                "var lay = %1;\n"
                "var p = __findProp(lay, %2);\n"
                "p.setValue(%3); return 'OK';"
            ).arg(layerRef, jsStr(prop), valStr);

            return evalScript(script, QStringLiteral("AI Set Property"));
        }

        QJsonObject McpDispatcher::toolSetKeyframe(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
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
                    setCall = QStringLiteral("p.setValueAtFrameWithEasing(%1, %2, %3);").arg(QString::number(frame), valStr, jsStr(easing));
                } else {
                    setCall = QStringLiteral("p.setValueAtFrame(%1, %2);").arg(QString::number(frame), valStr);
                }
            } else {
                const qreal time = args.value(QStringLiteral("time")).toDouble(0);
                if (!easing.isEmpty()) {
                    setCall = QStringLiteral("p.setValueAtTimeWithEasing(%1, %2, %3);").arg(QString::number(time), valStr, jsStr(easing));
                } else {
                    setCall = QStringLiteral("p.setValueAtTime(%1, %2);").arg(QString::number(time), valStr);
                }
            }

            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "var p = __findProp(lay, %2);\n"
                "%3 return 'OK';"
            ).arg(layerRef, jsStr(prop), setCall);

            return evalScript(script, QStringLiteral("AI Set Keyframe"));
        }

        QJsonObject McpDispatcher::toolRemoveKeyframe(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString prop = args.value(QStringLiteral("property")).toString();
            // frame from time uses the scene's real fps (a hardcoded
            // 60 corrupted 24/25/30 fps projects before)
            QString frameExpr;
            if (args.contains(QStringLiteral("frame"))) {
                frameExpr = QString::number(args.value(QStringLiteral("frame")).toInt());
            } else {
                frameExpr = QStringLiteral("Math.round(%1 * app.activeScene.fps)")
                        .arg(args.value(QStringLiteral("time")).toDouble());
            }

            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "var p = __findProp(lay, %2);\n"
                "p.removeKeyAtFrame(%3); return 'OK';"
            ).arg(layerRef, jsStr(prop), frameExpr);

            return evalScript(script, QStringLiteral("AI Remove Keyframe"));
        }

        QJsonObject McpDispatcher::toolClearKeyframes(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString prop = args.value(QStringLiteral("property")).toString();
            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "var p = __findProp(lay, %2);\n"
                "while (p.numKeys > 0) {\n"
                "  p.removeKeyAtFrame(p.keyFrame(1));\n"
                "}\n"
                "return 'OK';"
            ).arg(layerRef, jsStr(prop));

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

            // call the same toggle the Space key uses; synthesizing a
            // raw key event made playback depend on widget focus (a
            // focused text field would just receive a space)
            auto *mainWin = qobject_cast<MainWindow*>(mw);
            auto timeline = mainWin ? mainWin->getTimeLineWidget() : nullptr;
            if (!timeline) {
                resp[QStringLiteral("error")] = QStringLiteral("Timeline widget unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            timeline->spaceToggle();

            bool playing = false;
            if (RenderHandler::sInstance) {
                playing = RenderHandler::sInstance->currentPreviewState() == PreviewState::playing;
            }
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("playing")] = playing;
            return resp;
        }

        QJsonObject McpDispatcher::toolSetInOutPoint(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
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
                "var lay = %1;\n"
                "%2"
                "return { name: lay.name, inPoint: lay.inPoint(), outPoint: lay.outPoint() };"
            ).arg(layerRef, setCalls);

            return evalScript(script, QStringLiteral("AI Set In Out Point"));
        }

        QJsonObject McpDispatcher::toolSetTextProperties(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QString script = QStringLiteral("var lay = %1;\n").arg(layerRef);
            if (args.contains(QStringLiteral("text"))) {
                script += QStringLiteral("lay.setText(%1);\n").arg(jsStr(args.value(QStringLiteral("text")).toString()));
            }
            if (args.contains(QStringLiteral("fontSize"))) {
                script += QStringLiteral("lay.setFontSize(%1);\n").arg(args.value(QStringLiteral("fontSize")).toDouble());
            }
            if (args.contains(QStringLiteral("fontFamily")) || args.contains(QStringLiteral("font"))) {
                QString fn = args.contains(QStringLiteral("fontFamily")) ? args.value(QStringLiteral("fontFamily")).toString() : args.value(QStringLiteral("font")).toString();
                script += QStringLiteral("lay.setFontFamily(%1);\n").arg(jsStr(fn));
            }
            if (args.contains(QStringLiteral("letterSpacing"))) {
                script += QStringLiteral("lay.setLetterSpacing(%1);\n").arg(args.value(QStringLiteral("letterSpacing")).toDouble());
            }
            if (args.contains(QStringLiteral("lineSpacing"))) {
                script += QStringLiteral("lay.setLineSpacing(%1);\n").arg(args.value(QStringLiteral("lineSpacing")).toDouble());
            }
            if (args.contains(QStringLiteral("alignment"))) {
                script += QStringLiteral("lay.setTextAlignment(%1);\n").arg(jsStr(args.value(QStringLiteral("alignment")).toString()));
            }
            if (args.contains(QStringLiteral("color"))) {
                script += QStringLiteral("lay.setFillColor(%1);\n").arg(jsStr(args.value(QStringLiteral("color")).toString()));
            }
            script += QStringLiteral("return { success: true, name: lay.name, index: lay.index };\n");

            return evalScript(script, QStringLiteral("Set Text Properties"));
        }

        QJsonObject McpDispatcher::toolSetLayerStyle(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            QString script = QStringLiteral("var lay = %1;\n").arg(layerRef);
            if (args.contains(QStringLiteral("fillColor"))) {
                script += QStringLiteral("lay.setFillColor(%1);\n").arg(jsStr(args.value(QStringLiteral("fillColor")).toString()));
            }
            if (args.contains(QStringLiteral("strokeColor"))) {
                script += QStringLiteral("lay.setStrokeColor(%1);\n").arg(jsStr(args.value(QStringLiteral("strokeColor")).toString()));
            }
            if (args.contains(QStringLiteral("strokeWidth"))) {
                script += QStringLiteral("lay.setStrokeWidth(%1);\n").arg(args.value(QStringLiteral("strokeWidth")).toDouble());
            }
            if (args.contains(QStringLiteral("blendMode")) || args.contains(QStringLiteral("blend"))) {
                const QString bm = args.contains(QStringLiteral("blendMode")) ? args.value(QStringLiteral("blendMode")).toString() : args.value(QStringLiteral("blend")).toString();
                script += QStringLiteral("lay.setBlendMode(%1);\n").arg(jsStr(bm));
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
            const QString layerRef = layerRefExpr(args, QString(), &ok);
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
                "var lay = %1;\n"
                "%2\n"
                "return { name: lay.name, index: lay.index, action: '%3' };"
            ).arg(layerRef, call, action);

            return evalScript(script, QStringLiteral("AI Set Layer Order"));
        }

        QJsonObject McpDispatcher::toolSetLayerVisibility(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const bool visible = args.value(QStringLiteral("visible")).toBool(true);
            const QString script = QStringLiteral("var lay = %1;\nlay.visible = %2;\nreturn { name: lay.name, visible: lay.visible };\n").arg(layerRef).arg(visible ? QStringLiteral("true") : QStringLiteral("false"));
            return evalScript(script, QStringLiteral("Set Layer Visibility"));
        }

        QJsonObject McpDispatcher::toolSetLayerLock(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const bool locked = args.value(QStringLiteral("locked")).toBool(true);
            const QString script = QStringLiteral("var lay = %1;\nlay.setLocked(%2);\nreturn { name: lay.name, locked: lay.isLocked() };\n").arg(layerRef).arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
            return evalScript(script, QStringLiteral("Set Layer Lock"));
        }

        QJsonObject McpDispatcher::toolSetKeyframeEasing(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
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
                "var lay = %1;\n"
                "var p = __findProp(lay, %2);\n"
                "var ok = p.setEasing(%3, %4, %5);\n"
                "return { name: lay.name, property: %2, easing: %3, success: ok };"
            ).arg(layerRef, jsStr(prop), jsStr(easing), startF, endF);

            return evalScript(script, QStringLiteral("AI Set Keyframe Easing"));
        }

        QJsonObject McpDispatcher::toolListAvailableEffects(const QJsonObject &)
        {
            QJsonObject resp;
            // enumerated from the shared name/type table consumed by
            // addEffect() (jsapi), so the list can never go stale
            // relative to what the engine actually accepts
            const auto effectNames = Friction::Core::knownEffectNames();
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("effects")] = QJsonArray::fromStringList(effectNames);
            return resp;
        }

        QJsonObject McpDispatcher::toolAddRasterEffect(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString effectType = args.value(QStringLiteral("effectType")).toString(QStringLiteral("glow"));
            const QString script = QStringLiteral("var lay = %1;\nvar ok = lay.addEffect(%2);\nreturn { name: lay.name, effect: %2, success: ok };\n").arg(layerRef, jsStr(effectType));
            return evalScript(script, QStringLiteral("Add Raster Effect"));
        }

        QJsonObject McpDispatcher::toolRemoveRasterEffect(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const int effectIndex = args.value(QStringLiteral("effectIndex")).toInt(0);
            const QString script = QStringLiteral("var lay = %1;\nvar ok = lay.removeEffect(%2);\nreturn { name: lay.name, removedIndex: %2, success: ok };\n").arg(layerRef).arg(effectIndex);
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

        QJsonObject McpDispatcher::toolImportFile(const QJsonObject &args)
        {
            const QString path = args.value(QStringLiteral("path")).toString(args.value(QStringLiteral("filePath")).toString());
            if (path.isEmpty()) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Missing path parameter");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (!QFileInfo::exists(path)) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("File does not exist: %1").arg(path);
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            auto *canvas = activeScene();
            if (!canvas) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (Actions::sInstance) {
                auto *imported = Actions::sInstance->importFile(path, canvas->getCurrentGroup());
                QJsonObject resp;
                resp[QStringLiteral("success")] = (imported != nullptr);
                if (imported) {
                    const QString customName = args.value(QStringLiteral("name")).toString();
                    if (!customName.isEmpty()) {
                        imported->prp_setName(customName);
                    }
                    resp[QStringLiteral("name")] = imported->prp_getName();
                } else {
                    resp[QStringLiteral("error")] = QStringLiteral("Failed to import file");
                }
                return resp;
            }
            QJsonObject resp;
            resp[QStringLiteral("error")] = QStringLiteral("Actions unavailable");
            resp[QStringLiteral("success")] = false;
            return resp;
        }

        // stable cache dir for AI-authored lottie json so updates can
        // rewrite the same file the layer points at
        static QString mcpLottieCacheDir()
        {
            const QString base = QDir::temp().absoluteFilePath(
                        QStringLiteral("friction-mcp-lottie"));
            QDir().mkpath(base);
            return base;
        }

        static bool writeLottieJsonToCache(const QString &json,
                                           QString &pathOut,
                                           QString &errOut)
        {
            QJsonParseError parseErr;
            const auto doc = QJsonDocument::fromJson(json.toUtf8(),
                                                     &parseErr);
            if (parseErr.error != QJsonParseError::NoError
                    || !doc.isObject()) {
                errOut = QStringLiteral("Invalid Lottie JSON: %1")
                        .arg(parseErr.errorString());
                return false;
            }
            static int counter = 0;
            const QString path = mcpLottieCacheDir() +
                    QStringLiteral("/lottie-%1.json")
                        .arg(++counter, 4, 10, QLatin1Char('0'));
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                errOut = QStringLiteral("Cannot write %1").arg(path);
                return false;
            }
            file.write(json.toUtf8());
            file.close();
            pathOut = path;
            return true;
        }

        QJsonObject McpDispatcher::toolCreateLottieLayer(const QJsonObject &args)
        {
            const QString json = args.value(QStringLiteral("json")).toString();
            QString path = args.value(QStringLiteral("path")).toString();
            QJsonObject resp;
            if (json.isEmpty() && path.isEmpty()) {
                resp[QStringLiteral("error")] = QStringLiteral("Missing json or path parameter");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (!LottieLib::available()) {
                resp[QStringLiteral("error")] = QStringLiteral(
                            "Lottie runtime not available (frictionskottie library missing)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (json.isEmpty()) {
                if (!QFileInfo::exists(path)) {
                    resp[QStringLiteral("error")] = QStringLiteral(
                                "File does not exist: %1").arg(path);
                    resp[QStringLiteral("success")] = false;
                    return resp;
                }
            } else {
                QString err;
                if (!writeLottieJsonToCache(json, path, err)) {
                    resp[QStringLiteral("error")] = err;
                    resp[QStringLiteral("success")] = false;
                    return resp;
                }
            }
            auto *canvas = activeScene();
            if (!canvas) {
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (Actions::sInstance) {
                // importFile sniffs the json and routes to a LottieBox
                auto *imported = Actions::sInstance->importFile(
                            path, canvas->getCurrentGroup());
                const auto lottie = enve_cast<LottieBox*>(imported);
                if (!lottie) {
                    resp[QStringLiteral("error")] = QStringLiteral(
                                "Import did not produce a Lottie layer "
                                "(is the json a valid bodymovin document?)");
                    resp[QStringLiteral("success")] = false;
                    return resp;
                }
                const QString customName = args.value(QStringLiteral("name")).toString();
                if (!customName.isEmpty()) {
                    imported->prp_setName(customName);
                }
                resp[QStringLiteral("success")] = true;
                resp[QStringLiteral("name")] = imported->prp_getName();
                resp[QStringLiteral("path")] = path;
                resp[QStringLiteral("fps")] = lottie->getLottieFps();
                return resp;
            }
            resp[QStringLiteral("error")] = QStringLiteral("Actions unavailable");
            resp[QStringLiteral("success")] = false;
            return resp;
        }

        QJsonObject McpDispatcher::toolUpdateLottieLayer(const QJsonObject &args)
        {
            const QString json = args.value(QStringLiteral("json")).toString();
            QString path = args.value(QStringLiteral("path")).toString();
            QJsonObject resp;
            if (json.isEmpty() && path.isEmpty()) {
                resp[QStringLiteral("error")] = QStringLiteral("Missing json or path parameter");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (!LottieLib::available()) {
                resp[QStringLiteral("error")] = QStringLiteral(
                            "Lottie runtime not available (frictionskottie library missing)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            auto *canvas = activeScene();
            if (!canvas) {
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            bool refOk = false;
            const auto box = resolveLayerRefCxx(args, canvas->getCurrentGroup(),
                                                QString(), &refOk);
            const auto lottie = enve_cast<LottieBox*>(box);
            if (!refOk || !lottie) {
                resp[QStringLiteral("error")] = QStringLiteral(
                            "Layer not found or not a Lottie layer");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (json.isEmpty()) {
                if (!QFileInfo::exists(path)) {
                    resp[QStringLiteral("error")] = QStringLiteral(
                                "File does not exist: %1").arg(path);
                    resp[QStringLiteral("success")] = false;
                    return resp;
                }
            } else {
                // only rewrite in place when the layer points into the
                // MCP cache; user files always get a fresh cache copy
                const QString current = lottie->filePath();
                if (current.startsWith(mcpLottieCacheDir())) {
                    path = current;
                    QFile file(path);
                    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        resp[QStringLiteral("error")] = QStringLiteral(
                                    "Cannot write %1").arg(path);
                        resp[QStringLiteral("success")] = false;
                        return resp;
                    }
                    file.write(json.toUtf8());
                    file.close();
                } else {
                    QString err;
                    if (!writeLottieJsonToCache(json, path, err)) {
                        resp[QStringLiteral("error")] = err;
                        resp[QStringLiteral("success")] = false;
                        return resp;
                    }
                }
            }
            // setFilePathNoRename reloads the animation and
            // invalidates the whole influence range itself
            lottie->setFilePathNoRename(path);
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("name")] = lottie->prp_getName();
            resp[QStringLiteral("path")] = path;
            resp[QStringLiteral("fps")] = lottie->getLottieFps();
            return resp;
        }

        QJsonObject McpDispatcher::toolSetMarker(const QJsonObject &args)
        {
            auto *canvas = activeScene();
            if (!canvas) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            const int frame = args.value(QStringLiteral("frame")).toInt();
            const QString title = args.value(QStringLiteral("title")).toString();
            canvas->setMarker(title, frame);
            QJsonObject resp;
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("frame")] = frame;
            resp[QStringLiteral("title")] = title;
            return resp;
        }

        QJsonObject McpDispatcher::toolClearMarkers(const QJsonObject &)
        {
            auto *canvas = activeScene();
            if (!canvas) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            canvas->clearMarkers();
            QJsonObject resp;
            resp[QStringLiteral("success")] = true;
            return resp;
        }

        QJsonObject McpDispatcher::toolListMarkers(const QJsonObject &)
        {
            auto *canvas = activeScene();
            if (!canvas) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            QJsonArray markersArr;
            for (const auto &m : canvas->getMarkers()) {
                QJsonObject obj;
                obj[QStringLiteral("title")] = m.title;
                obj[QStringLiteral("frame")] = m.frame;
                obj[QStringLiteral("enabled")] = m.enabled;
                markersArr.append(obj);
            }
            QJsonObject resp;
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("markers")] = markersArr;
            return resp;
        }

        void McpDispatcher::toolRenderMarkupAsync(const QJsonObject &args,
                                                  const std::function<void(const QJsonObject&)> &callback)
        {
            const QString markup = args.value(QStringLiteral("markup")).toString().trimmed();
            if (markup.isEmpty()) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("Markup is empty");
                resp[QStringLiteral("success")] = false;
                callback(resp);
                return;
            }

            const QString mode = args.value(QStringLiteral("mode")).toString(QStringLiteral("replace")).toLower();
            const qreal timeOffset = args.value(QStringLiteral("timeOffset")).toDouble(0.0);

            const QString scriptPath = findMarkupScriptPath();
            if (scriptPath.isEmpty()) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("friction_markup.py compiler script not found");
                resp[QStringLiteral("success")] = false;
                callback(resp);
                return;
            }

            // interpreter candidates in order of preference; the
            // Windows "py" launcher needs an explicit -3
            QStringList pyCandidates;
            const auto foundP3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
            const auto foundPy = QStandardPaths::findExecutable(QStringLiteral("python"));
            const auto foundLauncher = QStandardPaths::findExecutable(QStringLiteral("py"));
            if (!foundP3.isEmpty()) { pyCandidates << foundP3; }
            if (!foundPy.isEmpty()) { pyCandidates << foundPy; }
            if (!foundLauncher.isEmpty()) { pyCandidates << foundLauncher; }
            pyCandidates << QStringLiteral("python3") << QStringLiteral("python") << QStringLiteral("py");
            pyCandidates.removeDuplicates();

            const auto candIdx = std::make_shared<int>(0);
            const auto timedOut = std::make_shared<bool>(false);
            const auto responded = std::make_shared<bool>(false);

            auto *proc = new QProcess(this);
            auto *timeout = new QTimer(proc); // child of proc: cleaned up together
            timeout->setSingleShot(true);

            const QString groupName = (mode == QStringLiteral("append"))
                    ? QStringLiteral("AI Append Markup")
                    : QStringLiteral("AI Render Markup");

            // single-response guard: several signal paths (error,
            // finished, timeout) can race after a kill
            const auto finishWith = [callback, responded, proc, timeout](QJsonObject resp) {
                if (*responded) { return; }
                *responded = true;
                timeout->stop();
                proc->deleteLater();
                callback(resp);
            };

            // feeds the markup via stdin once the process is alive
            connect(proc, &QProcess::started, this, [proc, markup, timeout]() {
                proc->write(markup.toUtf8());
                proc->closeWriteChannel();
                timeout->start(15000);
            });

            connect(timeout, &QTimer::timeout, this, [proc, timedOut]() {
                *timedOut = true;
                proc->kill();
            });

            // tries the next interpreter; returns false when the
            // candidate list is exhausted
            const auto startAttempt = [proc, pyCandidates, candIdx, scriptPath, mode, timeOffset]() -> bool {
                while (*candIdx < pyCandidates.size()) {
                    const QString exe = pyCandidates.at((*candIdx)++);
                    QStringList procArgs;
                    if (QFileInfo(exe).completeBaseName().compare(
                                QStringLiteral("py"), Qt::CaseInsensitive) == 0) {
                        procArgs << QStringLiteral("-3");
                    }
                    procArgs << scriptPath << QStringLiteral("--compile-only")
                             << QStringLiteral("--mode") << mode
                             << QStringLiteral("--time-offset") << QString::number(timeOffset)
                             << QStringLiteral("-");
                    proc->start(exe, procArgs);
                    return true;
                }
                return false;
            };

            connect(proc, &QProcess::errorOccurred,
                    this, [startAttempt, finishWith](const QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) { return; }
                if (startAttempt()) { return; }
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral(
                            "Failed to start python (tried python3, python, py); install Python 3 "
                            "or compile the markup externally and submit it via friction_eval_script");
                resp[QStringLiteral("success")] = false;
                finishWith(resp);
            });

            connect(proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    this, [this, proc, timedOut, finishWith, mode, timeOffset, groupName]
                    (const int exitCode, const QProcess::ExitStatus) {
                if (*timedOut) {
                    QJsonObject resp;
                    resp[QStringLiteral("error")] = QStringLiteral("friction_markup.py compilation timed out");
                    resp[QStringLiteral("success")] = false;
                    finishWith(resp);
                    return;
                }
                const QString stdErr = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                const QString compiledJs = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                if (exitCode != 0 || compiledJs.isEmpty()) {
                    QJsonObject resp;
                    resp[QStringLiteral("error")] = stdErr.isEmpty() ?
                                QStringLiteral("Markup compilation failed (empty output)") : stdErr;
                    resp[QStringLiteral("success")] = false;
                    finishWith(resp);
                    return;
                }
                QJsonObject evalResult = evalScript(compiledJs, groupName);
                evalResult[QStringLiteral("mode")] = mode;
                evalResult[QStringLiteral("timeOffset")] = timeOffset;
                finishWith(evalResult);
            });

            if (!startAttempt()) {
                QJsonObject resp;
                resp[QStringLiteral("error")] = QStringLiteral("No python interpreter available");
                resp[QStringLiteral("success")] = false;
                callback(resp);
            }
        }

        QJsonObject McpDispatcher::toolUpdateLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                err[QStringLiteral("success")] = false;
                return err;
            }

            QStringList ops;
            if (args.contains(QStringLiteral("newName"))) {
                ops << QStringLiteral("lay.name = %1;").arg(jsStr(args.value(QStringLiteral("newName")).toString()));
            }
            if (args.contains(QStringLiteral("text"))) {
                ops << QStringLiteral("lay.setText(%1);").arg(jsStr(args.value(QStringLiteral("text")).toString()));
            }
            if (args.contains(QStringLiteral("fontSize"))) {
                ops << QStringLiteral("lay.setFontSize(%1);").arg(args.value(QStringLiteral("fontSize")).toDouble());
            }
            if (args.contains(QStringLiteral("fontFamily")) || args.contains(QStringLiteral("font"))) {
                QString f = args.contains(QStringLiteral("fontFamily")) ? args.value(QStringLiteral("fontFamily")).toString() : args.value(QStringLiteral("font")).toString();
                ops << QStringLiteral("lay.setFontFamily(%1);").arg(jsStr(f));
            }
            if (args.contains(QStringLiteral("alignment"))) {
                ops << QStringLiteral("lay.setTextAlignment(%1);").arg(jsStr(args.value(QStringLiteral("alignment")).toString()));
            }
            if (args.contains(QStringLiteral("fillColor")) || args.contains(QStringLiteral("color"))) {
                const QString c = args.contains(QStringLiteral("fillColor")) ? args.value(QStringLiteral("fillColor")).toString() : args.value(QStringLiteral("color")).toString();
                ops << QStringLiteral("lay.setFillColor(%1);").arg(jsStr(c));
            }
            if (args.contains(QStringLiteral("strokeColor"))) {
                ops << QStringLiteral("lay.setStrokeColor(%1);").arg(jsStr(args.value(QStringLiteral("strokeColor")).toString()));
            }
            if (args.contains(QStringLiteral("strokeWidth"))) {
                ops << QStringLiteral("lay.setStrokeWidth(%1);").arg(args.value(QStringLiteral("strokeWidth")).toDouble());
            }
            if (args.contains(QStringLiteral("blendMode"))) {
                ops << QStringLiteral("lay.setBlendMode(%1);").arg(jsStr(args.value(QStringLiteral("blendMode")).toString()));
            }
            if (args.contains(QStringLiteral("cornerRadius")) || args.contains(QStringLiteral("radius"))) {
                const double r = args.contains(QStringLiteral("cornerRadius")) ? args.value(QStringLiteral("cornerRadius")).toDouble() : args.value(QStringLiteral("radius")).toDouble();
                ops << QStringLiteral("lay.setCornerRadius(%1);").arg(r);
            }
            if (args.contains(QStringLiteral("opacity"))) {
                ops << QStringLiteral("lay.opacity = %1;").arg(normOpacity(args.value(QStringLiteral("opacity")).toDouble()));
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
                const QString pRef = layerRefExpr(args, QStringLiteral("parent"), &pOk);
                const auto parentVal = args.value(QStringLiteral("parent"));
                QString nullishText;
                if (parentVal.isString()) { nullishText = parentVal.toString().trimmed(); }
                const QString nameVal = args.value(QStringLiteral("parentName")).toString().trimmed().toLower();
                const bool wantsUnparent =
                        (nullishText.isEmpty() ||
                         nullishText.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0 ||
                         nullishText.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) &&
                        nameVal.isEmpty();
                if (!wantsUnparent && pOk) {
                    ops << QStringLiteral("lay.setParentLayer(%1);").arg(pRef);
                } else {
                    ops << QStringLiteral("lay.setParentLayer(null);");
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
                "var lay = %1;\n"
                "%2\n"
                "return { success: true, name: lay.name, index: lay.index, visible: lay.visible, opacity: lay.opacity };"
            ).arg(layerRef, ops.join(QStringLiteral("\n")));

            return evalScript(script, QStringLiteral("AI Update Layer"));
        }

        QJsonObject McpDispatcher::toolAnimateLayer(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
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
                "var lay = %1;\n"
                "var fps = (app.activeScene && app.activeScene.fps) ? app.activeScene.fps : 60;\n"
                "var startF = (%2 >= 0) ? %2 : ((%3 >= 0) ? Math.round(%3 * fps) : 0);\n"
                "var dur = (%4 > 0) ? %4 : ((%5 > 0) ? Math.round(%5 * fps) : 25);\n"
                "var endF = startF + dur;\n"
                "var isOut = %6;\n"
                "var userEasing = %7;\n"
                "var dist = %8;\n"
                "var preset = %9;\n"
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
                 jsStr(userEasing), QString::number(distance), jsStr(preset));

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
                // bounded settle loop lets scheduled preview renders
                // flush; re-entrant tool calls arriving here are
                // rejected by the dispatcher guard
                settleCanvas();

                CleanCanvasGrab clean;
                if (auto *cw = qobject_cast<CanvasWindow*>(grabWidget)) {
                    clean.begin(cw->getCurrentCanvas());
                }
                QPixmap pix = grabWidget->grab();
                clean.end();
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
            settleCanvas();

            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("format")] = format;
            resp[QStringLiteral("storyboard")] = storyboard;
            return resp;
        }

        QJsonObject McpDispatcher::toolListAnimPresets(const QJsonObject &args)
        {
            const QString kindFilter = args.value(QStringLiteral("kind")).toString().toLower();
            QJsonObject resp;
            QJsonArray layerArr;
            QJsonArray textArr;
            if (kindFilter.isEmpty() || kindFilter == QStringLiteral("layer")) {
                for (const auto &p : LayerAnimPresets::all()) {
                    QJsonObject o;
                    o[QStringLiteral("id")] = p.id;
                    o[QStringLiteral("name")] = p.name;
                    o[QStringLiteral("desc")] = p.desc;
                    o[QStringLiteral("category")] = (p.category == 2) ? QStringLiteral("loop") : QStringLiteral("inout");
                    o[QStringLiteral("duration")] = p.duration;
                    o[QStringLiteral("tag")] = p.tag;
                    o[QStringLiteral("supportsOut")] = (p.outGen != nullptr);
                    layerArr.append(o);
                }
            }
            if (kindFilter.isEmpty() || kindFilter == QStringLiteral("text")) {
                for (const auto &p : TextAnimPresets::all()) {
                    QJsonObject o;
                    o[QStringLiteral("id")] = p.id;
                    o[QStringLiteral("name")] = p.name;
                    o[QStringLiteral("desc")] = p.desc;
                    o[QStringLiteral("category")] = (p.category == 2) ? QStringLiteral("loop") : QStringLiteral("inout");
                    o[QStringLiteral("fragment")] = (p.fragment == 0) ? QStringLiteral("letters") :
                                                    (p.fragment == 1) ? QStringLiteral("words") : QStringLiteral("lines");
                    o[QStringLiteral("duration")] = p.duration;
                    o[QStringLiteral("tag")] = p.tag;
                    o[QStringLiteral("supportsOut")] = p.supportsOut;
                    textArr.append(o);
                }
            }
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("layerPresets")] = layerArr;
            resp[QStringLiteral("textPresets")] = textArr;
            return resp;
        }

        QJsonObject McpDispatcher::toolApplyAnimPreset(const QJsonObject &args)
        {
            QJsonObject resp;
            auto scene = activeScene();
            if (!scene) {
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString kind = args.value(QStringLiteral("kind")).toString(QStringLiteral("layer")).toLower();
            const QString presetId = args.value(QStringLiteral("preset")).toString(
                        args.value(QStringLiteral("presetId")).toString()).trimmed();
            if (presetId.isEmpty()) {
                resp[QStringLiteral("error")] = QStringLiteral("preset is required (list ids via friction_list_anim_presets)");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const QString direction = args.value(QStringLiteral("direction")).toString(QStringLiteral("in")).toLower();
            if (direction != QStringLiteral("in") && direction != QStringLiteral("out") &&
                    direction != QStringLiteral("both")) {
                resp[QStringLiteral("error")] = QStringLiteral("direction must be in, out or both");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            const qreal fps = scene->getFps() > 0 ? scene->getFps() : 60.0;
            const qreal durationScale = args.value(QStringLiteral("durationScale")).toDouble(1.0);

            int baseFrame = 0;
            if (args.contains(QStringLiteral("startFrame"))) {
                baseFrame = args.value(QStringLiteral("startFrame")).toInt();
            } else if (args.contains(QStringLiteral("startTime"))) {
                baseFrame = qRound(args.value(QStringLiteral("startTime")).toDouble() * fps);
            }
            int outBase = -1;
            if (args.contains(QStringLiteral("outFrame"))) {
                outBase = args.value(QStringLiteral("outFrame")).toInt();
            } else if (args.contains(QStringLiteral("outTime"))) {
                outBase = qRound(args.value(QStringLiteral("outTime")).toDouble() * fps);
            }

            // rhythm: stagger each subsequent layer so MG scenes do
            // not animate all at once; 8 frames is the sane default.
            // timeline mode moves each LAYER later on the timeline
            // (fragment + keyframes together, the drag gesture
            // equivalent); preset mode only offsets the animation
            // start frame
            const QString order = args.value(QStringLiteral("order")).toString(QStringLiteral("top")).toLower();
            const QString staggerMode = args.value(QStringLiteral("staggerMode"))
                    .toString(QStringLiteral("timeline")).toLower();
            int stagger = -1;
            if (args.contains(QStringLiteral("staggerFrames"))) {
                stagger = args.value(QStringLiteral("staggerFrames")).toInt();
            } else if (args.contains(QStringLiteral("staggerSeconds"))) {
                stagger = qRound(args.value(QStringLiteral("staggerSeconds")).toDouble() * fps);
            } else {
                stagger = 8;
            }
            if (stagger < 0) { stagger = 0; }

            // targets: one addressed layer, or every top-level row
            QVector<BoundingBox*> targets;
            const bool scopeAll = args.value(QStringLiteral("scope")).toString().toLower() == QStringLiteral("all");
            if (scopeAll) {
                const auto &contained = scene->getContained();
                for (const auto &child : contained) {
                    const auto box = enve_cast<BoundingBox*>(child.get());
                    if (box) { targets.append(box); }
                }
            } else {
                bool refOk = false;
                const auto box = resolveLayerRefCxx(args, scene, QString(), &refOk);
                if (!refOk || !box) {
                    resp[QStringLiteral("error")] = QStringLiteral(
                                "Must address a layer (index / path \"2/1\" / name) or pass scope:\"all\"");
                    resp[QStringLiteral("success")] = false;
                    return resp;
                }
                targets.append(box);
            }
            if (targets.isEmpty()) {
                resp[QStringLiteral("error")] = QStringLiteral("No layers to animate");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            if (kind == QStringLiteral("text")) {
                const auto *preset = TextAnimPresets::byId(presetId);
                if (!preset) {
                    resp[QStringLiteral("error")] = QStringLiteral("Unknown text preset: %1 (list ids via friction_list_anim_presets)").arg(presetId);
                    resp[QStringLiteral("success")] = false;
                    return resp;
                }
                if (direction == QStringLiteral("out") && !preset->supportsOut) {
                    resp[QStringLiteral("error")] = QStringLiteral("Preset %1 does not support the out direction").arg(presetId);
                    resp[QStringLiteral("success")] = false;
                    return resp;
                }
                Friction::Core::beginUndoGroupBatch();
                QJsonArray applied;
                int skipped = 0;
                for (int i = 0; i < targets.count(); ++i) {
                    auto *box = targets.at(i);
                    const int slot = (order == QStringLiteral("bottom")) ?
                                (targets.count() - 1 - i) : i;
                    const int start = (staggerMode == QStringLiteral("preset")) ?
                                baseFrame + slot * stagger : baseFrame;
                    auto *tb = enve_cast<TextBox*>(box);
                    if (!tb) { skipped++; continue; }
                    const bool ok = TextAnimPresets::apply(tb, *preset, start, fps,
                                                           durationScale,
                                                           direction == QStringLiteral("out"));
                    if (ok) {
                        applied.append(QJsonObject{
                            {QStringLiteral("row"), i + 1},
                            {QStringLiteral("name"), box->prp_getName()},
                            {QStringLiteral("startFrame"), start + slot * stagger}});
                    } else { skipped++; }
                }
                if (staggerMode != QStringLiteral("preset") && stagger > 0) {
                    staggerTargetsInTime(targets, stagger, order);
                    if (args.value(QStringLiteral("extendScene")).toBool(true)) {
                        const auto range = scene->getFrameRange();
                        scene->setFrameRange({range.fMin,
                                              range.fMax + (targets.count() - 1) * stagger});
                    }
                }
                Friction::Core::endUndoGroupBatch();
                resp[QStringLiteral("success")] = true;
                resp[QStringLiteral("applied")] = applied;
                resp[QStringLiteral("skipped")] = skipped;
                resp[QStringLiteral("staggerMode")] = QStringLiteral("timeline");
                return resp;
            }

            // layer presets (any layer type)
            const auto *preset = LayerAnimPresets::byId(presetId);
            if (!preset) {
                resp[QStringLiteral("error")] = QStringLiteral("Unknown layer preset: %1 (list ids via friction_list_anim_presets)").arg(presetId);
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (direction == QStringLiteral("out") && !preset->outGen) {
                resp[QStringLiteral("error")] = QStringLiteral("Preset %1 has no exit variant").arg(presetId);
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            if (direction == QStringLiteral("both") && outBase < 0) {
                resp[QStringLiteral("error")] = QStringLiteral("direction \"both\" requires outFrame or outTime");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            Friction::Core::beginUndoGroupBatch();
            QJsonArray applied;
            for (int i = 0; i < targets.count(); ++i) {
                auto *box = targets.at(i);
                const int slot = (order == QStringLiteral("bottom")) ?
                            (targets.count() - 1 - i) : i;
                const int start = (staggerMode == QStringLiteral("preset")) ?
                            baseFrame + slot * stagger : baseFrame;
                int inS = -1;
                int outS = -1;
                if (direction == QStringLiteral("in") || direction == QStringLiteral("both")) {
                    inS = start;
                }
                if (direction == QStringLiteral("out")) {
                    outS = start;
                } else if (direction == QStringLiteral("both")) {
                    outS = outBase;
                }
                LayerAnimPresets::apply(box, *preset, inS, outS, fps, durationScale,
                                        scene->getCanvasWidth(), scene->getCanvasHeight(), true);
                applied.append(QJsonObject{
                    {QStringLiteral("row"), i + 1},
                    {QStringLiteral("name"), box->prp_getName()},
                    {QStringLiteral("startFrame"), start + slot * stagger}});
            }
            if (staggerMode != QStringLiteral("preset") && stagger > 0) {
                staggerTargetsInTime(targets, stagger, order);
                if (args.value(QStringLiteral("extendScene")).toBool(true)) {
                    const auto range = scene->getFrameRange();
                    scene->setFrameRange({range.fMin,
                                          range.fMax + (targets.count() - 1) * stagger});
                }
            }
            Friction::Core::endUndoGroupBatch();
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("applied")] = applied;
            resp[QStringLiteral("staggerMode")] = QStringLiteral("timeline");
            return resp;
        }

        QJsonObject McpDispatcher::toolListEasingPresets(const QJsonObject &)
        {
            QJsonObject resp;
            if (!eSettings::sInstance) {
                resp[QStringLiteral("error")] = QStringLiteral("Settings unavailable");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            // same registry and filter the easing presets panel uses
            QJsonArray arr;
            const auto presets = eSettings::sInstance->fExpressions.getCore(QStringLiteral("Easing"));
            for (const auto &p : presets) {
                const bool isEasing = p.id.contains(QLatin1String("InOut")) ||
                        p.id.contains(QLatin1String("easeIn")) ||
                        p.id.contains(QLatin1String("easeOut"));
                if (!isEasing || !p.enabled) { continue; }
                QJsonObject o;
                o[QStringLiteral("id")] = p.id;
                o[QStringLiteral("title")] = p.title;
                if (!p.description.isEmpty()) {
                    o[QStringLiteral("desc")] = p.description;
                }
                arr.append(o);
            }
            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("easings")] = arr;
            resp[QStringLiteral("usage")] = QStringLiteral(
                        "pass the id to friction_set_keyframe_easing (easing param) - the same engine the easing presets panel applies");
            return resp;
        }

        QJsonObject McpDispatcher::toolStaggerLayers(const QJsonObject &args)
        {
            QJsonObject resp;
            auto scene = activeScene();
            if (!scene) {
                resp[QStringLiteral("error")] = QStringLiteral("No active scene");
                resp[QStringLiteral("success")] = false;
                return resp;
            }
            const qreal fps = scene->getFps() > 0 ? scene->getFps() : 60.0;
            const QString order = args.value(QStringLiteral("order"))
                    .toString(QStringLiteral("top")).toLower();
            int stagger = -1;
            if (args.contains(QStringLiteral("staggerFrames"))) {
                stagger = args.value(QStringLiteral("staggerFrames")).toInt();
            } else if (args.contains(QStringLiteral("staggerSeconds"))) {
                stagger = qRound(args.value(QStringLiteral("staggerSeconds")).toDouble() * fps);
            } else {
                stagger = 8;
            }
            if (stagger < 0) { stagger = 0; }
            const bool extendScene = args.value(QStringLiteral("extendScene")).toBool(true);

            // targets: explicit layer refs, or every top-level row
            QVector<BoundingBox*> targets;
            if (args.contains(QStringLiteral("layers"))) {
                const auto arr = args.value(QStringLiteral("layers")).toArray();
                for (const auto &v : arr) {
                    if (!v.isObject()) { continue; }
                    bool ok = false;
                    const auto box = resolveLayerRefCxx(v.toObject(), scene, QString(), &ok);
                    if (ok && box && !targets.contains(box)) { targets.append(box); }
                }
            }
            if (targets.isEmpty()) {
                const auto &contained = scene->getContained();
                for (const auto &child : contained) {
                    const auto box = enve_cast<BoundingBox*>(child.get());
                    if (box) { targets.append(box); }
                }
            }
            if (targets.count() < 2) {
                resp[QStringLiteral("error")] = QStringLiteral("Need at least two layers to stagger");
                resp[QStringLiteral("success")] = false;
                return resp;
            }

            Friction::Core::beginUndoGroupBatch();
            const QJsonArray shifted = staggerTargetsInTime(targets, stagger, order);
            if (extendScene && stagger > 0) {
                const auto range = scene->getFrameRange();
                scene->setFrameRange({range.fMin,
                                      range.fMax + (targets.count() - 1) * stagger});
            }
            Friction::Core::endUndoGroupBatch();

            resp[QStringLiteral("success")] = true;
            resp[QStringLiteral("staggerFrames")] = stagger;
            resp[QStringLiteral("order")] = order;
            resp[QStringLiteral("shifted")] = shifted;
            return resp;
        }

        QJsonObject McpDispatcher::toolGetKeyframes(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const QString prop = args.value(QStringLiteral("property")).toString().trimmed();
            if (prop.isEmpty()) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify property (e.g. position, scale, rotation, opacity)");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "var p = __findProp(lay, %2);\n"
                "var n = p.numKeys;\n"
                "var keys = [];\n"
                "for (var i = 1; i <= n; i++) {\n"
                "  var f = p.keyFrame(i);\n"
                "  keys.push({ index: i, frame: f, time: p.keyTime(i), value: p.valueAtFrame(f) });\n"
                "}\n"
                "return { name: lay.name, property: %2, numKeys: n, keys: keys };"
            ).arg(layerRef, jsStr(prop));

            return evalScript(script, QStringLiteral("AI Get Keyframes"));
        }

        QJsonObject McpDispatcher::toolSetExpression(const QJsonObject &args)
        {
            bool ok = false;
            const QString layerRef = layerRefExpr(args, QString(), &ok);
            if (!ok) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify layer (e.g. name, index, layer)");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const QString prop = args.value(QStringLiteral("property")).toString().trimmed();
            const QString bindings = args.value(QStringLiteral("bindings")).toString();
            const QString body = args.value(QStringLiteral("script")).toString();
            if (prop.isEmpty() || body.trimmed().isEmpty()) {
                QJsonObject err;
                err[QStringLiteral("error")] = QStringLiteral("Must specify property and script (JS function body that returns a number); bindings optional (e.g. \"x = $frame;\")");
                err[QStringLiteral("success")] = false;
                return err;
            }

            const QString script = QStringLiteral(
                "var lay = %1;\n"
                "var p = __findProp(lay, %2);\n"
                "var err = p.setExpression(%3, %4);\n"
                "if (err && err.length) { throw new Error('setExpression failed: ' + err); }\n"
                "return 'OK';"
            ).arg(layerRef, jsStr(prop), jsStr(bindings), jsStr(body));

            return evalScript(script, QStringLiteral("AI Set Expression"));
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
                                  QStringLiteral("Recursive layer tree of the active scene. Each layer reports its 1-based row within its container and a group path like \"2/1\" (top-level row 2, child row 1). Group-internal rows and top-level rows reuse the same numbers by design - ALWAYS address layers by row index or full path (primary), name only as a fallback. Also reports type, visibility, opacity, 3D mode, in/out points and depth."),
                                  QJsonObject()));

            // 4. get_layer_properties
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Top-level row number, 1-based from top")}};
                props[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Group path, e.g. \"2/1\" = top-level row 2, child row 1 (each segment 1-based within its container)")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name (fallback; ambiguous when duplicated)")}};
                tools.append(makeTool(QStringLiteral("friction_get_layer_properties"),
                                      QStringLiteral("Get transform properties, bounds and keyframe counts for a layer"),
                                      props));
            }

            // 5. create_layer
            {
                QJsonObject props;
                props[QStringLiteral("type")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("rect"), QStringLiteral("ellipse"), QStringLiteral("text"), QStringLiteral("null"), QStringLiteral("group"), QStringLiteral("container"), QStringLiteral("sound"), QStringLiteral("audio")}}, {QStringLiteral("description"), QStringLiteral("Type of layer to create (aliases: solid/box=rect, circle/ball=ellipse, layer/panel=container, sound/audio=sound file)")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name")}};
                props[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("File path for audio or media layer")}};
                props[QStringLiteral("x")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("X position for rect")}};
                props[QStringLiteral("y")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Y position for rect")}};
                props[QStringLiteral("width")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Width for rect")}};
                props[QStringLiteral("height")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Height for rect")}};
                props[QStringLiteral("radius")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Radius for ellipse")}};
                props[QStringLiteral("text")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Text content for text layer")}};
                props[QStringLiteral("opacity")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Opacity 0-100 (values in (0,1] are treated as fractions and scaled x100)")}};
                tools.append(makeTool(QStringLiteral("friction_create_layer"),
                                      QStringLiteral("Create a new layer in the active scene (rect, ellipse, text, null, group, container, sound)"),
                                      props, QJsonArray{QStringLiteral("type"), QStringLiteral("name")}));
            }

            // 5b. create_lottie_layer
            {
                QJsonObject props;
                props[QStringLiteral("json")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Lottie (bodymovin) animation JSON text, written directly by the AI")}};
                props[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Path to a .json (bodymovin) or .lottie (zip) file")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name")}};
                tools.append(makeTool(QStringLiteral("friction_create_lottie_layer"),
                                      QStringLiteral("Create a Lottie animation layer in the active scene, from bodymovin JSON text or a .json/.lottie file. Renders with alpha; use for AI-generated motion graphics and titles"),
                                      props));
            }

            // 5c. update_lottie_layer
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Layer index (1-based, top-level)")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name")}};
                props[QStringLiteral("json")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Replacement Lottie (bodymovin) JSON text")}};
                props[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Path to a replacement .json/.lottie file")}};
                tools.append(makeTool(QStringLiteral("friction_update_lottie_layer"),
                                      QStringLiteral("Replace the animation of an existing Lottie layer with new bodymovin JSON text or file (keeps the layer name and transform)"),
                                      props));
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
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Property name: position, scale, rotation, rotationX, rotationY, zPosition, opacity (0-100), anchorPoint")}};
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
                props[QStringLiteral("easing")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Easing preset id (see friction_list_easing_presets; e.g. easeOutCubic, easeInOutQuad, easeOutBack, easeOutBounce, easeOutElastic)")}};
                props[QStringLiteral("startFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Range start frame number (not a key index; omit or -1 to apply to all keys)")}};
                props[QStringLiteral("endFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Range end frame number (not a key index; omit or -1 to apply to all keys)")}};
                props[QStringLiteral("startTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Start time in seconds")}};
                props[QStringLiteral("endTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("End time in seconds")}};
                tools.append(makeTool(QStringLiteral("friction_set_keyframe_easing"),
                                      QStringLiteral("Apply an easing curve between keyframes on a property - the programmatic equivalent of the easing presets panel (same preset registry; enumerate ids with friction_list_easing_presets)"),
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
                                  QStringLiteral("List all raster effects accepted by add_raster_effect (enumerated live from the engine's effect registry: glow, liquid_glass, blur, glitch, vignette, etc.)"),
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
                                      QStringLiteral("Capture current viewport as Base64-encoded image for multimodal AI vision analysis (selection outlines and rulers are hidden during capture for a clean frame)"),
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
                props[QStringLiteral("opacity")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Opacity 0-100 (values in (0,1] are treated as fractions and scaled x100)")}};
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
                        QStringLiteral("fade"), QStringLiteral("flip_y"), QStringLiteral("flip_x"),
                        QStringLiteral("rotate"), QStringLiteral("spin"), QStringLiteral("pop_fade"),
                        QStringLiteral("slide_up_fade")
                    }},
                    {QStringLiteral("description"), QStringLiteral("High-level animation macro preset (combos like pop_fade / slide_up_fade apply both effects)")}
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
                                      QStringLiteral("Sample multiple visual frames across the timeline and return Base64 storyboard strips for AI vision review (selection outlines and rulers hidden)"),
                                      props));
            }

            // 26.1 get_keyframes
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Property name to inspect (position, scale, rotation, opacity, ...)")}};
                tools.append(makeTool(QStringLiteral("friction_get_keyframes"),
                                      QStringLiteral("List every keyframe on a layer property with frame, time and stored value"),
                                      props, QJsonArray{QStringLiteral("property")}));
            }

            // 26.2 set_expression
            {
                QJsonObject props;
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
                props[QStringLiteral("property")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Scalar property or sub-axis (e.g. rotation, positionx) - point properties must target sub-animators")}};
                props[QStringLiteral("bindings")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("One binding per line, e.g. 'f = $frame;' or 'src = ctrl.transform.rotation;' (omit for pure math on $value)")}};
                props[QStringLiteral("script")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("JS function body that returns a number, e.g. 'return Math.sin(f/10)*45;'")}};
                tools.append(makeTool(QStringLiteral("friction_set_expression"),
                                      QStringLiteral("Attach a JS expression to a property (AE expression equivalent); bindings bind $frame/$value/other properties, script computes the number"),
                                      props, QJsonArray{QStringLiteral("property"), QStringLiteral("script")}));
            }

            // 26.4 list_anim_presets
            tools.append(makeTool(QStringLiteral("friction_list_anim_presets"),
                                  QStringLiteral("Enumerate the animation preset library of the text-animation presets panel: layer presets (any layer type, from the panel's layer-motion category) and text presets (per-letter/word/line, text layers only). Returns ids with names, categories (inout/loop), durations and whether the out direction is supported. Use the ids with friction_apply_anim_preset."),
                                  QJsonObject()));

            // 26.5 apply_anim_preset
            {
                QJsonObject props;
                props[QStringLiteral("kind")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("layer"), QStringLiteral("text")}}, {QStringLiteral("description"), QStringLiteral("layer = panel's layer-motion presets, works on every layer (default); text = per-fragment text presets, text layers only")}};
                props[QStringLiteral("preset")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Preset id from friction_list_anim_presets")}};
                props[QStringLiteral("direction")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("both")}}, {QStringLiteral("description"), QStringLiteral("in = entrance (default), out = exit, both = entrance + exit (requires outFrame/outTime)")}};
                props[QStringLiteral("scope")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("layer"), QStringLiteral("all")}}, {QStringLiteral("description"), QStringLiteral("all = animate every top-level layer in row order (the default MG flow); otherwise address one layer via index/path/name")}};
                props[QStringLiteral("index")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Top-level row number, 1-based from top")}};
                props[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Group path, e.g. \"2/1\" = top-level row 2, child row 1")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Layer name (fallback)")}};
                props[QStringLiteral("startFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("First layer starts here (default 0)")}};
                props[QStringLiteral("startTime")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("First layer starts here, seconds (converted with scene fps)")}};
                props[QStringLiteral("outFrame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Exit window start for direction=both")}};
                props[QStringLiteral("durationScale")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Multiplies each preset's built-in duration (default 1)")}};
                props[QStringLiteral("staggerMode")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("timeline"), QStringLiteral("preset")}}, {QStringLiteral("description"), QStringLiteral("timeline (default) = move each layer later ON the timeline (fragment + keyframes together, stagger made visible in the layer stack); preset = only offset the animation start frames")}};
                props[QStringLiteral("staggerFrames")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Frames between consecutive layers (default 8) - the rhythm knob. 0 = simultaneous")}};
                props[QStringLiteral("staggerSeconds")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Stagger in seconds (converted with scene fps)")}};
                props[QStringLiteral("order")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("top"), QStringLiteral("bottom")}}, {QStringLiteral("description"), QStringLiteral("Which row animates first when staggering (default top)")}};
                tools.append(makeTool(QStringLiteral("friction_apply_anim_preset"),
                                      QStringLiteral("Apply an animation-preset-panel preset: one layer (index/path/name) or scope:\"all\" for every top-level layer staggered by row (default 8 frames, layers move later on the timeline). The default single-call MG flow. One undo step for the whole batch."),
                                      props, QJsonArray{QStringLiteral("preset")}));
            }

            // 26.5b stagger_layers
            {
                QJsonObject props;
                props[QStringLiteral("staggerFrames")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Frames each following layer moves later (default 8)")}};
                props[QStringLiteral("staggerSeconds")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("description"), QStringLiteral("Stagger in seconds (converted with scene fps)")}};
                props[QStringLiteral("order")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("top"), QStringLiteral("bottom")}}, {QStringLiteral("description"), QStringLiteral("Which row stays put (default top)")}};
                props[QStringLiteral("layers")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("description"), QStringLiteral("Optional array of layer refs ({index}|{path}|{name}); default = every top-level layer in row order")}};
                props[QStringLiteral("extendScene")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Widen the scene frame range so shifted layers stay visible (default true)")}};
                tools.append(makeTool(QStringLiteral("friction_stagger_layers"),
                                      QStringLiteral("Move each layer later on the timeline by row order (layer fragment + all its keyframes travel together, exactly like dragging the layer in the timeline): first layer stays, each following layer +staggerFrames. Use on existing animations to give them rhythm. One undo step."),
                                      props));
            }

            // 26.6 list_easing_presets
            tools.append(makeTool(QStringLiteral("friction_list_easing_presets"),
                                  QStringLiteral("Enumerate the easing preset ids of the easing presets panel (same registry). Pass an id to friction_set_keyframe_easing after creating keyframes - that tool is the programmatic equivalent of the panel."),
                                  QJsonObject()));

            // 27. import_file
            {
                QJsonObject props;
                props[QStringLiteral("path")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("File path to import (audio, video, image, vector, OCA)")}};
                props[QStringLiteral("name")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Optional layer name")}};
                tools.append(makeTool(QStringLiteral("friction_import_file"),
                                      QStringLiteral("Import an external media file into the active scene (supports audio files: mp3, wav, flac, ogg, images, videos, svg)"),
                                      props, QJsonArray{QStringLiteral("path")}));
            }

            // 28. set_marker
            {
                QJsonObject props;
                props[QStringLiteral("frame")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Timeline frame index for the marker (1-based or 0-based)")}};
                props[QStringLiteral("title")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Optional marker label/title, e.g. beat note, shot name, rhythm cue")}};
                tools.append(makeTool(QStringLiteral("friction_set_marker"),
                                      QStringLiteral("Set a timeline marker at a specific frame (used for music beat snapping, rhythm cues, shot cuts)"),
                                      props, QJsonArray{QStringLiteral("frame")}));
            }

            // 29. clear_markers
            tools.append(makeTool(QStringLiteral("friction_clear_markers"),
                                  QStringLiteral("Remove all timeline markers from the active scene"),
                                  QJsonObject()));

            // 30. list_markers
            tools.append(makeTool(QStringLiteral("friction_list_markers"),
                                  QStringLiteral("List all timeline markers currently set in the active scene with their frame and title"),
                                  QJsonObject()));

            // 26.3 get_api_schema (introspection)
            tools.append(makeTool(QStringLiteral("friction_get_api_schema"),
                                  QStringLiteral("Return the full tools and resources schema of this MCP server (self-introspection)"),
                                  QJsonObject()));

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
