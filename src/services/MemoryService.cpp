#include "services/MemoryService.h"

#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

namespace privateclaw::services {

namespace {

QStringList parseTags(const QString& rawTags)
{
    QStringList tags;
    const QStringList parts = rawTags.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString normalized = part.trimmed();
        if (!normalized.isEmpty()) {
            tags.append(normalized);
        }
    }

    return tags;
}

QString serializeTags(const QStringList& tags)
{
    QStringList normalized;
    normalized.reserve(tags.size());
    for (const QString& tag : tags) {
        const QString cleaned = tag.trimmed();
        if (!cleaned.isEmpty()) {
            normalized.append(cleaned);
        }
    }

    return normalized.join(", ");
}

domain::MemoryEntry mapEntry(const QSqlQuery& query)
{
    domain::MemoryEntry entry;
    entry.id = query.value(0).toLongLong();
    entry.projectId = query.value(1).toLongLong();
    entry.type = query.value(2).toString();
    entry.content = query.value(3).toString();
    entry.source = query.value(4).toString();
    entry.tags = parseTags(query.value(5).toString());
    entry.relevance = query.value(6).toInt();
    entry.createdAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
    return entry;
}

} // namespace

MemoryService::MemoryService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

int MemoryService::memoryCount() const
{
    QSqlQuery query(m_databaseManager.database());
    if (!query.exec("SELECT COUNT(*) FROM memory_entries")) {
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
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

QList<domain::MemoryEntry> MemoryService::listEntries(
    const qint64 projectId,
    const QString& searchText,
    const int limit
) const
{
    QList<domain::MemoryEntry> entries;

    QString statement =
        "SELECT id, project_id, entry_type, content, source, tags, relevance, created_at "
        "FROM memory_entries";
    QStringList conditions;
    if (projectId > 0) {
        conditions.append("project_id = ?");
    }

    const QString normalizedSearch = searchText.trimmed().toLower();
    if (!normalizedSearch.isEmpty()) {
        conditions.append(
            "(LOWER(entry_type) LIKE ? OR LOWER(content) LIKE ? OR LOWER(source) LIKE ? OR LOWER(tags) LIKE ?)"
        );
    }

    if (!conditions.isEmpty()) {
        statement += " WHERE " + conditions.join(" AND ");
    }

    statement += " ORDER BY relevance DESC, created_at DESC, id DESC";
    if (limit > 0) {
        statement += " LIMIT ?";
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare(statement);
    if (projectId > 0) {
        query.addBindValue(projectId);
    }

    if (!normalizedSearch.isEmpty()) {
        const QString likeValue = "%" + normalizedSearch + "%";
        query.addBindValue(likeValue);
        query.addBindValue(likeValue);
        query.addBindValue(likeValue);
        query.addBindValue(likeValue);
    }

    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        return entries;
    }

    while (query.next()) {
        entries.append(mapEntry(query));
    }

    return entries;
}

QList<domain::MemoryEntry> MemoryService::recentEntries(const qint64 projectId, const int limit) const
{
    QList<domain::MemoryEntry> entries;
    if (projectId <= 0) {
        return entries;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "SELECT id, project_id, entry_type, content, source, tags, relevance, created_at "
        "FROM memory_entries "
        "WHERE project_id = ? "
        "ORDER BY relevance DESC, created_at DESC, id DESC "
        "LIMIT ?"
    );
    query.addBindValue(projectId);
    query.addBindValue(limit > 0 ? limit : 20);

    if (!query.exec()) {
        return entries;
    }

    while (query.next()) {
        entries.append(mapEntry(query));
    }

    return entries;
}

bool MemoryService::saveEntry(domain::MemoryEntry* entry, QString* errorMessage) const
{
    if (entry == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Memory-Eintrag uebergeben.";
        }
        return false;
    }

    if (entry->projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Memory-Eintrag braucht ein gueltiges Projekt.";
        }
        return false;
    }

    entry->type = entry->type.trimmed().isEmpty() ? "note" : entry->type.trimmed();
    entry->content = entry->content.trimmed();
    entry->source = entry->source.trimmed();
    entry->tags = parseTags(serializeTags(entry->tags));

    if (entry->content.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Memory-Inhalt darf nicht leer sein.";
        }
        return false;
    }

    QSqlQuery query(m_databaseManager.database());
    if (entry->id > 0) {
        query.prepare(
            "UPDATE memory_entries "
            "SET project_id = ?, entry_type = ?, content = ?, source = ?, tags = ?, relevance = ? "
            "WHERE id = ?"
        );
        query.addBindValue(entry->projectId);
        query.addBindValue(entry->type);
        query.addBindValue(entry->content);
        query.addBindValue(entry->source);
        query.addBindValue(serializeTags(entry->tags));
        query.addBindValue(entry->relevance);
        query.addBindValue(entry->id);
    } else {
        const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        query.prepare(
            "INSERT INTO memory_entries (project_id, entry_type, content, source, tags, relevance, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"
        );
        query.addBindValue(entry->projectId);
        query.addBindValue(entry->type);
        query.addBindValue(entry->content);
        query.addBindValue(entry->source);
        query.addBindValue(serializeTags(entry->tags));
        query.addBindValue(entry->relevance);
        query.addBindValue(timestamp);
    }

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (entry->id <= 0) {
        entry->id = query.lastInsertId().toLongLong();
        entry->createdAt = QDateTime::currentDateTimeUtc();
    }

    return true;
}

bool MemoryService::deleteEntry(const qint64 entryId, QString* errorMessage) const
{
    if (entryId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein gueltiger Memory-Eintrag ausgewaehlt.";
        }
        return false;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare("DELETE FROM memory_entries WHERE id = ?");
    query.addBindValue(entryId);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

} // namespace privateclaw::services
