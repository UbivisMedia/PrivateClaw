#include "providers/LmStudioProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace privateclaw::providers {

namespace {

QString manualCancellationMessage()
{
    return "Workflow wurde manuell abgebrochen.";
}

bool isCancellationRequested(const std::function<bool()>& shouldCancel)
{
    return static_cast<bool>(shouldCancel) && shouldCancel();
}

struct ModelContextInfo
{
    int contextLength = -1;
    int maxContextLength = -1;
};

QUrl buildEndpoint(const QString& baseUrl, const QString& suffix)
{
    QUrl url(baseUrl);
    QString path = url.path().trimmed();
    const QString normalizedSuffix = suffix.startsWith('/') ? suffix : QString("/%1").arg(suffix);

    if (path.isEmpty() || path == "/") {
        path = normalizedSuffix;
    } else {
        if (path.endsWith('/')) {
            path.chop(1);
        }

        if (path.endsWith(normalizedSuffix)) {
            url.setPath(path);
            return url;
        }

        if (path == "/v1" && normalizedSuffix.startsWith("/v1/")) {
            path += normalizedSuffix.mid(3);
        } else {
            path += normalizedSuffix;
        }
    }

    url.setPath(path);
    return url;
}

QUrl buildServerEndpoint(const QString& baseUrl, const QString& suffix)
{
    QUrl url(baseUrl);
    QString path = url.path().trimmed();
    const QString normalizedSuffix = suffix.startsWith('/') ? suffix : QString("/%1").arg(suffix);

    if (path.endsWith('/')) {
        path.chop(1);
    }
    if (path.endsWith("/api/v1")) {
        path.chop(7);
    } else if (path.endsWith("/v1")) {
        path.chop(3);
    }

    if (path.isEmpty()) {
        path = normalizedSuffix;
    } else {
        path += normalizedSuffix;
    }

    url.setPath(path);
    return url;
}

QUrl modelsEndpoint(const QString& baseUrl)
{
    return buildEndpoint(baseUrl, "/v1/models");
}

QUrl chatEndpoint(const QString& baseUrl)
{
    return buildEndpoint(baseUrl, "/v1/chat/completions");
}

QUrl restModelsEndpoint(const QString& baseUrl)
{
    return buildServerEndpoint(baseUrl, "/api/v1/models");
}

QJsonArray buildMessages(const ChatRequest& request)
{
    QJsonArray messages;
    if (!request.systemPrompt.trimmed().isEmpty()) {
        messages.append(QJsonObject{
            { "role", "system" },
            { "content", request.systemPrompt }
        });
    }
    messages.append(QJsonObject{
        { "role", "user" },
        { "content", request.userPrompt }
    });
    return messages;
}

QJsonObject buildChatPayload(const ChatRequest& request, const bool stream)
{
    return QJsonObject{
        { "model", request.model.trimmed() },
        { "stream", stream },
        { "messages", buildMessages(request) }
    };
}

QString lmStudioErrorMessageFromJsonObject(const QJsonObject& root)
{
    const QJsonValue errorValue = root.value("error");
    if (errorValue.isString()) {
        return errorValue.toString().trimmed();
    }

    if (errorValue.isObject()) {
        return errorValue.toObject().value("message").toString().trimmed();
    }

    return root.value("message").toString().trimmed();
}

QString lmStudioErrorMessageFromPayload(const QByteArray& payload)
{
    const QJsonDocument json = QJsonDocument::fromJson(payload);
    if (!json.isObject()) {
        return {};
    }

    return lmStudioErrorMessageFromJsonObject(json.object());
}

QString extractTextFromValue(const QJsonValue& value, const bool trimParts, const QString& separator)
{
    if (value.isString()) {
        return trimParts ? value.toString().trimmed() : value.toString();
    }

    if (!value.isArray()) {
        return {};
    }

    QStringList parts;
    const QJsonArray contentItems = value.toArray();
    for (const QJsonValue& itemValue : contentItems) {
        const QJsonObject itemObject = itemValue.toObject();
        const QString type = itemObject.value("type").toString().trimmed();
        if (type.compare("text", Qt::CaseInsensitive) == 0 || type.isEmpty()) {
            const QString text = trimParts
                ? itemObject.value("text").toString().trimmed()
                : itemObject.value("text").toString();
            if (!text.isEmpty()) {
                parts.append(text);
            }
        }
    }

    return parts.join(separator);
}

QString extractMessageText(const QJsonObject& messageObject)
{
    return extractTextFromValue(messageObject.value("content"), true, "\n").trimmed();
}

QString extractMessageChunk(const QJsonObject& messageObject)
{
    return extractTextFromValue(messageObject.value("content"), false, QString());
}

QString firstNonEmptyString(const QJsonObject& object, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString value = object.value(key).toString();
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

QString extractLmStudioResponseText(const QJsonObject& messageObject, bool* reasoningOpen)
{
    const QString reasoningFragment = firstNonEmptyString(
        messageObject,
        { "reasoning_content", "reasoning", "thinking", "analysis", "thought", "reflection" }
    );
    const QString contentFragment = messageObject.contains("content")
        ? extractMessageChunk(messageObject)
        : firstNonEmptyString(messageObject, { "text" });

    QString chunk;
    if (!reasoningFragment.isEmpty()) {
        if (reasoningOpen != nullptr && !*reasoningOpen) {
            chunk += "<think>";
            *reasoningOpen = true;
        }
        chunk += reasoningFragment;
    }

    if (!contentFragment.isEmpty()) {
        if (reasoningOpen != nullptr && *reasoningOpen) {
            chunk += "</think>";
            *reasoningOpen = false;
        }
        chunk += contentFragment;
    }

    return chunk;
}

QString extractLmStudioChoiceText(const QJsonObject& choiceObject, bool* reasoningOpen)
{
    QString chunk = extractLmStudioResponseText(choiceObject.value("delta").toObject(), reasoningOpen);
    if (!chunk.isEmpty()) {
        return chunk;
    }

    chunk = extractLmStudioResponseText(choiceObject.value("message").toObject(), reasoningOpen);
    if (!chunk.isEmpty()) {
        return chunk;
    }

    chunk = extractLmStudioResponseText(choiceObject, reasoningOpen);
    if (!chunk.isEmpty()) {
        return chunk;
    }

    if (choiceObject.contains("content")) {
        return extractTextFromValue(choiceObject.value("content"), false, QString());
    }

    return firstNonEmptyString(choiceObject, { "text" });
}

bool parseLmStudioChatPayload(const QByteArray& payload, QString* text, QString* errorMessage = nullptr)
{
    if (text == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Zielpuffer fuer LM-Studio-Antwort fehlt.";
        }
        return false;
    }

    const QJsonDocument json = QJsonDocument::fromJson(payload);
    if (!json.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = "LM Studio hat keine gueltige JSON-Antwort geliefert.";
        }
        return false;
    }

    const QJsonObject root = json.object();
    const QString serverError = lmStudioErrorMessageFromJsonObject(root);
    if (!serverError.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = serverError;
        }
        return false;
    }

    const QJsonArray choices = root.value("choices").toArray();
    if (choices.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "LM Studio hat keine Antwortauswahl geliefert.";
        }
        return false;
    }

    const QJsonObject choiceObject = choices.first().toObject();
    bool reasoningOpen = false;
    QString parsedText = extractLmStudioChoiceText(choiceObject, &reasoningOpen).trimmed();
    if (reasoningOpen) {
        parsedText += "</think>";
    }

    if (parsedText.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "LM Studio hat keine auswertbare Antwort geliefert.";
        }
        return false;
    }

    *text = parsedText;
    return true;
}

bool isContextLengthError(const QString& errorMessage)
{
    const QString normalized = errorMessage.trimmed().toLower();
    return normalized.contains("n_ctx")
        || normalized.contains("n_keep")
        || normalized.contains("context length")
        || normalized.contains("context window")
        || normalized.contains("tokens to keep");
}

int extractContextLengthFromError(const QString& errorMessage)
{
    static const QRegularExpression directPattern("n_ctx\\s*:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch directMatch = directPattern.match(errorMessage);
    if (directMatch.hasMatch()) {
        return directMatch.captured(1).toInt();
    }

    static const QRegularExpression fallbackPattern(
        "context\\s+length[^\\d]*(\\d+)",
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpressionMatch fallbackMatch = fallbackPattern.match(errorMessage);
    return fallbackMatch.hasMatch() ? fallbackMatch.captured(1).toInt() : -1;
}

QString trimTextForContext(const QString& text, const int maxChars, const QString& label)
{
    if (maxChars <= 0 || text.size() <= maxChars) {
        return text;
    }

    const QString marker =
        QString("\n\n[... %1 wurde automatisch gekuerzt, damit der Request in das LM-Studio-Kontextfenster passt ...]\n\n")
            .arg(label);
    if (maxChars <= marker.size() + 48) {
        return text.left(qMax(0, maxChars)).trimmed();
    }

    const int keepChars = maxChars - marker.size();
    const int headChars = qMax(24, keepChars * 2 / 3);
    const int tailChars = qMax(24, keepChars - headChars);
    return text.left(headChars).trimmed() + marker + text.right(tailChars).trimmed();
}

int estimatedTokenCount(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return 0;
    }

    const int charEstimate = (text.size() + 2) / 3;
    const int newlinePenalty = text.count('\n') / 8;
    return charEstimate + newlinePenalty;
}

ChatRequest prepareRequestForContext(
    const ChatRequest& request,
    const ModelContextInfo& contextInfo,
    QStringList* logs = nullptr,
    const bool aggressive = false
)
{
    ChatRequest preparedRequest = request;
    const int contextLength = contextInfo.contextLength > 0 ? contextInfo.contextLength : contextInfo.maxContextLength;
    if (contextLength <= 0) {
        return preparedRequest;
    }

    const int outputReserve = aggressive
        ? qBound(256, contextLength / 5, 1536)
        : qBound(192, contextLength / 6, 1024);
    const int promptBudgetTokens = qMax(512, contextLength - outputReserve - 128);
    const int estimatedPromptTokens =
        estimatedTokenCount(request.systemPrompt) + estimatedTokenCount(request.userPrompt) + 48;
    if (estimatedPromptTokens <= promptBudgetTokens) {
        return preparedRequest;
    }

    const int safeChars = qMax(1600, aggressive ? promptBudgetTokens * 2 : (promptBudgetTokens * 5) / 2);
    int systemBudget = 0;
    int userBudget = 0;
    if (!request.systemPrompt.isEmpty() && !request.userPrompt.isEmpty()) {
        systemBudget = qMin(request.systemPrompt.size(), qMax(500, safeChars * 3 / 10));
        userBudget = qMin(request.userPrompt.size(), qMax(900, safeChars - systemBudget));
        if (systemBudget + userBudget > safeChars) {
            userBudget = qMax(400, safeChars - systemBudget);
        }
        if (systemBudget + userBudget > safeChars) {
            systemBudget = qMax(200, safeChars - userBudget);
        }
        if (request.userPrompt.size() < userBudget) {
            systemBudget = qMin(request.systemPrompt.size(), safeChars - request.userPrompt.size());
        } else if (request.systemPrompt.size() < systemBudget) {
            userBudget = qMin(request.userPrompt.size(), safeChars - request.systemPrompt.size());
        }
    } else if (!request.systemPrompt.isEmpty()) {
        systemBudget = safeChars;
    } else {
        userBudget = safeChars;
    }

    preparedRequest.systemPrompt = trimTextForContext(request.systemPrompt, systemBudget, "Systemprompt");
    preparedRequest.userPrompt = trimTextForContext(request.userPrompt, userBudget, "Prompt");
    if (logs != nullptr) {
        logs->append(
            QString(
                "[warn] LM Studio Request wurde vorab auf das gemeldete Kontextfenster von %1 Token gekuerzt."
            ).arg(contextLength)
        );
    }
    return preparedRequest;
}

bool runNetworkLoop(
    QNetworkReply* reply,
    const int timeoutMs,
    const std::function<bool()>& shouldCancel,
    QByteArray* payload,
    QString* errorMessage
)
{
    if (reply == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Netzwerk-Reply fehlt.";
        }
        return false;
    }

    QEventLoop loop;
    QTimer timeoutTimer;
    QTimer cancelTimer;
    bool cancelled = false;

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (timeoutMs > 0) {
        timeoutTimer.setInterval(timeoutMs);
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeoutTimer.start();
    }
    if (shouldCancel) {
        cancelTimer.setInterval(100);
        cancelTimer.setSingleShot(false);
        QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
            if (!isCancellationRequested(shouldCancel)) {
                return;
            }

            cancelled = true;
            if (reply->isRunning()) {
                reply->abort();
            }
            loop.quit();
        });
        cancelTimer.start();
    }

    loop.exec();

    if (cancelTimer.isActive()) {
        cancelTimer.stop();
    }
    if (timeoutMs > 0 && timeoutTimer.isActive()) {
        timeoutTimer.stop();
    } else if (timeoutMs > 0) {
        reply->abort();
        if (errorMessage != nullptr) {
            *errorMessage = "Zeitueberschreitung bei der LM-Studio-Anfrage.";
        }
        reply->deleteLater();
        return false;
    }

    const QByteArray payloadBytes = reply->readAll();
    if (payload != nullptr) {
        *payload = payloadBytes;
    }

    if (cancelled || (isCancellationRequested(shouldCancel) && reply->error() == QNetworkReply::OperationCanceledError)) {
        if (errorMessage != nullptr) {
            *errorMessage = manualCancellationMessage();
        }
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage != nullptr) {
            const QString serverError = lmStudioErrorMessageFromPayload(payloadBytes);
            *errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        }
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    return true;
}

ModelContextInfo fetchModelContextInfo(
    const QString& baseUrl,
    const QString& requestedModel,
    const std::function<bool()>& shouldCancel,
    QStringList* logs = nullptr
)
{
    ModelContextInfo info;
    if (requestedModel.trimmed().isEmpty()) {
        return info;
    }

    QNetworkAccessManager networkManager;
    QNetworkRequest request(restModelsEndpoint(baseUrl));
    QByteArray payload;
    QString errorMessage;
    if (!runNetworkLoop(networkManager.get(request), 3500, shouldCancel, &payload, &errorMessage)) {
        if (logs != nullptr && !errorMessage.trimmed().isEmpty() && errorMessage != manualCancellationMessage()) {
            logs->append(QString("[warn] LM Studio Kontextfenster konnte nicht abgefragt werden: %1").arg(errorMessage));
        }
        return info;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (logs != nullptr) {
            logs->append(
                QString("[warn] LM Studio Kontextfenster konnte nicht geparst werden: %1").arg(parseError.errorString())
            );
        }
        return info;
    }

    const QString normalizedRequestedModel = requestedModel.trimmed().toCaseFolded();
    const QJsonArray models = document.object().value("models").toArray();

    QJsonObject exactLoadedMatch;
    QJsonObject exactModelMatch;
    QJsonObject singleLoadedModel;
    int loadedModelCount = 0;

    for (const QJsonValue& modelValue : models) {
        const QJsonObject modelObject = modelValue.toObject();
        const QString key = modelObject.value("key").toString().trimmed();
        const QString displayName = modelObject.value("display_name").toString().trimmed();
        const QJsonArray loadedInstances = modelObject.value("loaded_instances").toArray();

        if (!loadedInstances.isEmpty()) {
            ++loadedModelCount;
            singleLoadedModel = modelObject;
        }

        for (const QJsonValue& loadedValue : loadedInstances) {
            const QString loadedId = loadedValue.toObject().value("id").toString().trimmed();
            if (!loadedId.isEmpty() && loadedId.toCaseFolded() == normalizedRequestedModel) {
                exactLoadedMatch = modelObject;
                break;
            }
        }
        if (!exactLoadedMatch.isEmpty()) {
            break;
        }

        if ((!key.isEmpty() && key.toCaseFolded() == normalizedRequestedModel)
            || (!displayName.isEmpty() && displayName.toCaseFolded() == normalizedRequestedModel)) {
            exactModelMatch = modelObject;
        }
    }

    const QJsonObject matchedModel = !exactLoadedMatch.isEmpty()
        ? exactLoadedMatch
        : (!exactModelMatch.isEmpty() ? exactModelMatch : (loadedModelCount == 1 ? singleLoadedModel : QJsonObject()));
    if (matchedModel.isEmpty()) {
        return info;
    }

    info.maxContextLength = matchedModel.value("max_context_length").toInt(-1);
    const QJsonArray loadedInstances = matchedModel.value("loaded_instances").toArray();
    for (const QJsonValue& loadedValue : loadedInstances) {
        const QJsonObject loadedObject = loadedValue.toObject();
        const QString loadedId = loadedObject.value("id").toString().trimmed();
        if (!normalizedRequestedModel.isEmpty()
            && !loadedId.isEmpty()
            && loadedId.toCaseFolded() != normalizedRequestedModel
            && loadedModelCount != 1) {
            continue;
        }

        info.contextLength = loadedObject.value("config").toObject().value("context_length").toInt(-1);
        if (info.contextLength > 0) {
            break;
        }
    }

    return info;
}

ChatResponse executeChatOnce(const QString& baseUrl, const ChatRequest& request, const std::function<bool()>& shouldCancel)
{
    ChatResponse response;

    QNetworkAccessManager networkManager;
    QNetworkRequest networkRequest(chatEndpoint(baseUrl));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QByteArray payloadBytes;
    QString requestError;
    if (!runNetworkLoop(
            networkManager.post(networkRequest, QJsonDocument(buildChatPayload(request, false)).toJson(QJsonDocument::Compact)),
            0,
            shouldCancel,
            &payloadBytes,
            &requestError
        )) {
        response.errorMessage = requestError;
        return response;
    }

    QString text;
    QString parseError;
    if (!parseLmStudioChatPayload(payloadBytes, &text, &parseError)) {
        response.errorMessage = parseError;
        return response;
    }

    response.success = true;
    response.text = text;
    return response;
}

ChatResponse executeStreamingChatOnce(
    const QString& baseUrl,
    const ChatRequest& request,
    const ChatStreamCallback& onChunk,
    const std::function<bool()>& shouldCancel
)
{
    ChatResponse response;

    QNetworkAccessManager networkManager;
    QNetworkRequest networkRequest(chatEndpoint(baseUrl));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QEventLoop loop;
    QTimer cancelTimer;
    QNetworkReply* reply = networkManager.post(
        networkRequest,
        QJsonDocument(buildChatPayload(request, true)).toJson(QJsonDocument::Compact)
    );
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    QByteArray pendingBuffer;
    QByteArray rawPayload;
    QString accumulatedText;
    QString parseError;
    bool reasoningOpen = false;
    bool cancelled = false;

    const auto appendChunk = [&](const QString& chunk) {
        if (chunk.isEmpty()) {
            return;
        }

        accumulatedText += chunk;
        if (onChunk) {
            onChunk(chunk);
        }
    };

    const auto processEventLine = [&](QByteArray line) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(':') || !parseError.isEmpty()) {
            return;
        }

        if (!line.startsWith("data:")) {
            return;
        }

        const QByteArray data = line.mid(5).trimmed();
        if (data == "[DONE]") {
            if (reasoningOpen) {
                appendChunk("</think>");
                reasoningOpen = false;
            }
            return;
        }

        QJsonParseError jsonError;
        const QJsonDocument json = QJsonDocument::fromJson(data, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !json.isObject()) {
            parseError = QString("LM-Studio-Streaming lieferte ungueltiges JSON: %1").arg(jsonError.errorString());
            return;
        }

        const QJsonObject root = json.object();
        const QString streamError = lmStudioErrorMessageFromJsonObject(root);
        if (!streamError.isEmpty()) {
            parseError = streamError;
            return;
        }

        const QJsonArray choices = root.value("choices").toArray();
        if (choices.isEmpty()) {
            return;
        }

        appendChunk(extractLmStudioChoiceText(choices.first().toObject(), &reasoningOpen));
    };

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        const QByteArray incoming = reply->readAll();
        rawPayload += incoming;
        pendingBuffer += incoming;

        int newlineIndex = pendingBuffer.indexOf('\n');
        while (newlineIndex >= 0) {
            processEventLine(pendingBuffer.left(newlineIndex));
            pendingBuffer.remove(0, newlineIndex + 1);
            newlineIndex = pendingBuffer.indexOf('\n');
        }
    });

    if (shouldCancel) {
        cancelTimer.setInterval(100);
        cancelTimer.setSingleShot(false);
        QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
            if (!isCancellationRequested(shouldCancel)) {
                return;
            }

            cancelled = true;
            if (reply->isRunning()) {
                reply->abort();
            }
            loop.quit();
        });
        cancelTimer.start();
    }

    loop.exec();
    if (cancelTimer.isActive()) {
        cancelTimer.stop();
    }

    if (!pendingBuffer.trimmed().isEmpty()) {
        processEventLine(pendingBuffer);
    }

    if (cancelled || (isCancellationRequested(shouldCancel) && reply->error() == QNetworkReply::OperationCanceledError)) {
        response.errorMessage = manualCancellationMessage();
        reply->deleteLater();
        return response;
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = lmStudioErrorMessageFromPayload(rawPayload);
        response.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return response;
    }

    reply->deleteLater();

    if (!parseError.isEmpty() && accumulatedText.trimmed().isEmpty() && !isCancellationRequested(shouldCancel)) {
        QString fallbackText;
        if (parseLmStudioChatPayload(rawPayload, &fallbackText, nullptr)) {
            appendChunk(fallbackText);
            parseError.clear();
        }
    }

    if (!parseError.isEmpty() && accumulatedText.trimmed().isEmpty() && !isCancellationRequested(shouldCancel)) {
        const ChatResponse fallbackResponse = executeChatOnce(baseUrl, request, shouldCancel);
        if (fallbackResponse.success) {
            appendChunk(fallbackResponse.text);
            response.logs.append(fallbackResponse.logs);
            parseError.clear();
        } else if (!fallbackResponse.errorMessage.trimmed().isEmpty()) {
            response.errorMessage = fallbackResponse.errorMessage.trimmed();
            response.logs.append(fallbackResponse.logs);
            return response;
        }
    }

    if (!parseError.isEmpty() && accumulatedText.trimmed().isEmpty()) {
        response.errorMessage = parseError;
        return response;
    }

    if (reasoningOpen) {
        appendChunk("</think>");
    }

    if (accumulatedText.trimmed().isEmpty()) {
        QString fallbackText;
        if (!isCancellationRequested(shouldCancel) && parseLmStudioChatPayload(rawPayload, &fallbackText, nullptr)) {
            appendChunk(fallbackText);
        } else {
            if (isCancellationRequested(shouldCancel)) {
                response.errorMessage = manualCancellationMessage();
                return response;
            }
            const ChatResponse fallbackResponse = executeChatOnce(baseUrl, request, shouldCancel);
            response.logs.append(fallbackResponse.logs);
            if (fallbackResponse.success) {
                appendChunk(fallbackResponse.text);
            } else {
                response.errorMessage = fallbackResponse.errorMessage.trimmed().isEmpty()
                    ? "LM Studio hat keine auswertbare Streaming-Antwort geliefert."
                    : fallbackResponse.errorMessage.trimmed();
                return response;
            }
        }
    }

    response.success = true;
    response.text = accumulatedText.trimmed();
    return response;
}

} // namespace

LmStudioProvider::LmStudioProvider(QString baseUrl, std::function<bool()> shouldCancel)
    : m_baseUrl(std::move(baseUrl))
    , m_shouldCancel(std::move(shouldCancel))
{
}

QString LmStudioProvider::name() const
{
    return "LM Studio";
}

QString LmStudioProvider::baseUrl() const
{
    return m_baseUrl;
}

bool LmStudioProvider::isConfigured() const
{
    return !m_baseUrl.trimmed().isEmpty();
}

bool LmStudioProvider::supportsStreaming() const
{
    return true;
}

ProviderHealth LmStudioProvider::healthCheck()
{
    ProviderHealth health;
    if (!isConfigured()) {
        health.message = "Keine LM-Studio-URL konfiguriert.";
        return health;
    }

    QNetworkAccessManager networkManager;
    QNetworkRequest request(modelsEndpoint(m_baseUrl));

    QEventLoop loop;
    QTimer timeout;
    timeout.setInterval(3000);
    timeout.setSingleShot(true);

    QNetworkReply* reply = networkManager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start();
    loop.exec();

    if (timeout.isActive()) {
        timeout.stop();
    } else {
        reply->abort();
        health.message = "Zeitueberschreitung beim Verbindungsaufbau.";
        reply->deleteLater();
        return health;
    }

    health.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        health.message = reply->errorString();
        reply->deleteLater();
        return health;
    }

    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    const QJsonDocument json = QJsonDocument::fromJson(payload);
    const QJsonArray models = json.object().value("data").toArray();
    for (const QJsonValue& value : models) {
        const QString modelName = value.toObject().value("id").toString();
        if (!modelName.isEmpty()) {
            health.models.append(modelName);
        }
    }

    health.success = true;
    health.message = QString("Verbindung erfolgreich. %1 Modell(e) gefunden.").arg(health.models.size());
    return health;
}

QStringList LmStudioProvider::listModels()
{
    return healthCheck().models;
}

ChatResponse LmStudioProvider::chat(const ChatRequest& request)
{
    ChatResponse response;
    if (!isConfigured()) {
        response.errorMessage = "Keine LM-Studio-URL konfiguriert.";
        return response;
    }

    if (request.model.trimmed().isEmpty()) {
        response.errorMessage = "Kein LM-Studio-Modell angegeben.";
        return response;
    }

    const QString cacheKey = request.model.trimmed().toCaseFolded();
    QStringList preparationLogs;
    ModelContextInfo contextInfo;
    contextInfo.contextLength = m_contextLengthCache.value(cacheKey, -1);
    contextInfo.maxContextLength = m_maxContextLengthCache.value(cacheKey, -1);
    if (contextInfo.contextLength <= 0 && contextInfo.maxContextLength <= 0) {
        contextInfo = fetchModelContextInfo(m_baseUrl, request.model, m_shouldCancel, &preparationLogs);
        if (contextInfo.contextLength > 0) {
            m_contextLengthCache.insert(cacheKey, contextInfo.contextLength);
        }
        if (contextInfo.maxContextLength > 0) {
            m_maxContextLengthCache.insert(cacheKey, contextInfo.maxContextLength);
        }
    }

    if (contextInfo.contextLength > 0 && contextInfo.maxContextLength > contextInfo.contextLength) {
        preparationLogs.append(
            QString(
                "[warn] LM Studio meldet fuer '%1' aktuell %2 Token geladenen Kontext bei %3 Token Modellmaximum."
            ).arg(request.model).arg(contextInfo.contextLength).arg(contextInfo.maxContextLength)
        );
        preparationLogs.append(
            "[warn] Hinweis: Wenn du die Kontextlaenge gerade in LM Studio erhoeht hast, muss das Modell meist neu geladen werden."
        );
    }

    ChatRequest preparedRequest = prepareRequestForContext(request, contextInfo, &preparationLogs, false);
    response = executeChatOnce(m_baseUrl, preparedRequest, m_shouldCancel);
    response.logs.append(preparationLogs);

    if (!response.success && isContextLengthError(response.errorMessage) && !isCancellationRequested(m_shouldCancel)) {
        const int errorContextLength = extractContextLengthFromError(response.errorMessage);
        if (errorContextLength > 0) {
            contextInfo.contextLength = errorContextLength;
            m_contextLengthCache.insert(cacheKey, errorContextLength);
        }

        QStringList retryLogs;
        const ChatRequest retryRequest = prepareRequestForContext(request, contextInfo, &retryLogs, true);
        if (retryRequest.systemPrompt != preparedRequest.systemPrompt || retryRequest.userPrompt != preparedRequest.userPrompt) {
            const QString retryNote =
                "[warn] LM Studio meldete ein Kontextlimit. Request wird mit aggressiverer Kuerzung erneut gesendet.";
            response.logs.append(retryNote);
            const ChatResponse retryResponse = executeChatOnce(m_baseUrl, retryRequest, m_shouldCancel);
            response.logs.append(retryLogs);
            response.logs.append(retryResponse.logs);
            if (retryResponse.success || !retryResponse.errorMessage.trimmed().isEmpty()) {
                response = retryResponse;
                response.logs.append(retryNote);
                response.logs.append(preparationLogs);
                response.logs.append(retryLogs);
            }
        }
    }

    if (!response.success && isContextLengthError(response.errorMessage)) {
        const int effectiveContextLength = contextInfo.contextLength > 0 ? contextInfo.contextLength : extractContextLengthFromError(response.errorMessage);
        if (effectiveContextLength > 0) {
            response.errorMessage = QString(
                "%1 LM Studio meldet derzeit ein geladenes Kontextfenster von %2 Token."
            ).arg(response.errorMessage, QString::number(effectiveContextLength));
        }
    }

    return response;
}

ChatResponse LmStudioProvider::chatStream(const ChatRequest& request, const ChatStreamCallback& onChunk)
{
    ChatResponse response;
    if (!isConfigured()) {
        response.errorMessage = "Keine LM-Studio-URL konfiguriert.";
        return response;
    }

    if (request.model.trimmed().isEmpty()) {
        response.errorMessage = "Kein LM-Studio-Modell angegeben.";
        return response;
    }

    const QString cacheKey = request.model.trimmed().toCaseFolded();
    QStringList preparationLogs;
    ModelContextInfo contextInfo;
    contextInfo.contextLength = m_contextLengthCache.value(cacheKey, -1);
    contextInfo.maxContextLength = m_maxContextLengthCache.value(cacheKey, -1);
    if (contextInfo.contextLength <= 0 && contextInfo.maxContextLength <= 0) {
        contextInfo = fetchModelContextInfo(m_baseUrl, request.model, m_shouldCancel, &preparationLogs);
        if (contextInfo.contextLength > 0) {
            m_contextLengthCache.insert(cacheKey, contextInfo.contextLength);
        }
        if (contextInfo.maxContextLength > 0) {
            m_maxContextLengthCache.insert(cacheKey, contextInfo.maxContextLength);
        }
    }

    if (contextInfo.contextLength > 0 && contextInfo.maxContextLength > contextInfo.contextLength) {
        preparationLogs.append(
            QString(
                "[warn] LM Studio meldet fuer '%1' aktuell %2 Token geladenen Kontext bei %3 Token Modellmaximum."
            ).arg(request.model).arg(contextInfo.contextLength).arg(contextInfo.maxContextLength)
        );
        preparationLogs.append(
            "[warn] Hinweis: Wenn du die Kontextlaenge gerade in LM Studio erhoeht hast, muss das Modell meist neu geladen werden."
        );
    }

    ChatRequest preparedRequest = prepareRequestForContext(request, contextInfo, &preparationLogs, false);
    response = executeStreamingChatOnce(m_baseUrl, preparedRequest, onChunk, m_shouldCancel);
    response.logs.append(preparationLogs);

    if (!response.success && isContextLengthError(response.errorMessage) && !isCancellationRequested(m_shouldCancel)) {
        const int errorContextLength = extractContextLengthFromError(response.errorMessage);
        if (errorContextLength > 0) {
            contextInfo.contextLength = errorContextLength;
            m_contextLengthCache.insert(cacheKey, errorContextLength);
        }

        QStringList retryLogs;
        const ChatRequest retryRequest = prepareRequestForContext(request, contextInfo, &retryLogs, true);
        if (retryRequest.systemPrompt != preparedRequest.systemPrompt || retryRequest.userPrompt != preparedRequest.userPrompt) {
            const QString retryNote =
                "[warn] LM Studio meldete ein Kontextlimit. Streaming-Request wird mit aggressiverer Kuerzung erneut gesendet.";
            response.logs.append(retryNote);
            const ChatResponse retryResponse = executeStreamingChatOnce(m_baseUrl, retryRequest, onChunk, m_shouldCancel);
            response.logs.append(retryLogs);
            response.logs.append(retryResponse.logs);
            if (retryResponse.success || !retryResponse.errorMessage.trimmed().isEmpty()) {
                response = retryResponse;
                response.logs.append(retryNote);
                response.logs.append(preparationLogs);
                response.logs.append(retryLogs);
            }
        }
    }

    if (!response.success && isContextLengthError(response.errorMessage)) {
        const int effectiveContextLength = contextInfo.contextLength > 0 ? contextInfo.contextLength : extractContextLengthFromError(response.errorMessage);
        if (effectiveContextLength > 0) {
            response.errorMessage = QString(
                "%1 LM Studio meldet derzeit ein geladenes Kontextfenster von %2 Token."
            ).arg(response.errorMessage, QString::number(effectiveContextLength));
        }
    }

    return response;
}

} // namespace privateclaw::providers
