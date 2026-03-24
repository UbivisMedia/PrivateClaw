#include "services/UpdateChecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace privateclaw::services {

namespace {

#ifndef PRIVATECLAW_APP_VERSION
#define PRIVATECLAW_APP_VERSION "0.0.0"
#endif

#ifndef PRIVATECLAW_GITHUB_RELEASE_API_URL
#define PRIVATECLAW_GITHUB_RELEASE_API_URL "https://api.github.com/repos/UbivisMedia/PrivateClaw/releases/latest"
#endif

#ifndef PRIVATECLAW_GITHUB_RELEASES_URL
#define PRIVATECLAW_GITHUB_RELEASES_URL "https://github.com/UbivisMedia/PrivateClaw/releases/latest"
#endif

QString normalizedVersion(const QString& version)
{
    QString normalized = version.trimmed();
    if (normalized.startsWith('v', Qt::CaseInsensitive)) {
        normalized.remove(0, 1);
    }
    return normalized;
}

QList<int> versionSegments(const QString& version)
{
    QList<int> segments;
    QRegularExpressionMatchIterator iterator =
        QRegularExpression("(\\d+)").globalMatch(normalizedVersion(version));
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        segments.append(match.captured(1).toInt());
    }
    return segments;
}

QString parsedErrorMessage(const QByteArray& payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return {};
    }

    return document.object().value("message").toString().trimmed();
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
{
}

QString UpdateChecker::currentVersion()
{
    return QString::fromUtf8(PRIVATECLAW_APP_VERSION).trimmed();
}

QString UpdateChecker::releaseApiUrl()
{
    return QString::fromUtf8(PRIVATECLAW_GITHUB_RELEASE_API_URL).trimmed();
}

QString UpdateChecker::releasesPageUrl()
{
    return QString::fromUtf8(PRIVATECLAW_GITHUB_RELEASES_URL).trimmed();
}

int UpdateChecker::compareVersions(const QString& leftVersion, const QString& rightVersion)
{
    const QList<int> leftSegments = versionSegments(leftVersion);
    const QList<int> rightSegments = versionSegments(rightVersion);
    const int maxSegmentCount = qMax(leftSegments.size(), rightSegments.size());

    for (int index = 0; index < maxSegmentCount; ++index) {
        const int leftValue = index < leftSegments.size() ? leftSegments.at(index) : 0;
        const int rightValue = index < rightSegments.size() ? rightSegments.at(index) : 0;
        if (leftValue < rightValue) {
            return -1;
        }
        if (leftValue > rightValue) {
            return 1;
        }
    }

    return 0;
}

bool UpdateChecker::isNewerVersionAvailable(const QString& currentVersionValue, const QString& latestVersion)
{
    return compareVersions(latestVersion, currentVersionValue) > 0;
}

void UpdateChecker::checkForUpdates(const bool manual)
{
    if (m_activeReply != nullptr) {
        emit checkFinished(
            manual,
            false,
            false,
            currentVersion(),
            QString(),
            releasesPageUrl(),
            "Eine Update-Pruefung laeuft bereits."
        );
        return;
    }

    QNetworkRequest request{QUrl(releaseApiUrl())};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader(
        "User-Agent",
        QString("PrivateClaw/%1").arg(currentVersion()).toUtf8()
    );
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_activeReply = m_networkAccessManager->get(request);
    emit checkStarted(manual);

    QObject::connect(m_activeReply, &QNetworkReply::finished, this, [this, manual]() {
        QNetworkReply* reply = m_activeReply;
        m_activeReply = nullptr;

        if (reply == nullptr) {
            emit checkFinished(
                manual,
                false,
                false,
                currentVersion(),
                QString(),
                releasesPageUrl(),
                "Die Update-Antwort ist unerwartet verschwunden."
            );
            return;
        }

        const QByteArray payload = reply->readAll();
        const QString fallbackReleaseUrl = releasesPageUrl();

        if (reply->error() != QNetworkReply::NoError) {
            QString errorMessage = parsedErrorMessage(payload);
            if (errorMessage.isEmpty()) {
                errorMessage = reply->errorString();
            }

            reply->deleteLater();
            emit checkFinished(
                manual,
                false,
                false,
                currentVersion(),
                QString(),
                fallbackReleaseUrl,
                QString("Update-Pruefung fehlgeschlagen: %1").arg(errorMessage)
            );
            return;
        }

        reply->deleteLater();

        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            emit checkFinished(
                manual,
                false,
                false,
                currentVersion(),
                QString(),
                fallbackReleaseUrl,
                "GitHub lieferte keine gueltigen Release-Daten."
            );
            return;
        }

        const QJsonObject rootObject = document.object();
        const QString latestVersion = rootObject.value("tag_name").toString().trimmed();
        const QString releaseUrl = rootObject.value("html_url").toString().trimmed().isEmpty()
            ? fallbackReleaseUrl
            : rootObject.value("html_url").toString().trimmed();

        if (latestVersion.isEmpty()) {
            emit checkFinished(
                manual,
                false,
                false,
                currentVersion(),
                QString(),
                releaseUrl,
                "GitHub lieferte keinen gueltigen Release-Tag."
            );
            return;
        }

        const QString localVersion = currentVersion();
        const int versionComparison = compareVersions(latestVersion, localVersion);
        const bool updateAvailable = versionComparison > 0;

        QString message;
        if (updateAvailable) {
            message = QString("Neue Version %1 verfuegbar. Lokale Version: %2.")
                          .arg(latestVersion, localVersion);
        } else if (versionComparison == 0) {
            message = QString("Du nutzt bereits das aktuelle Release %1.").arg(localVersion);
        } else {
            message = QString("Lokaler Entwicklungsstand %1 ist neuer als das aktuelle Release %2.")
                          .arg(localVersion, latestVersion);
        }

        emit checkFinished(
            manual,
            true,
            updateAvailable,
            localVersion,
            latestVersion,
            releaseUrl,
            message
        );
    });
}

bool UpdateChecker::isChecking() const
{
    return m_activeReply != nullptr;
}

} // namespace privateclaw::services
