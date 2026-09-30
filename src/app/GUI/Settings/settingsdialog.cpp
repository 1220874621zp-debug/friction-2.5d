#include "settingsdialog.h"
#include "Private/esettings.h"
#include "hardwareinfo.h"
#include "exceptions.h"
#include "GUI/global.h"

#include "appsupport.h"

#include "generalsettingswidget.h"
#include "themesettingswidget.h"
#include "timelinesettingswidget.h"
#include "shortcutsettingswidget.h"

#ifndef USE_GLES
#include "pluginssettingswidget.h"
#endif

#include "widgets/performancesettingswidget.h"
#include "widgets/canvassettingswidget.h"
#include "widgets/presetsettingswidget.h"
#include "aiagentsettingswidget.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QStatusBar>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QStyle>
#include <QGuiApplication>
#include <QScreen>

namespace {
// distance kept between the dialog frame and the screen edges
const int kScreenEdgeMargin = 12;
// the dialog may be shrunk down to this - the panels scroll from there
const int kMinDialogWidth = 640;
const int kMinDialogHeight = 420;
// sane fallback when the screen metrics are not available (or bogus)
const QSize kFallbackScreenSize(1024, 768);

int styleFrameWidth(const QWidget* const widget)
{
    return widget->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,
                                        nullptr,
                                        widget);
}
} // namespace

SettingsDialog::SettingsDialog(QWidget * const parent)
    : QDialog(parent)
{

    setWindowTitle(tr("Preferences"));

    const auto mainLayout = new QVBoxLayout;
    setLayout(mainLayout);

    mTabWidget = new QTabWidget(this);

    const auto general = new GeneralSettingsWidget(this);
    addSettingsWidget(general, tr("General"));

    const auto theme = new ThemeSettingsWidget(this);
    addSettingsWidget(theme, tr("Theme & Appearance"));

    const auto performance = new PerformanceSettingsWidget(this);
    addSettingsWidget(performance,tr("Hardware"));

    const auto canvas = new CanvasSettingsWidget(this);
    addSettingsWidget(canvas, tr("Canvas"));

    const auto timeline = new TimelineSettingsWidget(this);
    addSettingsWidget(timeline, tr("Timeline"));

    const auto shortcuts = new ShortcutSettingsWidget(this);
    addSettingsWidget(shortcuts, tr("Shortcuts"));

#ifndef USE_GLES
    const auto plugins = new PluginsSettingsWidget(this);
    addSettingsWidget(plugins, tr("Shaders"));
#endif

    const auto presets = new PresetSettingsWidget(this);
    addSettingsWidget(presets, tr("Presets"));

    const auto aiAgent = new AIAgentSettingsWidget(this);
    addSettingsWidget(aiAgent, tr("AI & MCP Agent"));

    mainLayout->addWidget(mTabWidget);

    const auto buttonsLayout = new QHBoxLayout;

    const auto restoreButton = new QPushButton(QIcon::fromTheme("loop_back"),
                                               tr("Restore Defaults"),
                                               this);
    restoreButton->setFocusPolicy(Qt::NoFocus);

    const auto cancelButton = new QPushButton(QIcon::fromTheme("dialog-cancel"),
                                              tr("Close"),
                                              this);
    cancelButton->setFocusPolicy(Qt::NoFocus);

    const auto applyButton = new QPushButton(QIcon::fromTheme("disk_drive"),
                                             tr("Save"),
                                             this);
    applyButton->setFocusPolicy(Qt::NoFocus);

    buttonsLayout->addWidget(restoreButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(applyButton);
    buttonsLayout->addWidget(cancelButton);

    eSizesUI::widget.add(restoreButton, [restoreButton,
                                         applyButton,
                                         cancelButton](const int size) {
        Q_UNUSED(size)
        restoreButton->setFixedHeight(eSizesUI::button);
        cancelButton->setFixedHeight(eSizesUI::button);
        applyButton->setFixedHeight(eSizesUI::button);
    });

    mainLayout->addLayout(buttonsLayout);
    const auto statusBar = new QStatusBar(this);
    statusBar->setSizeGripEnabled(false);
    mainLayout->addWidget(statusBar);

    connect(restoreButton, &QPushButton::released, this, [this]() {
        eSettings::sInstance->loadDefaults();
        updateSettings(true /* restore */);
        ThemeSupport::applyThemeLive();
    });

    connect(cancelButton, &QPushButton::released, this, &QDialog::close);

    connect(applyButton, &QPushButton::released,
            this, [this, statusBar]() {
        for (const auto widget : mSettingWidgets) {
            widget->applySettings();
        }
        emit eSettings::sInstance->settingsChanged();
        try {
            eSettings& sett = *eSettings::sInstance;
            sett.saveToFile();
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
        ThemeSupport::applyThemeLive();
        statusBar->showMessage(tr("Settings saved successfully"), 2000);
    });

    updateSettings();
    // restore the last size when there is one, otherwise the panels get
    // the size they ask for once the dialog is shown (the widget hints
    // are still settling here) - either way the result is clamped to
    // the screen so the buttons at the bottom stay reachable
    mUsePanelsSizeOnShow = !restoreGeometry(
                AppSupport::getSettings("ui",
                                        "SettingsDialogGeometry").toByteArray());
    fitToScreen();
}

SettingsDialog::~SettingsDialog()
{
    AppSupport::setSettings("ui",
                            "SettingsDialogGeometry",
                            saveGeometry());
}

void SettingsDialog::showEvent(QShowEvent * const event)
{
    QDialog::showEvent(event);
    // the widget hints are final here, so a dialog without a stored
    // geometry can take the size the panels ask for - and both cases
    // are clamped to the screen
    const QSize preferred = mUsePanelsSizeOnShow ? panelsSizeHint() : QSize();
    mUsePanelsSizeOnShow = false;
    fitToScreen(preferred);
    // the frame title bar is not necessarily known at this point (the
    // window manager reports it asynchronously), so verify once more
    // when the window is up
    QTimer::singleShot(0, this, [this]() { fitToScreen(); });
}

bool SettingsDialog::event(QEvent * const event)
{
    const bool result = QDialog::event(event);
    // the maximum size depends on the screen the dialog lives on
    if (event->type() == QEvent::ScreenChangeInternal) { fitToScreen(); }
    return result;
}

QRect SettingsDialog::availableScreenRect() const
{
    const auto dialogScreen = screen();
    const auto target = dialogScreen ? dialogScreen
                                     : QGuiApplication::primaryScreen();
    if (!target) { return QRect(QPoint(0, 0), kFallbackScreenSize); }
    const QRect avail = target->availableGeometry();
    if (avail.width() < kMinDialogWidth || avail.height() < kMinDialogHeight) {
        // bogus metrics (screen not resolved yet), use the primary one
        const auto primary = QGuiApplication::primaryScreen();
        if (primary && primary != target) {
            const QRect primaryAvail = primary->availableGeometry();
            if (primaryAvail.width() >= kMinDialogWidth &&
                primaryAvail.height() >= kMinDialogHeight) {
                return primaryAvail;
            }
        }
        if (avail.width() <= 0 || avail.height() <= 0) {
            return QRect(QPoint(0, 0), kFallbackScreenSize);
        }
    }
    return avail;
}

QSize SettingsDialog::panelsSizeHint() const
{
    // the scroll areas around the panels report a capped size hint, so
    // the difference to the real panel hint is added back here
    if (!mTabWidget) { return sizeHint(); }
    QSize panels;
    for (int i = 0; i < mTabWidget->count(); ++i) {
        const auto area = qobject_cast<QScrollArea*>(mTabWidget->widget(i));
        if (area && area->widget()) {
            panels = panels.expandedTo(area->widget()->sizeHint());
        }
    }
    const QSize cappedHint = mTabWidget->sizeHint();
    return sizeHint() + QSize(qMax(0, panels.width() - cappedHint.width()),
                             qMax(0, panels.height() - cappedHint.height()));
}

void SettingsDialog::fitToScreen(const QSize& preferred)
{
    const QRect avail = availableScreenRect();

    // the frame (title bar + borders) is not part of the client area,
    // and it is only known once the window has been created
    int frameW = frameGeometry().width() - width();
    int frameH = frameGeometry().height() - height();
    int titleH = geometry().top() - frameGeometry().top();
    if (frameH <= 0 || titleH <= 0) {
        const int border = styleFrameWidth(this);
        titleH = style()->pixelMetric(QStyle::PM_TitleBarHeight, nullptr, this);
        if (titleH <= 0) { titleH = 24; }
        frameW = qMax(frameW, 2 * border + 8);
        frameH = titleH + 2 * border;
    } else {
        frameW = qMax(0, frameW);
    }

    const QSize maxClient(
        qMax(kMinDialogWidth, avail.width() - frameW - 2 * kScreenEdgeMargin),
        qMax(kMinDialogHeight, avail.height() - frameH - 2 * kScreenEdgeMargin));

    // the panels are scrollable, so the dialog never needs more room
    // than the screen can offer
    setMaximumSize(maxClient);
    const QSize minClient(qMin(kMinDialogWidth, maxClient.width()),
                          qMin(kMinDialogHeight, maxClient.height()));
    setMinimumSize(minClient);

    QSize target = (preferred.isValid() && !preferred.isEmpty())
                       ? preferred : size();
    target = target.boundedTo(maxClient).expandedTo(minClient);

    // keep the whole frame inside the usable area: the title bar sits
    // above the client rect, so clamping the client rect alone could
    // push the title bar off the top edge
    const QRect usable(avail.left() + kScreenEdgeMargin,
                       avail.top() + kScreenEdgeMargin,
                       qMax(0, avail.width() - 2 * kScreenEdgeMargin),
                       qMax(0, avail.height() - 2 * kScreenEdgeMargin));
    const int clientW = target.width();
    const int clientH = target.height();
    const int frameX = pos().x();
    const int frameY = pos().y() - titleH;
    const int x = qBound(usable.left(), frameX,
                         qMax(usable.left(), usable.right() - clientW - frameW + 1));
    const int y = qBound(usable.top(), frameY,
                         qMax(usable.top(), usable.bottom() - clientH - frameH + 1));

    const QRect g(x, y + titleH, clientW, clientH);
    if (geometry() != g) { setGeometry(g); }
}

void SettingsDialog::addSettingsWidget(SettingsWidget * const widget,
                                       const QString &name)
{
    // every panel is embedded in a scroll area: the panels are taller
    // than the display of smaller laptops, and without the scroll areas
    // the dialog could not be shrunk below the tallest panel - which
    // pushed the buttons at the bottom below the screen edge
    const auto area = new QScrollArea(mTabWidget);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setContentsMargins(0, 0, 0, 0);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // let the tab pane background show through
    area->viewport()->setAutoFillBackground(false);
    area->setWidget(widget);

    mTabWidget->addTab(area, name);
    mSettingWidgets << widget;
}

void SettingsDialog::setCurrentIndex(int index)
{
    if (mTabWidget && index >= 0 && index < mTabWidget->count()) {
        mTabWidget->setCurrentIndex(index);
    }
}

int SettingsDialog::count() const
{
    return mTabWidget ? mTabWidget->count() : 0;
}

void SettingsDialog::updateSettings(bool restore)
{
    for (const auto widget : mSettingWidgets) {
        widget->updateSettings(restore);
    }
}
