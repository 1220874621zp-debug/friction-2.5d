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

#include "quickeffectsearchdialog.h"
#include "mainwindow.h"

#include <QVBoxLayout>
#include <QKeyEvent>
#include <QCursor>
#include <QScreen>
#include <QGuiApplication>
#include <QPainter>
#include <QStyledItemDelegate>

#include "RasterEffects/rastereffectmenucreator.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "PathEffects/patheffectmenucreator.h"
#include "themesupport.h"

namespace {
class QuickEffectItemDelegate : public QStyledItemDelegate {
public:
    explicit QuickEffectItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const bool isSelected = option.state & QStyle::State_Selected;
        if (isSelected) {
            painter->fillRect(option.rect, QColor(40, 110, 200, 200));
        } else if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(option.rect, QColor(60, 65, 75, 150));
        }

        const QString text = index.data(Qt::DisplayRole).toString();
        const QString category = index.data(Qt::UserRole + 1).toString();

        // Draw icon
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        QRect iconRect(option.rect.left() + 8, option.rect.top() + (option.rect.height() - 18) / 2, 18, 18);
        if (!icon.isNull()) {
            icon.paint(painter, iconRect);
        }

        // Draw effect name
        painter->setPen(isSelected ? Qt::white : QColor(230, 230, 230));
        QFont nameFont = option.font;
        nameFont.setPointSize(nameFont.pointSize() + 1);
        painter->setFont(nameFont);
        QRect textRect(iconRect.right() + 10, option.rect.top(), option.rect.width() - 160, option.rect.height());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        // Draw category badge
        if (!category.isEmpty()) {
            painter->setPen(isSelected ? QColor(220, 230, 255) : QColor(140, 150, 165));
            QFont catFont = option.font;
            catFont.setPointSize(std::max(8, catFont.pointSize() - 1));
            painter->setFont(catFont);
            QRect catRect(option.rect.right() - 130, option.rect.top(), 120, option.rect.height());
            painter->drawText(catRect, Qt::AlignVCenter | Qt::AlignRight, "[" + category + "]");
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return QSize(380, 34);
    }
};
}

QuickEffectSearchDialog::QuickEffectSearchDialog(MainWindow * const mainWindow,
                                                 QWidget * const parent) :
    QDialog(parent, Qt::FramelessWindowHint | Qt::Popup),
    mMainWindow(mainWindow)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedWidth(420);
    setFixedHeight(360);

    const auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);

    mSearchEdit = new QLineEdit(this);
    mSearchEdit->setPlaceholderText(tr("Search Effects (Ctrl+Space)..."));
    mSearchEdit->setClearButtonEnabled(true);
    mSearchEdit->setFixedHeight(36);
    QFont f = mSearchEdit->font();
    f.setPointSize(f.pointSize() + 2);
    mSearchEdit->setFont(f);
    mSearchEdit->setStyleSheet("QLineEdit { background: #1e1e24; color: #fff; border: 1px solid #4a5568; border-radius: 4px; padding: 4px 8px; }");
    mSearchEdit->installEventFilter(this);
    connect(mSearchEdit, &QLineEdit::textChanged, this, &QuickEffectSearchDialog::onSearchTextChanged);
    mainLayout->addWidget(mSearchEdit);

    mListWidget = new QListWidget(this);
    mListWidget->setItemDelegate(new QuickEffectItemDelegate(this));
    mListWidget->setStyleSheet("QListWidget { background: #18181c; border: 1px solid #333; border-radius: 4px; }");
    mListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(mListWidget, &QListWidget::itemActivated, this, &QuickEffectSearchDialog::onItemActivated);
    mainLayout->addWidget(mListWidget);

    setStyleSheet("QDialog { background: #25252b; border: 1px solid #555; border-radius: 6px; }");

    populateEffects();
}

void QuickEffectSearchDialog::populateEffects()
{
    mAllEffects.clear();

    // 1. Raster Effects
    RasterEffectMenuCreator::forEveryEffectCore(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            const QString category = cat.isEmpty() ? tr("General") : cat;
            mAllEffects.append({name, category, name, [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffect(creator());
            }});
        });

    RasterEffectMenuCreator::forEveryEffectCustom(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            const QString category = cat.isEmpty() ? tr("Custom") : cat;
            mAllEffects.append({name, category, name, [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffect(creator());
            }});
        });

    RasterEffectMenuCreator::forEveryEffectShader(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            const QString category = cat.isEmpty() ? tr("Shader") : cat;
            mAllEffects.append({name, category, name, [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffect(creator());
            }});
        });

    // 2. Path Effects
    PathEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const PathEffectMenuCreator::EffectCreator &creator) {
            mAllEffects.append({name, tr("Path Effects"), name, [this, creator]() {
                if (mMainWindow) mMainWindow->addPathEffect(creator());
            }});
        });

    // 3. Blend Effects
    BlendEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const BlendEffectMenuCreator::EffectCreator &creator) {
            mAllEffects.append({name, tr("Blend Effects"), name, [this, creator]() {
                if (mMainWindow) mMainWindow->addBlendEffect(creator());
            }});
        });

    // 4. Transform Effects
    TransformEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const TransformEffectMenuCreator::EffectCreator &creator) {
            mAllEffects.append({name, tr("Transform Effects"), name, [this, creator]() {
                if (mMainWindow) mMainWindow->addTransformEffect(creator());
            }});
        });

    onSearchTextChanged(QString());
}

void QuickEffectSearchDialog::showAtCursor()
{
    populateEffects();
    mSearchEdit->clear();

    const QPoint cursorPos = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(cursorPos);
    if (!screen) { screen = QGuiApplication::primaryScreen(); }

    QRect screenGeo = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    int x = cursorPos.x() - width() / 2;
    int y = cursorPos.y() - 40;

    if (x + width() > screenGeo.right()) x = screenGeo.right() - width() - 10;
    if (x < screenGeo.left()) x = screenGeo.left() + 10;
    if (y + height() > screenGeo.bottom()) y = screenGeo.bottom() - height() - 10;
    if (y < screenGeo.top()) y = screenGeo.top() + 10;

    move(x, y);
    show();
    mSearchEdit->setFocus();
}

void QuickEffectSearchDialog::onSearchTextChanged(const QString &text)
{
    mListWidget->clear();
    const QString filter = text.trimmed().toLower();

    for (const auto &item : mAllEffects) {
        if (filter.isEmpty() ||
            item.displayName.toLower().contains(filter) ||
            item.category.toLower().contains(filter) ||
            item.rawName.toLower().contains(filter)) {

            auto listItem = new QListWidgetItem(mListWidget);
            listItem->setText(item.displayName);
            listItem->setIcon(QIcon::fromTheme("effect"));
            listItem->setData(Qt::UserRole + 1, item.category);
            listItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<quintptr>(mListWidget->count())));
        }
    }

    if (mListWidget->count() > 0) {
        mListWidget->setCurrentRow(0);
    }
}

void QuickEffectSearchDialog::onItemActivated(QListWidgetItem *item)
{
    if (!item) return;
    const QString name = item->text();
    for (const auto &fx : mAllEffects) {
        if (fx.displayName == name) {
            fx.applyFunc();
            break;
        }
    }
    accept();
}

void QuickEffectSearchDialog::applySelected()
{
    const auto current = mListWidget->currentItem();
    if (current) {
        onItemActivated(current);
    }
}

bool QuickEffectSearchDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == mSearchEdit && event->type() == QEvent::KeyPress) {
        auto keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Down) {
            int row = mListWidget->currentRow();
            if (row < mListWidget->count() - 1) {
                mListWidget->setCurrentRow(row + 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Up) {
            int row = mListWidget->currentRow();
            if (row > 0) {
                mListWidget->setCurrentRow(row - 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            applySelected();
            return true;
        } else if (keyEvent->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}
