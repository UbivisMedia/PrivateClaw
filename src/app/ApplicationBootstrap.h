#pragma once

#include <memory>

#include "providers/ProviderManager.h"
#include "services/MemoryService.h"
#include "services/ProjectService.h"
#include "services/RunService.h"
#include "services/ScheduleService.h"
#include "services/SettingsService.h"
#include "services/WorkflowService.h"
#include "storage/DatabaseManager.h"

namespace privateclaw::ui {
class MainWindow;
}

namespace privateclaw::app {

class ApplicationBootstrap
{
public:
    ApplicationBootstrap();
    ~ApplicationBootstrap();

    bool initialize(QString* errorMessage = nullptr);
    int run();
    void showStartupError(const QString& message) const;

private:
    void registerProviders();

    storage::DatabaseManager m_databaseManager;
    services::SettingsService m_settingsService;
    providers::ProviderManager m_providerManager;
    services::ProjectService m_projectService;
    services::WorkflowService m_workflowService;
    services::MemoryService m_memoryService;
    services::RunService m_runService;
    services::ScheduleService m_scheduleService;
    std::unique_ptr<ui::MainWindow> m_mainWindow;
};

} // namespace privateclaw::app
