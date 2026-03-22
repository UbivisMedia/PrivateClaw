#include "services/ProjectService.h"

#include "storage/DatabaseManager.h"

#include <QSqlQuery>

namespace privateclaw::services {

ProjectService::ProjectService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

int ProjectService::projectCount() const
{
    QSqlQuery query(m_databaseManager.database());
    if (!query.exec("SELECT COUNT(*) FROM projects")) {
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QList<domain::Project> ProjectService::listProjects() const
{
    return {};
}

} // namespace privateclaw::services

