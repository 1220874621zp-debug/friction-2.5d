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

#include "effectspresetspanel.h"
#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QShortcut>
#include <QDrag>
#include <QMimeData>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStackedWidget>
#include <QToolButton>
#include <QTimer>
#include <QScrollArea>
#include <QMouseEvent>
#include <QButtonGroup>
#include <QPainter>
#include <QtConcurrent/QtConcurrentMap>

#include "Boxes/boundingbox.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "RasterEffects/effectpreview.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "PathEffects/patheffectmenucreator.h"
#include "widgets/flowlayout.h"
#include "themesupport.h"
#include "appsupport.h"
#include "effectsloader.h"

namespace {
// drag payload = generation token; the closure itself lives in the
// panel's static registry (see beginEffectDrag/takeEffectDrag)
class EffectsTreeWidget : public QTreeWidget {
public:
    explicit EffectsTreeWidget(EffectsPresetsPanel* const panel,
                               QWidget* const parent = nullptr) :
        QTreeWidget(parent), mPanel(panel) {}

protected:
    void startDrag(Qt::DropActions) {
        const auto item = currentItem();
        if (!item || !mPanel) { return; }
        const auto apply = mPanel->effectCallback(item);
        if (!apply) { return; }
        auto mimeData = new QMimeData;
        mimeData->setData(EffectsPresetsPanel::sMimeFormat(),
                          EffectsPresetsPanel::beginEffectDrag(apply));
        QDrag drag(this);
        drag.setMimeData(mimeData);
        const auto pm = item->icon(0).pixmap(32, 32);
        if (!pm.isNull()) {
            drag.setPixmap(pm);
            drag.setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
        }
        drag.exec(Qt::CopyAction);
    }

    void contextMenuEvent(QContextMenuEvent* event) {
        if (mPanel) { mPanel->showTreeContextMenu(event->globalPos()); }
    }

private:
    EffectsPresetsPanel* mPanel = nullptr;
};

QByteArray readUserPresetStream(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QString data = doc.object().value("data").toString();
    if (data.isEmpty()) { return QByteArray(); }
    return QByteArray::fromBase64(data.toLatin1());
}

// worker-thread render input: pure data, no widget pointers
struct TileRenderParams {
    RasterEffectType type = RasterEffectType::BLUR;
    int nFrames = 16;
    QSize size = QSize(160, 160);
};

// runs on a QtConcurrent worker thread: CPU-only offscreen effect
// evaluation (see core EffectPreview), safe without GL or a scene
QList<QImage> renderTilePreview(const TileRenderParams& p)
{
    return EffectPreview::renderEffectFrames(p.type, p.nFrames, p.size);
}
}

// ------------------------------------------------------- preview & tile

EffectPreviewArea::EffectPreviewArea(QWidget* const parent)
    : QWidget(parent)
{
    setMinimumHeight(60);
}

void EffectPreviewArea::setFrames(const QList<QImage>& frames)
{
    mFrames = frames;
    mFrame = 0;
    mPlaceholder.clear();
    update();
}

void EffectPreviewArea::advance()
{
    if (mFrames.count() > 1) {
        mFrame = (mFrame + 1) % mFrames.count();
        update();
    }
}

void EffectPreviewArea::setPlaceholder(const QString& text)
{
    mPlaceholder = text;
    update();
}

void EffectPreviewArea::setLightBase(const bool light)
{
    mLightBase = light;
    update();
}

void EffectPreviewArea::paintEvent(QPaintEvent* const e)
{
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // rounded frame; content is clipped to it. Shadow-type effects
    // use a light base so their black output stays readable
    const int rad = ThemeSupport::borderRadius();
    QPainterPath frame;
    frame.addRoundedRect(rect().adjusted(0, 0, -1, -1), rad, rad);
    p.fillPath(frame, mLightBase ? QColor(225, 228, 233)
                                 : ThemeSupport::getThemeBaseDarkerColor());
    p.save();
    p.setClipPath(frame);
    if (!mFrames.isEmpty()) {
        const auto& img = mFrames.at(mFrame % mFrames.count());
        const qreal s = qMin(static_cast<qreal>(width() - 8)/img.width(),
                             static_cast<qreal>(height() - 8)/img.height());
        const int w = qRound(img.width()*s);
        const int h = qRound(img.height()*s);
        p.drawImage(QRect((width() - w)/2, (height() - h)/2, w, h), img);
    } else if (!mPlaceholder.isEmpty()) {
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor(140, 140, 140));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, mPlaceholder);
    }
    p.restore();
}

EffectPreviewTile::EffectPreviewTile(const RasterEffectType type,
                                     const QString& name,
                                     const QString& category,
                                     const EffectApplyFn& apply,
                                     QWidget* const parent)
    : QWidget(parent)
    , mType(type)
    , mName(name)
    , mCategory(category)
    , mApply(apply)
{
    const auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(5, 5, 5, 5);
    lay->setSpacing(3);

    mPreviewArea = new EffectPreviewArea(this);
    mPreviewArea->setFixedSize(130, 130);
    lay->addWidget(mPreviewArea, 0, Qt::AlignHCenter);

    mNameLabel = new QLabel(mName, this);
    QFont nf = mNameLabel->font();
    nf.setPixelSize(11);
    nf.setBold(true);
    mNameLabel->setFont(nf);
    mNameLabel->setAlignment(Qt::AlignCenter);
    mNameLabel->setStyleSheet(QStringLiteral("color: %1;")
                              .arg(palette().color(QPalette::WindowText).name()));
    lay->addWidget(mNameLabel);

    mTagLabel = new QLabel(mCategory, this);
    QFont tf = mTagLabel->font();
    tf.setPixelSize(10);
    mTagLabel->setFont(tf);
    mTagLabel->setAlignment(Qt::AlignCenter);
    mTagLabel->setStyleSheet(QStringLiteral("color: %1;")
                             .arg(ThemeSupport::getThemeColorTextDisabled().name()));
    lay->addWidget(mTagLabel);

    mApplyBtn = new QPushButton(QString::fromUtf8("应用"), this);
    QFont bf = mApplyBtn->font();
    bf.setPixelSize(10);
    bf.setBold(true);
    mApplyBtn->setFont(bf);
    mApplyBtn->setFixedHeight(22);
    mApplyBtn->setCursor(Qt::PointingHandCursor);
    const QColor btnBg = ThemeSupport::getThemeButtonBaseColor(240);
    const QColor btnBorder = ThemeSupport::getThemeButtonBorderColor(180);
    const QColor btnHoverBg = ThemeSupport::getThemeHighlightColor();
    const int rad = ThemeSupport::borderRadius() > 3 ? 3 : ThemeSupport::borderRadius();
    mApplyBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 0px 8px; font-weight: bold; }"
        "QPushButton:hover { background: %5; border-color: %5; color: #ffffff; }"
    ).arg(btnBg.name(QColor::HexArgb),
          palette().color(QPalette::WindowText).name(),
          btnBorder.name(QColor::HexArgb),
          QString::number(rad),
          btnHoverBg.name()));
    mApplyBtn->setToolTip(QString::fromUtf8("将「%1」添加到全部选中图层").arg(mName));
    lay->addWidget(mApplyBtn, 0, Qt::AlignHCenter);
    connect(mApplyBtn, &QPushButton::clicked,
            this, [this]() { emit applyRequested(this); });

    setToolTip(QString::fromUtf8(
                "双击或点击「应用」添加到选中图层；\n"
                "也可以拖拽到画布或时间轴图层行。"));
    setCursor(Qt::PointingHandCursor);
}

void EffectPreviewTile::setLoading()
{
    if (mPreviewArea) {
        mPreviewArea->setPlaceholder(QString::fromUtf8("渲染中..."));
    }
}

void EffectPreviewTile::setChecked(const bool checked)
{
    mChecked = checked;
    update();
}

bool EffectPreviewTile::matches(const QString& query,
                                const QString& categoryTag) const
{
    if (categoryTag != QStringLiteral("all") && mCategory != categoryTag) {
        return false;
    }
    if (!query.isEmpty() &&
        !mName.contains(query, Qt::CaseInsensitive) &&
        !mCategory.contains(query, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

void EffectPreviewTile::mousePressEvent(QMouseEvent* const e)
{
    if (e->button() == Qt::LeftButton) {
        mDragStart = e->pos();
        mDragging = false;
        emit tileClicked(this);
    }
    QWidget::mousePressEvent(e);
}

void EffectPreviewTile::mouseMoveEvent(QMouseEvent* const e)
{
    if (e->buttons() & Qt::LeftButton && !mDragging &&
        (e->pos() - mDragStart).manhattanLength() > 8) {
        mDragging = true;
        startEffectDrag();
    }
    QWidget::mouseMoveEvent(e);
}

void EffectPreviewTile::mouseDoubleClickEvent(QMouseEvent* const e)
{
    Q_UNUSED(e)
    emit applyRequested(this);
}

void EffectPreviewTile::paintEvent(QPaintEvent* const e)
{
    Q_UNUSED(e)
    if (!mChecked) { return; }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int rad = ThemeSupport::borderRadius();
    QPainterPath frame;
    frame.addRoundedRect(rect().adjusted(0, 0, -1, -1), rad, rad);
    QPen pen(ThemeSupport::getThemeHighlightColor(), 2);
    p.setPen(pen);
    p.drawPath(frame);
}

QSize EffectPreviewTile::sizeHint() const
{
    const int w = mPreviewArea ? mPreviewArea->width() + 10 : 140;
    const int h = mPreviewArea ? mPreviewArea->height() + 84 : 214;
    return QSize(w, h);
}

void EffectPreviewTile::startEffectDrag()
{
    if (!mApply) { return; }
    auto mimeData = new QMimeData;
    mimeData->setData(EffectsPresetsPanel::sMimeFormat(),
                      EffectsPresetsPanel::beginEffectDrag(mApply));
    QDrag drag(this);
    drag.setMimeData(mimeData);
    const auto pm = grab().scaled(64, 64,
                                  Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation);
    if (!pm.isNull()) {
        drag.setPixmap(pm);
        drag.setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
    }
    drag.exec(Qt::CopyAction);
}

quint64 EffectsPresetsPanel::sDragGeneration = 0;
EffectsPresetsPanel::EffectApplyFn EffectsPresetsPanel::sDragCallback;

const QString& EffectsPresetsPanel::sMimeFormat()
{
    static const QString format = QStringLiteral(
                "application/x-friction-effect");
    return format;
}

QByteArray EffectsPresetsPanel::beginEffectDrag(
        const EffectApplyFn &apply)
{
    sDragCallback = apply;
    return QByteArray::number(++sDragGeneration);
}

EffectsPresetsPanel::EffectApplyFn EffectsPresetsPanel::takeEffectDrag(
        const QByteArray &token)
{
    if (token != QByteArray::number(sDragGeneration)) { return nullptr; }
    auto apply = sDragCallback;
    sDragCallback = nullptr;
    return apply;
}

EffectsPresetsPanel::EffectsPresetsPanel(MainWindow * const mainWindow,
                                         QWidget * const parent) :
    QWidget(parent),
    mMainWindow(mainWindow)
{
    const auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // top row: search + view toggle (classic tree / visual cards)
    const auto searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->setSpacing(2);

    mSearchEdit = new QLineEdit(this);
    mSearchEdit->setPlaceholderText(tr("Search Effects & Presets..."));
    mSearchEdit->setClearButtonEnabled(true);
    connect(mSearchEdit, &QLineEdit::textChanged,
            this, &EffectsPresetsPanel::onSearchTextChanged);
    searchRow->addWidget(mSearchEdit, 1);

    mViewToggleBtn = new QToolButton(this);
    mViewToggleBtn->setCheckable(true);
    auto gridIcon = QIcon::fromTheme(QStringLiteral("view-list-icons"));
    if (gridIcon.isNull()) {
        gridIcon = QIcon::fromTheme(QStringLiteral("view-grid"));
    }
    if (!gridIcon.isNull()) { mViewToggleBtn->setIcon(gridIcon); }
    else { mViewToggleBtn->setText(QString::fromUtf8("卡片")); }
    mViewToggleBtn->setToolTip(QString::fromUtf8(
                "在经典列表与可视化卡片视图之间切换"));
    mViewToggleBtn->setFocusPolicy(Qt::NoFocus);
    connect(mViewToggleBtn, &QToolButton::toggled,
            this, &EffectsPresetsPanel::onViewModeToggled);
    searchRow->addWidget(mViewToggleBtn, 0);
    mainLayout->addLayout(searchRow);

    // two views sharing the toolbar and bottom buttons
    mStack = new QStackedWidget(this);

    mTreeWidget = new EffectsTreeWidget(this, this);
    mTreeWidget->setHeaderHidden(true);
    mTreeWidget->setAnimated(true);
    mTreeWidget->setIndentation(16);
    mTreeWidget->setPalette(ThemeSupport::getDefaultPalette());
    mTreeWidget->setFrameShape(QFrame::NoFrame);
    mTreeWidget->setDragEnabled(true);
    mTreeWidget->setDragDropMode(QAbstractItemView::DragOnly);
    mTreeWidget->setDefaultDropAction(Qt::CopyAction);
    mTreeWidget->setContextMenuPolicy(Qt::DefaultContextMenu);
    connect(mTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &EffectsPresetsPanel::onItemDoubleClicked);
    mStack->addWidget(mTreeWidget);

    mStack->addWidget(buildGridView());
    mainLayout->addWidget(mStack, 1);

    const auto returnShortcut = new QShortcut(QKeySequence(Qt::Key_Return), mTreeWidget);
    connect(returnShortcut, &QShortcut::activated,
            this, &EffectsPresetsPanel::onApplyPressed);

    const auto enterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), mTreeWidget);
    connect(enterShortcut, &QShortcut::activated,
            this, &EffectsPresetsPanel::onApplyPressed);

    // Bottom tool buttons: Import custom effect / Open folder / Refresh
    const auto btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(2);

    const auto importBtn = new QPushButton(QIcon::fromTheme("document-open"), tr("Import..."), this);
    importBtn->setToolTip(tr("Import custom GLSL shader effect (.frag / .json)"));
    importBtn->setFocusPolicy(Qt::NoFocus);
    connect(importBtn, &QPushButton::clicked, this, &EffectsPresetsPanel::onImportEffectClicked);

    const auto folderBtn = new QPushButton(QIcon::fromTheme("file_folder"), tr("Folder"), this);
    folderBtn->setToolTip(tr("Open custom effects & shaders directory"));
    folderBtn->setFocusPolicy(Qt::NoFocus);
    connect(folderBtn, &QPushButton::clicked, this, &EffectsPresetsPanel::onOpenFolderClicked);

    const auto refreshBtn = new QPushButton(QIcon::fromTheme("reload"), tr("Refresh"), this);
    refreshBtn->setToolTip(tr("Reload shader effects and presets"));
    refreshBtn->setFocusPolicy(Qt::NoFocus);
    connect(refreshBtn, &QPushButton::clicked, this, &EffectsPresetsPanel::onRefreshClicked);

    btnLayout->addWidget(importBtn);
    btnLayout->addWidget(folderBtn);
    btnLayout->addWidget(refreshBtn);
    mainLayout->addLayout(btnLayout);

    // gallery playback: advances every visible tile's preview
    mPlayTimer = new QTimer(this);
    mPlayTimer->setInterval(40);
    connect(mPlayTimer, &QTimer::timeout, this, [this]() {
        if (mStack->currentIndex() != 1) { return; }
        for (const auto tile : mTiles) {
            if (tile && tile->isVisible()) { tile->advance(); }
        }
    });

    populateEffects();

    // restore the last view mode (card grid is the default: it is the
    // whole point of the panel for browsing; classic-tree users
    // switch once and their choice sticks)
    const bool gridView = AppSupport::getSettings(QStringLiteral("EffectsPanel"),
                                                  QStringLiteral("gridView"),
                                                  true).toBool();
    if (gridView) { mViewToggleBtn->setChecked(true); }
    else { mStack->setCurrentIndex(0); }
}

void EffectsPresetsPanel::focusSearch()
{
    if (mSearchEdit) {
        mSearchEdit->setFocus();
        mSearchEdit->selectAll();
    }
}

void EffectsPresetsPanel::addEffectItem(const QString &categoryName,
                                        const QString &effectName,
                                        const QString &desc,
                                        const EffectApplyFn &applyFunc)
{
    QTreeWidgetItem *catItem = mCategoryItems.value(categoryName, nullptr);
    if (!catItem) {
        catItem = new QTreeWidgetItem(mTreeWidget);
        catItem->setText(0, categoryName);
        catItem->setIcon(0, QIcon::fromTheme("file_folder"));
        QFont f = catItem->font(0);
        f.setBold(true);
        catItem->setFont(0, f);
        mCategoryItems[categoryName] = catItem;
    }

    const auto item = new QTreeWidgetItem(catItem);
    item->setText(0, effectName);
    item->setIcon(0, QIcon::fromTheme("effect"));
    if (!desc.isEmpty()) {
        item->setToolTip(0, desc);
    }
    mApplyCallbacks[item] = applyFunc;
}

void EffectsPresetsPanel::populateUserPresets()
{
    // user-saved effect stacks (".ffp"): layer right-click
    // "保存为效果预设..." writes them; double-click / Enter / drag
    // applies the whole stack (appending) to the target layer(s)
    const QString dirPath = AppSupport::getAppUserFxPresetsPath();
    const QDir dir(dirPath, QStringLiteral("*.ffp"),
                   QDir::Name | QDir::IgnoreCase, QDir::Files);
    const auto entries = dir.entryInfoList();
    for (const auto& fi : entries) {
        QString name;
        {
            QFile file(fi.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly)) {
                name = QJsonDocument::fromJson(file.readAll()).object()
                        .value("name").toString();
                file.close();
            }
        }
        const auto apply = [this, path = fi.absoluteFilePath()](
                BoundingBox* const target) {
            if (!mMainWindow) { return; }
            mMainWindow->applyRasterEffectStreamToTargets(
                        readUserPresetStream(path), target);
        };
        addEffectItem(tr("我的效果预设"),
                      name.isEmpty() ? fi.completeBaseName() : name,
                      tr("效果栈预设（双击应用到全部选中图层，拖拽到指定图层）"),
                      apply);
        mUserPresetPaths[mApplyCallbacks.lastKey()] = fi.absoluteFilePath();
    }
}

void EffectsPresetsPanel::populateEffects()
{
    mTreeWidget->clear();
    mCategoryItems.clear();
    mApplyCallbacks.clear();
    mUserPresetPaths.clear();

    populateUserPresets();

    // 1. Raster Effects
    RasterEffectMenuCreator::forEveryEffectCore(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            QString category = cat.isEmpty() ? tr("General") : cat;
            addEffectItem(category, name, QString(), [this, creator](BoundingBox* target) {
                if (mMainWindow) mMainWindow->addRasterEffectToTarget(creator, target);
            });
        });

    RasterEffectMenuCreator::forEveryEffectCustom(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            QString category = cat.isEmpty() ? tr("Custom") : cat;
            addEffectItem(category, name, QString(), [this, creator](BoundingBox* target) {
                if (mMainWindow) mMainWindow->addRasterEffectToTarget(creator, target);
            });
        });

    RasterEffectMenuCreator::forEveryEffectShader(
        [this](const QString &name, const QString &cat,
               const RasterEffectMenuCreator::EffectCreator &creator) {
            QString category = cat.isEmpty() ? tr("Shader") : cat;
            addEffectItem(category, name, QString(), [this, creator](BoundingBox* target) {
                if (mMainWindow) mMainWindow->addRasterEffectToTarget(creator, target);
            });
        });

    // 2. Path Effects
    PathEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const PathEffectMenuCreator::EffectCreator &creator) {
            addEffectItem(tr("Path Effects"), name, QString(), [this, creator](BoundingBox* target) {
                if (mMainWindow) mMainWindow->addPathEffectToTarget(creator, target);
            });
        });

    // 3. Blend Effects
    BlendEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const BlendEffectMenuCreator::EffectCreator &creator) {
            addEffectItem(tr("Blend Effects"), name, QString(), [this, creator](BoundingBox* target) {
                if (mMainWindow) mMainWindow->addBlendEffectToTarget(creator, target);
            });
        });

    // 4. Transform Effects
    TransformEffectMenuCreator::forEveryEffect(
        [this](const QString &name,
               const TransformEffectMenuCreator::EffectCreator &creator) {
            addEffectItem(tr("Transform Effects"), name, QString(), [this, creator](BoundingBox* target) {
                if (mMainWindow) mMainWindow->addTransformEffectToTarget(creator, target);
            });
        });

    mTreeWidget->expandAll();
}

QWidget* EffectsPresetsPanel::buildGridView()
{
    const auto page = new QWidget(this);
    const auto lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    // horizontal category pill bar
    const auto catScroll = new QScrollArea(page);
    catScroll->setWidgetResizable(true);
    catScroll->setFixedHeight(34);
    catScroll->setFrameShape(QFrame::NoFrame);
    catScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    catScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    catScroll->setAutoFillBackground(false);
    catScroll->viewport()->setAutoFillBackground(false);

    const auto catHost = new QWidget(catScroll);
    mCatLayout = new QHBoxLayout(catHost);
    mCatLayout->setContentsMargins(0, 1, 0, 1);
    mCatLayout->setSpacing(5);
    mCatGroup = new QButtonGroup(this);
    mCatGroup->setExclusive(true);
    connect(mCatGroup,
            static_cast<void(QButtonGroup::*)(QAbstractButton*)>(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton* const btn) {
        if (btn) {
            mActiveCategory = btn->property("catTag").toString();
            filterTiles();
        }
    });

    // placeholder button ("全部") so the bar is never empty before
    // buildTiles runs; buildTiles clears and repopulates it
    const auto allBtn = new QPushButton(QString::fromUtf8("全部"), catHost);
    allBtn->setCheckable(true);
    allBtn->setChecked(true);
    allBtn->setProperty("catTag", QStringLiteral("all"));
    mCatGroup->addButton(allBtn);
    mCatLayout->addWidget(allBtn);
    mCatLayout->addStretch(1);
    catScroll->setWidget(catHost);
    lay->addWidget(catScroll);

    // card gallery
    mGridScroll = new QScrollArea(page);
    mGridScroll->setWidgetResizable(true);
    mGridScroll->setFrameShape(QFrame::NoFrame);
    mGridScroll->setAutoFillBackground(true);
    mGridScroll->setPalette(ThemeSupport::getDarkPalette());
    mGridHost = new QWidget(mGridScroll);
    mGridHost->setPalette(ThemeSupport::getDarkPalette());
    mGridHost->setAutoFillBackground(true);
    mFlow = new FlowLayout(mGridHost);
    mGridScroll->setWidget(mGridHost);
    lay->addWidget(mGridScroll, 1);

    return page;
}

QString EffectsPresetsPanel::categoryTag(const QString& category) const
{
    return category.isEmpty() ? tr("General") : category;
}

void EffectsPresetsPanel::buildTiles()
{
    if (mTilesBuilt) { return; }
    mTilesBuilt = true;

    struct Entry {
        QString name;
        QString cat;
        RasterEffectType type;
        RasterEffectMenuCreator::EffectCreator creator;
    };
    QList<Entry> entries;
    RasterEffectMenuCreator::forEveryEffectCore(
                [&entries](const QString& name, const QString& cat,
                           const RasterEffectMenuCreator::EffectCreator& creator) {
        const auto inst = creator();
        if (!inst) { return; }
        entries << Entry{ name, cat, inst->getEffectType(), creator };
    });

    // category pills in first-seen order
    QStringList cats;
    for (const auto& e : entries) {
        const auto tag = categoryTag(e.cat);
        if (!cats.contains(tag)) { cats << tag; }
    }

    // reset the pill bar (remove the constructor placeholder)
    while (mCatLayout->count() > 0) {
        const auto item = mCatLayout->takeAt(0);
        if (const auto w = item->widget()) { w->deleteLater(); }
        delete item;
    }
    const auto addPill = [this](const QString& label, const QString& tag) {
        auto btn = new QPushButton(label, mGridHost);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        const int rad = ThemeSupport::borderRadius() > 11 ? 11 : qMax(ThemeSupport::borderRadius(), 4);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: %1;"
            "  color: %2;"
            "  border: 1px solid %3;"
            "  border-radius: %4px;"
            "  padding: 2px 10px;"
            "  font-size: 11px;"
            "  font-weight: 500;"
            "}"
            "QPushButton:hover {"
            "  background: %5;"
            "  border-color: %6;"
            "}"
            "QPushButton:checked {"
            "  background: %6;"
            "  border-color: %6;"
            "  color: #ffffff;"
            "  font-weight: bold;"
            "}"
        ).arg(ThemeSupport::getThemeButtonBaseColor().name(),
              ThemeSupport::getThemeColorTextDisabled().name(),
              ThemeSupport::getThemeButtonBorderColor().name(),
              QString::number(rad),
              ThemeSupport::getThemeButtonHoverColor().name(),
              ThemeSupport::getThemeHighlightColor().name()));
        btn->setProperty("catTag", tag);
        if (tag == mActiveCategory) { btn->setChecked(true); }
        mCatGroup->addButton(btn);
        mCatLayout->addWidget(btn);
    };
    addPill(QString::fromUtf8("全部"), QStringLiteral("all"));
    for (const auto& c : cats) { addPill(c, c); }
    mCatLayout->addStretch(1);
    if (mActiveCategory != QStringLiteral("all") && !cats.contains(mActiveCategory)) {
        mActiveCategory = QStringLiteral("all");
        const auto btns = mCatGroup->buttons();
        if (!btns.isEmpty()) { btns.first()->setChecked(true); }
    }

    for (const auto& e : entries) {
        const auto apply = [this, creator = e.creator](BoundingBox* const target) {
            if (mMainWindow) { mMainWindow->addRasterEffectToTarget(creator, target); }
        };
        const auto tile = new EffectPreviewTile(e.type, e.name,
                                                categoryTag(e.cat),
                                                apply, mGridHost);
        connect(tile, &EffectPreviewTile::applyRequested,
                this, &EffectsPresetsPanel::onTileApplyRequested);
        connect(tile, &EffectPreviewTile::tileClicked,
                this, &EffectsPresetsPanel::onTileClicked);
        mFlow->addWidget(tile);
        mTiles << tile;
        if (e.type == RasterEffectType::SHADOW ||
            e.type == RasterEffectType::DROP_SHADOW) {
            // black shadows vanish on the dark base
            tile->setLightPreview();
        }
        if (!EffectPreview::canPreview(e.type)) {
            // not "loading": nothing will ever arrive for this tile
            tile->setUnavailable();
        }
    }

    filterTiles();
}

void EffectsPresetsPanel::queueTileRender()
{
    if (mTiles.isEmpty()) { return; }
    if (!isVisible() || mStack->currentIndex() != 1) {
        mRenderOnShow = true;
        return;
    }

    QVector<TileRenderParams> params;
    QVector<QPointer<EffectPreviewTile>> targets;
    for (const auto tile : mTiles) {
        if (!tile || !EffectPreview::canPreview(tile->effectType())) { continue; }
        TileRenderParams p;
        p.type = tile->effectType();
        p.nFrames = 16;
        p.size = QSize(160, 160);
        params << p;
        targets << tile;
        tile->setLoading();
    }
    if (params.isEmpty()) { return; }

    const int gen = mTileGeneration;
    const auto targetsPtr =
            QSharedPointer<QVector<QPointer<EffectPreviewTile>>>::create(targets);
    const auto watcher = new QFutureWatcher<QList<QImage>>(this);
    connect(watcher, &QFutureWatcher<QList<QImage>>::finished,
            this, [this, watcher, targetsPtr, gen]() {
        watcher->deleteLater();
        if (gen != mTileGeneration) { return; }
        const auto results = watcher->future().results();
        const int n = qMin(results.size(), targetsPtr->size());
        for (int i = 0; i < n; i++) {
            const auto tile = targetsPtr->at(i);
            if (!tile) { continue; }
            const auto& frames = results.at(i);
            if (frames.isEmpty()) {
                // the renderer bailed out (exception / unsupported)
                tile->setUnavailable();
            } else {
                tile->setTileFrames(frames);
            }
        }
    });
    watcher->setFuture(QtConcurrent::mapped(params, &renderTilePreview));
}

void EffectsPresetsPanel::filterTiles()
{
    const QString filter = mSearchEdit ? mSearchEdit->text().trimmed() : QString();
    for (const auto tile : mTiles) {
        if (tile) { tile->setVisible(tile->matches(filter, mActiveCategory)); }
    }
}

void EffectsPresetsPanel::updatePlayTimer()
{
    const bool active = isVisible() && mStack && mStack->currentIndex() == 1
                        && !mGalleryPaused;
    if (active) { mPlayTimer->start(); }
    else { mPlayTimer->stop(); }
}

void EffectsPresetsPanel::onViewModeToggled(const bool checked)
{
    if (!mStack) { return; }
    mStack->setCurrentIndex(checked ? 1 : 0);
    AppSupport::setSettings(QStringLiteral("EffectsPanel"),
                            QStringLiteral("gridView"), checked);
    if (checked) {
        buildTiles();
        queueTileRender();
    }
    updatePlayTimer();
}

void EffectsPresetsPanel::onTileApplyRequested(EffectPreviewTile* const tile)
{
    if (!tile) { return; }
    tile->applyNow();
}

void EffectsPresetsPanel::onTileClicked(EffectPreviewTile* const tile)
{
    if (!tile) { return; }
    for (const auto t : mTiles) {
        if (t) { t->setChecked(t == tile); }
    }
}

void EffectsPresetsPanel::setGalleryPaused(const bool paused)
{
    mGalleryPaused = paused;
    updatePlayTimer();
}

void EffectsPresetsPanel::showEvent(QShowEvent* const e)
{
    QWidget::showEvent(e);
    if (mRenderOnShow && mStack && mStack->currentIndex() == 1) {
        mRenderOnShow = false;
        queueTileRender();
    }
    updatePlayTimer();
}

void EffectsPresetsPanel::hideEvent(QHideEvent* const e)
{
    QWidget::hideEvent(e);
    updatePlayTimer();
}

void EffectsPresetsPanel::showTreeContextMenu(const QPoint &globalPos)
{
    const auto item = mTreeWidget->itemAt(
                mTreeWidget->mapFromGlobal(globalPos));
    if (!item) { return; }
    const auto it = mUserPresetPaths.find(item);
    if (it == mUserPresetPaths.end()) { return; }
    QMenu menu(this);
    const auto delAct = menu.addAction(tr("删除预设"), this, [this, path = it.value()]() {
        if (QFile::exists(path)) { QFile::remove(path); }
        populateEffects();
    });
    Q_UNUSED(delAct)
    menu.exec(globalPos);
}

void EffectsPresetsPanel::onSearchTextChanged(const QString &text)
{
    const QString filter = text.trimmed().toLower();
    const bool isSearching = !filter.isEmpty();

    for (auto catIt = mCategoryItems.begin(); catIt != mCategoryItems.end(); ++catIt) {
        QTreeWidgetItem *cat = catIt.value();
        bool catMatch = cat->text(0).toLower().contains(filter);
        int visibleChildren = 0;

        for (int i = 0; i < cat->childCount(); ++i) {
            QTreeWidgetItem *child = cat->child(i);
            bool childMatch = child->text(0).toLower().contains(filter) || catMatch;
            child->setHidden(isSearching && !childMatch);
            if (!child->isHidden()) {
                visibleChildren++;
            }
        }

        cat->setHidden(isSearching && visibleChildren == 0);
        if (isSearching && visibleChildren > 0) {
            cat->setExpanded(true);
        }
    }

    filterTiles();
}

void EffectsPresetsPanel::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (!item) { return; }
    if (mApplyCallbacks.contains(item)) {
        mApplyCallbacks[item](nullptr);
    }
}

void EffectsPresetsPanel::onApplyPressed()
{
    const auto item = mTreeWidget->currentItem();
    if (item && mApplyCallbacks.contains(item)) {
        mApplyCallbacks[item](nullptr);
    }
}

void EffectsPresetsPanel::onImportEffectClicked()
{
    const QString targetDir = AppSupport::getAppShaderEffectsPath();
    QDir().mkpath(targetDir);

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Import Custom GLSL Shader Effect"),
        QDir::homePath(),
        tr("Shader Effects (*.frag *.json *.glsl);;All Files (*)")
    );

    if (files.isEmpty()) { return; }

    int importedCount = 0;
    for (const auto &filePath : files) {
        const QFileInfo fi(filePath);
        const QString destPath = targetDir + QDir::separator() + fi.fileName();
        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        if (QFile::copy(filePath, destPath)) {
            importedCount++;
        }
    }

    if (EffectsLoader::sInstance) {
        EffectsLoader::sInstance->iniShaderEffects();
    }
    populateEffects();

    QMessageBox::information(
        this,
        tr("Import Complete"),
        tr("Successfully imported %1 shader effect(s).\nSaved to: %2").arg(importedCount).arg(targetDir)
    );
}

void EffectsPresetsPanel::onOpenFolderClicked()
{
    const QString dirPath = AppSupport::getAppShaderEffectsPath();
    QDir().mkpath(dirPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void EffectsPresetsPanel::onRefreshClicked()
{
    if (EffectsLoader::sInstance) {
        EffectsLoader::sInstance->iniShaderEffects();
    }
    populateEffects();
}
