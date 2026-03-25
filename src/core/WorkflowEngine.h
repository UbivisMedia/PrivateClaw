#pragma once

#include "core/RunContext.h"
#include "domain/MemoryEntry.h"
#include "domain/Workflow.h"
#include "providers/ILlmProvider.h"
#include "tools/ToolExecutor.h"

#include <QList>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace privateclaw::core {

struct WorkflowDebugVariable
{
    QString key;
    QString value;
};

struct WorkflowDebugStep
{
    int executionIndex = 0;
    QString stepId;
    QString stepType;
    QString stepName;
    QString status;
    QString summary;
    QString inputPreview;
    QString outputKey;
    QString outputPreview;
    QString outputText;
    QString reasoningText;
    QString nextStepId;
    QString errorMessage;
    int memoryEntryCount = 0;
    int directMemoryEntryCount = 0;
    int compressedMemoryEntryCount = 0;
    int totalPinnedMemoryEntryCount = 0;
    QList<WorkflowDebugVariable> variablesAfterStep;
    QStringList logs;
};

struct ExecutionResult
{
    bool success = false;
    QString errorMessage;
    QString finalOutput;
    QStringList logs;
    QHash<QString, QString> variables;
    QList<domain::MemoryEntry> memoryEntriesToPersist;
    QList<WorkflowDebugStep> debugSteps;
};

struct ExecutionCallbacks
{
    std::function<void(const QString& line)> onLogLine;
    std::function<void(const QString& stepId, const QString& statusText)> onStepStatus;
    std::function<void(const QString& stepId, const QString& chunk)> onPromptChunk;
};

class WorkflowEngine
{
public:
    explicit WorkflowEngine(const tools::ToolExecutor* toolExecutor = nullptr);

    void setToolExecutor(const tools::ToolExecutor* toolExecutor);
    QString validateWorkflow(const domain::Workflow& workflow) const;
    QString previewExecution(const domain::Workflow& workflow, const RunContext& runContext) const;
    ExecutionResult executeWorkflow(
        const domain::Workflow& workflow,
        RunContext runContext,
        providers::ILlmProvider& provider,
        ExecutionCallbacks callbacks = {}
    ) const;

private:
    QString renderTemplate(const QString& templateText, const RunContext& runContext) const;

    const tools::ToolExecutor* m_toolExecutor = nullptr;
};

} // namespace privateclaw::core
