#include "services/MemoryService.h"

#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

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
    entry.pinned = query.value(7).toInt() > 0;
    entry.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
    return entry;
}

QString formatMemorySnippet(const domain::MemoryEntry& entry)
{
    QStringList parts;
    if (entry.pinned) {
        parts.append("Angepinnt");
    }

    parts.append(QString("Typ: %1").arg(entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed()));

    if (!entry.source.trimmed().isEmpty()) {
        parts.append(QString("Quelle: %1").arg(entry.source.trimmed()));
    }

    if (!entry.tags.isEmpty()) {
        parts.append(QString("Tags: %1").arg(entry.tags.join(", ")));
    }

    QString content = entry.content.simplified();
    if (content.size() > 240) {
        content = content.left(237) + "...";
    }
    parts.append(QString("Inhalt: %1").arg(content));

    return QString("- %1").arg(parts.join(" | "));
}

QString formatCountSummary(const QHash<QString, int>& counts, const int maxItems)
{
    QList<QPair<QString, int>> pairs;
    pairs.reserve(counts.size());
    for (auto iterator = counts.constBegin(); iterator != counts.constEnd(); ++iterator) {
        pairs.append(qMakePair(iterator.key(), iterator.value()));
    }

    std::sort(
        pairs.begin(),
        pairs.end(),
        [](const QPair<QString, int>& left, const QPair<QString, int>& right) {
            if (left.second != right.second) {
                return left.second > right.second;
            }
            return left.first < right.first;
        }
    );

    QStringList summary;
    const int summaryCount = qMin(maxItems, pairs.size());
    for (int index = 0; index < summaryCount; ++index) {
        summary.append(QString("%1 x%2").arg(pairs.at(index).first, QString::number(pairs.at(index).second)));
    }

    return summary.join(", ");
}

QString formatCompressedMemorySnippet(
    const QList<domain::MemoryEntry>& sampleEntries,
    const int totalRemainingEntries,
    const int hiddenEntries
)
{
    if (totalRemainingEntries <= 0) {
        return {};
    }

    QStringList lines;
    lines.append(QString("- Verdichtete Erinnerung aus %1 weiteren Eintraegen:").arg(totalRemainingEntries));

    int pinnedCount = 0;
    QHash<QString, int> typeCounts;
    QHash<QString, int> tagCounts;
    for (const domain::MemoryEntry& entry : sampleEntries) {
        if (entry.pinned) {
            ++pinnedCount;
        }

        const QString typeKey = entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed();
        typeCounts[typeKey] += 1;

        for (const QString& tag : entry.tags) {
            const QString normalizedTag = tag.trimmed();
            if (!normalizedTag.isEmpty()) {
                tagCounts[normalizedTag] += 1;
            }
        }
    }

    if (pinnedCount > 0) {
        lines.append(QString("  Angepinnte Rest-Eintraege: %1").arg(pinnedCount));
    }

    const QString typeSummary = formatCountSummary(typeCounts, 4);
    if (!typeSummary.isEmpty()) {
        lines.append(QString("  Typen: %1").arg(typeSummary));
    }

    const QString tagSummary = formatCountSummary(tagCounts, 6);
    if (!tagSummary.isEmpty()) {
        lines.append(QString("  Wichtige Tags: %1").arg(tagSummary));
    }

    const int previewCount = qMin(sampleEntries.size(), 4);
    for (int index = 0; index < previewCount; ++index) {
        const domain::MemoryEntry& entry = sampleEntries.at(index);
        QString content = entry.content.simplified();
        if (content.size() > 140) {
            content = content.left(137) + "...";
        }

        const QString prefix = entry.pinned ? "  * [PIN]" : "  *";
        lines.append(
            QString("%1 %2: %3")
                .arg(prefix, entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed(), content)
        );
    }

    if (hiddenEntries > 0) {
        lines.append(QString("  ... %1 weitere Eintraege sind zusaetzlich verdichtet.").arg(hiddenEntries));
    }

    return lines.join("\n");
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

int MemoryService::pinnedMemoryCountForProject(const qint64 projectId) const
{
    QSqlQuery query(m_databaseManager.database());
    query.prepare("SELECT COUNT(*) FROM memory_entries WHERE project_id = ? AND is_pinned = 1");
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
        "SELECT id, project_id, entry_type, content, source, tags, relevance, is_pinned, created_at "
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

    statement += " ORDER BY is_pinned DESC, relevance DESC, created_at DESC, id DESC";
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
        "SELECT id, project_id, entry_type, content, source, tags, relevance, is_pinned, created_at "
        "FROM memory_entries "
        "WHERE project_id = ? "
        "ORDER BY is_pinned DESC, relevance DESC, created_at DESC, id DESC "
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

PreparedMemoryContext MemoryService::prepareRunContext(
    const qint64 projectId,
    const int fetchLimit,
    const int directEntryLimit
) const
{
    PreparedMemoryContext context;
    context.totalEntries = memoryCountForProject(projectId);
    context.totalPinnedEntries = pinnedMemoryCountForProject(projectId);
    if (projectId <= 0 || context.totalEntries <= 0) {
        return context;
    }

    const int resolvedFetchLimit = fetchLimit > 0 ? fetchLimit : 48;
    const int resolvedDirectLimit = qMax(1, directEntryLimit);
    const QList<domain::MemoryEntry> memoryEntries = listEntries(projectId, QString(), resolvedFetchLimit);
    if (memoryEntries.isEmpty()) {
        return context;
    }

    const int selectedDirectEntries = qMin(memoryEntries.size(), resolvedDirectLimit);
    context.directEntries = selectedDirectEntries;
    for (int index = 0; index < selectedDirectEntries; ++index) {
        const domain::MemoryEntry& entry = memoryEntries.at(index);
        context.snippets.append(formatMemorySnippet(entry));
        if (entry.pinned) {
            ++context.pinnedEntries;
        }
    }

    const int totalRemainingEntries = qMax(0, context.totalEntries - selectedDirectEntries);
    if (totalRemainingEntries <= 0) {
        return context;
    }

    const QList<domain::MemoryEntry> remainingSampleEntries = memoryEntries.mid(selectedDirectEntries);
    const int hiddenEntries = qMax(0, totalRemainingEntries - remainingSampleEntries.size());
    const QString compressedSnippet = formatCompressedMemorySnippet(
        remainingSampleEntries,
        totalRemainingEntries,
        hiddenEntries
    );
    if (!compressedSnippet.trimmed().isEmpty()) {
        context.snippets.append(compressedSnippet);
        context.compressedEntries = totalRemainingEntries;
    }

    return context;
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
            "SET project_id = ?, entry_type = ?, content = ?, source = ?, tags = ?, relevance = ?, is_pinned = ? "
            "WHERE id = ?"
        );
        query.addBindValue(entry->projectId);
        query.addBindValue(entry->type);
        query.addBindValue(entry->content);
        query.addBindValue(entry->source);
        query.addBindValue(serializeTags(entry->tags));
        query.addBindValue(entry->relevance);
        query.addBindValue(entry->pinned ? 1 : 0);
        query.addBindValue(entry->id);
    } else {
        const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        query.prepare(
            "INSERT INTO memory_entries (project_id, entry_type, content, source, tags, relevance, is_pinned, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
        );
        query.addBindValue(entry->projectId);
        query.addBindValue(entry->type);
        query.addBindValue(entry->content);
        query.addBindValue(entry->source);
        query.addBindValue(serializeTags(entry->tags));
        query.addBindValue(entry->relevance);
        query.addBindValue(entry->pinned ? 1 : 0);
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
