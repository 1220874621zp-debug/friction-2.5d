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

#ifndef EFFECTSPRESETSPANEL_H
#define EFFECTSPRESETSPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <functional>

class MainWindow;

// AE-style Effects & Presets panel with live search filtering,
// category tree, and double-click / Enter instant application
class EffectsPresetsPanel : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPresetsPanel(MainWindow * const mainWindow,
                                QWidget * const parent = nullptr);

    void populateEffects();

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onApplyPressed();

private:
    void addEffectItem(const QString &categoryName,
                       const QString &effectName,
                       const QString &desc,
                       const std::function<void()> &applyFunc);

    MainWindow *mMainWindow = nullptr;
    QLineEdit *mSearchEdit = nullptr;
    QTreeWidget *mTreeWidget = nullptr;
    QMap<QString, QTreeWidgetItem*> mCategoryItems;
    QMap<QTreeWidgetItem*, std::function<void()>> mApplyCallbacks;
};

#endif // EFFECTSPRESETSPANEL_H
