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

#include "scriptmanager.h"
#include "scriptconsole.h"
#include "mainwindow.h"

#include "Scripting/jsapi.h"
#include "appsupport.h"

#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDir>
#include <QDebug>
#include <QDockWidget>
#include <QPushButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QStyle>

ScriptManager::ScriptManager(MainWindow * const parent)
    : QObject(parent)
    , mMainWindow(parent)
{
    mConsole = new ScriptConsoleDock(parent);
    mConsole->setReloadCallback([this]() { reload(); });
    mConsole->setScriptsPath(AppSupport::getAppScriptsPath());

    mScriptsMenu = new QMenu(tr("Scripts"), parent);

    // note: no "Script Console" toggle here - the console dock is
    // always visible (non-closable) so a menu entry is redundant

    mScriptsMenu->addAction(QIcon::fromTheme("view_refresh"),
                            tr("Reload Scripts"),
                            this, &ScriptManager::reload);

    mScriptsMenu->addAction(QIcon::fromTheme("file_folder"),
                            tr("Open Scripts Folder"),
                            this, []() {
        const QString path = AppSupport::getAppScriptsPath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    mScriptsMenu->addSeparator();

    loadScripts();
    rebuildMenu();
}

void ScriptManager::reload()
{
    loadScripts();
    rebuildMenu();
    output(tr("Scripts reloaded (%1 command(s))")
           .arg(mCommands.count()));
}

void ScriptManager::output(const QString &message)
{
    if (mConsole) { mConsole->appendOutput(message); }
    qWarning() << "[script]" << message;
}

void ScriptManager::loadScripts()
{
    mCommands.clear();
    // drop previous script panels before rebuilding
    for (const auto dock : mPanels) {
        mMainWindow->removeDockWidget(dock);
        delete dock;
    }
    mPanels.clear();
    mPanelHosts.clear();

    const QString dirPath = AppSupport::getAppScriptsPath();
    const QDir dir(dirPath);
    const auto entries = dir.entryInfoList(
                QStringList() << QStringLiteral("*.js"),
                QDir::Files, QDir::Name);

    for (const auto &entry : entries) {
        const auto host = new Friction::Core::JsHost(this);
        host->setHandlers(
            // print / $.writeln -> console + debug log
            [this](const QString &message) { output(message); },
            // alert -> message box
            [this](const QString &message) {
                QMessageBox::information(mMainWindow,
                                         tr("Script"),
                                         message);
            },
            // confirm -> question box
            [this](const QString &message) {
                return QMessageBox::question(
                            mMainWindow, tr("Script"), message,
                            QMessageBox::Yes | QMessageBox::No)
                        == QMessageBox::Yes;
            });

        const QString error = host->loadScript(entry.absoluteFilePath());
        if (!error.isEmpty()) {
            output(QStringLiteral("%1: %2")
                   .arg(entry.fileName(), error));
            delete host;
            continue;
        }
        for (const auto &label : host->commandLabels()) {
            mCommands.insert(label, host);
        }
        if (host->panelDesc().valid) {
            createPanel(host);
        }
    }
}

void ScriptManager::createPanel(Friction::Core::JsHost * const host)
{
    const auto &desc = host->panelDesc();

    const auto dock = new QDockWidget(mMainWindow);
    // unique objectName from the script title (stable across reloads
    // for saveState persistence)
    dock->setObjectName(QStringLiteral("dockScriptPanel_%1")
                        .arg(QString::fromUtf8(desc.title.toUtf8()
                                               .toBase64(
                                                   QByteArray::Base64UrlEncoding |
                                                   QByteArray::OmitTrailingEquals))));
    dock->setWindowTitle(desc.title);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);

    const auto content = new QWidget(dock);
    const auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // slider rows (above the grid)
    for (const auto &s : desc.sliders) {
        const auto row = new QHBoxLayout();
        row->setSpacing(4);
        if (!s.label.isEmpty()) {
            const auto lbl = new QLabel(s.label, content);
            row->addWidget(lbl);
        }
        const auto slider = new QSlider(Qt::Horizontal, content);
        slider->setFocusPolicy(Qt::NoFocus);
        const int scale = qPow(10, qMax(0, s.decimals));
        slider->setRange(qRound(s.min * scale), qRound(s.max * scale));
        slider->setValue(qRound(s.value * scale));
        const auto spin = new QDoubleSpinBox(content);
        spin->setDecimals(qMax(0, s.decimals));
        spin->setRange(s.min, s.max);
        spin->setValue(s.value);
        spin->setFixedWidth(64);
        spin->setFocusPolicy(Qt::ClickFocus);
        row->addWidget(slider, 1);
        row->addWidget(spin);
        layout->addLayout(row);

        // slider <-> spinbox sync + callbacks
        connect(slider, &QSlider::valueChanged, this,
                [spin, scale](const int v) {
            spin->setValue(qreal(v) / scale);
        });
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [slider, scale](const double v) {
            slider->blockSignals(true);
            slider->setValue(qRound(v * scale));
            slider->blockSignals(false);
        });
        // drag = live preview (onChanging), coalesced by a 100ms
        // tail-merge timer (raw sliderMoved rates would flood the
        // undo stack and renderer); release/editing = final, immediate
        connect(slider, &QSlider::sliderMoved, this,
                [this, host, &s, scale](const int v) {
            mPendingSliderHost = host;
            mPendingSliderId = s.id;
            mPendingSliderValue = qreal(v) / scale;
            if (!mSliderThrottle) {
                mSliderThrottle = new QTimer(this);
                mSliderThrottle->setSingleShot(true);
                mSliderThrottle->setInterval(100);
                connect(mSliderThrottle, &QTimer::timeout, this, [this]() {
                    if (mPendingSliderHost) {
                        mPendingSliderHost->invokePanelValue(
                                    0, mPendingSliderId,
                                    mPendingSliderValue, QString());
                    }
                });
            }
            mSliderThrottle->start(); // restart: run when dragging pauses
        });
        // release: stop the pending preview and run the final value
        connect(slider, &QSlider::sliderReleased, this,
                [this, host, &s, slider, scale]() {
            if (mSliderThrottle) { mSliderThrottle->stop(); }
            host->invokePanelValue(1, s.id,
                                   qreal(slider->value()) / scale,
                                   QString());
        });
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                [host, &s, spin]() {
            host->invokePanelValue(1, s.id, spin->value(), QString());
        });
    }

    // combo rows (above the grid)
    for (const auto &c : desc.combos) {
        const auto row = new QHBoxLayout();
        row->setSpacing(4);
        if (!c.label.isEmpty()) {
            const auto lbl = new QLabel(c.label, content);
            row->addWidget(lbl);
        }
        const auto combo = new QComboBox(content);
        combo->addItems(c.options);
        combo->setCurrentIndex(
                    qBound(0, c.index, c.options.count() - 1));
        combo->setFocusPolicy(Qt::NoFocus);
        row->addWidget(combo, 1);
        layout->addLayout(row);

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [host, &c, combo](const int index) {
            host->invokePanelValue(1, c.id, index,
                                   combo->currentText());
        });
    }

    // button grid (columns from the script config)
    const int nGrid = desc.buttons.count();
    if (nGrid > 0) {
        const auto grid = new QGridLayout();
        grid->setSpacing(2);
        for (int i = 0; i < nGrid; i++) {
            const auto &btn = desc.buttons.at(i);
            const auto pb = new QPushButton(btn.label, content);
            pb->setMinimumSize(desc.buttonMinSize, desc.buttonMinSize);
            pb->setFocusPolicy(Qt::NoFocus);
            pb->setCursor(Qt::PointingHandCursor);
            if (!btn.tooltip.isEmpty()) { pb->setToolTip(btn.tooltip); }
            connect(pb, &QPushButton::clicked, this, [host, i]() {
                host->invokePanelButton(i);
            });
            grid->addWidget(pb, i / desc.columns, i % desc.columns);
        }
        layout->addLayout(grid);
    }

    // full-width extra buttons below the grid
    const int nExtra = desc.extraButtons.count();
    for (int i = 0; i < nExtra; i++) {
        const auto &btn = desc.extraButtons.at(i);
        const auto pb = new QPushButton(btn.label, content);
        pb->setFocusPolicy(Qt::NoFocus);
        pb->setCursor(Qt::PointingHandCursor);
        if (!btn.tooltip.isEmpty()) { pb->setToolTip(btn.tooltip); }
        connect(pb, &QPushButton::clicked, this, [host, nGrid, i]() {
            host->invokePanelButton(nGrid + i);
        });
        layout->addWidget(pb);
    }
    layout->addStretch();

    dock->setWidget(content);
    mMainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
    mPanels.append(dock);
    mPanelHosts.insert(host, dock);

    // panels start visible (script authors want them immediately)
}

void ScriptManager::rebuildMenu()
{
    // remove previous command actions (keep the fixed entries above
    // the separator, i.e. everything before the first command)
    const auto actions = mScriptsMenu->actions();
    bool afterSeparator = false;
    for (const auto act : actions) {
        if (act->isSeparator()) { afterSeparator = true; continue; }
        if (afterSeparator) {
            mScriptsMenu->removeAction(act);
            delete act;
        }
    }
    if (mCommands.isEmpty()) {
        const auto noneAct = mScriptsMenu->addAction(
                    tr("No scripts found (see Scripts folder)"));
        noneAct->setEnabled(false);
        return;
    }
    for (auto it = mCommands.constBegin(); it != mCommands.constEnd(); ++it) {
        const QString label = it.key();
        mScriptsMenu->addAction(label, this, [this, label]() {
            runCommand(label);
        });
    }
    // panel-type scripts (registerPanel) live as dock widgets - list
    // them here too so a closed panel can be brought back (click =
    // toggle visibility); without this the two entry kinds were split
    // across different places and closed panels were unreachable
    if(!mPanelHosts.isEmpty()) {
        mScriptsMenu->addSeparator();
        for (auto it = mPanelHosts.constBegin();
             it != mPanelHosts.constEnd(); ++it) {
            const auto host = it.key();
            const auto dock = it.value();
            const QString title = host->panelDesc().title;
            const auto act = mScriptsMenu->addAction(
                        tr("Panel: %1").arg(title), this,
                        [dock]() {
                if(dock->isVisible()) dock->hide();
                else {
                    dock->show();
                    dock->raise();
                }
            });
            act->setCheckable(true);
            act->setChecked(dock->isVisible());
            // keep the checkmark live while the user docks/undocks
            connect(dock, &QDockWidget::visibilityChanged,
                    act, &QAction::setChecked);
        }
    }
}

void ScriptManager::runCommand(const QString &label)
{
    const auto host = mCommands.value(label);
    if (!host) { return; }
    const QString error = host->callCommand(label);
    if (!error.isEmpty()) {
        output(QStringLiteral("%1: %2").arg(label, error));
        QMessageBox::warning(mMainWindow, tr("Script error"), error);
    }
}
