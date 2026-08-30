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

#ifndef AEPROPERTIESINSPECTOR_H
#define AEPROPERTIESINSPECTOR_H

#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QFrame>
#include <functional>

class Document;
class Canvas;
class BoundingBox;
class PathBox;
class Animator;
class QrealAnimator;
class ColorAnimator;
class Property;

// High-performance custom-painted Keyframe Diamond Button (Zero CSS overhead)
class KeyframeDiamondButton : public QWidget
{
    Q_OBJECT
public:
    explicit KeyframeDiamondButton(Animator *anim, Canvas *scene, QWidget *parent = nullptr);
    void setDualAnimators(Animator *animX, Animator *animY);
    void setScene(Canvas *scene);

    QSize sizeHint() const override { return QSize(16, 20); }
    QSize minimumSizeHint() const override { return QSize(16, 20); }

signals:
    void keyframeToggled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Animator *mAnim = nullptr;
    Animator *mAnimY = nullptr;
    Canvas *mScene = nullptr;
    bool mHovered = false;
};

// High-performance custom-painted Color Swatch Button (Zero pixmap rebuild overhead)
class InspectorColorButton : public QWidget
{
    Q_OBJECT
public:
    explicit InspectorColorButton(ColorAnimator *anim, Canvas *scene, QWidget *parent = nullptr);

    QSize sizeHint() const override { return QSize(60, 20); }
    QSize minimumSizeHint() const override { return QSize(40, 20); }

signals:
    void colorChanged(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    ColorAnimator *mAnim = nullptr;
    Canvas *mScene = nullptr;
    bool mHovered = false;
};

class AEPropertiesInspector : public QScrollArea
{
    Q_OBJECT
public:
    explicit AEPropertiesInspector(Document &doc, QWidget *parent = nullptr);
    ~AEPropertiesInspector() override = default;

    void setCurrentScene(Canvas *scene);

public slots:
    void refreshSelection();
    void refreshValues();

protected:
    void changeEvent(QEvent *event) override;

private:
    QFrame* createSectionCard(const QString &title, const QIcon &icon, QGridLayout *&outGrid, bool defaultExpanded = true);
    QWidget* createKeyframeNav(Animator *anim);
    QWidget* createDualKeyframeNav(Animator *animX, Animator *animY);

    void buildSceneProperties();
    void buildBoxProperties(BoundingBox *box);
    void setupTransformControls(QGridLayout *grid, BoundingBox *box);
    void setupPathStyleControls(QGridLayout *grid, PathBox *pathBox);
    void setupEffectsControls(QVBoxLayout *layout, BoundingBox *box);
    void setupEffectPropertyControl(QGridLayout *grid, int row, Property *prop, BoundingBox *box);

    Document &mDoc;
    Canvas *mScene = nullptr;
    BoundingBox *mCurrentBox = nullptr;
    QWidget *mContainer = nullptr;
    QVBoxLayout *mMainLayout = nullptr;
    QList<QWidget*> mStatefulWidgets;
};

#endif // AEPROPERTIESINSPECTOR_H
