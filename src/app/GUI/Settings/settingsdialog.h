#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QRect>
#include <QSize>

class SettingsWidget;
class QShowEvent;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget * const parent = nullptr);
    ~SettingsDialog();

    void setCurrentIndex(int index);
    int count() const;

protected:
    void showEvent(QShowEvent* const event) override;
    bool event(QEvent* const event) override;

private:
    void addSettingsWidget(SettingsWidget* const widget,
                           const QString& name);
    void updateSettings(bool restore = false);

    // The panels are embedded in scroll areas, which caps the size hint
    // they report: this is the size the panels would like to have.
    QSize panelsSizeHint() const;

    // Keeps the dialog inside the screen. Every settings panel is
    // scrollable, so the window itself never has to be taller/wider
    // than the display - otherwise the buttons at the bottom end up
    // below the screen edge on smaller displays.
    // When @a preferred is valid it is used as target size, otherwise
    // the current size (which may come from the restored geometry).
    void fitToScreen(const QSize& preferred = QSize());
    QRect availableScreenRect() const;

    QTabWidget* mTabWidget = nullptr;
    QList<SettingsWidget*> mSettingWidgets;
    bool mUsePanelsSizeOnShow = false;
};

#endif // SETTINGSDIALOG_H
