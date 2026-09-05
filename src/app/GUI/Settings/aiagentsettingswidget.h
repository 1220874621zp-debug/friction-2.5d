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

#ifndef AIAGENTSETTINGSWIDGET_H
#define AIAGENTSETTINGSWIDGET_H

#include "widgets/settingswidget.h"

#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

class AIAgentSettingsWidget : public SettingsWidget
{
    Q_OBJECT
public:
    explicit AIAgentSettingsWidget(QWidget *parent = nullptr);
    void applySettings() override;
    void updateSettings(bool restore = false) override;

private slots:
    void restartServer();
    void testConnection();
    void copyClaudeConfig();
    void copyCursorConfig();
    void copyPythonSnippet();
    void copyCurlExample();
    void copyAiPrompt();
    void updateStatusDisplay();

private:
    QCheckBox *mEnableServer = nullptr;
    QCheckBox *mAutoStart = nullptr;
    QSpinBox *mPort = nullptr;
    QLineEdit *mSocketPath = nullptr;
    QLabel *mStatusLabel = nullptr;
    QLabel *mHttpUrlLabel = nullptr;
    QLabel *mSocketUrlLabel = nullptr;
    QLabel *mTestResultLabel = nullptr;
};

#endif // AIAGENTSETTINGSWIDGET_H
