#pragma once

#include "core/WorkflowEngine.h"
#include "domain/Project.h"
#include "domain/Workflow.h"

#include <QJsonObject>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QFrame;
class QFutureWatcherBase;
class QLabel;
class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;
class QTextEdit;
class QTimer;

namespace privateclaw::providers {
class ProviderManager;
}

namespace privateclaw::services {
class MemoryService;
class ProjectService;
class SettingsService;
class WorkflowService;
}

namespace privateclaw::ui {

class WorkflowPanel : public QWidget
{
public:
    WorkflowPanel(
        services::ProjectService& projectService,
        services::SettingsService& settingsService,
        services::MemoryService& memoryService,
        services::WorkflowService& workflowService,
        providers::ProviderManager& providerManager,
        QWidget* parent = nullptr
    );

    int workflowCount() const;
    void reloadData();
    void setOnWorkflowDataChanged(std::function<void()> callback);
    void setOnExecutionLogChanged(std::function<void(const QString&)> callback);

private:
    void buildUi();
    void refreshData(qint64 workflowIdToSelect = -1);
    void refreshProjects(qint64 selectedProjectId = -1);
    void refreshWorkflowList(qint64 workflowIdToSelect = -1);
    void loadWorkflowFromRow(int row);
    void saveWorkflow();
    void executeWorkflow();
    void updateExecutionStatus();
    void updateVisualEditorVisibility();
    void scheduleVisualSyncFromJson();
    void syncVisualEditorFromJson();
    void rebuildVisualStepList(const QString& stepIdToSelect = QString());
    void loadVisualStepFromRow(int row);
    void clearVisualStepEditor();
    void updateVisualConfigPage();
    void scheduleVisualStepApply();
    void applyVisualStepChanges();
    void addVisualStep(const QString& stepType);
    void removeSelectedVisualStep();
    void moveSelectedVisualStep(int offset);
    QString selectedVisualStepId() const;
    QString generateVisualStepId(const QString& stepType) const;
    QString visualStepLabel(const QJsonObject& stepObject) const;
    void resetEditor(bool keepFeedback = false);
    int indexOfProject(qint64 projectId) const;
    qint64 currentProjectId() const;
    const domain::Project* currentProject() const;
    QString projectNameForId(qint64 projectId) const;
    QString formatWorkflowLabel(const domain::Workflow& workflow) const;
    void publishExecutionLog(const QString& text) const;

    services::ProjectService& m_projectService;
    services::SettingsService& m_settingsService;
    services::MemoryService& m_memoryService;
    services::WorkflowService& m_workflowService;
    providers::ProviderManager& m_providerManager;
    core::WorkflowEngine m_workflowEngine;

    QList<domain::Project> m_projects;
    QList<domain::Workflow> m_workflows;
    qint64 m_currentWorkflowId = -1;
    int m_activeRunCount = 0;
    int m_nextExecutionId = 1;
    int m_executionStatusFrame = 0;
    std::function<void()> m_onWorkflowDataChanged;
    std::function<void(const QString&)> m_onExecutionLogChanged;
    bool m_isSyncingVisualEditor = false;
    bool m_visualEditorHasValidJson = false;
    QJsonObject m_visualDefinitionRoot;

    QListWidget* m_workflowList = nullptr;
    QLabel* m_workflowCountLabel = nullptr;
    QComboBox* m_projectCombo = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QTextEdit* m_descriptionEdit = nullptr;
    QCheckBox* m_visualEditorToggle = nullptr;
    QFrame* m_visualEditorFrame = nullptr;
    QLabel* m_visualEditorStatusLabel = nullptr;
    QListWidget* m_visualStepList = nullptr;
    QLineEdit* m_visualStepIdEdit = nullptr;
    QComboBox* m_visualStepTypeCombo = nullptr;
    QLineEdit* m_visualStepNameEdit = nullptr;
    QStackedWidget* m_visualStepConfigStack = nullptr;
    QTextEdit* m_promptTextEdit = nullptr;
    QTextEdit* m_promptSystemPromptEdit = nullptr;
    QLineEdit* m_promptOutputEdit = nullptr;
    QLineEdit* m_promptModelEdit = nullptr;
    QTextEdit* m_memoryContentEdit = nullptr;
    QLineEdit* m_memoryTypeEdit = nullptr;
    QLineEdit* m_memorySourceEdit = nullptr;
    QLineEdit* m_memoryTagsEdit = nullptr;
    QSpinBox* m_memoryRelevanceSpin = nullptr;
    QLabel* m_decisionAdvancedLabel = nullptr;
    QLineEdit* m_decisionInputEdit = nullptr;
    QComboBox* m_decisionOperatorCombo = nullptr;
    QLineEdit* m_decisionValueEdit = nullptr;
    QLineEdit* m_decisionOutputEdit = nullptr;
    QLineEdit* m_decisionTrueResultEdit = nullptr;
    QLineEdit* m_decisionFalseResultEdit = nullptr;
    QLineEdit* m_decisionIfTrueEdit = nullptr;
    QLineEdit* m_decisionIfFalseEdit = nullptr;
    QCheckBox* m_decisionCaseSensitiveCheckBox = nullptr;
    QPlainTextEdit* m_definitionEdit = nullptr;
    QPlainTextEdit* m_executionOutputView = nullptr;
    QCheckBox* m_activeCheckBox = nullptr;
    QLabel* m_executionStatusLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QTimer* m_executionStatusTimer = nullptr;
    QTimer* m_visualSyncTimer = nullptr;
    QTimer* m_visualApplyTimer = nullptr;
};

} // namespace privateclaw::ui
