#pragma once

#include "domain/MemoryEntry.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace privateclaw::providers {
class ILlmProvider;
}

namespace privateclaw::tools {

struct ToolExecutionRequest
{
    QString toolName;
    QJsonObject config;
    qint64 projectId = -1;
    QString projectName;
    QString workflowName;
    QString stepId;
    QString selectedModel;
    QString systemPrompt;
    providers::ILlmProvider* llmProvider = nullptr;
    bool allowShellRun = false;
    bool allowFileEditDiff = false;
    bool allowHttpRequest = false;
};

struct ToolExecutionResult
{
    bool success = false;
    QString outputText;
    QString errorMessage;
    QStringList logs;
    QHash<QString, QString> outputVariables;
    QList<domain::MemoryEntry> memoryEntriesToPersist;
};

class ToolExecutor
{
public:
    ToolExecutor(
        QString workspaceRoot = QString(),
        QString comfyUiBaseUrl = QString(),
        QString databasePath = QString(),
        QStringList allowedToolPaths = {}
    );

    QStringList availableTools() const;
    ToolExecutionResult execute(const ToolExecutionRequest& request) const;

    QString workspaceRoot() const;
    QString comfyUiBaseUrl() const;
    QString databasePath() const;

private:
    ToolExecutionResult executeFileRead(const QJsonObject& config) const;
    ToolExecutionResult executeJsonExtract(const QJsonObject& config) const;
    ToolExecutionResult executeCsvRead(const QJsonObject& config) const;
    ToolExecutionResult executeCsvWrite(const QJsonObject& config) const;
    ToolExecutionResult executeDirectoryReadRecursive(const QJsonObject& config) const;
    ToolExecutionResult executeDirectoryReadChanged(const QJsonObject& config) const;
    ToolExecutionResult executeDirectoryList(const QJsonObject& config) const;
    ToolExecutionResult executeMemorySearch(const ToolExecutionRequest& request) const;
    ToolExecutionResult executeMemorySummarize(const ToolExecutionRequest& request) const;
    ToolExecutionResult executeMemoryDeleteOld(const ToolExecutionRequest& request) const;
    ToolExecutionResult executeMemoryIngestDirectory(const ToolExecutionRequest& request) const;
    ToolExecutionResult executeVariablesSet(const QJsonObject& config) const;
    ToolExecutionResult executeFileWriteText(const QJsonObject& config) const;
    ToolExecutionResult executeFileEditDiff(const QJsonObject& config) const;
    ToolExecutionResult executeHttpRequest(const QJsonObject& config) const;
    ToolExecutionResult executeShellRun(const QJsonObject& config) const;
    ToolExecutionResult executeComfyUiWorkflow(const QJsonObject& config) const;

    QString resolveWorkspacePath(const QString& path, bool allowNonExisting, QString* errorMessage) const;
    bool isPathWithinAllowedRoots(const QString& absolutePath) const;

    QString m_workspaceRoot;
    QString m_comfyUiBaseUrl;
    QString m_databasePath;
    QStringList m_allowedToolPaths;
};

} // namespace privateclaw::tools
