#pragma once

#include "domain/MemoryEntry.h"

#include <QList>

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::services {

class MemoryService
{
public:
    explicit MemoryService(storage::DatabaseManager& databaseManager);

    int memoryCountForProject(qint64 projectId) const;
    QList<domain::MemoryEntry> recentEntries(qint64 projectId, int limit = 20) const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services

