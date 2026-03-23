#include "scheduler/SchedulerService.h"

#include <QTime>

namespace privateclaw::scheduler {

namespace {

QDateTime parseIsoDateTime(const QString& text)
{
    QDateTime dateTime = QDateTime::fromString(text.trimmed(), Qt::ISODateWithMs);
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(text.trimmed(), Qt::ISODate);
    }
    return dateTime;
}

QTime parseDailyTime(const QString& text)
{
    QTime time = QTime::fromString(text.trimmed(), "HH:mm");
    if (!time.isValid()) {
        time = QTime::fromString(text.trimmed(), "HH:mm:ss");
    }
    return time;
}

QString formatLocalDateTime(const QDateTime& dateTime)
{
    if (!dateTime.isValid()) {
        return "kein gueltiger Zeitpunkt";
    }

    return dateTime.toLocalTime().toString("dd.MM.yyyy HH:mm");
}

} // namespace

QString SchedulerService::normalizedTriggerType(const QString& triggerType) const
{
    return triggerType.trimmed().toLower();
}

QString SchedulerService::validateSchedule(const domain::Schedule& schedule) const
{
    const QString triggerType = normalizedTriggerType(schedule.triggerType);
    const QString expression = schedule.triggerExpression.trimmed();
    if (triggerType.isEmpty()) {
        return "Zeitplan braucht einen Trigger-Typ.";
    }

    if (triggerType == "once") {
        const QDateTime dateTime = parseIsoDateTime(expression);
        if (!dateTime.isValid()) {
            return "Einmalige Zeitplaene brauchen einen gueltigen ISO-Zeitpunkt.";
        }

        if (dateTime.toUTC() <= QDateTime::currentDateTimeUtc()) {
            return "Einmalige Zeitplaene muessen in der Zukunft liegen.";
        }

        return {};
    }

    if (triggerType == "interval_minutes") {
        bool ok = false;
        const int minutes = expression.toInt(&ok);
        if (!ok || minutes <= 0) {
            return "Intervall-Zeitplaene brauchen eine positive Minutenanzahl.";
        }
        return {};
    }

    if (triggerType == "daily_time") {
        if (!parseDailyTime(expression).isValid()) {
            return "Taegliche Zeitplaene brauchen eine Uhrzeit im Format HH:mm.";
        }
        return {};
    }

    return QString("Unbekannter Trigger-Typ '%1'.").arg(schedule.triggerType);
}

QString SchedulerService::describeSchedule(const domain::Schedule& schedule) const
{
    const QString triggerType = normalizedTriggerType(schedule.triggerType);
    if (triggerType == "once") {
        return QString("Einmalig am %1").arg(formatLocalDateTime(parseIsoDateTime(schedule.triggerExpression)));
    }

    if (triggerType == "interval_minutes") {
        const int minutes = schedule.triggerExpression.trimmed().toInt();
        return QString("Alle %1 Minute(n)").arg(minutes > 0 ? minutes : 0);
    }

    if (triggerType == "daily_time") {
        return QString("Taeglich um %1 Uhr").arg(schedule.triggerExpression.trimmed());
    }

    return QString("Trigger '%1' mit Ausdruck '%2'").arg(schedule.triggerType, schedule.triggerExpression);
}

QDateTime SchedulerService::calculateNextRunAtUtc(
    const domain::Schedule& schedule,
    const QDateTime& referenceLocalTime
) const
{
    const QString triggerType = normalizedTriggerType(schedule.triggerType);
    const QString expression = schedule.triggerExpression.trimmed();

    if (triggerType == "once") {
        return parseIsoDateTime(expression).toUTC();
    }

    if (triggerType == "interval_minutes") {
        bool ok = false;
        const int minutes = expression.toInt(&ok);
        return ok && minutes > 0
            ? referenceLocalTime.toUTC().addSecs(minutes * 60)
            : QDateTime();
    }

    if (triggerType == "daily_time") {
        const QTime targetTime = parseDailyTime(expression);
        if (!targetTime.isValid()) {
            return {};
        }

        const QDateTime localReference = referenceLocalTime.isValid()
            ? referenceLocalTime.toLocalTime()
            : QDateTime::currentDateTime();
        QDateTime candidate(localReference.date(), targetTime, Qt::LocalTime);
        if (candidate <= localReference) {
            candidate = candidate.addDays(1);
        }
        return candidate.toUTC();
    }

    return {};
}

QDateTime SchedulerService::calculateNextRunAtAfterExecutionUtc(
    const domain::Schedule& schedule,
    const QDateTime& finishedAtUtc
) const
{
    const QString triggerType = normalizedTriggerType(schedule.triggerType);
    if (triggerType == "once") {
        return {};
    }

    if (triggerType == "interval_minutes") {
        bool ok = false;
        const int minutes = schedule.triggerExpression.trimmed().toInt(&ok);
        return ok && minutes > 0
            ? finishedAtUtc.toUTC().addSecs(minutes * 60)
            : QDateTime();
    }

    if (triggerType == "daily_time") {
        return calculateNextRunAtUtc(schedule, finishedAtUtc.toLocalTime());
    }

    return {};
}

} // namespace privateclaw::scheduler
