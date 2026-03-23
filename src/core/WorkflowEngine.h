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

namespace privateclaw::core {

struct ExecutionResult
{
    bool success = false;
    QString errorMessage;
    QString finalOutput;
    QStringList logs;
    QHash<QString, QString> variables;
    QList<domain::MemoryEntry> memoryEntriesToPersist;
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
        providers::ILlmProvider& provider
    ) const;

private:
    QString renderTemplate(const QString& templateText, const RunContext& runContext) const;

    const tools::ToolExecutor* m_toolExecutor = nullptr;
};

} // namespace privateclaw::core
