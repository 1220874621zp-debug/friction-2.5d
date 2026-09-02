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

#ifndef MCPDISPATCHER_H
#define MCPDISPATCHER_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <memory>

class QMainWindow;
class Canvas;

namespace Friction
{
    namespace Core
    {
        class JsHost;
    }

    namespace AI
    {
        class McpDispatcher : public QObject
        {
            Q_OBJECT
        public:
            explicit McpDispatcher(QObject *parent = nullptr);
            ~McpDispatcher() override;

            // Execute a tool by name with the given JSON arguments
            QJsonObject dispatchTool(const QString &toolName,
                                     const QJsonObject &arguments);

            // Get MCP tools list schema
            QJsonArray getToolsSchema() const;

            // Get MCP resources list schema
            QJsonArray getResourcesSchema() const;

            // Read MCP resource
            QJsonObject readResource(const QString &uri);

            // Capture current viewport as Base64 PNG image
            QJsonObject captureViewport(const QString &format = QStringLiteral("png"),
                                        const int quality = 90);

            // Evaluate JavaScript in JsHost
            QJsonObject evalScript(const QString &code,
                                   const QString &undoGroupName = QStringLiteral("AI Action"));

        private:
            // High-level tool implementations
            QJsonObject toolGetSceneInfo(const QJsonObject &args);
            QJsonObject toolSetSceneInfo(const QJsonObject &args);
            QJsonObject toolCreateScene(const QJsonObject &args);
            QJsonObject toolSwitchScene(const QJsonObject &args);
            QJsonObject toolDeleteScene(const QJsonObject &args);
            QJsonObject toolListScenes(const QJsonObject &args);
            QJsonObject toolListLayers(const QJsonObject &args);
            QJsonObject toolGetLayerProperties(const QJsonObject &args);
            QJsonObject toolCreateLayer(const QJsonObject &args);
            QJsonObject toolDuplicateLayer(const QJsonObject &args);
            QJsonObject toolDeleteLayer(const QJsonObject &args);
            QJsonObject toolSetParentLayer(const QJsonObject &args);
            QJsonObject toolSetLayerOrder(const QJsonObject &args);
            QJsonObject toolSetLayerVisibility(const QJsonObject &args);
            QJsonObject toolSetLayerLock(const QJsonObject &args);
            QJsonObject toolSet3DMode(const QJsonObject &args);
            QJsonObject toolSetProperty(const QJsonObject &args);
            QJsonObject toolSetKeyframe(const QJsonObject &args);
            QJsonObject toolSetKeyframeEasing(const QJsonObject &args);
            QJsonObject toolRemoveKeyframe(const QJsonObject &args);
            QJsonObject toolClearKeyframes(const QJsonObject &args);
            QJsonObject toolSeekTimeline(const QJsonObject &args);
            QJsonObject toolPlayPause(const QJsonObject &args);
            QJsonObject toolSetInOutPoint(const QJsonObject &args);
            QJsonObject toolSetTextProperties(const QJsonObject &args);
            QJsonObject toolSetLayerStyle(const QJsonObject &args);
            QJsonObject toolListAvailableEffects(const QJsonObject &args);
            QJsonObject toolAddRasterEffect(const QJsonObject &args);
            QJsonObject toolRemoveRasterEffect(const QJsonObject &args);
            QJsonObject toolUndo(const QJsonObject &args);
            QJsonObject toolRedo(const QJsonObject &args);

            QMainWindow *mainWindow() const;
            Canvas *activeScene() const;
            Friction::Core::JsHost *getJsHost();

            std::unique_ptr<Friction::Core::JsHost> mJsHost;
        };
    }
}

#endif // MCPDISPATCHER_H
