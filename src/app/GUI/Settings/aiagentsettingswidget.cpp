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

#include "aiagentsettingswidget.h"
#include "appsupport.h"
#include "AI/mcpserver.h"
#include "GUI/global.h"
#include "Private/document.h"
#include "canvas.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QMessageBox>
#include <QUuid>

AIAgentSettingsWidget::AIAgentSettingsWidget(QWidget *parent)
    : SettingsWidget(parent)
{
    const auto scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    const auto contentWidget = new QWidget(scrollArea);
    const auto contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(14);

    // 1. Status and Server Overview
    const auto statusGroup = new QGroupBox(tr("Server Status"), contentWidget);
    statusGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto statusLayout = new QFormLayout(statusGroup);
    statusLayout->setSpacing(10);
    statusLayout->setContentsMargins(12, 16, 12, 14);

    mStatusLabel = new QLabel(statusGroup);
    mHttpUrlLabel = new QLabel(statusGroup);
    mSocketUrlLabel = new QLabel(statusGroup);

    mStatusLabel->setTextFormat(Qt::RichText);
    mHttpUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mSocketUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    statusLayout->addRow(tr("Status:"), mStatusLabel);
    statusLayout->addRow(tr("HTTP / REST Endpoint:"), mHttpUrlLabel);
    statusLayout->addRow(tr("Local Socket / Named Pipe:"), mSocketUrlLabel);

    contentLayout->addWidget(statusGroup);

    // 2. Server Configuration
    const auto configGroup = new QGroupBox(tr("Connection Settings"), contentWidget);
    configGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto configLayout = new QFormLayout(configGroup);
    configLayout->setSpacing(10);
    configLayout->setContentsMargins(12, 16, 12, 14);

    mEnableServer = new QCheckBox(tr("Enable AI Agent & MCP Server"), configGroup);
    mAutoStart = new QCheckBox(tr("Auto-start server when application launches"), configGroup);

    mPort = new QSpinBox(configGroup);
    mPort->setRange(1024, 65535);
    mPort->setValue(9527);

    mSocketPath = new QLineEdit(configGroup);
#ifdef Q_OS_WIN
    mSocketPath->setText(QStringLiteral("friction_mcp"));
#else
    mSocketPath->setText(QStringLiteral("/tmp/friction_mcp.sock"));
#endif

    configLayout->addRow(mEnableServer);
    configLayout->addRow(mAutoStart);
    configLayout->addRow(tr("TCP Port (HTTP & JSON-RPC):"), mPort);
    configLayout->addRow(tr("IPC Socket / Pipe Name:"), mSocketPath);

    // access token row: required by every HTTP endpoint except the
    // liveness probes; the named pipe stays open to local processes
    mTokenEdit = new QLineEdit(configGroup);
    mTokenEdit->setReadOnly(true);
    mTokenEdit->setToolTip(tr("HTTP requests must carry this token (Authorization: Bearer <token>, X-Friction-Token header or ?token= query). The local named pipe does not require it."));
    const auto regenTokenBtn = new QPushButton(QIcon::fromTheme("view_refresh"), tr("Regenerate"), configGroup);
    regenTokenBtn->setToolTip(tr("Generate a new access token; clients configured with the old token will stop working"));
    const auto tokenRow = new QHBoxLayout();
    tokenRow->setSpacing(8);
    tokenRow->addWidget(mTokenEdit, 1);
    tokenRow->addWidget(regenTokenBtn);
    configLayout->addRow(tr("Access Token (HTTP):"), tokenRow);
    connect(regenTokenBtn, &QPushButton::clicked,
            this, &AIAgentSettingsWidget::regenerateToken);

    const auto actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);
    const auto restartBtn = new QPushButton(QIcon::fromTheme("view_refresh"), tr("Restart Server"), configGroup);
    const auto testBtn = new QPushButton(QIcon::fromTheme("dialog-ok"), tr("Test Connection"), configGroup);
    mTestResultLabel = new QLabel(configGroup);

    actionRow->addWidget(restartBtn);
    actionRow->addWidget(testBtn);
    actionRow->addWidget(mTestResultLabel);
    actionRow->addStretch();
    configLayout->addRow(actionRow);

    connect(restartBtn, &QPushButton::clicked, this, &AIAgentSettingsWidget::restartServer);
    connect(testBtn, &QPushButton::clicked, this, &AIAgentSettingsWidget::testConnection);

    contentLayout->addWidget(configGroup);

    // 3. One-Click AI Integration Configs
    const auto exportGroup = new QGroupBox(tr("AI Agent One-Click Configs"), contentWidget);
    exportGroup->setObjectName(QStringLiteral("BlueBox"));
    const auto exportLayout = new QVBoxLayout(exportGroup);
    exportLayout->setSpacing(10);
    exportLayout->setContentsMargins(12, 16, 12, 14);

    const auto hintLabel = new QLabel(
        tr("Click below to copy ready-to-use configuration files or client snippets for your AI Agent:"), exportGroup);
    hintLabel->setWordWrap(true);
    exportLayout->addWidget(hintLabel);

    const auto btnGrid = new QGridLayout();
    btnGrid->setSpacing(8);
    const auto btnClaude = new QPushButton(QIcon::fromTheme("edit-copy"), tr("Copy Claude Desktop Config"), exportGroup);
    const auto btnCursor = new QPushButton(QIcon::fromTheme("edit-copy"), tr("Copy Cursor / Antigravity Config"), exportGroup);
    const auto btnPython = new QPushButton(QIcon::fromTheme("edit-copy"), tr("Copy Python Snippet"), exportGroup);
    const auto btnCurl = new QPushButton(QIcon::fromTheme("edit-copy"), tr("Copy cURL Command"), exportGroup);
    const auto btnPrompt = new QPushButton(QIcon::fromTheme("dialog-information"), tr("Copy AI Connection Prompt"), exportGroup);
    btnPrompt->setToolTip(tr("Generate and copy a ready-to-paste prompt for any AI (Claude, GPT, Gemini, etc.) to connect to Friction"));

    btnGrid->addWidget(btnClaude, 0, 0);
    btnGrid->addWidget(btnCursor, 0, 1);
    btnGrid->addWidget(btnPython, 1, 0);
    btnGrid->addWidget(btnCurl, 1, 1);
    btnGrid->addWidget(btnPrompt, 2, 0, 1, 2);

    connect(btnClaude, &QPushButton::clicked, this, &AIAgentSettingsWidget::copyClaudeConfig);
    connect(btnCursor, &QPushButton::clicked, this, &AIAgentSettingsWidget::copyCursorConfig);
    connect(btnPython, &QPushButton::clicked, this, &AIAgentSettingsWidget::copyPythonSnippet);
    connect(btnCurl, &QPushButton::clicked, this, &AIAgentSettingsWidget::copyCurlExample);
    connect(btnPrompt, &QPushButton::clicked, this, &AIAgentSettingsWidget::copyAiPrompt);

    exportLayout->addLayout(btnGrid);
    contentLayout->addWidget(exportGroup);

    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    addWidget(scrollArea);

    updateSettings();
    updateStatusDisplay();
}

void AIAgentSettingsWidget::applySettings()
{
    AppSupport::setSettings(QStringLiteral("ai"), QStringLiteral("enabled"), mEnableServer->isChecked());
    AppSupport::setSettings(QStringLiteral("ai"), QStringLiteral("autoStart"), mAutoStart->isChecked());
    AppSupport::setSettings(QStringLiteral("ai"), QStringLiteral("port"), mPort->value());
    AppSupport::setSettings(QStringLiteral("ai"), QStringLiteral("socketName"), mSocketPath->text().trimmed());

    auto server = Friction::AI::McpServer::instance();
    if (server) {
        if (mEnableServer->isChecked()) {
            server->start(quint16(mPort->value()), mSocketPath->text().trimmed());
        } else {
            server->stop();
        }
    }
    updateStatusDisplay();
}

void AIAgentSettingsWidget::updateSettings(bool restore)
{
    if (restore) {
        mEnableServer->setChecked(true);
        mAutoStart->setChecked(true);
        mPort->setValue(9527);
#ifdef Q_OS_WIN
        mSocketPath->setText(QStringLiteral("friction_mcp"));
#else
        mSocketPath->setText(QStringLiteral("/tmp/friction_mcp.sock"));
#endif
    } else {
        mEnableServer->setChecked(AppSupport::getSettings(QStringLiteral("ai"), QStringLiteral("enabled"), true).toBool());
        mAutoStart->setChecked(AppSupport::getSettings(QStringLiteral("ai"), QStringLiteral("autoStart"), true).toBool());
        mPort->setValue(AppSupport::getSettings(QStringLiteral("ai"), QStringLiteral("port"), 9527).toInt());
#ifdef Q_OS_WIN
        const QString defSock = QStringLiteral("friction_mcp");
#else
        const QString defSock = QStringLiteral("/tmp/friction_mcp.sock");
#endif
        mSocketPath->setText(AppSupport::getSettings(QStringLiteral("ai"), QStringLiteral("socketName"), defSock).toString());
    }
    if (mTokenEdit) {
        mTokenEdit->setText(Friction::AI::McpServer::ensureToken());
    }
    updateStatusDisplay();
}

void AIAgentSettingsWidget::updateStatusDisplay()
{
    auto server = Friction::AI::McpServer::instance();
    const bool isRunning = server && server->isRunning();

    if (isRunning) {
        mStatusLabel->setText(tr("<span style='color: #48bb78; font-weight: bold;'>● RUNNING</span> (Listening on Port %1)").arg(server->port()));
        mHttpUrlLabel->setText(QStringLiteral("<a href='%1'>%1</a>").arg(server->httpUrl()));
        mSocketUrlLabel->setText(server->socketPath());
    } else {
        mStatusLabel->setText(tr("<span style='color: #e53e3e; font-weight: bold;'>○ STOPPED</span>"));
        mHttpUrlLabel->setText(tr("Not active"));
        mSocketUrlLabel->setText(tr("Not active"));
    }
}

void AIAgentSettingsWidget::restartServer()
{
    auto server = Friction::AI::McpServer::instance();
    if (server) {
        server->stop();
        if (mEnableServer->isChecked()) {
            server->start(quint16(mPort->value()), mSocketPath->text().trimmed());
        }
    }
    updateStatusDisplay();
    if (mTestResultLabel) {
        mTestResultLabel->setText(tr("Server restarted"));
    }
}

void AIAgentSettingsWidget::testConnection()
{
    mTestResultLabel->setText(tr("Testing..."));
    QElapsedTimer timer;
    timer.start();

    QLocalSocket socket;
    socket.connectToServer(mSocketPath->text().trimmed());
    if (socket.waitForConnected(500)) {
        const qint64 elapsed = timer.elapsed();
        socket.disconnectFromServer();
        mTestResultLabel->setText(tr("<span style='color: #48bb78;'>Connected successfully (%1 ms)</span>").arg(elapsed));
    } else {
        mTestResultLabel->setText(tr("<span style='color: #e53e3e;'>Connection failed</span>"));
    }
}

void AIAgentSettingsWidget::regenerateToken()
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    AppSupport::setSettings(QStringLiteral("ai"), QStringLiteral("token"), token);
    if (mTokenEdit) {
        mTokenEdit->setText(token);
    }
    auto server = Friction::AI::McpServer::instance();
    if (server) {
        server->refreshToken();
    }
    if (mTestResultLabel) {
        mTestResultLabel->setText(tr("Access token regenerated"));
    }
}

void AIAgentSettingsWidget::copyClaudeConfig()
{
    const QString appPath = QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/mcp/friction_mcp_bridge.py");
    QJsonObject serverObj;
    serverObj[QStringLiteral("command")] = QStringLiteral("python3");
    serverObj[QStringLiteral("args")] = QJsonArray{
        appPath,
        QStringLiteral("--port"),
        QString::number(mPort->value()),
        QStringLiteral("--token"),
        Friction::AI::McpServer::ensureToken()
    };

    QJsonObject mcpServers;
    mcpServers[QStringLiteral("friction-2.5d")] = serverObj;

    QJsonObject root;
    root[QStringLiteral("mcpServers")] = mcpServers;

    const QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QApplication::clipboard()->setText(json);

    if (mTestResultLabel) {
        mTestResultLabel->setText(tr("Copied Claude Desktop configuration to clipboard"));
    }
}

void AIAgentSettingsWidget::copyCursorConfig()
{
    const QString appPath = QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/mcp/friction_mcp_bridge.py");
    QJsonObject serverObj;
    serverObj[QStringLiteral("command")] = QStringLiteral("python3");
    serverObj[QStringLiteral("args")] = QJsonArray{
        appPath,
        QStringLiteral("--port"),
        QString::number(mPort->value()),
        QStringLiteral("--token"),
        Friction::AI::McpServer::ensureToken()
    };

    QJsonObject mcpServers;
    mcpServers[QStringLiteral("friction")] = serverObj;

    QJsonObject root;
    root[QStringLiteral("mcpServers")] = mcpServers;

    const QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QApplication::clipboard()->setText(json);

    if (mTestResultLabel) {
        mTestResultLabel->setText(tr("Copied Cursor / Antigravity configuration to clipboard"));
    }
}

void AIAgentSettingsWidget::copyPythonSnippet()
{
    const QString snippet = QStringLiteral(
        "import requests\n\n"
        "FRICTION_URL = 'http://127.0.0.1:%1'\n"
        "FRICTION_TOKEN = '%2'\n"
        "HEADERS = {'X-Friction-Token': FRICTION_TOKEN}\n\n"
        "# 1. Query scene metadata\n"
        "scene = requests.get(f'{FRICTION_URL}/api/scene', headers=HEADERS).json()\n"
        "print('Scene:', scene)\n\n"
        "# 2. Create layer and keyframe animation\n"
        "code = '''\n"
        "var layer = app.activeScene.addRect(\"Box\", 0, 0, 200, 200);\n"
        "layer.position().setValueAtFrame(0, [100, 100]);\n"
        "layer.position().setValueAtFrame(60, [800, 500]);\n"
        "'''\n"
        "res = requests.post(f'{FRICTION_URL}/api/eval', headers=HEADERS, json={'script': code}).json()\n"
        "print('Result:', res)\n"
    ).arg(QString::number(mPort->value()), Friction::AI::McpServer::ensureToken());

    QApplication::clipboard()->setText(snippet);

    if (mTestResultLabel) {
        mTestResultLabel->setText(tr("Copied Python snippet to clipboard"));
    }
}

void AIAgentSettingsWidget::copyCurlExample()
{
    const QString curlCmd = QStringLiteral(
        "curl -X POST http://127.0.0.1:%1/api/eval \\\n"
        "  -H \"Content-Type: application/json\" \\\n"
        "  -H \"Authorization: Bearer %2\" \\\n"
        "  -d '{\"script\": \"app.activeScene.addRect(\\\"Rect\\\", 0, 0, 200, 200);\"}'"
    ).arg(QString::number(mPort->value()), Friction::AI::McpServer::ensureToken());

    QApplication::clipboard()->setText(curlCmd);

    if (mTestResultLabel) {
        mTestResultLabel->setText(tr("Copied cURL command to clipboard"));
    }
}

void AIAgentSettingsWidget::copyAiPrompt()
{
    // Load the base prompt from the bundled markdown file
    const QStringList candidatePaths = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/../tools/mcp/FRICTION_AI_CONNECT.md"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/mcp/FRICTION_AI_CONNECT.md"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/tools/mcp/FRICTION_AI_CONNECT.md"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/friction/tools/mcp/FRICTION_AI_CONNECT.md"),
        QDir::homePath() + QStringLiteral("/.local/share/friction/tools/mcp/FRICTION_AI_CONNECT.md")
    };

    QString basePrompt;
    for (const auto &path : candidatePaths) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts.setEncoding(QStringConverter::Utf8);
            basePrompt = ts.readAll();
            f.close();
            if (!basePrompt.trimmed().isEmpty()) {
                break;
            }
        }
    }

    if (basePrompt.isEmpty()) {
        // Fallback short prompt if file not found
        basePrompt = tr(
            "# Friction 2.5D AI 动画助手\n\n"
            "- **接口**：http://127.0.0.1:%1/mcp（POST JSON-RPC），请求头带 X-Friction-Token: <访问令牌>（本页\"访问令牌\"即令牌值）\n"
            "- **图层寻址**：行号为主（index:3 = 顶层第 3 行，1-based）；组内行号与组外重复须用组路径（path:\"2/1\"）；名称仅兜底；先 friction_list_layers 看树\n\n"
            "## 默认动效流程（无详细要求时，一次调用）\n\n"
            "friction_apply_anim_preset {\"preset\":\"l-sheet-slide-up\",\"scope\":\"all\"}\n"
            "= 每层套预设 + 按行序在时间轴上错开 8 帧（图层连关键帧整体后移，单步撤销）。\n"
            "推荐预设：l-fade / l-pop / l-drop / l-sheet-slide-up / l-elastic-scale / l-rubber-bounce / l-flip-y（图层）；sharp-elastic-pop / sharp-cascade-word-pop（文字，kind:\"text\"）。\n"
            "已有动画只加节奏：friction_stagger_layers {\"staggerFrames\":8}。\n"
            "打关键帧后套缓动：friction_set_keyframe_easing（id 见 friction_list_easing_presets）。\n\n"
            "## 声明式 markup（文字 PV 首选）\n\n"
            "friction_render_markup 传 HTML 风格标记（scene/seq/card/col/text/h1/line，动画值 from->to，mode:\"append\" 追加）：\n"
            "<scene width=\"1920\" height=\"1080\" fps=\"60\" duration=\"6\" bg=\"#0a0c12\"><seq from=\"0\" to=\"2.5\"><card 3d=\"true\" rotY=\"-25->0\"><col><h1>KINETIC</h1><line in=\"expand\"/></col></card></seq></scene>\n\n"
            "## 协作守则\n\n"
            "1. 严禁清屏重绘——改已有内容用 friction_update_layer 或 markup mode:\"append\"\n"
            "2. 报错附可用图层/属性清单，直接自纠重试\n"
            "3. 仅被明确要求时才截图审片，不要主动多轮复查\n"
            "4. 缓动优先，禁线性插值\n"
        ).arg(mPort->value());
    }

    // Fetch live scene info directly from active document/scene
    QString statusBlock;
    Canvas *activeScene = nullptr;
    if (Document::sInstance) {
        if (Document::sInstance->fActiveScene) {
            activeScene = Document::sInstance->fActiveScene;
        } else if (!Document::sInstance->fScenes.isEmpty()) {
            activeScene = Document::sInstance->fScenes.first().get();
        }
    }

    if (activeScene) {
        const int w = activeScene->getCanvasWidth();
        const int h = activeScene->getCanvasHeight();
        const double fps = activeScene->getFps();
        const double dur = activeScene->getFrameRange().fMax / (fps > 0 ? fps : 60.0);
        const int totalFrames = activeScene->getFrameRange().fMax;
        const int curFrame = Document::sInstance ? Document::sInstance->getActiveSceneFrame() : 0;
        const int layers = activeScene->getContained().count();

        statusBlock = QStringLiteral("\n\n---\n\n")
            + tr("## 当前场景实时状态\n\n"
                 "| 属性 | 值 |\n"
                 "|:---|:---|\n"
                 "| 分辨率 | %1 × %2 px |\n"
                 "| 帧率 | %3 FPS |\n"
                 "| 时长 | %4 秒（共 %5 帧）|\n"
                 "| 当前帧 | %6 帧 |\n"
                 "| 图层数 | %7 |\n\n"
                 "Friction 正在运行，可直接发送指令开始创作\n")
                 .arg(w).arg(h).arg(fps).arg(dur).arg(totalFrames).arg(curFrame).arg(layers);
    }

    const QString fullPrompt = basePrompt + statusBlock;
    QApplication::clipboard()->setText(fullPrompt);

    if (mTestResultLabel) {
        mTestResultLabel->setText(tr("AI connection prompt copied to clipboard"));
    }
}

