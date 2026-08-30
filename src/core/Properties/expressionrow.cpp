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

#include "expressionrow.h"

#include "Animators/qrealanimator.h"

#include <QHash>
#include <QPointer>

namespace {
// expanded rows keyed by animator; QPointer values so rows deleted
// with their parent animator vanish from the map on the next lookup
QHash<const QrealAnimator*, QPointer<ExpressionRow>>& rowsMap() {
    static QHash<const QrealAnimator*, QPointer<ExpressionRow>> map;
    return map;
}
}

ExpressionRow::ExpressionRow(const QString& name,
                             QrealAnimator * const target) :
    Property(name), mTarget(target) {}

QDomElement ExpressionRow::prp_writePropertyXEV_impl(
        const XevExporter& exp) const {
    Q_UNUSED(exp)
    return QDomElement();
}

void ExpressionRow::prp_readPropertyXEV_impl(const QDomElement& ele,
                                             const XevImporter& imp) {
    Q_UNUSED(ele)
    Q_UNUSED(imp)
}

ExpressionRow *ExpressionRow::sRowFor(
        const QrealAnimator * const anim) {
    const auto it = rowsMap().constFind(anim);
    if (it == rowsMap().constEnd()) return nullptr;
    return it.value().data();
}

bool ExpressionRow::sSetExpanded(QrealAnimator * const anim,
                                 const bool expanded,
                                 const QString& name) {
    auto& rows = rowsMap();
    if (expanded) {
        if (rows.value(anim)) return false;
        const auto row = new ExpressionRow(name, anim);
        // die with the animator, no matter in which order the SWT
        // tree and the property system are torn down
        row->QObject::setParent(anim);
        rows.insert(anim, row);
        anim->SWT_addChild(row);
    } else {
        const auto row = rows.value(anim);
        if (!row) return false;
        anim->SWT_removeChild(row);
        rows.remove(anim);
        delete row;
    }
    return true;
}
