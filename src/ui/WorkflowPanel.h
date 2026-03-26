#pragma once

#include "core/WorkflowEngine.h"
#include "domain/Project.h"
#include "domain/Workflow.h"
#include "services/ComfyUiMetadataService.h"

#include <QHash>
#include <QJsonObject>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QFutureWatcherBase;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTextEdit;
class QTimer;

namespace privateclaw::providers {
class ProviderManager;
}

namespace privateclaw::services {
class MemoryService;
class ProjectService;
class ProjectVariableService;
class RunService;
class SecretsService;
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
        services::ProjectVariableService& projectVariableService,
        services::MemoryService& memoryService,
        services::SecretsService& secretsService,
        services::RunService& runService,
        services::WorkflowService& workflowService,
        providers::ProviderManager& providerManager,
        QWidget* parent = nullptr
    );

    int workflowCount() const;
    void reloadData();
    void setOnWorkflowDataChanged(std::function<void()> callback);
    void setOnRunDataChanged(std::function<void()> callback);
    void setOnSettingsDataChanged(std::function<void()> callback);
    void setOnExecutionLogChanged(std::function<void(const QString&)> callback);
    void setOnExecutionStreamChunk(
        std::function<void(const QString& streamId, const QString& prefix, const QString& chunk)> callback
    );

private:
    void buildUi();
    void refreshData(qint64 workflowIdToSelect = -1);
    void refreshProjects(qint64 selectedProjectId = -1);
    void refreshWorkflowList(qint64 workflowIdToSelect = -1);
    void loadWorkflowFromRow(int row);
    void saveWorkflow();
    void executeWorkflow(bool debugRequested = false);
    void cancelActiveExecution();
    void updateExecutionStatus();
    void updateVisualEditorVisibility();
    void startNewWorkflow();
    void refreshTemplateLibrary();
    void updateTemplatePreview();
    void setTemplateLibraryExpanded(bool expanded);
    void updateTemplateLibraryVisibility();
    void loadSelectedTemplateIntoEditor();
    void registerAllowlistPrompt(QLineEdit* lineEdit, bool preferParentDirectory, const QString& contextLabel);
    void registerAllowlistPrompt(QComboBox* comboBox, bool preferParentDirectory, const QString& contextLabel);
    bool ensurePathAllowedForUi(const QString& path, bool preferParentDirectory, const QString& contextLabel);
    bool ensureWorkflowDefinitionPathsAllowed(const QString& definitionJson, const QString& actionLabel);
    void scheduleVisualSyncFromJson();
    void syncVisualEditorFromJson();
    void rebuildVisualStepList(const QString& stepIdToSelect = QString());
    void rebuildVisualGraph(const QString& stepIdToSelect = QString());
    void loadVisualStepFromRow(int row);
    void clearVisualStepEditor();
    void updateVisualConfigPage();
    void updateVisualToolConfigPage();
    void refreshComfyMetadata(bool forceReload = false);
    void applyComfyCatalogToUi();
    void addComfyLoraRow(
        const QString& loraName = QString(),
        double modelStrength = 1.0,
        double clipStrength = 1.0,
        bool enabled = true
    );
    void removeSelectedComfyLoraRow();
    void scheduleVisualStepApply();
    void applyVisualStepChanges();
    void addVisualStep(const QString& stepType);
    void removeSelectedVisualStep();
    void moveSelectedVisualStep(int offset);
    QString selectedVisualStepId() const;
    QString generateVisualStepId(const QString& stepType) const;
    QString visualStepLabel(const QJsonObject& stepObject) const;
    void resetDebugger(bool keepVisibilityState = true);
    void updateDebuggerVisibility();
    void rebuildDebugStepList();
    void loadDebugStepFromRow(int row);
    void resetEditor(bool keepFeedback = false);
    int indexOfProject(qint64 projectId) const;
    qint64 currentProjectId() const;
    const domain::Project* currentProject() const;
    QString currentComfyUiBaseUrl() const;
    QString projectNameForId(qint64 projectId) const;
    QString formatWorkflowLabel(const domain::Workflow& workflow) const;
    void publishExecutionLog(const QString& text) const;

    services::ProjectService& m_projectService;
    services::SettingsService& m_settingsService;
    services::ProjectVariableService& m_projectVariableService;
    services::MemoryService& m_memoryService;
    services::SecretsService& m_secretsService;
    services::RunService& m_runService;
    services::WorkflowService& m_workflowService;
    providers::ProviderManager& m_providerManager;
    core::WorkflowEngine m_workflowEngine;

    QList<domain::Project> m_projects;
    QList<domain::Workflow> m_workflows;
    qint64 m_currentWorkflowId = -1;
    int m_activeRunCount = 0;
    int m_nextExecutionId = 1;
    int m_executionStatusFrame = 0;
    QString m_activeExecutionDetail;
    QList<int> m_activeExecutionOrder;
    QHash<int, std::shared_ptr<std::atomic_bool>> m_executionCancelFlags;
    std::function<void()> m_onWorkflowDataChanged;
    std::function<void()> m_onRunDataChanged;
    std::function<void()> m_onSettingsDataChanged;
    std::function<void(const QString&)> m_onExecutionLogChanged;
    std::function<void(const QString& streamId, const QString& prefix, const QString& chunk)>
        m_onExecutionStreamChunk;
    bool m_isSyncingVisualEditor = false;
    bool m_isSyncingVisualGraph = false;
    bool m_visualEditorHasValidJson = false;
    bool m_lastExecutionWasDebug = false;
    bool m_comfyMetadataLoaded = false;
    bool m_comfyMetadataLoading = false;
    QString m_comfyCatalogBaseUrl;
    QJsonObject m_visualDefinitionRoot;
    services::ComfyUiCatalog m_comfyCatalog;
    QList<core::WorkflowDebugStep> m_lastDebugSteps;

    QListWidget* m_workflowList = nullptr;
    QLabel* m_workflowCountLabel = nullptr;
    QLabel* m_jsonDefinitionLabel = nullptr;
    QComboBox* m_projectCombo = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QTextEdit* m_descriptionEdit = nullptr;
    QFrame* m_templateFrame = nullptr;
    QFrame* m_templateBodyFrame = nullptr;
    QPushButton* m_templateToggleButton = nullptr;
    QComboBox* m_templateCombo = nullptr;
    QLabel* m_templateDescriptionLabel = nullptr;
    QFrame* m_visualEditorFrame = nullptr;
    QCheckBox* m_debuggerToggle = nullptr;
    QFrame* m_debuggerFrame = nullptr;
    QLabel* m_debuggerStatusLabel = nullptr;
    QListWidget* m_debugStepList = nullptr;
    QTabWidget* m_debugDetailTabs = nullptr;
    QTabWidget* m_editorTabs = nullptr;
    QPlainTextEdit* m_debugSummaryView = nullptr;
    QPlainTextEdit* m_debugVariablesView = nullptr;
    QPlainTextEdit* m_debugLogsView = nullptr;
    QFrame* m_jsonDefinitionFrame = nullptr;
    QLabel* m_visualEditorStatusLabel = nullptr;
    QGraphicsView* m_visualGraphView = nullptr;
    QGraphicsScene* m_visualGraphScene = nullptr;
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
    QComboBox* m_toolNameCombo = nullptr;
    QLineEdit* m_toolOutputEdit = nullptr;
    QStackedWidget* m_toolConfigStack = nullptr;
    QLineEdit* m_toolFileReadPathEdit = nullptr;
    QSpinBox* m_toolFileReadLineStartSpin = nullptr;
    QSpinBox* m_toolFileReadLineEndSpin = nullptr;
    QSpinBox* m_toolFileReadMaxCharsSpin = nullptr;
    QLineEdit* m_toolJsonInputEdit = nullptr;
    QLineEdit* m_toolJsonPathEdit = nullptr;
    QCheckBox* m_toolJsonPrettyCheckBox = nullptr;
    QLineEdit* m_toolCsvPathEdit = nullptr;
    QComboBox* m_toolCsvDelimiterCombo = nullptr;
    QCheckBox* m_toolCsvHasHeaderCheckBox = nullptr;
    QSpinBox* m_toolCsvMaxRowsSpin = nullptr;
    QComboBox* m_toolCsvOutputFormatCombo = nullptr;
    QComboBox* m_toolCsvSourceFormatCombo = nullptr;
    QCheckBox* m_toolCsvCreateDirsCheckBox = nullptr;
    QCheckBox* m_toolCsvReturnContentCheckBox = nullptr;
    QPlainTextEdit* m_toolCsvContentEdit = nullptr;
    QLineEdit* m_toolDirectoryReadPathEdit = nullptr;
    QLineEdit* m_toolDirectoryReadExtensionsEdit = nullptr;
    QLineEdit* m_toolDirectoryReadExcludeEdit = nullptr;
    QLineEdit* m_toolDirectoryReadModifiedAfterEdit = nullptr;
    QSpinBox* m_toolDirectoryReadMaxFilesSpin = nullptr;
    QSpinBox* m_toolDirectoryReadMaxCharsPerFileSpin = nullptr;
    QSpinBox* m_toolDirectoryReadMaxTotalCharsSpin = nullptr;
    QSpinBox* m_toolDirectoryReadWithinMinutesSpin = nullptr;
    QCheckBox* m_toolDirectoryReadIncludeHiddenCheckBox = nullptr;
    QCheckBox* m_toolDirectoryReadSkipBinaryCheckBox = nullptr;
    QLineEdit* m_toolDirectoryListPathEdit = nullptr;
    QLineEdit* m_toolDirectoryListExtensionsEdit = nullptr;
    QLineEdit* m_toolDirectoryListExcludeEdit = nullptr;
    QSpinBox* m_toolDirectoryListMaxEntriesSpin = nullptr;
    QCheckBox* m_toolDirectoryListRecursiveCheckBox = nullptr;
    QCheckBox* m_toolDirectoryListIncludeHiddenCheckBox = nullptr;
    QCheckBox* m_toolDirectoryListDirectoriesOnlyCheckBox = nullptr;
    QComboBox* m_toolMemoryIngestModeCombo = nullptr;
    QLineEdit* m_toolMemoryIngestTypeEdit = nullptr;
    QLineEdit* m_toolMemoryIngestSourceEdit = nullptr;
    QLineEdit* m_toolMemoryIngestTagsEdit = nullptr;
    QSpinBox* m_toolMemoryIngestRelevanceSpin = nullptr;
    QLineEdit* m_toolMemoryQueryEdit = nullptr;
    QLineEdit* m_toolMemoryTypeFilterEdit = nullptr;
    QLineEdit* m_toolMemoryTagsFilterEdit = nullptr;
    QSpinBox* m_toolMemoryLimitSpin = nullptr;
    QSpinBox* m_toolMemoryMaxCharsSpin = nullptr;
    QComboBox* m_toolMemoryFormatCombo = nullptr;
    QPlainTextEdit* m_toolMemorySummaryPromptEdit = nullptr;
    QLineEdit* m_toolMemorySummarySystemPromptEdit = nullptr;
    QCheckBox* m_toolMemorySaveSummaryCheckBox = nullptr;
    QLineEdit* m_toolMemorySummaryTypeEdit = nullptr;
    QLineEdit* m_toolMemorySummarySourceEdit = nullptr;
    QLineEdit* m_toolMemorySummaryTagsEdit = nullptr;
    QSpinBox* m_toolMemorySummaryRelevanceSpin = nullptr;
    QSpinBox* m_toolMemoryDeleteOlderThanDaysSpin = nullptr;
    QSpinBox* m_toolMemoryDeleteKeepLatestSpin = nullptr;
    QSpinBox* m_toolMemoryDeleteKeepRelevanceSpin = nullptr;
    QCheckBox* m_toolMemoryDeleteDryRunCheckBox = nullptr;
    QComboBox* m_toolVariableScopeCombo = nullptr;
    QLineEdit* m_toolVariableNameEdit = nullptr;
    QComboBox* m_toolVariableTypeCombo = nullptr;
    QComboBox* m_toolVariableOperationCombo = nullptr;
    QLineEdit* m_toolVariableValueEdit = nullptr;
    QLineEdit* m_toolVariableCurrentValueEdit = nullptr;
    QDoubleSpinBox* m_toolVariableAmountSpin = nullptr;
    QPlainTextEdit* m_toolForeachItemsEdit = nullptr;
    QLineEdit* m_toolForeachItemVarEdit = nullptr;
    QLineEdit* m_toolForeachIndexVarEdit = nullptr;
    QComboBox* m_toolForeachResultModeCombo = nullptr;
    QLineEdit* m_toolForeachResultSourceEdit = nullptr;
    QLineEdit* m_toolForeachResultVarEdit = nullptr;
    QLineEdit* m_toolForeachJoinWithEdit = nullptr;
    QComboBox* m_toolForeachOnErrorCombo = nullptr;
    QSpinBox* m_toolForeachMaxIterationsSpin = nullptr;
    QPlainTextEdit* m_toolForeachStepsEdit = nullptr;
    QLineEdit* m_toolFileWritePathEdit = nullptr;
    QComboBox* m_toolFileWriteModeCombo = nullptr;
    QCheckBox* m_toolFileWriteCreateDirsCheckBox = nullptr;
    QCheckBox* m_toolFileWriteReturnContentCheckBox = nullptr;
    QPlainTextEdit* m_toolFileWriteContentEdit = nullptr;
    QLineEdit* m_toolFileEditPathEdit = nullptr;
    QCheckBox* m_toolFileEditReturnContentCheckBox = nullptr;
    QPlainTextEdit* m_toolFileEditDiffEdit = nullptr;
    QLineEdit* m_toolHttpUrlEdit = nullptr;
    QComboBox* m_toolHttpMethodCombo = nullptr;
    QSpinBox* m_toolHttpTimeoutSpin = nullptr;
    QCheckBox* m_toolHttpBodyJsonCheckBox = nullptr;
    QPlainTextEdit* m_toolHttpHeadersEdit = nullptr;
    QPlainTextEdit* m_toolHttpBodyEdit = nullptr;
    QLineEdit* m_toolShellCommandEdit = nullptr;
    QLineEdit* m_toolShellWorkingDirEdit = nullptr;
    QSpinBox* m_toolShellTimeoutSpin = nullptr;
    QSpinBox* m_toolShellMaxOutputCharsSpin = nullptr;
    QCheckBox* m_toolShellIncludeStderrCheckBox = nullptr;
    QPushButton* m_toolComfyRefreshButton = nullptr;
    QLabel* m_toolComfyStatusLabel = nullptr;
    QLineEdit* m_toolComfyBaseUrlEdit = nullptr;
    QComboBox* m_toolComfyModeCombo = nullptr;
    QStackedWidget* m_toolComfyModeStack = nullptr;
    QComboBox* m_toolComfyCheckpointCombo = nullptr;
    QComboBox* m_toolComfyVaeCombo = nullptr;
    QComboBox* m_toolComfyImageCombo = nullptr;
    QComboBox* m_toolComfyMaskImageCombo = nullptr;
    QComboBox* m_toolComfyMaskChannelCombo = nullptr;
    QSpinBox* m_toolComfyMaskGrowSpin = nullptr;
    QTextEdit* m_toolComfyPositivePromptEdit = nullptr;
    QTextEdit* m_toolComfyNegativePromptEdit = nullptr;
    QSpinBox* m_toolComfyWidthSpin = nullptr;
    QSpinBox* m_toolComfyHeightSpin = nullptr;
    QSpinBox* m_toolComfyBatchSizeSpin = nullptr;
    QSpinBox* m_toolComfyStepsSpin = nullptr;
    QSpinBox* m_toolComfySeedSpin = nullptr;
    QCheckBox* m_toolComfyRandomizeSeedCheckBox = nullptr;
    QDoubleSpinBox* m_toolComfyCfgSpin = nullptr;
    QDoubleSpinBox* m_toolComfyDenoiseSpin = nullptr;
    QComboBox* m_toolComfySamplerCombo = nullptr;
    QComboBox* m_toolComfySchedulerCombo = nullptr;
    QSpinBox* m_toolComfyClipSkipSpin = nullptr;
    QLineEdit* m_toolComfyFilenamePrefixEdit = nullptr;
    QTableWidget* m_toolComfyLoraTable = nullptr;
    QPushButton* m_toolComfyAddLoraButton = nullptr;
    QPushButton* m_toolComfyRemoveLoraButton = nullptr;
    QPlainTextEdit* m_toolComfyWorkflowEdit = nullptr;
    QLineEdit* m_toolComfyOutputDirEdit = nullptr;
    QCheckBox* m_toolComfyDownloadImagesCheckBox = nullptr;
    QCheckBox* m_toolComfyIncludeHistoryCheckBox = nullptr;
    QSpinBox* m_toolComfyPollIntervalSpin = nullptr;
    QSpinBox* m_toolComfyTimeoutSpin = nullptr;
    QPlainTextEdit* m_definitionEdit = nullptr;
    QPlainTextEdit* m_executionOutputView = nullptr;
    QCheckBox* m_activeCheckBox = nullptr;
    QLabel* m_executionStatusLabel = nullptr;
    QPushButton* m_cancelRunButton = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QTimer* m_executionStatusTimer = nullptr;
    QTimer* m_visualSyncTimer = nullptr;
    QTimer* m_visualApplyTimer = nullptr;
};

} // namespace privateclaw::ui
