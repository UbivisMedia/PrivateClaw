#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace privateclaw::core {

struct RunContext
{
    qint64 projectId = -1;
    qint64 workflowId = -1;
    QString projectName;
    QString workflowName;
    QString selectedModel;
    QString systemPrompt;
    QHash<QString, QString> variables;
    QStringList memorySnippets;
};

} // namespace privateclaw::core

