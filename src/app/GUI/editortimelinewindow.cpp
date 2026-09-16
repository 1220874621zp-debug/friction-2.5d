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
    QToolBar *tb = addToolBar(QStringLiteral("main"));
    tb->setMovable(false);
    tb->setStyleSheet(QStringLiteral(
        "QToolBar{background:#1d1d1d;border-bottom:1px solid #2c2c2c;spacing:6px;padding:4px;}"
        "QToolButton{color:#c8c8c8;background:transparent;border:1px solid transparent;"
        "border-radius:4px;padding:4px 10px;}"
        "QToolButton:hover{background:#2b2b2b;border-color:#3a3a3a;}"
        "QToolButton:pressed{background:#08A581;color:#fff;}"));

    QAction *aAddV = tb->addAction(QStringLiteral("+ 视频"));
    QAction *aAddA = tb->addAction(QStringLiteral("+ 音频"));
    QAction *aDel  = tb->addAction(QStringLiteral("删除"));
    tb->addSeparator();
    // CapCut-style magnetic mode: no gaps within a track (turning it on
    // compacts immediately; drag/trim releases keep it compact)
    QAction *aMag = tb->addAction(QStringLiteral("磁吸"));
    aMag->setCheckable(true);
    aMag->setChecked(false);
    tb->addSeparator();
    QAction *aZi = tb->addAction(QStringLiteral("放大"));
    QAction *aZo = tb->addAction(QStringLiteral("缩小"));
    QAction *aFit = tb->addAction(QStringLiteral("适配"));
    tb->addSeparator();
    QAction *aLog = tb->addAction(QStringLiteral("调试日志"));

    connect(aAddV, &QAction::triggered, m_timeline, &EditorTimelineWidget::addVideoClip);
    connect(aAddA, &QAction::triggered, m_timeline, &EditorTimelineWidget::addAudioClip);
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
