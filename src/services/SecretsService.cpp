#include "services/SecretsService.h"

#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace privateclaw::services {

namespace {

QString dpapiErrorMessage(const DWORD errorCode)
{
    return QString("Windows-DPAPI-Fehler %1").arg(errorCode);
}

} // namespace

SecretsService::SecretsService() = default;

bool SecretsService::initialize(QString* errorMessage)
{
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = "Secret-Speicher konnte nicht initialisiert werden.";
        }
        return false;
    }

    return true;
}

QStringList SecretsService::listSecretNames(const qint64 projectId) const
{
    QStringList names;
    if (projectId <= 0) {
        return names;
    }

    m_settings.beginGroup(groupPathForProject(projectId));
    const QStringList encodedNames = m_settings.childGroups();
    m_settings.endGroup();

    names.reserve(encodedNames.size());
    for (const QString& encodedName : encodedNames) {
        const QString decodedName = decodedSecretName(encodedName);
        if (!decodedName.isEmpty()) {
            names.append(decodedName);
        }
    }

    names.sort(Qt::CaseInsensitive);
    return names;
}

QHash<QString, QString> SecretsService::loadSecretsForProject(const qint64 projectId, QString* errorMessage) const
{
    QHash<QString, QString> secrets;
    if (projectId <= 0) {
        return secrets;
    }

    const QStringList names = listSecretNames(projectId);
    for (const QString& name : names) {
        QString plainText;
        m_settings.beginGroup(groupPathForProject(projectId) + "/" + encodedSecretName(name));
        const QString encryptedValue = m_settings.value("ciphertext").toString();
        m_settings.endGroup();

        if (encryptedValue.trimmed().isEmpty()) {
            continue;
        }

        QString decryptError;
        if (!decryptSecret(encryptedValue, &plainText, &decryptError)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = QString("Secret '%1' konnte nicht geladen werden: %2").arg(name, decryptError);
            }
            continue;
        }

        secrets.insert(name, plainText);
    }

    return secrets;
}

bool SecretsService::saveSecret(
    const qint64 projectId,
    const QString& name,
    const QString& value,
    QString* errorMessage
)
{
    if (projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Secrets koennen erst gespeichert werden, wenn das Projekt eine gueltige ID hat.";
        }
        return false;
    }

    const QString normalizedName = normalizedSecretName(name);
    if (!isValidSecretName(normalizedName)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Secret-Namen duerfen nur Buchstaben, Zahlen, Punkt, Minus und Unterstrich enthalten.";
        }
        return false;
    }

    if (value.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Secret-Wert darf nicht leer sein.";
        }
        return false;
    }

    QString encryptedValue;
    if (!encryptSecret(value, &encryptedValue, errorMessage)) {
        return false;
    }

    m_settings.beginGroup(groupPathForProject(projectId) + "/" + encodedSecretName(normalizedName));
    m_settings.setValue("ciphertext", encryptedValue);
    m_settings.setValue("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_settings.endGroup();
    m_settings.sync();

    if (m_settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = "Secret konnte nicht gespeichert werden.";
        }
        return false;
    }

    return true;
}

bool SecretsService::deleteSecret(const qint64 projectId, const QString& name, QString* errorMessage)
{
    if (projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zum Entfernen eines Secrets wird ein gespeichertes Projekt benoetigt.";
        }
        return false;
    }

    const QString normalizedName = normalizedSecretName(name);
    if (normalizedName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Secret-Name darf nicht leer sein.";
        }
        return false;
    }

    m_settings.remove(groupPathForProject(projectId) + "/" + encodedSecretName(normalizedName));
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = "Secret konnte nicht entfernt werden.";
        }
        return false;
    }

    return true;
}

bool SecretsService::deleteSecretsForProject(const qint64 projectId, QString* errorMessage)
{
    if (projectId <= 0) {
        return true;
    }

    m_settings.remove(groupPathForProject(projectId));
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projekt-Secrets konnten nicht entfernt werden.";
        }
        return false;
    }

    return true;
}

bool SecretsService::isValidSecretName(const QString& name) const
{
    static const QRegularExpression pattern("^[A-Za-z0-9_.-]+$");
    return pattern.match(normalizedSecretName(name)).hasMatch();
}

QString SecretsService::normalizedSecretName(const QString& name) const
{
    return name.trimmed();
}

QString SecretsService::groupPathForProject(const qint64 projectId) const
{
    return QString("secrets/projects/%1").arg(projectId);
}

QString SecretsService::encodedSecretName(const QString& name) const
{
    return QString::fromLatin1(QUrl::toPercentEncoding(name));
}

QString SecretsService::decodedSecretName(const QString& name) const
{
    return QUrl::fromPercentEncoding(name.toLatin1());
}

bool SecretsService::encryptSecret(const QString& plainText, QString* encryptedValue, QString* errorMessage) const
{
    if (encryptedValue == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Zielpuffer fuer Secrets fehlt.";
        }
        return false;
    }

#ifdef Q_OS_WIN
    QByteArray plainBytes = plainText.toUtf8();
    DATA_BLOB inputBlob;
    inputBlob.pbData = reinterpret_cast<BYTE*>(plainBytes.data());
    inputBlob.cbData = static_cast<DWORD>(plainBytes.size());

    QByteArray entropyBytes("PrivateClaw.SecretStore");
    DATA_BLOB entropyBlob;
    entropyBlob.pbData = reinterpret_cast<BYTE*>(entropyBytes.data());
    entropyBlob.cbData = static_cast<DWORD>(entropyBytes.size());

    DATA_BLOB outputBlob;
    outputBlob.pbData = nullptr;
    outputBlob.cbData = 0;

    if (!CryptProtectData(
            &inputBlob,
            L"PrivateClaw Secret",
            &entropyBlob,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &outputBlob
        )) {
        if (errorMessage != nullptr) {
            *errorMessage = dpapiErrorMessage(GetLastError());
        }
        return false;
    }

    QByteArray encryptedBytes(
        reinterpret_cast<const char*>(outputBlob.pbData),
        static_cast<qsizetype>(outputBlob.cbData)
    );
    *encryptedValue = QString::fromLatin1(encryptedBytes.toBase64());
    if (outputBlob.pbData != nullptr) {
        LocalFree(outputBlob.pbData);
    }
    return true;
#else
    Q_UNUSED(plainText);
    if (errorMessage != nullptr) {
        *errorMessage = "Sichere Secret-Speicherung ist in diesem Build nicht verfuegbar.";
    }
    return false;
#endif
}

bool SecretsService::decryptSecret(const QString& encryptedValue, QString* plainText, QString* errorMessage) const
{
    if (plainText == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Zielpuffer fuer Secret-Entschluesselung fehlt.";
        }
        return false;
    }

#ifdef Q_OS_WIN
    QByteArray encryptedBytes = QByteArray::fromBase64(encryptedValue.toLatin1());
    DATA_BLOB inputBlob;
    inputBlob.pbData = reinterpret_cast<BYTE*>(encryptedBytes.data());
    inputBlob.cbData = static_cast<DWORD>(encryptedBytes.size());

    QByteArray entropyBytes("PrivateClaw.SecretStore");
    DATA_BLOB entropyBlob;
    entropyBlob.pbData = reinterpret_cast<BYTE*>(entropyBytes.data());
    entropyBlob.cbData = static_cast<DWORD>(entropyBytes.size());

    DATA_BLOB outputBlob;
    outputBlob.pbData = nullptr;
    outputBlob.cbData = 0;

    if (!CryptUnprotectData(
            &inputBlob,
            nullptr,
            &entropyBlob,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &outputBlob
        )) {
        if (errorMessage != nullptr) {
            *errorMessage = dpapiErrorMessage(GetLastError());
        }
        return false;
    }

    QByteArray plainBytes(
        reinterpret_cast<const char*>(outputBlob.pbData),
        static_cast<qsizetype>(outputBlob.cbData)
    );
    *plainText = QString::fromUtf8(plainBytes);
    if (outputBlob.pbData != nullptr) {
        LocalFree(outputBlob.pbData);
    }
    return true;
#else
    Q_UNUSED(encryptedValue);
    if (errorMessage != nullptr) {
        *errorMessage = "Sichere Secret-Speicherung ist in diesem Build nicht verfuegbar.";
    }
    return false;
#endif
}

} // namespace privateclaw::services
