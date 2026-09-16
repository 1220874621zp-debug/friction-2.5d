#include "editortimelinewindow.h"
#include "editortimelinewidget.h"

#include <QToolBar>
#include <QScrollBar>
#include <QStatusBar>
#include <QLabel>
#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QClipboard>
#include <QGuiApplication>
#include <QAction>
#include <QApplication>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QPainter>
#include <QShowEvent>

#include "themesupport.h"

// user-supplied magnet glyph; the fill is swapped per state
static const char* kMagneticSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
        "<path fill=\"%1\" d=\"M827.968 145.792a361.088 361.088 0 0 1 61.632 493.44"
        "l-11.328 14.72L648.32 934.4l-4.8 5.312a70.72 70.72 0 0 1-94.592 4.48"
        "l-134.016-109.888-6.4-6.4a44.992 44.992 0 0 1-4.864-49.216l5.056-7.488"
        " 246.08-300.288 4.416-5.952a72.32 72.32 0 0 0-14.464-95.744L638.848 364.8"
        "a72.32 72.32 0 0 0-90.752 8.96l-4.992 5.504-246.08 300.288a44.928 44.928"
        " 0 0 1-63.232 6.272L99.84 575.936a70.592 70.592 0 0 1-14.208-93.44"
        "l4.224-5.888 229.888-280.384 12.16-14.08a361.088 361.088 0 0 1 481.344"
        "-47.68l14.72 11.328zM218.56 420.544l-79.168 96.64a6.592 6.592 0 0 0"
        " 0.96 9.28l119.296 97.728 84.48-103.104-125.568-100.48z m610.176 192.832"
        "A297.088 297.088 0 0 0 369.28 236.8L259.2 371.072l125.632 100.48"
        " 108.8-132.864a136.32 136.32 0 0 1 210.816 172.8l-94.08 114.816"
        " 125.504 100.48 92.928-113.408z m-259.136 62.528l-99.2 121.024"
        " 119.232 97.792c2.816 2.24 6.912 1.92 9.216-0.896l96.32-117.504"
        "L569.6 675.84z\"/></svg>";

// rasterize at the device pixel ratio (physical-px painter, tagged dpr,
// same pattern as timelinedockwidget's svgToolbarPixmap)
static QPixmap magneticPixmap(const QColor &color, const int base = 24)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.;
    QPixmap pm(QSize(base, base) * dpr);
    pm.fill(Qt::transparent);
    const QString svg = QString(kMagneticSvg).arg(color.name());
    QSvgRenderer renderer(svg.toUtf8());
    if (renderer.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p, QRectF(0, 0, base * dpr, base * dpr));
        p.end();
    }
    pm.setDevicePixelRatio(dpr);
    return pm;
}

EditorTimelineWindow::EditorTimelineWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("TimelineDemo - Qt6 剪辑时间轴"));
    // hosting: demo main.cpp sets Fusion + palette app-wide; friction
    // owns those globally, so scope both to this embedded window only
    setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    resize(1100, 420);

    // ---- central: timeline + horizontal scrollbar ----
    QWidget *central = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_timeline = new EditorTimelineWidget(central);
    QScrollBar *hbar = new QScrollBar(Qt::Horizontal, central);
    hbar->setFixedHeight(12);
    m_timeline->setScrollBar(hbar);

    lay->addWidget(m_timeline, 1);
    lay->addWidget(hbar, 0);
    setCentralWidget(central);

    // ---- toolbar ----
    mToolBar = addToolBar(QStringLiteral("main"));
    mToolBar->setMovable(false);
    refreshThemeColors();

    // "+ video/audio" from the demo stay out: they add fake clips that
    // the next rebuild discards - the real panel mirrors scene layers.
    // Keeping the bar short also matters: a narrow bottom dock would
    // push later actions into the toolbar overflow menu and hide them
    QAction *aDel  = mToolBar->addAction(QStringLiteral("删除"));
    // CapCut-style magnetic mode: no gaps within a track (turning it on
    // compacts immediately; drag/trim releases keep it compact).
    // Icon-only with a checked highlight: dim glyph when off, white
    // glyph on the theme accent when on (colors refreshed on show)
    mMagOff = QIcon(magneticPixmap(QColor(0xc8, 0xc8, 0xc8)));
    mMagOn = QIcon(magneticPixmap(QColor(0xff, 0xff, 0xff)));
    QAction *aMag = mToolBar->addAction(mMagOff, QString());
    aMag->setCheckable(true);
    aMag->setChecked(false);
    aMag->setToolTip(QStringLiteral("磁吸：开启后同轨块贴紧无间隙"));
    mMagAction = aMag;
    connect(aMag, &QAction::toggled, this, [this](const bool on) {
        if (mMagAction) { mMagAction->setIcon(on ? mMagOn : mMagOff); }
    });
    mToolBar->addSeparator();
    QAction *aZi = mToolBar->addAction(QStringLiteral("放大"));
    QAction *aZo = mToolBar->addAction(QStringLiteral("缩小"));
    QAction *aFit = mToolBar->addAction(QStringLiteral("适配"));
    mToolBar->addSeparator();
    QAction *aLog = mToolBar->addAction(QStringLiteral("调试日志"));

    connect(aDel,  &QAction::triggered, m_timeline, &EditorTimelineWidget::removeSelectedClip);
    connect(aMag, &QAction::toggled, m_timeline, &EditorTimelineWidget::setMagnetic);
    connect(aZi,   &QAction::triggered, m_timeline, &EditorTimelineWidget::zoomIn);
    connect(aZo,   &QAction::triggered, m_timeline, &EditorTimelineWidget::zoomOut);
    connect(aFit,  &QAction::triggered, m_timeline, &EditorTimelineWidget::zoomFit);
    connect(aLog,  &QAction::triggered, this, &EditorTimelineWindow::showDebugLog);

    // ---- status bar ----
    m_selLabel = new QLabel(QStringLiteral("未选中素材"), this);
    statusBar()->addWidget(m_selLabel, 1);
    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar{background:#1d1d1d;color:#888;border-top:1px solid #2c2c2c;}"
        "QStatusBar::item{border:none;}"));

    // ---- log dialog (created lazily, view kept for appending) ----
    m_logDlg = new QDialog(this);
    m_logDlg->setWindowTitle(QStringLiteral("调试日志"));
    m_logDlg->resize(560, 300);
    QVBoxLayout *dlay = new QVBoxLayout(m_logDlg);
    m_logView = new QPlainTextEdit(m_logDlg);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit{background:#161616;color:#b8ffb0;border:1px solid #2c2c2c;"
        "font-family:Consolas,monospace;font-size:12px;}"));
    QPushButton *copyBtn = new QPushButton(QStringLiteral("复制全部"), m_logDlg);
    QHBoxLayout *btnLay = new QHBoxLayout;
    btnLay->addStretch(1);
    btnLay->addWidget(copyBtn);
    dlay->addWidget(m_logView, 1);
    dlay->addLayout(btnLay, 0);
    connect(copyBtn, &QPushButton::clicked, this, [this]{
        QGuiApplication::clipboard()->setText(m_logView->toPlainText());
        m_logView->appendPlainText(QStringLiteral("[log] copied to clipboard"));
    });

    connect(m_timeline, &EditorTimelineWidget::logMessage,
            m_logView, &QPlainTextEdit::appendPlainText);
    connect(m_timeline, &EditorTimelineWidget::selectionChanged, this, [this](const QString &info){
        m_selLabel->setText(info.isEmpty() ? QStringLiteral("未选中素材")
                                           : QStringLiteral("选中: ") + info);
    });

    // dark palette for the whole app
    QPalette pal = qApp->palette();
    pal.setColor(QPalette::Window, QColor(0x1b, 0x1b, 0x1b));
    pal.setColor(QPalette::WindowText, QColor(0xc8, 0xc8, 0xc8));
    pal.setColor(QPalette::Base, QColor(0x16, 0x16, 0x16));
    pal.setColor(QPalette::Text, QColor(0xc8, 0xc8, 0xc8));
    pal.setColor(QPalette::Button, QColor(0x24, 0x24, 0x26));
    pal.setColor(QPalette::ButtonText, QColor(0xc8, 0xc8, 0xc8));
    setPalette(pal);
}

void EditorTimelineWindow::showDebugLog()
{
    m_logDlg->show();
    m_logDlg->raise();
    m_logDlg->activateWindow();
}

void EditorTimelineWindow::refreshThemeColors()
{
    // accent from the app theme (follows the accent preset / custom
    // color); press/checked highlights and the magnetic icons re-tint
    const QColor accent = ThemeSupport::getThemeHighlightColor();
    if (mToolBar) {
        mToolBar->setStyleSheet(QStringLiteral(
            "QToolBar{background:#1d1d1d;border-bottom:1px solid #2c2c2c;spacing:6px;padding:4px;}"
            "QToolButton{color:#c8c8c8;background:transparent;border:1px solid transparent;"
            "border-radius:4px;padding:4px 10px;}"
            "QToolButton:hover{background:#2b2b2b;border-color:#3a3a3a;}"
            "QToolButton:pressed{background:%1;color:#fff;}"
            "QToolButton:checked{background:%1;border:1px solid %1;color:#fff;}")
                .arg(accent.name()));
    }
    // the "on" glyph sits on the accent background, so tint it with the
    // accent itself only when the accent is dark enough for white text
    // (the default); otherwise keep white on a darkened accent
    QColor onGlyph(0xff, 0xff, 0xff);
    if (accent.lightness() > 150) {
        onGlyph = ThemeSupport::getThemeHighlightDarkerColor().darker(160);
    }
    mMagOn = QIcon(magneticPixmap(onGlyph));
    if (mMagAction && mMagAction->isChecked()) {
        mMagAction->setIcon(mMagOn);
    }
}

void EditorTimelineWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // the theme may have changed while the dock was hidden; every show
    // re-pulls the accent colors
    refreshThemeColors();
}
