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

QUrl modelsEndpoint(const QString& baseUrl)
{
    QUrl url(baseUrl);
    QString path = url.path().trimmed();
    if (path.isEmpty() || path == "/") {
        path = "/v1/models";
    } else {
        if (path.endsWith('/')) {
            path.chop(1);
        }
        path += "/v1/models";
    }
    url.setPath(path);
    return url;
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
    response.success = false;
    response.errorMessage = QString(
        "LM-Studio-Integration fuer Modell '%1' ist im Grundgeruest noch nicht implementiert."
    ).arg(request.model);
    return response;
}

} // namespace privateclaw::providers
