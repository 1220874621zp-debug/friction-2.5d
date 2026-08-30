/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "aepropertiesinspector.h"
#include "canvas.h"
#include "Boxes/boundingbox.h"
#include "Boxes/pathbox.h"
#include "Boxes/boxwithpatheffects.h"
#include "Boxes/adjustmentlayer.h"
#include "Boxes/nullobject.h"
#include "Animators/transformanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/paintsettingsanimator.h"
#include "Animators/outlinesettingsanimator.h"
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffect.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "themesupport.h"
#include "Private/document.h"
#include "widgets/qrealanimatorvalueslider.h"
#include "widgets/colorsettingswidget.h"

#include <QColorDialog>
#include <QMenu>
#include <QWidgetAction>
#include <QIcon>
#include <QPainter>
#include <QEvent>
#include <QPainterPath>

// ============================================================================
// KeyframeDiamondButton implementation
// ============================================================================
KeyframeDiamondButton::KeyframeDiamondButton(Animator *anim, Canvas *scene, QWidget *parent)
    : QWidget(parent)
    , mAnim(anim)
    , mScene(scene)
{
    setFixedSize(14, 16);
    setCursor(Qt::PointingHandCursor);
}

void KeyframeDiamondButton::setDualAnimators(Animator *animX, Animator *animY)
{
    mAnim = animX;
    mAnimY = animY;
    update();
}

void KeyframeDiamondButton::setScene(Canvas *scene)
{
    mScene = scene;
    update();
}

void KeyframeDiamondButton::enterEvent(QEvent *)
{
    mHovered = true;
    update();
}

void KeyframeDiamondButton::leaveEvent(QEvent *)
{
    mHovered = false;
    update();
}

void KeyframeDiamondButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool hasKey = (mAnim && mAnim->anim_getKeyOnCurrentFrame()) ||
                        (mAnimY && mAnimY->anim_getKeyOnCurrentFrame());
    const bool isRec = (mAnim && (mAnim->anim_isRecording() || mAnim->anim_hasKeys())) ||
                       (mAnimY && (mAnimY->anim_isRecording() || mAnimY->anim_hasKeys()));

    const qreal cx = width() / 2.0;
    const qreal cy = height() / 2.0;
    const qreal r = 3.8;

    QPolygonF diamond;
    diamond << QPointF(cx, cy - r)
            << QPointF(cx + r, cy)
            << QPointF(cx, cy + r)
            << QPointF(cx - r, cy);

    if (hasKey) {
        p.setBrush(ThemeSupport::getThemeHighlightColor());
        p.setPen(QPen(mHovered ? Qt::white : ThemeSupport::getThemeHighlightSelectedColor(), 0.8));
        p.drawPolygon(diamond);
    } else if (isRec) {
        p.setBrush(mHovered ? ThemeSupport::getThemeHighlightColor(35) : Qt::NoBrush);
        p.setPen(QPen(mHovered ? ThemeSupport::getThemeHighlightColor() : QColor(165, 165, 175), 1.2));
        p.drawPolygon(diamond);
    } else {
        p.setBrush(mHovered ? QColor(255, 255, 255, 20) : Qt::NoBrush);
        p.setPen(QPen(mHovered ? QColor(160, 160, 168) : QColor(95, 95, 102), 1.0));
        p.drawPolygon(diamond);
    }
}

void KeyframeDiamondButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !mScene) { return; }

    const bool hasKey = (mAnim && mAnim->anim_getKeyOnCurrentFrame()) ||
                        (mAnimY && mAnimY->anim_getKeyOnCurrentFrame());
    if (hasKey) {
        if (mAnim && mAnim->anim_getKeyOnCurrentFrame()) {
            mAnim->anim_removeKey(mAnim->anim_getKeyOnCurrentFrame()->ref<Key>());
        }
        if (mAnimY && mAnimY->anim_getKeyOnCurrentFrame()) {
            mAnimY->anim_removeKey(mAnimY->anim_getKeyOnCurrentFrame()->ref<Key>());
        }
    } else {
        if (mAnim) {
            mAnim->anim_setRecording(true);
            mAnim->anim_saveCurrentValueAsKey();
        }
        if (mAnimY) {
            mAnimY->anim_setRecording(true);
            mAnimY->anim_saveCurrentValueAsKey();
        }
    }

    mScene->requestUpdate();
    emit keyframeToggled();
    update();
}

// ============================================================================
// InspectorColorButton implementation
// ============================================================================
InspectorColorButton::InspectorColorButton(ColorAnimator *anim, Canvas *scene, QWidget *parent)
    : QWidget(parent)
    , mAnim(anim)
    , mScene(scene)
{
    setFixedHeight(18);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void InspectorColorButton::enterEvent(QEvent *)
{
    mHovered = true;
    update();
}

void InspectorColorButton::leaveEvent(QEvent *)
{
    mHovered = false;
    update();
}

void InspectorColorButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect().adjusted(1, 1, -1, -1);
    const QColor col = mAnim ? mAnim->getColor() : Qt::white;

    // Draw checkerboard for alpha
    if (col.alpha() < 255) {
        const int sz = 4;
        for (int x = r.left(); x < r.right(); x += sz) {
            for (int y = r.top(); y < r.bottom(); y += sz) {
                const bool odd = ((x / sz) + (y / sz)) % 2 != 0;
                p.fillRect(QRect(x, y, sz, sz).intersected(r), odd ? QColor(80, 80, 80) : QColor(140, 140, 140));
            }
        }
    }

    // Color fill
    p.setBrush(col);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(r, 2, 2);

    // Border
    const QColor borderCol = mHovered ? ThemeSupport::getThemeHighlightColor() : ThemeSupport::getThemeButtonBorderColor();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(borderCol, mHovered ? 1.5 : 1.0));
    p.drawRoundedRect(r, 2, 2);

    // Hex / RGB label inside swatch
    const QString colName = col.name().toUpper();
    p.setFont(QFont(QStringLiteral("sans-serif"), 8));
    p.setPen(col.lightnessF() > 0.5 ? QColor(20, 20, 20) : QColor(240, 240, 240));
    p.drawText(r, Qt::AlignCenter, colName);
}

void InspectorColorButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !mAnim) { return; }

    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setPalette(ThemeSupport::getDefaultPalette());
    menu->setStyleSheet(QStringLiteral(
        "QMenu {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: 4px;"
        "}"
    ).arg(ThemeSupport::getThemeToolBarColor().name(),
          ThemeSupport::getThemeButtonBorderColor().name(),
          QString::number(ThemeSupport::borderRadius())));

    auto colorWidget = new ColorSettingsWidget(menu);
    colorWidget->setTarget(mAnim);

    auto act = new QWidgetAction(menu);
    act->setDefaultWidget(colorWidget);
    menu->addAction(act);

    connect(colorWidget, &ColorSettingsWidget::colorSettingSignal, this, [this]() {
        if (mScene) { mScene->requestUpdate(); }
        update();
    });

    menu->exec(mapToGlobal(QPoint(0, height() + 2)));
}

// ============================================================================
// AEPropertiesInspector implementation
// ============================================================================
AEPropertiesInspector::AEPropertiesInspector(Document &doc, QWidget *parent)
    : QScrollArea(parent)
    , mDoc(doc)
{
    setObjectName(QStringLiteral("AEPropertiesInspector"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAutoFillBackground(true);

    if (viewport()) {
        viewport()->setAutoFillBackground(true);
    }

    mContainer = new QWidget(this);
    mContainer->setObjectName(QStringLiteral("InspectorContainer"));
    mContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    mMainLayout = new QVBoxLayout(mContainer);
    mMainLayout->setContentsMargins(4, 4, 4, 4);
    mMainLayout->setSpacing(3);
    mMainLayout->setAlignment(Qt::AlignTop);

    setWidget(mContainer);
}

void AEPropertiesInspector::setCurrentScene(Canvas *scene)
{
    if (mScene == scene) { return; }
    if (mScene) {
        disconnect(mScene, nullptr, this, nullptr);
    }
    mScene = scene;
    if (mScene) {
        connect(mScene, &Canvas::objectSelectionChanged, this, &AEPropertiesInspector::refreshSelection);
        connect(mScene, &Canvas::currentFrameChanged, this, &AEPropertiesInspector::refreshValues);
    }
    refreshSelection();
}

void AEPropertiesInspector::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        refreshSelection();
    }
    QScrollArea::changeEvent(event);
}

void AEPropertiesInspector::refreshSelection()
{
    if (!mMainLayout) { return; }
    mStatefulWidgets.clear();

    QLayoutItem *item;
    while ((item = mMainLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (!mScene) { return; }

    const auto selected = mScene->getSelectedBoxesList();
    if (selected.isEmpty()) {
        mCurrentBox = nullptr;
        buildSceneProperties();
    } else {
        mCurrentBox = selected.last();
        buildBoxProperties(mCurrentBox);
    }
    mMainLayout->addStretch(1);
}

void AEPropertiesInspector::refreshValues()
{
    for (auto *widget : mStatefulWidgets) {
        if (widget) { widget->update(); }
    }
}

QFrame* AEPropertiesInspector::createSectionCard(const QString &title, const QIcon &icon, QGridLayout *&outGrid, bool defaultExpanded)
{
    auto card = new QFrame(mContainer);
    card->setObjectName(QStringLiteral("InspectorCard"));
    card->setAutoFillBackground(false);
    card->setPalette(ThemeSupport::getDefaultPalette());
    card->setStyleSheet(QStringLiteral(
        "QFrame#InspectorCard {"
        "  background-color: transparent;"
        "  border: none;"
        "  margin: 0px;"
        "  padding: 0px;"
        "}"
    ));

    auto cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 3, 0, 3);
    cardLayout->setSpacing(2);

    auto header = new QWidget(card);
    auto headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 1, 0, 1);
    headerLayout->setSpacing(3);

    auto foldBtn = new QToolButton(header);
    foldBtn->setObjectName(QStringLiteral("FlatButton"));
    foldBtn->setText(defaultExpanded ? QStringLiteral("▼") : QStringLiteral("▶"));
    foldBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: %1; padding: 0; border: none; background: transparent;")
                           .arg(ThemeSupport::getThemeHighlightColor().name()));
    headerLayout->addWidget(foldBtn);

    if (!icon.isNull()) {
        auto iconLbl = new QLabel(header);
        iconLbl->setPixmap(icon.pixmap(12, 12));
        headerLayout->addWidget(iconLbl);
    }

    auto titleLbl = new QLabel(title, header);
    titleLbl->setStyleSheet(QStringLiteral("font-weight: bold; color: #ffffff; font-size: 13px;"));
    headerLayout->addWidget(titleLbl);
    headerLayout->addStretch(1);

    cardLayout->addWidget(header);

    auto contentWidget = new QWidget(card);
    contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    outGrid = new QGridLayout(contentWidget);
    outGrid->setContentsMargins(0, 1, 0, 1);
    outGrid->setSpacing(2);
    outGrid->setColumnMinimumWidth(0, 36); // Keyframe controls
    outGrid->setColumnMinimumWidth(1, 38); // Property label
    outGrid->setColumnStretch(2, 1);        // Value inputs (flexible)
    outGrid->setColumnMinimumWidth(3, 14); // Reset button

    contentWidget->setVisible(defaultExpanded);

    connect(foldBtn, &QToolButton::clicked, [contentWidget, foldBtn]() {
        const bool visible = !contentWidget->isVisible();
        contentWidget->setVisible(visible);
        foldBtn->setText(visible ? QStringLiteral("▼") : QStringLiteral("▶"));
    });

    cardLayout->addWidget(contentWidget);
    return card;
}

QWidget* AEPropertiesInspector::createKeyframeNav(Animator *anim)
{
    auto row = new QWidget();
    row->setFixedWidth(36);
    auto h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    auto prevBtn = new QToolButton(row);
    prevBtn->setObjectName(QStringLiteral("FlatButton"));
    prevBtn->setText(QStringLiteral("◀"));
    prevBtn->setFixedSize(10, 16);
    prevBtn->setStyleSheet(QStringLiteral("font-size: 9px; padding: 0; color: #c8c8d0; border: none; background: transparent;"));
    prevBtn->setToolTip(tr("跳转至上一关键帧"));
    connect(prevBtn, &QToolButton::clicked, [anim, this]() {
        if (!anim || !mScene) { return; }
        int prevRel;
        if (anim->anim_prevRelFrameWithKey(mScene->getCurrentFrame(), prevRel)) {
            mScene->anim_setAbsFrame(prevRel);
        }
    });
    h->addWidget(prevBtn);

    auto diamond = new KeyframeDiamondButton(anim, mScene, row);
    diamond->setToolTip(tr("添加/删除当前帧关键帧"));
    mStatefulWidgets.append(diamond);
    h->addWidget(diamond);

    auto nextBtn = new QToolButton(row);
    nextBtn->setObjectName(QStringLiteral("FlatButton"));
    nextBtn->setText(QStringLiteral("▶"));
    nextBtn->setFixedSize(10, 16);
    nextBtn->setStyleSheet(QStringLiteral("font-size: 9px; padding: 0; color: #c8c8d0; border: none; background: transparent;"));
    nextBtn->setToolTip(tr("跳转至下一关键帧"));
    connect(nextBtn, &QToolButton::clicked, [anim, this]() {
        if (!anim || !mScene) { return; }
        int nextRel;
        if (anim->anim_nextRelFrameWithKey(mScene->getCurrentFrame(), nextRel)) {
            mScene->anim_setAbsFrame(nextRel);
        }
    });
    h->addWidget(nextBtn);

    return row;
}

QWidget* AEPropertiesInspector::createDualKeyframeNav(Animator *animX, Animator *animY)
{
    auto row = new QWidget();
    row->setFixedWidth(36);
    auto h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    auto prevBtn = new QToolButton(row);
    prevBtn->setObjectName(QStringLiteral("FlatButton"));
    prevBtn->setText(QStringLiteral("◀"));
    prevBtn->setFixedSize(10, 16);
    prevBtn->setStyleSheet(QStringLiteral("font-size: 9px; padding: 0; color: #c8c8d0; border: none; background: transparent;"));
    prevBtn->setToolTip(tr("跳转至上一关键帧"));
    connect(prevBtn, &QToolButton::clicked, [animX, animY, this]() {
        if (!mScene) { return; }
        int pX = -999999, pY = -999999;
        const bool hX = animX && animX->anim_prevRelFrameWithKey(mScene->getCurrentFrame(), pX);
        const bool hY = animY && animY->anim_prevRelFrameWithKey(mScene->getCurrentFrame(), pY);
        if (hX && hY) { mScene->anim_setAbsFrame(qMax(pX, pY)); }
        else if (hX) { mScene->anim_setAbsFrame(pX); }
        else if (hY) { mScene->anim_setAbsFrame(pY); }
    });
    h->addWidget(prevBtn);

    auto diamond = new KeyframeDiamondButton(animX, mScene, row);
    diamond->setDualAnimators(animX, animY);
    diamond->setToolTip(tr("添加/删除 X/Y 关键帧"));
    mStatefulWidgets.append(diamond);
    h->addWidget(diamond);

    auto nextBtn = new QToolButton(row);
    nextBtn->setObjectName(QStringLiteral("FlatButton"));
    nextBtn->setText(QStringLiteral("▶"));
    nextBtn->setFixedSize(10, 16);
    nextBtn->setStyleSheet(QStringLiteral("font-size: 9px; padding: 0; color: #c8c8d0; border: none; background: transparent;"));
    nextBtn->setToolTip(tr("跳转至下一关键帧"));
    connect(nextBtn, &QToolButton::clicked, [animX, animY, this]() {
        if (!mScene) { return; }
        int nX = 999999, nY = 999999;
        const bool hX = animX && animX->anim_nextRelFrameWithKey(mScene->getCurrentFrame(), nX);
        const bool hY = animY && animY->anim_nextRelFrameWithKey(mScene->getCurrentFrame(), nY);
        if (hX && hY) { mScene->anim_setAbsFrame(qMin(nX, nY)); }
        else if (hX) { mScene->anim_setAbsFrame(nX); }
        else if (hY) { mScene->anim_setAbsFrame(nY); }
    });
    h->addWidget(nextBtn);

    return row;
}

void AEPropertiesInspector::buildSceneProperties()
{
    QGridLayout *grid = nullptr;
    auto card = createSectionCard(tr("合成属性"), QIcon::fromTheme(QStringLiteral("settings")), grid);

    auto addSceneRow = [grid](int rowIdx, const QString &label, const QString &value) {
        auto l = new QLabel(label);
        l->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px;"));
        auto v = new QLabel(value);
        v->setStyleSheet(QStringLiteral("color: #ffffff; font-weight: bold; font-size: 12px;"));
        grid->addWidget(l, rowIdx, 1);
        grid->addWidget(v, rowIdx, 2);
    };

    if (mScene) {
        addSceneRow(0, tr("分辨率"), QStringLiteral("%1 × %2 px").arg(mScene->getCanvasWidth()).arg(mScene->getCanvasHeight()));
        addSceneRow(1, tr("帧率"), QStringLiteral("%1 fps").arg(mScene->getFps()));
        addSceneRow(2, tr("持续时长"), QStringLiteral("%1 帧 (%2 秒)").arg(mScene->getMaxFrame() + 1).arg((mScene->getMaxFrame() + 1) / qMax(1.0, mScene->getFps()), 0, 'f', 2));
    }

    mMainLayout->addWidget(card);
}

void AEPropertiesInspector::buildBoxProperties(BoundingBox *box)
{
    if (!box) { return; }

    // Top Header Bar: Clean, flat, borderless
    auto headerWidget = new QWidget(mContainer);
    auto hLayout = new QHBoxLayout(headerWidget);
    hLayout->setContentsMargins(0, 1, 0, 3);
    hLayout->setSpacing(4);

    auto nameEdit = new QLineEdit(box->prp_getName(), headerWidget);
    nameEdit->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background-color: %1;"
        "  color: #ececf0;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: 2px 6px;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}"
        "QLineEdit:focus {"
        "  border-color: %4;"
        "}"
    ).arg(ThemeSupport::getThemeButtonBaseColor().name(),
          ThemeSupport::getThemeButtonBorderColor().name(),
          QString::number(ThemeSupport::borderRadius()),
          ThemeSupport::getThemeHighlightColor().name()));
    connect(nameEdit, &QLineEdit::editingFinished, [box, nameEdit]() {
        box->prp_setName(nameEdit->text());
    });
    hLayout->addWidget(nameEdit, 1);

    QString typeStr = tr("图层");
    if (box->getBoxType() == eBoxType::adjustmentLayer) { typeStr = tr("调整图层"); }
    else if (enve_cast<PathBox*>(box)) { typeStr = tr("矢量路径"); }
    else if (enve_cast<NullObject*>(box)) { typeStr = tr("空对象"); }

    auto typeBadge = new QLabel(typeStr, headerWidget);
    typeBadge->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; padding: 0 4px;"));
    hLayout->addWidget(typeBadge);

    auto eyeBtn = new QToolButton(headerWidget);
    eyeBtn->setObjectName(QStringLiteral("FlatButton"));
    eyeBtn->setIcon(QIcon::fromTheme(box->isVisible() ? QStringLiteral("visible") : QStringLiteral("novisible")));
    eyeBtn->setToolTip(tr("显示/隐藏图层"));
    eyeBtn->setCheckable(true);
    eyeBtn->setChecked(box->isVisible());
    connect(eyeBtn, &QToolButton::clicked, [box, eyeBtn, this](bool checked) {
        box->setVisible(checked);
        eyeBtn->setIcon(QIcon::fromTheme(checked ? QStringLiteral("visible") : QStringLiteral("novisible")));
        if (mScene) { mScene->requestUpdate(); }
    });
    hLayout->addWidget(eyeBtn);

    auto lockBtn = new QToolButton(headerWidget);
    lockBtn->setObjectName(QStringLiteral("FlatButton"));
    lockBtn->setIcon(QIcon::fromTheme(box->isLocked() ? QStringLiteral("locked") : QStringLiteral("unlocked")));
    lockBtn->setToolTip(box->isLocked() ? tr("已锁定图层 (点击解锁)") : tr("未锁定图层 (点击锁定)"));
    lockBtn->setCheckable(true);
    lockBtn->setChecked(box->isLocked());
    if (box->isLocked()) {
        lockBtn->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 3px;").arg(ThemeSupport::getThemeHighlightColor(120).name(QColor::HexArgb)));
    }
    connect(lockBtn, &QToolButton::clicked, [box, lockBtn, this](bool checked) {
        box->setLocked(checked);
        lockBtn->setIcon(QIcon::fromTheme(checked ? QStringLiteral("locked") : QStringLiteral("unlocked")));
        lockBtn->setToolTip(checked ? tr("已锁定图层 (点击解锁)") : tr("未锁定图层 (点击锁定)"));
        if (checked) {
            lockBtn->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 3px;").arg(ThemeSupport::getThemeHighlightColor(120).name(QColor::HexArgb)));
        } else {
            lockBtn->setStyleSheet(QString());
        }
        if (mScene) { mScene->requestUpdate(); }
    });
    hLayout->addWidget(lockBtn);

    const auto advTrans = enve_cast<AdvancedTransformAnimator*>(box->getTransformAnimator());
    if (advTrans) {
        auto cube3DBtn = new QToolButton(headerWidget);
        cube3DBtn->setObjectName(QStringLiteral("FlatButton"));
        cube3DBtn->setIcon(QIcon::fromTheme(QStringLiteral("boxTransform")));
        cube3DBtn->setToolTip(tr("2.5D 图层开关"));
        cube3DBtn->setCheckable(true);
        cube3DBtn->setChecked(advTrans->is3DEnabled());
        connect(cube3DBtn, &QToolButton::clicked, [advTrans, this](bool checked) {
            advTrans->set3DEnabled(checked);
            if (mScene) { mScene->requestUpdate(); }
            refreshSelection();
        });
        hLayout->addWidget(cube3DBtn);
    }

    mMainLayout->addWidget(headerWidget);

    // Section 1: Transform Controls (Grid Aligned)
    QGridLayout *transGrid = nullptr;
    auto transCard = createSectionCard(tr("变换"), QIcon::fromTheme(QStringLiteral("transform")), transGrid);
    setupTransformControls(transGrid, box);
    mMainLayout->addWidget(transCard);

    // Section 2: Vector Shape Style Controls (Fill / Stroke if PathBox)
    if (const auto pathBox = enve_cast<PathBox*>(box)) {
        QGridLayout *styleGrid = nullptr;
        auto styleCard = createSectionCard(tr("形状与样式"), QIcon::fromTheme(QStringLiteral("draw-brush")), styleGrid);
        setupPathStyleControls(styleGrid, pathBox);
        mMainLayout->addWidget(styleCard);
    }

    // Section 3: Effects Controls
    auto effWidget = new QWidget(mContainer);
    auto effLayout = new QVBoxLayout(effWidget);
    effLayout->setContentsMargins(0, 3, 0, 3);
    effLayout->setSpacing(2);
    setupEffectsControls(effLayout, box);
    mMainLayout->addWidget(effWidget);
}

void AEPropertiesInspector::setupTransformControls(QGridLayout *grid, BoundingBox *box)
{
    const auto advTrans = enve_cast<AdvancedTransformAnimator*>(box->getTransformAnimator());
    if (!advTrans || !grid) { return; }

    int rowIdx = 0;

    auto addDualRow = [grid, this, &rowIdx](const QString &label, QrealAnimator *animX, QrealAnimator *animY, qreal defaultVal = 0.0, bool isScale = false) {
        if (!animX || !animY) { return; }

        grid->addWidget(createDualKeyframeNav(animX, animY), rowIdx, 0, Qt::AlignCenter);

        auto lbl = new QLabel(label);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px;"));
        grid->addWidget(lbl, rowIdx, 1);

        auto inputContainer = new QWidget();
        inputContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto ih = new QHBoxLayout(inputContainer);
        ih->setContentsMargins(0, 0, 0, 0);
        ih->setSpacing(2);

        auto sliderX = new QrealAnimatorValueSlider(animX, inputContainer);
        sliderX->setAutoAdjustWidth(false);
        sliderX->setName(QStringLiteral("X"));
        sliderX->setNameVisible(true);
        sliderX->setIsLeftSlider(true);
        sliderX->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        ih->addWidget(sliderX, 1);

        QToolButton *linkBtn = nullptr;
        if (isScale) {
            linkBtn = new QToolButton(inputContainer);
            linkBtn->setObjectName(QStringLiteral("FlatButton"));
            linkBtn->setCheckable(true);
            linkBtn->setChecked(true);
            linkBtn->setIcon(QIcon::fromTheme(QStringLiteral("linked")));
            linkBtn->setFixedSize(16, 16);

            auto updateLinkBtnStyle = [linkBtn](bool locked) {
                if (locked) {
                    linkBtn->setStyleSheet(QStringLiteral(
                        "QToolButton {"
                        "  background-color: %1;"
                        "  border: 1px solid %2;"
                        "  border-radius: 3px;"
                        "  padding: 1px;"
                        "}"
                    ).arg(ThemeSupport::getThemeHighlightColor(120).name(QColor::HexArgb),
                          ThemeSupport::getThemeHighlightColor().name()));
                    linkBtn->setToolTip(tr("等比缩放已锁定 (点击解锁独立缩放)"));
                } else {
                    linkBtn->setStyleSheet(QStringLiteral(
                        "QToolButton {"
                        "  background-color: transparent;"
                        "  border: 1px dashed %1;"
                        "  border-radius: 3px;"
                        "  padding: 1px;"
                        "  opacity: 0.5;"
                        "}"
                    ).arg(ThemeSupport::getThemeColorTextDisabled().name()));
                    linkBtn->setToolTip(tr("等比缩放已解锁 (点击锁定等比缩放)"));
                }
            };
            updateLinkBtnStyle(true);
            connect(linkBtn, &QToolButton::toggled, updateLinkBtnStyle);

            ih->addWidget(linkBtn);
        }

        auto sliderY = new QrealAnimatorValueSlider(animY, inputContainer);
        sliderY->setAutoAdjustWidth(false);
        sliderY->setName(QStringLiteral("Y"));
        sliderY->setNameVisible(true);
        sliderY->setIsRightSlider(true);
        sliderY->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        ih->addWidget(sliderY, 1);

        if (isScale && linkBtn) {
            connect(sliderX, &QrealAnimatorValueSlider::valueEdited, [animY, sliderY, linkBtn, this](qreal val) {
                if (linkBtn->isChecked() && animY) {
                    animY->setCurrentBaseValue(val);
                    sliderY->setDisplayedValue(val);
                    if (mScene) { mScene->requestUpdate(); }
                }
            });
            connect(sliderY, &QrealAnimatorValueSlider::valueEdited, [animX, sliderX, linkBtn, this](qreal val) {
                if (linkBtn->isChecked() && animX) {
                    animX->setCurrentBaseValue(val);
                    sliderX->setDisplayedValue(val);
                    if (mScene) { mScene->requestUpdate(); }
                }
            });
        }

        grid->addWidget(inputContainer, rowIdx, 2);

        auto resetBtn = new QToolButton();
        resetBtn->setObjectName(QStringLiteral("FlatButton"));
        resetBtn->setText(QStringLiteral("↺"));
        resetBtn->setFixedSize(14, 16);
        resetBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: #707078; padding: 0; border: none; background: transparent;"));
        resetBtn->setToolTip(tr("重置为默认值"));
        connect(resetBtn, &QToolButton::clicked, [animX, animY, defaultVal, this]() {
            if (animX) { animX->setCurrentBaseValue(defaultVal); }
            if (animY) { animY->setCurrentBaseValue(defaultVal); }
            if (mScene) { mScene->requestUpdate(); }
            refreshValues();
        });
        grid->addWidget(resetBtn, rowIdx, 3, Qt::AlignCenter);

        rowIdx++;
    };

    auto addSingleRow = [grid, this, &rowIdx](const QString &label, QrealAnimator *anim, const QString &unit = QString(), qreal defaultVal = 0.0) {
        if (!anim) { return; }

        grid->addWidget(createKeyframeNav(anim), rowIdx, 0, Qt::AlignCenter);

        auto lbl = new QLabel(label);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px;"));
        grid->addWidget(lbl, rowIdx, 1);

        auto slider = new QrealAnimatorValueSlider(anim, nullptr);
        slider->setAutoAdjustWidth(false);
        slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        if (!unit.isEmpty()) {
            slider->setName(unit);
            slider->setNameVisible(true);
        }
        grid->addWidget(slider, rowIdx, 2);

        auto resetBtn = new QToolButton();
        resetBtn->setObjectName(QStringLiteral("FlatButton"));
        resetBtn->setText(QStringLiteral("↺"));
        resetBtn->setFixedSize(14, 16);
        resetBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: #707078; padding: 0; border: none; background: transparent;"));
        resetBtn->setToolTip(tr("重置为默认值"));
        connect(resetBtn, &QToolButton::clicked, [anim, defaultVal, this]() {
            if (anim) { anim->setCurrentBaseValue(defaultVal); }
            if (mScene) { mScene->requestUpdate(); }
            refreshValues();
        });
        grid->addWidget(resetBtn, rowIdx, 3, Qt::AlignCenter);

        rowIdx++;
    };

    // 1. Anchor Point (Pivot)
    if (const auto pivotAnim = advTrans->getPivotAnimator()) {
        addDualRow(tr("锚点"), pivotAnim->getXAnimator(), pivotAnim->getYAnimator(), 0.0);
    }

    // 2. Position
    if (const auto posAnim = advTrans->getPosAnimator()) {
        addDualRow(tr("位置"), posAnim->getXAnimator(), posAnim->getYAnimator(), 0.0);
    }

    // 3. Scale
    if (const auto scaleAnim = advTrans->getScaleAnimator()) {
        addDualRow(tr("缩放"), scaleAnim->getXAnimator(), scaleAnim->getYAnimator(), 1.0, true);
    }

    // 4. Rotation
    addSingleRow(tr("旋转"), advTrans->getRotAnimator(), tr("°"), 0.0);

    // 5. Opacity
    addSingleRow(tr("不透明度"), advTrans->getOpacityAnimator(), QStringLiteral("%"), 100.0);

    // 6. 2.5D properties
    if (advTrans->is3DEnabled()) {
        addDualRow(tr("3D旋转"), advTrans->getRotXAnimator(), advTrans->getRotYAnimator(), 0.0);
        addSingleRow(tr("深度 Z"), advTrans->getZPosAnimator(), QStringLiteral("Z"), 0.0);
    }
}

void AEPropertiesInspector::setupPathStyleControls(QGridLayout *grid, PathBox *pathBox)
{
    if (!pathBox || !grid) { return; }

    int rowIdx = 0;

    // Fill Color
    if (const auto fill = pathBox->getFillSettings()) {
        if (const auto colAnim = fill->getColorAnimator()) {
            grid->addWidget(createKeyframeNav(colAnim), rowIdx, 0, Qt::AlignCenter);

            auto lbl = new QLabel(tr("填充"));
            lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            lbl->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
            grid->addWidget(lbl, rowIdx, 1);

            auto colorBtn = new InspectorColorButton(colAnim, mScene);
            colorBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            mStatefulWidgets.append(colorBtn);
            grid->addWidget(colorBtn, rowIdx, 2);

            auto resetBtn = new QToolButton();
            resetBtn->setObjectName(QStringLiteral("FlatButton"));
            resetBtn->setText(QStringLiteral("↺"));
            resetBtn->setFixedSize(14, 16);
            resetBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: #707078; padding: 0; border: none; background: transparent;"));
            resetBtn->setToolTip(tr("重置为白色"));
            connect(resetBtn, &QToolButton::clicked, [colAnim, colorBtn, this]() {
                if (colAnim) { colAnim->setColor(Qt::white); }
                if (mScene) { mScene->requestUpdate(); }
                colorBtn->update();
            });
            grid->addWidget(resetBtn, rowIdx, 3, Qt::AlignCenter);

            rowIdx++;
        }
    }

    // Stroke Color & Width
    if (const auto stroke = pathBox->getStrokeSettings()) {
        if (const auto colAnim = stroke->getColorAnimator()) {
            grid->addWidget(createKeyframeNav(colAnim), rowIdx, 0, Qt::AlignCenter);

            auto lbl = new QLabel(tr("描边"));
            lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            lbl->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
            grid->addWidget(lbl, rowIdx, 1);

            auto colorBtn = new InspectorColorButton(colAnim, mScene);
            colorBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            mStatefulWidgets.append(colorBtn);
            grid->addWidget(colorBtn, rowIdx, 2);

            auto resetBtn = new QToolButton();
            resetBtn->setObjectName(QStringLiteral("FlatButton"));
            resetBtn->setText(QStringLiteral("↺"));
            resetBtn->setFixedSize(14, 16);
            resetBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: #707078; padding: 0; border: none; background: transparent;"));
            resetBtn->setToolTip(tr("重置为黑色"));
            connect(resetBtn, &QToolButton::clicked, [colAnim, colorBtn, this]() {
                if (colAnim) { colAnim->setColor(Qt::black); }
                if (mScene) { mScene->requestUpdate(); }
                colorBtn->update();
            });
            grid->addWidget(resetBtn, rowIdx, 3, Qt::AlignCenter);

            rowIdx++;
        }

        if (const auto thickAnim = stroke->getLineWidthAnimator()) {
            grid->addWidget(createKeyframeNav(thickAnim), rowIdx, 0, Qt::AlignCenter);

            auto lbl = new QLabel(tr("线宽"));
            lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            lbl->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
            grid->addWidget(lbl, rowIdx, 1);

            auto slider = new QrealAnimatorValueSlider(thickAnim, nullptr);
            slider->setAutoAdjustWidth(false);
            slider->setName(QStringLiteral("px"));
            slider->setNameVisible(true);
            slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            grid->addWidget(slider, rowIdx, 2);

            auto resetBtn = new QToolButton();
            resetBtn->setObjectName(QStringLiteral("FlatButton"));
            resetBtn->setText(QStringLiteral("↺"));
            resetBtn->setFixedSize(14, 16);
            resetBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: #707078; padding: 0; border: none; background: transparent;"));
            resetBtn->setToolTip(tr("重置为默认宽度"));
            connect(resetBtn, &QToolButton::clicked, [thickAnim, this]() {
                if (thickAnim) { thickAnim->setCurrentBaseValue(10.0); }
                if (mScene) { mScene->requestUpdate(); }
                refreshValues();
            });
            grid->addWidget(resetBtn, rowIdx, 3, Qt::AlignCenter);

            rowIdx++;
        }
    }
}

void AEPropertiesInspector::setupEffectsControls(QVBoxLayout *layout, BoundingBox *box)
{
    const auto coll = box->rasterEffectsCollection();
    if (!coll) { return; }

    auto topBar = new QWidget();
    auto topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(0, 1, 0, 2);
    topBarLayout->setSpacing(3);

    auto title = new QLabel(tr("特效管线"), topBar);
    title->setStyleSheet(QStringLiteral("font-weight: bold; color: %1; font-size: 10px;")
                         .arg(ThemeSupport::getThemeHighlightColor().name()));
    topBarLayout->addWidget(title);
    topBarLayout->addStretch(1);

    auto addBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("添加特效..."), topBar);
    addBtn->setObjectName(QStringLiteral("FlatButton"));
    addBtn->setStyleSheet(QStringLiteral(
        "QPushButton#FlatButton {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: 2px 6px;"
        "  font-weight: bold;"
        "  font-size: 9px;"
        "}"
        "QPushButton#FlatButton:hover {"
        "  background-color: %4;"
        "  border-color: %5;"
        "}"
    ).arg(ThemeSupport::getThemeButtonBaseColor().name(),
          ThemeSupport::getThemeButtonBorderColor().name(),
          QString::number(ThemeSupport::borderRadius()),
          ThemeSupport::getThemeButtonHoverColor().name(),
          ThemeSupport::getThemeHighlightColor().name()));

    connect(addBtn, &QPushButton::clicked, [box, addBtn, this]() {
        QMenu menu;
        menu.setPalette(ThemeSupport::getDefaultPalette());
        QHash<QString, QMenu*> catMenus;
        auto getCat = [&menu, &catMenus](const QString& cat) -> QMenu* {
            if (catMenus.contains(cat)) { return catMenus[cat]; }
            auto m = menu.addMenu(cat);
            m->setPalette(ThemeSupport::getDefaultPalette());
            catMenus[cat] = m;
            return m;
        };

        const auto adder = [box, getCat, this](const QString& name, const QString& cat, const RasterEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) { return; }
            auto targetMenu = getCat(cat.isEmpty() ? tr("通用") : cat);
            auto act = targetMenu->addAction(name);
            connect(act, &QAction::triggered, [box, creator, this]() {
                box->addRasterEffect(creator());
                if (mScene) { mScene->requestUpdate(); }
                refreshSelection();
            });
        };

        RasterEffectMenuCreator::forEveryEffectCore(adder);
        RasterEffectMenuCreator::forEveryEffectCustom(adder);
        RasterEffectMenuCreator::forEveryEffectShader(adder);

        menu.exec(addBtn->mapToGlobal(QPoint(0, addBtn->height())));
    });

    topBarLayout->addWidget(addBtn);
    layout->addWidget(topBar);

    const int numEffects = coll->ca_getNumberOfChildren();
    if (numEffects == 0) {
        auto emptyLbl = new QLabel(tr("当前图层未添加任何特效"), this);
        emptyLbl->setStyleSheet(QStringLiteral("color: %1; font-style: italic; font-size: 10px; padding: 2px;")
                                .arg(ThemeSupport::getThemeColorTextDisabled().name()));
        layout->addWidget(emptyLbl);
        return;
    }

    for (int i = 0; i < numEffects; ++i) {
        auto effect = coll->ca_getChildAt<RasterEffect>(i);
        if (!effect) { continue; }

        auto effectWidget = new QWidget();
        auto eLayout = new QVBoxLayout(effectWidget);
        eLayout->setContentsMargins(0, 2, 0, 2);
        eLayout->setSpacing(2);

        auto eHeader = new QWidget(effectWidget);
        auto ehLayout = new QHBoxLayout(eHeader);
        ehLayout->setContentsMargins(0, 0, 0, 1);
        ehLayout->setSpacing(3);

        const QString effTitle = translatePropertyName(effect->prp_getName());
        auto titleLbl = new QLabel(effTitle, eHeader);
        titleLbl->setStyleSheet(QStringLiteral("font-weight: bold; color: %1; font-size: 10px;")
                                .arg(ThemeSupport::getThemeHighlightColor().name()));
        ehLayout->addWidget(titleLbl, 1);

        auto delBtn = new QToolButton(eHeader);
        delBtn->setObjectName(QStringLiteral("FlatButton"));
        delBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
        delBtn->setFixedSize(14, 16);
        delBtn->setToolTip(tr("删除此特效"));
        connect(delBtn, &QToolButton::clicked, [box, effect, this]() {
            box->removeRasterEffect(effect->ref<RasterEffect>());
            if (mScene) { mScene->requestUpdate(); }
            refreshSelection();
        });
        ehLayout->addWidget(delBtn);

        eLayout->addWidget(eHeader);

        auto gridWidget = new QWidget(effectWidget);
        gridWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto grid = new QGridLayout(gridWidget);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        grid->setColumnMinimumWidth(0, 36);
        grid->setColumnMinimumWidth(1, 38);
        grid->setColumnStretch(2, 1);
        grid->setColumnMinimumWidth(3, 14);

        const int numProps = effect->ca_getNumberOfChildren();
        for (int p = 0; p < numProps; ++p) {
            auto prop = effect->ca_getChildAt<Property>(p);
            if (!prop) { continue; }
            setupEffectPropertyControl(grid, p, prop, box);
        }

        eLayout->addWidget(gridWidget);
        layout->addWidget(effectWidget);
    }
}

void AEPropertiesInspector::setupEffectPropertyControl(QGridLayout *grid, int rowIdx, Property *prop, BoundingBox *box)
{
    Q_UNUSED(box)
    if (!prop || !grid) { return; }

    const QString propName = translatePropertyName(prop->prp_getName());

    if (auto qrealAnim = enve_cast<QrealAnimator*>(prop)) {
        grid->addWidget(createKeyframeNav(qrealAnim), rowIdx, 0, Qt::AlignCenter);

        auto lbl = new QLabel(propName);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
        grid->addWidget(lbl, rowIdx, 1);

        auto slider = new QrealAnimatorValueSlider(qrealAnim, nullptr);
        slider->setAutoAdjustWidth(false);
        slider->setName(propName);
        slider->setNameVisible(false);
        slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        grid->addWidget(slider, rowIdx, 2);

        auto resetBtn = new QToolButton();
        resetBtn->setObjectName(QStringLiteral("FlatButton"));
        resetBtn->setText(QStringLiteral("↺"));
        resetBtn->setFixedSize(14, 16);
        resetBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: #707078; padding: 0; border: none; background: transparent;"));
        resetBtn->setToolTip(tr("重置"));
        connect(resetBtn, &QToolButton::clicked, [qrealAnim, this]() {
            if (qrealAnim) { qrealAnim->setCurrentBaseValue(0.0); }
            if (mScene) { mScene->requestUpdate(); }
            refreshValues();
        });
        grid->addWidget(resetBtn, rowIdx, 3, Qt::AlignCenter);
    } else if (auto colAnim = enve_cast<ColorAnimator*>(prop)) {
        grid->addWidget(createKeyframeNav(colAnim), rowIdx, 0, Qt::AlignCenter);

        auto lbl = new QLabel(propName);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
        grid->addWidget(lbl, rowIdx, 1);

        auto colorBtn = new InspectorColorButton(colAnim, mScene);
        colorBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        mStatefulWidgets.append(colorBtn);
        grid->addWidget(colorBtn, rowIdx, 2);

        auto resetBtn = new QToolButton();
        resetBtn->setObjectName(QStringLiteral("FlatButton"));
        resetBtn->setText(QStringLiteral("↺"));
        resetBtn->setFixedSize(14, 16);
        resetBtn->setStyleSheet(QStringLiteral("font-size: 10px; color: #707078; padding: 0; border: none; background: transparent;"));
        resetBtn->setToolTip(tr("重置"));
        connect(resetBtn, &QToolButton::clicked, [colAnim, colorBtn, this]() {
            if (colAnim) { colAnim->setColor(Qt::white); }
            if (mScene) { mScene->requestUpdate(); }
            colorBtn->update();
        });
        grid->addWidget(resetBtn, rowIdx, 3, Qt::AlignCenter);
    }
}
