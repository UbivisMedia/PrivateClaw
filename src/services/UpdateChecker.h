#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace privateclaw::services {

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    static QString currentVersion();
    static QString releaseApiUrl();
    static QString releasesPageUrl();
    static int compareVersions(const QString& leftVersion, const QString& rightVersion);
    static bool isNewerVersionAvailable(const QString& currentVersion, const QString& latestVersion);

    void checkForUpdates(bool manual = false);
    bool isChecking() const;

signals:
    void checkStarted(bool manual);
    void checkFinished(
        bool manual,
        bool success,
        bool updateAvailable,
        const QString& currentVersion,
        const QString& latestVersion,
        const QString& releaseUrl,
        const QString& message
    );

private:
    QNetworkAccessManager* m_networkAccessManager = nullptr;
    QNetworkReply* m_activeReply = nullptr;
};

} // namespace privateclaw::services
