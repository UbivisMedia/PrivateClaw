#pragma once

#include <QHash>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace privateclaw::services {

class SecretsService
{
public:
    SecretsService();

    bool initialize(QString* errorMessage = nullptr);
    QStringList listSecretNames(qint64 projectId) const;
    QHash<QString, QString> loadSecretsForProject(qint64 projectId, QString* errorMessage = nullptr) const;
    bool saveSecret(qint64 projectId, const QString& name, const QString& value, QString* errorMessage = nullptr);
    bool deleteSecret(qint64 projectId, const QString& name, QString* errorMessage = nullptr);
    bool deleteSecretsForProject(qint64 projectId, QString* errorMessage = nullptr);
    bool isValidSecretName(const QString& name) const;

private:
    QString normalizedSecretName(const QString& name) const;
    QString groupPathForProject(qint64 projectId) const;
    QString encodedSecretName(const QString& name) const;
    QString decodedSecretName(const QString& name) const;
    bool encryptSecret(const QString& plainText, QString* encryptedValue, QString* errorMessage) const;
    bool decryptSecret(const QString& encryptedValue, QString* plainText, QString* errorMessage) const;

    mutable QSettings m_settings;
};

} // namespace privateclaw::services
