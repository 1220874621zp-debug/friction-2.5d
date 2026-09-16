#ifndef EDITORTIMELINEWINDOW_H
#define EDITORTIMELINEWINDOW_H

#include <QMainWindow>

class EditorTimelineWidget;
class QPlainTextEdit;
class QLabel;
class QDialog;

class EditorTimelineWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit EditorTimelineWindow(QWidget *parent = nullptr);

private slots:
    void showDebugLog();

private:
    EditorTimelineWidget *m_timeline;
    QPlainTextEdit *m_logView;   // owned by log dialog
    QDialog *m_logDlg = nullptr;
    QLabel *m_selLabel;
};

#endif // EDITORTIMELINEWINDOW_H
