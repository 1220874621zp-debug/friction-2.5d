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

#ifndef VECTORTRACEDIALOG_H
#define VECTORTRACEDIALOG_H

#include <QDialog>

#include "vtracerprovider.h"

class QComboBox;
class QSpinBox;

class VectorTraceDialog : public QDialog {
    Q_OBJECT
public:
    explicit VectorTraceDialog(QWidget* const parent = nullptr);

    VTracer::Options options() const;

    // Shows the dialog (options pre-filled from the last run) and stores
    // the accepted values back into opts; returns false when cancelled.
    static bool sExec(VTracer::Options& opts, QWidget* const parent);

private:
    QComboBox* mModeCombo;
    QComboBox* mHierCombo;
    QComboBox* mColorModeCombo;
    QSpinBox* mFilterSpeckle;
    QSpinBox* mColorPrecision;
    QSpinBox* mMaxPaths;
    QSpinBox* mLayerDifference;
    QSpinBox* mCornerThreshold;
    QSpinBox* mLengthThreshold;
    QSpinBox* mMaxIterations;
    QSpinBox* mSpliceThreshold;
    QSpinBox* mPathPrecision;
};

#endif // VECTORTRACEDIALOG_H
