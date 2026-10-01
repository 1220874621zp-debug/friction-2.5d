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
#include "GUI/coloranimatorbutton.h"

#include "Scripting/jsapi.h"
#include "appsupport.h"

#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDir>
#include <QDebug>
#include <QDockWidget>
#include <QTimer>
#include <QPushButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QDateTime>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QStyle>

ScriptManager::ScriptManager(MainWindow * const parent)
    : QObject(parent)
    , mMainWindow(parent)
{
    mConsole = new ScriptConsoleDock(parent);
    mConsole->setReloadCallback([this]() { reload(); });
    mConsole->setScriptsPath(AppSupport::getAppScriptsPath());

    mScriptsMenu = new QMenu(tr("Scripts"), parent);

    // the console toggle lives here only; the Workspace > Panels
    // menu deliberately does not repeat it
    mScriptsMenu->addAction(
                QIcon::fromTheme("cmd"),
                tr("Script Console"),
                this, [this]() {
        if (!mConsole) { return; }
        if (mConsole->isHidden()) {
            mConsole->show();
            mConsole->raise();
        } else {
            mConsole->hide();
        }
    });

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
    // recreates the panels the user has open immediately visible
    mScriptsReload = true;
    loadScripts();
    mScriptsReload = false;
    rebuildMenu();
    output(tr("Scripts reloaded (%1 command(s))")
           .arg(mCommands.count()));
}

void ScriptManager::output(const QString &message)
{
    if (mConsole) { mConsole->appendOutput(message); }
    qWarning() << "[script]" << message;
}

//---------------------------- ScriptPaintProxy ----------------------------

ScriptPaintProxy::ScriptPaintProxy(QObject * const parent)
    : QObject(parent)
    , mEpoch(QDateTime::currentMSecsSinceEpoch())
{}

void ScriptPaintProxy::begin(QPainter * const p)
{
    mPainter = p;
}

void ScriptPaintProxy::end()
{
    mPainter = nullptr;
}

void ScriptPaintProxy::clear(const QString &color)
{
    if (!mPainter) { return; }
    mPainter->fillRect(mPainter->viewport(), QColor(color));
}

void ScriptPaintProxy::grid(qreal cell, const QString &color)
{
    if (!mPainter || cell < 1) { return; }
    QPen pen{QColor(color)};
    pen.setWidthF(0.5);
    mPainter->setPen(pen);
    const auto r = mPainter->viewport();
    for (qreal x = 0; x <= r.width(); x += cell) {
        mPainter->drawLine(QPointF(x, 0), QPointF(x, r.height()));
    }
    for (qreal y = 0; y <= r.height(); y += cell) {
        mPainter->drawLine(QPointF(0, y), QPointF(r.width(), y));
    }
}

void ScriptPaintProxy::setStroke(const QString &color, qreal width)
{
    if (!mPainter) { return; }
    QPen pen{QColor(color)};
    pen.setWidthF(qMax(0.1, width));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    mPainter->setPen(pen);
}

void ScriptPaintProxy::setCap(const QString &cap)
{
    if (!mPainter) { return; }
    auto pen = mPainter->pen();
    if (cap == QLatin1String("butt")) { pen.setCapStyle(Qt::FlatCap); }
    else if (cap == QLatin1String("square")) {
        pen.setCapStyle(Qt::SquareCap);
    } else { pen.setCapStyle(Qt::RoundCap); }
    pen.setJoinStyle(cap == QLatin1String("miter") ?
                         Qt::MiterJoin : Qt::RoundJoin);
    mPainter->setPen(pen);
}

void ScriptPaintProxy::setDash(qreal dash, qreal gap, qreal offset)
{
    if (!mPainter) { return; }
    auto pen = mPainter->pen();
    QVector<qreal> pattern;
    pattern << qMax(0.1, dash) << qMax(0.1, gap);
    pen.setDashPattern(pattern);
    pen.setDashOffset(offset);
    mPainter->setPen(pen);
}

void ScriptPaintProxy::noDash()
{
    if (!mPainter) { return; }
    auto pen = mPainter->pen();
    pen.setStyle(Qt::SolidLine);
    mPainter->setPen(pen);
}

void ScriptPaintProxy::beginPath()
{
    if (!mPainter) { return; }
    mPath = QPainterPath();
    mPathStarted = false;
}

void ScriptPaintProxy::moveTo(qreal x, qreal y)
{
    if (!mPainter) { return; }
    mPath.moveTo(x, y);
    mPathStarted = true;
}

void ScriptPaintProxy::lineTo(qreal x, qreal y)
{
    if (!mPainter || !mPathStarted) { return; }
    mPath.lineTo(x, y);
}

void ScriptPaintProxy::cubicTo(qreal c1x, qreal c1y,
                               qreal c2x, qreal c2y,
                               qreal x, qreal y)
{
    if (!mPainter || !mPathStarted) { return; }
    mPath.cubicTo(c1x, c1y, c2x, c2y, x, y);
}

void ScriptPaintProxy::closePath()
{
    if (!mPainter) { return; }
    mPath.closeSubpath();
}

void ScriptPaintProxy::stroke()
{
    if (!mPainter) { return; }
    mPainter->setBrush(Qt::NoBrush);
    mPainter->drawPath(mPath);
}

void ScriptPaintProxy::fillPoly(const QList<qreal> &xy,
                                const QString &color)
{
    if (!mPainter || xy.count() < 4) { return; }
    QPolygonF poly;
    for (int i = 0; i + 1 < xy.count(); i += 2) {
        poly << QPointF(xy.at(i), xy.at(i + 1));
    }
    mPainter->setPen(Qt::NoPen);
    mPainter->setBrush(QColor(color));
    mPainter->drawPolygon(poly);
}

void ScriptPaintProxy::fillCircle(qreal x, qreal y, qreal r,
                                  const QString &color)
{
    if (!mPainter) { return; }
    mPainter->setPen(Qt::NoPen);
    mPainter->setBrush(QColor(color));
    mPainter->drawEllipse(QPointF(x, y), r, r);
}

double ScriptPaintProxy::now()
{
    return double(QDateTime::currentMSecsSinceEpoch() - mEpoch);
}

//---------------------------- ScriptPreviewWidget ----------------------------

ScriptPreviewWidget::ScriptPreviewWidget(
        Friction::Core::JsHost * const host,
        const int width, const int height,
        const bool animated,
        QWidget * const parent)
    : QWidget(parent)
    , mHost(host)
{
    setMinimumSize(width, height);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (animated) {
        mTimer = new QTimer(this);
        mTimer->setInterval(33);
        connect(mTimer, &QTimer::timeout, this,
                QOverload<>::of(&QWidget::update));
        mTimer->start();
    }
}

void ScriptPreviewWidget::paintEvent(QPaintEvent * const event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    mProxy.begin(&painter);
    mHost->invokePreviewPaint(&mProxy);
    mProxy.end();
}

void ScriptManager::loadScripts()
{
    mCommands.clear();
    mPanelScriptHosts.clear();
    // tear down previous script panels; guarded because the deletes
    // emit visibilityChanged which must not rewrite the persisted
    // list of open panels
    mUpdatingPanels = true;
    for (const auto dock : mPanels) {
        mMainWindow->removeDockWidget(dock);
        delete dock;
    }
    mPanels.clear();
    mPanelHosts.clear();
    mUpdatingPanels = false;

    // panels are only created for scripts the user actually had open;
    // every other panel-type script stays a plain menu entry until the
    // user clicks it (then it opens as a floating window)
    const auto openNames = AppSupport::getSettings(
                QStringLiteral("scripts"), QStringLiteral("openPanels"),
                QStringList()).toStringList();

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
        // chooseFile -> native open-file dialog (audio import etc.)
        host->setChooseFileHandler(
            [this](const QString &caption, const QString &filter) {
                return QFileDialog::getOpenFileName(
                            mMainWindow, caption, QString(), filter);
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
            mPanelScriptHosts.append(host);
            const auto name = panelObjectName(host->panelDesc().title);
            if (openNames.contains(name)) {
                // startup: hidden, the saved layout brings it back;
                // reload: recreate it visible right away
                createPanel(host, mScriptsReload);
            }
        }
    }
}

QString ScriptManager::panelObjectName(const QString &title)
{
    // unique objectName from the script title (stable across reloads
    // for saveState persistence) - must stay "dockScriptPanel_" so a
    // saved layout keeps restoring panels the user docked on purpose
    return QStringLiteral("dockScriptPanel_%1")
            .arg(QString::fromUtf8(title.toUtf8()
                                   .toBase64(
                                       QByteArray::Base64UrlEncoding |
                                       QByteArray::OmitTrailingEquals)));
}

void ScriptManager::scheduleSaveOpenPanels()
{
    // debounce: a layout restore flips several panels at once and every
    // visibilityChanged would hit the settings backend separately
    if (!mOpenPanelsTimer) {
        mOpenPanelsTimer = new QTimer(this);
        mOpenPanelsTimer->setSingleShot(true);
        mOpenPanelsTimer->setInterval(200);
        connect(mOpenPanelsTimer, &QTimer::timeout,
                this, [this]() { saveOpenPanels(); });
    }
    mOpenPanelsTimer->start();
}

void ScriptManager::flushOpenPanels()
{
    // called on window close so a pending debounce cannot lose the
    // final visibility state
    if (mOpenPanelsTimer && mOpenPanelsTimer->isActive()) {
        mOpenPanelsTimer->stop();
        saveOpenPanels();
    }
}

void ScriptManager::saveOpenPanels() const
{
    if (mUpdatingPanels) { return; }
    QStringList names;
    for (const auto dock : mPanels) {
        if (dock && dock->isVisible()) { names << dock->objectName(); }
    }
    AppSupport::setSettings(QStringLiteral("scripts"),
                            QStringLiteral("openPanels"), names);
}

namespace {
// QMainWindow::saveState serializes the dock objectNames as QDataStream
// strings (UTF-16BE) - searching the raw UTF-16BE bytes of a name tells
// whether a layout snapshot references that panel
bool stateContainsName(const QByteArray &state, const QString &name)
{
    QByteArray needle;
    needle.resize(name.size() * 2);
    for (int i = 0; i < name.size(); i++) {
        const ushort u = name.at(i).unicode();
        needle[2 * i] = char(u >> 8);
        needle[2 * i + 1] = char(u & 0xff);
    }
    return state.contains(needle);
}
} // namespace

void ScriptManager::ensurePanelsInState(const QByteArray &state)
{
    if (state.isEmpty()) { return; }
    for (const auto host : mPanelScriptHosts) {
        if (mPanelHosts.contains(host)) { continue; }
        if (stateContainsName(state,
                              panelObjectName(host->panelDesc().title))) {
            // hidden; the restoreState() that follows shows it at the
            // saved position (and its visibility lands in openPanels)
            createPanel(host, false);
        }
    }
}

void ScriptManager::hidePanelsNotInState(const QByteArray &state)
{
    // switching workspaces: restoreState() leaves docks that are not in
    // the new snapshot untouched - script panels from the previous
    // workspace would stay open and pollute the persisted open list
    if (state.isEmpty()) { return; }
    for (const auto dock : mPanels) {
        if (dock && dock->isVisible()
                && !stateContainsName(state, dock->objectName())) {
            dock->hide();
        }
    }
}

void ScriptManager::createPanel(Friction::Core::JsHost * const host,
                                const bool show)
{
    const auto &desc = host->panelDesc();

    const auto dock = new QDockWidget(mMainWindow);
    dock->setObjectName(panelObjectName(desc.title));
    dock->setWindowTitle(desc.title);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);

    const auto content = new QWidget(dock);
    const auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // live preview canvas (on top): paints via the script's
    // onPaint callback through a paint proxy; the internal timer
    // repaints for animations (flowing dashes)
    if (desc.preview.valid) {
        const auto preview = new ScriptPreviewWidget(
                    host, desc.preview.width, desc.preview.height,
                    desc.preview.animated, content);
        layout->addWidget(preview);
    }

    // color swatch row (on top): each button opens friction's own
    // color picker (wheel/HSV panel, follows the app theme)
    if (!desc.colors.isEmpty()) {
        const auto row = new QHBoxLayout();
        row->setSpacing(4);
        for (const auto &c : desc.colors) {
            if (!c.label.isEmpty()) {
                row->addWidget(new QLabel(c.label, content));
            }
            const auto pb = new ColorAnimatorButton(
                        QColor(c.value), content);
            pb->setFixedSize(44, 22);
            pb->setFocusPolicy(Qt::NoFocus);
            pb->setCursor(Qt::PointingHandCursor);
            pb->setToolTip(c.tooltip.isEmpty() ? c.label : c.tooltip);
            connect(pb, &ColorAnimatorButton::colorChanged, this,
                    [host, c](const QColor &col) {
                if (!col.isValid()) { return; }
                host->invokePanelValue(1, c.id, 0, col.name());
            });
            row->addWidget(pb);
        }
        row->addStretch();
        layout->addLayout(row);
    }

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
        // any value change = live preview (onChanging), coalesced by a
        // 100ms tail-merge timer (raw rates would flood the undo stack
        // and renderer); valueChanged (not sliderMoved) so groove
        // clicks, keyboard arrows and drag all preview live
        connect(slider, &QSlider::valueChanged, this,
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

        // runtime refresh: script updateCombo(id, options, index)
        // swaps the option list (e.g. re-reading scene layer names);
        // blocked so repopulating never fires onChange; context is
        // the combo itself so the connection dies with the widget
        const QString comboId = c.id;
        connect(host, &Friction::Core::JsHost::panelComboChanged, combo,
                [combo, comboId](const QString &id,
                                 const QStringList &options,
                                 const int index) {
            if (id != comboId || options.isEmpty()) { return; }
            QSignalBlocker block(combo);
            combo->clear();
            combo->addItems(options);
            combo->setCurrentIndex(qBound(0, index, options.count() - 1));
        });

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
    // created in floating state: startup-created panels stay hidden
    // until the saved window layout restores them, panels opened from
    // the Scripts menu (or a reload) display immediately
    dock->setFloating(true);
    const auto hint = content->sizeHint();
    dock->resize(qMax(320, hint.width()) + 16,
                 qMax(240, hint.height()) + 40);
    // cascade so several panels don't stack on the exact same spot
    const int cascade = mPanels.count() * 32;
    dock->move(mMainWindow->mapToGlobal(QPoint(0, 0))
               + QPoint(80 + cascade, 120 + cascade));
    // keep the persisted open-panel list in sync with the UI so the
    // next launch creates exactly the panels the user has open
    connect(dock, &QDockWidget::visibilityChanged,
            this, [this](const bool) { scheduleSaveOpenPanels(); });
    if (show) { dock->show(); dock->raise(); }
    else { dock->hide(); }
    mPanels.append(dock);
    mPanelHosts.insert(host, dock);
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
        // scripts with a panel expose their actions on the panel
        // itself; listing their commands here too showed the same
        // feature twice in the menu
        if (it.value()->panelDesc().valid) { continue; }
        const QString label = it.key();
        mScriptsMenu->addAction(label, this, [this, label]() {
            runCommand(label);
        });
    }
    // panel-type scripts (registerPanel): a panel only exists once the
    // user opens it; unopened scripts are listed here and clicking one
    // creates its panel as a floating window, created ones toggle
    // visibility (click = show/hide)
    if (!mPanelScriptHosts.isEmpty()) {
        mScriptsMenu->addSeparator();
        for (const auto host : mPanelScriptHosts) {
            const QString title = host->panelDesc().title;
            const auto dock = mPanelHosts.value(host, nullptr);
            const auto act = mScriptsMenu->addAction(
                        tr("Panel: %1").arg(title), this, [this, host]() {
                const auto d = mPanelHosts.value(host, nullptr);
                if (d) {
                    if (d->isVisible()) { d->hide(); }
                    else { d->show(); d->raise(); }
                } else {
                    // lazy creation on first open - floating window
                    createPanel(host, true);
                    // refresh the menu so this entry turns into a
                    // proper toggle bound to the new dock (deferred:
                    // the action that triggered this is still alive)
                    QTimer::singleShot(0, this, [this]() {
                        rebuildMenu();
                    });
                }
            });
            if (dock) {
                act->setCheckable(true);
                act->setChecked(dock->isVisible());
                // keep the checkmark live while the user docks/undocks
                connect(dock, &QDockWidget::visibilityChanged,
                        act, &QAction::setChecked);
            }
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
