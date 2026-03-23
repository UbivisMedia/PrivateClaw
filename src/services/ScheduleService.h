#pragma once

#include "domain/Schedule.h"

#include <QDateTime>
#include <QList>

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::services {

class ScheduleService
{
public:
    explicit ScheduleService(storage::DatabaseManager& databaseManager);

    int scheduleCount() const;
    QList<domain::Schedule> listSchedules() const;
    QList<domain::Schedule> dueSchedules(const QDateTime& nowUtc = QDateTime::currentDateTimeUtc()) const;
    bool saveSchedule(domain::Schedule* schedule, QString* errorMessage = nullptr) const;
    bool deleteSchedule(qint64 scheduleId, QString* errorMessage = nullptr) const;
    bool completeScheduleRun(
        domain::Schedule* schedule,
        const QDateTime& finishedAtUtc,
        QString* errorMessage = nullptr
    ) const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services
