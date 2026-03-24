#include "tools/ToolRisk.h"

#include <QJsonObject>

namespace privateclaw::tools {

namespace {

QString configString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

QString normalizePreview(QString text)
{
    text = text.simplified();
    if (text.size() > 160) {
        text = text.left(157) + "...";
    }
    return text;
}

QString summarizeRiskyToolDetail(const QString& toolName, const QJsonObject& config)
{
    const QString normalizedToolName = toolName.trimmed().toLower();
    if (normalizedToolName == "shell.run") {
        return normalizePreview(configString(config, "command"));
    }

    if (normalizedToolName == "file.edit_diff") {
        return normalizePreview(configString(config, "path"));
    }

    if (normalizedToolName == "http.request") {
        const QString method = configString(config, "method").isEmpty()
            ? "GET"
            : configString(config, "method").toUpper();
        const QString url = configString(config, "url");
        return normalizePreview(QString("%1 %2").arg(method, url));
    }

    return {};
}

} // namespace

RiskyToolKind classifyRiskyTool(const QString& toolName)
{
    const QString normalizedToolName = toolName.trimmed().toLower();
    if (normalizedToolName == "shell.run") {
        return RiskyToolKind::ShellRun;
    }

    if (normalizedToolName == "file.edit_diff") {
        return RiskyToolKind::FileEditDiff;
    }

    if (normalizedToolName == "http.request") {
        return RiskyToolKind::HttpRequest;
    }

    return RiskyToolKind::None;
}

QString riskyToolDisplayName(const RiskyToolKind kind)
{
    switch (kind) {
    case RiskyToolKind::ShellRun:
        return "shell.run";
    case RiskyToolKind::FileEditDiff:
        return "file.edit_diff";
    case RiskyToolKind::HttpRequest:
        return "http.request";
    case RiskyToolKind::None:
    default:
        return "tool";
    }
}

QList<RiskyToolStep> collectRiskyToolSteps(const domain::Workflow& workflow)
{
    QList<RiskyToolStep> riskySteps;
    for (const domain::WorkflowStep& step : workflow.steps) {
        if (step.type.trimmed().compare("tool", Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QString toolName = configString(step.config, "tool");
        const RiskyToolKind kind = classifyRiskyTool(toolName);
        if (kind == RiskyToolKind::None) {
            continue;
        }

        RiskyToolStep riskyStep;
        riskyStep.kind = kind;
        riskyStep.toolName = toolName;
        riskyStep.stepId = step.id.trimmed();
        riskyStep.detail = summarizeRiskyToolDetail(toolName, step.config);
        riskySteps.append(riskyStep);
    }

    return riskySteps;
}

} // namespace privateclaw::tools
