#include "core/WorkflowEngine.h"
#include "utils/JsonExtraction.h"
#include "utils/ModelResponseSanitizer.h"
#include "utils/PersistenceGuards.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <algorithm>

namespace privateclaw::core {

namespace {

struct SanitizedPromptResponse
{
    QString visibleText;
    QString reasoningText;
    bool hadReasoningContent = false;
};

QString configString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

QString normalizedStepType(const QString& type)
{
    return type.trimmed().toLower();
}

QString manualCancellationMessage()
{
    return "Workflow wurde manuell abgebrochen.";
}

bool isCancellationRequested(const ExecutionCallbacks& callbacks)
{
    return static_cast<bool>(callbacks.shouldCancel) && callbacks.shouldCancel();
}

int configInt(const QJsonObject& object, const QString& key, const int fallback = 0)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toInt();
    }

    bool ok = false;
    const int parsed = value.toString().trimmed().toInt(&ok);
    return ok ? parsed : fallback;
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

int renderedConfigInt(const QJsonObject& object, const QString& key, const RunContext& runContext, const int fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toInt();
    }

    bool ok = false;
    const int parsed = renderTemplateWithVariables(value.toString(), runContext).trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

QString defaultSystemPrompt()
{
    return QStringLiteral(
        "Du bist ein praeziser Assistent fuer Projektarbeit. "
        "Antworte standardmaessig auf Deutsch. "
        "Gib niemals Thinking Process, Analyse, Planungsnotizen oder Prompt-Wiederholungen aus, "
        "sondern nur die finale Nutzantwort. "
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
    if (entry.pinned) {
        parts.append("Angepinnt");
    }
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

QString debugVariableValueForDisplay(const QString& key, QString value)
{
    if (key.startsWith("secret.", Qt::CaseInsensitive)) {
        return "[SECRET]";
    }

    if (value.size() > 6000) {
        value = value.left(6000) + "\n...[gekuerzt]";
    }
    return value;
}

QList<WorkflowDebugVariable> snapshotDebugVariables(const QHash<QString, QString>& variables)
{
    QList<WorkflowDebugVariable> snapshot;
    QStringList keys = variables.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& left, const QString& right) {
        return left.toCaseFolded() < right.toCaseFolded();
    });

    snapshot.reserve(keys.size());
    for (const QString& key : keys) {
        WorkflowDebugVariable variable;
        variable.key = key;
        variable.value = debugVariableValueForDisplay(key, variables.value(key));
        snapshot.append(variable);
    }

    return snapshot;
}

QString scopedStepId(const QString& scopePrefix, const QString& stepId)
{
    const QString normalizedStepId = stepId.trimmed();
    if (normalizedStepId.isEmpty()) {
        return {};
    }

    return scopePrefix.trimmed().isEmpty() ? normalizedStepId : scopePrefix.trimmed() + "/" + normalizedStepId;
}

QString nextStepLabel(const QVector<domain::WorkflowStep>& steps, const int stepIndex, const QString& scopePrefix = {})
{
    if (stepIndex >= 0 && stepIndex < steps.size()) {
        return scopedStepId(scopePrefix, steps.at(stepIndex).id.trimmed());
    }

    return "[ende]";
}

QVector<domain::WorkflowStep> workflowStepsFromJsonArray(const QJsonArray& stepsArray)
{
    QVector<domain::WorkflowStep> steps;
    steps.reserve(stepsArray.size());
    for (const QJsonValue& stepValue : stepsArray) {
        if (!stepValue.isObject()) {
            continue;
        }

        const QJsonObject stepObject = stepValue.toObject();
        domain::WorkflowStep step;
        step.id = stepObject.value("id").toString();
        step.type = stepObject.value("type").toString();
        step.name = stepObject.value("name").toString();
        step.config = stepObject.value("config").toObject();
        steps.append(step);
    }

    return steps;
}

QString jsonValueToWorkflowText(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        const double numericValue = value.toDouble();
        const qlonglong integralValue = static_cast<qlonglong>(numericValue);
        if (qFuzzyCompare(numericValue + 1.0, static_cast<double>(integralValue) + 1.0)) {
            return QString::number(integralValue);
        }
        return QString::number(numericValue, 'g', 16);
    }
    if (value.isBool()) {
        return value.toBool() ? "true" : "false";
    }
    if (value.isNull() || value.isUndefined()) {
        return "null";
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }

    return {};
}

QJsonValue workflowTextToJsonValue(const QString& text)
{
    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        return QString();
    }

    QJsonDocument jsonDocument;
    if (utils::extractJsonDocumentFromText(trimmedText, &jsonDocument)) {
        if (jsonDocument.isArray()) {
            return jsonDocument.array();
        }
        if (jsonDocument.isObject()) {
            return jsonDocument.object();
        }
    }

    if (trimmedText.compare("true", Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (trimmedText.compare("false", Qt::CaseInsensitive) == 0) {
        return false;
    }
    if (trimmedText.compare("null", Qt::CaseInsensitive) == 0) {
        return QJsonValue(QJsonValue::Null);
    }

    bool numericOk = false;
    const double numericValue = trimmedText.toDouble(&numericOk);
    if (numericOk) {
        return numericValue;
    }

    return trimmedText;
}

void collectLoopVariableAssignments(
    const QString& variableName,
    const QJsonValue& value,
    QHash<QString, QString>* assignments,
    QStringList* assignedKeys
)
{
    if (assignments == nullptr) {
        return;
    }

    const QString normalizedVariableName = variableName.trimmed();
    if (normalizedVariableName.isEmpty()) {
        return;
    }

    assignments->insert(normalizedVariableName, jsonValueToWorkflowText(value));
    if (assignedKeys != nullptr && !assignedKeys->contains(normalizedVariableName)) {
        assignedKeys->append(normalizedVariableName);
    }

    if (value.isObject()) {
        const QJsonObject objectValue = value.toObject();
        for (auto it = objectValue.constBegin(); it != objectValue.constEnd(); ++it) {
            collectLoopVariableAssignments(
                normalizedVariableName + "." + it.key(),
                it.value(),
                assignments,
                assignedKeys
            );
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray arrayValue = value.toArray();
        for (int index = 0; index < arrayValue.size(); ++index) {
            collectLoopVariableAssignments(
                QString("%1.%2").arg(normalizedVariableName).arg(index),
                arrayValue.at(index),
                assignments,
                assignedKeys
            );
        }
    }
}

bool resolveForeachItems(
    const QJsonValue& value,
    const RunContext& runContext,
    QJsonArray* resolvedItems,
    QString* errorMessage
)
{
    if (resolvedItems == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Kein Ziel fuer foreach-Items vorhanden.";
        }
        return false;
    }

    *resolvedItems = QJsonArray();

    const QJsonValue renderedValue = renderJsonValue(value, runContext);
    if (renderedValue.isArray()) {
        *resolvedItems = renderedValue.toArray();
        return true;
    }

    if (renderedValue.isObject()) {
        resolvedItems->append(renderedValue.toObject());
        return true;
    }

    if (renderedValue.isNull() || renderedValue.isUndefined()) {
        return true;
    }

    if (!renderedValue.isString()) {
        resolvedItems->append(renderedValue);
        return true;
    }

    const QString renderedText = renderedValue.toString().trimmed();
    if (renderedText.isEmpty() || renderedText == "null") {
        return true;
    }

    const QJsonValue structuredValue = workflowTextToJsonValue(renderedText);
    if (structuredValue.isArray()) {
        *resolvedItems = structuredValue.toArray();
        return true;
    }
    if (structuredValue.isObject()) {
        resolvedItems->append(structuredValue.toObject());
        return true;
    }

    const QStringList lineParts = renderedText.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    if (lineParts.size() > 1) {
        for (const QString& part : lineParts) {
            const QString normalized = part.trimmed();
            if (!normalized.isEmpty()) {
                resolvedItems->append(normalized);
            }
        }
        return true;
    }

    const QStringList commaParts = renderedText.split(',', Qt::SkipEmptyParts);
    if (commaParts.size() > 1) {
        for (const QString& part : commaParts) {
            const QString normalized = part.trimmed();
            if (!normalized.isEmpty()) {
                resolvedItems->append(normalized);
            }
        }
        return true;
    }

    resolvedItems->append(renderedText);
    return true;
}

void captureDebugState(WorkflowDebugStep* debugStep, const RunContext& runContext)
{
    if (debugStep == nullptr) {
        return;
    }

    debugStep->memoryEntryCount = runContext.memoryEntryCount;
    debugStep->directMemoryEntryCount = runContext.directMemoryEntryCount;
    debugStep->compressedMemoryEntryCount = runContext.compressedMemoryEntryCount;
    debugStep->totalPinnedMemoryEntryCount = runContext.totalPinnedMemoryEntryCount;
    debugStep->variablesAfterStep = snapshotDebugVariables(runContext.variables);
}

SanitizedPromptResponse sanitizePromptResponse(const QString& rawText)
{
    const utils::SanitizedResponseContent sanitized = utils::sanitizeModelVisibleText(rawText);

    SanitizedPromptResponse result;
    result.visibleText = sanitized.visibleText;
    result.reasoningText = sanitized.reasoningText;
    result.hadReasoningContent = sanitized.hadReasoningContent;
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
    const auto parseNumeric = [&](const QString& text, double* parsedValue) {
        if (parsedValue == nullptr) {
            return false;
        }

        bool ok = false;
        const double numericValue = text.trimmed().toDouble(&ok);
        if (!ok) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Decision erwartet fuer '%1' einen numerischen Wert.").arg(operatorName);
            }
            return false;
        }

        *parsedValue = numericValue;
        return true;
    };

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
    } else if (normalizedOperator == "greater_than"
        || normalizedOperator == "greater_or_equal"
        || normalizedOperator == "less_than"
        || normalizedOperator == "less_or_equal") {
        double leftNumber = 0.0;
        double rightNumber = 0.0;
        if (!parseNumeric(inputValue, &leftNumber) || !parseNumeric(comparisonValue, &rightNumber)) {
            return false;
        }

        if (normalizedOperator == "greater_than") {
            localMatch = leftNumber > rightNumber;
        } else if (normalizedOperator == "greater_or_equal") {
            localMatch = leftNumber >= rightNumber;
        } else if (normalizedOperator == "less_than") {
            localMatch = leftNumber < rightNumber;
        } else {
            localMatch = leftNumber <= rightNumber;
        }
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

    std::function<QString(const QVector<domain::WorkflowStep>&, const QString&)> validateStepList;
    validateStepList = [&](const QVector<domain::WorkflowStep>& steps, const QString& scopeLabel) -> QString {
        if (steps.isEmpty()) {
            return QString("%1 enthaelt noch keine Schritte.").arg(scopeLabel);
        }

        QSet<QString> stepIds;
        for (const domain::WorkflowStep& step : steps) {
            if (step.id.trimmed().isEmpty() || step.type.trimmed().isEmpty()) {
                return QString("%1: Jeder Schritt braucht mindestens 'id' und 'type'.").arg(scopeLabel);
            }

            const QString normalizedStepId = step.id.trimmed();
            if (stepIds.contains(normalizedStepId)) {
                return QString("%1: Schritt-ID '%2' ist mehrfach vorhanden.").arg(scopeLabel, normalizedStepId);
            }
            stepIds.insert(normalizedStepId);
        }

        for (const domain::WorkflowStep& step : steps) {
            const QString stepType = normalizedStepType(step.type);

            if (stepType != "prompt" && stepType != "save_memory" && stepType != "decision" && stepType != "tool") {
                return QString(
                    "%1: Schritt '%2' nutzt Typ '%3'. Aktuell werden nur 'prompt', 'save_memory', 'decision' und 'tool' unterstuetzt."
                ).arg(scopeLabel, step.id, step.type);
            }

            if (stepType == "prompt" && configString(step.config, "prompt").isEmpty()) {
                return QString("%1: Prompt-Schritt '%2' braucht ein config.prompt-Feld.").arg(scopeLabel, step.id);
            }

            if (stepType == "tool" && configString(step.config, "tool").isEmpty()) {
                return QString("%1: Tool-Schritt '%2' braucht ein config.tool-Feld.").arg(scopeLabel, step.id);
            }

            if (stepType == "decision") {
                const QJsonValue rulesValue = step.config.value("rules");
                const bool hasRules = rulesValue.isArray() && !rulesValue.toArray().isEmpty();

                if (!rulesValue.isUndefined() && !rulesValue.isArray()) {
                    return QString("%1: Decision-Schritt '%2' hat ein ungueltiges 'rules'-Feld.").arg(scopeLabel, step.id);
                }

                if (!hasRules && configString(step.config, "operator").isEmpty()) {
                    return QString(
                        "%1: Decision-Schritt '%2' braucht entweder ein 'rules'-Array oder ein 'operator'-Feld."
                    ).arg(scopeLabel, step.id);
                }

                if (!validateTargetStepId(configString(step.config, "if_true"), stepIds, step.id, "if_true", nullptr)) {
                    return QString("%1: Schritt '%2' verweist in 'if_true' auf unbekannten Schritt '%3'.")
                        .arg(scopeLabel, step.id, configString(step.config, "if_true"));
                }
                if (!validateTargetStepId(configString(step.config, "if_false"), stepIds, step.id, "if_false", nullptr)) {
                    return QString("%1: Schritt '%2' verweist in 'if_false' auf unbekannten Schritt '%3'.")
                        .arg(scopeLabel, step.id, configString(step.config, "if_false"));
                }
                if (!validateTargetStepId(configString(step.config, "default_next"), stepIds, step.id, "default_next", nullptr)) {
                    return QString("%1: Schritt '%2' verweist in 'default_next' auf unbekannten Schritt '%3'.")
                        .arg(scopeLabel, step.id, configString(step.config, "default_next"));
                }

                if (hasRules) {
                    const QJsonArray rules = rulesValue.toArray();
                    for (int index = 0; index < rules.size(); ++index) {
                        if (!rules.at(index).isObject()) {
                            return QString("%1: Decision-Schritt '%2' hat in 'rules' einen ungueltigen Eintrag.")
                                .arg(scopeLabel, step.id);
                        }

                        const QJsonObject rule = rules.at(index).toObject();
                        const QString operatorName = configString(rule, "operator");
                        if (operatorName.isEmpty()) {
                            return QString("%1: Decision-Schritt '%2' braucht in Regel %3 ein 'operator'-Feld.")
                                .arg(scopeLabel, step.id, QString::number(index + 1));
                        }

                        QString targetError;
                        if (!validateTargetStepId(configString(rule, "next_step"), stepIds, step.id, "next_step", &targetError)) {
                            return QString("%1: %2").arg(scopeLabel, targetError);
                        }
                    }
                }
            }

            if (stepType == "tool" && !configString(step.config, "if_true").isEmpty()) {
                return QString("%1: Tool-Schritt '%2' unterstuetzt aktuell keine Decision-Sprungfelder.")
                    .arg(scopeLabel, step.id);
            }

            if (stepType == "tool" && configString(step.config, "tool").trimmed().toLower() == "workflow.foreach") {
                const QJsonValue nestedStepsValue = step.config.value("steps");
                if (!nestedStepsValue.isArray() || nestedStepsValue.toArray().isEmpty()) {
                    return QString("%1: Foreach-Tool '%2' braucht ein Array 'steps' mit Unter-Schritten.")
                        .arg(scopeLabel, step.id);
                }

                const QJsonArray nestedStepsArray = nestedStepsValue.toArray();
                for (int index = 0; index < nestedStepsArray.size(); ++index) {
                    if (!nestedStepsArray.at(index).isObject()) {
                        return QString("%1: Foreach-Tool '%2' hat in 'steps' einen ungueltigen Eintrag.")
                            .arg(scopeLabel, step.id);
                    }
                }

                const QString nestedError = validateStepList(
                    workflowStepsFromJsonArray(nestedStepsArray),
                    QString("%1 > Foreach '%2'").arg(scopeLabel, step.id)
                );
                if (!nestedError.isEmpty()) {
                    return nestedError;
                }
            }
        }

        return {};
    };

    return validateStepList(workflow.steps, QString("Workflow '%1'").arg(workflow.name));
}

QString WorkflowEngine::previewExecution(const domain::Workflow& workflow, const RunContext& runContext) const
{
    return QString("Workflow '%1' wurde fuer Projekt '%2' im Vorschaumodus vorbereitet.")
        .arg(workflow.name, runContext.projectName);
}

ExecutionResult WorkflowEngine::executeWorkflow(
    const domain::Workflow& workflow,
    RunContext runContext,
    providers::ILlmProvider& provider,
    ExecutionCallbacks callbacks
) const
{
    ExecutionResult result;
    const auto appendLog = [&](const QString& line) {
        result.logs.append(line);
        if (callbacks.onLogLine) {
            callbacks.onLogLine(line);
        }
    };
    const auto updateStepStatus = [&](const QString& stepId, const QString& statusText) {
        if (callbacks.onStepStatus) {
            callbacks.onStepStatus(stepId, statusText);
        }
    };
    const auto appendPromptChunk = [&](const QString& stepId, const QString& chunk) {
        if (!chunk.isEmpty() && callbacks.onPromptChunk) {
            callbacks.onPromptChunk(stepId, chunk);
        }
    };
    const auto interruptExecution = [&](const QString& message) {
        result.interrupted = true;
        result.errorMessage = message;
        result.memoryEntriesToPersist.clear();
        appendLog(QString("[engine] %1").arg(message));
        result.variables = runContext.variables;
    };

    appendLog(
        QString("[engine] Starte Workflow '%1' fuer Projekt '%2' ueber Provider '%3'.")
            .arg(workflow.name, runContext.projectName, provider.name())
    );

    if (isCancellationRequested(callbacks)) {
        interruptExecution(manualCancellationMessage());
        return result;
    }

    const QString validationError = validateWorkflow(workflow);
    if (!validationError.isEmpty()) {
        result.errorMessage = validationError;
        appendLog(QString("[engine] Abbruch: %1").arg(validationError));
        return result;
    }

    if (runContext.selectedModel.trimmed().isEmpty()) {
        result.errorMessage = "Kein Modell fuer die Workflow-Ausfuehrung konfiguriert.";
        appendLog(QString("[engine] Abbruch: %1").arg(result.errorMessage));
        return result;
    }

    runContext.variables.insert("project_name", runContext.projectName);
    runContext.variables.insert("project_id", QString::number(runContext.projectId));
    runContext.variables.insert("workflow_name", runContext.workflowName);
    runContext.variables.insert("selected_model", runContext.selectedModel);
    const auto syncProjectMemoryVariables = [&runContext]() {
        runContext.variables.insert("project_memory", runContext.memorySnippets.join("\n"));
        runContext.variables.insert("project_memory_count", QString::number(runContext.memoryEntryCount));
        runContext.variables.insert("project_memory_snippet_count", QString::number(runContext.memorySnippets.size()));
        runContext.variables.insert("project_memory_direct_count", QString::number(runContext.directMemoryEntryCount));
        runContext.variables.insert(
            "project_memory_compressed_count",
            QString::number(runContext.compressedMemoryEntryCount)
        );
        runContext.variables.insert("project_memory_pinned_count", QString::number(runContext.pinnedMemoryEntryCount));
        runContext.variables.insert(
            "project_memory_total_pinned_count",
            QString::number(runContext.totalPinnedMemoryEntryCount)
        );
    };
    const auto appendPreparedMemoryEntries = [&](const QList<domain::MemoryEntry>& entries) {
        for (const domain::MemoryEntry& entry : entries) {
            result.memoryEntriesToPersist.append(entry);
            runContext.memorySnippets.append(memorySnippetFromEntry(entry));
            ++runContext.memoryEntryCount;
            ++runContext.directMemoryEntryCount;
            if (entry.pinned) {
                ++runContext.pinnedMemoryEntryCount;
                ++runContext.totalPinnedMemoryEntryCount;
            }
            runContext.variables.insert("last_memory_content", entry.content);
            runContext.variables.insert("last_memory_type", entry.type);
        }
        syncProjectMemoryVariables();
    };

    syncProjectMemoryVariables();

    int executedStepCount = 0;
    const int maxStepExecutions = qMax(200, workflow.steps.size() * 500);
    std::function<bool(const QVector<domain::WorkflowStep>&, const QString&)> executeStepList;
    executeStepList = [&](const QVector<domain::WorkflowStep>& stepList, const QString& scopePrefix) -> bool {
        QHash<QString, int> stepIndexById;
        stepIndexById.reserve(stepList.size());
        for (int index = 0; index < stepList.size(); ++index) {
            stepIndexById.insert(stepList.at(index).id.trimmed(), index);
        }

        int currentStepIndex = 0;
        while (currentStepIndex >= 0 && currentStepIndex < stepList.size()) {
            if (isCancellationRequested(callbacks)) {
                interruptExecution(manualCancellationMessage());
                return false;
            }

            if (++executedStepCount > maxStepExecutions) {
                result.errorMessage =
                    "Workflow wurde wegen zu vieler Schritt-Ausfuehrungen abgebrochen. Pruefe Decision-Spruenge.";
                appendLog(QString("[engine] Abbruch: %1").arg(result.errorMessage));
                result.variables = runContext.variables;
                return false;
            }

            const domain::WorkflowStep& step = stepList.at(currentStepIndex);
            const int sequentialNextIndex = currentStepIndex + 1;
            const QString stepType = normalizedStepType(step.type);
            const QString displayStepId = scopedStepId(scopePrefix, step.id.trimmed());
            const int stepLogStartIndex = result.logs.size();
            WorkflowDebugStep debugStep;
            debugStep.executionIndex = executedStepCount;
            debugStep.stepId = displayStepId;
            debugStep.stepType = stepType;
            debugStep.stepName = step.name;
            updateStepStatus(
                displayStepId,
                QString("Schritt '%1' (%2) wird ausgefuehrt").arg(displayStepId, stepType)
            );
            const auto finalizeDebugStep = [&](const QString& status, const QString& summary) {
                debugStep.status = status;
                debugStep.summary = summary;
                captureDebugState(&debugStep, runContext);
                for (int logIndex = stepLogStartIndex; logIndex < result.logs.size(); ++logIndex) {
                    debugStep.logs.append(result.logs.at(logIndex));
                }
                result.debugSteps.append(debugStep);
                updateStepStatus(displayStepId, QString("Schritt '%1': %2").arg(displayStepId, summary));
            };
            const auto failStep = [&](const QString& errorMessage, const QString& summary) {
                result.errorMessage = errorMessage;
                appendLog(QString("[step:%1] Fehler: %2").arg(displayStepId, errorMessage));
                debugStep.errorMessage = errorMessage;
                debugStep.nextStepId = "[abbruch]";
                finalizeDebugStep("failed", summary);
                result.variables = runContext.variables;
            };
            const auto interruptStep = [&](const QString& message, const QString& summary) {
                appendLog(QString("[step:%1] Abbruch: %2").arg(displayStepId, message));
                debugStep.errorMessage = message;
                debugStep.nextStepId = "[abbruch]";
                finalizeDebugStep("interrupted", summary);
                result.interrupted = true;
                result.errorMessage = message;
                result.memoryEntriesToPersist.clear();
                result.variables = runContext.variables;
            };

        if (stepType == "save_memory") {
            QJsonObject renderedConfig = renderJsonValue(step.config, runContext).toObject();
            if (!renderedConfig.contains("content")) {
                renderedConfig.insert("content", runContext.variables.value("last_response"));
            }

            domain::MemoryEntry entry;
            entry.projectId = runContext.projectId;
            entry.type = configString(renderedConfig, "entry_type");
            if (entry.type.isEmpty()) {
                entry.type = configString(renderedConfig, "memory_type");
            }
            if (entry.type.isEmpty()) {
                entry.type = "note";
            }

            QString content = renderedConfig.value("content").toString().trimmed();
            debugStep.inputPreview = previewText(content);

            const utils::PersistedContentGuardResult guard = utils::guardPersistedContent(
                content,
                entry.type,
                renderedConfig
            );
            for (const QString& warning : guard.warnings) {
                appendLog(QString("[warn][step:%1] %2").arg(displayStepId, warning));
            }

            if (guard.shouldFail) {
                failStep(
                    QString("Schritt '%1' fehlgeschlagen: %2").arg(displayStepId, guard.errorMessage),
                    "Memory-Persistenz von Guardrails blockiert."
                );
                return false;
            }

            if (guard.shouldSkip) {
                appendLog(QString("[step:%1] Memory-Speichern wurde bewusst uebersprungen.").arg(displayStepId));
                runContext.variables.insert("last_memory_content", QString());
                runContext.variables.insert("last_memory_type", entry.type);
                debugStep.outputKey = "last_memory_content";
                debugStep.outputPreview = "-";
                debugStep.outputText.clear();
                debugStep.nextStepId = nextStepLabel(stepList, sequentialNextIndex, scopePrefix);
                finalizeDebugStep(
                    "skipped",
                    QString("Memory-Eintrag vom Typ '%1' wurde uebersprungen.").arg(entry.type)
                );
                currentStepIndex = sequentialNextIndex;
                continue;
            }

            content = guard.finalText.trimmed();
            if (content.isEmpty()) {
                failStep(
                    QString("Schritt '%1' konnte keinen Memory-Inhalt erzeugen.").arg(displayStepId),
                    "Kein Memory-Inhalt erzeugt."
                );
                return false;
            }

            entry.source = configString(renderedConfig, "source");
            if (entry.source.isEmpty()) {
                entry.source = QString("workflow:%1/%2").arg(runContext.workflowName, displayStepId);
            }

            entry.tags = configTags(renderedConfig, "tags", runContext);
            entry.relevance = configRelevance(renderedConfig, "relevance", 50);
            entry.pinned = configBool(renderedConfig, "pinned", false);
            entry.content = content;

            appendPreparedMemoryEntries({ entry });

            appendLog(QString("[step:%1] Typ: save_memory").arg(displayStepId));
            appendLog(QString("[step:%1] Memory-Typ: %2").arg(displayStepId, entry.type));
            appendLog(QString("[step:%1] Memory-Quelle: %2").arg(displayStepId, entry.source));
            if (entry.pinned) {
                appendLog(QString("[step:%1] Memory wird angepinnt gespeichert.").arg(displayStepId));
            }
            appendLog(QString("[step:%1] Memory gespeichert vorgemerkt.").arg(displayStepId));
            appendLog(QString("[step:%1] Inhalt: %2").arg(displayStepId, previewText(entry.content)));
            debugStep.outputKey = "last_memory_content";
            debugStep.outputPreview = previewText(entry.content);
            debugStep.outputText = debugVariableValueForDisplay({}, entry.content);
            debugStep.nextStepId = nextStepLabel(stepList, sequentialNextIndex, scopePrefix);
            finalizeDebugStep("completed", QString("Memory-Eintrag vom Typ '%1' vorgemerkt.").arg(entry.type));
            currentStepIndex = sequentialNextIndex;
            continue;
        }

        if (stepType == "decision") {
            const QString inputTemplate = configString(step.config, "input").isEmpty()
                ? "{{last_response}}"
                : configString(step.config, "input");
            const QString inputValue = renderTemplate(inputTemplate, runContext);
            debugStep.inputPreview = previewText(inputValue);
            QString outputKey = configString(step.config, "output").isEmpty()
                ? step.id.trimmed()
                : renderTemplate(configString(step.config, "output"), runContext).trimmed();
            if (outputKey.isEmpty()) {
                outputKey = step.id.trimmed();
            }
            const bool caseSensitive = configBool(step.config, "case_sensitive", false);

            appendLog(QString("[step:%1] Typ: decision").arg(displayStepId));
            appendLog(QString("[step:%1] Input: %2").arg(displayStepId, previewText(inputValue)));

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
                        debugStep.outputKey = outputKey;
                        failStep(
                            QString("Schritt '%1' fehlgeschlagen: %2").arg(displayStepId, evaluationError),
                            "Decision-Auswertung fehlgeschlagen."
                        );
                        return false;
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
                    appendLog(
                        QString("[step:%1] Regel %2 getroffen (%3).")
                            .arg(displayStepId)
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
                    appendLog(QString("[step:%1] Keine Regel getroffen.").arg(displayStepId));
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
                    debugStep.outputKey = outputKey;
                    failStep(
                        QString("Schritt '%1' fehlgeschlagen: %2").arg(displayStepId, evaluationError),
                        "Decision-Auswertung fehlgeschlagen."
                    );
                    return false;
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

                appendLog(
                    QString("[step:%1] Entscheidung: %2")
                        .arg(displayStepId, matched ? "true" : "false")
                );
            }

            runContext.variables.insert(outputKey, decisionValue);
            runContext.variables.insert("last_decision", decisionValue);
            appendLog(QString("[step:%1] Ergebnisvariable '%2' = %3").arg(displayStepId, outputKey, decisionValue));

            QString targetError;
            const int resolvedIndex = resolveNextStepIndex(
                targetStepId,
                stepIndexById,
                sequentialNextIndex,
                &targetError
            );
            if (resolvedIndex < 0) {
                debugStep.outputKey = outputKey;
                debugStep.outputPreview = previewText(decisionValue);
                debugStep.outputText = debugVariableValueForDisplay({}, decisionValue);
                debugStep.nextStepId = targetStepId.trimmed().isEmpty()
                    ? "[abbruch]"
                    : scopedStepId(scopePrefix, targetStepId.trimmed());
                failStep(
                    QString("Schritt '%1' fehlgeschlagen: %2").arg(displayStepId, targetError),
                    "Decision-Zielsprung ungueltig."
                );
                return false;
            }

            if (targetStepId.trimmed().isEmpty()) {
                appendLog(QString("[step:%1] Weiter mit naechstem Schritt.").arg(displayStepId));
            } else {
                appendLog(QString("[step:%1] Springe zu '%2'.").arg(displayStepId, targetStepId));
            }

            debugStep.outputKey = outputKey;
            debugStep.outputPreview = previewText(decisionValue);
            debugStep.outputText = debugVariableValueForDisplay({}, decisionValue);
            debugStep.nextStepId = targetStepId.trimmed().isEmpty()
                ? nextStepLabel(stepList, resolvedIndex, scopePrefix)
                : scopedStepId(scopePrefix, targetStepId.trimmed());
            finalizeDebugStep(
                "completed",
                QString("Decision-Ergebnis '%1' mit Folge '%2'.")
                    .arg(decisionValue, debugStep.nextStepId)
            );
            currentStepIndex = resolvedIndex;
            continue;
        }

        if (stepType == "tool") {
            const QString rawToolName = configString(step.config, "tool").trimmed().toLower();
            QString outputKey = configString(step.config, "output").isEmpty()
                ? step.id.trimmed()
                : renderTemplate(configString(step.config, "output"), runContext).trimmed();
            if (outputKey.isEmpty()) {
                outputKey = step.id.trimmed();
            }

            if (rawToolName == "workflow.foreach") {
                appendLog(QString("[step:%1] Typ: tool").arg(displayStepId));
                appendLog(QString("[step:%1] Tool: workflow.foreach").arg(displayStepId));
                debugStep.inputPreview = "workflow.foreach";

                const QJsonValue rawItemsValue = step.config.contains("items")
                    ? step.config.value("items")
                    : QJsonValue(QString("{{last_response}}"));
                QJsonArray items;
                QString itemResolutionError;
                if (!resolveForeachItems(rawItemsValue, runContext, &items, &itemResolutionError)) {
                    debugStep.outputKey = outputKey;
                    failStep(
                        QString("Schritt '%1' fehlgeschlagen: %2").arg(displayStepId, itemResolutionError),
                        "Foreach-Items konnten nicht aufgeloest werden."
                    );
                    return false;
                }

                const int maxIterations = qMax(1, renderedConfigInt(step.config, "max_iterations", runContext, 100));
                const int iterationCount = qMin(items.size(), maxIterations);
                const QString onErrorMode = renderTemplate(configString(step.config, "on_error"), runContext)
                    .trimmed()
                    .toLower();
                const QString itemVar = renderTemplate(
                    configString(step.config, "item_var").isEmpty()
                        ? "loop_item"
                        : configString(step.config, "item_var"),
                    runContext
                ).trimmed();
                const QString resolvedItemVar = itemVar.isEmpty() ? "loop_item" : itemVar;
                const QString indexVar = renderTemplate(
                    configString(step.config, "index_var").isEmpty()
                        ? "loop_index"
                        : configString(step.config, "index_var"),
                    runContext
                ).trimmed();
                const QString resolvedIndexVar = indexVar.isEmpty() ? "loop_index" : indexVar;
                const QString resultMode = renderTemplate(
                    configString(step.config, "result_mode").isEmpty()
                        ? "text_joined"
                        : configString(step.config, "result_mode"),
                    runContext
                ).trimmed().toLower();
                const QString resultSourceTemplate = step.config.value("result_source").toString().trimmed().isEmpty()
                    ? QString("{{last_response}}")
                    : step.config.value("result_source").toString();
                QString joinWith = renderTemplate(step.config.value("join_with").toString(), runContext);
                if (!step.config.contains("join_with")) {
                    joinWith = "\n\n";
                }
                const QString resultVar = renderTemplate(configString(step.config, "result_var"), runContext)
                    .trimmed();
                const QVector<domain::WorkflowStep> nestedSteps =
                    workflowStepsFromJsonArray(step.config.value("steps").toArray());

                QHash<QString, QString> originalLoopValues;
                QSet<QString> originalLoopExistingKeys;
                QSet<QString> trackedLoopKeys;
                QStringList previousIterationKeys;
                QString previousIterationOutput;
                QStringList textResults;
                QJsonArray jsonResults;
                const auto rememberOriginalLoopVariable = [&](const QString& key) {
                    if (key.trimmed().isEmpty() || trackedLoopKeys.contains(key)) {
                        return;
                    }
                    trackedLoopKeys.insert(key);
                    if (runContext.variables.contains(key)) {
                        originalLoopExistingKeys.insert(key);
                        originalLoopValues.insert(key, runContext.variables.value(key));
                    }
                };

                appendLog(
                    QString("[step:%1] Foreach verarbeitet %2 Item(s).").arg(displayStepId).arg(iterationCount)
                );
                if (items.size() > maxIterations) {
                    appendLog(
                        QString("[step:%1] Foreach kuerzt %2 Items auf max_iterations=%3.")
                            .arg(displayStepId)
                            .arg(items.size())
                            .arg(maxIterations)
                    );
                }

                for (int iterationIndex = 0; iterationIndex < iterationCount; ++iterationIndex) {
                    if (isCancellationRequested(callbacks)) {
                        debugStep.outputKey = outputKey;
                        interruptStep(manualCancellationMessage(), "Foreach wurde manuell abgebrochen.");
                        return false;
                    }

                    for (const QString& key : previousIterationKeys) {
                        runContext.variables.remove(key);
                    }
                    previousIterationKeys.clear();

                    const QJsonValue currentItem = items.at(iterationIndex);
                    QHash<QString, QString> loopAssignments;
                    collectLoopVariableAssignments(
                        resolvedItemVar,
                        currentItem,
                        &loopAssignments,
                        &previousIterationKeys
                    );
                    collectLoopVariableAssignments("loop_item", currentItem, &loopAssignments, &previousIterationKeys);
                    loopAssignments.insert(resolvedIndexVar, QString::number(iterationIndex));
                    loopAssignments.insert("loop_index", QString::number(iterationIndex));
                    loopAssignments.insert("loop_first", iterationIndex == 0 ? "true" : "false");
                    loopAssignments.insert("loop_last", iterationIndex == iterationCount - 1 ? "true" : "false");
                    loopAssignments.insert("loop_count", QString::number(iterationCount));
                    loopAssignments.insert("loop_prev_output", previousIterationOutput);
                    for (auto it = loopAssignments.constBegin(); it != loopAssignments.constEnd(); ++it) {
                        rememberOriginalLoopVariable(it.key());
                        if (!previousIterationKeys.contains(it.key())) {
                            previousIterationKeys.append(it.key());
                        }
                        runContext.variables.insert(it.key(), it.value());
                    }

                    const QHash<QString, QString> iterationVariableSnapshot = runContext.variables;
                    const QStringList iterationMemorySnippetSnapshot = runContext.memorySnippets;
                    const int iterationMemoryEntryCount = runContext.memoryEntryCount;
                    const int iterationDirectMemoryEntryCount = runContext.directMemoryEntryCount;
                    const int iterationPinnedMemoryEntryCount = runContext.pinnedMemoryEntryCount;
                    const int iterationTotalPinnedMemoryEntryCount = runContext.totalPinnedMemoryEntryCount;
                    const int persistedMemoryCount = result.memoryEntriesToPersist.size();
                    const QString finalOutputBeforeIteration = result.finalOutput;

                    const QString iterationScope = scopedStepId(
                        scopePrefix,
                        QString("%1/iter_%2").arg(step.id.trimmed()).arg(iterationIndex + 1)
                    );
                    if (!executeStepList(nestedSteps, iterationScope)) {
                        const QString iterationError = result.errorMessage;
                        if (result.interrupted) {
                            debugStep.outputKey = outputKey;
                            debugStep.nextStepId = "[abbruch]";
                            finalizeDebugStep(
                                "interrupted",
                                QString("Foreach wurde in Iteration %1 manuell abgebrochen.").arg(iterationIndex + 1)
                            );
                            result.variables = runContext.variables;
                            return false;
                        }

                        if (onErrorMode == "continue") {
                            appendLog(
                                QString("[step:%1] Iteration %2 uebersprungen: %3")
                                    .arg(displayStepId)
                                    .arg(iterationIndex + 1)
                                    .arg(iterationError)
                            );
                            runContext.variables = iterationVariableSnapshot;
                            runContext.memorySnippets = iterationMemorySnippetSnapshot;
                            runContext.memoryEntryCount = iterationMemoryEntryCount;
                            runContext.directMemoryEntryCount = iterationDirectMemoryEntryCount;
                            runContext.pinnedMemoryEntryCount = iterationPinnedMemoryEntryCount;
                            runContext.totalPinnedMemoryEntryCount = iterationTotalPinnedMemoryEntryCount;
                            while (result.memoryEntriesToPersist.size() > persistedMemoryCount) {
                                result.memoryEntriesToPersist.removeLast();
                            }
                            result.finalOutput = finalOutputBeforeIteration;
                            result.errorMessage.clear();
                            previousIterationOutput.clear();
                            continue;
                        }

                        debugStep.outputKey = outputKey;
                        debugStep.nextStepId = "[abbruch]";
                        finalizeDebugStep(
                            "failed",
                            QString("Foreach brach in Iteration %1 ab.").arg(iterationIndex + 1)
                        );
                        result.variables = runContext.variables;
                        return false;
                    }

                    const QString iterationResultText = renderTemplate(resultSourceTemplate, runContext).trimmed();
                    previousIterationOutput = iterationResultText;
                    if (resultMode == "json_array") {
                        jsonResults.append(workflowTextToJsonValue(iterationResultText));
                    } else {
                        textResults.append(iterationResultText);
                    }
                }

                for (const QString& key : previousIterationKeys) {
                    runContext.variables.remove(key);
                }
                for (const QString& key : trackedLoopKeys) {
                    if (originalLoopExistingKeys.contains(key)) {
                        runContext.variables.insert(key, originalLoopValues.value(key));
                    } else {
                        runContext.variables.remove(key);
                    }
                }

                const QString aggregatedOutput = resultMode == "json_array"
                    ? QString::fromUtf8(QJsonDocument(jsonResults).toJson(QJsonDocument::Indented)).trimmed()
                    : textResults.join(joinWith);
                runContext.variables.insert(outputKey, aggregatedOutput);
                runContext.variables.insert("last_tool_output", aggregatedOutput);
                runContext.variables.insert("last_tool_name", "workflow.foreach");
                if (!resultVar.isEmpty()) {
                    runContext.variables.insert(resultVar, aggregatedOutput);
                }
                result.finalOutput = aggregatedOutput;

                appendLog(
                    QString("[step:%1] Foreach-Ausgabe gespeichert in Variable '%2'.")
                        .arg(displayStepId, outputKey)
                );
                appendLog(QString("[step:%1] Ausgabe: %2").arg(displayStepId, previewText(aggregatedOutput)));
                debugStep.outputKey = outputKey;
                debugStep.outputPreview = previewText(aggregatedOutput);
                debugStep.outputText = debugVariableValueForDisplay({}, aggregatedOutput);
                debugStep.nextStepId = nextStepLabel(stepList, sequentialNextIndex, scopePrefix);
                finalizeDebugStep(
                    "completed",
                    QString("Foreach mit %1 Iteration(en) abgeschlossen.").arg(iterationCount)
                );
                currentStepIndex = sequentialNextIndex;
                continue;
            }

            if (m_toolExecutor == nullptr) {
                failStep(
                    QString("Schritt '%1' fehlgeschlagen: Kein ToolExecutor konfiguriert.").arg(displayStepId),
                    "Kein ToolExecutor konfiguriert."
                );
                return false;
            }

            const QJsonObject renderedConfig = renderJsonValue(step.config, runContext).toObject();
            const QString toolName = configString(renderedConfig, "tool");
            debugStep.inputPreview = toolName;
            outputKey = configString(renderedConfig, "output").isEmpty()
                ? step.id.trimmed()
                : configString(renderedConfig, "output");

            appendLog(QString("[step:%1] Typ: tool").arg(displayStepId));
            appendLog(QString("[step:%1] Tool: %2").arg(displayStepId, toolName));

            tools::ToolExecutionRequest request;
            request.toolName = toolName;
            request.config = renderedConfig;
            request.projectId = runContext.projectId;
            request.projectName = runContext.projectName;
            request.workflowName = runContext.workflowName;
            request.stepId = displayStepId;
            request.selectedModel = runContext.selectedModel;
            request.systemPrompt = runContext.systemPrompt;
            request.llmProvider = &provider;
            request.allowShellRun = runContext.allowShellRun;
            request.allowFileEditDiff = runContext.allowFileEditDiff;
            request.allowHttpRequest = runContext.allowHttpRequest;

            const tools::ToolExecutionResult toolResult = m_toolExecutor->execute(request);
            for (const QString& logLine : toolResult.logs) {
                appendLog(QString("[step:%1] %2").arg(displayStepId, logLine));
            }

            if (!toolResult.success) {
                debugStep.outputKey = outputKey;
                if (isCancellationRequested(callbacks)) {
                    interruptStep(manualCancellationMessage(), QString("Tool '%1' wurde manuell abgebrochen.").arg(toolName));
                    return false;
                }
                failStep(
                    QString("Schritt '%1' fehlgeschlagen: %2").arg(displayStepId, toolResult.errorMessage),
                    QString("Tool '%1' fehlgeschlagen.").arg(toolName)
                );
                return false;
            }

            runContext.variables.insert(outputKey, toolResult.outputText);
            runContext.variables.insert("last_tool_output", toolResult.outputText);
            runContext.variables.insert("last_tool_name", toolName);
            QStringList updatedVariableNames = toolResult.outputVariables.keys();
            std::sort(updatedVariableNames.begin(), updatedVariableNames.end(), [](const QString& left, const QString& right) {
                return left.toCaseFolded() < right.toCaseFolded();
            });
            for (const QString& variableName : updatedVariableNames) {
                runContext.variables.insert(variableName, toolResult.outputVariables.value(variableName));
            }
            result.finalOutput = toolResult.outputText;

            if (!toolResult.memoryEntriesToPersist.isEmpty()) {
                appendPreparedMemoryEntries(toolResult.memoryEntriesToPersist);
                appendLog(
                    QString("[step:%1] Tool hat %2 Memory-Eintrag(e) vorgemerkt.")
                        .arg(displayStepId)
                        .arg(toolResult.memoryEntriesToPersist.size())
                );
            }

            if (!updatedVariableNames.isEmpty()) {
                appendLog(
                    QString("[step:%1] Tool hat %2 Variable(n) aktualisiert: %3")
                        .arg(displayStepId)
                        .arg(updatedVariableNames.size())
                        .arg(updatedVariableNames.join(", "))
                );
            }

            appendLog(
                QString("[step:%1] Tool-Ausgabe gespeichert in Variable '%2'.").arg(displayStepId, outputKey)
            );
            appendLog(QString("[step:%1] Ausgabe: %2").arg(displayStepId, previewText(toolResult.outputText)));
            debugStep.outputKey = outputKey;
            debugStep.outputPreview = previewText(toolResult.outputText);
            debugStep.outputText = debugVariableValueForDisplay({}, toolResult.outputText);
            debugStep.nextStepId = nextStepLabel(stepList, sequentialNextIndex, scopePrefix);
            finalizeDebugStep(
                "completed",
                toolResult.memoryEntriesToPersist.isEmpty()
                    ? QString("Tool '%1' erfolgreich ausgefuehrt.").arg(toolName)
                    : QString("Tool '%1' erfolgreich, %2 Memory-Eintraege vorgemerkt.")
                          .arg(toolName)
                          .arg(toolResult.memoryEntriesToPersist.size())
            );
            currentStepIndex = sequentialNextIndex;
            continue;
        }

        const QString promptTemplate = configString(step.config, "prompt");
        QString outputKey = configString(step.config, "output").isEmpty()
            ? step.id.trimmed()
            : renderTemplate(configString(step.config, "output"), runContext).trimmed();
        if (outputKey.isEmpty()) {
            outputKey = step.id.trimmed();
        }
        const QString model = configString(step.config, "model").isEmpty()
            ? runContext.selectedModel
            : renderTemplate(configString(step.config, "model"), runContext).trimmed();

        QString systemPrompt = step.config.value("system_prompt").toString();
        if (systemPrompt.trimmed().isEmpty()) {
            systemPrompt = runContext.systemPrompt;
        }
        if (systemPrompt.trimmed().isEmpty()) {
            systemPrompt = defaultSystemPrompt();
        }

        const bool promptUsesExplicitMemory = promptTemplate.contains("{{project_memory}}")
            || systemPrompt.contains("{{project_memory}}");
        const QString renderedPrompt = renderTemplate(promptTemplate, runContext);
        debugStep.inputPreview = previewText(renderedPrompt);
        systemPrompt = renderTemplate(systemPrompt, runContext);
        if (!promptUsesExplicitMemory) {
            const QString memoryBlock = memoryContextBlock(runContext.memorySnippets);
            if (!memoryBlock.isEmpty()) {
                systemPrompt += "\n\n" + memoryBlock;
            }
        }

        appendLog(QString("[step:%1] Typ: prompt").arg(displayStepId));
        appendLog(QString("[step:%1] Modell: %2").arg(displayStepId, model));
        if (!runContext.memorySnippets.isEmpty()) {
            appendLog(
                QString("[step:%1] Memory-Kontext: %2 Eintraege verfuegbar (%3 direkte Snippets, %4 komprimiert).")
                    .arg(displayStepId)
                    .arg(runContext.memoryEntryCount)
                    .arg(runContext.directMemoryEntryCount)
                    .arg(runContext.compressedMemoryEntryCount)
            );
        }
        appendLog(QString("[step:%1] Prompt: %2").arg(displayStepId, previewText(renderedPrompt)));

        providers::ChatRequest request;
        request.model = model;
        request.systemPrompt = systemPrompt;
        request.userPrompt = renderedPrompt;

        providers::ChatResponse response;
        const bool shouldStreamPrompt = provider.supportsStreaming() && static_cast<bool>(callbacks.onPromptChunk);
        if (shouldStreamPrompt) {
            appendLog(QString("[step:%1] Live-Streaming aktiviert.").arg(displayStepId));
            updateStepStatus(displayStepId, QString("Schritt '%1' streamt Modellausgabe").arg(displayStepId));
            response = provider.chatStream(request, [&](const QString& chunk) {
                appendPromptChunk(displayStepId, chunk);
            });
        } else {
            response = provider.chat(request);
        }
        for (const QString& providerLogLine : response.logs) {
            appendLog(QString("[step:%1] %2").arg(displayStepId, providerLogLine));
        }
        if (!response.success) {
            debugStep.outputKey = outputKey;
            if (isCancellationRequested(callbacks)) {
                interruptStep(
                    manualCancellationMessage(),
                    QString("Prompt-Schritt mit Modell '%1' wurde manuell abgebrochen.").arg(model)
                );
                return false;
            }
            failStep(
                QString("Schritt '%1' fehlgeschlagen: %2").arg(displayStepId, response.errorMessage),
                QString("Prompt-Schritt mit Modell '%1' fehlgeschlagen.").arg(model)
            );
            return false;
        }

        const SanitizedPromptResponse sanitizedResponse = sanitizePromptResponse(response.text);

        runContext.variables.insert(outputKey, sanitizedResponse.visibleText);
        runContext.variables.insert("last_response", sanitizedResponse.visibleText);
        runContext.variables.insert(outputKey + "_raw", response.text);
        runContext.variables.insert("last_response_raw", response.text);
        result.finalOutput = sanitizedResponse.visibleText;

        if (sanitizedResponse.hadReasoningContent) {
            runContext.variables.insert(outputKey + "_reasoning", sanitizedResponse.reasoningText);
            runContext.variables.insert("last_reasoning", sanitizedResponse.reasoningText);
            runContext.variables.insert("last_response_reasoning", sanitizedResponse.reasoningText);
            appendLog(
                QString("[warn][step:%1] Reasoning- oder Meta-Inhalt erkannt und aus der sichtbaren Ausgabe entfernt.")
                    .arg(displayStepId)
            );
            if (!sanitizedResponse.reasoningText.isEmpty()) {
                appendLog(
                    QString("[warn][step:%1] Reasoning gespeichert in '%2_reasoning' und 'last_reasoning'.")
                        .arg(displayStepId, outputKey)
                );
            }
        } else {
            runContext.variables.remove(outputKey + "_reasoning");
            runContext.variables.remove("last_reasoning");
            runContext.variables.remove("last_response_reasoning");
        }

        appendLog(
            QString("[step:%1] Antwort gespeichert in Variable '%2'.").arg(displayStepId, outputKey)
        );
        appendLog(QString("[step:%1] Ausgabe: %2").arg(displayStepId, previewText(sanitizedResponse.visibleText)));
        debugStep.outputKey = outputKey;
        debugStep.outputPreview = previewText(sanitizedResponse.visibleText);
        debugStep.outputText = debugVariableValueForDisplay({}, sanitizedResponse.visibleText);
        debugStep.reasoningText = debugVariableValueForDisplay({}, sanitizedResponse.reasoningText);
        debugStep.nextStepId = nextStepLabel(stepList, sequentialNextIndex, scopePrefix);
        finalizeDebugStep(
            "completed",
            sanitizedResponse.hadReasoningContent
                ? QString("Prompt-Schritt mit Modell '%1' erfolgreich. Reasoning bzw. Meta-Inhalt wurde getrennt gespeichert.").arg(model)
                : QString("Prompt-Schritt mit Modell '%1' erfolgreich.").arg(model)
        );
        currentStepIndex = sequentialNextIndex;
        }

        return true;
    };

    if (!executeStepList(workflow.steps, {})) {
        return result;
    }

    result.success = true;
    result.variables = runContext.variables;
    appendLog(
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
