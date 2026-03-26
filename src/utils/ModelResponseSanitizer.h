#pragma once

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace privateclaw::utils {

struct SanitizedResponseContent
{
    QString visibleText;
    QString reasoningText;
    bool hadReasoningContent = false;
};

inline QString cleanupSanitizedText(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace(QRegularExpression("\n{3,}"), "\n\n");
    return text.trimmed();
}

inline QString canonicalMetaLine(QString line)
{
    static const QRegularExpression markdownListPrefix(
        R"(^\s*(?:(?:[-*+]|(?:\d+\.))\s*)+)"
    );
    static const QRegularExpression emphasisPattern(R"((\*\*|\*|`|_))");

    line.remove(markdownListPrefix);
    line.remove(emphasisPattern);
    return line.simplified().toCaseFolded();
}

inline bool isHardMetaLine(const QString& line)
{
    const QString normalized = canonicalMetaLine(line);
    if (normalized.isEmpty()) {
        return false;
    }

    static const QStringList phrases = {
        "thinking process",
        "the user wants me",
        "analyze the request",
        "context analysis",
        "drafting - step-by-step",
        "text construction",
        "final polish",
        "revised draft",
        "better version",
        "immediate continuation",
        "scene shift",
        "writing the text",
        "let's make it",
        "let's focus",
        "let's create",
        "let's refine",
        "let's write",
        "okay, final version",
        "wait, i need to",
        "wait, need to",
        "okay, i will",
        "denkprozess",
        "der nutzer will",
        "analysiere die anfrage",
        "kontextanalyse",
        "textkonstruktion",
        "finaler schliff",
        "ueberarbeiteter entwurf"
    };

    for (const QString& phrase : phrases) {
        if (normalized.contains(phrase)) {
            return true;
        }
    }

    return false;
}

inline bool isAnswerMarkerLine(const QString& line)
{
    static const QRegularExpression pattern(
        R"(^\s*(?:\*{0,2}\s*)?(?:okay,\s*)?(?:final(?:e|er)?(?: version| antwort)?|final answer|answer|antwort|text|ausgabe|result(?:at)?)(?:\s*\*{0,2})?\s*:\s*$)",
        QRegularExpression::CaseInsensitiveOption
    );
    return pattern.match(line.trimmed()).hasMatch();
}

inline bool isSoftMetaLine(const QString& line)
{
    const QString normalized = canonicalMetaLine(line);
    if (normalized.isEmpty()) {
        return false;
    }

    if (isAnswerMarkerLine(line)) {
        return true;
    }

    static const QStringList prefixes = {
        "role:",
        "task:",
        "context:",
        "constraints:",
        "format:",
        "goal:",
        "plan:",
        "draft:",
        "current state:",
        "determine the current state",
        "previous paragraph:",
        "current beat:",
        "scene setting:",
        "text construction:",
        "writing the text:",
        "refining the flow:",
        "drafting:",
        "refining:",
        "rules:",
        "overall context:",
        "full context:",
        "scene:",
        "start:",
        "middle:",
        "end:",
        "aktueller beat:",
        "gesamtkontext:",
        "regeln:",
        "bisheriger kontext aus dem gedaechtnis:",
        "projektwissen aus persistenter erinnerung:",
        "vorheriger direkt geschriebener absatz:",
        "rolle:",
        "aufgabe:",
        "kontext:",
        "einschraenkungen:",
        "format:",
        "ziel:",
        "plan:",
        "entwurf:",
        "fokusfigur:",
        "ort:",
        "konflikt:",
        "wendung:",
        "szene:",
        "lektorats-feedback:"
    };

    for (const QString& prefix : prefixes) {
        if (normalized.startsWith(prefix)) {
            return true;
        }
    }

    static const QRegularExpression labelPattern(
        R"(^\s*(?:\*{0,2}\s*)?(?:analyze the request|context analysis|draft(?:ing)?|refining(?: the content| for flow)?|final plan|text|current state|writing the text|plan|entwurf|analyse|kontext|finaler plan)(?:\s*\*{0,2})?\s*:\s*$)",
        QRegularExpression::CaseInsensitiveOption
    );

    return labelPattern.match(line.trimmed()).hasMatch();
}

inline QString stripWrappingQuotes(QString text)
{
    text = text.trimmed();
    if (text.size() < 2) {
        return text;
    }

    const QChar first = text.front();
    const QChar last = text.back();
    const bool matchingAsciiQuotes = (first == '"' && last == '"') || (first == '\'' && last == '\'');
    const bool matchingCurlyQuotes = (first == QChar(0x201C) && last == QChar(0x201D))
        || (first == QChar(0x201E) && last == QChar(0x201C));
    if (matchingAsciiQuotes || matchingCurlyQuotes) {
        return text.mid(1, text.size() - 2).trimmed();
    }

    return text;
}

inline QString deduplicateParagraphs(const QString& text)
{
    const QString cleaned = cleanupSanitizedText(text);
    if (cleaned.isEmpty()) {
        return {};
    }

    const QStringList rawParagraphs = cleaned.split(QRegularExpression(R"(\n\s*\n)"), Qt::SkipEmptyParts);
    QStringList paragraphs;
    QSet<QString> seen;

    for (QString paragraph : rawParagraphs) {
        paragraph = stripWrappingQuotes(cleanupSanitizedText(paragraph));
        if (paragraph.isEmpty()) {
            continue;
        }

        const QString key = paragraph.simplified().toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }

        seen.insert(key);
        paragraphs.append(paragraph);
    }

    return cleanupSanitizedText(paragraphs.join("\n\n"));
}

inline QString stripPlainTextReasoning(const QString& text, QString* removedText)
{
    const QString cleaned = cleanupSanitizedText(text);
    if (cleaned.isEmpty()) {
        if (removedText != nullptr) {
            *removedText = {};
        }
        return {};
    }

    const QStringList lines = cleaned.split('\n');
    int hardIndicatorCount = 0;
    int softIndicatorCount = 0;
    const int probeLineCount = qMin(lines.size(), 80);
    for (int index = 0; index < probeLineCount; ++index) {
        const QString& line = lines.at(index);
        if (isHardMetaLine(line)) {
            ++hardIndicatorCount;
        } else if (isSoftMetaLine(line)) {
            ++softIndicatorCount;
        }
    }

    const bool looksContaminated = hardIndicatorCount > 0 || softIndicatorCount >= 4;
    if (!looksContaminated) {
        if (removedText != nullptr) {
            *removedText = {};
        }
        return cleaned;
    }

    int startLineIndex = 0;
    for (int index = lines.size() - 1; index >= 0; --index) {
        if (isAnswerMarkerLine(lines.at(index))) {
            startLineIndex = index + 1;
            break;
        }
    }

    const auto filterFromLine = [&](const int beginIndex, QString* localRemovedText) {
        QStringList keptLines;
        QStringList removedLines;
        bool lastKeptLineWasBlank = true;

        for (int index = beginIndex; index < lines.size(); ++index) {
            const QString line = lines.at(index);
            const QString trimmedLine = line.trimmed();

            if (trimmedLine.isEmpty()) {
                if (!lastKeptLineWasBlank && !keptLines.isEmpty()) {
                    keptLines.append(QString());
                    lastKeptLineWasBlank = true;
                }
                continue;
            }

            if (isHardMetaLine(trimmedLine) || isSoftMetaLine(trimmedLine)) {
                removedLines.append(trimmedLine);
                continue;
            }

            keptLines.append(stripWrappingQuotes(line));
            lastKeptLineWasBlank = false;
        }

        if (localRemovedText != nullptr) {
            *localRemovedText = cleanupSanitizedText(removedLines.join("\n"));
        }
        return deduplicateParagraphs(keptLines.join("\n"));
    };

    QString localRemovedText;
    QString filteredText = filterFromLine(startLineIndex, &localRemovedText);
    if (filteredText.isEmpty() && startLineIndex > 0) {
        filteredText = filterFromLine(0, &localRemovedText);
    }

    if (removedText != nullptr) {
        *removedText = localRemovedText;
    }

    if (filteredText.isEmpty()) {
        if (removedText != nullptr) {
            *removedText = {};
        }
        return cleaned;
    }

    return filteredText;
}

inline SanitizedResponseContent sanitizeModelVisibleText(const QString& rawText)
{
    SanitizedResponseContent result;
    result.visibleText = cleanupSanitizedText(rawText);

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
        const QString reasoning = cleanupSanitizedText(match.captured(2));
        if (!reasoning.isEmpty()) {
            reasoningParts.append(reasoning);
        }
        result.hadReasoningContent = true;
    }

    result.visibleText.remove(reasoningBlockPattern);
    if (result.visibleText.contains(strayReasoningTagPattern)) {
        result.visibleText.remove(strayReasoningTagPattern);
        result.hadReasoningContent = true;
    }

    QString removedReasoning;
    result.visibleText = stripPlainTextReasoning(result.visibleText, &removedReasoning);
    if (!removedReasoning.isEmpty()) {
        reasoningParts.append(removedReasoning);
        result.hadReasoningContent = true;
    }

    result.visibleText = cleanupSanitizedText(result.visibleText);
    result.reasoningText = cleanupSanitizedText(reasoningParts.join("\n\n"));
    return result;
}

} // namespace privateclaw::utils
