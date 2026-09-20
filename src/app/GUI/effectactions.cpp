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

#include "mainwindow.h"

#include <QStatusBar>
#include <QTimer>
#include <QBuffer>

#include "aepropertiesinspector.h"
#include "RasterEffects/rastereffectmenucreator.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "PathEffects/patheffectmenucreator.h"
#include "ReadWrite/ereadstream.h"
#include "exceptions.h"
#include "quickeffectsearchdialog.h"

void MainWindow::setupMenuEffects()
{
    const QIcon eIcon = QIcon::fromTheme("effect");

    const QString fxKey = AppSupport::getSettings("shortcuts", "quickEffects", "Ctrl+Space").toString();
    const auto quickAct = mEffectsMenu->addAction(eIcon, tr("Quick Search Effects..."), this, &MainWindow::showQuickEffectSearch, QKeySequence(fxKey));
    quickAct->setData(tr("Quick Search Effects (AE: FX Console)"));
    cmdAddAction(quickAct);
    // pre-create the dialog: building it inside the first shortcut
    // press races with async window activation on Windows (first
    // press could look like "no reaction"); an existing hidden Tool
    // window shows reliably on the first press
    mQuickEffectSearch = new QuickEffectSearchDialog(this, this);
    mEffectsMenu->addSeparator();

    QMap<QString, QMenu*> categoryMenus;

    const auto getCatMenu = [this, &categoryMenus, eIcon](const QString& category) -> QMenu* {
        const QString catName = category.isEmpty() ? tr("General") : category;
        if (!categoryMenus.contains(catName)) {
            categoryMenus[catName] = mEffectsMenu->addMenu(eIcon, catName);
        }
        return categoryMenus[catName];
    };

    // 1. Raster Effects by Category (Blur, Color, Distort, Light, Stylize, Simulation, Transitions...)
    RasterEffectMenuCreator::forEveryEffectCore(
        [this, getCatMenu, eIcon](const QString& name, const QString& cat,
                                  const RasterEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) { return; }
            const auto targetMenu = getCatMenu(cat);
            const auto act = targetMenu->addAction(eIcon, name);
            act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Raster Effect)")));
            cmdAddAction(act);
            connect(act, &QAction::triggered, this, [this, creator]() {
                addRasterEffectToTarget(creator, nullptr);
            });
        });

    RasterEffectMenuCreator::forEveryEffectCustom(
        [this, getCatMenu, eIcon](const QString& name, const QString& cat,
                                  const RasterEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) { return; }
            const auto targetMenu = getCatMenu(cat.isEmpty() ? tr("Custom") : cat);
            const auto act = targetMenu->addAction(eIcon, name);
            act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Raster Effect)")));
            cmdAddAction(act);
            connect(act, &QAction::triggered, this, [this, creator]() {
                addRasterEffectToTarget(creator, nullptr);
            });
        });

    RasterEffectMenuCreator::forEveryEffectShader(
        [this, getCatMenu, eIcon](const QString& name, const QString& cat,
                                  const RasterEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) { return; }
            const auto targetMenu = getCatMenu(cat.isEmpty() ? tr("Shader") : cat);
            const auto act = targetMenu->addAction(eIcon, name);
            act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Raster Effect)")));
            cmdAddAction(act);
            connect(act, &QAction::triggered, this, [this, creator]() {
                addRasterEffectToTarget(creator, nullptr);
            });
        });

    mEffectsMenu->addSeparator();

    // 2. Path Effects
    {
        const auto menu1 = mEffectsMenu->addMenu(eIcon, tr("Path Effects"));
        const auto menu2 = mEffectsMenu->addMenu(eIcon, tr("Fill Effects"));
        const auto menu3 = mEffectsMenu->addMenu(eIcon, tr("Outline Base Effects"));
        const auto menu4 = mEffectsMenu->addMenu(eIcon, tr("Outline Effects"));
        const auto adder = [this, menu1, menu2, menu3, menu4, eIcon](const QString& name,
                                   const PathEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) { return; }
            {
                const auto act = menu1->addAction(eIcon, name);
                act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Path Effect)")));
                cmdAddAction(act);
                connect(act, &QAction::triggered, this, [this, creator]() {
                    addPathEffectToTarget(creator, nullptr);
                });
            }
            {
                const auto act = menu2->addAction(eIcon, name);
                act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Fill Effect)")));
                cmdAddAction(act);
                connect(act, &QAction::triggered, this, [this, creator]() {
                    addFillPathEffectToTarget(creator, nullptr);
                });
            }
            {
                const auto act = menu3->addAction(eIcon, name);
                act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Outline Base Effect)")));
                cmdAddAction(act);
                connect(act, &QAction::triggered, this, [this, creator]() {
                    addOutlineBasePathEffectToTarget(creator, nullptr);
                });
            }
            {
                const auto act = menu4->addAction(eIcon, name);
                act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Outline Effect)")));
                cmdAddAction(act);
                connect(act, &QAction::triggered, this, [this, creator]() {
                    addOutlinePathEffectToTarget(creator, nullptr);
                });
            }
        };
        PathEffectMenuCreator::forEveryEffect(adder);
    }

    // 3. Blend Effects
    {
        const auto menu = mEffectsMenu->addMenu(eIcon, tr("Blend Effects"));
        const auto adder = [this, menu, eIcon](const QString& name,
                                        const BlendEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) { return; }
            const auto act = menu->addAction(eIcon, name);
            act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Blend Effect)")));
            cmdAddAction(act);
            connect(act, &QAction::triggered, this, [this, creator]() {
                addBlendEffectToTarget(creator, nullptr);
            });
        };
        BlendEffectMenuCreator::forEveryEffect(adder);
    }

    // 4. Transform Effects
    {
        const auto menu = mEffectsMenu->addMenu(eIcon, tr("Transform Effects"));
        const auto adder = [this, menu, eIcon](const QString& name,
                                        const TransformEffectMenuCreator::EffectCreator& creator) {
            if (name.isEmpty()) { return; }
            const auto act = menu->addAction(eIcon, name);
            act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Transform Effect)")));
            cmdAddAction(act);
            connect(act, &QAction::triggered, this, [this, creator]() {
                addTransformEffectToTarget(creator, nullptr);
            });
        };
        TransformEffectMenuCreator::forEveryEffect(adder);
    }
}

QList<BoundingBox*> MainWindow::effectApplyTargets(BoundingBox* const target)
{
    if (target) { return {target}; }
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return {}; }
    return scene->getSelectedBoxesList();
}

namespace {
// shared tail for every apply path: finish the undo set, expand the
// layer + effects rows so the new effect is immediately visible, and
// refresh + scroll the AE inspector to the rebuilt pipeline
void finishEffectApplication(MainWindow* const,
                              AEPropertiesInspector* const inspector)
{
    if (inspector) {
        inspector->refreshSelection();
        // the scroll range settles one layout pass after the rebuild
        QTimer::singleShot(0, inspector, &AEPropertiesInspector::scrollToEnd);
    }
}
}

void MainWindow::addRasterEffectToTarget(
        const std::function<qsptr<RasterEffect>()> &creator,
        BoundingBox* const target)
{
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) {
        statusBar()->showMessage(tr("请先选中一个图层"), 3000);
        return;
    }
    for (const auto& box : targets) {
        const auto effect = creator();
        box->addRasterEffect(effect);
        if (effect) { effect->SWT_setContentVisible(true); }
        box->SWT_setContentVisible(true);
        if (const auto coll = box->rasterEffectsCollection()) {
            coll->SWT_setContentVisible(true);
        }
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::addBlendEffectToTarget(
        const std::function<qsptr<BlendEffect>()> &creator,
        BoundingBox* const target)
{
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) {
        statusBar()->showMessage(tr("请先选中一个图层"), 3000);
        return;
    }
    for (const auto& box : targets) {
        box->addBlendEffect(creator());
        box->SWT_setContentVisible(true);
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::addTransformEffectToTarget(
        const std::function<qsptr<TransformEffect>()> &creator,
        BoundingBox* const target)
{
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) {
        statusBar()->showMessage(tr("请先选中一个图层"), 3000);
        return;
    }
    for (const auto& box : targets) {
        box->addTransformEffect(creator());
        box->SWT_setContentVisible(true);
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::addPathEffectToTarget(
        const std::function<qsptr<PathEffect>()> &creator,
        BoundingBox* const target)
{
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) {
        statusBar()->showMessage(tr("请先选中一个图层"), 3000);
        return;
    }
    for (const auto& box : targets) {
        box->addPathEffect(creator());
        box->SWT_setContentVisible(true);
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::addFillPathEffectToTarget(
        const std::function<qsptr<PathEffect>()> &creator,
        BoundingBox* const target)
{
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) { return; }
    for (const auto& box : targets) {
        box->addFillPathEffect(creator());
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::addOutlineBasePathEffectToTarget(
        const std::function<qsptr<PathEffect>()> &creator,
        BoundingBox* const target)
{
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) { return; }
    for (const auto& box : targets) {
        box->addOutlineBasePathEffect(creator());
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::addOutlinePathEffectToTarget(
        const std::function<qsptr<PathEffect>()> &creator,
        BoundingBox* const target)
{
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) { return; }
    for (const auto& box : targets) {
        box->addOutlinePathEffect(creator());
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::applyRasterEffectStreamToTargets(const QByteArray &streamData,
                                                  BoundingBox* const target)
{
    if (streamData.isEmpty()) { return; }
    const auto targets = effectApplyTargets(target);
    if (targets.isEmpty()) {
        statusBar()->showMessage(tr("请先选中一个图层"), 3000);
        return;
    }
    int applied = 0;
    for (const auto& box : targets) {
        const auto coll = box->rasterEffectsCollection();
        if (!coll) { continue; }
        QByteArray data = streamData;
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        eReadStream readStream(&buffer);
        try {
            coll->prp_readProperty(readStream);
            applied++;
        } catch (const std::exception& e) {
            gPrintExceptionCritical(e);
        }
        buffer.close();
        box->SWT_setContentVisible(true);
        coll->SWT_setContentVisible(true);
    }
    if (applied == 0) {
        statusBar()->showMessage(tr("预设应用失败：效果不可用"), 4000);
        return;
    }
    mDocument.actionFinished();
    finishEffectApplication(this, mPropertiesInspector);
}

void MainWindow::showQuickEffectSearch()
{
    if (!mQuickEffectSearch) {
        mQuickEffectSearch = new QuickEffectSearchDialog(this, this);
    }
    mQuickEffectSearch->showAtCursor();
}
