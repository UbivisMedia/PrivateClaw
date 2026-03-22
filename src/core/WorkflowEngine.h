#pragma once

#include "core/RunContext.h"
#include "domain/Workflow.h"

#include <QString>

namespace privateclaw::core {

class WorkflowEngine
{
public:
    QString validateWorkflow(const domain::Workflow& workflow) const;
    QString previewExecution(const domain::Workflow& workflow, const RunContext& runContext) const;
};

} // namespace privateclaw::core

