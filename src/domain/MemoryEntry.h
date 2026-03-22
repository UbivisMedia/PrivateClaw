#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace privateclaw::domain {

struct MemoryEntry
{
    qint64 id = -1;
    qint64 projectId = -1;
    QString type;
    QString content;
    QString source;
    QStringList tags;
    int relevance = 0;
    QDateTime createdAt;
};

} // namespace privateclaw::domain

