/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#ifndef AEPROPERTIESINSPECTOR_H
#define AEPROPERTIESINSPECTOR_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QToolButton>
#include <QPushButton>
#include <QGroupBox>
#include <QPointer>
#include "smartPointers/ememory.h"

class Canvas;
class BoundingBox;
class Document;
class BasicTransformAnimator;
class RasterEffect;
class Property;

class AEPropertiesInspector : public QScrollArea {
    Q_OBJECT
public:
    explicit AEPropertiesInspector(Document &doc, QWidget *parent = nullptr);
    ~AEPropertiesInspector() override = default;

    void setCurrentScene(Canvas *scene);

public slots:
    void refreshSelection();
    void refreshValues();

private:
    void buildSceneProperties();
    void buildBoxProperties(BoundingBox *box);
    QWidget* createCard(const QString &title, const QIcon &icon = QIcon());
    void setupTransformControls(QVBoxLayout *layout, BoundingBox *box);
    void setupEffectsControls(QVBoxLayout *layout, BoundingBox *box);
    void setupAppearanceControls(QVBoxLayout *layout, BoundingBox *box);
    void setupEffectPropertyControl(QVBoxLayout *layout, Property *prop, BoundingBox *box);

    Document &mDoc;
    QPointer<Canvas> mScene;
    QWidget *mContainer;
    QVBoxLayout *mMainLayout;
    bool mUpdatingUI = false;

    // Track active target box
    BoundingBox *mCurrentBox = nullptr;
};

#endif // AEPROPERTIESINSPECTOR_H
