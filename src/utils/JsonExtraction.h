#pragma once

#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QString>
#include <QVector>

namespace privateclaw::utils {

inline QString normalizedJsonText(QString text)
{
    if (!text.isEmpty() && text.front() == QChar(0xFEFF)) {
        text.remove(0, 1);
    }
    return text.trimmed();
}

inline bool parseStrictJsonDocument(
    const QString& rawText,
    QJsonDocument* document,
    QString* errorMessage = nullptr,
    QString* normalizedText = nullptr
)
{
    if (document == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: JSON-Zieldokument fehlt.";
        }
        return false;
    }

    const QString cleanedText = normalizedJsonText(rawText);
    if (normalizedText != nullptr) {
        *normalizedText = cleanedText;
    }

    if (cleanedText.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Leerer JSON-Text.";
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument parsedDocument = QJsonDocument::fromJson(cleanedText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || parsedDocument.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = parseError.errorString();
        }
        return false;
    }

    if (!parsedDocument.isObject() && !parsedDocument.isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein JSON-Objekt oder JSON-Array gefunden.";
        }
        return false;
    }

    *document = parsedDocument;
    return true;
}

inline bool extractJsonDocumentFromText(
    const QString& rawText,
    QJsonDocument* document,
    QString* extractedJsonText = nullptr,
    QString* errorMessage = nullptr
)
{
    QString strictError;
    QString normalizedWholeText;
    if (parseStrictJsonDocument(rawText, document, &strictError, &normalizedWholeText)) {
        if (extractedJsonText != nullptr) {
            *extractedJsonText = normalizedWholeText;
        }
        return true;
    }

    const QString normalizedInput = normalizedJsonText(rawText);
    if (normalizedInput.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = strictError.isEmpty() ? QString("Leerer JSON-Text.") : strictError;
        }
        return false;
    }

    static const QRegularExpression fencedBlockPattern(
        R"(```(?:[A-Za-z0-9_+-]+)?[ \t]*\r?\n(.*?)```)",
        QRegularExpression::DotMatchesEverythingOption
    );

    const QRegularExpressionMatchIterator fencedMatches = fencedBlockPattern.globalMatch(normalizedInput);
    for (QRegularExpressionMatchIterator it = fencedMatches; it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString fencedContent = match.captured(1);
        QString normalizedFencedContent;
        if (parseStrictJsonDocument(fencedContent, document, nullptr, &normalizedFencedContent)) {
            if (extractedJsonText != nullptr) {
                *extractedJsonText = normalizedFencedContent;
            }
            return true;
        }
    }

    for (int startIndex = 0; startIndex < normalizedInput.size(); ++startIndex) {
        const QChar startCharacter = normalizedInput.at(startIndex);
        if (startCharacter != '{' && startCharacter != '[') {
            continue;
        }

        QVector<QChar> expectedClosers;
        bool insideString = false;
        bool escaped = false;

        for (int endIndex = startIndex; endIndex < normalizedInput.size(); ++endIndex) {
            const QChar character = normalizedInput.at(endIndex);

            if (insideString) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    insideString = false;
                }
                continue;
            }

            if (character == '"') {
                insideString = true;
                continue;
            }

            if (character == '{') {
                expectedClosers.append('}');
                continue;
            }

            if (character == '[') {
                expectedClosers.append(']');
                continue;
            }

            if (character == '}' || character == ']') {
                if (expectedClosers.isEmpty() || expectedClosers.back() != character) {
                    break;
                }

                expectedClosers.removeLast();
                if (!expectedClosers.isEmpty()) {
                    continue;
                }

                const QString candidate = normalizedInput.mid(startIndex, endIndex - startIndex + 1);
                QString normalizedCandidate;
                if (parseStrictJsonDocument(candidate, document, nullptr, &normalizedCandidate)) {
                    if (extractedJsonText != nullptr) {
                        *extractedJsonText = normalizedCandidate;
                    }
                    return true;
                }
                break;
            }
        }
    }

    if (errorMessage != nullptr) {
        *errorMessage = strictError.isEmpty()
            ? QString("Kein JSON-Objekt oder JSON-Array im Text gefunden.")
            : strictError;
    }
    return false;
}

} // namespace privateclaw::utils
