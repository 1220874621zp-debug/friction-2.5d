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

#ifndef BOOLPROPERTYWIDGET_H
#define BOOLPROPERTYWIDGET_H

#include <QWidget>
#include "Properties/boolproperty.h"
#include "Properties/boolpropertycontainer.h"
#include "Animators/boolanimator.h"

class BoolPropertyWidget : public QWidget {
    Q_OBJECT
public:
    BoolPropertyWidget(QWidget *parent = nullptr);

    void setTarget(BoolProperty *property);
    void setTarget(BoolPropertyContainer *property);
    // keyframable bool (IntAnimator-based): checked = effective value
    // at the current frame; clicking keys when keys exist/recording
    void setTarget(BoolAnimator * const animator);
protected:
    void mousePressEvent(QMouseEvent *);
    void paintEvent(QPaintEvent *);
    void enterEvent(QEvent *);
    void leaveEvent(QEvent *);
private:
    bool mHovered = false;
    qptr<BoolProperty> mTarget;
    qptr<BoolPropertyContainer> mTargetContainer;
    qptr<BoolAnimator> mTargetAnimator;
};

#endif // BOOLPROPERTYWIDGET_H
