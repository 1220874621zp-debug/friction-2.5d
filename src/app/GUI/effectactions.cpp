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

#include "RasterEffects/rastereffectmenucreator.h"
#include "BlendEffects/blendeffectmenucreator.h"
#include "TransformEffects/transformeffectmenucreator.h"
#include "PathEffects/patheffectmenucreator.h"

void MainWindow::setupMenuEffects()
{
    const QIcon eIcon = QIcon::fromTheme("effect");
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
                addRasterEffect(creator());
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
                addRasterEffect(creator());
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
                addRasterEffect(creator());
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
                    addPathEffect(creator());
                });
            }
            {
                const auto act = menu2->addAction(eIcon, name);
                act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Fill Effect)")));
                cmdAddAction(act);
                connect(act, &QAction::triggered, this, [this, creator]() {
                    addFillPathEffect(creator());
                });
            }
            {
                const auto act = menu3->addAction(eIcon, name);
                act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Outline Base Effect)")));
                cmdAddAction(act);
                connect(act, &QAction::triggered, this, [this, creator]() {
                    addOutlineBasePathEffect(creator());
                });
            }
            {
                const auto act = menu4->addAction(eIcon, name);
                act->setData(QString(name).prepend(tr("Add ")).append(tr(" (Outline Effect)")));
                cmdAddAction(act);
                connect(act, &QAction::triggered, this, [this, creator]() {
                    addOutlinePathEffect(creator());
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
                addBlendEffect(creator());
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
                addTransformEffect(creator());
            });
        };
        TransformEffectMenuCreator::forEveryEffect(adder);
    }
}

void MainWindow::addRasterEffect(const qsptr<RasterEffect> &effect)
{
    const auto box = getCurrentBox();
    if (!box) { return; }

    box->addRasterEffect(effect);
    mDocument.actionFinished();
}

void MainWindow::addBlendEffect(const qsptr<BlendEffect> &effect)
{
    const auto box = getCurrentBox();
    if (!box) { return; }

    box->addBlendEffect(effect);
    mDocument.actionFinished();
}

void MainWindow::addTransformEffect(const qsptr<TransformEffect> &effect)
{
    const auto box = getCurrentBox();
    if (!box) { return; }

    box->addTransformEffect(effect);
    mDocument.actionFinished();
}

void MainWindow::addPathEffect(const qsptr<PathEffect> &effect)
{
    const auto box = getCurrentBox();
    if (!box) { return; }

    box->addPathEffect(effect);
    mDocument.actionFinished();
}

void MainWindow::addFillPathEffect(const qsptr<PathEffect> &effect)
{
    const auto box = getCurrentBox();
    if (!box) { return; }

    box->addFillPathEffect(effect);
    mDocument.actionFinished();
}

void MainWindow::addOutlineBasePathEffect(const qsptr<PathEffect> &effect)
{
    const auto box = getCurrentBox();
    if (!box) { return; }

    box->addOutlineBasePathEffect(effect);
    mDocument.actionFinished();
}

void MainWindow::addOutlinePathEffect(const qsptr<PathEffect> &effect)
{
    const auto box = getCurrentBox();
    if (!box) { return; }

    box->addOutlinePathEffect(effect);
    mDocument.actionFinished();
}
