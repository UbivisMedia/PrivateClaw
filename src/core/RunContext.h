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
    int memoryEntryCount = 0;
    int directMemoryEntryCount = 0;
    int compressedMemoryEntryCount = 0;
    int pinnedMemoryEntryCount = 0;
    int totalPinnedMemoryEntryCount = 0;
    bool allowShellRun = false;
    bool allowFileEditDiff = false;
    bool allowHttpRequest = false;
};

} // namespace privateclaw::core
