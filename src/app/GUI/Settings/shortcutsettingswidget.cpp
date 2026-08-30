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

#include "shortcutsettingswidget.h"
#include "appsupport.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>

// full table of configurable shortcuts.
// active entries are read at startup by toolbox/menu/timeline;
// inactive entries are reserved for planned features (kept in
// settings so they survive restarts and are ready to bind later).
// NOTE: in the AE preset, P/S/R/T/A/U belong to the property
// reveal shortcuts (like AE), so tool keys that would conflict
// (localPivot P, colorBookmark B, pointTransform A) are disabled.
const QList<ShortcutEntry> &ShortcutSettingsWidget::entries()
{
    static const QList<ShortcutEntry> list = {
        // id, name, category, default, ae, active
        // --- tools (bound in ToolBox) ---
        {"boxTransform", tr("Object Mode (Select Tool)"), tr("Tools"), "F1", "V", true},
        {"pointTransform", tr("Point Mode (Edit Points)"), tr("Tools"), "F2", "", true},
        {"pathCreate", tr("Add Path (Pen Tool)"), tr("Tools"), "F3", "G", true},
        {"drawPath", tr("Draw Path (Freehand)"), tr("Tools"), "F4", "F4", true},
        {"circleMode", tr("Add Circle (Shape)"), tr("Tools"), "F5", "Q", true},
        {"rectMode", tr("Add Rectangle (Shape)"), tr("Tools"), "F6", "Shift+Q", true},
        {"textMode", tr("Add Text"), tr("Tools"), "F7", "Ctrl+T", true},
        {"nullMode", tr("Add Null Object"), tr("Tools"), "F8", "F8", true},
        {"pickMode", tr("Color Pick Mode (Eyedropper)"), tr("Tools"), "F9", "F9", true},
        {"localPivot", tr("Pivot Global / Local"), tr("Tools"), "P", "", true},
        {"colorBookmark", tr("Bookmark Current Color"), tr("Tools"), "B", "", true},
        // --- playback / navigation (bound in TimelineDockWidget) ---
        {"rewind", tr("Go to First Frame"), tr("Timeline"), "Shift+Left", "Home", true},
        {"fastForward", tr("Go to Last Frame"), tr("Timeline"), "Shift+Right", "End", true},
        // --- property reveal (AE A/P/S/R/T/U, bound in TimelineDockWidget) ---
        {"showAnchor", tr("Show Anchor Point Property"), tr("Properties"), "", "A", true},
        {"showPosition", tr("Show Position Property"), tr("Properties"), "", "P", true},
        {"showScale", tr("Show Scale Property"), tr("Properties"), "", "S", true},
        {"showRotation", tr("Show Rotation Property"), tr("Properties"), "", "R", true},
        {"showOpacity", tr("Show Opacity Property"), tr("Properties"), "", "T", true},
        {"showAnimated", tr("Show Animated Properties (U)"), tr("Properties"), "", "U", true},
        // --- global (bound in Menu / main window) ---
        {"quickEffects", tr("Quick Search Effects (AE: FX Console)"), tr("Effects"), "Ctrl+Space", "Ctrl+Space", true},
        {"fullScreen", tr("Full Screen Preview"), tr("View"), "F11", "`", true},
        {"cmdPalette", tr("Command Palette"), tr("View"), "Ctrl+P", "Ctrl+P", true},
        {"previewSVG", tr("Preview SVG"), tr("File"), "Ctrl+F12", "Ctrl+F12", true},
        {"exportSVG", tr("Export SVG"), tr("File"), "Shift+F12", "Shift+F12", true},
        {"addToQue", tr("Add to Render Queue"), tr("Scene"), "F12", "F12", true},
        // --- reserved for planned features (not bound yet) ---
        {"newScene", tr("New Scene (AE: Composition)"), tr("Scene"), "", "Ctrl+N", false},
        {"sceneSettings", tr("Scene Settings (AE: Comp Settings)"), tr("Scene"), "", "Ctrl+K", false},
        {"duplicate", tr("Duplicate Layer"), tr("Layer"), "", "Ctrl+D", false},
        {"splitLayer", tr("Split Layer at Playhead"), tr("Layer"), "", "Ctrl+Shift+D", false},
        {"groupLayers", tr("Group Layers (AE: Pre-compose)"), tr("Layer"), "", "Ctrl+Shift+C", false},
        {"renameLayer", tr("Rename Layer"), tr("Layer"), "", "Return", false},
        {"raiseLayer", tr("Raise Layer One Level"), tr("Layer"), "", "Ctrl+]", false},
        {"lowerLayer", tr("Lower Layer One Level"), tr("Layer"), "", "Ctrl+[", false},
        {"nextKeyframe", tr("Go to Next Keyframe"), tr("Timeline"), "", "K", false},
        {"prevKeyframe", tr("Go to Previous Keyframe"), tr("Timeline"), "", "J", false},
        {"layerInPoint", tr("Go to Layer In-Point"), tr("Timeline"), "", "I", false},
        {"layerOutPoint", tr("Go to Layer Out-Point"), tr("Timeline"), "", "O", false},
        {"workAreaStart", tr("Set Work Area Start"), tr("Timeline"), "", "B", false},
        {"workAreaEnd", tr("Set Work Area End"), tr("Timeline"), "", "N", false},
        {"stepFrameBack", tr("Step One Frame Back"), tr("Timeline"), "", "Left", false},
        {"stepFrameFwd", tr("Step One Frame Forward"), tr("Timeline"), "", "Right", false}
    };
    return list;
}

ShortcutSettingsWidget::ShortcutSettingsWidget(QWidget *parent)
    : SettingsWidget(parent)
{
    // NOTE: SettingsWidget base already installs the main layout;
    // use addLayout()/addWidget() instead of setLayout()
    // preset selector row
    const auto presetLayout = new QHBoxLayout;
    presetLayout->addWidget(new QLabel(tr("Preset"), this));
    mPreset = new QComboBox(this);
    mPreset->addItem(tr("Default (Friction)"), "default");
    mPreset->addItem(tr("AE (After Effects)"), "ae");
    mPreset->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    mPreset->setMinimumWidth(180);
    presetLayout->addWidget(mPreset);
    const auto applyPresetBtn = new QPushButton(tr("Apply Preset"), this);
    presetLayout->addWidget(applyPresetBtn);
    presetLayout->addStretch();
    addLayout(presetLayout);

    mTable = new QTableWidget(this);
    mTable->setColumnCount(4);
    mTable->setHorizontalHeaderLabels({tr("Action"),
                                       tr("Category"),
                                       tr("AE Default"),
                                       tr("Shortcut")});
    mTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    mTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mTable->verticalHeader()->hide();
    mTable->setSelectionMode(QAbstractItemView::NoSelection);
    addWidget(mTable);

    const auto note = new QLabel(tr("Changes take effect after restart. "
                                    "Entries marked [reserved] are planned "
                                    "features and will be bound in a "
                                    "future version."), this);
    note->setWordWrap(true);
    addWidget(note);

    connect(applyPresetBtn, &QPushButton::released,
            this, [this]() { applyPreset(mPreset->currentData().toString()); });

    // restore the stored preset selection
    const QString preset = AppSupport::getSettings("shortcuts",
                                                   "preset",
                                                   "default").toString();
    const int idx = mPreset->findData(preset);
    mPreset->setCurrentIndex(idx < 0 ? 0 : idx);

    populateTable(preset);
}

void ShortcutSettingsWidget::applyPreset(const QString &preset)
{
    // write every entry's preset value into settings immediately
    for (const auto &e : entries()) {
        const QString seq = preset == "ae" ? e.ae : e.def;
        AppSupport::setSettings("shortcuts", e.id, seq);
    }
    AppSupport::setSettings("shortcuts", "preset", preset);
    populateTable(preset);
}

void ShortcutSettingsWidget::populateTable(const QString &preset)
{
    mTable->setRowCount(0);
    mEdits.clear();
    const auto &list = entries();
    mTable->setRowCount(list.count());
    int row = 0;
    for (const auto &e : list) {
        const auto nameItem = new QTableWidgetItem(
                    e.active ? e.name
                             : QStringLiteral("%1 [%2]").arg(
                                   e.name, tr("reserved")));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        mTable->setItem(row, 0, nameItem);

        const auto catItem = new QTableWidgetItem(e.category);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsEditable);
        mTable->setItem(row, 1, catItem);

        const auto aeItem = new QTableWidgetItem(e.ae);
        aeItem->setFlags(aeItem->flags() & ~Qt::ItemIsEditable);
        mTable->setItem(row, 2, aeItem);

        const auto edit = new QKeySequenceEdit(
                    QKeySequence(AppSupport::getSettings("shortcuts",
                                                         e.id,
                                                         preset == "ae" ? e.ae : e.def)
                                 .toString()),
                    this);
        mEdits.append(edit);
        mTable->setCellWidget(row, 3, edit);
        mTable->setRowHeight(row, edit->sizeHint().height());
        row++;
    }
}

void ShortcutSettingsWidget::applySettings()
{
    const auto &list = entries();
    for (int i = 0; i < mEdits.count() && i < list.count(); i++) {
        AppSupport::setSettings("shortcuts",
                                list.at(i).id,
                                mEdits.at(i)->keySequence().toString());
    }
    AppSupport::setSettings("shortcuts",
                            "preset",
                            mPreset->currentData().toString());
}

void ShortcutSettingsWidget::updateSettings(bool restore)
{
    Q_UNUSED(restore)
    const QString preset = AppSupport::getSettings("shortcuts",
                                                   "preset",
                                                   "default").toString();
    const int idx = mPreset->findData(preset);
    mPreset->setCurrentIndex(idx < 0 ? 0 : idx);
    populateTable(preset);
}
