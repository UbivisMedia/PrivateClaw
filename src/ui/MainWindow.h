#pragma once

#include <QMainWindow>

class QListWidget;
class QStackedWidget;

namespace privateclaw::providers {
class ProviderManager;
}

namespace privateclaw::services {
class SettingsService;
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
        providers::ProviderManager& providerManager,
        QWidget* parent = nullptr
    );

private:
    void buildUi();
    void updateStatusBar();

    storage::DatabaseManager& m_databaseManager;
    services::SettingsService& m_settingsService;
    providers::ProviderManager& m_providerManager;

    QListWidget* m_navigation = nullptr;
    QStackedWidget* m_pages = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    WorkflowPanel* m_workflowPanel = nullptr;
    MemoryPanel* m_memoryPanel = nullptr;
    SchedulePanel* m_schedulePanel = nullptr;
    RunLogPanel* m_runLogPanel = nullptr;
};

} // namespace privateclaw::ui

