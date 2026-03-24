#pragma once

#include "domain/Schedule.h"
#include "scheduler/SchedulerService.h"

#include <QMainWindow>
#include <QSet>

class QAction;
class QCloseEvent;
class QListWidget;
class QMenu;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;

namespace privateclaw::providers {
class ProviderManager;
}

namespace privateclaw::services {
class MemoryService;
class ProjectService;
class RunService;
class ScheduleService;
class SecretsService;
class SettingsService;
class UpdateChecker;
class WorkflowService;
}

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::ui {

class ProjectPanel;
class WorkflowPanel;
class MemoryPanel;
class RunPanel;
class SchedulePanel;
class RunLogPanel;

class MainWindow : public QMainWindow
{
protected:
    void closeEvent(QCloseEvent* event) override;

public:
    MainWindow(
        storage::DatabaseManager& databaseManager,
        services::SettingsService& settingsService,
        services::ProjectService& projectService,
        services::MemoryService& memoryService,
        services::SecretsService& secretsService,
        services::WorkflowService& workflowService,
        services::RunService& runService,
        services::ScheduleService& scheduleService,
        providers::ProviderManager& providerManager,
        QWidget* parent = nullptr
    );

    void refreshProjectDependentViews();

private:
    void buildUi();
    void buildSystemTray();
    void recoverInterruptedRuns();
    void hideToBackground();
    void requestApplicationQuit();
    void startUpdateChecks();
    void updateStatusBar();
    void startSchedulePolling();
    void pollDueSchedules();
    void triggerUpdateCheck(bool manual);
    void handleUpdateCheckFinished(
        bool manual,
        bool success,
        bool updateAvailable,
        const QString& currentVersion,
        const QString& latestVersion,
        const QString& releaseUrl,
        const QString& message
    );
    void updateTrayPresentation();
    void openLatestReleasePage();
    void showOrRaiseWindow();
    void executeSchedule(
        const domain::Schedule& schedule,
        bool advanceScheduleAfterRun,
        const QString& originLabel
    );

    storage::DatabaseManager& m_databaseManager;
    services::SettingsService& m_settingsService;
    services::ProjectService& m_projectService;
    services::MemoryService& m_memoryService;
    services::SecretsService& m_secretsService;
    services::WorkflowService& m_workflowService;
    services::RunService& m_runService;
    services::ScheduleService& m_scheduleService;
    providers::ProviderManager& m_providerManager;
    scheduler::SchedulerService m_schedulerService;

    QListWidget* m_navigation = nullptr;
    QStackedWidget* m_pages = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    WorkflowPanel* m_workflowPanel = nullptr;
    MemoryPanel* m_memoryPanel = nullptr;
    RunPanel* m_runPanel = nullptr;
    SchedulePanel* m_schedulePanel = nullptr;
    RunLogPanel* m_runLogPanel = nullptr;
    services::UpdateChecker* m_updateChecker = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_hideToTrayAction = nullptr;
    QAction* m_openWindowAction = nullptr;
    QAction* m_checkUpdatesAction = nullptr;
    QAction* m_openReleasePageAction = nullptr;
    QAction* m_quitAction = nullptr;
    QTimer* m_schedulePollTimer = nullptr;
    QTimer* m_updateCheckTimer = nullptr;
    QTimer* m_trayBlinkTimer = nullptr;
    QSet<qint64> m_runningScheduleIds;
    bool m_updateAvailable = false;
    bool m_trayBlinkHighlighted = false;
    bool m_updateNotificationShown = false;
    bool m_forceQuitRequested = false;
    QString m_latestReleaseVersion;
    QString m_latestReleaseUrl;
};

} // namespace privateclaw::ui
