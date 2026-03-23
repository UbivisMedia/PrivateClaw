#pragma once

#include "domain/MemoryEntry.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace privateclaw::tools {

struct ToolExecutionRequest
{
    QString toolName;
    QJsonObject config;
    qint64 projectId = -1;
    QString projectName;
    QString workflowName;
    QString stepId;
};

struct ToolExecutionResult
{
    bool success = false;
    QString outputText;
    QString errorMessage;
    QStringList logs;
    QList<domain::MemoryEntry> memoryEntriesToPersist;
};

class ToolExecutor
{
public:
    ToolExecutor(QString workspaceRoot = QString(), QString comfyUiBaseUrl = QString());

    QStringList availableTools() const;
    ToolExecutionResult execute(const ToolExecutionRequest& request) const;

    QString workspaceRoot() const;
    QString comfyUiBaseUrl() const;

private:
    ToolExecutionResult executeFileRead(const QJsonObject& config) const;
    ToolExecutionResult executeDirectoryReadRecursive(const QJsonObject& config) const;
    ToolExecutionResult executeDirectoryReadChanged(const QJsonObject& config) const;
    ToolExecutionResult executeMemoryIngestDirectory(const ToolExecutionRequest& request) const;
    ToolExecutionResult executeFileEditDiff(const QJsonObject& config) const;
    ToolExecutionResult executeComfyUiWorkflow(const QJsonObject& config) const;

    QString resolveWorkspacePath(const QString& path, bool allowNonExisting, QString* errorMessage) const;
    bool isPathWithinWorkspace(const QString& absolutePath) const;

    QString m_workspaceRoot;
    QString m_comfyUiBaseUrl;
};

} // namespace privateclaw::tools
