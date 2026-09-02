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

#include "mcpserver.h"
#include "mcpdispatcher.h"
#include "appsupport.h"

#include <QLocalSocket>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QUrlQuery>

namespace Friction
{
    namespace AI
    {
        McpServer *McpServer::sInstance = nullptr;

        McpServer::McpServer(QObject *parent)
            : QObject(parent)
            , mDispatcher(std::make_unique<McpDispatcher>(this))
        {
            sInstance = this;
        }

        McpServer::~McpServer()
        {
            stop();
            if (sInstance == this) {
                sInstance = nullptr;
            }
        }

        McpServer *McpServer::instance()
        {
            return sInstance;
        }

        bool McpServer::isRunning() const
        {
            return (mLocalServer && mLocalServer->isListening()) ||
                   (mTcpServer && mTcpServer->isListening());
        }

        QString McpServer::httpUrl() const
        {
            if (mTcpServer && mTcpServer->isListening()) {
                return QStringLiteral("http://127.0.0.1:%1").arg(mPort);
            }
            return QString();
        }

        bool McpServer::start(const quint16 port,
                              const QString &socketName)
        {
            stop();

            mPort = port;
            mSocketName = socketName.isEmpty() ?
#ifdef Q_OS_WIN
                QStringLiteral("friction_mcp")
#else
                QStringLiteral("/tmp/friction_mcp.sock")
#endif
                : socketName;

            // Start Local Socket Server (Unix Domain Socket / Windows Named Pipe)
#ifndef Q_OS_WIN
            QLocalServer::removeServer(mSocketName);
#endif
            mLocalServer = new QLocalServer(this);
            connect(mLocalServer, &QLocalServer::newConnection,
                    this, &McpServer::handleNewLocalConnection);

            const bool localOk = mLocalServer->listen(mSocketName);
            if (!localOk) {
                qWarning() << "[McpServer] Failed to listen on local socket:" << mSocketName << mLocalServer->errorString();
            } else {
                qDebug() << "[McpServer] Local socket listening on:" << mSocketName;
            }

            // Start TCP Server (for HTTP / REST / JSON-RPC)
            mTcpServer = new QTcpServer(this);
            connect(mTcpServer, &QTcpServer::newConnection,
                    this, &McpServer::handleNewTcpConnection);

            const bool tcpOk = mTcpServer->listen(QHostAddress::LocalHost, mPort);
            if (!tcpOk) {
                qWarning() << "[McpServer] Failed to listen on TCP port:" << mPort << mTcpServer->errorString();
            } else {
                qDebug() << "[McpServer] HTTP & JSON-RPC server listening on:" << httpUrl();
            }

            if (localOk || tcpOk) {
                emit serverStarted();
                return true;
            }
            return false;
        }

        void McpServer::stop()
        {
            if (mLocalServer) {
                if (mLocalServer->isListening()) {
                    mLocalServer->close();
                }
                mLocalServer->deleteLater();
                mLocalServer = nullptr;
            }
#ifndef Q_OS_WIN
            if (!mSocketName.isEmpty()) {
                QLocalServer::removeServer(mSocketName);
            }
#endif
            if (mTcpServer) {
                if (mTcpServer->isListening()) {
                    mTcpServer->close();
                }
                mTcpServer->deleteLater();
                mTcpServer = nullptr;
            }
            emit serverStopped();
        }

        void McpServer::handleNewLocalConnection()
        {
            if (!mLocalServer) { return; }
            while (mLocalServer->hasPendingConnections()) {
                auto socket = mLocalServer->nextPendingConnection();
                if (!socket) { continue; }
                emit clientConnected();
                connect(socket, &QLocalSocket::readyRead,
                        this, &McpServer::handleLocalSocketReadyRead);
                connect(socket, &QLocalSocket::disconnected,
                        socket, &QLocalSocket::deleteLater);
            }
        }

        void McpServer::handleLocalSocketReadyRead()
        {
            auto socket = qobject_cast<QLocalSocket*>(sender());
            if (!socket) { return; }

            while (socket->canReadLine()) {
                const QByteArray line = socket->readLine().trimmed();
                if (line.isEmpty()) { continue; }

                QJsonParseError parseErr;
                const auto doc = QJsonDocument::fromJson(line, &parseErr);
                if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                    QJsonObject errResp;
                    errResp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
                    errResp[QStringLiteral("error")] = QJsonObject{
                        {QStringLiteral("code"), -32700},
                        {QStringLiteral("message"), QStringLiteral("Parse error")}
                    };
                    socket->write(QJsonDocument(errResp).toJson(QJsonDocument::Compact) + "\n");
                    socket->flush();
                    continue;
                }

                const auto resp = processJsonRpc(doc.object());
                socket->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + "\n");
                socket->flush();
            }
        }

        void McpServer::handleNewTcpConnection()
        {
            if (!mTcpServer) { return; }
            while (mTcpServer->hasPendingConnections()) {
                auto socket = mTcpServer->nextPendingConnection();
                if (!socket) { continue; }
                emit clientConnected();
                connect(socket, &QTcpSocket::readyRead,
                        this, &McpServer::handleTcpSocketReadyRead);
                connect(socket, &QTcpSocket::disconnected,
                        socket, &QTcpSocket::deleteLater);
            }
        }

        void McpServer::handleTcpSocketReadyRead()
        {
            auto socket = qobject_cast<QTcpSocket*>(sender());
            if (!socket) { return; }

            const QByteArray data = socket->readAll();
            handleHttpRequest(socket, data);
        }

        void McpServer::sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                                         const QByteArray &body, const QString &contentType)
        {
            if (!socket) { return; }

            QByteArray header;
            header += QStringLiteral("HTTP/1.1 %1 %2\r\n").arg(QString::number(statusCode), statusText).toUtf8();
            header += "Content-Type: " + contentType.toUtf8() + "\r\n";
            header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            header += "Access-Control-Allow-Origin: *\r\n";
            header += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
            header += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
            header += "Connection: close\r\n";
            header += "\r\n";

            socket->write(header);
            socket->write(body);
            socket->flush();
            socket->disconnectFromHost();
        }

        void McpServer::handleHttpRequest(QTcpSocket *socket, const QByteArray &data)
        {
            const int headerEnd = data.indexOf("\r\n\r\n");
            if (headerEnd == -1) {
                sendHttpResponse(socket, 400, QStringLiteral("Bad Request"), "{\"error\":\"Malformed request\"}");
                return;
            }

            const QString headerStr = QString::fromUtf8(data.left(headerEnd));
            const QStringList lines = headerStr.split(QStringLiteral("\r\n"));
            if (lines.isEmpty()) { return; }

            const QStringList reqLineParts = lines.first().split(' ');
            if (reqLineParts.size() < 2) { return; }

            const QString method = reqLineParts.at(0).toUpper();
            const QString path = reqLineParts.at(1);
            const QByteArray body = data.mid(headerEnd + 4);

            // Handle CORS preflight
            if (method == QStringLiteral("OPTIONS")) {
                sendHttpResponse(socket, 200, QStringLiteral("OK"), "");
                return;
            }

            // Endpoints
            if (method == QStringLiteral("GET")) {
                if (path == QStringLiteral("/") || path == QStringLiteral("/api/status")) {
                    QJsonObject status;
                    status[QStringLiteral("status")] = QStringLiteral("online");
                    status[QStringLiteral("app")] = QStringLiteral("Friction 2.5D");
                    status[QStringLiteral("version")] = AppSupport::getAppVersion();
                    status[QStringLiteral("mcp")] = true;
                    status[QStringLiteral("port")] = mPort;
                    sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(status).toJson());
                    return;
                } else if (path == QStringLiteral("/api/scene")) {
                    const auto res = mDispatcher->dispatchTool(QStringLiteral("friction_get_scene_info"), QJsonObject());
                    sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(res).toJson());
                    return;
                } else if (path == QStringLiteral("/api/layers")) {
                    const auto res = mDispatcher->dispatchTool(QStringLiteral("friction_list_layers"), QJsonObject());
                    sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(res).toJson());
                    return;
                } else if (path == QStringLiteral("/api/schema")) {
                    const auto res = mDispatcher->dispatchTool(QStringLiteral("friction_get_api_schema"), QJsonObject());
                    sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(res).toJson());
                    return;
                } else if (path.startsWith(QStringLiteral("/api/screenshot"))) {
                    const auto res = mDispatcher->captureViewport(QStringLiteral("png"), 90);
                    if (path.contains(QStringLiteral("raw=true")) || path.contains(QStringLiteral("format=binary"))) {
                        const QByteArray rawBytes = QByteArray::fromBase64(res.value(QStringLiteral("data")).toString().toLatin1());
                        sendHttpResponse(socket, 200, QStringLiteral("OK"), rawBytes, QStringLiteral("image/png"));
                    } else {
                        sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(res).toJson());
                    }
                    return;
                }
            } else if (method == QStringLiteral("POST")) {
                QJsonDocument bodyDoc = QJsonDocument::fromJson(body);
                const QJsonObject bodyObj = bodyDoc.isObject() ? bodyDoc.object() : QJsonObject();

                if (path == QStringLiteral("/api/eval")) {
                    const QString code = bodyObj.value(QStringLiteral("script")).toString();
                    const QString grp = bodyObj.value(QStringLiteral("undoGroupName")).toString(QStringLiteral("AI HTTP Action"));
                    const auto res = mDispatcher->evalScript(code, grp);
                    sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(res).toJson());
                    return;
                } else if (path.startsWith(QStringLiteral("/api/tool/"))) {
                    const QString toolName = path.mid(10);
                    const auto res = mDispatcher->dispatchTool(toolName, bodyObj);
                    sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(res).toJson());
                    return;
                } else if (path == QStringLiteral("/jsonrpc") || path == QStringLiteral("/mcp") || path == QStringLiteral("/")) {
                    const auto res = processJsonRpc(bodyObj);
                    sendHttpResponse(socket, 200, QStringLiteral("OK"), QJsonDocument(res).toJson());
                    return;
                }
            }

            sendHttpResponse(socket, 404, QStringLiteral("Not Found"), "{\"error\":\"Endpoint not found\"}");
        }

        QJsonObject McpServer::processJsonRpc(const QJsonObject &request)
        {
            const QJsonValue idVal = request.value(QStringLiteral("id"));
            const QString method = request.value(QStringLiteral("method")).toString();
            const QJsonObject params = request.value(QStringLiteral("params")).toObject();

            QJsonObject response;
            response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            if (!idVal.isUndefined()) {
                response[QStringLiteral("id")] = idVal;
            }

            // MCP Standard Methods
            if (method == QStringLiteral("initialize")) {
                QJsonObject serverInfo;
                serverInfo[QStringLiteral("name")] = QStringLiteral("friction-2.5d");
                serverInfo[QStringLiteral("version")] = AppSupport::getAppVersion();

                QJsonObject capabilities;
                capabilities[QStringLiteral("tools")] = QJsonObject{};
                capabilities[QStringLiteral("resources")] = QJsonObject{};

                QJsonObject result;
                result[QStringLiteral("protocolVersion")] = QStringLiteral("2024-11-05");
                result[QStringLiteral("serverInfo")] = serverInfo;
                result[QStringLiteral("capabilities")] = capabilities;

                response[QStringLiteral("result")] = result;
                return response;
            } else if (method == QStringLiteral("notifications/initialized") ||
                       method == QStringLiteral("initialized")) {
                // MCP post-init notification, return empty result or no-op
                response[QStringLiteral("result")] = QJsonObject{};
                return response;
            } else if (method == QStringLiteral("ping")) {
                response[QStringLiteral("result")] = QJsonObject{};
                return response;
            } else if (method == QStringLiteral("tools/list")) {
                QJsonObject result;
                result[QStringLiteral("tools")] = mDispatcher->getToolsSchema();
                response[QStringLiteral("result")] = result;
                return response;
            } else if (method == QStringLiteral("tools/call")) {
                const QString toolName = params.value(QStringLiteral("name")).toString();
                const QJsonObject toolArgs = params.value(QStringLiteral("arguments")).toObject();
                const QJsonObject toolResult = mDispatcher->dispatchTool(toolName, toolArgs);

                const bool isSuccess = toolResult.value(QStringLiteral("success")).toBool(true);
                QJsonArray contentArray;

                // If tool returned an image (captureViewport)
                if (toolResult.contains(QStringLiteral("data")) && toolResult.contains(QStringLiteral("format"))) {
                    QJsonObject imgContent;
                    imgContent[QStringLiteral("type")] = QStringLiteral("image");
                    imgContent[QStringLiteral("data")] = toolResult.value(QStringLiteral("data")).toString();
                    imgContent[QStringLiteral("mimeType")] = QStringLiteral("image/") + toolResult.value(QStringLiteral("format")).toString();
                    contentArray.append(imgContent);
                } else {
                    QJsonObject textContent;
                    textContent[QStringLiteral("type")] = QStringLiteral("text");
                    textContent[QStringLiteral("text")] = QString::fromUtf8(QJsonDocument(toolResult).toJson(QJsonDocument::Indented));
                    contentArray.append(textContent);
                }

                QJsonObject result;
                result[QStringLiteral("content")] = contentArray;
                result[QStringLiteral("isError")] = !isSuccess;
                response[QStringLiteral("result")] = result;
                return response;
            } else if (method == QStringLiteral("resources/list")) {
                QJsonObject result;
                result[QStringLiteral("resources")] = mDispatcher->getResourcesSchema();
                response[QStringLiteral("result")] = result;
                return response;
            } else if (method == QStringLiteral("resources/read")) {
                const QString uri = params.value(QStringLiteral("uri")).toString();
                const QJsonObject res = mDispatcher->readResource(uri);
                response[QStringLiteral("result")] = res;
                return response;
            }

            // Unknown method error
            QJsonObject err;
            err[QStringLiteral("code")] = -32601;
            err[QStringLiteral("message")] = QStringLiteral("Method not found: %1").arg(method);
            response[QStringLiteral("error")] = err;
            return response;
        }
    }
}
