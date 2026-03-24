#include "services/RunService.h"

#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

namespace privateclaw::services {

namespace {

QString normalizedText(QString value)
{
    if (value.isNull()) {
        return QStringLiteral("");
    }
    return value;
}

QString normalizedTrimmedText(QString value)
{
    QString trimmedValue = normalizedText(std::move(value)).trimmed();
    if (trimmedValue.isNull()) {
        return QStringLiteral("");
    }
    return trimmedValue;
}

domain::Run mapRun(const QSqlQuery& query)
{
    domain::Run run;
    run.id = query.value(0).toLongLong();
    run.projectId = query.value(1).toLongLong();
    run.workflowId = query.value(2).toLongLong();
    run.status = query.value(3).toString();
    run.origin = query.value(4).toString();
    run.providerName = query.value(5).toString();
    run.modelName = query.value(6).toString();
    run.summary = query.value(7).toString();
    run.outputText = query.value(8).toString();
    run.logText = query.value(9).toString();
    run.errorMessage = query.value(10).toString();
    run.savedMemoryCount = query.value(11).toInt();
    run.startedAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODate);
    run.finishedAt = QDateTime::fromString(query.value(13).toString(), Qt::ISODate);
    return run;
}

} // namespace

RunService::RunService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

int RunService::runCount() const
{
    QSqlQuery query(m_databaseManager.database());
    if (!query.exec("SELECT COUNT(*) FROM runs")) {
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QList<domain::Run> RunService::listRuns(const qint64 projectId, const int limit) const
{
    QList<domain::Run> runs;

    QString statement =
        "SELECT id, project_id, workflow_id, status, origin, provider_name, model_name, summary, "
        "output_text, log_text, error_message, saved_memory_count, started_at, finished_at "
        "FROM runs";
    if (projectId > 0) {
        statement += " WHERE project_id = ?";
    }
    statement += " ORDER BY started_at DESC, id DESC";
    if (limit > 0) {
        statement += " LIMIT ?";
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare(statement);
    if (projectId > 0) {
        query.addBindValue(projectId);
    }
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        return runs;
    }

    while (query.next()) {
        runs.append(mapRun(query));
    }

    return runs;
}

bool RunService::startRun(domain::Run* run, QString* errorMessage) const
{
    if (run == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Run uebergeben.";
        }
        return false;
    }

    if (run->projectId <= 0 || run->workflowId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Run braucht Projekt- und Workflow-ID.";
        }
        return false;
    }

    if (!run->startedAt.isValid()) {
        run->startedAt = QDateTime::currentDateTimeUtc();
    }

    run->status = run->status.trimmed().isEmpty() ? "running" : run->status.trimmed();
    run->origin = normalizedTrimmedText(run->origin);
    run->providerName = normalizedTrimmedText(run->providerName);
    run->modelName = normalizedTrimmedText(run->modelName);
    run->summary = normalizedTrimmedText(run->summary);
    run->outputText = normalizedText(run->outputText);
    run->logText = normalizedText(run->logText);
    run->errorMessage = normalizedText(run->errorMessage);

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "INSERT INTO runs ("
        "project_id, workflow_id, status, origin, provider_name, model_name, summary, "
        "output_text, log_text, error_message, saved_memory_count, started_at, finished_at"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(run->projectId);
    query.addBindValue(run->workflowId);
    query.addBindValue(run->status);
    query.addBindValue(run->origin);
    query.addBindValue(run->providerName);
    query.addBindValue(run->modelName);
    query.addBindValue(run->summary);
    query.addBindValue(run->outputText);
    query.addBindValue(run->logText);
    query.addBindValue(run->errorMessage);
    query.addBindValue(run->savedMemoryCount);
    query.addBindValue(run->startedAt.toString(Qt::ISODate));
    query.addBindValue(run->finishedAt.isValid() ? run->finishedAt.toString(Qt::ISODate) : QString());

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    run->id = query.lastInsertId().toLongLong();
    return true;
}

bool RunService::finishRun(domain::Run* run, QString* errorMessage) const
{
    if (run == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Run uebergeben.";
        }
        return false;
    }

    if (run->id <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Run-Update braucht eine gueltige ID.";
        }
        return false;
    }

    if (!run->finishedAt.isValid()) {
        run->finishedAt = QDateTime::currentDateTimeUtc();
    }

    run->status = run->status.trimmed().isEmpty() ? "completed" : run->status.trimmed();
    run->origin = normalizedTrimmedText(run->origin);
    run->providerName = normalizedTrimmedText(run->providerName);
    run->modelName = normalizedTrimmedText(run->modelName);
    run->summary = normalizedTrimmedText(run->summary);
    run->outputText = normalizedText(run->outputText);
    run->logText = normalizedText(run->logText);
    run->errorMessage = normalizedTrimmedText(run->errorMessage);

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "UPDATE runs "
        "SET status = ?, origin = ?, provider_name = ?, model_name = ?, summary = ?, output_text = ?, "
        "log_text = ?, error_message = ?, saved_memory_count = ?, finished_at = ? "
        "WHERE id = ?"
    );
    query.addBindValue(run->status);
    query.addBindValue(run->origin);
    query.addBindValue(run->providerName);
    query.addBindValue(run->modelName);
    query.addBindValue(run->summary);
    query.addBindValue(run->outputText);
    query.addBindValue(run->logText);
    query.addBindValue(run->errorMessage);
    query.addBindValue(run->savedMemoryCount);
    query.addBindValue(run->finishedAt.toString(Qt::ISODate));
    query.addBindValue(run->id);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

int RunService::recoverInterruptedRuns(QString* errorMessage) const
{
    const QString recoveryTimestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString recoveryMessage = "Lauf wurde beim App-Start als unterbrochen markiert.";
    const QString recoveryLogLine = QString("[%1] %2").arg(recoveryTimestamp, recoveryMessage);

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "UPDATE runs "
        "SET status = 'interrupted', "
        "summary = CASE "
        "    WHEN TRIM(COALESCE(summary, '')) = '' THEN ? "
        "    ELSE summary "
        "END, "
        "log_text = CASE "
        "    WHEN TRIM(COALESCE(log_text, '')) = '' THEN ? "
        "    ELSE log_text || '\n' || ? "
        "END, "
        "error_message = CASE "
        "    WHEN TRIM(COALESCE(error_message, '')) = '' THEN ? "
        "    ELSE error_message "
        "END, "
        "finished_at = ? "
        "WHERE LOWER(status) = 'running' AND (finished_at IS NULL OR TRIM(finished_at) = '')"
    );
    query.addBindValue(recoveryMessage);
    query.addBindValue(recoveryLogLine);
    query.addBindValue(recoveryLogLine);
    query.addBindValue(recoveryMessage);
    query.addBindValue(recoveryTimestamp);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return -1;
    }

    return query.numRowsAffected();
}

} // namespace privateclaw::services
