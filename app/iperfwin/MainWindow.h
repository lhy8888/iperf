#pragma once

#include <QCloseEvent>
#include <QMainWindow>
#include <QString>

class IperfCoreBridge;
class IperfGuiConfig;
class IperfGuiEvent;
class IperfSessionRecord;
class HistoryPage;
class QuickTestPage;
class AdvancedClientPage;
class ServerPage;
class SettingsPage;
class QFrame;
class QLabel;
class QListWidget;
class QStackedWidget;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void loadWindowSettings();
    void saveWindowSettings() const;
    void updateHeaderFromConfig(const IperfGuiConfig &config);
    void updateHeaderFromState(const QString &state);
    void updateHeaderFromEvent(const IperfGuiEvent &event);
    void updateHeaderFromSession(const IperfSessionRecord &record);

    IperfCoreBridge *m_bridge = nullptr;
    QListWidget *m_navigation = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_targetLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QuickTestPage *m_quickPage = nullptr;
    AdvancedClientPage *m_advancedPage = nullptr;
    ServerPage *m_serverPage = nullptr;
    HistoryPage *m_historyPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
};
