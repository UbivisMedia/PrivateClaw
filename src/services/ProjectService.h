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

    bool createProject(domain::Project* project, QString* errorMessage = nullptr) const;
    bool updateProject(const domain::Project& project, QString* errorMessage = nullptr) const;
    bool deleteProject(qint64 projectId, QString* errorMessage = nullptr) const;
    int projectCount() const;
    QList<domain::Project> listProjects() const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services
