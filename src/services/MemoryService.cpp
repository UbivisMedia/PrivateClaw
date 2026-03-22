#include "services/MemoryService.h"

#include "storage/DatabaseManager.h"

#include <QSqlQuery>

namespace privateclaw::services {

MemoryService::MemoryService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

int MemoryService::memoryCountForProject(const qint64 projectId) const
{
    QSqlQuery query(m_databaseManager.database());
    query.prepare("SELECT COUNT(*) FROM memory_entries WHERE project_id = ?");
    query.addBindValue(projectId);

    if (!query.exec() || !query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QList<domain::MemoryEntry> MemoryService::recentEntries(const qint64 projectId, const int limit) const
{
    Q_UNUSED(projectId);
    Q_UNUSED(limit);
    return {};
}

} // namespace privateclaw::services

