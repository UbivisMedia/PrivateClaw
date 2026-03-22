#pragma once

#include "domain/Schedule.h"

#include <QString>

namespace privateclaw::scheduler {

class SchedulerService
{
public:
    QString describeSchedule(const domain::Schedule& schedule) const;
};

} // namespace privateclaw::scheduler

