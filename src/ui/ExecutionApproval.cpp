#include "ui/ExecutionApproval.h"

#include "tools/ToolRisk.h"

#include <QMessageBox>
#include <QStringList>

namespace privateclaw::ui {

namespace {

bool projectRequiresConfirmation(const domain::Project& project, const tools::RiskyToolKind kind)
{
    switch (kind) {
    case tools::RiskyToolKind::ShellRun:
        return project.confirmShellRun;
    case tools::RiskyToolKind::FileEditDiff:
        return project.confirmFileEditDiff;
    case tools::RiskyToolKind::HttpRequest:
        return project.confirmHttpRequest;
    case tools::RiskyToolKind::None:
    default:
        return false;
    }
}

void setRiskAllowance(RiskApprovalDecision* decision, const tools::RiskyToolKind kind)
{
    if (decision == nullptr) {
        return;
    }

    switch (kind) {
    case tools::RiskyToolKind::ShellRun:
        decision->allowShellRun = true;
        break;
    case tools::RiskyToolKind::FileEditDiff:
        decision->allowFileEditDiff = true;
        break;
    case tools::RiskyToolKind::HttpRequest:
        decision->allowHttpRequest = true;
        break;
    case tools::RiskyToolKind::None:
    default:
        break;
    }
}

QString formatRiskySteps(const QList<tools::RiskyToolStep>& steps)
{
    QStringList lines;
    lines.reserve(steps.size());
    for (const tools::RiskyToolStep& step : steps) {
        QString line = QString("- %1 in Schritt '%2'")
                           .arg(tools::riskyToolDisplayName(step.kind), step.stepId);
        if (!step.detail.isEmpty()) {
            line += QString(": %1").arg(step.detail);
        }
        lines.append(line);
    }
    return lines.join('\n');
}

} // namespace

RiskApprovalDecision evaluateRiskyToolExecution(
    const domain::Project& project,
    const domain::Workflow& workflow,
    const RiskApprovalOptions& options
)
{
    RiskApprovalDecision decision;
    const QList<tools::RiskyToolStep> riskySteps = tools::collectRiskyToolSteps(workflow);
    if (riskySteps.isEmpty()) {
        return decision;
    }

    QList<tools::RiskyToolStep> stepsRequiringConfirmation;
    for (const tools::RiskyToolStep& step : riskySteps) {
        if (!projectRequiresConfirmation(project, step.kind)) {
            setRiskAllowance(&decision, step.kind);
            continue;
        }

        if (options.unattended && !project.allowUnattendedRiskyTools) {
            decision.allowed = false;
            decision.message = QString(
                "%1 wurde blockiert, weil projektbezogene Richtlinien fuer riskante Tools eine manuelle Freigabe verlangen:\n%2"
            )
                                   .arg(options.executionLabel.isEmpty() ? "Der Lauf" : options.executionLabel)
                                   .arg(formatRiskySteps(riskySteps));
            return decision;
        }

        stepsRequiringConfirmation.append(step);
    }

    if (!stepsRequiringConfirmation.isEmpty()) {
        if (!options.unattended) {
            const QString title = options.executionLabel.isEmpty()
                ? "Riskante Tools bestaetigen"
                : QString("%1 bestaetigen").arg(options.executionLabel);
            const QString text = QString(
                "%1 nutzt riskante Tools, fuer die dieses Projekt eine manuelle Freigabe verlangt:\n\n%2\n\nDiesen Lauf jetzt starten?"
            )
                                     .arg(options.executionLabel.isEmpty() ? "Der Workflow" : options.executionLabel)
                                     .arg(formatRiskySteps(stepsRequiringConfirmation));

            const QMessageBox::StandardButton answer = QMessageBox::question(
                options.dialogParent,
                title,
                text,
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
            );
            if (answer != QMessageBox::Yes) {
                decision.allowed = false;
                decision.message = QString(
                    "%1 wurde abgebrochen, weil die Freigabe fuer riskante Tools nicht bestaetigt wurde."
                ).arg(options.executionLabel.isEmpty() ? "Der Lauf" : options.executionLabel);
                return decision;
            }
        }

        for (const tools::RiskyToolStep& step : stepsRequiringConfirmation) {
            setRiskAllowance(&decision, step.kind);
        }
    }

    return decision;
}

} // namespace privateclaw::ui
