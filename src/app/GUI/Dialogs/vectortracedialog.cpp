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

#include "vectortracedialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "appsupport.h"

namespace {

QSpinBox* makeSpin(QWidget* const parent, const int min, const int max,
                   const int value, const QString& tooltip)
{
    auto const spin = new QSpinBox(parent);
    spin->setRange(min, max);
    spin->setValue(value);
    spin->setToolTip(tooltip);
    return spin;
}

} // namespace

VectorTraceDialog::VectorTraceDialog(QWidget* const parent)
    : QDialog(parent)
{
    setWindowTitle(tr("矢量描摹"));
    setMinimumWidth(420);

    auto const hint = new QLabel(tr("矢量描摹适用于<b>文字、Logo 与简单图形</b>的转绘；"
                                    "插画与照片类图像不建议使用，"
                                    "超出路径数上限会被自动拦截。"), this);
    hint->setWordWrap(true);

    mModeCombo = new QComboBox(this);
    mModeCombo->addItem(tr("曲线"));
    mModeCombo->addItem(tr("多边形"));
    mModeCombo->addItem(tr("像素"));
    mModeCombo->setToolTip(tr("曲线输出贝塞尔节点，最便于后续编辑"));

    mHierCombo = new QComboBox(this);
    mHierCombo->addItem(tr("层叠"));
    mHierCombo->addItem(tr("镶嵌"));
    mHierCombo->setToolTip(tr("层叠色块互相覆盖；镶嵌为无缝拼接"));

    mColorModeCombo = new QComboBox(this);
    mColorModeCombo->addItem(tr("彩色"));
    mColorModeCombo->addItem(tr("黑白"));

    mFilterSpeckle = makeSpin(this, 0, 128, 4,
        tr("过滤小色斑，值越大细节越少（0-128）"));
    mColorPrecision = makeSpin(this, 1, 8, 6,
        tr("颜色量化精度，值越大保留的颜色越多（1-8）"));
    mMaxPaths = makeSpin(this, 10, 1000, 500,
        tr("超过该路径数时自动收紧参数重试，仍超限则拦截"));

    auto const basicForm = new QFormLayout;
    basicForm->addRow(tr("拟合模式"), mModeCombo);
    basicForm->addRow(tr("层次结构"), mHierCombo);
    basicForm->addRow(tr("颜色模式"), mColorModeCombo);
    basicForm->addRow(tr("斑点过滤"), mFilterSpeckle);
    basicForm->addRow(tr("颜色精度"), mColorPrecision);
    basicForm->addRow(tr("最高路径数"), mMaxPaths);

    auto const advanced = new QGroupBox(tr("高级参数"), this);
    mLayerDifference = makeSpin(advanced, 0, 256, 16,
        tr("相邻色层合并的最小色差"));
    mCornerThreshold = makeSpin(advanced, 0, 180, 60,
        tr("角点阈值，越小转折越多"));
    mLengthThreshold = makeSpin(advanced, 1, 64, 4,
        tr("短线段合并长度阈值"));
    mMaxIterations = makeSpin(advanced, 1, 64, 10,
        tr("路径拟合最大迭代次数"));
    mSpliceThreshold = makeSpin(advanced, 0, 180, 45,
        tr("节点拼接阈值，越小节点越多"));
    mPathPrecision = makeSpin(advanced, 0, 8, 2,
        tr("路径坐标小数位数"));
    auto const advancedForm = new QFormLayout(advanced);
    advancedForm->addRow(tr("层差"), mLayerDifference);
    advancedForm->addRow(tr("角点阈值"), mCornerThreshold);
    advancedForm->addRow(tr("长度阈值"), mLengthThreshold);
    advancedForm->addRow(tr("最大迭代"), mMaxIterations);
    advancedForm->addRow(tr("拼接阈值"), mSpliceThreshold);
    advancedForm->addRow(tr("坐标精度"), mPathPrecision);

    auto const buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                              QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("转绘"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto const mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(hint);
    mainLayout->addLayout(basicForm);
    mainLayout->addWidget(advanced);
    mainLayout->addWidget(buttons);
}

VTracer::Options VectorTraceDialog::options() const
{
    VTracer::Options opts;
    opts.mode = mModeCombo->currentIndex();
    opts.hierarchical = mHierCombo->currentIndex();
    opts.binary = mColorModeCombo->currentIndex();
    opts.filterSpeckle = mFilterSpeckle->value();
    opts.colorPrecision = mColorPrecision->value();
    opts.maxPaths = mMaxPaths->value();
    opts.layerDifference = mLayerDifference->value();
    opts.cornerThreshold = mCornerThreshold->value();
    opts.lengthThreshold = mLengthThreshold->value();
    opts.maxIterations = mMaxIterations->value();
    opts.spliceThreshold = mSpliceThreshold->value();
    opts.pathPrecision = mPathPrecision->value();
    return opts;
}

bool VectorTraceDialog::sExec(VTracer::Options& opts, QWidget* const parent)
{
    const QString group = QStringLiteral("VectorTrace");

    VectorTraceDialog dialog(parent);
    dialog.mModeCombo->setCurrentIndex(
                AppSupport::getSettings(group, "mode", opts.mode).toInt());
    dialog.mHierCombo->setCurrentIndex(
                AppSupport::getSettings(group, "hierarchical", opts.hierarchical).toInt());
    dialog.mColorModeCombo->setCurrentIndex(
                AppSupport::getSettings(group, "binary", opts.binary).toInt());
    dialog.mFilterSpeckle->setValue(
                AppSupport::getSettings(group, "filterSpeckle", opts.filterSpeckle).toInt());
    dialog.mColorPrecision->setValue(
                AppSupport::getSettings(group, "colorPrecision", opts.colorPrecision).toInt());
    dialog.mMaxPaths->setValue(
                AppSupport::getSettings(group, "maxPaths", opts.maxPaths).toInt());
    dialog.mLayerDifference->setValue(
                AppSupport::getSettings(group, "layerDifference", opts.layerDifference).toInt());
    dialog.mCornerThreshold->setValue(
                AppSupport::getSettings(group, "cornerThreshold", opts.cornerThreshold).toInt());
    dialog.mLengthThreshold->setValue(
                AppSupport::getSettings(group, "lengthThreshold", opts.lengthThreshold).toInt());
    dialog.mMaxIterations->setValue(
                AppSupport::getSettings(group, "maxIterations", opts.maxIterations).toInt());
    dialog.mSpliceThreshold->setValue(
                AppSupport::getSettings(group, "spliceThreshold", opts.spliceThreshold).toInt());
    dialog.mPathPrecision->setValue(
                AppSupport::getSettings(group, "pathPrecision", opts.pathPrecision).toInt());

    if (dialog.exec() != QDialog::Accepted) { return false; }

    opts = dialog.options();
    AppSupport::setSettings(group, "mode", opts.mode);
    AppSupport::setSettings(group, "hierarchical", opts.hierarchical);
    AppSupport::setSettings(group, "binary", opts.binary);
    AppSupport::setSettings(group, "filterSpeckle", opts.filterSpeckle);
    AppSupport::setSettings(group, "colorPrecision", opts.colorPrecision);
    AppSupport::setSettings(group, "maxPaths", opts.maxPaths);
    AppSupport::setSettings(group, "layerDifference", opts.layerDifference);
    AppSupport::setSettings(group, "cornerThreshold", opts.cornerThreshold);
    AppSupport::setSettings(group, "lengthThreshold", opts.lengthThreshold);
    AppSupport::setSettings(group, "maxIterations", opts.maxIterations);
    AppSupport::setSettings(group, "spliceThreshold", opts.spliceThreshold);
    AppSupport::setSettings(group, "pathPrecision", opts.pathPrecision);
    return true;
}
