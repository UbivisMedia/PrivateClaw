#include "tools/ToolExecutor.h"

#include "providers/ILlmProvider.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <limits>
#include <utility>

namespace privateclaw::tools {

namespace {

struct UnifiedDiffHunk
{
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    QStringList lines;
};

struct NetworkCallResult
{
    bool success = false;
    QByteArray payload;
    QString errorMessage;
    int statusCode = 0;
};

struct SanitizedModelResponse
{
    QString visibleText;
    QString reasoningText;
    bool hadReasoningTags = false;
};

struct ScopedSqliteConnection
{
    QString connectionName;
    QSqlDatabase database;

    ~ScopedSqliteConnection()
    {
        if (connectionName.isEmpty()) {
            return;
        }

        if (database.isValid()) {
            database.close();
        }

        database = QSqlDatabase();
        if (QSqlDatabase::contains(connectionName)) {
            QSqlDatabase::removeDatabase(connectionName);
        }
    }
};

struct MemoryQueryOptions
{
    qint64 projectId = -1;
    QString queryText;
    QString entryType;
    QStringList tags;
    int limit = 20;
};

struct MemoryQueryResult
{
    QList<domain::MemoryEntry> entries;
    QString errorMessage;
};

QString configString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

int configInt(const QJsonObject& object, const QString& key, const int fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toInt();
    }

    bool ok = false;
    const int parsed = value.toString().trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

bool configBool(const QJsonObject& object, const QString& key, const bool fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isBool()) {
        return value.toBool();
    }

    const QString text = value.toString().trimmed().toLower();
    if (text == "true" || text == "1" || text == "yes") {
        return true;
    }
    if (text == "false" || text == "0" || text == "no") {
        return false;
    }

    return fallback;
}

double configDouble(const QJsonObject& object, const QString& key, const double fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toDouble(fallback);
    }

    bool ok = false;
    const double parsed = value.toString().trimmed().toDouble(&ok);
    return ok ? parsed : fallback;
}

QStringList jsonArrayToStringList(const QJsonArray& array)
{
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue& value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

QStringList configStringList(const QJsonObject& object, const QString& key)
{
    QStringList values;
    const QJsonValue value = object.value(key);
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                values.append(text);
            }
        }
        return values;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return values;
    }

    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString normalized = part.trimmed();
        if (!normalized.isEmpty()) {
            values.append(normalized);
        }
    }

    return values;
}

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

QString normalizeLineEndings(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text;
}

QStringList splitLines(const QString& text, bool* trailingNewline = nullptr)
{
    QString normalized = normalizeLineEndings(text);
    const bool hasTrailingNewline = normalized.endsWith('\n');
    if (hasTrailingNewline) {
        normalized.chop(1);
    }

    if (trailingNewline != nullptr) {
        *trailingNewline = hasTrailingNewline;
    }

    if (normalized.isEmpty()) {
        return {};
    }

    return normalized.split('\n');
}

QString joinLines(const QStringList& lines, const bool trailingNewline)
{
    QString text = lines.join('\n');
    if (trailingNewline) {
        text += '\n';
    }
    return text;
}

bool openSqliteConnection(
    const QString& databasePath,
    const QString& purpose,
    ScopedSqliteConnection* connection,
    QString* errorMessage
)
{
    if (connection == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: SQLite-Verbindungspuffer fehlt.";
        }
        return false;
    }

    if (databasePath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Datenbankpfad fuer Tool-Zugriff konfiguriert.";
        }
        return false;
    }

    connection->connectionName = QString("privateclaw_tool_%1_%2")
        .arg(purpose, QUuid::createUuid().toString(QUuid::WithoutBraces));
    connection->database = QSqlDatabase::addDatabase("QSQLITE", connection->connectionName);
    connection->database.setDatabaseName(databasePath);
    if (!connection->database.open()) {
        if (errorMessage != nullptr) {
            *errorMessage = connection->database.lastError().text();
        }
        return false;
    }

    return true;
}

domain::MemoryEntry mapMemoryEntry(const QSqlQuery& query)
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

MemoryQueryResult queryMemoryEntries(const QSqlDatabase& database, const MemoryQueryOptions& options)
{
    MemoryQueryResult result;
    if (!database.isValid() || !database.isOpen()) {
        result.errorMessage = "SQLite-Verbindung fuer Memory-Zugriff ist nicht verfuegbar.";
        return result;
    }

    if (options.projectId <= 0) {
        result.errorMessage = "Memory-Tools brauchen ein gueltiges Projekt im Run-Kontext.";
        return result;
    }

    QString statement =
        "SELECT id, project_id, entry_type, content, source, tags, relevance, created_at "
        "FROM memory_entries "
        "WHERE project_id = ?";

    const QString normalizedQuery = options.queryText.trimmed().toLower();
    if (!normalizedQuery.isEmpty()) {
        statement +=
            " AND (LOWER(entry_type) LIKE ? OR LOWER(content) LIKE ? OR LOWER(source) LIKE ? OR LOWER(tags) LIKE ?)";
    }

    const QString normalizedEntryType = options.entryType.trimmed().toLower();
    if (!normalizedEntryType.isEmpty()) {
        statement += " AND LOWER(entry_type) = ?";
    }

    QStringList normalizedTags;
    for (const QString& tag : options.tags) {
        const QString normalizedTag = tag.trimmed().toLower();
        if (!normalizedTag.isEmpty()) {
            normalizedTags.append(normalizedTag);
        }
    }
    if (!normalizedTags.isEmpty()) {
        QStringList tagConditions;
        tagConditions.reserve(normalizedTags.size());
        for (int index = 0; index < normalizedTags.size(); ++index) {
            tagConditions.append("LOWER(tags) LIKE ?");
        }
        statement += " AND (" + tagConditions.join(" OR ") + ")";
    }

    statement += " ORDER BY relevance DESC, created_at DESC, id DESC";
    if (options.limit > 0) {
        statement += " LIMIT ?";
    }

    QSqlQuery query(database);
    query.prepare(statement);
    query.addBindValue(options.projectId);

    if (!normalizedQuery.isEmpty()) {
        const QString likeValue = "%" + normalizedQuery + "%";
        query.addBindValue(likeValue);
        query.addBindValue(likeValue);
        query.addBindValue(likeValue);
        query.addBindValue(likeValue);
    }

    if (!normalizedEntryType.isEmpty()) {
        query.addBindValue(normalizedEntryType);
    }

    for (const QString& tag : normalizedTags) {
        query.addBindValue("%" + tag + "%");
    }

    if (options.limit > 0) {
        query.addBindValue(options.limit);
    }

    if (!query.exec()) {
        result.errorMessage = query.lastError().text();
        return result;
    }

    while (query.next()) {
        result.entries.append(mapMemoryEntry(query));
    }

    return result;
}

QString previewText(QString text, const int maxChars = 220)
{
    text = text.simplified();
    if (maxChars > 0 && text.size() > maxChars) {
        text = text.left(qMax(1, maxChars - 3)) + "...";
    }
    return text;
}

QString formatMemoryEntries(
    const QList<domain::MemoryEntry>& entries,
    const bool fullContent,
    const int maxChars
)
{
    QStringList blocks;
    int consumedChars = 0;
    const int perEntryPreviewChars = fullContent ? 0 : 600;

    for (const domain::MemoryEntry& entry : entries) {
        QStringList blockLines;
        blockLines.append(QString("### MEMORY %1").arg(entry.id));
        blockLines.append(QString("Typ: %1").arg(entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed()));
        if (!entry.source.trimmed().isEmpty()) {
            blockLines.append(QString("Quelle: %1").arg(entry.source.trimmed()));
        }
        if (!entry.tags.isEmpty()) {
            blockLines.append(QString("Tags: %1").arg(entry.tags.join(", ")));
        }
        if (entry.createdAt.isValid()) {
            blockLines.append(QString("Erstellt: %1").arg(entry.createdAt.toUTC().toString(Qt::ISODate)));
        }

        const QString content = fullContent ? entry.content : previewText(entry.content, perEntryPreviewChars);
        blockLines.append("Inhalt:");
        blockLines.append(content);

        QString block = blockLines.join('\n');
        if (maxChars > 0 && consumedChars + block.size() > maxChars) {
            const int remainingChars = qMax(0, maxChars - consumedChars);
            if (remainingChars <= 0) {
                break;
            }
            block = block.left(remainingChars);
            if (!block.endsWith("...")) {
                block += "...";
            }
            blocks.append(block);
            break;
        }

        consumedChars += block.size() + 2;
        blocks.append(block);
    }

    return blocks.join("\n\n").trimmed();
}

QString cleanupVisibleText(QString text)
{
    text.replace(QRegularExpression("\n{3,}"), "\n\n");
    return text.trimmed();
}

SanitizedModelResponse sanitizeModelResponse(const QString& rawText)
{
    SanitizedModelResponse result;
    result.visibleText = rawText.trimmed();

    static const QRegularExpression reasoningBlockPattern(
        R"(<\s*(think|thinking|reasoning|analysis|thought|reflection)\b[^>]*>(.*?)<\s*/\s*\1\s*>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
    );
    static const QRegularExpression strayReasoningTagPattern(
        R"(<\s*/?\s*(think|thinking|reasoning|analysis|thought|reflection)\b[^>]*>)",
        QRegularExpression::CaseInsensitiveOption
    );

    QStringList reasoningParts;
    const QRegularExpressionMatchIterator matches = reasoningBlockPattern.globalMatch(result.visibleText);
    for (QRegularExpressionMatchIterator it = matches; it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString reasoning = cleanupVisibleText(match.captured(2));
        if (!reasoning.isEmpty()) {
            reasoningParts.append(reasoning);
        }
        result.hadReasoningTags = true;
    }

    result.visibleText.remove(reasoningBlockPattern);
    if (result.visibleText.contains(strayReasoningTagPattern)) {
        result.visibleText.remove(strayReasoningTagPattern);
        result.hadReasoningTags = true;
    }

    result.visibleText = cleanupVisibleText(result.visibleText);
    result.reasoningText = reasoningParts.join("\n\n").trimmed();
    return result;
}

QString serverErrorMessageFromPayload(const QByteArray& payload)
{
    const QJsonDocument json = QJsonDocument::fromJson(payload);
    if (!json.isObject()) {
        return {};
    }

    const QJsonObject root = json.object();
    const QJsonValue errorValue = root.value("error");
    if (errorValue.isString()) {
        return errorValue.toString().trimmed();
    }

    if (errorValue.isObject()) {
        return errorValue.toObject().value("message").toString().trimmed();
    }

    return root.value("message").toString().trimmed();
}

bool resolveJsonPath(
    const QJsonValue& rootValue,
    const QString& path,
    QJsonValue* resolvedValue,
    QString* errorMessage
)
{
    if (resolvedValue == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: JSON-Zielpuffer fehlt.";
        }
        return false;
    }

    QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        *resolvedValue = rootValue;
        return true;
    }

    QJsonValue currentValue = rootValue;
    int position = 0;
    while (position < trimmedPath.size()) {
        if (trimmedPath.at(position) == '.') {
            ++position;
            continue;
        }

        if (trimmedPath.at(position) == '[') {
            const int closingIndex = trimmedPath.indexOf(']', position);
            if (closingIndex <= position + 1) {
                if (errorMessage != nullptr) {
                    *errorMessage = QString("Ungueltiger JSON-Pfad bei Position %1.").arg(position + 1);
                }
                return false;
            }

            bool ok = false;
            const int arrayIndex = trimmedPath.mid(position + 1, closingIndex - position - 1).toInt(&ok);
            if (!ok || !currentValue.isArray()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "JSON-Pfad verweist auf keinen gueltigen Array-Index.";
                }
                return false;
            }

            const QJsonArray array = currentValue.toArray();
            if (arrayIndex < 0 || arrayIndex >= array.size()) {
                if (errorMessage != nullptr) {
                    *errorMessage = QString("Array-Index %1 liegt ausserhalb des gueltigen Bereichs.").arg(arrayIndex);
                }
                return false;
            }

            currentValue = array.at(arrayIndex);
            position = closingIndex + 1;
            continue;
        }

        int segmentEnd = position;
        while (segmentEnd < trimmedPath.size()
               && trimmedPath.at(segmentEnd) != '.'
               && trimmedPath.at(segmentEnd) != '[') {
            ++segmentEnd;
        }

        const QString key = trimmedPath.mid(position, segmentEnd - position).trimmed();
        if (key.isEmpty() || !currentValue.isObject()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("JSON-Pfad verweist auf keinen gueltigen Objektschluessel: %1").arg(key);
            }
            return false;
        }

        const QJsonObject object = currentValue.toObject();
        if (!object.contains(key)) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("JSON-Pfad-Schluessel nicht gefunden: %1").arg(key);
            }
            return false;
        }

        currentValue = object.value(key);
        position = segmentEnd;
    }

    *resolvedValue = currentValue;
    return true;
}

QChar csvDelimiterFromConfig(const QJsonObject& config)
{
    QString delimiterText = configString(config, "delimiter");
    if (delimiterText == "\\t" || delimiterText.compare("tab", Qt::CaseInsensitive) == 0) {
        return '\t';
    }
    if (delimiterText.isEmpty()) {
        return ',';
    }
    return delimiterText.front();
}

QString encodeCsvField(const QString& value, const QChar delimiter)
{
    QString field = value;
    field.replace("\"", "\"\"");
    const bool needsQuotes = field.contains(delimiter)
        || field.contains('"')
        || field.contains('\n')
        || field.contains('\r');
    return needsQuotes ? QString("\"%1\"").arg(field) : field;
}

bool parseCsvRow(
    const QString& line,
    const QChar delimiter,
    QStringList* fields,
    QString* errorMessage
)
{
    if (fields == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: CSV-Zielpuffer fehlt.";
        }
        return false;
    }

    fields->clear();
    QString currentField;
    bool insideQuotes = false;

    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (insideQuotes) {
            if (character == '"') {
                if (index + 1 < line.size() && line.at(index + 1) == '"') {
                    currentField += '"';
                    ++index;
                } else {
                    insideQuotes = false;
                }
            } else {
                currentField += character;
            }
            continue;
        }

        if (character == '"') {
            insideQuotes = true;
        } else if (character == delimiter) {
            fields->append(currentField);
            currentField.clear();
        } else {
            currentField += character;
        }
    }

    if (insideQuotes) {
        if (errorMessage != nullptr) {
            *errorMessage = "CSV-Zeile enthaelt einen nicht geschlossenen String.";
        }
        return false;
    }

    fields->append(currentField);
    return true;
}

QUrl buildEndpoint(const QString& baseUrl, const QString& suffix)
{
    QUrl url(baseUrl);
    QString path = url.path().trimmed();
    const QString normalizedSuffix = suffix.startsWith('/') ? suffix : QString("/%1").arg(suffix);

    if (path.isEmpty() || path == "/") {
        path = normalizedSuffix;
    } else {
        if (path.endsWith('/')) {
            path.chop(1);
        }

        if (path.endsWith(normalizedSuffix)) {
            url.setPath(path);
            return url;
        }

        path += normalizedSuffix;
    }

    url.setPath(path);
    return url;
}

NetworkCallResult waitForReply(QNetworkReply* reply, const int timeoutMs)
{
    NetworkCallResult result;
    if (reply == nullptr) {
        result.errorMessage = "Netzwerk-Reply fehlt.";
        return result;
    }

    QEventLoop loop;
    QTimer timeoutTimer;

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (timeoutMs > 0) {
        timeoutTimer.setInterval(timeoutMs);
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeoutTimer.start();
    }

    loop.exec();

    if (timeoutMs > 0 && !timeoutTimer.isActive()) {
        reply->abort();
        result.errorMessage = "Zeitueberschreitung bei der Netzwerkanfrage.";
        reply->deleteLater();
        return result;
    }

    if (timeoutMs > 0) {
        timeoutTimer.stop();
    }

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.payload = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = serverErrorMessageFromPayload(result.payload);
        result.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return result;
    }

    reply->deleteLater();
    result.success = true;
    return result;
}

bool parseUnifiedDiff(
    const QString& diffText,
    QString* targetPathFromPatch,
    QList<UnifiedDiffHunk>* hunks,
    QString* errorMessage
)
{
    if (hunks == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Patch-Speicher fehlt.";
        }
        return false;
    }

    hunks->clear();
    QString parsedTargetPath;
    const QString normalizedDiff = normalizeLineEndings(diffText);
    QStringList lines = normalizedDiff.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }

    static const QRegularExpression hunkPattern("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@");

    int index = 0;
    while (index < lines.size()) {
        const QString& line = lines.at(index);

        if (line.startsWith("+++ ")) {
            QString patchPath = line.mid(4).trimmed();
            if (patchPath.startsWith("b/")) {
                patchPath = patchPath.mid(2);
            }
            parsedTargetPath = patchPath;
            ++index;
            continue;
        }

        if (!line.startsWith("@@")) {
            ++index;
            continue;
        }

        const QRegularExpressionMatch match = hunkPattern.match(line);
        if (!match.hasMatch()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Ungueltiger Hunk-Header: %1").arg(line);
            }
            return false;
        }

        UnifiedDiffHunk hunk;
        hunk.oldStart = match.captured(1).toInt();
        hunk.oldCount = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
        hunk.newStart = match.captured(3).toInt();
        hunk.newCount = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();

        ++index;
        while (index < lines.size()) {
            const QString& hunkLine = lines.at(index);
            if (hunkLine.startsWith("@@")) {
                break;
            }

            if (hunkLine.startsWith("--- ") || hunkLine.startsWith("+++ ")) {
                break;
            }

            if (hunkLine.startsWith('\\')) {
                ++index;
                continue;
            }

            if (!hunkLine.isEmpty()) {
                const QChar prefix = hunkLine.front();
                if (prefix != ' ' && prefix != '+' && prefix != '-') {
                    if (errorMessage != nullptr) {
                        *errorMessage = QString("Ungueltige Diff-Zeile: %1").arg(hunkLine);
                    }
                    return false;
                }
            }

            hunk.lines.append(hunkLine);
            ++index;
        }

        hunks->append(hunk);
    }

    if (hunks->isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Diff enthaelt keine anwendbaren Hunks.";
        }
        return false;
    }

    if (targetPathFromPatch != nullptr) {
        *targetPathFromPatch = parsedTargetPath;
    }
    return true;
}

bool applyUnifiedDiffToText(
    const QString& originalText,
    const QList<UnifiedDiffHunk>& hunks,
    QString* patchedText,
    QString* errorMessage
)
{
    if (patchedText == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Zielpuffer fuer Diff fehlt.";
        }
        return false;
    }

    bool hadTrailingNewline = false;
    const QStringList originalLines = splitLines(originalText, &hadTrailingNewline);
    QStringList outputLines;
    int sourceIndex = 0;

    for (const UnifiedDiffHunk& hunk : hunks) {
        const int targetIndex = qMax(0, hunk.oldStart - 1);
        if (targetIndex < sourceIndex) {
            if (errorMessage != nullptr) {
                *errorMessage = "Diff-Hunks ueberlappen oder sind unsortiert.";
            }
            return false;
        }

        while (sourceIndex < targetIndex && sourceIndex < originalLines.size()) {
            outputLines.append(originalLines.at(sourceIndex));
            ++sourceIndex;
        }

        int consumedOldLines = 0;
        int producedNewLines = 0;

        for (const QString& line : hunk.lines) {
            if (line.isEmpty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Diff-Zeile ohne Praefix gefunden.";
                }
                return false;
            }

            const QChar prefix = line.front();
            const QString text = line.mid(1);

            if (prefix == ' ') {
                if (sourceIndex >= originalLines.size() || originalLines.at(sourceIndex) != text) {
                    if (errorMessage != nullptr) {
                        *errorMessage = QString(
                            "Kontextzeile passt nicht zum Dateiinhalt: %1"
                        ).arg(text);
                    }
                    return false;
                }
                outputLines.append(originalLines.at(sourceIndex));
                ++sourceIndex;
                ++consumedOldLines;
                ++producedNewLines;
                continue;
            }

            if (prefix == '-') {
                if (sourceIndex >= originalLines.size() || originalLines.at(sourceIndex) != text) {
                    if (errorMessage != nullptr) {
                        *errorMessage = QString(
                            "Zu entfernende Zeile passt nicht zum Dateiinhalt: %1"
                        ).arg(text);
                    }
                    return false;
                }
                ++sourceIndex;
                ++consumedOldLines;
                continue;
            }

            if (prefix == '+') {
                outputLines.append(text);
                ++producedNewLines;
                continue;
            }

            if (errorMessage != nullptr) {
                *errorMessage = QString("Unbekannter Diff-Praefix '%1'.").arg(prefix);
            }
            return false;
        }

        if (hunk.oldCount >= 0 && consumedOldLines != hunk.oldCount) {
            if (errorMessage != nullptr) {
                *errorMessage = QString(
                    "Diff-Hunk erwartet %1 alte Zeilen, verarbeitet wurden aber %2."
                ).arg(hunk.oldCount).arg(consumedOldLines);
            }
            return false;
        }

        if (hunk.newCount >= 0 && producedNewLines != hunk.newCount) {
            if (errorMessage != nullptr) {
                *errorMessage = QString(
                    "Diff-Hunk erwartet %1 neue Zeilen, erzeugt wurden aber %2."
                ).arg(hunk.newCount).arg(producedNewLines);
            }
            return false;
        }
    }

    while (sourceIndex < originalLines.size()) {
        outputLines.append(originalLines.at(sourceIndex));
        ++sourceIndex;
    }

    *patchedText = joinLines(outputLines, hadTrailingNewline);
    return true;
}

QJsonObject extractComfyHistoryEntry(const QJsonObject& rootObject, const QString& promptId)
{
    const QJsonValue promptValue = rootObject.value(promptId);
    if (promptValue.isObject()) {
        return promptValue.toObject();
    }

    if (rootObject.contains("outputs") || rootObject.contains("status")) {
        return rootObject;
    }

    return {};
}

QString sanitizedUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int generatedSeed()
{
    return static_cast<int>(QRandomGenerator::global()->bounded(std::numeric_limits<int>::max()));
}

QString prepareComfyInputImageReference(
    const QString& imageValue,
    const QString& workspaceRoot,
    const QString& comfyUiBaseUrl,
    QString* errorMessage,
    QStringList* logs
)
{
    const QString trimmedValue = imageValue.trimmed();
    if (trimmedValue.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "ComfyUI-Bildpfad darf nicht leer sein.";
        }
        return {};
    }

    QFileInfo fileInfo(trimmedValue);
    QString absolutePath;
    if (fileInfo.isAbsolute() && fileInfo.exists()) {
        absolutePath = fileInfo.absoluteFilePath();
    } else {
        const QString workspaceCandidate = QDir(workspaceRoot).absoluteFilePath(trimmedValue);
        if (QFileInfo::exists(workspaceCandidate)) {
            absolutePath = workspaceCandidate;
        }
    }

    if (absolutePath.isEmpty()) {
        return trimmedValue;
    }

    auto* file = new QFile(absolutePath);
    if (!file->open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Bild konnte nicht gelesen werden: %1").arg(absolutePath);
        }
        file->deleteLater();
        return {};
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart imagePart;
    imagePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QVariant(QString("form-data; name=\"image\"; filename=\"%1\"").arg(QFileInfo(absolutePath).fileName()))
    );
    imagePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(imagePart);

    QHttpPart overwritePart;
    overwritePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"overwrite\""));
    overwritePart.setBody("true");
    multiPart->append(overwritePart);

    QHttpPart typePart;
    typePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"type\""));
    typePart.setBody("input");
    multiPart->append(typePart);

    QNetworkAccessManager networkManager;
    QNetworkRequest request(buildEndpoint(comfyUiBaseUrl, "/upload/image"));
    QNetworkReply* reply = networkManager.post(request, multiPart);
    multiPart->setParent(reply);

    const NetworkCallResult uploadResult = waitForReply(reply, 15000);
    if (!uploadResult.success) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("ComfyUI-Bildupload fehlgeschlagen: %1").arg(uploadResult.errorMessage);
        }
        return {};
    }

    const QJsonDocument uploadDocument = QJsonDocument::fromJson(uploadResult.payload);
    const QJsonObject uploadObject = uploadDocument.object();
    const QString uploadedName = uploadObject.value("name").toString().trimmed();
    const QString uploadedSubfolder = uploadObject.value("subfolder").toString().trimmed();
    if (uploadedName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "ComfyUI-Bildupload lieferte keinen Dateinamen zurueck.";
        }
        return {};
    }

    if (logs != nullptr) {
        logs->append(QString("ComfyUI-Bild hochgeladen: %1").arg(QFileInfo(absolutePath).fileName()));
    }
    return uploadedSubfolder.isEmpty() ? uploadedName : uploadedSubfolder + "/" + uploadedName;
}

QJsonObject buildComfyGeneratedImageWorkflow(
    const QJsonObject& config,
    const QString& workspaceRoot,
    const QString& comfyUiBaseUrl,
    QString* errorMessage,
    QStringList* logs
)
{
    const QString builderMode = configString(config, "builder_mode").toLower().isEmpty()
        ? "txt2img"
        : configString(config, "builder_mode").toLower();
    const QString checkpoint = configString(config, "checkpoint");
    if (checkpoint.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "ComfyUI txt2img braucht einen Checkpoint.";
        }
        return {};
    }

    const QString positivePrompt = configString(config, "positive_prompt");
    const QString negativePrompt = configString(config, "negative_prompt");
    const int width = qBound(16, configInt(config, "width", 1024), 16384);
    const int height = qBound(16, configInt(config, "height", 1024), 16384);
    const int batchSize = qBound(1, configInt(config, "batch_size", 1), 256);
    const int steps = qBound(1, configInt(config, "steps", 20), 10000);
    const double cfg = qBound(0.0, configDouble(config, "cfg", 8.0), 100.0);
    const double denoise = qBound(0.0, configDouble(config, "denoise", 1.0), 1.0);
    const QString samplerName = configString(config, "sampler_name").isEmpty()
        ? "euler"
        : configString(config, "sampler_name");
    const QString scheduler = configString(config, "scheduler").isEmpty()
        ? "normal"
        : configString(config, "scheduler");
    const QString filenamePrefix = configString(config, "filename_prefix").isEmpty()
        ? "PrivateClaw"
        : configString(config, "filename_prefix");
    const int clipSkip = configInt(config, "clip_skip", -1);
    int seed = qMax(0, configInt(config, "seed", 0));
    if (configBool(config, "randomize_seed", false)) {
        seed = generatedSeed();
        if (logs != nullptr) {
            logs->append(QString("ComfyUI txt2img: Seed zufaellig erzeugt (%1).").arg(seed));
        }
    }

    QJsonObject workflow;
    int nextNodeId = 3;
    const auto makeNodeId = [&nextNodeId]() {
        return QString::number(nextNodeId++);
    };

    const QString checkpointNodeId = makeNodeId();
    workflow.insert(
        checkpointNodeId,
        QJsonObject{
            { "class_type", "CheckpointLoaderSimple" },
            { "inputs", QJsonObject{ { "ckpt_name", checkpoint } } }
        }
    );

    QString currentModelNodeId = checkpointNodeId;
    int currentModelOutputIndex = 0;
    QString currentClipNodeId = checkpointNodeId;
    int currentClipOutputIndex = 1;
    QString currentVaeNodeId = checkpointNodeId;
    int currentVaeOutputIndex = 2;

    const QJsonArray loraArray = config.value("loras").toArray();
    int appliedLoras = 0;
    for (const QJsonValue& loraValue : loraArray) {
        if (!loraValue.isObject()) {
            continue;
        }

        const QJsonObject loraObject = loraValue.toObject();
        if (!configBool(loraObject, "enabled", true)) {
            continue;
        }

        const QString loraName = configString(loraObject, "name");
        if (loraName.isEmpty()) {
            continue;
        }

        const QString loraNodeId = makeNodeId();
        workflow.insert(
            loraNodeId,
            QJsonObject{
                { "class_type", "LoraLoader" },
                { "inputs",
                  QJsonObject{
                      { "model", QJsonArray{ currentModelNodeId, currentModelOutputIndex } },
                      { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } },
                      { "lora_name", loraName },
                      { "strength_model", configDouble(loraObject, "strength_model", 1.0) },
                      { "strength_clip", configDouble(loraObject, "strength_clip", 1.0) }
                  } }
            }
        );
        currentModelNodeId = loraNodeId;
        currentModelOutputIndex = 0;
        currentClipNodeId = loraNodeId;
        currentClipOutputIndex = 1;
        ++appliedLoras;
    }

    if (logs != nullptr) {
        logs->append(QString("ComfyUI txt2img: %1 LoRA(s) aktiv.").arg(appliedLoras));
    }

    if (clipSkip != -1) {
        const QString clipSkipNodeId = makeNodeId();
        workflow.insert(
            clipSkipNodeId,
            QJsonObject{
                { "class_type", "CLIPSetLastLayer" },
                { "inputs",
                  QJsonObject{
                      { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } },
                      { "stop_at_clip_layer", clipSkip }
                  } }
            }
        );
        currentClipNodeId = clipSkipNodeId;
        currentClipOutputIndex = 0;
    }

    const QString positiveNodeId = makeNodeId();
    workflow.insert(
        positiveNodeId,
        QJsonObject{
            { "class_type", "CLIPTextEncode" },
            { "inputs",
              QJsonObject{
                  { "text", positivePrompt },
                  { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } }
              } }
        }
    );

    const QString negativeNodeId = makeNodeId();
    workflow.insert(
        negativeNodeId,
        QJsonObject{
            { "class_type", "CLIPTextEncode" },
            { "inputs",
              QJsonObject{
                  { "text", negativePrompt },
                  { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } }
              } }
        }
    );

    const QString vaeOverride = configString(config, "vae_name");
    if (!vaeOverride.isEmpty()) {
        const QString vaeLoaderNodeId = makeNodeId();
        workflow.insert(
            vaeLoaderNodeId,
            QJsonObject{
                { "class_type", "VAELoader" },
                { "inputs", QJsonObject{ { "vae_name", vaeOverride } } }
            }
        );
        currentVaeNodeId = vaeLoaderNodeId;
        currentVaeOutputIndex = 0;
    }

    QString latentSourceNodeId;
    int latentSourceOutputIndex = 0;
    if (builderMode == "img2img" || builderMode == "inpainting") {
        QString prepareError;
        const QString preparedInputImage = prepareComfyInputImageReference(
            configString(config, "input_image"),
            workspaceRoot,
            comfyUiBaseUrl,
            &prepareError,
            logs
        );
        if (preparedInputImage.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = prepareError.isEmpty()
                    ? "ComfyUI braucht ein gueltiges Startbild."
                    : prepareError;
            }
            return {};
        }

        const QString loadImageNodeId = makeNodeId();
        workflow.insert(
            loadImageNodeId,
            QJsonObject{
                { "class_type", "LoadImage" },
                { "inputs", QJsonObject{ { "image", preparedInputImage } } }
            }
        );

        if (builderMode == "inpainting") {
            QString maskError;
            const QString preparedMaskImage = prepareComfyInputImageReference(
                configString(config, "mask_image"),
                workspaceRoot,
                comfyUiBaseUrl,
                &maskError,
                logs
            );
            if (preparedMaskImage.isEmpty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = maskError.isEmpty()
                        ? "ComfyUI-Inpainting braucht ein Maskenbild."
                        : maskError;
                }
                return {};
            }

            const QString maskNodeId = makeNodeId();
            workflow.insert(
                maskNodeId,
                QJsonObject{
                    { "class_type", "LoadImageMask" },
                    { "inputs",
                      QJsonObject{
                          { "image", preparedMaskImage },
                          { "channel", configString(config, "mask_channel").isEmpty()
                                ? "alpha"
                                : configString(config, "mask_channel") }
                      } }
                }
            );

            const QString encodeNodeId = makeNodeId();
            workflow.insert(
                encodeNodeId,
                QJsonObject{
                    { "class_type", "VAEEncodeForInpaint" },
                    { "inputs",
                      QJsonObject{
                          { "pixels", QJsonArray{ loadImageNodeId, 0 } },
                          { "vae", QJsonArray{ currentVaeNodeId, currentVaeOutputIndex } },
                          { "mask", QJsonArray{ maskNodeId, 0 } },
                          { "grow_mask_by", qMax(0, configInt(config, "mask_grow_by", 6)) }
                      } }
                }
            );
            latentSourceNodeId = encodeNodeId;
        } else {
            const QString encodeNodeId = makeNodeId();
            workflow.insert(
                encodeNodeId,
                QJsonObject{
                    { "class_type", "VAEEncode" },
                    { "inputs",
                      QJsonObject{
                          { "pixels", QJsonArray{ loadImageNodeId, 0 } },
                          { "vae", QJsonArray{ currentVaeNodeId, currentVaeOutputIndex } }
                      } }
                }
            );
            latentSourceNodeId = encodeNodeId;
        }
    } else {
        const QString latentNodeId = makeNodeId();
        workflow.insert(
            latentNodeId,
            QJsonObject{
                { "class_type", "EmptyLatentImage" },
                { "inputs",
                  QJsonObject{
                      { "width", width },
                      { "height", height },
                      { "batch_size", batchSize }
                  } }
            }
        );
        latentSourceNodeId = latentNodeId;
    }

    const QString samplerNodeId = makeNodeId();
    workflow.insert(
        samplerNodeId,
        QJsonObject{
            { "class_type", "KSampler" },
            { "inputs",
              QJsonObject{
                  { "model", QJsonArray{ currentModelNodeId, currentModelOutputIndex } },
                  { "seed", seed },
                  { "steps", steps },
                  { "cfg", cfg },
                  { "sampler_name", samplerName },
                  { "scheduler", scheduler },
                  { "positive", QJsonArray{ positiveNodeId, 0 } },
                  { "negative", QJsonArray{ negativeNodeId, 0 } },
                  { "latent_image", QJsonArray{ latentSourceNodeId, latentSourceOutputIndex } },
                  { "denoise", denoise }
              } }
        }
    );

    const QString decodeNodeId = makeNodeId();
    workflow.insert(
        decodeNodeId,
        QJsonObject{
            { "class_type", "VAEDecode" },
            { "inputs",
              QJsonObject{
                  { "samples", QJsonArray{ samplerNodeId, 0 } },
                  { "vae", QJsonArray{ currentVaeNodeId, currentVaeOutputIndex } }
              } }
        }
    );

    const QString saveNodeId = makeNodeId();
    workflow.insert(
        saveNodeId,
        QJsonObject{
            { "class_type", "SaveImage" },
            { "inputs",
              QJsonObject{
                  { "images", QJsonArray{ decodeNodeId, 0 } },
                  { "filename_prefix", filenamePrefix }
              } }
        }
    );

    if (logs != nullptr) {
        logs->append(QString("ComfyUI %1: Workflow aus Formular-Daten erzeugt (%2x%3, %4 Schritte).")
                         .arg(builderMode)
                         .arg(width)
                         .arg(height)
                         .arg(steps));
    }
    return workflow;
}

bool looksBinary(const QByteArray& payload)
{
    const int inspectionLength = qMin(payload.size(), 4096);
    for (int index = 0; index < inspectionLength; ++index) {
        if (payload.at(index) == '\0') {
            return true;
        }
    }

    return false;
}

QString normalizedExtensionFilter(QString extension)
{
    extension = extension.trimmed().toLower();
    if (extension.startsWith("*.")) {
        extension = extension.mid(1);
    } else if (!extension.startsWith('.')) {
        extension.prepend('.');
    }

    return extension;
}

bool matchesDirectoryReadFilters(
    const QString& relativePath,
    const QStringList& includeExtensions,
    const QStringList& excludeFragments
)
{
    const QString normalizedPath = relativePath.toLower();

    for (const QString& fragment : excludeFragments) {
        if (normalizedPath.contains(fragment.toLower())) {
            return false;
        }
    }

    if (includeExtensions.isEmpty()) {
        return true;
    }

    for (const QString& extension : includeExtensions) {
        if (normalizedPath.endsWith(extension)) {
            return true;
        }
    }

    return false;
}

bool isSafeShellProgram(const QString& programName, const QStringList& arguments, QString* errorMessage)
{
    const QString normalizedProgram = programName.trimmed().toLower();
    if (normalizedProgram.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "shell.run braucht ein ausfuehrbares Programm.";
        }
        return false;
    }

    const QStringList allowedPrograms{
        "rg",
        "git",
        "cmake",
        "ctest",
        "where"
    };
    if (!allowedPrograms.contains(normalizedProgram)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString(
                "Programm '%1' ist in shell.run nicht freigegeben. Erlaubt sind nur %2."
            ).arg(programName, allowedPrograms.join(", "));
        }
        return false;
    }

    const QStringList blockedArguments{
        "&&",
        "||",
        "|",
        ">",
        ">>",
        "<",
        ";"
    };
    for (const QString& argument : arguments) {
        if (blockedArguments.contains(argument)) {
            if (errorMessage != nullptr) {
                *errorMessage = "shell.run blockiert Shell-Verkettungen und Umleitungen.";
            }
            return false;
        }
    }

    if (normalizedProgram == "git") {
        if (arguments.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = "shell.run braucht fuer git einen expliziten Unterbefehl.";
            }
            return false;
        }

        const QString subcommand = arguments.first().trimmed().toLower();
        const QStringList allowedGitSubcommands{
            "status",
            "diff",
            "show",
            "log",
            "branch",
            "rev-parse",
            "ls-files",
            "grep"
        };
        if (!allowedGitSubcommands.contains(subcommand)) {
            if (errorMessage != nullptr) {
                *errorMessage = QString(
                    "Git-Unterbefehl '%1' ist in shell.run nicht freigegeben."
                ).arg(subcommand);
            }
            return false;
        }
    }

    return true;
}

} // namespace

ToolExecutor::ToolExecutor(
    QString workspaceRoot,
    QString comfyUiBaseUrl,
    QString databasePath,
    QStringList allowedToolPaths
)
    : m_workspaceRoot(std::move(workspaceRoot))
    , m_comfyUiBaseUrl(std::move(comfyUiBaseUrl))
    , m_databasePath(std::move(databasePath))
    , m_allowedToolPaths(std::move(allowedToolPaths))
{
    if (m_workspaceRoot.trimmed().isEmpty()) {
        m_workspaceRoot = QDir::currentPath();
    }

    m_workspaceRoot = QDir(m_workspaceRoot).absolutePath();
    QStringList normalizedAllowedPaths;
    QStringList seenPaths;
    const auto appendAllowedPath = [this, &normalizedAllowedPaths, &seenPaths](const QString& configuredPath) {
        if (configuredPath.trimmed().isEmpty()) {
            return;
        }

        const QFileInfo fileInfo(configuredPath);
        const QString absolutePath = fileInfo.isAbsolute()
            ? fileInfo.absoluteFilePath()
            : QDir(m_workspaceRoot).absoluteFilePath(configuredPath);
        const QString normalizedPath = QDir::cleanPath(absolutePath);
        const QString foldedPath = normalizedPath.toCaseFolded();
        if (seenPaths.contains(foldedPath)) {
            return;
        }

        seenPaths.append(foldedPath);
        normalizedAllowedPaths.append(normalizedPath);
    };

    appendAllowedPath(m_workspaceRoot);
    for (const QString& configuredPath : std::as_const(m_allowedToolPaths)) {
        appendAllowedPath(configuredPath);
    }
    m_allowedToolPaths = normalizedAllowedPaths;

    if (m_databasePath.trimmed().isEmpty()) {
        const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!dataDirectory.trimmed().isEmpty()) {
            m_databasePath = QDir(dataDirectory).filePath("privateclaw.sqlite");
        }
    }
}

QStringList ToolExecutor::availableTools() const
{
    return {
        "file.read",
        "json.extract",
        "csv.read",
        "csv.write",
        "directory.read_recursive",
        "directory.read_changed",
        "directory.list",
        "memory.search",
        "memory.summarize",
        "memory.delete_old",
        "memory.ingest_directory",
        "file.write_text",
        "file.edit_diff",
        "http.request",
        "shell.run",
        "comfyui.workflow"
    };
}

ToolExecutionResult ToolExecutor::execute(const ToolExecutionRequest& request) const
{
    const QString toolName = request.toolName.trimmed().toLower();
    if (toolName == "file.read") {
        return executeFileRead(request.config);
    }

    if (toolName == "json.extract") {
        return executeJsonExtract(request.config);
    }

    if (toolName == "csv.read") {
        return executeCsvRead(request.config);
    }

    if (toolName == "csv.write") {
        return executeCsvWrite(request.config);
    }

    if (toolName == "directory.read_recursive") {
        return executeDirectoryReadRecursive(request.config);
    }

    if (toolName == "directory.read_changed") {
        return executeDirectoryReadChanged(request.config);
    }

    if (toolName == "directory.list") {
        return executeDirectoryList(request.config);
    }

    if (toolName == "memory.search") {
        return executeMemorySearch(request);
    }

    if (toolName == "memory.summarize") {
        return executeMemorySummarize(request);
    }

    if (toolName == "memory.delete_old") {
        return executeMemoryDeleteOld(request);
    }

    if (toolName == "memory.ingest_directory") {
        return executeMemoryIngestDirectory(request);
    }

    if (toolName == "file.write_text") {
        return executeFileWriteText(request.config);
    }

    if (toolName == "file.edit_diff") {
        return executeFileEditDiff(request.config);
    }

    if (toolName == "http.request") {
        return executeHttpRequest(request.config);
    }

    if (toolName == "shell.run") {
        return executeShellRun(request.config);
    }

    if (toolName == "comfyui.workflow") {
        return executeComfyUiWorkflow(request.config);
    }

    ToolExecutionResult result;
    result.errorMessage = QString("Unbekanntes Tool '%1'.").arg(request.toolName);
    return result;
}

QString ToolExecutor::workspaceRoot() const
{
    return m_workspaceRoot;
}

QString ToolExecutor::comfyUiBaseUrl() const
{
    return m_comfyUiBaseUrl;
}

QString ToolExecutor::databasePath() const
{
    return m_databasePath;
}

ToolExecutionResult ToolExecutor::executeFileRead(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absolutePath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absolutePath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QString("Datei konnte nicht gelesen werden: %1").arg(file.errorString());
        return result;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    bool hadTrailingNewline = false;
    const QStringList lines = splitLines(content, &hadTrailingNewline);
    const int lineStart = qMax(1, configInt(config, "line_start", 1));
    const int lineEnd = qMax(lineStart, configInt(config, "line_end", lines.isEmpty() ? lineStart : lines.size()));

    QString selectedContent;
    if (lines.isEmpty()) {
        selectedContent.clear();
    } else {
        QStringList selectedLines;
        for (int index = lineStart - 1; index < lines.size() && index < lineEnd; ++index) {
            selectedLines.append(lines.at(index));
        }
        selectedContent = joinLines(selectedLines, hadTrailingNewline && lineEnd >= lines.size());
    }

    const int maxChars = qMax(0, configInt(config, "max_chars", 20000));
    bool truncated = false;
    if (maxChars > 0 && selectedContent.size() > maxChars) {
        selectedContent = selectedContent.left(maxChars);
        truncated = true;
    }

    result.success = true;
    result.outputText = selectedContent;
    result.logs.append(QString("Datei gelesen: %1").arg(absolutePath));
    result.logs.append(QString("Zeilenbereich: %1-%2").arg(lineStart).arg(lineEnd));
    if (truncated) {
        result.logs.append(QString("Inhalt auf %1 Zeichen gekuerzt.").arg(maxChars));
    }
    return result;
}

ToolExecutionResult ToolExecutor::executeJsonExtract(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString inputJson = config.value("input").toString();
    if (inputJson.trimmed().isEmpty()) {
        result.errorMessage = "Tool 'json.extract' braucht ein Feld 'input' mit JSON-Inhalt.";
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(inputJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        result.errorMessage = QString("JSON konnte nicht geparst werden: %1").arg(parseError.errorString());
        return result;
    }

    QJsonValue rootValue;
    if (document.isObject()) {
        rootValue = document.object();
    } else if (document.isArray()) {
        rootValue = document.array();
    } else {
        result.errorMessage = "json.extract erwartet ein JSON-Objekt oder JSON-Array.";
        return result;
    }

    QString resolveError;
    QJsonValue resolvedValue;
    if (!resolveJsonPath(rootValue, configString(config, "path"), &resolvedValue, &resolveError)) {
        result.errorMessage = resolveError;
        return result;
    }

    const bool pretty = configBool(config, "pretty", true);
    if (resolvedValue.isString()) {
        result.outputText = resolvedValue.toString();
    } else if (resolvedValue.isDouble()) {
        result.outputText = QString::number(resolvedValue.toDouble());
    } else if (resolvedValue.isBool()) {
        result.outputText = resolvedValue.toBool() ? "true" : "false";
    } else if (resolvedValue.isNull() || resolvedValue.isUndefined()) {
        result.outputText = "null";
    } else if (resolvedValue.isObject()) {
        result.outputText = QString::fromUtf8(
            QJsonDocument(resolvedValue.toObject()).toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact)
        );
    } else if (resolvedValue.isArray()) {
        result.outputText = QString::fromUtf8(
            QJsonDocument(resolvedValue.toArray()).toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact)
        );
    }

    result.success = true;
    result.logs.append(QString("JSON erfolgreich extrahiert: %1").arg(configString(config, "path").isEmpty() ? "<root>" : configString(config, "path")));
    return result;
}

ToolExecutionResult ToolExecutor::executeCsvRead(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absolutePath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absolutePath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QString("CSV-Datei konnte nicht gelesen werden: %1").arg(file.errorString());
        return result;
    }

    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    bool hadTrailingNewline = false;
    QStringList lines = splitLines(content, &hadTrailingNewline);
    Q_UNUSED(hadTrailingNewline);
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
        lines.removeLast();
    }

    if (lines.isEmpty()) {
        result.errorMessage = "CSV-Datei ist leer.";
        return result;
    }

    const QChar delimiter = csvDelimiterFromConfig(config);
    const bool hasHeader = configBool(config, "has_header", true);
    const int maxRows = qMax(1, configInt(config, "max_rows", 200));
    const QString outputFormat = configString(config, "output_format").isEmpty()
        ? "json"
        : configString(config, "output_format").toLower();

    QStringList headers;
    int startIndex = 0;
    QString rowError;
    if (hasHeader) {
        if (!parseCsvRow(lines.first(), delimiter, &headers, &rowError)) {
            result.errorMessage = QString("CSV-Header konnte nicht gelesen werden: %1").arg(rowError);
            return result;
        }
        for (int index = 0; index < headers.size(); ++index) {
            if (headers.at(index).trimmed().isEmpty()) {
                headers[index] = QString("column_%1").arg(index + 1);
            }
        }
        startIndex = 1;
    }

    QJsonArray jsonRows;
    QStringList textRows;
    int readRows = 0;
    for (int lineIndex = startIndex; lineIndex < lines.size() && readRows < maxRows; ++lineIndex) {
        QStringList fields;
        if (!parseCsvRow(lines.at(lineIndex), delimiter, &fields, &rowError)) {
            result.errorMessage = QString("CSV-Zeile %1 konnte nicht gelesen werden: %2").arg(lineIndex + 1).arg(rowError);
            return result;
        }

        if (outputFormat == "text") {
            QStringList pairs;
            if (hasHeader) {
                for (int columnIndex = 0; columnIndex < fields.size(); ++columnIndex) {
                    const QString header = columnIndex < headers.size()
                        ? headers.at(columnIndex)
                        : QString("column_%1").arg(columnIndex + 1);
                    pairs.append(QString("%1=%2").arg(header, fields.at(columnIndex)));
                }
            } else {
                for (int columnIndex = 0; columnIndex < fields.size(); ++columnIndex) {
                    pairs.append(QString("c%1=%2").arg(columnIndex + 1).arg(fields.at(columnIndex)));
                }
            }
            textRows.append(pairs.join(" | "));
        } else if (hasHeader) {
            QJsonObject rowObject;
            for (int columnIndex = 0; columnIndex < fields.size(); ++columnIndex) {
                const QString header = columnIndex < headers.size()
                    ? headers.at(columnIndex)
                    : QString("column_%1").arg(columnIndex + 1);
                rowObject.insert(header, fields.at(columnIndex));
            }
            jsonRows.append(rowObject);
        } else {
            QJsonArray rowArray;
            for (const QString& field : fields) {
                rowArray.append(field);
            }
            jsonRows.append(rowArray);
        }

        ++readRows;
    }

    if (outputFormat == "text") {
        result.outputText = textRows.join('\n');
    } else {
        result.outputText = QString::fromUtf8(QJsonDocument(jsonRows).toJson(QJsonDocument::Indented));
    }

    result.success = true;
    result.logs.append(QString("CSV-Datei gelesen: %1").arg(absolutePath));
    result.logs.append(QString("Zeilen im Ergebnis: %1").arg(readRows));
    result.logs.append(QString("Ausgabeformat: %1").arg(outputFormat));
    return result;
}

ToolExecutionResult ToolExecutor::executeCsvWrite(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absolutePath = resolveWorkspacePath(configString(config, "path"), true, &errorMessage);
    if (absolutePath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    const bool createDirs = configBool(config, "create_dirs", true);
    const QString parentDirectory = QFileInfo(absolutePath).absolutePath();
    if (!parentDirectory.isEmpty() && !QFileInfo::exists(parentDirectory)) {
        if (!createDirs) {
            result.errorMessage = QString("Zielverzeichnis existiert nicht: %1").arg(parentDirectory);
            return result;
        }
        if (!QDir().mkpath(parentDirectory)) {
            result.errorMessage = QString("Zielverzeichnis konnte nicht erstellt werden: %1").arg(parentDirectory);
            return result;
        }
    }

    const QString sourceFormat = configString(config, "source_format").isEmpty()
        ? "rows_json"
        : configString(config, "source_format").toLower();
    const QChar delimiter = csvDelimiterFromConfig(config);
    const bool includeHeader = configBool(config, "has_header", true);
    QString csvText;

    if (sourceFormat == "csv_text") {
        csvText = config.value("content").toString();
    } else {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(config.value("content").toString().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            result.errorMessage = QString("csv.write erwartet fuer rows_json ein JSON-Array: %1").arg(parseError.errorString());
            return result;
        }

        const QJsonArray rows = document.array();
        QStringList outputLines;
        QStringList headers;

        if (!rows.isEmpty() && rows.first().isObject()) {
            const QJsonObject firstRow = rows.first().toObject();
            for (auto it = firstRow.constBegin(); it != firstRow.constEnd(); ++it) {
                headers.append(it.key());
            }
            if (includeHeader) {
                QStringList headerFields;
                for (const QString& header : headers) {
                    headerFields.append(encodeCsvField(header, delimiter));
                }
                outputLines.append(headerFields.join(delimiter));
            }

            for (const QJsonValue& rowValue : rows) {
                if (!rowValue.isObject()) {
                    result.errorMessage = "csv.write erwartet einheitlich JSON-Objekte oder JSON-Arrays.";
                    return result;
                }
                const QJsonObject rowObject = rowValue.toObject();
                QStringList rowFields;
                for (const QString& header : headers) {
                    rowFields.append(encodeCsvField(rowObject.value(header).toVariant().toString(), delimiter));
                }
                outputLines.append(rowFields.join(delimiter));
            }
        } else {
            for (const QJsonValue& rowValue : rows) {
                if (!rowValue.isArray()) {
                    result.errorMessage = "csv.write erwartet bei array-basierten Daten nur JSON-Arrays pro Zeile.";
                    return result;
                }
                QStringList rowFields;
                const QJsonArray rowArray = rowValue.toArray();
                for (const QJsonValue& fieldValue : rowArray) {
                    rowFields.append(encodeCsvField(fieldValue.toVariant().toString(), delimiter));
                }
                outputLines.append(rowFields.join(delimiter));
            }
        }

        csvText = outputLines.join('\n');
        if (!csvText.endsWith('\n')) {
            csvText += '\n';
        }
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.errorMessage = QString("CSV-Datei konnte nicht geschrieben werden: %1").arg(file.errorString());
        return result;
    }
    file.write(csvText.toUtf8());
    file.close();

    if (configBool(config, "return_content", false)) {
        result.outputText = csvText;
    } else {
        QJsonObject summary{
            { "tool", "csv.write" },
            { "path", QDir(m_workspaceRoot).relativeFilePath(absolutePath) },
            { "bytes_written", csvText.toUtf8().size() }
        };
        result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    }

    result.success = true;
    result.logs.append(QString("CSV-Datei geschrieben: %1").arg(absolutePath));
    result.logs.append(QString("Quellformat: %1").arg(sourceFormat));
    return result;
}

ToolExecutionResult ToolExecutor::executeDirectoryReadRecursive(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absoluteDirectoryPath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absoluteDirectoryPath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QFileInfo directoryInfo(absoluteDirectoryPath);
    if (!directoryInfo.isDir()) {
        result.errorMessage = QString("Pfad ist kein Verzeichnis: %1").arg(absoluteDirectoryPath);
        return result;
    }

    QStringList includeExtensions = configStringList(config, "include_extensions");
    for (QString& extension : includeExtensions) {
        extension = normalizedExtensionFilter(extension);
    }

    QStringList excludeFragments = configStringList(config, "exclude_paths");
    if (excludeFragments.isEmpty()) {
        excludeFragments = {
            ".git",
            "/build",
            "\\build",
            "node_modules",
            "__pycache__"
        };
    }

    const bool includeHidden = configBool(config, "include_hidden", false);
    const bool skipBinary = configBool(config, "skip_binary", true);
    const int maxFiles = qMax(1, configInt(config, "max_files", 40));
    const int maxCharsPerFile = qMax(0, configInt(config, "max_chars_per_file", 8000));
    const int maxTotalChars = qMax(0, configInt(config, "max_total_chars", 120000));

    QDirIterator iterator(
        absoluteDirectoryPath,
        QDir::Files | QDir::NoDotAndDotDot | (includeHidden ? QDir::Hidden : QDir::NoFilter),
        QDirIterator::Subdirectories
    );

    QStringList blocks;
    int fileCount = 0;
    int skippedBinaryFiles = 0;
    int skippedByFilter = 0;
    int totalChars = 0;
    bool totalLimitReached = false;

    while (iterator.hasNext()) {
        const QString absoluteFilePath = iterator.next();
        const QFileInfo fileInfo(absoluteFilePath);
        const QString relativePath = QDir(absoluteDirectoryPath).relativeFilePath(absoluteFilePath);

        if (!matchesDirectoryReadFilters(relativePath, includeExtensions, excludeFragments)) {
            ++skippedByFilter;
            continue;
        }

        QFile file(absoluteFilePath);
        if (!file.open(QIODevice::ReadOnly)) {
            result.logs.append(QString("Datei uebersprungen (nicht lesbar): %1").arg(relativePath));
            continue;
        }

        QByteArray payload = file.readAll();
        file.close();

        if (skipBinary && looksBinary(payload)) {
            ++skippedBinaryFiles;
            continue;
        }

        QString content = QString::fromUtf8(payload);
        bool truncatedPerFile = false;
        if (maxCharsPerFile > 0 && content.size() > maxCharsPerFile) {
            content = content.left(maxCharsPerFile);
            truncatedPerFile = true;
        }

        QString block = QString("### FILE: %1\n%2").arg(relativePath, content);
        if (truncatedPerFile) {
            block += "\n[... Dateiinhalt gekuerzt ...]";
        }

        const int projectedTotal = totalChars + block.size() + 2;
        if (maxTotalChars > 0 && projectedTotal > maxTotalChars) {
            totalLimitReached = true;
            break;
        }

        blocks.append(block);
        totalChars = projectedTotal;
        ++fileCount;

        if (fileCount >= maxFiles) {
            break;
        }
    }

    if (blocks.isEmpty()) {
        result.errorMessage = "Es konnten keine passenden Textdateien aus dem Verzeichnis gelesen werden.";
        return result;
    }

    result.success = true;
    result.outputText = blocks.join("\n\n");
    result.logs.append(QString("Verzeichnis rekursiv gelesen: %1").arg(absoluteDirectoryPath));
    result.logs.append(QString("Dateien im Ergebnis: %1").arg(fileCount));
    result.logs.append(QString("Durch Filter uebersprungen: %1").arg(skippedByFilter));
    if (skipBinary) {
        result.logs.append(QString("Binaerdateien uebersprungen: %1").arg(skippedBinaryFiles));
    }
    if (totalLimitReached) {
        result.logs.append("Abbruch wegen max_total_chars.");
    } else if (fileCount >= maxFiles) {
        result.logs.append("Abbruch wegen max_files.");
    }

    return result;
}

ToolExecutionResult ToolExecutor::executeDirectoryReadChanged(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absoluteDirectoryPath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absoluteDirectoryPath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QFileInfo directoryInfo(absoluteDirectoryPath);
    if (!directoryInfo.isDir()) {
        result.errorMessage = QString("Pfad ist kein Verzeichnis: %1").arg(absoluteDirectoryPath);
        return result;
    }

    const int withinMinutes = qMax(0, configInt(config, "within_minutes", 0));
    QDateTime modifiedAfter;
    if (withinMinutes > 0) {
        modifiedAfter = QDateTime::currentDateTimeUtc().addSecs(-withinMinutes * 60);
    } else {
        const QString modifiedAfterIso = configString(config, "modified_after_iso");
        if (!modifiedAfterIso.isEmpty()) {
            modifiedAfter = QDateTime::fromString(modifiedAfterIso, Qt::ISODate);
            if (modifiedAfter.isValid()) {
                modifiedAfter = modifiedAfter.toUTC();
            }
        }
    }

    if (!modifiedAfter.isValid()) {
        result.errorMessage = "Tool 'directory.read_changed' braucht 'within_minutes' oder 'modified_after_iso'.";
        return result;
    }

    QStringList includeExtensions = configStringList(config, "include_extensions");
    for (QString& extension : includeExtensions) {
        extension = normalizedExtensionFilter(extension);
    }

    QStringList excludeFragments = configStringList(config, "exclude_paths");
    if (excludeFragments.isEmpty()) {
        excludeFragments = {
            ".git",
            "/build",
            "\\build",
            "node_modules",
            "__pycache__"
        };
    }

    const bool includeHidden = configBool(config, "include_hidden", false);
    const bool skipBinary = configBool(config, "skip_binary", true);
    const int maxFiles = qMax(1, configInt(config, "max_files", 20));
    const int maxCharsPerFile = qMax(0, configInt(config, "max_chars_per_file", 6000));
    const int maxTotalChars = qMax(0, configInt(config, "max_total_chars", 80000));

    QDirIterator iterator(
        absoluteDirectoryPath,
        QDir::Files | QDir::NoDotAndDotDot | (includeHidden ? QDir::Hidden : QDir::NoFilter),
        QDirIterator::Subdirectories
    );

    QStringList blocks;
    int fileCount = 0;
    int skippedBinaryFiles = 0;
    int skippedByFilter = 0;
    int skippedByAge = 0;
    int totalChars = 0;
    bool totalLimitReached = false;

    while (iterator.hasNext()) {
        const QString absoluteFilePath = iterator.next();
        const QFileInfo fileInfo(absoluteFilePath);
        const QString relativePath = QDir(absoluteDirectoryPath).relativeFilePath(absoluteFilePath);

        if (!matchesDirectoryReadFilters(relativePath, includeExtensions, excludeFragments)) {
            ++skippedByFilter;
            continue;
        }

        if (fileInfo.lastModified().toUTC() < modifiedAfter) {
            ++skippedByAge;
            continue;
        }

        QFile file(absoluteFilePath);
        if (!file.open(QIODevice::ReadOnly)) {
            result.logs.append(QString("Datei uebersprungen (nicht lesbar): %1").arg(relativePath));
            continue;
        }

        QByteArray payload = file.readAll();
        file.close();

        if (skipBinary && looksBinary(payload)) {
            ++skippedBinaryFiles;
            continue;
        }

        QString content = QString::fromUtf8(payload);
        bool truncatedPerFile = false;
        if (maxCharsPerFile > 0 && content.size() > maxCharsPerFile) {
            content = content.left(maxCharsPerFile);
            truncatedPerFile = true;
        }

        QString block = QString("### FILE: %1\n%2").arg(relativePath, content);
        if (truncatedPerFile) {
            block += "\n[... Dateiinhalt gekuerzt ...]";
        }

        const int projectedTotal = totalChars + block.size() + 2;
        if (maxTotalChars > 0 && projectedTotal > maxTotalChars) {
            totalLimitReached = true;
            break;
        }

        blocks.append(block);
        totalChars = projectedTotal;
        ++fileCount;

        if (fileCount >= maxFiles) {
            break;
        }
    }

    if (blocks.isEmpty()) {
        result.errorMessage = "Es wurden keine geaenderten passenden Textdateien gefunden.";
        return result;
    }

    result.success = true;
    result.outputText = blocks.join("\n\n");
    result.logs.append(QString("Geaenderte Dateien rekursiv gelesen: %1").arg(absoluteDirectoryPath));
    result.logs.append(QString("Aenderungsgrenze: %1").arg(modifiedAfter.toString(Qt::ISODate)));
    result.logs.append(QString("Dateien im Ergebnis: %1").arg(fileCount));
    result.logs.append(QString("Durch Filter uebersprungen: %1").arg(skippedByFilter));
    result.logs.append(QString("Wegen Alter uebersprungen: %1").arg(skippedByAge));
    if (skipBinary) {
        result.logs.append(QString("Binaerdateien uebersprungen: %1").arg(skippedBinaryFiles));
    }
    if (totalLimitReached) {
        result.logs.append("Abbruch wegen max_total_chars.");
    } else if (fileCount >= maxFiles) {
        result.logs.append("Abbruch wegen max_files.");
    }

    return result;
}

ToolExecutionResult ToolExecutor::executeMemoryIngestDirectory(const ToolExecutionRequest& request) const
{
    ToolExecutionResult result;
    if (request.projectId <= 0) {
        result.errorMessage = "Tool 'memory.ingest_directory' braucht ein Projekt im Run-Kontext.";
        return result;
    }

    const QJsonObject& config = request.config;
    const QString mode = configString(config, "mode").toLower();
    const bool useChangedMode = mode == "changed"
        || configInt(config, "within_minutes", 0) > 0
        || !configString(config, "modified_after_iso").isEmpty();

    const ToolExecutionResult directoryResult = useChangedMode
        ? executeDirectoryReadChanged(config)
        : executeDirectoryReadRecursive(config);
    for (const QString& logLine : directoryResult.logs) {
        result.logs.append(logLine);
    }

    if (!directoryResult.success) {
        result.errorMessage = directoryResult.errorMessage;
        return result;
    }

    const QString configuredPath = configString(config, "path");
    const QString absoluteDirectoryPath = resolveWorkspacePath(configuredPath, false, nullptr);
    const QString relativeDirectoryPath = absoluteDirectoryPath.isEmpty()
        ? configuredPath
        : QDir(m_workspaceRoot).relativeFilePath(absoluteDirectoryPath);

    domain::MemoryEntry entry;
    entry.projectId = request.projectId;
    entry.type = configString(config, "entry_type");
    if (entry.type.isEmpty()) {
        entry.type = "artifact";
    }

    entry.source = configString(config, "source");
    if (entry.source.isEmpty()) {
        const QString workflowName = request.workflowName.trimmed().isEmpty()
            ? "workflow"
            : request.workflowName.trimmed();
        const QString stepId = request.stepId.trimmed().isEmpty()
            ? "memory_ingest_directory"
            : request.stepId.trimmed();
        entry.source = QString("workflow:%1/%2").arg(workflowName, stepId);
    }

    entry.tags = configStringList(config, "tags");
    entry.relevance = qBound(0, configInt(config, "relevance", 80), 100);

    QStringList headerLines;
    headerLines.append(QString("Memory-Ingest aus Verzeichnis: %1").arg(relativeDirectoryPath));
    headerLines.append(QString("Modus: %1").arg(useChangedMode ? "changed" : "recursive"));
    headerLines.append(QString("Erstellt: %1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    entry.content = headerLines.join("\n") + "\n\n" + directoryResult.outputText;

    result.memoryEntriesToPersist.append(entry);
    result.success = true;

    QJsonObject summary{
        { "tool", "memory.ingest_directory" },
        { "path", relativeDirectoryPath },
        { "mode", useChangedMode ? "changed" : "recursive" },
        { "memory_entries", 1 },
        { "char_count", entry.content.size() },
        { "source", entry.source }
    };
    result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    result.logs.append(QString("Memory-Ingest vorbereitet fuer Projekt %1.").arg(request.projectId));
    result.logs.append(QString("Memory-Quelle: %1").arg(entry.source));
    result.logs.append(QString("Memory-Typ: %1").arg(entry.type));
    result.logs.append(QString("Memory-Zeichen: %1").arg(entry.content.size()));
    return result;
}

ToolExecutionResult ToolExecutor::executeDirectoryList(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absoluteDirectoryPath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absoluteDirectoryPath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QFileInfo directoryInfo(absoluteDirectoryPath);
    if (!directoryInfo.isDir()) {
        result.errorMessage = QString("Pfad ist kein Verzeichnis: %1").arg(absoluteDirectoryPath);
        return result;
    }

    QStringList includeExtensions = configStringList(config, "include_extensions");
    for (QString& extension : includeExtensions) {
        extension = normalizedExtensionFilter(extension);
    }

    QStringList excludeFragments = configStringList(config, "exclude_paths");
    if (excludeFragments.isEmpty()) {
        excludeFragments = {
            ".git",
            "/build",
            "\\build",
            "node_modules",
            "__pycache__"
        };
    }

    const bool recursive = configBool(config, "recursive", true);
    const bool includeHidden = configBool(config, "include_hidden", false);
    const bool directoriesOnly = configBool(config, "directories_only", false);
    const int maxEntries = qMax(1, configInt(config, "max_entries", 200));

    QDirIterator iterator(
        absoluteDirectoryPath,
        QDir::AllEntries | QDir::NoDotAndDotDot | (includeHidden ? QDir::Hidden : QDir::NoFilter),
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags
    );

    QStringList lines;
    int directoryCount = 0;
    int fileCount = 0;
    int skippedByFilter = 0;
    bool hitEntryLimit = false;

    while (iterator.hasNext()) {
        const QString absoluteEntryPath = iterator.next();
        const QFileInfo entryInfo = iterator.fileInfo();
        const QString relativePath = QDir(absoluteDirectoryPath).relativeFilePath(absoluteEntryPath);

        bool excluded = false;
        const QString normalizedRelativePath = relativePath.toLower();
        for (const QString& fragment : excludeFragments) {
            if (!fragment.trimmed().isEmpty() && normalizedRelativePath.contains(fragment.toLower())) {
                excluded = true;
                break;
            }
        }
        if (excluded) {
            ++skippedByFilter;
            continue;
        }

        if (entryInfo.isDir()) {
            lines.append(QString("[DIR] %1/").arg(relativePath));
            ++directoryCount;
        } else {
            if (directoriesOnly) {
                ++skippedByFilter;
                continue;
            }

            if (!includeExtensions.isEmpty() && !matchesDirectoryReadFilters(relativePath, includeExtensions, {})) {
                ++skippedByFilter;
                continue;
            }

            lines.append(QString("[FILE] %1").arg(relativePath));
            ++fileCount;
        }

        if (lines.size() >= maxEntries) {
            hitEntryLimit = true;
            break;
        }
    }

    if (lines.isEmpty()) {
        result.errorMessage = "Es konnten keine passenden Verzeichniseintraege gefunden werden.";
        return result;
    }

    result.success = true;
    result.outputText = lines.join('\n');
    result.logs.append(QString("Verzeichnis gelistet: %1").arg(absoluteDirectoryPath));
    result.logs.append(QString("Modus: %1").arg(recursive ? "rekursiv" : "nur direkt"));
    result.logs.append(QString("Verzeichnisse: %1").arg(directoryCount));
    result.logs.append(QString("Dateien: %1").arg(fileCount));
    result.logs.append(QString("Durch Filter uebersprungen: %1").arg(skippedByFilter));
    if (hitEntryLimit) {
        result.logs.append("Abbruch wegen max_entries.");
    }
    return result;
}

ToolExecutionResult ToolExecutor::executeMemorySearch(const ToolExecutionRequest& request) const
{
    ToolExecutionResult result;
    if (request.projectId <= 0) {
        result.errorMessage = "Tool 'memory.search' braucht ein Projekt im Run-Kontext.";
        return result;
    }

    QString errorMessage;
    ScopedSqliteConnection connection;
    if (!openSqliteConnection(m_databasePath, "memory_search", &connection, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    MemoryQueryOptions options;
    options.projectId = request.projectId;
    options.queryText = configString(request.config, "query");
    options.entryType = configString(request.config, "entry_type");
    options.tags = configStringList(request.config, "tags");
    options.limit = qMax(1, configInt(request.config, "limit", 10));

    const MemoryQueryResult queryResult = queryMemoryEntries(connection.database, options);
    if (!queryResult.errorMessage.isEmpty()) {
        result.errorMessage = queryResult.errorMessage;
        return result;
    }

    const QString format = configString(request.config, "format").isEmpty()
        ? "snippets"
        : configString(request.config, "format").toLower();
    const int maxChars = qMax(0, configInt(request.config, "max_chars", 16000));
    result.outputText = formatMemoryEntries(queryResult.entries, format == "full", maxChars);
    result.success = true;
    result.logs.append(QString("Memory-Treffer: %1").arg(queryResult.entries.size()));
    result.logs.append(QString("Format: %1").arg(format));
    if (queryResult.entries.isEmpty()) {
        result.logs.append("Keine passenden Memory-Eintraege gefunden.");
    }
    return result;
}

ToolExecutionResult ToolExecutor::executeMemorySummarize(const ToolExecutionRequest& request) const
{
    ToolExecutionResult result;
    if (request.projectId <= 0) {
        result.errorMessage = "Tool 'memory.summarize' braucht ein Projekt im Run-Kontext.";
        return result;
    }

    if (request.llmProvider == nullptr) {
        result.errorMessage = "Tool 'memory.summarize' braucht einen aktiven LLM-Provider.";
        return result;
    }

    if (request.selectedModel.trimmed().isEmpty()) {
        result.errorMessage = "Tool 'memory.summarize' braucht ein ausgewaehltes Modell.";
        return result;
    }

    QString errorMessage;
    ScopedSqliteConnection connection;
    if (!openSqliteConnection(m_databasePath, "memory_summarize", &connection, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    MemoryQueryOptions options;
    options.projectId = request.projectId;
    options.queryText = configString(request.config, "query");
    options.entryType = configString(request.config, "entry_type");
    options.tags = configStringList(request.config, "tags");
    options.limit = qMax(1, configInt(request.config, "limit", 12));

    const MemoryQueryResult queryResult = queryMemoryEntries(connection.database, options);
    if (!queryResult.errorMessage.isEmpty()) {
        result.errorMessage = queryResult.errorMessage;
        return result;
    }

    if (queryResult.entries.isEmpty()) {
        result.success = true;
        result.logs.append("Keine passenden Memory-Eintraege zum Zusammenfassen gefunden.");
        return result;
    }

    const int maxChars = qMax(0, configInt(request.config, "max_chars", 24000));
    const QString memoryContext = formatMemoryEntries(queryResult.entries, true, maxChars);
    QString summaryPrompt = configString(request.config, "prompt");
    if (summaryPrompt.isEmpty()) {
        summaryPrompt =
            "Fasse die folgenden Projekt-Memory-Eintraege kompakt und ohne Halluzinationen zusammen. "
            "Arbeite in klaren Stichpunkten und erhalte Fakten, Entscheidungen und offene Punkte.\n\n"
            "{{memory_context}}";
    }
    summaryPrompt.replace("{{memory_context}}", memoryContext);

    QString systemPrompt = configString(request.config, "system_prompt");
    if (systemPrompt.isEmpty()) {
        systemPrompt = request.systemPrompt.trimmed();
    }
    if (systemPrompt.isEmpty()) {
        systemPrompt =
            "Du bist ein praeziser Assistent fuer Projektgedaechtnis. "
            "Verdichte Inhalte verlustarm, erfinde nichts und antworte standardmaessig auf Deutsch.";
    }

    providers::ChatRequest chatRequest;
    chatRequest.model = request.selectedModel;
    chatRequest.systemPrompt = systemPrompt;
    chatRequest.userPrompt = summaryPrompt;

    const providers::ChatResponse response = request.llmProvider->chat(chatRequest);
    if (!response.success) {
        result.errorMessage = QString("Memory-Zusammenfassung fehlgeschlagen: %1").arg(response.errorMessage);
        return result;
    }

    const SanitizedModelResponse sanitized = sanitizeModelResponse(response.text);
    result.success = true;
    result.outputText = sanitized.visibleText;
    result.logs.append(QString("Memory-Eintraege zusammengefasst: %1").arg(queryResult.entries.size()));
    if (sanitized.hadReasoningTags) {
        result.logs.append("Reasoning-Tags wurden aus der Zusammenfassung entfernt.");
    }

    if (configBool(request.config, "save_as_memory", true)) {
        domain::MemoryEntry entry;
        entry.projectId = request.projectId;
        entry.type = configString(request.config, "summary_entry_type");
        if (entry.type.isEmpty()) {
            entry.type = "summary";
        }
        entry.source = configString(request.config, "summary_source");
        if (entry.source.isEmpty()) {
            entry.source = QString("workflow:%1/%2").arg(request.workflowName, request.stepId);
        }
        entry.tags = configStringList(request.config, "summary_tags");
        entry.relevance = qBound(0, configInt(request.config, "summary_relevance", 75), 100);
        entry.content = sanitized.visibleText.trimmed();
        if (!entry.content.isEmpty()) {
            result.memoryEntriesToPersist.append(entry);
            result.logs.append("Zusammenfassung als neuer Memory-Eintrag vorgemerkt.");
        }
    }

    return result;
}

ToolExecutionResult ToolExecutor::executeMemoryDeleteOld(const ToolExecutionRequest& request) const
{
    ToolExecutionResult result;
    if (request.projectId <= 0) {
        result.errorMessage = "Tool 'memory.delete_old' braucht ein Projekt im Run-Kontext.";
        return result;
    }

    const int olderThanDays = qMax(1, configInt(request.config, "older_than_days", 30));
    const int keepLatest = qMax(0, configInt(request.config, "keep_latest", 0));
    const int keepRelevanceAtOrAbove = qBound(0, configInt(request.config, "keep_relevance_at_or_above", 90), 100);
    const bool dryRun = configBool(request.config, "dry_run", true);

    QString errorMessage;
    ScopedSqliteConnection connection;
    if (!openSqliteConnection(m_databasePath, "memory_delete_old", &connection, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    MemoryQueryOptions options;
    options.projectId = request.projectId;
    options.queryText = configString(request.config, "query");
    options.entryType = configString(request.config, "entry_type");
    options.tags = configStringList(request.config, "tags");
    options.limit = 0;

    const MemoryQueryResult queryResult = queryMemoryEntries(connection.database, options);
    if (!queryResult.errorMessage.isEmpty()) {
        result.errorMessage = queryResult.errorMessage;
        return result;
    }

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-olderThanDays);
    QList<qint64> idsToDelete;
    int preservedByRecency = 0;
    int preservedByRelevance = 0;
    int index = 0;
    for (const domain::MemoryEntry& entry : queryResult.entries) {
        if (index < keepLatest) {
            ++preservedByRecency;
            ++index;
            continue;
        }

        ++index;
        const QDateTime entryTime = entry.createdAt.isValid() ? entry.createdAt.toUTC() : QDateTime();
        if (entryTime.isValid() && entryTime >= cutoff) {
            continue;
        }
        if (entry.relevance >= keepRelevanceAtOrAbove) {
            ++preservedByRelevance;
            continue;
        }
        idsToDelete.append(entry.id);
    }

    if (!dryRun && !idsToDelete.isEmpty()) {
        QSqlQuery deleteQuery(connection.database);
        deleteQuery.prepare("DELETE FROM memory_entries WHERE id = ?");
        for (const qint64 id : idsToDelete) {
            deleteQuery.addBindValue(id);
            if (!deleteQuery.exec()) {
                result.errorMessage = deleteQuery.lastError().text();
                return result;
            }
            deleteQuery.finish();
        }
    }

    QJsonObject summary{
        { "tool", "memory.delete_old" },
        { "dry_run", dryRun },
        { "matched_entries", queryResult.entries.size() },
        { "delete_candidates", idsToDelete.size() },
        { "older_than_days", olderThanDays },
        { "keep_latest", keepLatest },
        { "keep_relevance_at_or_above", keepRelevanceAtOrAbove }
    };
    result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    result.success = true;
    result.logs.append(QString("Memory-Eintraege geprueft: %1").arg(queryResult.entries.size()));
    result.logs.append(QString("%1 Kandidat(en) fuer Loeschung.").arg(idsToDelete.size()));
    result.logs.append(QString("Durch Recency geschuetzt: %1").arg(preservedByRecency));
    result.logs.append(QString("Durch Relevanz geschuetzt: %1").arg(preservedByRelevance));
    if (dryRun) {
        result.logs.append("Dry-Run aktiv: Es wurden keine Eintraege geloescht.");
    } else {
        result.logs.append("Loeschung abgeschlossen.");
    }
    return result;
}

ToolExecutionResult ToolExecutor::executeFileWriteText(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absolutePath = resolveWorkspacePath(configString(config, "path"), true, &errorMessage);
    if (absolutePath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QFileInfo targetInfo(absolutePath);
    if (targetInfo.exists() && targetInfo.isDir()) {
        result.errorMessage = QString("Pfad ist ein Verzeichnis und keine Datei: %1").arg(absolutePath);
        return result;
    }

    const QString mode = configString(config, "mode").isEmpty()
        ? "overwrite"
        : configString(config, "mode").toLower();
    if (mode != "overwrite" && mode != "append") {
        result.errorMessage = "Tool 'file.write_text' unterstuetzt nur mode 'overwrite' oder 'append'.";
        return result;
    }

    const bool createDirs = configBool(config, "create_dirs", true);
    const QString parentDirectory = QFileInfo(absolutePath).absolutePath();
    if (!parentDirectory.isEmpty() && !QFileInfo::exists(parentDirectory)) {
        if (!createDirs) {
            result.errorMessage = QString("Zielverzeichnis existiert nicht: %1").arg(parentDirectory);
            return result;
        }
        if (!QDir().mkpath(parentDirectory)) {
            result.errorMessage = QString("Zielverzeichnis konnte nicht erstellt werden: %1").arg(parentDirectory);
            return result;
        }
    }

    const QString content = config.value("content").toString();
    const bool returnContent = configBool(config, "return_content", false);

    if (mode == "append") {
        QFile file(absolutePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            result.errorMessage = QString("Datei konnte nicht zum Schreiben geoeffnet werden: %1").arg(file.errorString());
            return result;
        }
        if (file.write(content.toUtf8()) < 0) {
            result.errorMessage = QString("Datei konnte nicht beschrieben werden: %1").arg(file.errorString());
            file.close();
            return result;
        }
        file.close();
    } else {
        QSaveFile saveFile(absolutePath);
        if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            result.errorMessage = QString("Datei konnte nicht zum Schreiben geoeffnet werden: %1").arg(saveFile.errorString());
            return result;
        }
        saveFile.write(content.toUtf8());
        if (!saveFile.commit()) {
            result.errorMessage = QString("Datei konnte nicht gespeichert werden: %1").arg(saveFile.errorString());
            return result;
        }
    }

    if (returnContent) {
        QFile file(absolutePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.errorMessage = QString("Aktualisierte Datei konnte nicht erneut gelesen werden: %1").arg(file.errorString());
            return result;
        }
        result.outputText = QString::fromUtf8(file.readAll());
        file.close();
    } else {
        QJsonObject summary{
            { "tool", "file.write_text" },
            { "path", QDir(m_workspaceRoot).relativeFilePath(absolutePath) },
            { "mode", mode },
            { "bytes_written", content.toUtf8().size() }
        };
        result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    }

    result.success = true;
    result.logs.append(QString("Datei geschrieben: %1").arg(absolutePath));
    result.logs.append(QString("Schreibmodus: %1").arg(mode));
    result.logs.append(QString("Zeichen: %1").arg(content.size()));
    return result;
}

ToolExecutionResult ToolExecutor::executeFileEditDiff(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString patchPath;
    QList<UnifiedDiffHunk> hunks;
    QString parseError;
    const QString diffText = configString(config, "diff").isEmpty()
        ? configString(config, "patch")
        : configString(config, "diff");
    if (diffText.isEmpty()) {
        result.errorMessage = "Tool 'file.edit_diff' braucht ein Feld 'diff' oder 'patch'.";
        return result;
    }

    if (!parseUnifiedDiff(diffText, &patchPath, &hunks, &parseError)) {
        result.errorMessage = parseError;
        return result;
    }

    QString targetPath = configString(config, "path");
    if (targetPath.isEmpty()) {
        targetPath = patchPath;
    }

    QString resolveError;
    const QString absolutePath = resolveWorkspacePath(targetPath, false, &resolveError);
    if (absolutePath.isEmpty()) {
        result.errorMessage = resolveError;
        return result;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QString("Datei konnte nicht geoeffnet werden: %1").arg(file.errorString());
        return result;
    }

    const QString originalContent = QString::fromUtf8(file.readAll());
    file.close();

    QString patchedContent;
    QString applyError;
    if (!applyUnifiedDiffToText(originalContent, hunks, &patchedContent, &applyError)) {
        result.errorMessage = applyError;
        return result;
    }

    QSaveFile saveFile(absolutePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.errorMessage = QString("Datei konnte nicht zum Schreiben geoeffnet werden: %1").arg(saveFile.errorString());
        return result;
    }

    saveFile.write(patchedContent.toUtf8());
    if (!saveFile.commit()) {
        result.errorMessage = QString("Dateiaenderung konnte nicht gespeichert werden: %1").arg(saveFile.errorString());
        return result;
    }

    const bool returnContent = configBool(config, "return_content", false);
    if (returnContent) {
        result.outputText = patchedContent;
    } else {
        QJsonObject summary{
            { "tool", "file.edit_diff" },
            { "path", QDir(m_workspaceRoot).relativeFilePath(absolutePath) },
            { "status", "updated" }
        };
        result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    }

    result.success = true;
    result.logs.append(QString("Datei per Diff aktualisiert: %1").arg(absolutePath));
    return result;
}

ToolExecutionResult ToolExecutor::executeHttpRequest(const QJsonObject& config) const
{
    ToolExecutionResult result;

    const QString urlText = configString(config, "url");
    if (urlText.isEmpty()) {
        result.errorMessage = "Tool 'http.request' braucht ein Feld 'url'.";
        return result;
    }

    const QUrl url(urlText);
    if (!url.isValid() || url.scheme().trimmed().isEmpty()) {
        result.errorMessage = QString("HTTP-URL ist ungueltig: %1").arg(urlText);
        return result;
    }

    const QString method = configString(config, "method").isEmpty()
        ? "GET"
        : configString(config, "method").toUpper();
    const int timeoutMs = qMax(0, configInt(config, "timeout_ms", 30000));

    QJsonObject headersObject;
    if (config.value("headers").isObject()) {
        headersObject = config.value("headers").toObject();
    } else if (!configString(config, "headers_json").isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument headersDocument = QJsonDocument::fromJson(configString(config, "headers_json").toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !headersDocument.isObject()) {
            result.errorMessage = QString("headers_json ist kein gueltiges JSON-Objekt: %1").arg(parseError.errorString());
            return result;
        }
        headersObject = headersDocument.object();
    }

    QByteArray requestBody;
    bool hasRequestBody = false;
    if (config.contains("body_json")) {
        QJsonDocument bodyDocument;
        const QJsonValue bodyValue = config.value("body_json");
        if (bodyValue.isObject()) {
            bodyDocument = QJsonDocument(bodyValue.toObject());
        } else if (bodyValue.isArray()) {
            bodyDocument = QJsonDocument(bodyValue.toArray());
        } else {
            QJsonParseError parseError;
            bodyDocument = QJsonDocument::fromJson(bodyValue.toString().toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || bodyDocument.isNull()) {
                result.errorMessage = QString("body_json ist kein gueltiges JSON: %1").arg(parseError.errorString());
                return result;
            }
        }
        requestBody = bodyDocument.toJson(QJsonDocument::Compact);
        hasRequestBody = true;
    } else if (config.contains("body")) {
        requestBody = config.value("body").toString().toUtf8();
        hasRequestBody = true;
    }

    bool hasContentTypeHeader = false;
    QNetworkRequest request(url);
    for (auto it = headersObject.constBegin(); it != headersObject.constEnd(); ++it) {
        const QString headerName = it.key().trimmed();
        if (headerName.isEmpty()) {
            continue;
        }

        QString headerValue;
        if (it.value().isString()) {
            headerValue = it.value().toString();
        } else if (it.value().isBool()) {
            headerValue = it.value().toBool() ? "true" : "false";
        } else if (it.value().isDouble()) {
            headerValue = QString::number(it.value().toDouble());
        } else if (it.value().isObject()) {
            headerValue = QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
        } else if (it.value().isArray()) {
            headerValue = QString::fromUtf8(QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact));
        }

        request.setRawHeader(headerName.toUtf8(), headerValue.toUtf8());
        if (headerName.compare("Content-Type", Qt::CaseInsensitive) == 0) {
            hasContentTypeHeader = true;
        }
    }
    if (config.contains("body_json") && !hasContentTypeHeader) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    }

    QNetworkAccessManager networkManager;
    NetworkCallResult response;

    if (method == "GET") {
        response = waitForReply(networkManager.get(request), timeoutMs);
    } else if (method == "POST") {
        response = waitForReply(networkManager.post(request, requestBody), timeoutMs);
    } else if (method == "PUT") {
        response = waitForReply(networkManager.put(request, requestBody), timeoutMs);
    } else if (method == "PATCH") {
        response = waitForReply(networkManager.sendCustomRequest(request, "PATCH", requestBody), timeoutMs);
    } else if (method == "DELETE") {
        response = hasRequestBody
            ? waitForReply(networkManager.sendCustomRequest(request, "DELETE", requestBody), timeoutMs)
            : waitForReply(networkManager.deleteResource(request), timeoutMs);
    } else if (method == "HEAD") {
        response = waitForReply(networkManager.head(request), timeoutMs);
    } else {
        result.errorMessage = QString("HTTP-Methode wird nicht unterstuetzt: %1").arg(method);
        return result;
    }

    if (!response.success) {
        result.errorMessage = QString("HTTP-Anfrage fehlgeschlagen: %1").arg(response.errorMessage);
        return result;
    }

    const QString responseText = QString::fromUtf8(response.payload);
    if (responseText.trimmed().isEmpty()) {
        QJsonObject summary{
            { "tool", "http.request" },
            { "url", url.toString() },
            { "method", method },
            { "status_code", response.statusCode },
            { "bytes", static_cast<int>(response.payload.size()) }
        };
        result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    } else {
        result.outputText = responseText;
    }

    result.success = true;
    result.logs.append(QString("HTTP %1 %2").arg(method, url.toString()));
    result.logs.append(QString("Statuscode: %1").arg(response.statusCode));
    result.logs.append(QString("Antwortgroesse: %1 Bytes").arg(response.payload.size()));
    if (timeoutMs > 0) {
        result.logs.append(QString("Lokales Timeout: %1 ms").arg(timeoutMs));
    } else {
        result.logs.append("Lokales Timeout: deaktiviert.");
    }
    return result;
}

ToolExecutionResult ToolExecutor::executeShellRun(const QJsonObject& config) const
{
    ToolExecutionResult result;

    const QString command = configString(config, "command");
    if (command.isEmpty()) {
        result.errorMessage = "Tool 'shell.run' braucht ein Feld 'command'.";
        return result;
    }

    const QStringList commandParts = QProcess::splitCommand(command);
    if (commandParts.isEmpty()) {
        result.errorMessage = "shell.run konnte den Befehl nicht in Programm und Argumente aufteilen.";
        return result;
    }

    const QString program = commandParts.first();
    const QStringList arguments = commandParts.mid(1);

    QString safetyError;
    if (!isSafeShellProgram(QFileInfo(program).fileName(), arguments, &safetyError)) {
        result.errorMessage = safetyError;
        return result;
    }

    QString workingDirectory = configString(config, "working_directory");
    if (workingDirectory.isEmpty()) {
        workingDirectory = m_workspaceRoot;
    }

    QString resolveError;
    const QString absoluteWorkingDirectory = QFileInfo(workingDirectory).isAbsolute()
        ? resolveWorkspacePath(workingDirectory, false, &resolveError)
        : resolveWorkspacePath(workingDirectory, false, &resolveError);
    if (absoluteWorkingDirectory.isEmpty()) {
        result.errorMessage = resolveError;
        return result;
    }

    const int timeoutMs = qMax(0, configInt(config, "timeout_ms", 60000));
    const int maxOutputChars = qMax(0, configInt(config, "max_output_chars", 20000));
    const bool includeStderr = configBool(config, "include_stderr", true);

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setWorkingDirectory(absoluteWorkingDirectory);
    process.start();

    if (!process.waitForStarted(5000)) {
        result.errorMessage = QString("Prozess konnte nicht gestartet werden: %1").arg(process.errorString());
        return result;
    }

    if (timeoutMs > 0) {
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(3000);
            result.errorMessage = "shell.run wurde wegen lokaler Zeitueberschreitung beendet.";
            return result;
        }
    } else {
        process.waitForFinished(-1);
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QString stderrText = QString::fromUtf8(process.readAllStandardError());
    if (includeStderr && !stderrText.trimmed().isEmpty()) {
        if (!output.trimmed().isEmpty()) {
            output += "\n";
        }
        output += "[stderr]\n" + stderrText;
    }

    if (maxOutputChars > 0 && output.size() > maxOutputChars) {
        output = output.left(maxOutputChars) + "\n[... Ausgabe gekuerzt ...]";
    }

    result.success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    result.outputText = output.trimmed();
    if (!result.success) {
        result.errorMessage = QString("shell.run endete mit Exit-Code %1.").arg(process.exitCode());
    }
    result.logs.append(QString("Programm: %1").arg(program));
    result.logs.append(QString("Arbeitsverzeichnis: %1").arg(absoluteWorkingDirectory));
    result.logs.append(QString("Exit-Code: %1").arg(process.exitCode()));
    return result;
}

ToolExecutionResult ToolExecutor::executeComfyUiWorkflow(const QJsonObject& config) const
{
    ToolExecutionResult result;
    const QString comfyUiBaseUrl = configString(config, "base_url").isEmpty()
        ? m_comfyUiBaseUrl.trimmed()
        : configString(config, "base_url");
    if (comfyUiBaseUrl.trimmed().isEmpty()) {
        result.errorMessage = "Keine ComfyUI-URL konfiguriert.";
        return result;
    }

    QJsonObject workflowObject;
    if (config.value("workflow").isObject()) {
        workflowObject = config.value("workflow").toObject();
    } else if ((configString(config, "builder_mode").toLower() == "txt2img"
            || configString(config, "builder_mode").toLower() == "img2img"
            || configString(config, "builder_mode").toLower() == "inpainting")
        || (!configString(config, "checkpoint").isEmpty() && configString(config, "workflow_json").isEmpty())) {
        QString buildError;
        workflowObject = buildComfyGeneratedImageWorkflow(
            config,
            m_workspaceRoot,
            comfyUiBaseUrl,
            &buildError,
            &result.logs
        );
        if (workflowObject.isEmpty()) {
            result.errorMessage = buildError.isEmpty()
                ? "ComfyUI-Workflow aus Formular-Daten konnte nicht erzeugt werden."
                : buildError;
            return result;
        }
    } else {
        const QString workflowJson = configString(config, "workflow_json");
        if (workflowJson.isEmpty()) {
            result.errorMessage = "Tool 'comfyui.workflow' braucht 'workflow', 'workflow_json' oder eine txt2img-Konfiguration.";
            return result;
        }

        QJsonParseError parseError;
        const QJsonDocument workflowDocument = QJsonDocument::fromJson(workflowJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !workflowDocument.isObject()) {
            result.errorMessage = QString("ComfyUI-Workflow ist kein gueltiges JSON-Objekt: %1").arg(parseError.errorString());
            return result;
        }
        workflowObject = workflowDocument.object();
    }

    const QString clientId = configString(config, "client_id").isEmpty()
        ? sanitizedUuid()
        : configString(config, "client_id");
    const QString promptId = configString(config, "prompt_id").isEmpty()
        ? sanitizedUuid()
        : configString(config, "prompt_id");

    QJsonObject payload{
        { "prompt", workflowObject },
        { "client_id", clientId },
        { "prompt_id", promptId }
    };

    if (config.value("extra_data").isObject()) {
        payload.insert("extra_data", config.value("extra_data").toObject());
    } else if (!configString(config, "extra_data_json").isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument extraDataDocument = QJsonDocument::fromJson(configString(config, "extra_data_json").toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !extraDataDocument.isObject()) {
            result.errorMessage = QString("ComfyUI extra_data_json ist ungueltig: %1").arg(parseError.errorString());
            return result;
        }
        payload.insert("extra_data", extraDataDocument.object());
    }

    QNetworkAccessManager networkManager;
    QNetworkRequest submitRequest(buildEndpoint(comfyUiBaseUrl, "/prompt"));
    submitRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const int submitTimeoutMs = qMax(1000, configInt(config, "submit_timeout_ms", 15000));
    const NetworkCallResult submitResult = waitForReply(
        networkManager.post(submitRequest, QJsonDocument(payload).toJson(QJsonDocument::Compact)),
        submitTimeoutMs
    );
    if (!submitResult.success) {
        result.errorMessage = QString("ComfyUI-Queue fehlgeschlagen: %1").arg(submitResult.errorMessage);
        return result;
    }

    result.logs.append(QString("ComfyUI-Prompt eingereiht: %1").arg(promptId));

    const bool waitForCompletion = configBool(config, "wait_for_completion", true);
    if (!waitForCompletion) {
        QJsonObject summary{
            { "tool", "comfyui.workflow" },
            { "prompt_id", promptId },
            { "client_id", clientId },
            { "queued", true }
        };
        result.success = true;
        result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
        return result;
    }

    const int pollIntervalMs = qBound(200, configInt(config, "poll_interval_ms", 1500), 60000);
    const int timeoutMs = qMax(0, configInt(config, "timeout_ms", 0));
    const bool includeHistoryJson = configBool(config, "include_history_json", false);
    const bool downloadImages = configBool(config, "download_images", !configString(config, "save_outputs_to").isEmpty());

    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    QJsonObject historyEntry;
    while (historyEntry.isEmpty()) {
        if (timeoutMs > 0 && elapsedTimer.elapsed() > timeoutMs) {
            result.errorMessage = "Zeitueberschreitung beim Warten auf ComfyUI-History.";
            return result;
        }

        const NetworkCallResult historyResult = waitForReply(
            networkManager.get(QNetworkRequest(buildEndpoint(comfyUiBaseUrl, QString("/history/%1").arg(promptId)))),
            10000
        );
        if (historyResult.success) {
            const QJsonDocument historyDocument = QJsonDocument::fromJson(historyResult.payload);
            if (historyDocument.isObject()) {
                historyEntry = extractComfyHistoryEntry(historyDocument.object(), promptId);
            }
        }

        if (historyEntry.isEmpty()) {
            QThread::msleep(static_cast<unsigned long>(pollIntervalMs));
        }
    }

    QStringList savedFiles;
    if (downloadImages) {
        QString outputDirectory = configString(config, "save_outputs_to");
        if (outputDirectory.isEmpty()) {
            outputDirectory = QString("artifacts/comfyui/%1").arg(promptId);
        }

        QString directoryError;
        const QString absoluteOutputDirectory = resolveWorkspacePath(outputDirectory, true, &directoryError);
        if (absoluteOutputDirectory.isEmpty()) {
            result.errorMessage = directoryError;
            return result;
        }

        QDir().mkpath(absoluteOutputDirectory);

        const QJsonObject outputs = historyEntry.value("outputs").toObject();
        for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
            const QJsonArray images = it.value().toObject().value("images").toArray();
            for (const QJsonValue& imageValue : images) {
                const QJsonObject imageObject = imageValue.toObject();
                QUrl viewUrl = buildEndpoint(comfyUiBaseUrl, "/view");
                QUrlQuery query;
                query.addQueryItem("filename", imageObject.value("filename").toString());
                query.addQueryItem("subfolder", imageObject.value("subfolder").toString());
                query.addQueryItem("type", imageObject.value("type").toString());
                viewUrl.setQuery(query);

                const NetworkCallResult imageResult = waitForReply(
                    networkManager.get(QNetworkRequest(viewUrl)),
                    15000
                );
                if (!imageResult.success) {
                    result.logs.append(
                        QString("Bild konnte nicht heruntergeladen werden: %1").arg(imageResult.errorMessage)
                    );
                    continue;
                }

                QString relativeTargetPath = outputDirectory;
                const QString subfolder = imageObject.value("subfolder").toString().trimmed();
                if (!subfolder.isEmpty()) {
                    relativeTargetPath += "/" + subfolder;
                }

                QString targetDirectoryError;
                const QString absoluteImageDirectory = resolveWorkspacePath(relativeTargetPath, true, &targetDirectoryError);
                if (absoluteImageDirectory.isEmpty()) {
                    result.logs.append(targetDirectoryError);
                    continue;
                }

                QDir().mkpath(absoluteImageDirectory);
                const QString fileName = imageObject.value("filename").toString();
                const QString absoluteFilePath = QDir(absoluteImageDirectory).filePath(fileName);

                QSaveFile saveFile(absoluteFilePath);
                if (!saveFile.open(QIODevice::WriteOnly)) {
                    result.logs.append(
                        QString("Bild konnte nicht gespeichert werden: %1").arg(saveFile.errorString())
                    );
                    continue;
                }

                saveFile.write(imageResult.payload);
                if (!saveFile.commit()) {
                    result.logs.append(
                        QString("Bild konnte nicht abgeschlossen gespeichert werden: %1").arg(saveFile.errorString())
                    );
                    continue;
                }

                savedFiles.append(QDir(m_workspaceRoot).relativeFilePath(absoluteFilePath));
            }
        }
    }

    QJsonObject summary{
        { "tool", "comfyui.workflow" },
        { "base_url", comfyUiBaseUrl },
        { "prompt_id", promptId },
        { "client_id", clientId },
        { "saved_files", QJsonArray::fromStringList(savedFiles) },
        { "output_node_count", historyEntry.value("outputs").toObject().size() }
    };
    if (includeHistoryJson) {
        summary.insert("history", historyEntry);
    }

    result.success = true;
    result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    result.logs.append(QString("ComfyUI-Ausfuehrung abgeschlossen: %1").arg(promptId));
    result.logs.append(QString("Gespeicherte Dateien: %1").arg(savedFiles.size()));
    return result;
}

QString ToolExecutor::resolveWorkspacePath(
    const QString& path,
    const bool allowNonExisting,
    QString* errorMessage
) const
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Tool-Pfad darf nicht leer sein.";
        }
        return {};
    }

    const QFileInfo inputInfo(trimmedPath);
    const QString absolutePath = inputInfo.isAbsolute()
        ? QDir::cleanPath(inputInfo.absoluteFilePath())
        : QDir(m_workspaceRoot).absoluteFilePath(trimmedPath);

    if (!allowNonExisting && !QFileInfo::exists(absolutePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Pfad existiert nicht: %1").arg(absolutePath);
        }
        return {};
    }

    if (!isPathWithinAllowedRoots(absolutePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Pfad liegt ausserhalb der erlaubten Dateipfade: %1").arg(absolutePath);
        }
        return {};
    }

    const QStringList pathSegments = QDir::cleanPath(absolutePath).split(QRegularExpression(R"([\\/]+)"), Qt::SkipEmptyParts);
    if (pathSegments.contains(".git")) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zugriffe auf .git-Verzeichnisse sind nicht erlaubt.";
        }
        return {};
    }

    return absolutePath;
}

bool ToolExecutor::isPathWithinAllowedRoots(const QString& absolutePath) const
{
    const QString normalizedCandidate = QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath()).toCaseFolded();

    for (const QString& allowedRoot : m_allowedToolPaths) {
        const QString normalizedAllowedRoot = QDir::cleanPath(allowedRoot).toCaseFolded();
        if (normalizedCandidate == normalizedAllowedRoot
            || normalizedCandidate.startsWith(normalizedAllowedRoot + "/")
            || normalizedCandidate.startsWith(normalizedAllowedRoot + "\\")) {
            return true;
        }
    }

    return false;
}

} // namespace privateclaw::tools
