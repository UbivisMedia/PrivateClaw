#pragma once

#include "domain/Workflow.h"

#include <QList>

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::services {

class WorkflowService
{
public:
    explicit WorkflowService(storage::DatabaseManager& databaseManager);

    bool hydrateWorkflowDefinition(domain::Workflow* workflow, QString* errorMessage = nullptr) const;
    bool saveWorkflow(domain::Workflow* workflow, QString* errorMessage = nullptr) const;
    int workflowCount() const;
    QList<domain::Workflow> listWorkflows() const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services
