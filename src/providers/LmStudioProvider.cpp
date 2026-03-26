#include "providers/LmStudioProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace privateclaw::providers {

namespace {

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

QUrl modelsEndpoint(const QString& baseUrl)
{
    return buildEndpoint(baseUrl, "/v1/models");
}

QUrl chatEndpoint(const QString& baseUrl)
{
    return buildEndpoint(baseUrl, "/v1/chat/completions");
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

QString lmStudioErrorMessageFromPayload(const QByteArray& payload)
{
    const QJsonDocument json = QJsonDocument::fromJson(payload);
    if (!json.isObject()) {
        return {};
    }

    const QJsonObject root = json.object();
    const QJsonValue errorValue = root.value("error");
    if (errorValue.isString()) {
        return errorValue.toString().trimmed();
    }

    if (errorValue.isObject()) {
        return errorValue.toObject().value("message").toString().trimmed();
    }

    return root.value("message").toString().trimmed();
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

    const QJsonArray choices = json.object().value("choices").toArray();
    if (choices.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "LM Studio hat keine Antwortauswahl geliefert.";
        }
        return false;
    }

    const QJsonObject choiceObject = choices.first().toObject();
    const QJsonObject messageObject = choiceObject.value("message").toObject();
    bool reasoningOpen = false;
    QString parsedText = extractLmStudioResponseText(messageObject, &reasoningOpen).trimmed();
    if (parsedText.isEmpty()) {
        parsedText = choiceObject.value("text").toString().trimmed();
    }
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

} // namespace

LmStudioProvider::LmStudioProvider(QString baseUrl)
    : m_baseUrl(std::move(baseUrl))
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

    QNetworkAccessManager networkManager;
    QNetworkRequest networkRequest(chatEndpoint(m_baseUrl));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QEventLoop loop;
    QNetworkReply* reply = networkManager.post(
        networkRequest,
        QJsonDocument(buildChatPayload(request, false)).toJson(QJsonDocument::Compact)
    );
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray payloadBytes = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = lmStudioErrorMessageFromPayload(payloadBytes);
        response.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return response;
    }

    reply->deleteLater();

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

    QNetworkAccessManager networkManager;
    QNetworkRequest networkRequest(chatEndpoint(m_baseUrl));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QEventLoop loop;
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

        const QJsonArray choices = json.object().value("choices").toArray();
        if (choices.isEmpty()) {
            return;
        }

        appendChunk(extractLmStudioResponseText(choices.first().toObject().value("delta").toObject(), &reasoningOpen));
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

    loop.exec();

    if (!pendingBuffer.trimmed().isEmpty()) {
        processEventLine(pendingBuffer);
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = lmStudioErrorMessageFromPayload(rawPayload);
        response.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return response;
    }

    reply->deleteLater();

    if (!parseError.isEmpty() && accumulatedText.trimmed().isEmpty()) {
        QString fallbackText;
        if (parseLmStudioChatPayload(rawPayload, &fallbackText, nullptr)) {
            appendChunk(fallbackText);
            parseError.clear();
        }
    }

    if (!parseError.isEmpty() && accumulatedText.trimmed().isEmpty()) {
        const ChatResponse fallbackResponse = chat(request);
        if (fallbackResponse.success) {
            appendChunk(fallbackResponse.text);
            parseError.clear();
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
        if (parseLmStudioChatPayload(rawPayload, &fallbackText, nullptr)) {
            appendChunk(fallbackText);
        } else {
            const ChatResponse fallbackResponse = chat(request);
            if (fallbackResponse.success) {
                appendChunk(fallbackResponse.text);
            } else {
                response.errorMessage = "LM Studio hat keine auswertbare Streaming-Antwort geliefert.";
                return response;
            }
        }
    }

    response.success = true;
    response.text = accumulatedText.trimmed();
    return response;
}

} // namespace privateclaw::providers
