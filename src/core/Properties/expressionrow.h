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

// AE-style inline expression row: a transient SWT child of a
// QrealAnimator that renders a single-line script editor under the
// property row (toggled by the fx button in BoxSingleWidget). It is
// pure UI state - never serialized, never part of the property tree.

#ifndef EXPRESSIONROW_H
#define EXPRESSIONROW_H

#include "property.h"

class QrealAnimator;

class CORE_EXPORT ExpressionRow : public Property {
    Q_OBJECT
    e_OBJECT
protected:
    ExpressionRow(const QString& name, QrealAnimator * const target);
public:
    // the row for the animator currently expanded under it, null if
    // collapsed (the row's presence in the SWT tree IS the state)
    static ExpressionRow *sRowFor(const QrealAnimator * const anim);

    // expand under anim / collapse again; returns true if the state
    // changed (reparents the row to the animator so it dies with it);
    // the name labels the row (e.g. "fx x" / "fx y" for point channels)
    static bool sSetExpanded(QrealAnimator * const anim,
                             const bool expanded,
                             const QString& name = QStringLiteral("fx"));

    QrealAnimator *target() const { return mTarget; }

    // pure-UI helper, never serialized (empty stubs satisfy Property)
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const;
    void prp_readPropertyXEV_impl(const QDomElement& ele, const XevImporter& imp);
private:
    QPointer<QrealAnimator> mTarget;
};

#endif // EXPRESSIONROW_H
