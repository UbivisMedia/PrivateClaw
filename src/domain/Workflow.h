#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace privateclaw::domain {

struct WorkflowStep
{
    QString id;
    QString type;
    QString name;
    QJsonObject config;
};

struct Workflow
{
    qint64 id = -1;
    qint64 projectId = -1;
    QString name;
    QString description;
    QString definitionJson;
    QVector<WorkflowStep> steps;
    bool active = true;
    QDateTime createdAt;
    QDateTime updatedAt;
};

} // namespace privateclaw::domain

