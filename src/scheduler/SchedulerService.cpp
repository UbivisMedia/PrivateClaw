#include "scheduler/SchedulerService.h"

namespace privateclaw::scheduler {

QString SchedulerService::describeSchedule(const domain::Schedule& schedule) const
{
    return QString("Trigger '%1' mit Ausdruck '%2'")
        .arg(schedule.triggerType, schedule.triggerExpression);
}

} // namespace privateclaw::scheduler

