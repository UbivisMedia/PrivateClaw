#include "core/WorkflowEngine.h"

namespace privateclaw::core {

QString WorkflowEngine::validateWorkflow(const domain::Workflow& workflow) const
{
    if (workflow.name.trimmed().isEmpty()) {
        return "Workflow braucht einen Namen.";
    }

    if (workflow.steps.isEmpty()) {
        return "Workflow enthaelt noch keine Schritte.";
    }

    return {};
}

QString WorkflowEngine::previewExecution(const domain::Workflow& workflow, const RunContext& runContext) const
{
    return QString("Workflow '%1' wurde fuer Projekt '%2' im Vorschaumodus vorbereitet.")
        .arg(workflow.name, runContext.projectName);
}

} // namespace privateclaw::core

