#include "core/WorkflowEngine.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

namespace privateclaw::core {

namespace {

struct SanitizedPromptResponse
{
    QString visibleText;
    QString reasoningText;
    bool hadReasoningTags = false;
};

QString configString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

QString normalizedStepType(const QString& type)
{
    return type.trimmed().toLower();
}

QString renderTemplateWithVariables(const QString& templateText, const RunContext& runContext)
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

QJsonValue renderJsonValue(const QJsonValue& value, const RunContext& runContext)
{
    if (value.isString()) {
        return renderTemplateWithVariables(value.toString(), runContext);
    }

    if (value.isArray()) {
        QJsonArray renderedArray;
        const QJsonArray sourceArray = value.toArray();
        for (const QJsonValue& item : sourceArray) {
            renderedArray.append(renderJsonValue(item, runContext));
        }
        return renderedArray;
    }

    if (value.isObject()) {
        QJsonObject renderedObject;
        const QJsonObject sourceObject = value.toObject();
        for (auto it = sourceObject.constBegin(); it != sourceObject.constEnd(); ++it) {
            renderedObject.insert(it.key(), renderJsonValue(it.value(), runContext));
        }
        return renderedObject;
    }

    return value;
}

QStringList configTags(const QJsonObject& object, const QString& key, const RunContext& runContext)
{
    QStringList tags;
    const QJsonValue value = object.value(key);
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        tags.reserve(array.size());
        for (const QJsonValue& tagValue : array) {
            const QString tag = tagValue.toString().trimmed();
            if (!tag.isEmpty()) {
                tags.append(tag);
            }
        }
        return tags;
    }

    const QString tagText = value.toString().trimmed();
    if (tagText.isEmpty()) {
        return tags;
    }

    const QString rendered = renderTemplateWithVariables(tagText, runContext);
    const QStringList parts = rendered.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString normalized = part.trimmed();
        if (!normalized.isEmpty()) {
            tags.append(normalized);
        }
    }

    return tags;
}

int configRelevance(const QJsonObject& object, const QString& key, const int fallback = 50)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return qBound(0, value.toInt(), 100);
    }

    bool ok = false;
    const int parsed = value.toString().trimmed().toInt(&ok);
    return ok ? qBound(0, parsed, 100) : fallback;
}

bool configBool(const QJsonObject& object, const QString& key, const bool fallback = false)
{
    const QJsonValue value = object.value(key);
    if (value.isBool()) {
        return value.toBool();
    }

    const QString text = value.toString().trimmed().toLower();
    if (text == "true" || text == "1" || text == "yes") {
        return true;
    }
    if (text == "false" || text == "0" || text == "no") {
        return false;
    }

    return fallback;
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

QString memorySnippetFromEntry(const domain::MemoryEntry& entry)
{
    QStringList parts;
    parts.append(QString("Typ: %1").arg(entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed()));

    if (!entry.source.trimmed().isEmpty()) {
        parts.append(QString("Quelle: %1").arg(entry.source.trimmed()));
    }

    if (!entry.tags.isEmpty()) {
        parts.append(QString("Tags: %1").arg(entry.tags.join(", ")));
    }

    parts.append(QString("Inhalt: %1").arg(previewText(entry.content)));
    return QString("- %1").arg(parts.join(" | "));
}

QString cleanupVisiblePromptText(QString text)
{
    text.replace(QRegularExpression("\n{3,}"), "\n\n");
    return text.trimmed();
}

SanitizedPromptResponse sanitizePromptResponse(const QString& rawText)
{
    SanitizedPromptResponse result;
    result.visibleText = rawText.trimmed();

    static const QRegularExpression reasoningBlockPattern(
        R"(<\s*(think|thinking|reasoning|analysis|thought|reflection)\b[^>]*>(.*?)<\s*/\s*\1\s*>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
    );
    static const QRegularExpression strayReasoningTagPattern(
        R"(<\s*/?\s*(think|thinking|reasoning|analysis|thought|reflection)\b[^>]*>)",
        QRegularExpression::CaseInsensitiveOption
    );

    QStringList reasoningParts;
    const QRegularExpressionMatchIterator matches = reasoningBlockPattern.globalMatch(result.visibleText);
    for (QRegularExpressionMatchIterator it = matches; it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString reasoning = cleanupVisiblePromptText(match.captured(2));
        if (!reasoning.isEmpty()) {
            reasoningParts.append(reasoning);
        }
        result.hadReasoningTags = true;
    }

    result.visibleText.remove(reasoningBlockPattern);
    if (result.visibleText.contains(strayReasoningTagPattern)) {
        result.visibleText.remove(strayReasoningTagPattern);
        result.hadReasoningTags = true;
    }

    result.visibleText = cleanupVisiblePromptText(result.visibleText);
    result.reasoningText = reasoningParts.join("\n\n").trimmed();
    return result;
}

bool validateTargetStepId(
    const QString& targetStepId,
    const QSet<QString>& knownStepIds,
    const QString& currentStepId,
    const QString& key,
    QString* errorMessage
)
{
    if (targetStepId.trimmed().isEmpty()) {
        return true;
    }

    if (knownStepIds.contains(targetStepId.trimmed())) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QString(
            "Schritt '%1' verweist in '%2' auf unbekannten Schritt '%3'."
        ).arg(currentStepId, key, targetStepId);
    }
    return false;
}

bool evaluateDecisionOperator(
    const QString& operatorName,
    const QString& inputValue,
    const QString& comparisonValue,
    const bool caseSensitive,
    bool* matched,
    QString* errorMessage
)
{
    if (matched != nullptr) {
        *matched = false;
    }

    const QString normalizedOperator = operatorName.trimmed().toLower();
    if (normalizedOperator.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Decision-Operator fehlt.";
        }
        return false;
    }

    const QString left = caseSensitive ? inputValue : inputValue.toCaseFolded();
    const QString right = caseSensitive ? comparisonValue : comparisonValue.toCaseFolded();

    bool localMatch = false;
    if (normalizedOperator == "equals") {
        localMatch = left == right;
    } else if (normalizedOperator == "not_equals") {
        localMatch = left != right;
    } else if (normalizedOperator == "contains") {
        localMatch = left.contains(right);
    } else if (normalizedOperator == "not_contains") {
        localMatch = !left.contains(right);
    } else if (normalizedOperator == "starts_with") {
        localMatch = left.startsWith(right);
    } else if (normalizedOperator == "ends_with") {
        localMatch = left.endsWith(right);
    } else if (normalizedOperator == "empty") {
        localMatch = inputValue.trimmed().isEmpty();
    } else if (normalizedOperator == "not_empty") {
        localMatch = !inputValue.trimmed().isEmpty();
    } else if (normalizedOperator == "regex") {
        const QRegularExpression pattern(
            comparisonValue,
            caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption
        );
        if (!pattern.isValid()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Ungueltiger Regex-Ausdruck: %1").arg(pattern.errorString());
            }
            return false;
        }
        localMatch = pattern.match(inputValue).hasMatch();
    } else {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Unbekannter Decision-Operator '%1'.").arg(operatorName);
        }
        return false;
    }

    if (matched != nullptr) {
        *matched = localMatch;
    }
    return true;
}

int resolveNextStepIndex(
    const QString& targetStepId,
    const QHash<QString, int>& stepIndexById,
    const int sequentialNextIndex,
    QString* errorMessage
)
{
    if (targetStepId.trimmed().isEmpty()) {
        return sequentialNextIndex;
    }

    const auto it = stepIndexById.constFind(targetStepId.trimmed());
    if (it == stepIndexById.constEnd()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Unbekannter Zielschritt '%1'.").arg(targetStepId);
        }
        return -1;
    }

    return it.value();
}

} // namespace

WorkflowEngine::WorkflowEngine(const tools::ToolExecutor* toolExecutor)
    : m_toolExecutor(toolExecutor)
{
}

void WorkflowEngine::setToolExecutor(const tools::ToolExecutor* toolExecutor)
{
    m_toolExecutor = toolExecutor;
}

QString WorkflowEngine::validateWorkflow(const domain::Workflow& workflow) const
{
    if (workflow.name.trimmed().isEmpty()) {
        return "Workflow braucht einen Namen.";
    }

    if (workflow.steps.isEmpty()) {
        return "Workflow enthaelt noch keine Schritte.";
    }

    QSet<QString> stepIds;
    for (const domain::WorkflowStep& step : workflow.steps) {
        if (step.id.trimmed().isEmpty() || step.type.trimmed().isEmpty()) {
            return "Jeder Workflow-Schritt braucht mindestens 'id' und 'type'.";
        }

        const QString normalizedStepId = step.id.trimmed();
        if (stepIds.contains(normalizedStepId)) {
            return QString("Schritt-ID '%1' ist mehrfach vorhanden.").arg(normalizedStepId);
        }
        stepIds.insert(normalizedStepId);
    }

    for (const domain::WorkflowStep& step : workflow.steps) {
        const QString stepType = normalizedStepType(step.type);

        if (stepType != "prompt" && stepType != "save_memory" && stepType != "decision" && stepType != "tool") {
            return QString(
                "Schritt '%1' nutzt Typ '%2'. Aktuell werden nur 'prompt', 'save_memory', 'decision' und 'tool' unterstuetzt."
            ).arg(step.id, step.type);
        }

        if (stepType == "prompt" && configString(step.config, "prompt").isEmpty()) {
            return QString("Prompt-Schritt '%1' braucht ein config.prompt-Feld.").arg(step.id);
        }

        if (stepType == "tool" && configString(step.config, "tool").isEmpty()) {
            return QString("Tool-Schritt '%1' braucht ein config.tool-Feld.").arg(step.id);
        }

        if (stepType == "decision") {
            const QJsonValue rulesValue = step.config.value("rules");
            const bool hasRules = rulesValue.isArray() && !rulesValue.toArray().isEmpty();

            if (!rulesValue.isUndefined() && !rulesValue.isArray()) {
                return QString("Decision-Schritt '%1' hat ein ungueltiges 'rules'-Feld.").arg(step.id);
            }

            if (!hasRules && configString(step.config, "operator").isEmpty()) {
                return QString(
                    "Decision-Schritt '%1' braucht entweder ein 'rules'-Array oder ein 'operator'-Feld."
                ).arg(step.id);
            }

            if (!validateTargetStepId(configString(step.config, "if_true"), stepIds, step.id, "if_true", nullptr)) {
                return QString(
                    "Schritt '%1' verweist in 'if_true' auf unbekannten Schritt '%2'."
                ).arg(step.id, configString(step.config, "if_true"));
            }
            if (!validateTargetStepId(configString(step.config, "if_false"), stepIds, step.id, "if_false", nullptr)) {
                return QString(
                    "Schritt '%1' verweist in 'if_false' auf unbekannten Schritt '%2'."
                ).arg(step.id, configString(step.config, "if_false"));
            }
            if (!validateTargetStepId(configString(step.config, "default_next"), stepIds, step.id, "default_next", nullptr)) {
                return QString(
                    "Schritt '%1' verweist in 'default_next' auf unbekannten Schritt '%2'."
                ).arg(step.id, configString(step.config, "default_next"));
            }

            if (hasRules) {
                const QJsonArray rules = rulesValue.toArray();
                for (int index = 0; index < rules.size(); ++index) {
                    if (!rules.at(index).isObject()) {
                        return QString("Decision-Schritt '%1' hat in 'rules' einen ungueltigen Eintrag.").arg(step.id);
                    }

                    const QJsonObject rule = rules.at(index).toObject();
                    const QString operatorName = configString(rule, "operator");
                    if (operatorName.isEmpty()) {
                        return QString(
                            "Decision-Schritt '%1' braucht in Regel %2 ein 'operator'-Feld."
                        ).arg(step.id, QString::number(index + 1));
                    }

                    QString targetError;
                    if (!validateTargetStepId(configString(rule, "next_step"), stepIds, step.id, "next_step", &targetError)) {
                        return targetError;
                    }
                }
            }
        }

        if (stepType == "tool" && !configString(step.config, "if_true").isEmpty()) {
            return QString("Tool-Schritt '%1' unterstuetzt aktuell keine Decision-Sprungfelder.").arg(step.id);
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
    runContext.variables.insert("project_id", QString::number(runContext.projectId));
    runContext.variables.insert("workflow_name", runContext.workflowName);
    runContext.variables.insert("selected_model", runContext.selectedModel);
    QHash<QString, int> stepIndexById;
    stepIndexById.reserve(workflow.steps.size());
    for (int index = 0; index < workflow.steps.size(); ++index) {
        stepIndexById.insert(workflow.steps.at(index).id.trimmed(), index);
    }

    int currentStepIndex = 0;
    int executedStepCount = 0;
    const int maxStepExecutions = qMax(20, workflow.steps.size() * 20);
    while (currentStepIndex >= 0 && currentStepIndex < workflow.steps.size()) {
        if (++executedStepCount > maxStepExecutions) {
            result.errorMessage = "Workflow wurde wegen zu vieler Schritt-Ausfuehrungen abgebrochen. Pruefe Decision-Spruenge.";
            result.logs.append(QString("[engine] Abbruch: %1").arg(result.errorMessage));
            result.variables = runContext.variables;
            return result;
        }

        const domain::WorkflowStep& step = workflow.steps.at(currentStepIndex);
        const int sequentialNextIndex = currentStepIndex + 1;
        const QString stepType = normalizedStepType(step.type);
        if (stepType == "save_memory") {
            QString contentTemplate = configString(step.config, "content");
            if (contentTemplate.isEmpty()) {
                contentTemplate = "{{last_response}}";
            }

            const QString content = renderTemplate(contentTemplate, runContext).trimmed();
            if (content.isEmpty()) {
                result.errorMessage = QString(
                    "Schritt '%1' konnte keinen Memory-Inhalt erzeugen."
                ).arg(step.id);
                result.logs.append(QString("[step:%1] Fehler: %2").arg(step.id, result.errorMessage));
                result.variables = runContext.variables;
                return result;
            }

            domain::MemoryEntry entry;
            entry.projectId = runContext.projectId;
            entry.type = configString(step.config, "entry_type");
            if (entry.type.isEmpty()) {
                entry.type = configString(step.config, "memory_type");
            }
            if (entry.type.isEmpty()) {
                entry.type = "note";
            }

            entry.source = renderTemplate(configString(step.config, "source"), runContext).trimmed();
            if (entry.source.isEmpty()) {
                entry.source = QString("workflow:%1/%2").arg(runContext.workflowName, step.id);
            }

            entry.tags = configTags(step.config, "tags", runContext);
            entry.relevance = configRelevance(step.config, "relevance", 50);
            entry.content = content;

            result.memoryEntriesToPersist.append(entry);
            runContext.memorySnippets.append(memorySnippetFromEntry(entry));
            runContext.variables.insert("last_memory_content", entry.content);
            runContext.variables.insert("last_memory_type", entry.type);
            runContext.variables.insert("project_memory", runContext.memorySnippets.join("\n"));
            runContext.variables.insert("project_memory_count", QString::number(runContext.memorySnippets.size()));

            result.logs.append(QString("[step:%1] Typ: save_memory").arg(step.id));
            result.logs.append(QString("[step:%1] Memory-Typ: %2").arg(step.id, entry.type));
            result.logs.append(QString("[step:%1] Memory-Quelle: %2").arg(step.id, entry.source));
            result.logs.append(QString("[step:%1] Memory gespeichert vorgemerkt.").arg(step.id));
            result.logs.append(QString("[step:%1] Inhalt: %2").arg(step.id, previewText(entry.content)));
            currentStepIndex = sequentialNextIndex;
            continue;
        }

        if (stepType == "decision") {
            const QString inputTemplate = configString(step.config, "input").isEmpty()
                ? "{{last_response}}"
                : configString(step.config, "input");
            const QString inputValue = renderTemplate(inputTemplate, runContext);
            const QString outputKey = configString(step.config, "output").isEmpty()
                ? step.id
                : configString(step.config, "output");
            const bool caseSensitive = configBool(step.config, "case_sensitive", false);

            result.logs.append(QString("[step:%1] Typ: decision").arg(step.id));
            result.logs.append(QString("[step:%1] Input: %2").arg(step.id, previewText(inputValue)));

            bool matched = false;
            QString decisionValue;
            QString targetStepId;
            QString evaluationError;

            const QJsonValue rulesValue = step.config.value("rules");
            if (rulesValue.isArray() && !rulesValue.toArray().isEmpty()) {
                const QJsonArray rules = rulesValue.toArray();
                for (int index = 0; index < rules.size(); ++index) {
                    const QJsonObject rule = rules.at(index).toObject();
                    const QString operatorName = configString(rule, "operator");
                    const QString comparisonValue = renderTemplate(configString(rule, "value"), runContext);

                    bool ruleMatched = false;
                    if (!evaluateDecisionOperator(
                            operatorName,
                            inputValue,
                            comparisonValue,
                            caseSensitive,
                            &ruleMatched,
                            &evaluationError
                        )) {
                        result.errorMessage = QString("Schritt '%1' fehlgeschlagen: %2").arg(step.id, evaluationError);
                        result.logs.append(QString("[step:%1] Fehler: %2").arg(step.id, evaluationError));
                        result.variables = runContext.variables;
                        return result;
                    }

                    if (!ruleMatched) {
                        continue;
                    }

                    matched = true;
                    decisionValue = renderTemplate(configString(rule, "result"), runContext);
                    if (decisionValue.isEmpty()) {
                        decisionValue = QString("rule_%1").arg(index + 1);
                    }
                    targetStepId = configString(rule, "next_step");
                    result.logs.append(
                        QString("[step:%1] Regel %2 getroffen (%3).")
                            .arg(step.id)
                            .arg(index + 1)
                            .arg(operatorName)
                    );
                    break;
                }

                if (!matched) {
                    decisionValue = renderTemplate(configString(step.config, "default_result"), runContext);
                    if (decisionValue.isEmpty()) {
                        decisionValue = "no_match";
                    }
                    targetStepId = configString(step.config, "default_next");
                    result.logs.append(QString("[step:%1] Keine Regel getroffen.").arg(step.id));
                }
            } else {
                const QString operatorName = configString(step.config, "operator");
                const QString comparisonValue = renderTemplate(configString(step.config, "value"), runContext);
                if (!evaluateDecisionOperator(
                        operatorName,
                        inputValue,
                        comparisonValue,
                        caseSensitive,
                        &matched,
                        &evaluationError
                    )) {
                    result.errorMessage = QString("Schritt '%1' fehlgeschlagen: %2").arg(step.id, evaluationError);
                    result.logs.append(QString("[step:%1] Fehler: %2").arg(step.id, evaluationError));
                    result.variables = runContext.variables;
                    return result;
                }

                if (matched) {
                    decisionValue = renderTemplate(configString(step.config, "true_result"), runContext);
                    if (decisionValue.isEmpty()) {
                        decisionValue = "true";
                    }
                    targetStepId = configString(step.config, "if_true");
                } else {
                    decisionValue = renderTemplate(configString(step.config, "false_result"), runContext);
                    if (decisionValue.isEmpty()) {
                        decisionValue = "false";
                    }
                    targetStepId = configString(step.config, "if_false");
                }

                result.logs.append(
                    QString("[step:%1] Entscheidung: %2")
                        .arg(step.id, matched ? "true" : "false")
                );
            }

            runContext.variables.insert(outputKey, decisionValue);
            runContext.variables.insert("last_decision", decisionValue);
            result.logs.append(QString("[step:%1] Ergebnisvariable '%2' = %3").arg(step.id, outputKey, decisionValue));

            QString targetError;
            const int resolvedIndex = resolveNextStepIndex(
                targetStepId,
                stepIndexById,
                sequentialNextIndex,
                &targetError
            );
            if (resolvedIndex < 0) {
                result.errorMessage = QString("Schritt '%1' fehlgeschlagen: %2").arg(step.id, targetError);
                result.logs.append(QString("[step:%1] Fehler: %2").arg(step.id, targetError));
                result.variables = runContext.variables;
                return result;
            }

            if (targetStepId.trimmed().isEmpty()) {
                result.logs.append(QString("[step:%1] Weiter mit naechstem Schritt.").arg(step.id));
            } else {
                result.logs.append(QString("[step:%1] Springe zu '%2'.").arg(step.id, targetStepId));
            }

            currentStepIndex = resolvedIndex;
            continue;
        }

        if (stepType == "tool") {
            if (m_toolExecutor == nullptr) {
                result.errorMessage = QString("Schritt '%1' fehlgeschlagen: Kein ToolExecutor konfiguriert.").arg(step.id);
                result.logs.append(QString("[step:%1] Fehler: %2").arg(step.id, result.errorMessage));
                result.variables = runContext.variables;
                return result;
            }

            const QJsonObject renderedConfig = renderJsonValue(step.config, runContext).toObject();
            const QString toolName = configString(renderedConfig, "tool");
            const QString outputKey = configString(renderedConfig, "output").isEmpty()
                ? step.id
                : configString(renderedConfig, "output");

            result.logs.append(QString("[step:%1] Typ: tool").arg(step.id));
            result.logs.append(QString("[step:%1] Tool: %2").arg(step.id, toolName));

            tools::ToolExecutionRequest request;
            request.toolName = toolName;
            request.config = renderedConfig;
            request.projectId = runContext.projectId;
            request.projectName = runContext.projectName;
            request.workflowName = runContext.workflowName;
            request.stepId = step.id;

            const tools::ToolExecutionResult toolResult = m_toolExecutor->execute(request);
            for (const QString& logLine : toolResult.logs) {
                result.logs.append(QString("[step:%1] %2").arg(step.id, logLine));
            }

            if (!toolResult.success) {
                result.errorMessage = QString("Schritt '%1' fehlgeschlagen: %2").arg(step.id, toolResult.errorMessage);
                result.logs.append(QString("[step:%1] Fehler: %2").arg(step.id, toolResult.errorMessage));
                result.variables = runContext.variables;
                return result;
            }

            runContext.variables.insert(outputKey, toolResult.outputText);
            runContext.variables.insert("last_tool_output", toolResult.outputText);
            runContext.variables.insert("last_tool_name", toolName);
            result.finalOutput = toolResult.outputText;

            if (!toolResult.memoryEntriesToPersist.isEmpty()) {
                for (const domain::MemoryEntry& entry : toolResult.memoryEntriesToPersist) {
                    result.memoryEntriesToPersist.append(entry);
                    runContext.memorySnippets.append(memorySnippetFromEntry(entry));
                    runContext.variables.insert("last_memory_content", entry.content);
                    runContext.variables.insert("last_memory_type", entry.type);
                }
                runContext.variables.insert("project_memory", runContext.memorySnippets.join("\n"));
                runContext.variables.insert("project_memory_count", QString::number(runContext.memorySnippets.size()));
                result.logs.append(
                    QString("[step:%1] Tool hat %2 Memory-Eintrag(e) vorgemerkt.")
                        .arg(step.id)
                        .arg(toolResult.memoryEntriesToPersist.size())
                );
            }

            result.logs.append(
                QString("[step:%1] Tool-Ausgabe gespeichert in Variable '%2'.").arg(step.id, outputKey)
            );
            result.logs.append(QString("[step:%1] Ausgabe: %2").arg(step.id, previewText(toolResult.outputText)));
            currentStepIndex = sequentialNextIndex;
            continue;
        }

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

        const SanitizedPromptResponse sanitizedResponse = sanitizePromptResponse(response.text);

        runContext.variables.insert(outputKey, sanitizedResponse.visibleText);
        runContext.variables.insert("last_response", sanitizedResponse.visibleText);
        runContext.variables.insert(outputKey + "_raw", response.text);
        runContext.variables.insert("last_response_raw", response.text);
        result.finalOutput = sanitizedResponse.visibleText;

        if (sanitizedResponse.hadReasoningTags) {
            runContext.variables.insert(outputKey + "_reasoning", sanitizedResponse.reasoningText);
            runContext.variables.insert("last_reasoning", sanitizedResponse.reasoningText);
            runContext.variables.insert("last_response_reasoning", sanitizedResponse.reasoningText);
            result.logs.append(
                QString("[step:%1] Reasoning-Tags erkannt und aus der sichtbaren Ausgabe entfernt.")
                    .arg(step.id)
            );
            if (!sanitizedResponse.reasoningText.isEmpty()) {
                result.logs.append(
                    QString("[step:%1] Reasoning gespeichert in '%2_reasoning' und 'last_reasoning'.")
                        .arg(step.id, outputKey)
                );
            }
        } else {
            runContext.variables.remove(outputKey + "_reasoning");
            runContext.variables.remove("last_reasoning");
            runContext.variables.remove("last_response_reasoning");
        }

        result.logs.append(
            QString("[step:%1] Antwort gespeichert in Variable '%2'.").arg(step.id, outputKey)
        );
        result.logs.append(QString("[step:%1] Ausgabe: %2").arg(step.id, previewText(sanitizedResponse.visibleText)));
        currentStepIndex = sequentialNextIndex;
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
    return renderTemplateWithVariables(templateText, runContext);
}

} // namespace privateclaw::core
