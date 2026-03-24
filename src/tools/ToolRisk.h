#pragma once

#include "domain/Workflow.h"

#include <QList>
#include <QString>

namespace privateclaw::tools {

enum class RiskyToolKind
{
    None,
    FileEditDiff,
    HttpRequest,
    ShellRun
};

struct RiskyToolStep
{
    RiskyToolKind kind = RiskyToolKind::None;
    QString toolName;
    QString stepId;
    QString detail;
};

RiskyToolKind classifyRiskyTool(const QString& toolName);
QString riskyToolDisplayName(RiskyToolKind kind);
QList<RiskyToolStep> collectRiskyToolSteps(const domain::Workflow& workflow);

} // namespace privateclaw::tools
