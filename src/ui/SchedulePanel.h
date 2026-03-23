#pragma once

#include "domain/Project.h"
#include "domain/Schedule.h"
#include "domain/Workflow.h"

#include <QList>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QTimeEdit;

namespace privateclaw::scheduler {
class SchedulerService;
}

namespace privateclaw::services {
class ProjectService;
class ScheduleService;
class WorkflowService;
}

namespace privateclaw::ui {

class SchedulePanel : public QWidget
{
public:
    SchedulePanel(
        services::ProjectService& projectService,
        services::WorkflowService& workflowService,
        services::ScheduleService& scheduleService,
        scheduler::SchedulerService& schedulerService,
        QWidget* parent = nullptr
    );

    void reloadData();
    void setOnScheduleDataChanged(std::function<void()> callback);
    void setOnRunScheduleRequested(std::function<void(const domain::Schedule&)> callback);

private:
    void buildUi();
    void refreshData(qint64 scheduleIdToSelect = -1);
    void refreshProjects(qint64 selectedProjectId = -1);
    void refreshWorkflows(qint64 projectId, qint64 workflowIdToSelect = -1);
    void refreshScheduleList(qint64 scheduleIdToSelect = -1);
    void loadScheduleFromRow(int row);
    void saveSchedule();
    void deleteSchedule();
    void requestRunNow();
    void resetEditor(bool keepFeedback = false);
    void updateTriggerEditor();
    void updatePreview();
    qint64 currentProjectId() const;
    qint64 currentWorkflowId() const;
    int indexOfProject(qint64 projectId) const;
    int indexOfWorkflow(qint64 workflowId) const;
    QString projectNameForId(qint64 projectId) const;
    QString workflowNameForId(qint64 workflowId) const;
    QString formatScheduleLabel(const domain::Schedule& schedule) const;
    const domain::Schedule* currentSchedule() const;

    services::ProjectService& m_projectService;
    services::WorkflowService& m_workflowService;
    services::ScheduleService& m_scheduleService;
    scheduler::SchedulerService& m_schedulerService;

    QList<domain::Project> m_projects;
    QList<domain::Workflow> m_workflows;
    QList<domain::Schedule> m_schedules;
    qint64 m_currentScheduleId = -1;
    std::function<void()> m_onScheduleDataChanged;
    std::function<void(const domain::Schedule&)> m_onRunScheduleRequested;

    QListWidget* m_scheduleList = nullptr;
    QLabel* m_scheduleCountLabel = nullptr;
    QComboBox* m_projectCombo = nullptr;
    QComboBox* m_workflowCombo = nullptr;
    QCheckBox* m_enabledCheckBox = nullptr;
    QComboBox* m_triggerTypeCombo = nullptr;
    QStackedWidget* m_triggerConfigStack = nullptr;
    QDateTimeEdit* m_onceDateTimeEdit = nullptr;
    QSpinBox* m_intervalMinutesSpin = nullptr;
    QTimeEdit* m_dailyTimeEdit = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_lastRunLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
};

} // namespace privateclaw::ui
