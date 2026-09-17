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

#include "manualdialog.h"

#include "appsupport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

namespace {
    const QString kResRoot = ":/manual";
    const QString kIndexPath = ":/manual/INDEX.md";

    const QRegularExpression kHeadingRx("^#{2,6}\\s+(.+)$");
    const QRegularExpression kLinkRx("\\[([^\\]]+)\\]\\(([^)\\s]+\\.md)\\)");
    const QRegularExpression kTitleRx("^#\\s+(.+)$");

    QString docTitleFromBody(const QString& body, const QString& fallbackName)
    {
        const auto lines = body.split('\n');
        for (const QString& line : lines) {
            const auto match = kTitleRx.match(line);
            if (match.hasMatch()) { return match.captured(1).trimmed(); }
        }
        return fallbackName;
    }
}

ManualDialog::ManualDialog(QWidget* const parent)
    : QDialog(parent)
{
    setWindowTitle(tr("User Manual"));
    resize(980, 660);

    scanResources();
    buildUi();
    buildTree();

    const QString lastPage = AppSupport::getSettings("ui",
                                                      "manualLastPage",
                                                      kIndexPath).toString();
    showPage(mDocs.contains(lastPage) ? lastPage : kIndexPath);
}

void ManualDialog::scanResources()
{
    QDirIterator it(kResRoot, { "*.md" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();

        QFile file(path);
        if (!file.open(QFile::ReadOnly | QFile::Text)) { continue; }

        ManualDoc doc;
        doc.resourcePath = path;
        doc.body = QString::fromUtf8(file.readAll());
        doc.title = docTitleFromBody(doc.body, QFileInfo(path).fileName());
        mDocs.insert(path, doc);
    }
}

void ManualDialog::buildUi()
{
    mSearchBox = new QLineEdit(this);
    mSearchBox->setPlaceholderText(tr("Search title or content ..."));
    mSearchBox->setClearButtonEnabled(true);

    mTree = new QTreeWidget(this);
    mTree->setHeaderHidden(true);
    mTree->setUniformRowHeights(true);

    mContent = new QTextBrowser(this);
    mContent->setOpenExternalLinks(true);

    auto* treeBox = new QVBoxLayout;
    treeBox->setContentsMargins(0, 0, 0, 0);
    treeBox->addWidget(mSearchBox);
    treeBox->addWidget(mTree, 1);

    auto* treePanel = new QWidget(this);
    treePanel->setLayout(treeBox);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(treePanel);
    splitter->addWidget(mContent);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 300, 680 });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(mTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/)
    {
        if (current != nullptr && !current->data(0, Qt::UserRole).isNull()) {
            showPage(current->data(0, Qt::UserRole).toString());
        }
    });

    connect(mSearchBox, &QLineEdit::textChanged,
            this, &ManualDialog::applyFilter);

    // relative links between manual pages: resolve against the
    // directory of the current page, then jump if the target exists
    connect(mContent, &QTextBrowser::anchorClicked, this,
            [this](const QUrl& url)
    {
        if (!url.isRelative()) { return; } // external: openExternalLinks
        const QString target = QDir::cleanPath(
                    QDir(QFileInfo(mCurrentPage).absolutePath()).filePath(url.toString()));
        if (mDocs.contains(target)) { showPage(target); }
    });
}

void ManualDialog::buildTree()
{
    mTree->clear();

    const auto makeItem = [this](const QString& label,
                                 const QString& path,
                                 QTreeWidgetItem* parent)
    {
        auto* item = (parent != nullptr) ? new QTreeWidgetItem(parent)
                                         : new QTreeWidgetItem;
        item->setText(0, label);
        item->setData(0, Qt::UserRole, path);
        if (parent == nullptr) { mTree->addTopLevelItem(item); }
        return item;
    };

    // front page (master index) always first
    if (mDocs.contains(kIndexPath)) {
        makeItem(tr("Contents"), kIndexPath, nullptr);
    }

    // tree structure is driven entirely by INDEX.md: headings become
    // groups, links inside a group become entries
    QStringList referenced;
    const auto indexIt = mDocs.constFind(kIndexPath);
    if (indexIt != mDocs.constEnd()) {
        QTreeWidgetItem* section = nullptr;
        const auto lines = indexIt->body.split('\n');
        for (const QString& line : lines) {
            const auto heading = kHeadingRx.match(line);
            if (heading.hasMatch()) {
                section = makeItem(heading.captured(1).trimmed(), QString(), nullptr);
                continue;
            }
            const auto link = kLinkRx.match(line);
            if (link.hasMatch() && section != nullptr) {
                const QString path = QDir::cleanPath(
                            QDir(kResRoot).filePath(link.captured(2)));
                makeItem(link.captured(1), path, section);
                referenced.append(path);
            }
        }
    }

    // fallback: docs in the qrc but not in INDEX.md, so they never
    // become silently invisible
    auto uncategorized = new QTreeWidgetItem;
    bool hasUncategorized = false;
    for (auto docIt = mDocs.constBegin(); docIt != mDocs.constEnd(); ++docIt) {
        if (docIt.key() == kIndexPath || referenced.contains(docIt.key())) { continue; }
        if (!hasUncategorized) {
            uncategorized->setText(0, tr("Uncategorized"));
            mTree->addTopLevelItem(uncategorized);
            hasUncategorized = true;
        }
        makeItem(docIt->title, docIt.key(), uncategorized);
    }

    mTree->expandAll();
}

void ManualDialog::showPage(const QString& resourcePath)
{
    mCurrentPage = resourcePath;
    AppSupport::setSettings("ui", "manualLastPage", resourcePath);

    const auto it = mDocs.constFind(resourcePath);
    if (it == mDocs.constEnd()) {
        mContent->setMarkdown(
                    tr("## Missing document\n\n`%1` is not registered in `manual.qrc`, see `manual/README.md` for how to add manual pages.")
                    .arg(resourcePath));
        return;
    }
    mContent->setMarkdown(it->body);

    // keep the tree selection in sync when jumping via in-page links
    for (QTreeWidgetItemIterator treeIt(mTree); *treeIt; ++treeIt) {
        if ((*treeIt)->data(0, Qt::UserRole).toString() == resourcePath) {
            QSignalBlocker blocker(mTree);
            mTree->setCurrentItem(*treeIt);
            break;
        }
    }
}

void ManualDialog::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();
    for (QTreeWidgetItemIterator it(mTree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) { continue; } // group node, handled below
        const auto docIt = mDocs.constFind(path);
        const QString haystack = (docIt != mDocs.constEnd())
                ? docIt->title + "\n" + docIt->body
                : item->text(0);
        item->setHidden(!needle.isEmpty()
                        && !haystack.contains(needle, Qt::CaseInsensitive));
    }
    // hide group nodes with no visible children
    for (int i = 0; i < mTree->topLevelItemCount(); i++) {
        QTreeWidgetItem* section = mTree->topLevelItem(i);
        bool anyVisible = false;
        for (int c = 0; c < section->childCount(); c++) {
            if (!section->child(c)->isHidden()) {
                anyVisible = true;
                break;
            }
        }
        section->setHidden(!anyVisible);
    }
}
