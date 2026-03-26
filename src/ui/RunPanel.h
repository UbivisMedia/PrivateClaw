#pragma once

#include "domain/Project.h"
#include "domain/Run.h"
#include "domain/Workflow.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;

namespace privateclaw::services {
class MemoryService;
class ProjectService;
class RunService;
class SettingsService;
class WorkflowService;
}

namespace privateclaw::ui {

class RunPanel : public QWidget
{
public:
    RunPanel(
        services::SettingsService& settingsService,
        services::ProjectService& projectService,
        services::MemoryService& memoryService,
        services::WorkflowService& workflowService,
        services::RunService& runService,
        QWidget* parent = nullptr
    );

    void reloadData();

private:
    void buildUi();
    void refreshData(qint64 runIdToSelect = -1);
    void refreshProjects();
    void refreshRuns(qint64 runIdToSelect = -1);
    void loadRunFromRow(int row);
    void cleanupContaminatedArtifacts();
    int indexOfRun(qint64 runId) const;
    qint64 currentProjectId() const;
    qint64 effectiveCleanupProjectId() const;
    QString projectNameForId(qint64 projectId) const;
    QString workflowNameForId(qint64 workflowId) const;
    QString formatRunLabel(const domain::Run& run) const;
    int warningCountForRun(const domain::Run& run) const;
    void clearDetails();

    services::SettingsService& m_settingsService;
    services::ProjectService& m_projectService;
    services::MemoryService& m_memoryService;
    services::WorkflowService& m_workflowService;
    services::RunService& m_runService;

    QList<domain::Project> m_projects;
    QList<domain::Workflow> m_workflows;
    QList<domain::Run> m_runs;
    qint64 m_currentRunId = -1;

    QComboBox* m_projectCombo = nullptr;
    QLabel* m_runCountLabel = nullptr;
    QListWidget* m_runList = nullptr;
    QLabel* m_statusValueLabel = nullptr;
    QLabel* m_originValueLabel = nullptr;
    QLabel* m_projectValueLabel = nullptr;
    QLabel* m_workflowValueLabel = nullptr;
    QLabel* m_providerValueLabel = nullptr;
    QLabel* m_modelValueLabel = nullptr;
    QLabel* m_startedAtValueLabel = nullptr;
    QLabel* m_finishedAtValueLabel = nullptr;
    QLabel* m_memoryCountValueLabel = nullptr;
    QLabel* m_warningCountValueLabel = nullptr;
    QLabel* m_summaryValueLabel = nullptr;
    QLabel* m_errorValueLabel = nullptr;
    QPlainTextEdit* m_outputView = nullptr;
    QPlainTextEdit* m_logView = nullptr;
};

} // namespace privateclaw::ui
