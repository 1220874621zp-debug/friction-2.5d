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
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffect.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "themesupport.h"
#include "Private/document.h"
#include "widgets/qrealanimatorvalueslider.h"
#include <QColorDialog>
#include <QMenu>
#include <QIcon>
#include <QPainter>

AEPropertiesInspector::AEPropertiesInspector(Document &doc, QWidget *parent) :
    QScrollArea(parent),
    mDoc(doc)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAutoFillBackground(true);
    setPalette(ThemeSupport::getDarkPalette());

    mContainer = new QWidget(this);
    mContainer->setAutoFillBackground(true);
    mContainer->setPalette(ThemeSupport::getDarkPalette());

    mMainLayout = new QVBoxLayout(mContainer);
    mMainLayout->setContentsMargins(6, 6, 6, 6);
    mMainLayout->setSpacing(6);
    mMainLayout->setAlignment(Qt::AlignTop);

    setWidget(mContainer);
}

void AEPropertiesInspector::setCurrentScene(Canvas *scene) {
    if (mScene == scene) return;
    if (mScene) {
        disconnect(mScene, nullptr, this, nullptr);
    }
    mScene = scene;
    if (mScene) {
        connect(mScene, &Canvas::objectSelectionChanged, this, &AEPropertiesInspector::refreshSelection);
        connect(mScene, &Canvas::currentFrameChanged, this, &AEPropertiesInspector::refreshValues);
        connect(mScene, &Canvas::requestUpdate, this, &AEPropertiesInspector::refreshValues);
    }
    refreshSelection();
}

void AEPropertiesInspector::refreshSelection() {
    QLayoutItem *item;
    while ((item = mMainLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (!mScene) {
        return;
    }

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

void AEPropertiesInspector::refreshValues() {
    if (!mCurrentBox || !mScene) return;
    // Sliders automatically track their target animators via Qt signal-slot bindings.
    // If the box selection or count changes, refreshSelection handles it.
}

QFrame* AEPropertiesInspector::createSectionCard(const QString &title, const QIcon &icon) {
    auto card = new QFrame(mContainer);
    card->setObjectName("InspectorCard");
    card->setAutoFillBackground(true);
    card->setPalette(ThemeSupport::getDarkPalette());
    card->setStyleSheet(QString(
        "QFrame#InspectorCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 4px;"
        "  margin: 1px 0px;"
        "}"
    ).arg(ThemeSupport::getThemeBaseColor().name(),
          ThemeSupport::getThemeButtonBorderColor().name()));

    auto cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(6, 6, 6, 6);
    cardLayout->setSpacing(4);

    auto header = new QWidget(card);
    auto headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 2);
    headerLayout->setSpacing(6);

    if (!icon.isNull()) {
        auto iconLbl = new QLabel(header);
        iconLbl->setPixmap(icon.pixmap(16, 16));
        headerLayout->addWidget(iconLbl);
    }

    auto titleLbl = new QLabel(title, header);
    titleLbl->setStyleSheet(QString("font-weight: bold; color: %1; font-size: 11px;")
                            .arg(ThemeSupport::getThemeHighlightColor().name()));
    headerLayout->addWidget(titleLbl);
    headerLayout->addStretch(1);

    cardLayout->addWidget(header);
    return card;
}

void AEPropertiesInspector::buildSceneProperties() {
    auto card = createSectionCard(tr("合成属性 (Composition)"), QIcon::fromTheme("settings"));
    auto layout = static_cast<QVBoxLayout*>(card->layout());

    auto addSceneRow = [card, layout](const QString &label, const QString &value) {
        auto row = new QWidget(card);
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(2, 2, 2, 2);
        auto l = new QLabel(label, row);
        l->setStyleSheet(QString("color: %1; font-size: 11px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
        auto v = new QLabel(value, row);
        v->setStyleSheet("color: #ececf0; font-weight: 500; font-size: 11px;");
        h->addWidget(l);
        h->addStretch(1);
        h->addWidget(v);
        layout->addWidget(row);
    };

    if (mScene) {
        addSceneRow(tr("分辨率 (Resolution)"), QString("%1 × %2 px").arg(mScene->getCanvasWidth()).arg(mScene->getCanvasHeight()));
        addSceneRow(tr("帧率 (Frame Rate)"), QString("%1 fps").arg(mScene->getFps()));
        addSceneRow(tr("持续时长 (Duration)"), QString("%1 帧").arg(mScene->getMaxFrame() + 1));
    }

    mMainLayout->addWidget(card);
}

void AEPropertiesInspector::buildBoxProperties(BoundingBox *box) {
    if (!box) return;

    // Header Card
    auto headerCard = new QFrame(mContainer);
    headerCard->setObjectName("InspectorCard");
    headerCard->setAutoFillBackground(true);
    headerCard->setPalette(ThemeSupport::getDarkPalette());
    headerCard->setStyleSheet(QString(
        "QFrame#InspectorCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 4px;"
        "  margin: 1px 0px;"
        "}"
    ).arg(ThemeSupport::getThemeButtonBaseColor().name(),
          ThemeSupport::getThemeButtonBorderColor().name()));

    auto hLayout = new QHBoxLayout(headerCard);
    hLayout->setContentsMargins(6, 4, 6, 4);
    hLayout->setSpacing(6);

    auto nameEdit = new QLineEdit(box->prp_getName(), headerCard);
    nameEdit->setPalette(ThemeSupport::getDarkerPalette());
    nameEdit->setStyleSheet("font-weight: bold; font-size: 11px;");
    connect(nameEdit, &QLineEdit::editingFinished, [box, nameEdit]() {
        box->prp_setName(nameEdit->text());
    });
    hLayout->addWidget(nameEdit, 1);

    QString typeStr = tr("图层");
    if (box->getBoxType() == eBoxType::adjustmentLayer) typeStr = tr("调整图层");
    else if (enve_cast<PathBox*>(box)) typeStr = tr("矢量路径");
    else if (enve_cast<NullObject*>(box)) typeStr = tr("空对象");

    auto typeBadge = new QLabel(typeStr, headerCard);
    typeBadge->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 3px; padding: 2px 5px; font-size: 10px;")
                             .arg(ThemeSupport::getThemeBaseDarkerColor().name(),
                                  ThemeSupport::getThemeHighlightColor().name()));
    hLayout->addWidget(typeBadge);

    auto eyeBtn = new QToolButton(headerCard);
    eyeBtn->setObjectName("FlatButton");
    eyeBtn->setIcon(QIcon::fromTheme(box->isVisible() ? "visible" : "novisible"));
    eyeBtn->setCheckable(true);
    eyeBtn->setChecked(box->isVisible());
    connect(eyeBtn, &QToolButton::clicked, [box, eyeBtn, this](bool checked) {
        box->setVisible(checked);
        eyeBtn->setIcon(QIcon::fromTheme(checked ? "visible" : "novisible"));
        if (mScene) mScene->requestUpdate();
    });
    hLayout->addWidget(eyeBtn);

    const auto advTrans = enve_cast<AdvancedTransformAnimator*>(box->getTransformAnimator());
    if (advTrans) {
        auto cube3DBtn = new QToolButton(headerCard);
        cube3DBtn->setObjectName("FlatButton");
        cube3DBtn->setIcon(QIcon::fromTheme("boxTransform"));
        cube3DBtn->setToolTip(tr("2.5D 图层开关 (3D Layer)"));
        cube3DBtn->setCheckable(true);
        cube3DBtn->setChecked(advTrans->is3DEnabled());
        connect(cube3DBtn, &QToolButton::clicked, [advTrans, this](bool checked) {
            advTrans->set3DEnabled(checked);
            if (mScene) mScene->requestUpdate();
            refreshSelection();
        });
        hLayout->addWidget(cube3DBtn);
    }

    mMainLayout->addWidget(headerCard);

    // 1. Transform Section
    auto transCard = createSectionCard(tr("变换 (Transform)"), QIcon::fromTheme("transform"));
    setupTransformControls(static_cast<QVBoxLayout*>(transCard->layout()), box);
    mMainLayout->addWidget(transCard);

    // 2. Effects Section
    auto effCard = createSectionCard(tr("特效 (Effects)"), QIcon::fromTheme("effect"));
    setupEffectsControls(static_cast<QVBoxLayout*>(effCard->layout()), box);
    mMainLayout->addWidget(effCard);
}

void AEPropertiesInspector::setupTransformControls(QVBoxLayout *layout, BoundingBox *box) {
    const auto advTrans = enve_cast<AdvancedTransformAnimator*>(box->getTransformAnimator());
    if (!advTrans) return;

    auto addDualSliderRow = [layout, this](const QString &label, QrealAnimator *animX, QrealAnimator *animY, const QString &nameX = "X", const QString &nameY = "Y") {
        if (!animX || !animY) return;
        auto row = new QWidget();
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);

        auto lbl = new QLabel(label, row);
        lbl->setMinimumWidth(60);
        lbl->setStyleSheet(QString("color: %1; font-size: 11px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
        h->addWidget(lbl);

        auto sliderX = new QrealAnimatorValueSlider(animX, row);
        sliderX->setName(nameX);
        sliderX->setNameVisible(true);
        sliderX->setIsLeftSlider(true);
        h->addWidget(sliderX, 1);

        auto sliderY = new QrealAnimatorValueSlider(animY, row);
        sliderY->setName(nameY);
        sliderY->setNameVisible(true);
        sliderY->setIsRightSlider(true);
        h->addWidget(sliderY, 1);

        layout->addWidget(row);
    };

    auto addSingleSliderRow = [layout, this](const QString &label, QrealAnimator *anim, const QString &name = "") {
        if (!anim) return;
        auto row = new QWidget();
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);

        auto lbl = new QLabel(label, row);
        lbl->setMinimumWidth(60);
        lbl->setStyleSheet(QString("color: %1; font-size: 11px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
        h->addWidget(lbl);

        auto slider = new QrealAnimatorValueSlider(anim, row);
        if (!name.isEmpty()) {
            slider->setName(name);
            slider->setNameVisible(true);
        }
        h->addWidget(slider, 1);

        layout->addWidget(row);
    };

    // 1. Anchor Point (Pivot)
    if (const auto pivotAnim = advTrans->getPivotAnimator()) {
        addDualSliderRow(tr("锚点"), pivotAnim->getXAnimator(), pivotAnim->getYAnimator());
    }

    // 2. Position
    if (const auto posAnim = advTrans->getPosAnimator()) {
        addDualSliderRow(tr("位置"), posAnim->getXAnimator(), posAnim->getYAnimator());
    }

    // 3. Scale
    if (const auto scaleAnim = advTrans->getScaleAnimator()) {
        addDualSliderRow(tr("缩放"), scaleAnim->getXAnimator(), scaleAnim->getYAnimator());
    }

    // 4. Rotation
    addSingleSliderRow(tr("旋转"), advTrans->getRotAnimator(), tr("角度"));

    // 5. Opacity
    addSingleSliderRow(tr("不透明度"), advTrans->getOpacityAnimator(), "%");

    // 6. 2.5D billboard properties if enabled
    if (advTrans->is3DEnabled()) {
        addDualSliderRow(tr("3D旋转"), advTrans->getRotXAnimator(), advTrans->getRotYAnimator(), "X", "Y");
        addSingleSliderRow(tr("3D位置 Z"), advTrans->getZPosAnimator(), "Z");
    }
}

void AEPropertiesInspector::setupEffectsControls(QVBoxLayout *layout, BoundingBox *box) {
    const auto coll = box->rasterEffectsCollection();
    if (!coll) return;

    // Header add effect button
    auto topBar = new QWidget();
    auto topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(0, 0, 0, 2);
    topBarLayout->setSpacing(4);

    auto addBtn = new QPushButton(QIcon::fromTheme("list-add"), tr("添加特效..."), topBar);
    addBtn->setObjectName("FlatButton");
    addBtn->setStyleSheet(QString(
        "QPushButton#FlatButton {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 3px;"
        "  padding: 3px 8px;"
        "  font-size: 11px;"
        "}"
        "QPushButton#FlatButton:hover {"
        "  background-color: %3;"
        "}"
    ).arg(ThemeSupport::getThemeButtonBaseColor().name(),
          ThemeSupport::getThemeButtonBorderColor().name(),
          ThemeSupport::getThemeHighlightColor().name()));

    connect(addBtn, &QPushButton::clicked, [box, addBtn, this]() {
        QMenu menu;
        menu.setPalette(ThemeSupport::getDarkPalette());
        QHash<QString, QMenu*> catMenus;
        auto getCat = [&menu, &catMenus](const QString& cat) -> QMenu* {
            if (catMenus.contains(cat)) return catMenus[cat];
            auto m = menu.addMenu(cat);
            m->setPalette(ThemeSupport::getDarkPalette());
            catMenus[cat] = m;
            return m;
        };

        const auto adder = [box, getCat, this](const QString& name, const QString& cat, const RasterEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) return;
            auto targetMenu = getCat(cat.isEmpty() ? tr("通用 (General)") : cat);
            auto act = targetMenu->addAction(name);
            connect(act, &QAction::triggered, [box, creator, this]() {
                box->addRasterEffect(creator());
                if (mScene) mScene->requestUpdate();
                refreshSelection();
            });
        };

        RasterEffectMenuCreator::forEveryEffectCore(adder);
        RasterEffectMenuCreator::forEveryEffectCustom(adder);
        RasterEffectMenuCreator::forEveryEffectShader(adder);

        menu.exec(addBtn->mapToGlobal(QPoint(0, addBtn->height())));
    });

    topBarLayout->addStretch(1);
    topBarLayout->addWidget(addBtn);
    layout->addWidget(topBar);

    const int numEffects = coll->ca_getNumberOfChildren();
    if (numEffects == 0) {
        auto emptyLbl = new QLabel(tr("当前图层未添加任何特效"), this);
        emptyLbl->setStyleSheet(QString("color: %1; font-style: italic; font-size: 11px;")
                                .arg(ThemeSupport::getThemeColorTextDisabled().name()));
        layout->addWidget(emptyLbl);
        return;
    }

    for (int i = 0; i < numEffects; ++i) {
        auto effect = coll->ca_getChildAt<RasterEffect>(i);
        if (!effect) continue;

        auto effectCard = new QFrame();
        effectCard->setObjectName("InspectorCard");
        effectCard->setAutoFillBackground(true);
        effectCard->setPalette(ThemeSupport::getDarkPalette());
        effectCard->setStyleSheet(QString(
            "QFrame#InspectorCard {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 3px;"
            "  margin: 1px 0px;"
            "}"
        ).arg(ThemeSupport::getThemeAlternateColor().name(),
              ThemeSupport::getThemeButtonBorderColor().name()));

        auto eLayout = new QVBoxLayout(effectCard);
        eLayout->setContentsMargins(4, 4, 4, 4);
        eLayout->setSpacing(3);

        // Effect Header
        auto eHeader = new QWidget(effectCard);
        auto ehLayout = new QHBoxLayout(eHeader);
        ehLayout->setContentsMargins(0, 0, 0, 2);
        ehLayout->setSpacing(4);

        const QString effTitle = translatePropertyName(effect->prp_getName());
        auto title = new QLabel(effTitle, eHeader);
        title->setStyleSheet(QString("font-weight: bold; color: %1; font-size: 11px;")
                             .arg(ThemeSupport::getThemeHighlightSelectedColor().name()));
        ehLayout->addWidget(title, 1);

        auto delBtn = new QToolButton(eHeader);
        delBtn->setObjectName("FlatButton");
        delBtn->setIcon(QIcon::fromTheme("edit-delete"));
        delBtn->setToolTip(tr("删除特效"));
        connect(delBtn, &QToolButton::clicked, [box, effect, this]() {
            box->removeRasterEffect(effect->ref<RasterEffect>());
            if (mScene) mScene->requestUpdate();
            refreshSelection();
        });
        ehLayout->addWidget(delBtn);

        eLayout->addWidget(eHeader);

        // Effect Properties
        const int numProps = effect->ca_getNumberOfChildren();
        for (int p = 0; p < numProps; ++p) {
            auto prop = effect->ca_getChildAt<Property>(p);
            if (!prop) continue;
            setupEffectPropertyControl(eLayout, prop, box);
        }

        layout->addWidget(effectCard);
    }
}

void AEPropertiesInspector::setupEffectPropertyControl(QVBoxLayout *layout, Property *prop, BoundingBox *box) {
    Q_UNUSED(box)
    if (!prop) return;

    const QString propName = translatePropertyName(prop->prp_getName());

    if (auto qrealAnim = enve_cast<QrealAnimator*>(prop)) {
        auto row = new QWidget();
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);

        auto lbl = new QLabel(propName, row);
        lbl->setMinimumWidth(80);
        lbl->setStyleSheet(QString("color: %1; font-size: 10px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
        h->addWidget(lbl);

        auto slider = new QrealAnimatorValueSlider(qrealAnim, row);
        slider->setName(propName);
        slider->setNameVisible(false);
        h->addWidget(slider, 1);

        layout->addWidget(row);
    } else if (auto colAnim = enve_cast<ColorAnimator*>(prop)) {
        auto row = new QWidget();
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);

        auto lbl = new QLabel(propName, row);
        lbl->setMinimumWidth(80);
        lbl->setStyleSheet(QString("color: %1; font-size: 10px;").arg(ThemeSupport::getThemeColorTextDisabled().name()));
        h->addWidget(lbl);

        auto colorBtn = new QToolButton(row);
        colorBtn->setObjectName("FlatButton");
        colorBtn->setFixedSize(50, 18);

        auto updateColorIcon = [colorBtn, colAnim]() {
            const QColor c = colAnim->getColor();
            QPixmap pix(44, 14);
            pix.fill(Qt::transparent);
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(c);
            p.setPen(ThemeSupport::getThemeButtonBorderColor());
            p.drawRoundedRect(pix.rect().adjusted(0, 0, -1, -1), 2, 2);
            p.end();
            colorBtn->setIcon(QIcon(pix));
            colorBtn->setIconSize(pix.size());
        };
        updateColorIcon();

        connect(colorBtn, &QToolButton::clicked, [colAnim, updateColorIcon, row, this]() {
            const QColor picked = QColorDialog::getColor(colAnim->getColor(), row, tr("选择颜色"), QColorDialog::ShowAlphaChannel);
            if (picked.isValid()) {
                colAnim->setColor(picked);
                updateColorIcon();
                if (mScene) mScene->requestUpdate();
            }
        });

        h->addWidget(colorBtn);
        h->addStretch(1);
        layout->addWidget(row);
    }
}
