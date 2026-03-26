#include "providers/OllamaProvider.h"

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

QString manualCancellationMessage()
{
    return "Workflow wurde manuell abgebrochen.";
}

bool isCancellationRequested(const std::function<bool()>& shouldCancel)
{
    return static_cast<bool>(shouldCancel) && shouldCancel();
}

QUrl buildEndpoint(const QString& baseUrl, const QString& suffix)
{
    QUrl url(baseUrl);
    QString path = url.path().trimmed();
    if (path.isEmpty() || path == "/") {
        path = suffix;
    } else {
        if (path.endsWith('/')) {
            path.chop(1);
        }
        path += suffix;
    }
    url.setPath(path);
    return url;
}

QUrl tagsEndpoint(const QString& baseUrl)
{
    return buildEndpoint(baseUrl, "/api/tags");
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

ProviderHealth performHealthCheck(const QUrl& url, const QString& modelKey)
{
    ProviderHealth health;

    QNetworkAccessManager networkManager;
    QNetworkRequest request(url);

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
    const QJsonArray models = json.object().value("models").toArray();
    for (const QJsonValue& value : models) {
        const QJsonObject modelObject = value.toObject();
        const QString modelName = modelObject.value(modelKey).toString();
        if (!modelName.isEmpty()) {
            health.models.append(modelName);
        }
    }

    health.success = true;
    health.message = QString("Verbindung erfolgreich. %1 Modell(e) gefunden.").arg(health.models.size());
    return health;
}

QString ollamaErrorMessageFromPayload(const QByteArray& payload)
{
    const QJsonDocument json = QJsonDocument::fromJson(payload);
    if (!json.isObject()) {
        return {};
    }

    return json.object().value("error").toString().trimmed();
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

QString extractOllamaResponseText(const QJsonObject& root, bool* reasoningOpen)
{
    const QJsonObject messageObject = root.value("message").toObject();
    const QString reasoningFragment = firstNonEmptyString(
        messageObject.isEmpty() ? root : messageObject,
        { "thinking", "reasoning", "analysis", "thought", "reflection" }
    );
    const QString contentFragment = messageObject.isEmpty()
        ? root.value("response").toString()
        : firstNonEmptyString(messageObject, { "content", "response" });

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

    if (root.value("done").toBool(false) && reasoningOpen != nullptr && *reasoningOpen) {
        chunk += "</think>";
        *reasoningOpen = false;
    }

    return chunk;
}

} // namespace

OllamaProvider::OllamaProvider(QString baseUrl, std::function<bool()> shouldCancel)
    : m_baseUrl(std::move(baseUrl))
    , m_shouldCancel(std::move(shouldCancel))
{
}

QString OllamaProvider::name() const
{
    return "Ollama";
}

QString OllamaProvider::baseUrl() const
{
    return m_baseUrl;
}

bool OllamaProvider::isConfigured() const
{
    return !m_baseUrl.trimmed().isEmpty();
}

bool OllamaProvider::supportsStreaming() const
{
    return true;
}

ProviderHealth OllamaProvider::healthCheck()
{
    if (!isConfigured()) {
        ProviderHealth health;
        health.message = "Keine Ollama-URL konfiguriert.";
        return health;
    }

    return performHealthCheck(tagsEndpoint(m_baseUrl), "name");
}

QStringList OllamaProvider::listModels()
{
    return healthCheck().models;
}

ChatResponse OllamaProvider::chat(const ChatRequest& request)
{
    ChatResponse response;
    if (!isConfigured()) {
        response.errorMessage = "Keine Ollama-URL konfiguriert.";
        return response;
    }

    if (request.model.trimmed().isEmpty()) {
        response.errorMessage = "Kein Ollama-Modell angegeben.";
        return response;
    }

    QNetworkAccessManager networkManager;
    QNetworkRequest networkRequest(buildEndpoint(m_baseUrl, "/api/chat"));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QEventLoop loop;
    QTimer cancelTimer;
    bool cancelled = false;

    QNetworkReply* reply = networkManager.post(
        networkRequest,
        QJsonDocument(buildChatPayload(request, false)).toJson(QJsonDocument::Compact)
    );
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (m_shouldCancel) {
        cancelTimer.setInterval(100);
        cancelTimer.setSingleShot(false);
        QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
            if (!isCancellationRequested(m_shouldCancel)) {
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

    const QByteArray payloadBytes = reply->readAll();
    if (cancelled || (isCancellationRequested(m_shouldCancel) && reply->error() == QNetworkReply::OperationCanceledError)) {
        response.errorMessage = manualCancellationMessage();
        reply->deleteLater();
        return response;
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = ollamaErrorMessageFromPayload(payloadBytes);
        response.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return response;
    }

    reply->deleteLater();

    const QJsonDocument json = QJsonDocument::fromJson(payloadBytes);
    const QJsonObject root = json.object();
    bool reasoningOpen = false;
    const QString text = extractOllamaResponseText(root, &reasoningOpen).trimmed();

    if (text.trimmed().isEmpty()) {
        response.errorMessage = "Ollama hat keine auswertbare Antwort geliefert.";
        return response;
    }

    response.success = true;
    response.text = text.trimmed();
    return response;
}

ChatResponse OllamaProvider::chatStream(const ChatRequest& request, const ChatStreamCallback& onChunk)
{
    ChatResponse response;
    if (!isConfigured()) {
        response.errorMessage = "Keine Ollama-URL konfiguriert.";
        return response;
    }

    if (request.model.trimmed().isEmpty()) {
        response.errorMessage = "Kein Ollama-Modell angegeben.";
        return response;
    }

    QNetworkAccessManager networkManager;
    QNetworkRequest networkRequest(buildEndpoint(m_baseUrl, "/api/chat"));
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

    const auto processLine = [&](QByteArray line) {
        line = line.trimmed();
        if (line.isEmpty() || !parseError.isEmpty()) {
            return;
        }

        QJsonParseError jsonError;
        const QJsonDocument json = QJsonDocument::fromJson(line, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !json.isObject()) {
            parseError = QString("Ollama-Streaming lieferte ungueltiges JSON: %1").arg(jsonError.errorString());
            return;
        }

        appendChunk(extractOllamaResponseText(json.object(), &reasoningOpen));
    };

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        const QByteArray incoming = reply->readAll();
        rawPayload += incoming;
        pendingBuffer += incoming;

        int newlineIndex = pendingBuffer.indexOf('\n');
        while (newlineIndex >= 0) {
            processLine(pendingBuffer.left(newlineIndex));
            pendingBuffer.remove(0, newlineIndex + 1);
            newlineIndex = pendingBuffer.indexOf('\n');
        }
    });

    if (m_shouldCancel) {
        cancelTimer.setInterval(100);
        cancelTimer.setSingleShot(false);
        QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
            if (!isCancellationRequested(m_shouldCancel)) {
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
        processLine(pendingBuffer);
    }

    if (cancelled || (isCancellationRequested(m_shouldCancel) && reply->error() == QNetworkReply::OperationCanceledError)) {
        response.errorMessage = manualCancellationMessage();
        reply->deleteLater();
        return response;
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = ollamaErrorMessageFromPayload(rawPayload);
        response.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return response;
    }

    reply->deleteLater();

    if (!parseError.isEmpty()) {
        response.errorMessage = parseError;
        return response;
    }

    if (reasoningOpen) {
        appendChunk("</think>");
    }

    if (accumulatedText.trimmed().isEmpty()) {
        response.errorMessage = "Ollama hat keine auswertbare Streaming-Antwort geliefert.";
        return response;
    }

    response.success = true;
    response.text = accumulatedText.trimmed();
    return response;
}

} // namespace privateclaw::providers
