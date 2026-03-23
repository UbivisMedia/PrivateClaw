#pragma once

#include "domain/Schedule.h"
#include "scheduler/SchedulerService.h"

#include <QMainWindow>
#include <QSet>

class QListWidget;
class QStackedWidget;
class QTimer;

namespace privateclaw::providers {
class ProviderManager;
}

namespace privateclaw::services {
class MemoryService;
class ProjectService;
class ScheduleService;
class SettingsService;
class WorkflowService;
}

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::ui {

class ProjectPanel;
class WorkflowPanel;
class MemoryPanel;
class SchedulePanel;
class RunLogPanel;

class MainWindow : public QMainWindow
{
public:
    MainWindow(
        storage::DatabaseManager& databaseManager,
        services::SettingsService& settingsService,
        services::ProjectService& projectService,
        services::MemoryService& memoryService,
        services::WorkflowService& workflowService,
        services::ScheduleService& scheduleService,
        providers::ProviderManager& providerManager,
        QWidget* parent = nullptr
    );

    void refreshProjectDependentViews();

private:
    void buildUi();
    void updateStatusBar();
    void startSchedulePolling();
    void pollDueSchedules();
    void executeSchedule(
        const domain::Schedule& schedule,
        bool advanceScheduleAfterRun,
        const QString& originLabel
    );

    storage::DatabaseManager& m_databaseManager;
    services::SettingsService& m_settingsService;
    services::ProjectService& m_projectService;
    services::MemoryService& m_memoryService;
    services::WorkflowService& m_workflowService;
    services::ScheduleService& m_scheduleService;
    providers::ProviderManager& m_providerManager;
    scheduler::SchedulerService m_schedulerService;

    QListWidget* m_navigation = nullptr;
    QStackedWidget* m_pages = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    WorkflowPanel* m_workflowPanel = nullptr;
    MemoryPanel* m_memoryPanel = nullptr;
    SchedulePanel* m_schedulePanel = nullptr;
    RunLogPanel* m_runLogPanel = nullptr;
    QTimer* m_schedulePollTimer = nullptr;
    QSet<qint64> m_runningScheduleIds;
};

} // namespace privateclaw::ui
