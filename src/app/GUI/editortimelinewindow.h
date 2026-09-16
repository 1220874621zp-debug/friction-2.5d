#ifndef EDITORTIMELINEWINDOW_H
#define EDITORTIMELINEWINDOW_H

#include <QMainWindow>
#include <QIcon>

class EditorTimelineWidget;
class QLabel;
class QAction;
class QToolBar;

class EditorTimelineWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit EditorTimelineWindow(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // re-pull accent colors from the app theme: toolbar QSS highlight
    // and the magnetic toggle icons follow theme changes on every show
    void refreshThemeColors();

    EditorTimelineWidget *m_timeline;
    QLabel *m_selLabel;
    QAction *mMagAction = nullptr;
    QIcon mMagOn;
    QIcon mMagOff;
    QToolBar *mToolBar = nullptr;
};

#endif // EDITORTIMELINEWINDOW_H
