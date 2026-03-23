#include "providers/LmStudioProvider.h"

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

QString extractMessageText(const QJsonObject& messageObject)
{
    const QJsonValue contentValue = messageObject.value("content");
    if (contentValue.isString()) {
        return contentValue.toString().trimmed();
    }

    if (!contentValue.isArray()) {
        return {};
    }

    QStringList parts;
    const QJsonArray contentItems = contentValue.toArray();
    for (const QJsonValue& itemValue : contentItems) {
        const QJsonObject itemObject = itemValue.toObject();
        const QString type = itemObject.value("type").toString().trimmed();
        if (type.compare("text", Qt::CaseInsensitive) == 0 || type.isEmpty()) {
            const QString text = itemObject.value("text").toString().trimmed();
            if (!text.isEmpty()) {
                parts.append(text);
            }
        }
    }

    return parts.join("\n").trimmed();
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
    QNetworkRequest networkRequest(chatEndpoint(m_baseUrl));
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
        const QString serverError = lmStudioErrorMessageFromPayload(payloadBytes);
        response.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return response;
    }

    reply->deleteLater();

    const QJsonDocument json = QJsonDocument::fromJson(payloadBytes);
    if (!json.isObject()) {
        response.errorMessage = "LM Studio hat keine gueltige JSON-Antwort geliefert.";
        return response;
    }

    const QJsonArray choices = json.object().value("choices").toArray();
    if (choices.isEmpty()) {
        response.errorMessage = "LM Studio hat keine Antwortauswahl geliefert.";
        return response;
    }

    QString text = extractMessageText(choices.first().toObject().value("message").toObject());
    if (text.isEmpty()) {
        text = choices.first().toObject().value("text").toString().trimmed();
    }

    if (text.isEmpty()) {
        response.errorMessage = "LM Studio hat keine auswertbare Antwort geliefert.";
        return response;
    }

    response.success = true;
    response.text = text;
    return response;
}

} // namespace privateclaw::providers
