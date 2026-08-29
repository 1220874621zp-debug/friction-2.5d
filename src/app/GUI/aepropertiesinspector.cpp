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
#include "Boxes/adjustmentlayer.h"
#include "Boxes/nullobject.h"
#include "Animators/transformanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffect.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "themesupport.h"
#include "Private/document.h"
#include <QColorDialog>
#include <QMenu>
#include <QFrame>

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
    mMainLayout = new QVBoxLayout(mContainer);
    mMainLayout->setContentsMargins(8, 8, 8, 8);
    mMainLayout->setSpacing(10);
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
    if (!mUpdatingUI) {
        mUpdatingUI = true;
        refreshSelection();
        mUpdatingUI = false;
    }
}

QWidget* AEPropertiesInspector::createCard(const QString &title, const QIcon &icon) {
    auto card = new QFrame(mContainer);
    card->setObjectName("InspectorCard");
    card->setStyleSheet(
        "QFrame#InspectorCard {"
        "  background-color: #2b2b2e;"
        "  border: 1px solid #3c3c42;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
    );

    auto cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(6, 6, 6, 6);
    cardLayout->setSpacing(6);

    auto header = new QWidget(card);
    auto headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 4);
    headerLayout->setSpacing(6);

    if (!icon.isNull()) {
        auto iconLbl = new QLabel(header);
        iconLbl->setPixmap(icon.pixmap(16, 16));
        headerLayout->addWidget(iconLbl);
    }

    auto titleLbl = new QLabel(title, header);
    titleLbl->setStyleSheet("font-weight: bold; color: #dcdce0; font-size: 11px;");
    headerLayout->addWidget(titleLbl);
    headerLayout->addStretch(1);

    cardLayout->addWidget(header);
    return card;
}

void AEPropertiesInspector::buildSceneProperties() {
    auto card = createCard(tr("合成属性 (Composition)"), QIcon::fromTheme("settings"));
    auto layout = static_cast<QVBoxLayout*>(card->layout());

    auto addSceneRow = [card, layout](const QString &label, const QString &value) {
        auto row = new QWidget(card);
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto l = new QLabel(label, row);
        l->setStyleSheet("color: #8a8a94; font-size: 11px;");
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

    // Header Badge Card
    auto headerCard = new QFrame(mContainer);
    headerCard->setStyleSheet(
        "QFrame {"
        "  background-color: #333338;"
        "  border: 1px solid #44444c;"
        "  border-radius: 6px;"
        "  padding: 6px;"
        "}"
    );
    auto hLayout = new QHBoxLayout(headerCard);
    hLayout->setContentsMargins(4, 4, 4, 4);
    hLayout->setSpacing(8);

    auto nameEdit = new QLineEdit(box->prp_getName(), headerCard);
    nameEdit->setStyleSheet("font-weight: bold; background: transparent; border: none; color: #ffffff; font-size: 12px;");
    connect(nameEdit, &QLineEdit::editingFinished, [box, nameEdit]() {
        box->prp_setName(nameEdit->text());
    });
    hLayout->addWidget(nameEdit, 1);

    QString typeStr = tr("图层");
    if (box->getBoxType() == eBoxType::adjustmentLayer) typeStr = tr("调整图层");
    else if (enve_cast<PathBox*>(box)) typeStr = tr("矢量路径");
    else if (enve_cast<NullObject*>(box)) typeStr = tr("空对象");

    auto typeBadge = new QLabel(typeStr, headerCard);
    typeBadge->setStyleSheet("background-color: #4a4a54; color: #b0c4de; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
    hLayout->addWidget(typeBadge);

    auto eyeBtn = new QToolButton(headerCard);
    eyeBtn->setIcon(QIcon::fromTheme(box->isVisible() ? "eye-open" : "eye-closed"));
    eyeBtn->setCheckable(true);
    eyeBtn->setChecked(box->isVisible());
    connect(eyeBtn, &QToolButton::clicked, [box, this](bool checked) {
        box->setVisible(checked);
        if (mScene) mScene->requestUpdate();
    });
    hLayout->addWidget(eyeBtn);

    mMainLayout->addWidget(headerCard);

    // 1. Transform Card
    auto transCard = createCard(tr("变换 (Transform)"), QIcon::fromTheme("transform"));
    setupTransformControls(static_cast<QVBoxLayout*>(transCard->layout()), box);
    mMainLayout->addWidget(transCard);

    // 2. Effects Card
    auto effCard = createCard(tr("特效 (Effects)"), QIcon::fromTheme("effect"));
    setupEffectsControls(static_cast<QVBoxLayout*>(effCard->layout()), box);
    mMainLayout->addWidget(effCard);
}

void AEPropertiesInspector::setupTransformControls(QVBoxLayout *layout, BoundingBox *box) {
    const auto trans = box->getTransformAnimator();
    if (!trans) return;

    auto addDblSpinRow = [layout, this](const QString &name, qreal val, qreal minV, qreal maxV, qreal step, const QString &suffix, auto setter) {
        auto row = new QWidget();
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(0, 2, 0, 2);
        h->setSpacing(6);

        auto lbl = new QLabel(name, row);
        lbl->setMinimumWidth(80);
        lbl->setStyleSheet("color: #a0a0aa; font-size: 11px;");
        h->addWidget(lbl);

        auto spin = new QDoubleSpinBox(row);
        spin->setRange(minV, maxV);
        spin->setSingleStep(step);
        spin->setValue(val);
        spin->setSuffix(suffix);
        spin->setStyleSheet("background-color: #1e1e22; color: #ffffff; border: 1px solid #3c3c44; border-radius: 3px; padding: 2px;");

        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [setter, this](double v) {
            setter(v);
            if (mScene) mScene->requestUpdate();
        });

        h->addWidget(spin, 1);
        layout->addWidget(row);
    };

    // Position X, Y
    auto posRow = new QWidget();
    auto posH = new QHBoxLayout(posRow);
    posH->setContentsMargins(0, 2, 0, 2);
    posH->setSpacing(6);

    auto pLbl = new QLabel(tr("位置 (Pos)"), posRow);
    pLbl->setMinimumWidth(80);
    pLbl->setStyleSheet("color: #a0a0aa; font-size: 11px;");
    posH->addWidget(pLbl);

    auto spinX = new QDoubleSpinBox(posRow);
    spinX->setRange(-99999, 99999);
    spinX->setValue(trans->dx());
    spinX->setPrefix("X: ");
    spinX->setStyleSheet("background-color: #1e1e22; color: #ff8888; border: 1px solid #3c3c44; border-radius: 3px;");
    connect(spinX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [trans, this](double x) {
        trans->setPosition(x, trans->dy());
        if (mScene) mScene->requestUpdate();
    });
    posH->addWidget(spinX, 1);

    auto spinY = new QDoubleSpinBox(posRow);
    spinY->setRange(-99999, 99999);
    spinY->setValue(trans->dy());
    spinY->setPrefix("Y: ");
    spinY->setStyleSheet("background-color: #1e1e22; color: #88ff88; border: 1px solid #3c3c44; border-radius: 3px;");
    connect(spinY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [trans, this](double y) {
        trans->setPosition(trans->dx(), y);
        if (mScene) mScene->requestUpdate();
    });
    posH->addWidget(spinY, 1);
    layout->addWidget(posRow);

    // Scale X, Y
    auto scaleRow = new QWidget();
    auto scaleH = new QHBoxLayout(scaleRow);
    scaleH->setContentsMargins(0, 2, 0, 2);
    scaleH->setSpacing(6);

    auto sLbl = new QLabel(tr("缩放 (Scale)"), scaleRow);
    sLbl->setMinimumWidth(80);
    sLbl->setStyleSheet("color: #a0a0aa; font-size: 11px;");
    scaleH->addWidget(sLbl);

    auto sSpinX = new QDoubleSpinBox(scaleRow);
    sSpinX->setRange(-10000, 10000);
    sSpinX->setValue(trans->xScale() * 100.0);
    sSpinX->setSuffix("%");
    sSpinX->setStyleSheet("background-color: #1e1e22; color: #ffffff; border: 1px solid #3c3c44; border-radius: 3px;");
    connect(sSpinX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [trans, this](double sx) {
        trans->setScale(sx * 0.01, trans->yScale());
        if (mScene) mScene->requestUpdate();
    });
    scaleH->addWidget(sSpinX, 1);

    auto sSpinY = new QDoubleSpinBox(scaleRow);
    sSpinY->setRange(-10000, 10000);
    sSpinY->setValue(trans->yScale() * 100.0);
    sSpinY->setSuffix("%");
    sSpinY->setStyleSheet("background-color: #1e1e22; color: #ffffff; border: 1px solid #3c3c44; border-radius: 3px;");
    connect(sSpinY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [trans, this](double sy) {
        trans->setScale(trans->xScale(), sy * 0.01);
        if (mScene) mScene->requestUpdate();
    });
    scaleH->addWidget(sSpinY, 1);
    layout->addWidget(scaleRow);

    // Rotation
    addDblSpinRow(tr("旋转 (Rot)"), trans->rot(), -36000, 36000, 1.0, "°", [trans](double r) {
        trans->setRotation(r);
    });

    // Opacity
    addDblSpinRow(tr("不透明 (Opacity)"), box->getOpacity(0) * 100.0, 0, 100, 1.0, "%", [box](double op) {
        box->setOpacity(op * 0.01);
    });
}

void AEPropertiesInspector::setupEffectsControls(QVBoxLayout *layout, BoundingBox *box) {
    const auto coll = box->rasterEffectsCollection();
    if (!coll) return;

    // Header button to add effect
    auto addBtn = new QPushButton(tr("+ 添加特效 (Add Effect)"));
    addBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #007acc;"
        "  color: #ffffff;"
        "  font-weight: bold;"
        "  border-radius: 4px;"
        "  padding: 6px 12px;"
        "}"
        "QPushButton:hover { background-color: #0098ff; }"
    );

    connect(addBtn, &QPushButton::clicked, [box, addBtn, this]() {
        QMenu menu;
        QHash<QString, QMenu*> catMenus;
        auto getCat = [&menu, &catMenus](const QString& cat) -> QMenu* {
            if (catMenus.contains(cat)) return catMenus[cat];
            auto m = menu.addMenu(cat);
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
    layout->addWidget(addBtn);

    const int numEffects = coll->ca_getNumberOfChildren();
    if (numEffects == 0) {
        auto emptyLbl = new QLabel(tr("当前图层未添加任何特效"), this);
        emptyLbl->setStyleSheet("color: #777780; font-style: italic; padding: 4px;");
        layout->addWidget(emptyLbl);
        return;
    }

    for (int i = 0; i < numEffects; ++i) {
        auto effect = coll->ca_getChildAt<RasterEffect>(i);
        if (!effect) continue;

        auto effectCard = new QFrame();
        effectCard->setObjectName("EffectCard");
        effectCard->setStyleSheet(
            "QFrame#EffectCard {"
            "  background-color: #242428;"
            "  border: 1px solid #3a3a42;"
            "  border-radius: 4px;"
            "  padding: 4px;"
            "}"
        );
        auto eLayout = new QVBoxLayout(effectCard);
        eLayout->setContentsMargins(4, 4, 4, 4);
        eLayout->setSpacing(4);

        // Effect Title Bar
        auto eHeader = new QWidget(effectCard);
        auto ehLayout = new QHBoxLayout(eHeader);
        ehLayout->setContentsMargins(0, 0, 0, 2);
        ehLayout->setSpacing(6);

        const QString effTitle = translatePropertyName(effect->prp_getName());
        auto title = new QLabel(effTitle, eHeader);
        title->setStyleSheet("font-weight: bold; color: #ffb86c; font-size: 11px;");
        ehLayout->addWidget(title, 1);

        auto delBtn = new QToolButton(eHeader);
        delBtn->setIcon(QIcon::fromTheme("edit-delete"));
        delBtn->setToolTip(tr("删除特效"));
        connect(delBtn, &QToolButton::clicked, [box, effect, this]() {
            box->removeRasterEffect(effect->ref<RasterEffect>());
            if (mScene) mScene->requestUpdate();
            refreshSelection();
        });
        ehLayout->addWidget(delBtn);

        eLayout->addWidget(eHeader);

        // Properties of the effect
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
        h->setContentsMargins(0, 1, 0, 1);
        h->setSpacing(6);

        auto lbl = new QLabel(propName, row);
        lbl->setMinimumWidth(85);
        lbl->setStyleSheet("color: #a8a8b2; font-size: 10px;");
        h->addWidget(lbl);

        const qreal minV = qrealAnim->getMinPossibleValue();
        const qreal maxV = qrealAnim->getMaxPossibleValue();
        const qreal curV = qrealAnim->getCurrentBaseValue();

        auto slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 1000);
        slider->setValue(qRound(((curV - minV) / (maxV - minV)) * 1000.0));
        slider->setStyleSheet(
            "QSlider::groove:horizontal { height: 4px; background: #3c3c44; border-radius: 2px; }"
            "QSlider::sub-page:horizontal { background: #007acc; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #e0e0e0; width: 10px; margin: -3px 0; border-radius: 5px; }"
        );

        auto spin = new QDoubleSpinBox(row);
        spin->setRange(minV, maxV);
        spin->setSingleStep(qrealAnim->getPrefferedValueStep());
        spin->setValue(curV);
        spin->setMinimumWidth(65);
        spin->setStyleSheet("background-color: #1a1a1e; color: #ffffff; border: 1px solid #36363e; border-radius: 3px; font-size: 10px;");

        connect(slider, &QSlider::valueChanged, [spin, qrealAnim, minV, maxV, this](int sv) {
            const qreal val = minV + (sv / 1000.0) * (maxV - minV);
            spin->blockSignals(true);
            spin->setValue(val);
            spin->blockSignals(false);
            qrealAnim->setCurrentBaseValue(val);
            if (mScene) mScene->requestUpdate();
        });

        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [slider, qrealAnim, minV, maxV, this](double v) {
            slider->blockSignals(true);
            slider->setValue(qRound(((v - minV) / (maxV - minV)) * 1000.0));
            slider->blockSignals(false);
            qrealAnim->setCurrentBaseValue(v);
            if (mScene) mScene->requestUpdate();
        });

        h->addWidget(slider, 1);
        h->addWidget(spin);
        layout->addWidget(row);
    } else if (auto colAnim = enve_cast<ColorAnimator*>(prop)) {
        auto row = new QWidget();
        auto h = new QHBoxLayout(row);
        h->setContentsMargins(0, 1, 0, 1);
        h->setSpacing(6);

        auto lbl = new QLabel(propName, row);
        lbl->setMinimumWidth(85);
        lbl->setStyleSheet("color: #a8a8b2; font-size: 10px;");
        h->addWidget(lbl);

        const QColor c = colAnim->getColor();
        auto btn = new QPushButton(row);
        btn->setStyleSheet(QString("background-color: %1; border: 1px solid #555560; border-radius: 3px; height: 18px;").arg(c.name()));

        connect(btn, &QPushButton::clicked, [btn, colAnim, row, this]() {
            const QColor picked = QColorDialog::getColor(colAnim->getColor(), row, tr("选择颜色"));
            if (picked.isValid()) {
                colAnim->setColor(picked);
                btn->setStyleSheet(QString("background-color: %1; border: 1px solid #555560; border-radius: 3px; height: 18px;").arg(picked.name()));
                if (mScene) mScene->requestUpdate();
            }
        });

        h->addWidget(btn, 1);
        layout->addWidget(row);
    }
}
