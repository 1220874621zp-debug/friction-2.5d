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
*/

#ifndef MANUALDIALOG_H
#define MANUALDIALOG_H

#include <QDialog>
#include <QMap>

class QLineEdit;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

// Offline user manual browser (Help > User Manual). Content source is
// src/app/manual/ (markdown embedded via manual.qrc, prefix :/manual).
// The left tree is driven entirely by manual/INDEX.md; docs present in
// the qrc but missing from INDEX.md fall into an "Uncategorized" group.
// See manual/README.md for how to add new pages.
class ManualDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManualDialog(QWidget* const parent = nullptr);

private:
    struct ManualDoc {
        QString resourcePath;   // :/manual/<folder>/<file>.md
        QString title;          // first "# " heading, fallback label
        QString body;           // raw text, used for search
    };

    void buildUi();
    void scanResources();
    void buildTree();
    void showPage(const QString& resourcePath);
    void applyFilter(const QString& text);

    QLineEdit*    mSearchBox = nullptr;
    QTreeWidget*  mTree = nullptr;
    QTextBrowser* mContent = nullptr;

    QMap<QString, ManualDoc> mDocs;  // key: resourcePath
    QString mCurrentPage;
};

#endif // MANUALDIALOG_H
