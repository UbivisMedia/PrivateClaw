#pragma once

#include <QDateTime>
#include <QString>

namespace privateclaw::domain {

struct ProjectVariable
{
    qint64 id = -1;
    qint64 projectId = -1;
    QString name;
    QString valueType = "string";
    QString valueText;
    QDateTime createdAt;
    QDateTime updatedAt;
};

} // namespace privateclaw::domain
