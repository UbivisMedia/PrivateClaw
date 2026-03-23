#include "core/WorkflowEngine.h"

#include <QJsonObject>
#include <QRegularExpression>

namespace privateclaw::core {

namespace {

QString configString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

QString defaultSystemPrompt()
{
    return QStringLiteral(
        "Du bist ein praeziser Assistent fuer Projektarbeit. "
        "Antworte standardmaessig auf Deutsch. "
        "Nutze nur Informationen aus dem gegebenen Kontext und erfinde keine Fakten. "
        "Wenn fuer eine Aufgabe wichtige Informationen fehlen, sage das klar und knapp. "
        "Wenn eine Zusammenfassung in Stichpunkten gewuenscht ist, liefere nur kurze Stichpunkte."
    );
}

QString memoryContextBlock(const QStringList& memorySnippets)
{
    if (memorySnippets.isEmpty()) {
        return {};
    }

    return QStringLiteral(
               "Projektwissen aus persistenter Erinnerung:\n"
               "%1\n"
               "Nutze diese Informationen nur, wenn sie zur Aufgabe passen."
           )
        .arg(memorySnippets.join("\n"));
}

QString previewText(QString text)
{
    text = text.simplified();
    if (text.size() > 180) {
        text = text.left(177) + "...";
    }
    return text;
}

} // namespace

QString WorkflowEngine::validateWorkflow(const domain::Workflow& workflow) const
{
    if (workflow.name.trimmed().isEmpty()) {
        return "Workflow braucht einen Namen.";
    }

    if (workflow.steps.isEmpty()) {
        return "Workflow enthaelt noch keine Schritte.";
    }

    for (const domain::WorkflowStep& step : workflow.steps) {
        if (step.id.trimmed().isEmpty() || step.type.trimmed().isEmpty()) {
            return "Jeder Workflow-Schritt braucht mindestens 'id' und 'type'.";
        }

        if (step.type.compare("prompt", Qt::CaseInsensitive) != 0) {
            return QString(
                "Schritt '%1' nutzt Typ '%2'. Aktuell wird nur 'prompt' unterstuetzt."
            ).arg(step.id, step.type);
        }

        if (configString(step.config, "prompt").isEmpty()) {
            return QString("Prompt-Schritt '%1' braucht ein config.prompt-Feld.").arg(step.id);
        }
    }

    return {};
}

QString WorkflowEngine::previewExecution(const domain::Workflow& workflow, const RunContext& runContext) const
{
    return QString("Workflow '%1' wurde fuer Projekt '%2' im Vorschaumodus vorbereitet.")
        .arg(workflow.name, runContext.projectName);
}

ExecutionResult WorkflowEngine::executeWorkflow(
    const domain::Workflow& workflow,
    RunContext runContext,
    providers::ILlmProvider& provider
) const
{
    ExecutionResult result;
    result.logs.append(
        QString("[engine] Starte Workflow '%1' fuer Projekt '%2' ueber Provider '%3'.")
            .arg(workflow.name, runContext.projectName, provider.name())
    );

    const QString validationError = validateWorkflow(workflow);
    if (!validationError.isEmpty()) {
        result.errorMessage = validationError;
        result.logs.append(QString("[engine] Abbruch: %1").arg(validationError));
        return result;
    }

    if (runContext.selectedModel.trimmed().isEmpty()) {
        result.errorMessage = "Kein Modell fuer die Workflow-Ausfuehrung konfiguriert.";
        result.logs.append(QString("[engine] Abbruch: %1").arg(result.errorMessage));
        return result;
    }

    runContext.variables.insert("project_name", runContext.projectName);
    runContext.variables.insert("workflow_name", runContext.workflowName);
    runContext.variables.insert("selected_model", runContext.selectedModel);

    for (const domain::WorkflowStep& step : workflow.steps) {
        const QString promptTemplate = configString(step.config, "prompt");
        const QString outputKey = configString(step.config, "output").isEmpty()
            ? step.id
            : configString(step.config, "output");
        const QString model = configString(step.config, "model").isEmpty()
            ? runContext.selectedModel
            : configString(step.config, "model");

        QString systemPrompt = configString(step.config, "system_prompt");
        if (systemPrompt.isEmpty()) {
            systemPrompt = runContext.systemPrompt;
        }
        if (systemPrompt.isEmpty()) {
            systemPrompt = defaultSystemPrompt();
        }

        const bool promptUsesExplicitMemory = promptTemplate.contains("{{project_memory}}")
            || systemPrompt.contains("{{project_memory}}");
        const QString renderedPrompt = renderTemplate(promptTemplate, runContext);
        systemPrompt = renderTemplate(systemPrompt, runContext);
        if (!promptUsesExplicitMemory) {
            const QString memoryBlock = memoryContextBlock(runContext.memorySnippets);
            if (!memoryBlock.isEmpty()) {
                systemPrompt += "\n\n" + memoryBlock;
            }
        }

        result.logs.append(QString("[step:%1] Typ: prompt").arg(step.id));
        result.logs.append(QString("[step:%1] Modell: %2").arg(step.id, model));
        if (!runContext.memorySnippets.isEmpty()) {
            result.logs.append(
                QString("[step:%1] Memory-Kontext: %2 Eintraege verfuegbar.")
                    .arg(step.id)
                    .arg(runContext.memorySnippets.size())
            );
        }
        result.logs.append(QString("[step:%1] Prompt: %2").arg(step.id, previewText(renderedPrompt)));

        providers::ChatRequest request;
        request.model = model;
        request.systemPrompt = systemPrompt;
        request.userPrompt = renderedPrompt;

        const providers::ChatResponse response = provider.chat(request);
        if (!response.success) {
            result.errorMessage = QString("Schritt '%1' fehlgeschlagen: %2").arg(step.id, response.errorMessage);
            result.logs.append(QString("[step:%1] Fehler: %2").arg(step.id, response.errorMessage));
            result.variables = runContext.variables;
            return result;
        }

        runContext.variables.insert(outputKey, response.text);
        runContext.variables.insert("last_response", response.text);
        result.finalOutput = response.text;

        result.logs.append(
            QString("[step:%1] Antwort gespeichert in Variable '%2'.").arg(step.id, outputKey)
        );
        result.logs.append(QString("[step:%1] Ausgabe: %2").arg(step.id, previewText(response.text)));
    }

    result.success = true;
    result.variables = runContext.variables;
    result.logs.append(
        QString("[engine] Workflow erfolgreich abgeschlossen. Letzte Ausgabezeichen: %1")
            .arg(result.finalOutput.size())
    );
    return result;
}

QString WorkflowEngine::renderTemplate(const QString& templateText, const RunContext& runContext) const
{
    static const QRegularExpression placeholderPattern("\\{\\{\\s*([A-Za-z0-9_.-]+)\\s*\\}\\}");

    QString rendered;
    rendered.reserve(templateText.size());

    int lastIndex = 0;
    const QRegularExpressionMatchIterator matches = placeholderPattern.globalMatch(templateText);
    for (QRegularExpressionMatchIterator it = matches; it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        rendered += templateText.mid(lastIndex, match.capturedStart() - lastIndex);

        const QString key = match.captured(1);
        rendered += runContext.variables.value(key);
        lastIndex = match.capturedEnd();
    }

    rendered += templateText.mid(lastIndex);
    return rendered;
}

} // namespace privateclaw::core
