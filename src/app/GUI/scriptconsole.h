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

#ifndef SCRIPTCONSOLE_H
#define SCRIPTCONSOLE_H

#include <QDockWidget>

class QPlainTextEdit;
class QLineEdit;
class QPushButton;

namespace Friction
{
    namespace Core
    {
        class JsHost;
    }
}

// Interactive JS console (REPL) dock: evaluate expressions against
// the full plugin API (app, scene, layers, properties).
class ScriptConsoleDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit ScriptConsoleDock(QWidget * const parent = nullptr);

    void appendOutput(const QString &text);
    void appendError(const QString &text);

    // wired by ScriptManager to its reload() slot, so the console
    // can rescan the scripts folder without the Scripts menu
    void setReloadCallback(const std::function<void()> &callback);

    // initial folder for the Open Script dialog (provided by
    // ScriptManager, which resolves it via AppSupport)
    void setScriptsPath(const QString &path) { mScriptsPath = path; }

private:
    void runInput();
    void openScript();

    QPlainTextEdit *mOutput = nullptr;
    QLineEdit *mInput = nullptr;
    QPushButton *mClearButton = nullptr;
    QPushButton *mReloadButton = nullptr;
    QPushButton *mOpenButton = nullptr;
    std::function<void()> mReloadCallback;
    QString mScriptsPath;
    Friction::Core::JsHost *mHost = nullptr;
    QStringList mHistory;
    int mHistoryIndex = -1;
};

#endif // SCRIPTCONSOLE_H
