#pragma once

#include "domain/MemoryEntry.h"

#include <QList>

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::services {

struct PreparedMemoryContext
{
    QStringList snippets;
    int totalEntries = 0;
    int directEntries = 0;
    int compressedEntries = 0;
    int pinnedEntries = 0;
    int totalPinnedEntries = 0;
};

class MemoryService
{
public:
    explicit MemoryService(storage::DatabaseManager& databaseManager);

    int memoryCount() const;
    int memoryCountForProject(qint64 projectId) const;
    int pinnedMemoryCountForProject(qint64 projectId) const;
    QList<domain::MemoryEntry> listEntries(
        qint64 projectId,
        const QString& searchText = QString(),
        int limit = 200
    ) const;
    QList<domain::MemoryEntry> recentEntries(qint64 projectId, int limit = 20) const;
    PreparedMemoryContext prepareRunContext(
        qint64 projectId,
        int fetchLimit = 48,
        int directEntryLimit = 8
    ) const;
    bool saveEntry(domain::MemoryEntry* entry, QString* errorMessage = nullptr) const;
    bool deleteEntry(qint64 entryId, QString* errorMessage = nullptr) const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services
