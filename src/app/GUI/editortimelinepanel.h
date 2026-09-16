#ifndef EDITORTIMELINEPANEL_H
#define EDITORTIMELINEPANEL_H

#include <QWidget>

class EditorTimelineWidget;
class QPlainTextEdit;
class QLabel;
class QDialog;

// NLE-style edit timeline panel shell: 1:1 port of the user's
// TimelineDemo main window (toolbar, timeline + scrollbar, status
// label, debug log dialog). Pure UI - no engine wiring yet.
class EditorTimelinePanel : public QWidget
{
    Q_OBJECT
public:
    explicit EditorTimelinePanel(QWidget *parent = nullptr);

private slots:
    void showDebugLog();

private:
    EditorTimelineWidget *m_timeline;
    QPlainTextEdit *m_logView;   // owned by log dialog
    QDialog *m_logDlg = nullptr;
    QLabel *m_selLabel;
};

#endif // EDITORTIMELINEPANEL_H
