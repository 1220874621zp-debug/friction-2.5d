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

#include "scriptconsole.h"

#include "Scripting/jsapi.h"
#include "themesupport.h"

#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>

// input line with Up/Down history navigation
class ScriptConsoleInput : public QLineEdit
{
public:
    ScriptConsoleInput(QWidget * const parent = nullptr)
        : QLineEdit(parent)
    {}

    void setHistory(QStringList * const history, int * const index)
    {
        mHistory = history;
        mIndex = index;
    }

protected:
    void keyPressEvent(QKeyEvent * const e) override
    {
        if (!mHistory || !mIndex) { QLineEdit::keyPressEvent(e); return; }
        if (e->key() == Qt::Key_Up) {
            if (mHistory->isEmpty()) { return; }
            *mIndex = (*mIndex < 0) ? mHistory->count() - 1
                                    : qMax(0, *mIndex - 1);
            if (*mIndex >= 0 && *mIndex < mHistory->count()) {
                setText(mHistory->at(*mIndex));
            }
            return;
        }
        if (e->key() == Qt::Key_Down) {
            if (*mIndex < 0) { QLineEdit::keyPressEvent(e); return; }
            *mIndex += 1;
            if (*mIndex >= mHistory->count()) {
                *mIndex = -1;
                clear();
            } else {
                setText(mHistory->at(*mIndex));
            }
            return;
        }
        QLineEdit::keyPressEvent(e);
    }

private:
    QStringList *mHistory;
    int *mIndex;
};

ScriptConsoleDock::ScriptConsoleDock(QWidget * const parent)
    : QDockWidget(parent)
{
    setObjectName(QStringLiteral("dockScriptConsole"));
    setWindowTitle(tr("Script Console"));
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);

    const auto content = new QWidget(this);
    const auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    mOutput = new QPlainTextEdit(content);
    mOutput->setReadOnly(true);
    mOutput->setLineWrapMode(QPlainTextEdit::NoWrap);
    mOutput->setPalette(ThemeSupport::getDarkPalette());
    mOutput->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { font-family: Consolas, monospace;"
        " font-size: 12px; background: #1a1a1a; color: #d0d0d0; }"));

    mInput = new ScriptConsoleInput(content);
    mInput->setPlaceholderText(
                tr("Enter a JS expression and press Enter (Up/Down: history)"));
    mInput->setPalette(ThemeSupport::getDarkPalette());
    static_cast<ScriptConsoleInput*>(mInput)->setHistory(&mHistory,
                                                         &mHistoryIndex);
    connect(mInput, &QLineEdit::returnPressed,
            this, &ScriptConsoleDock::runInput);

    const auto bottomBar = new QHBoxLayout();
    mOpenButton = new QPushButton(tr("Open Script"), content);
    mOpenButton->setFocusPolicy(Qt::NoFocus);
    mOpenButton->setToolTip(tr("Pick a .js file and run it in the console"));
    connect(mOpenButton, &QPushButton::clicked,
            this, &ScriptConsoleDock::openScript);
    mReloadButton = new QPushButton(tr("Reload Scripts"), content);
    mReloadButton->setFocusPolicy(Qt::NoFocus);
    mReloadButton->setToolTip(tr("Rescan the scripts folder and reload all plugins"));
    connect(mReloadButton, &QPushButton::clicked, this, [this]() {
        if (mReloadCallback) { mReloadCallback(); }
    });
    mClearButton = new QPushButton(tr("Clear"), content);
    mClearButton->setFocusPolicy(Qt::NoFocus);
    connect(mClearButton, &QPushButton::clicked,
            mOutput, &QPlainTextEdit::clear);
    bottomBar->addStretch();
    bottomBar->addWidget(mOpenButton);
    bottomBar->addWidget(mReloadButton);
    bottomBar->addWidget(mClearButton);

    layout->addWidget(mOutput, 1);
    layout->addLayout(bottomBar);
    layout->addWidget(mInput);
    setWidget(content);

    // REPL engine with the full plugin API
    mHost = new Friction::Core::JsHost(this);

    appendOutput(tr("Friction JS console ready. Try:")
                 + QStringLiteral("\n  app.activeScene")
                 + QStringLiteral("\n  scene.numLayers")
                 + QStringLiteral("\n  layer = scene.layer(1)"));
}

void ScriptConsoleDock::setReloadCallback(const std::function<void()> &callback)
{
    mReloadCallback = callback;
}

void ScriptConsoleDock::openScript()
{
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Open Script"),
                mScriptsPath,
                tr("JavaScript files (*.js)"));
    if (path.isEmpty()) { return; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendError(tr("Cannot open %1").arg(path));
        return;
    }
    const QString source = QString::fromUtf8(file.readAll());
    file.close();
    appendOutput(QStringLiteral("> ") + QFileInfo(path).fileName());
    const QString result = mHost->evaluate(source);
    if (result.startsWith(QStringLiteral("Uncaught"))) {
        appendError(result);
    } else if (!result.isEmpty()) {
        appendOutput(result);
    }
}

void ScriptConsoleDock::appendOutput(const QString &text)
{
    const auto stamp = QDateTime::currentDateTime().toString(
                QStringLiteral("HH:mm:ss"));
    mOutput->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, text));
}

void ScriptConsoleDock::appendError(const QString &text)
{
    appendOutput(QStringLiteral("ERROR: ") + text);
}

void ScriptConsoleDock::runInput()
{
    const QString source = mInput->text().trimmed();
    if (source.isEmpty()) { return; }
    mInput->clear();

    mHistory.append(source);
    mHistoryIndex = -1;

    appendOutput(QStringLiteral("> ") + source);
    const QString result = mHost->evaluate(source);
    if (result.startsWith(QStringLiteral("Uncaught"))) {
        appendError(result);
    } else {
        appendOutput(result);
    }
}
