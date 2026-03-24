#pragma once

#include "domain/Project.h"
#include "domain/Workflow.h"

#include <QString>

class QWidget;

namespace privateclaw::ui {

struct RiskApprovalOptions
{
    QWidget* dialogParent = nullptr;
    bool unattended = false;
    QString executionLabel;
};

struct RiskApprovalDecision
{
    bool allowed = true;
    bool allowShellRun = false;
    bool allowFileEditDiff = false;
    bool allowHttpRequest = false;
    QString message;
};

RiskApprovalDecision evaluateRiskyToolExecution(
    const domain::Project& project,
    const domain::Workflow& workflow,
    const RiskApprovalOptions& options
);

} // namespace privateclaw::ui
