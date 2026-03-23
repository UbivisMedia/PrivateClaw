#include "providers/OllamaProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace privateclaw::providers {

namespace {

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

} // namespace

OllamaProvider::OllamaProvider(QString baseUrl)
    : m_baseUrl(std::move(baseUrl))
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

    const QJsonObject payload{
        { "model", request.model.trimmed() },
        { "stream", false },
        { "messages", messages }
    };

    QNetworkAccessManager networkManager;
    QNetworkRequest networkRequest(buildEndpoint(m_baseUrl, "/api/chat"));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QEventLoop loop;

    QNetworkReply* reply = networkManager.post(
        networkRequest,
        QJsonDocument(payload).toJson(QJsonDocument::Compact)
    );
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray payloadBytes = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = ollamaErrorMessageFromPayload(payloadBytes);
        response.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return response;
    }

    reply->deleteLater();

    const QJsonDocument json = QJsonDocument::fromJson(payloadBytes);
    const QJsonObject root = json.object();
    QString text = root.value("message").toObject().value("content").toString();
    if (text.trimmed().isEmpty()) {
        text = root.value("response").toString();
    }

    if (text.trimmed().isEmpty()) {
        response.errorMessage = "Ollama hat keine auswertbare Antwort geliefert.";
        return response;
    }

    response.success = true;
    response.text = text.trimmed();
    return response;
}

} // namespace privateclaw::providers
