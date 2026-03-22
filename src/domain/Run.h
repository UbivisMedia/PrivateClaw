#pragma once

#include <QDateTime>
#include <QString>

namespace privateclaw::domain {

struct Run
{
    qint64 id = -1;
    qint64 projectId = -1;
    qint64 workflowId = -1;
    QString status;
    QString summary;
    QDateTime startedAt;
    QDateTime finishedAt;
};

} // namespace privateclaw::domain

