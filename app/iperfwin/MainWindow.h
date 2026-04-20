#pragma once

#include <QCloseEvent>
#include <QMainWindow>

class IperfCoreBridge;
class HistoryPage;
class SettingsPage;
class TestPage;
class QLabel;
class QListWidget;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void loadWindowSettings();
    void saveWindowSettings() const;
    void onRunningChanged(bool running);

    IperfCoreBridge *m_bridge      = nullptr;
    QListWidget     *m_navigation  = nullptr;
    QStackedWidget  *m_stack       = nullptr;
    QLabel          *m_stateLabel  = nullptr;

    TestPage     *m_testPage     = nullptr;
    HistoryPage  *m_historyPage  = nullptr;
    SettingsPage *m_settingsPage = nullptr;
};
