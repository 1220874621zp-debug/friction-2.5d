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

#ifndef QUICKEFFECTSEARCHDIALOG_H
#define QUICKEFFECTSEARCHDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <functional>
#include <QList>

class MainWindow;

struct QuickEffectItem {
    QString displayName;
    QString category;
    QString rawName;
    std::function<void()> applyFunc;
};

// AE FX Console style instant floating effect search popup (Ctrl+Space)
class QuickEffectSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit QuickEffectSearchDialog(MainWindow * const mainWindow,
                                    QWidget * const parent = nullptr);

    void populateEffects();
    void showAtCursor();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemActivated(QListWidgetItem *item);
    void applySelected();

private:
    MainWindow *mMainWindow = nullptr;
    QLineEdit *mSearchEdit = nullptr;
    QListWidget *mListWidget = nullptr;
    QList<QuickEffectItem> mAllEffects;
};

#endif // QUICKEFFECTSEARCHDIALOG_H
