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

#ifndef SHORTCUTSETTINGSWIDGET_H
#define SHORTCUTSETTINGSWIDGET_H

#include "widgets/settingswidget.h"

#include <QComboBox>
#include <QTableWidget>

// one configurable shortcut entry; entries with active=false are
// reserved for future features (stored but not yet bound anywhere)
struct ShortcutEntry {
    QString id;         // settings key under group "shortcuts"
    QString name;       // display name
    QString category;   // grouping shown in the editor
    QString def;        // default (Friction) key sequence
    QString ae;         // After Effects-style preset key sequence
    bool active;        // true = currently bound in the application
};

class QKeySequenceEdit;

class ShortcutSettingsWidget : public SettingsWidget
{
    Q_OBJECT
public:
    explicit ShortcutSettingsWidget(QWidget *parent = nullptr);
    void applySettings();
    void updateSettings(bool restore = false);

private:
    void applyPreset(const QString &preset);
    void populateTable(const QString &preset = QString());

    QComboBox *mPreset;
    QTableWidget *mTable;
    QList<QKeySequenceEdit*> mEdits;

    static const QList<ShortcutEntry> &entries();
};

#endif // SHORTCUTSETTINGSWIDGET_H
