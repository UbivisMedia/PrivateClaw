#include "services/ScheduleService.h"

#include "scheduler/SchedulerService.h"
#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

namespace privateclaw::services {

namespace {

domain::Schedule mapSchedule(const QSqlQuery& query)
{
    domain::Schedule schedule;
    schedule.id = query.value(0).toLongLong();
    schedule.projectId = query.value(1).toLongLong();
    schedule.workflowId = query.value(2).toLongLong();
    schedule.triggerType = query.value(3).toString();
    schedule.triggerExpression = query.value(4).toString();
    schedule.nextRunAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
    schedule.enabled = query.value(6).toInt() != 0;
    schedule.lastRunAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
    return schedule;
}

} // namespace

ScheduleService::ScheduleService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

int ScheduleService::scheduleCount() const
{
    QSqlQuery query(m_databaseManager.database());
    if (!query.exec("SELECT COUNT(*) FROM schedules")) {
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QList<domain::Schedule> ScheduleService::listSchedules() const
{
    QList<domain::Schedule> schedules;

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "SELECT id, project_id, workflow_id, trigger_type, trigger_expression, next_run_at, enabled, last_run_at "
        "FROM schedules "
        "ORDER BY enabled DESC, "
        "CASE WHEN next_run_at IS NULL OR next_run_at = '' THEN 1 ELSE 0 END ASC, "
        "next_run_at ASC, id DESC"
    );

    if (!query.exec()) {
        return schedules;
    }

    while (query.next()) {
        schedules.append(mapSchedule(query));
    }

    return schedules;
}

QList<domain::Schedule> ScheduleService::dueSchedules(const QDateTime& nowUtc) const
{
    QList<domain::Schedule> schedules;

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "SELECT s.id, s.project_id, s.workflow_id, s.trigger_type, s.trigger_expression, s.next_run_at, s.enabled, s.last_run_at "
        "FROM schedules s "
        "INNER JOIN workflows w ON w.id = s.workflow_id AND w.project_id = s.project_id "
        "INNER JOIN projects p ON p.id = s.project_id "
        "WHERE s.enabled = 1 "
        "AND w.is_active = 1 "
        "AND s.next_run_at IS NOT NULL "
        "AND s.next_run_at <> '' "
        "AND s.next_run_at <= ? "
        "ORDER BY s.next_run_at ASC, s.id ASC"
    );
    query.addBindValue(nowUtc.toUTC().toString(Qt::ISODate));

    if (!query.exec()) {
        return schedules;
    }

    while (query.next()) {
        schedules.append(mapSchedule(query));
    }

    return schedules;
}

bool ScheduleService::saveSchedule(domain::Schedule* schedule, QString* errorMessage) const
{
    if (schedule == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Zeitplan uebergeben.";
        }
        return false;
    }

    if (schedule->projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zeitplan braucht ein gueltiges Projekt.";
        }
        return false;
    }

    if (schedule->workflowId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zeitplan braucht einen gueltigen Workflow.";
        }
        return false;
    }

    scheduler::SchedulerService schedulerService;
    schedule->triggerType = schedulerService.normalizedTriggerType(schedule->triggerType);
    const QString validationError = schedulerService.validateSchedule(*schedule);
    if (!validationError.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = validationError;
        }
        return false;
    }

    schedule->nextRunAt = schedulerService.calculateNextRunAtUtc(*schedule, QDateTime::currentDateTime());

    QSqlQuery query(m_databaseManager.database());
    if (schedule->id > 0) {
        query.prepare(
            "UPDATE schedules "
            "SET project_id = ?, workflow_id = ?, trigger_type = ?, trigger_expression = ?, next_run_at = ?, enabled = ?, last_run_at = ? "
            "WHERE id = ?"
        );
        query.addBindValue(schedule->projectId);
        query.addBindValue(schedule->workflowId);
        query.addBindValue(schedule->triggerType);
        query.addBindValue(schedule->triggerExpression.trimmed());
        query.addBindValue(schedule->nextRunAt.isValid() ? schedule->nextRunAt.toUTC().toString(Qt::ISODate) : QString());
        query.addBindValue(schedule->enabled ? 1 : 0);
        query.addBindValue(schedule->lastRunAt.isValid() ? schedule->lastRunAt.toUTC().toString(Qt::ISODate) : QString());
        query.addBindValue(schedule->id);
    } else {
        query.prepare(
            "INSERT INTO schedules (project_id, workflow_id, trigger_type, trigger_expression, next_run_at, enabled, last_run_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"
        );
        query.addBindValue(schedule->projectId);
        query.addBindValue(schedule->workflowId);
        query.addBindValue(schedule->triggerType);
        query.addBindValue(schedule->triggerExpression.trimmed());
        query.addBindValue(schedule->nextRunAt.isValid() ? schedule->nextRunAt.toUTC().toString(Qt::ISODate) : QString());
        query.addBindValue(schedule->enabled ? 1 : 0);
        query.addBindValue(schedule->lastRunAt.isValid() ? schedule->lastRunAt.toUTC().toString(Qt::ISODate) : QString());
    }

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (schedule->id <= 0) {
        schedule->id = query.lastInsertId().toLongLong();
    }

    schedule->triggerExpression = schedule->triggerExpression.trimmed();
    return true;
}

bool ScheduleService::deleteSchedule(const qint64 scheduleId, QString* errorMessage) const
{
    if (scheduleId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein gueltiger Zeitplan ausgewaehlt.";
        }
        return false;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare("DELETE FROM schedules WHERE id = ?");
    query.addBindValue(scheduleId);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

bool ScheduleService::completeScheduleRun(
    domain::Schedule* schedule,
    const QDateTime& finishedAtUtc,
    QString* errorMessage
) const
{
    if (schedule == nullptr || schedule->id <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zum Aktualisieren des Zeitplans wird eine gueltige ID benoetigt.";
        }
        return false;
    }

    scheduler::SchedulerService schedulerService;
    schedule->lastRunAt = finishedAtUtc.toUTC();
    schedule->nextRunAt = schedulerService.calculateNextRunAtAfterExecutionUtc(*schedule, finishedAtUtc.toUTC());
    if (schedulerService.normalizedTriggerType(schedule->triggerType) == "once") {
        schedule->enabled = false;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "UPDATE schedules "
        "SET next_run_at = ?, last_run_at = ?, enabled = ? "
        "WHERE id = ?"
    );
    query.addBindValue(schedule->nextRunAt.isValid() ? schedule->nextRunAt.toUTC().toString(Qt::ISODate) : QString());
    query.addBindValue(schedule->lastRunAt.isValid() ? schedule->lastRunAt.toUTC().toString(Qt::ISODate) : QString());
    query.addBindValue(schedule->enabled ? 1 : 0);
    query.addBindValue(schedule->id);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

} // namespace privateclaw::services
