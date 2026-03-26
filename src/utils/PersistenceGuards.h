#pragma once

#include "utils/ModelResponseSanitizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace privateclaw::utils {

struct StructuredFieldSummary
{
    int totalFieldCount = 0;
    int nonEmptyFieldCount = 0;
    QStringList emptyFieldLabels;
    QStringList nonEmptyFieldLabels;
};

struct PersistedContentGuardResult
{
    QString finalText;
    QString errorMessage;
    QStringList warnings;
    bool shouldSkip = false;
    bool shouldFail = false;
    bool detectedMetaContent = false;
    bool sanitizedMetaContent = false;
    bool skippedBecauseInputsWereEmpty = false;
    bool skippedBecauseStructuredFieldsWereEmpty = false;
};

inline QStringList jsonStringList(const QJsonObject& object, const QString& key)
{
    QStringList values;
    const QJsonValue value = object.value(key);
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        values.reserve(array.size());
        for (const QJsonValue& item : array) {
            const QString text = cleanupSanitizedText(item.toString());
            if (!text.isEmpty()) {
                values.append(text);
            }
        }
        return values;
    }

    const QString text = cleanupSanitizedText(value.toString());
    if (!text.isEmpty()) {
        values.append(text);
    }
    return values;
}

inline bool areAllTextsEmpty(const QStringList& values)
{
    if (values.isEmpty()) {
        return false;
    }

    for (const QString& value : values) {
        if (!cleanupSanitizedText(value).isEmpty()) {
            return false;
        }
    }

    return true;
}

inline bool isEntityLikeMemoryType(const QString& entryType)
{
    const QString normalized = entryType.trimmed().toCaseFolded();
    return normalized == "character"
        || normalized == "figure"
        || normalized == "person"
        || normalized == "location"
        || normalized == "place"
        || normalized == "event";
}

inline StructuredFieldSummary analyzeStructuredFields(const QString& text)
{
    StructuredFieldSummary summary;
    static const QRegularExpression fieldPattern(
        R"(^\s*(?:[-*+]\s*)?([^:\n]{1,120})\s*:\s*(.*)$)"
    );

    const QStringList lines = cleanupSanitizedText(text).split('\n');
    for (const QString& rawLine : lines) {
        const QRegularExpressionMatch match = fieldPattern.match(rawLine);
        if (!match.hasMatch()) {
            continue;
        }

        const QString label = cleanupSanitizedText(match.captured(1));
        const QString value = cleanupSanitizedText(match.captured(2));
        if (label.isEmpty()) {
            continue;
        }

        ++summary.totalFieldCount;
        if (value.isEmpty()) {
            summary.emptyFieldLabels.append(label);
        } else {
            ++summary.nonEmptyFieldCount;
            summary.nonEmptyFieldLabels.append(label);
        }
    }

    return summary;
}

inline PersistedContentGuardResult guardPersistedContent(
    const QString& content,
    const QString& semanticType,
    const QJsonObject& config
)
{
    PersistedContentGuardResult result;
    result.finalText = content;

    const QStringList emptySkipInputs = jsonStringList(config, "skip_if_all_inputs_empty");
    if (areAllTextsEmpty(emptySkipInputs)) {
        result.shouldSkip = true;
        result.skippedBecauseInputsWereEmpty = true;
        result.warnings.append("Persistenz uebersprungen, weil alle relevanten Eingaben leer sind.");
        return result;
    }

    const QStringList emptyFailInputs = jsonStringList(config, "fail_if_all_inputs_empty");
    if (areAllTextsEmpty(emptyFailInputs)) {
        result.shouldFail = true;
        result.errorMessage = "Persistenz abgebrochen, weil alle erforderlichen Eingaben leer sind.";
        return result;
    }

    const bool skipIfStructuredFieldsEmpty = config.contains("skip_if_structured_fields_empty")
        ? config.value("skip_if_structured_fields_empty").toBool()
        : isEntityLikeMemoryType(semanticType);
    if (skipIfStructuredFieldsEmpty) {
        const StructuredFieldSummary structuredFields = analyzeStructuredFields(content);
        if (structuredFields.totalFieldCount >= 2 && structuredFields.nonEmptyFieldCount == 0) {
            result.shouldSkip = true;
            result.skippedBecauseStructuredFieldsWereEmpty = true;
            result.warnings.append(
                QString(
                    "Persistenz uebersprungen, weil strukturierte Felder fuer Typ '%1' keinen verwertbaren Inhalt enthalten."
                ).arg(semanticType.trimmed().isEmpty() ? "unbekannt" : semanticType.trimmed())
            );
            return result;
        }
    }

    const SanitizedResponseContent sanitized = sanitizeModelVisibleText(content);
    const QString normalizedOriginal = cleanupSanitizedText(content);
    const bool metaDetected = sanitized.hadReasoningContent
        || (!normalizedOriginal.isEmpty() && sanitized.visibleText != normalizedOriginal);
    result.detectedMetaContent = metaDetected;
    if (!metaDetected) {
        return result;
    }

    if (config.value("skip_if_meta_content").toBool(false)) {
        result.shouldSkip = true;
        result.warnings.append("Persistenz uebersprungen, weil offensichtlicher Meta- oder Reasoning-Inhalt erkannt wurde.");
        return result;
    }

    if (config.value("reject_meta_content").toBool(false)) {
        result.shouldFail = true;
        result.errorMessage = "Persistenz abgebrochen, weil offensichtlicher Meta- oder Reasoning-Inhalt erkannt wurde.";
        return result;
    }

    const bool sanitizeBeforePersist = config.value("sanitize_before_persist").toBool(false)
        || config.value("sanitize_before_write").toBool(false);
    if (sanitizeBeforePersist) {
        result.finalText = sanitized.visibleText;
        result.sanitizedMetaContent = result.finalText != normalizedOriginal;
        result.warnings.append("Meta- oder Reasoning-Inhalt wurde vor der Persistenz automatisch entfernt.");
        if (cleanupSanitizedText(result.finalText).isEmpty()) {
            result.shouldFail = true;
            result.errorMessage =
                "Persistenz abgebrochen, weil der Inhalt nach dem Entfernen von Meta- oder Reasoning-Anteilen leer waere.";
        }
        return result;
    }

    result.warnings.append(
        "Es wurde offensichtlicher Meta- oder Reasoning-Inhalt erkannt. Die Persistenz laeuft weiter, weil keine Blockierung konfiguriert ist."
    );
    return result;
}

} // namespace privateclaw::utils
