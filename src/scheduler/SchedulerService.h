#pragma once

#include "domain/Schedule.h"

#include <QDateTime>
#include <QString>

namespace privateclaw::scheduler {

class SchedulerService
{
public:
    QString normalizedTriggerType(const QString& triggerType) const;
    QString validateSchedule(const domain::Schedule& schedule) const;
    QString describeSchedule(const domain::Schedule& schedule) const;
    QDateTime calculateNextRunAtUtc(
        const domain::Schedule& schedule,
        const QDateTime& referenceLocalTime = QDateTime::currentDateTime()
    ) const;
    QDateTime calculateNextRunAtAfterExecutionUtc(
        const domain::Schedule& schedule,
        const QDateTime& finishedAtUtc
    ) const;
};

} // namespace privateclaw::scheduler
