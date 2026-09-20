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
#include <QTimer>
#include <QGraphicsDropShadowEffect>

#include "RasterEffects/rastereffectmenucreator.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "PathEffects/patheffectmenucreator.h"
#include "themesupport.h"

namespace {
// token matches a key when contained, or as an in-order subsequence
// ("bur" matches "blur") - case-insensitive
bool tokenMatchesKey(const QString &token, const QString &key)
{
    if (token.isEmpty()) { return true; }
    if (key.contains(token, Qt::CaseInsensitive)) { return true; }
    int pos = 0;
    for (const auto &c : key) {
        if (pos >= token.size()) { break; }
        if (c.toLower() == token.at(pos).toLower()) { ++pos; }
    }
    return pos >= token.size();
}

QStringList buildKeys(const QString &name, const QString &category)
{
    QStringList keys;
    keys << name.toLower();
    if (!category.isEmpty()) { keys << category.toLower(); }
    return keys;
}

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
        // view fonts can be pixel-size based (pointSize() == -1);
        // -1 + 1 == 0 would spam "Point size <= 0" warnings per paint
        const int basePt = option.font.pointSize();
        nameFont.setPointSize(basePt > 0 ? basePt + 1 : 10);
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
    // Qt::Tool instead of Qt::Popup: popup windows never get an IME
    // input context on Windows, making Chinese input impossible;
    // outside clicks now close via WindowDeactivate (see event()).
    // Translucent frameless window: the panel IS just the capsule
    // search field - the result list floats below in its own card
    // and only exists while there are results.
    QDialog(parent, Qt::FramelessWindowHint | Qt::Tool),
    mMainWindow(mainWindow)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(420);

    const auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(8);

    mSearchEdit = new QLineEdit(this);
    mSearchEdit->setPlaceholderText(tr("搜索特效…（支持中文 / 模糊匹配）"));
    mSearchEdit->setClearButtonEnabled(true);
    // capsule: black field, radius = half the fixed height
    mSearchEdit->setFixedHeight(36);
    QFont f = mSearchEdit->font();
    f.setPointSize(f.pointSize() + 2);
    mSearchEdit->setFont(f);
    mSearchEdit->setStyleSheet("QLineEdit { background: #000000; color: #fff; border: 1px solid #4a5568; border-radius: 18px; padding: 2px 14px; }");
    mSearchEdit->installEventFilter(this);
    connect(mSearchEdit, &QLineEdit::textChanged, this, &QuickEffectSearchDialog::onSearchTextChanged);
    mainLayout->addWidget(mSearchEdit);

    // floating results card - hidden until a search produces rows
    mListCard = new QFrame(this);
    mListCard->setObjectName(QStringLiteral("effectListCard"));
    mListCard->setStyleSheet(
                "QFrame#effectListCard {"
                "  background-color: rgba(22, 22, 28, 235);"
                "  border: 1px solid #33343c;"
                "  border-radius: 12px;"
                "}");
    // soft drop shadow sells the floating look on the transparent window
    const auto shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 170));
    mListCard->setGraphicsEffect(shadow);

    const auto cardLayout = new QVBoxLayout(mListCard);
    cardLayout->setContentsMargins(6, 6, 6, 6);
    cardLayout->setSpacing(0);

    mListWidget = new QListWidget(mListCard);
    mListWidget->setItemDelegate(new QuickEffectItemDelegate(this));
    mListWidget->setStyleSheet("QListWidget { background: transparent; border: none; }"
                               "QListWidget::item { border: none; }");
    mListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(mListWidget, &QListWidget::itemActivated, this, &QuickEffectSearchDialog::onItemActivated);
    // single click applies too (Enter / double-click keep working)
    connect(mListWidget, &QListWidget::itemClicked, this, &QuickEffectSearchDialog::onItemActivated);
    cardLayout->addWidget(mListWidget);

    mNoMatchLabel = new QLabel(tr("没有匹配的特效，换个关键词试试"), mListCard);
    mNoMatchLabel->setAlignment(Qt::AlignCenter);
    mNoMatchLabel->setStyleSheet("QLabel { color: #9aa0ab; font-size: 13px; padding: 10px 4px; background: transparent; border: none; }");
    mNoMatchLabel->hide();
    cardLayout->addWidget(mNoMatchLabel);

    mListCard->hide();
    mainLayout->addWidget(mListCard);

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
            mAllEffects.append({name, category, name, buildKeys(name, cat), [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffectToTarget(creator, nullptr);
            }});
        });

    RasterEffectMenuCreator::forEveryEffectCustom(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            const QString category = cat.isEmpty() ? tr("Custom") : cat;
            mAllEffects.append({name, category, name, buildKeys(name, cat), [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffectToTarget(creator, nullptr);
            }});
        });

    RasterEffectMenuCreator::forEveryEffectShader(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            const QString category = cat.isEmpty() ? tr("Shader") : cat;
            mAllEffects.append({name, category, name, buildKeys(name, cat), [this, creator]() {
                if (mMainWindow) mMainWindow->addRasterEffectToTarget(creator, nullptr);
            }});
        });

    // 2. Path Effects
    PathEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const PathEffectMenuCreator::EffectCreator &creator) {
            mAllEffects.append({name, tr("Path Effects"), name, buildKeys(name, QString()), [this, creator]() {
                if (mMainWindow) mMainWindow->addPathEffectToTarget(creator, nullptr);
            }});
        });

    // 3. Blend Effects
    BlendEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const BlendEffectMenuCreator::EffectCreator &creator) {
            mAllEffects.append({name, tr("Blend Effects"), name, buildKeys(name, QString()), [this, creator]() {
                if (mMainWindow) mMainWindow->addBlendEffectToTarget(creator, nullptr);
            }});
        });

    // 4. Transform Effects
    TransformEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const TransformEffectMenuCreator::EffectCreator &creator) {
            mAllEffects.append({name, tr("Transform Effects"), name, buildKeys(name, QString()), [this, creator]() {
                if (mMainWindow) mMainWindow->addTransformEffectToTarget(creator, nullptr);
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
    mWasActivated = false;
    show();
    raise();
    activateWindow();
    mSearchEdit->setFocus();
    qWarning() << "[FXSRCH] show" << pos() << size();
    // Windows activation is asynchronous - if it did not land right
    // after the first show (typically the very first press), retry
    // once after the deactivate-guard window has passed
    QTimer::singleShot(200, this, [this]() {
        if (!isVisible()) { return; }
        if (!isActiveWindow()) {
            qWarning() << "[FXSRCH] activation retry";
            activateWindow();
            raise();
            mSearchEdit->setFocus();
        }
    });
}

void QuickEffectSearchDialog::onSearchTextChanged(const QString &text)
{
    mListWidget->clear();
    const QString filter = text.trimmed();

    // empty query: just the capsule - no list, no card
    if (filter.isEmpty()) {
        updateResultsGeometry();
        return;
    }

    // every whitespace-separated token must match at least one key
    // (localized name, English name or category), fuzzy per token
    const QString normalized = QString(filter).replace(QChar(0x3000), QChar(' '));
    const auto tokens = normalized.split(QChar(' '), Qt::SkipEmptyParts);
    for (const auto &item : mAllEffects) {
        bool allMatch = true;
        for (const auto &token : tokens) {
            bool tokenMatch = false;
            for (const auto &key : item.keys) {
                if (tokenMatchesKey(token, key)) { tokenMatch = true; break; }
            }
            if (!tokenMatch) { allMatch = false; break; }
        }
        if (allMatch) {
            auto listItem = new QListWidgetItem(mListWidget);
            listItem->setText(item.displayName);
            listItem->setIcon(QIcon::fromTheme("effect"));
            listItem->setData(Qt::UserRole + 1, item.category);
        }
    }

    updateResultsGeometry();
    if (mListWidget->count() > 0) {
        mListWidget->setCurrentRow(0);
    }
}

// the panel hugs its content: capsule only when there is nothing to
// show, the card floats in below while there are rows (or the
// no-match hint); the list never reserves space up front
void QuickEffectSearchDialog::updateResultsGeometry()
{
    const int rows = mListWidget->count();
    if (rows > 0) {
        // delegate row height is 34; cap at 8 visible rows + scrollbar
        const int listH = qMin(rows, 8) * 34 + 4;
        mListWidget->setFixedHeight(listH);
        mNoMatchLabel->hide();
    } else if (!mSearchEdit->text().trimmed().isEmpty()) {
        mNoMatchLabel->show();
        mListWidget->setFixedHeight(0);
    } else {
        mNoMatchLabel->hide();
        mListWidget->setFixedHeight(0);
    }
    mListCard->setVisible(rows > 0 || mNoMatchLabel->isVisible());
    adjustSize();
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

bool QuickEffectSearchDialog::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        mWasActivated = true;
    } else if (event->type() == QEvent::WindowDeactivate) {
        // Qt::Tool has no popup grab - close when the dialog loses
        // activation (click elsewhere). Delayed check: a click on the
        // IME candidate window briefly deactivates without meaning to
        // dismiss; and a window whose activation never landed yet
        // (first show, async on Windows) must not be auto-hidden.
        QTimer::singleShot(150, this, [this]() {
            if (!isVisible() || isActiveWindow() || !mWasActivated) { return; }
            qWarning() << "[FXSRCH] auto-hide: focus lost";
            hide();
        });
    }
    return QDialog::event(event);
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
