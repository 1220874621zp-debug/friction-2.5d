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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "fontswidget.h"
#include "GUI/global.h"
#include "themesupport.h"
#include "Private/document.h"
#include "appsupport.h"

#include <QLineEdit>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <functional>
#include <cmath>
#include <QPainterPath>

using namespace Friction::Ui;

namespace {

QStringList loadFontFavorites()
{
    const auto var = AppSupport::getSettings("fonts",
                                             "favorites",
                                             QVariant());
    return var.toStringList();
}

void saveFontFavorites(const QStringList &favorites)
{
    AppSupport::setSettings("fonts", "favorites",
                            QVariant(favorites));
}

bool familyIsCJK(const QFontDatabase &db, const QString &family)
{
    const auto systems = db.writingSystems(family);
    return systems.contains(QFontDatabase::SimplifiedChinese) ||
           systems.contains(QFontDatabase::TraditionalChinese);
}

// one font row: draws the family name in its own font plus a
// favorite star on the right; clicking the star toggles the
// favorite instead of selecting the font
class FontRowDelegate : public QStyledItemDelegate
{
public:
    FontRowDelegate(QObject *parent, std::function<void()> favChanged)
        : QStyledItemDelegate(parent)
        , mFavChanged(std::move(favChanged)) {}

    static constexpr int starWidth = 30;

    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (option.state & QStyle::State_Selected) {
            p->fillRect(option.rect, QColor(60, 128, 200, 90));
        } else if (option.state & QStyle::State_MouseOver) {
            p->fillRect(option.rect, QColor(255, 255, 255, 18));
        }
        const auto family = index.data(Qt::UserRole).toString();
        const bool isHeader = index.data(Qt::UserRole + 2).toBool();
        QFont f = isHeader ? QFont() : QFont(family);
        f.setPixelSize(isHeader ? 11 : 14);
        if (isHeader) { f.setBold(true); }
        p->setFont(f);
        p->setPen(isHeader ? QColor(150, 150, 150) : QColor(220, 220, 220));
        const QRect textRect = option.rect.adjusted(8, 0,
                                                    -starWidth - 4, 0);
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                    family);
        // star
        if (!isHeader) {
            const bool fav = index.data(Qt::UserRole + 1).toBool();
            // vector star: star glyphs are missing from some fonts
            // on user systems, drawing the shape works everywhere
            const qreal cx = option.rect.right() - starWidth/2;
            const qreal cy = option.rect.center().y();
            const qreal r = option.rect.height()/2 - 7;
            QPainterPath star;
            for (int i = 0; i < 10; i++) {
                const qreal ang = -M_PI/2 + i*M_PI/5;
                const qreal rr = i % 2 == 0 ? r : r*0.45;
                const QPointF pt(cx + rr*std::cos(ang),
                                 cy + rr*std::sin(ang));
                if (i == 0) { star.moveTo(pt); }
                else { star.lineTo(pt); }
            }
            star.closeSubpath();
            if (fav) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(255, 200, 70));
                p->drawPath(star);
            } else {
                QPen pen(QColor(130, 130, 130), 1.2);
                p->setPen(pen);
                p->setBrush(Qt::NoBrush);
                p->drawPath(star);
            }
        }
        p->restore();
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override
    {
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto me = static_cast<QMouseEvent*>(event);
            if (me->pos().x() > option.rect.right() - starWidth) {
                // star click: toggle favorite and let the popup
                // rebuild (keeps the current filter)
                const auto family = index.data(Qt::UserRole).toString();
                QStringList favorites = loadFontFavorites();
                if (favorites.contains(family)) {
                    favorites.removeAll(family);
                } else {
                    favorites << family;
                }
                saveFontFavorites(favorites);
                if (mFavChanged) { mFavChanged(); }
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(event, model,
                                                option, index);
    }

private:
    std::function<void()> mFavChanged;
};

} // namespace

// ---------------------------------------------------------------- picker

FontFamilyPicker::FontFamilyPicker(QWidget *parent)
    : QPushButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setToolTip(tr("Font family"));
    setMinimumWidth(20);
    connect(this, &QPushButton::clicked,
            this, &FontFamilyPicker::showPopup);
}

QString FontFamilyPicker::currentText() const
{ return mCurrent; }

void FontFamilyPicker::setCurrentText(const QString &family)
{
    if (mCurrent == family) { return; }
    mCurrent = family;
    setText(family);
    emit currentTextChanged(family);
}

void FontFamilyPicker::setFontFamilies(const QStringList &families)
{
    mFamilies = families;
    // silent sync (no signal): pick a valid family when the current
    // one is empty or no longer installed
    if (!mFamilies.contains(mCurrent)) {
        mCurrent = families.isEmpty() ? QString() : families.first();
    }
    setText(mCurrent);
}

void FontFamilyPicker::showPopup()
{
    const auto popup = new QFrame(this, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::NoFrame);
    popup->setAutoFillBackground(true);
    popup->setPalette(ThemeSupport::getDarkerPalette());

    const auto lay = new QVBoxLayout(popup);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    const auto search = new QLineEdit(popup);
    search->setPlaceholderText(tr("Search fonts"));
    search->setClearButtonEnabled(true);
    lay->addWidget(search);

    const auto list = new QListWidget(popup);
    list->setMouseTracking(true);
    lay->addWidget(list);

    const auto rebuild = [this, list](const QString &filter) {
        list->clear();
        QFontDatabase db;
        const QStringList favorites = loadFontFavorites();
        const QString filterLower = filter.toLower();

        const auto addRow = [&](const QString &family, const bool fav) {
            auto item = new QListWidgetItem(family, list);
            item->setData(Qt::UserRole, family);
            item->setData(Qt::UserRole + 1, fav);
            item->setSizeHint(QSize(0, 30));
        };
        const auto addHeader = [&](const QString &title) {
            auto item = new QListWidgetItem(title, list);
            item->setFlags(Qt::NoItemFlags);
            item->setData(Qt::UserRole + 2, true);
            item->setSizeHint(QSize(0, 22));
        };

        const auto matches = [&](const QString &family) {
            return filterLower.isEmpty() ||
                   family.toLower().contains(filterLower);
        };

        QStringList favRows;
        QStringList cjkRows;
        QStringList latinRows;
        for (const auto &family : mFamilies) {
            if (!matches(family)) { continue; }
            if (favorites.contains(family)) { favRows << family; }
            else if (familyIsCJK(db, family)) { cjkRows << family; }
            else { latinRows << family; }
        }
        if (!favRows.isEmpty()) {
            addHeader(tr("Favorites"));
            for (const auto &f : favRows) { addRow(f, true); }
        }
        if (!cjkRows.isEmpty()) {
            addHeader(tr("Chinese fonts"));
            for (const auto &f : cjkRows) { addRow(f, false); }
        }
        if (!latinRows.isEmpty()) {
            addHeader(tr("Latin fonts"));
            for (const auto &f : latinRows) { addRow(f, false); }
        }
    };

    QObject::connect(search, &QLineEdit::textChanged, rebuild);
    rebuild(QString());

    list->setItemDelegate(new FontRowDelegate(
                              popup, [search, rebuild]() {
        rebuild(search->text());
    }));

    // capture popup BY VALUE: this lambda outlives showPopup(), a
    // by-reference capture would dangle and crash on click
    QObject::connect(list, &QListWidget::itemClicked,
                     this, [this, popup](QListWidgetItem *item) {
        const auto family = item->data(Qt::UserRole).toString();
        if (item->flags() == Qt::NoItemFlags) { return; }
        setCurrentText(family);
        popup->close();
    });

    const int w = qMax(280, width());
    popup->setFixedSize(w, 440);
    popup->move(mapToGlobal(QPoint(0, height() + 2)));
    popup->show();
    search->setFocus();
}

FontsWidget::FontsWidget(QWidget *parent,
                         const bool toolbar)
    : QWidget(parent)
    , mToolbar(toolbar)
    , mBlockEmit(0)
    , mBlockTextUpdate(false)
    , mFontFamilyPicker(nullptr)
    , mFontStyleCombo(nullptr)
    , mFontSizeSlider(nullptr)
    , mAlignLeft(nullptr)
    , mAlignCenter(nullptr)
    , mAlignRight(nullptr)
    , mAlignTop(nullptr)
    , mAlignVCenter(nullptr)
    , mAlignBottom(nullptr)
    , mTextInput(nullptr)
{
    mFontStyleCombo = new QComboBox(this);
    mFontStyleCombo->setMinimumWidth(20);
    mFontStyleCombo->setFocusPolicy(Qt::NoFocus);
    mFontStyleCombo->setToolTip(tr("Font style"));

    mFontFamilyPicker = new FontFamilyPicker(this);

    mFontSizeSlider = new QDoubleSlider(1, 999, 1, this, false);
    mFontSizeSlider->setMinimumWidth(20);
    mFontSizeSlider->setDisplayedValue(72);
    mFontSizeSlider->setToolTip(tr("Font size"));

    mFontFamilyPicker->setFontFamilies(filterFonts());

    connect(mFontFamilyPicker, &FontFamilyPicker::currentTextChanged,
            this, &FontsWidget::afterFamilyChange);

    connect(mFontStyleCombo, &QComboBox::currentTextChanged,
            this, &FontsWidget::afterStyleChange);

    connect(mFontSizeSlider, &QDoubleSlider::valueEdited,
            this, &FontsWidget::emitSizeChanged);

    QBoxLayout* mMainLayout;
    if (mToolbar) {
        mMainLayout = new QHBoxLayout(this);
        mMainLayout->setContentsMargins(0, 0, 0, 0);
        setContentsMargins(0, 0, 0, 0);
    } else {
        mMainLayout = new QVBoxLayout(this);
        mMainLayout->setContentsMargins(5, 5, 5, 0);
    }

    setLayout(mMainLayout);

    mFontFamilyPicker->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Preferred);
    mFontStyleCombo->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Preferred);
    mFontSizeSlider->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Preferred);

    if (mToolbar) {
        mFontFamilyPicker->setMaximumWidth(200);
        mFontStyleCombo->setMaximumWidth(120);
        mFontSizeSlider->setMaximumWidth(120);
        mFontSizeSlider->setMinimumWidth(80);
    } else {
        mFontFamilyPicker->setMinimumWidth(120);
        mFontStyleCombo->setMinimumWidth(80);
        mFontSizeSlider->setMinimumWidth(60);
    }

    if (!mToolbar) {
        QWidget *fontFamilyWidget = new QWidget(this);
        fontFamilyWidget->setContentsMargins(0, 0, 0, 0);

        QHBoxLayout *fontFamilyLayout = new QHBoxLayout(fontFamilyWidget);
        fontFamilyLayout->setMargin(0);

        fontFamilyLayout->addWidget(mFontFamilyPicker);
        fontFamilyLayout->addWidget(mFontStyleCombo);
        fontFamilyLayout->addWidget(mFontSizeSlider);

        mMainLayout->addWidget(fontFamilyWidget);
    } else {
        mMainLayout->addWidget(mFontFamilyPicker);
        mMainLayout->addWidget(mFontStyleCombo);
        mMainLayout->addWidget(mFontSizeSlider);
    }

    mAlignLeft = new QPushButton(QIcon::fromTheme("alignLeft"),
                                 QString(), this);
    mAlignLeft->setFocusPolicy(Qt::NoFocus);
    mAlignLeft->setToolTip(tr("Align Text Left"));
    connect(mAlignLeft, &QPushButton::pressed,
            this, [this]() { emit textAlignmentChanged(Qt::AlignLeft); });

    mAlignCenter = new QPushButton(QIcon::fromTheme("alignCenter"),
                                   QString(), this);
    mAlignCenter->setFocusPolicy(Qt::NoFocus);
    mAlignCenter->setToolTip(tr("Align Text Center"));
    connect(mAlignCenter, &QPushButton::pressed,
            this, [this]() { emit textAlignmentChanged(Qt::AlignCenter); });

    mAlignRight = new QPushButton(QIcon::fromTheme("alignRight"),
                                  QString(), this);
    mAlignRight->setFocusPolicy(Qt::NoFocus);
    mAlignRight->setToolTip(tr("Align Text Right"));
    connect(mAlignRight, &QPushButton::pressed,
            this, [this]() { emit textAlignmentChanged(Qt::AlignRight); });

    mAlignTop = new QPushButton(QIcon::fromTheme("alignTop"),
                                QString(), this);
    mAlignTop->setFocusPolicy(Qt::NoFocus);
    mAlignTop->setToolTip(tr("Align Text Top"));
    connect(mAlignTop, &QPushButton::pressed,
            this, [this]() { emit textVAlignmentChanged(Qt::AlignTop); });

    mAlignVCenter = new QPushButton(QIcon::fromTheme("alignVCenter"),
                                    QString(), this);
    mAlignVCenter->setFocusPolicy(Qt::NoFocus);
    mAlignVCenter->setToolTip(tr("Align Text Center"));
    connect(mAlignVCenter, &QPushButton::pressed,
            this, [this]() { emit textVAlignmentChanged(Qt::AlignCenter); });

    mAlignBottom = new QPushButton(QIcon::fromTheme("alignBottom"),
                                   QString(), this);
    mAlignBottom->setFocusPolicy(Qt::NoFocus);
    mAlignBottom->setToolTip(tr("Align Text Bottom"));
    connect(mAlignBottom, &QPushButton::pressed,
            this, [this]() { emit textVAlignmentChanged(Qt::AlignBottom); });

    mTextInput = new QPlainTextEdit(this);
    if (mToolbar) {
        mTextInput->setMaximumHeight(eSizesUI::button);
    }
    mTextInput->setPalette(ThemeSupport::getDarkerPalette());
    mTextInput->setAutoFillBackground(true);
    mTextInput->setFocusPolicy(Qt::ClickFocus);
    mTextInput->setPlaceholderText(tr("Enter text ..."));
    connect(mTextInput, &QPlainTextEdit::textChanged,
            this, [this]() {
        emit textChanged(mTextInput->toPlainText());
    });

    eSizesUI::widget.add(mAlignLeft, [this](const int size) {
        Q_UNUSED(size)
        mAlignLeft->setFixedHeight(eSizesUI::button);
        mAlignCenter->setFixedHeight(eSizesUI::button);
        mAlignRight->setFixedHeight(eSizesUI::button);
        mAlignTop->setFixedHeight(eSizesUI::button);
        mAlignVCenter->setFixedHeight(eSizesUI::button);
        mAlignBottom->setFixedHeight(eSizesUI::button);
        //if (mToolbar) { mTextInput->setMaximumHeight(eSizesUI::button); }
    });

    if (!mToolbar) {
        const auto buttonsLayout = new QHBoxLayout;
        buttonsLayout->setContentsMargins(0, 0, 0, 0);

        buttonsLayout->addWidget(mAlignLeft);
        buttonsLayout->addWidget(mAlignCenter);
        buttonsLayout->addWidget(mAlignRight);
        buttonsLayout->addWidget(mAlignTop);
        buttonsLayout->addWidget(mAlignVCenter);
        buttonsLayout->addWidget(mAlignBottom);

        mMainLayout->addLayout(buttonsLayout);
    } else {
        mMainLayout->addWidget(mAlignLeft);
        mMainLayout->addWidget(mAlignCenter);
        mMainLayout->addWidget(mAlignRight);
        mMainLayout->addWidget(mAlignTop);
        mMainLayout->addWidget(mAlignVCenter);
        mMainLayout->addWidget(mAlignBottom);
    }
    mMainLayout->addWidget(mTextInput);

    setDisabled(true);

    afterFamilyChange();

    QTimer::singleShot(100, this, [this]{ setVisible(false); });
}

void FontsWidget::updateStyles()
{
    mBlockEmit++;
    const QString currentStyle = fontStyle();

    mFontStyleCombo->clear();
    QStringList styles = mFontDatabase.styles(fontFamily());
    mFontStyleCombo->addItems(styles);

    if (styles.contains(currentStyle)) {
        mFontStyleCombo->setCurrentText(currentStyle);
    }
    mBlockEmit--;
}

void FontsWidget::afterFamilyChange()
{
    updateStyles();
    emitFamilyAndStyleChanged();
}

void FontsWidget::afterStyleChange()
{
    emitFamilyAndStyleChanged();
}

const QStringList FontsWidget::filterFonts()
{
    QStringList families = mFontDatabase.families();
    // "if the font family is available from two or more foundries the foundry name is included in the family name"

    // Yeah, that's not going to work. I get a lot of "family name [Bits]" and "family name [unknown]" from the font database.
    // This breaks font selection as skia expects the proper font family name (of course).

    // So ...
    QStringList fonts;
    for (int i = 0; i < families.size(); ++i) {
        QString font = families.at(i);
        if (font.startsWith(".")) { continue; } // get a lot of .someKindOfFont on macOS, ignore!
        if (font.contains("[") && font.contains("]")) {
            fonts << font.remove(QRegExp("\\[(.*)\\]")).trimmed();
        } else { fonts << font; }
    }
    fonts.removeDuplicates();
    return fonts;
}

float FontsWidget::fontSize() const
{
    return mFontSizeSlider->value();
}

QString FontsWidget::fontStyle() const
{
    return mFontStyleCombo->currentText();
}

QString FontsWidget::fontFamily() const
{
    return mFontFamilyPicker->currentText();
}

void FontsWidget::setCurrentBox(BoundingBox * const box)
{
    SkScalar fontSize = 0.;
    QString fontFamily;
    SkFontStyle fontStyle;
    QString fontText;
    if (const auto tBox = enve_cast<TextBox*>(box)) {
        fontSize = tBox->getFontSize();
        fontFamily = tBox->getFontFamily();
        fontStyle = tBox->getFontStyle();
        fontText = tBox->getCurrentValue();
        setEnabled(true);
        setBoxTarget(tBox);
        setVisible(true);
    } else {
        clearText();
        setDisabled(true);
        setBoxTarget(nullptr);
        setVisible(false);
    }
    setDisplayedSettings(fontSize,
                         fontFamily,
                         fontStyle,
                         fontText);
}

static QString styleStringHelper(const int weight,
                                 const SkFontStyle::Slant slant)
{
    QString result;
    if (weight > SkFontStyle::kNormal_Weight) {
        if (weight >= SkFontStyle::kBlack_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Black");
        } else if (weight >= SkFontStyle::kExtraBold_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Extra Bold");
        } else if (weight >= SkFontStyle::kBold_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Bold");
        } else if (weight >= SkFontStyle::kSemiBold_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Demi Bold");
        } else if (weight >= SkFontStyle::kMedium_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Medium", "The Medium font weight");
        }
    } else {
        if (weight <= SkFontStyle::kThin_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Thin");
        } else if (weight <= SkFontStyle::kExtraLight_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Extra Light");
        } else if (weight <= SkFontStyle::kLight_Weight) {
            result = QCoreApplication::translate("QFontDatabase", "Light");
        }
    }
    if (slant == SkFontStyle::kItalic_Slant) {
        result += QLatin1Char(' ') + QCoreApplication::translate("QFontDatabase", "Italic");
    } else if (slant == SkFontStyle::kOblique_Slant) {
        result += QLatin1Char(' ') + QCoreApplication::translate("QFontDatabase", "Oblique");
    }
    if (result.isEmpty()) {
        result = QCoreApplication::translate("QFontDatabase", "Regular");
    }
    return result.simplified();
}

void FontsWidget::setDisplayedSettings(const float size,
                                       const QString &family,
                                       const SkFontStyle &style,
                                       const QString &text)
{
    mTextInput->blockSignals(true);
    mTextInput->setPlainText(text);
    mTextInput->blockSignals(false);

    mBlockEmit++;
    mFontFamilyPicker->setCurrentText(family);
    const QString styleStr = styleStringHelper(style.weight(), style.slant());
    if (styleStr.isEmpty()) {
        mFontStyleCombo->setCurrentIndex(0);
    } else {
        mFontStyleCombo->setCurrentText(styleStr);
    }

    mFontSizeSlider->setDisplayedValue(size);
    mBlockEmit--;
}

void FontsWidget::setText(const QString &text)
{
    mTextInput->blockSignals(true);
    mTextInput->setPlainText(text);
    mTextInput->blockSignals(false);
}

const QString FontsWidget::getText()
{
    return mTextInput->toPlainText();
}

void FontsWidget::setTextFocus()
{
    mTextInput->setFocus();
    QTextCursor cursor = mTextInput->textCursor();
    cursor.movePosition(QTextCursor::End);
    mTextInput->setTextCursor(cursor);
}

void FontsWidget::clearText()
{
    mTextInput->blockSignals(true);
    mTextInput->clear();
    mTextInput->blockSignals(false);
}

void FontsWidget::setBoxTarget(TextBox * const target)
{
    mBoxTarget.assign(target);
    if (target) {
        mBoxTarget << connect(this, &FontsWidget::fontSizeChanged,
                              target, [target](const qreal &value) {
            target->setFontSize(value);
            Document::sInstance->fFontSize = value;
            Document::sInstance->actionFinished();
        });
        mBoxTarget << connect(this, &FontsWidget::textChanged,
                              target, [target, this](const QString &value) {
            mBlockTextUpdate = true;
            target->prp_startTransform();
            target->setCurrentValue(value);
            target->prp_finishTransform();
            Document::sInstance->actionFinished();
            mBlockTextUpdate = false;
        });
        mBoxTarget << connect(this, &FontsWidget::fontFamilyAndStyleChanged,
                              target, [target, this](const QString &family,
                                               const SkFontStyle &style) {
            mBlockTextUpdate = true;
            target->setFontFamilyAndStyle(family, style);
            Document::sInstance->fFontFamily = family;
            Document::sInstance->fFontStyle = style;
            Document::sInstance->actionFinished();
            mBlockTextUpdate = false;
        });
        mBoxTarget << connect(this, &FontsWidget::textAlignmentChanged,
                              target, [target](const Qt::Alignment &align) {
            target->setTextHAlignment(align);
            Document::sInstance->actionFinished();
        });
        mBoxTarget << connect(this, &FontsWidget::textVAlignmentChanged,
                              target, [target](const Qt::Alignment &align) {
            target->setTextVAlignment(align);
            Document::sInstance->actionFinished();
        });
        mBoxTarget << connect(target, &Property::prp_currentFrameChanged,
                              this, [this, target]() {
            if (mBlockTextUpdate) { return; }
            setDisplayedSettings(target->getFontSize(),
                                 target->getFontFamily(),
                                 target->getFontStyle(),
                                 target->getCurrentValue());
        });
    }
}

void FontsWidget::clearAll()
{
    setCurrentBox(nullptr);
}

void FontsWidget::emitFamilyAndStyleChanged()
{
    if (mBlockEmit) { return; }
    const auto family = fontFamily();
    const auto style = fontStyle();
    const int qWeight = mFontDatabase.weight(family, style);
    const int weight = QFontWeightToSkFontWeight(qWeight);
    const int width = SkFontStyle::kNormal_Width;
//    const bool italic = mFontDatabase.italic(family, style);
//    const auto slant = italic ? SkFontStyle::kItalic_Slant :
//                                SkFontStyle::kUpright_Slant;
    const auto qFont = mFontDatabase.font(family, style, 10);
    const auto slant = toSkSlant(qFont.style());
    const SkFontStyle skStyle(weight, width, slant);
    emit fontFamilyAndStyleChanged(family, skStyle);
}

void FontsWidget::emitSizeChanged()
{
    if (mBlockEmit) { return; }
    emit fontSizeChanged(fontSize());
}
