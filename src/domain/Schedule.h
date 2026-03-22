#pragma once

#include <QDateTime>
#include <QString>

namespace privateclaw::domain {

struct Schedule
{
    qint64 id = -1;
    qint64 projectId = -1;
    qint64 workflowId = -1;
    QString triggerType;
    QString triggerExpression;
    QDateTime nextRunAt;
    bool enabled = true;
};

} // namespace privateclaw::domain

