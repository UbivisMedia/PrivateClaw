#pragma once

#include "domain/Project.h"

#include <QList>

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::services {

class ProjectService
{
public:
    explicit ProjectService(storage::DatabaseManager& databaseManager);

    int projectCount() const;
    QList<domain::Project> listProjects() const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services

