#include "services/SettingsService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace privateclaw::services {

SettingsService::SettingsService() = default;

bool SettingsService::initialize()
{
    if (!m_settings.contains("providers/ollama/baseUrl")) {
        m_settings.setValue("providers/ollama/baseUrl", "http://127.0.0.1:11434");
    }

    if (!m_settings.contains("providers/lmstudio/baseUrl")) {
        m_settings.setValue("providers/lmstudio/baseUrl", "http://127.0.0.1:1234");
    }

    if (!m_settings.contains("tools/comfyui/baseUrl")) {
        m_settings.setValue("tools/comfyui/baseUrl", "http://127.0.0.1:8188");
    }

    if (!m_settings.contains("providers/defaultModel")) {
        m_settings.setValue("providers/defaultModel", "llama3");
    }

    if (!m_settings.contains("tools/workspaceRoot")) {
        const QString guessedWorkspaceRoot = QDir(
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../..")
        ).absolutePath();
        m_settings.setValue("tools/workspaceRoot", guessedWorkspaceRoot);
    }

    if (!m_settings.contains("tools/allowedPaths")) {
        m_settings.setValue("tools/allowedPaths", QStringList{});
    }

    m_settings.sync();
    return m_settings.status() == QSettings::NoError;
}

QString SettingsService::ollamaBaseUrl() const
{
    return m_settings.value("providers/ollama/baseUrl").toString();
}

QString SettingsService::lmStudioBaseUrl() const
{
    return m_settings.value("providers/lmstudio/baseUrl").toString();
}

QString SettingsService::comfyUiBaseUrl() const
{
    return m_settings.value("tools/comfyui/baseUrl").toString();
}

QString SettingsService::defaultModel() const
{
    return m_settings.value("providers/defaultModel").toString();
}

QString SettingsService::workspaceRoot() const
{
    return m_settings.value("tools/workspaceRoot").toString();
}

QStringList SettingsService::customAllowedToolPaths() const
{
    QStringList normalizedPaths;
    QStringList seenPaths;
    const QStringList configuredPaths = m_settings.value("tools/allowedPaths").toStringList();
    for (const QString& configuredPath : configuredPaths) {
        const QString normalizedPath = normalizedAbsolutePath(configuredPath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        const QString foldedPath = normalizedPath.toCaseFolded();
        if (seenPaths.contains(foldedPath)) {
            continue;
        }

        seenPaths.append(foldedPath);
        normalizedPaths.append(normalizedPath);
    }

    return normalizedPaths;
}

QStringList SettingsService::effectiveAllowedToolPaths() const
{
    QStringList allowedPaths;
    QStringList seenPaths;

    const QString workspacePath = normalizedAbsolutePath(workspaceRoot());
    if (!workspacePath.isEmpty()) {
        allowedPaths.append(workspacePath);
        seenPaths.append(workspacePath.toCaseFolded());
    }

    const QStringList customPaths = customAllowedToolPaths();
    for (const QString& customPath : customPaths) {
        const QString foldedPath = customPath.toCaseFolded();
        if (seenPaths.contains(foldedPath)) {
            continue;
        }

        seenPaths.append(foldedPath);
        allowedPaths.append(customPath);
    }

    return allowedPaths;
}

bool SettingsService::addAllowedToolPath(const QString& path, QString* errorMessage)
{
    const QString normalizedPath = normalizedAbsolutePath(path);
    if (normalizedPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Pfad fuer die Allowlist darf nicht leer sein.";
        }
        return false;
    }

    if (isPathAllowed(normalizedPath)) {
        return true;
    }

    QStringList configuredPaths = customAllowedToolPaths();
    configuredPaths.append(normalizedPath);
    m_settings.setValue("tools/allowedPaths", configuredPaths);
    m_settings.sync();

    if (m_settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = "Allowlist konnte nicht gespeichert werden.";
        }
        return false;
    }

    return true;
}

bool SettingsService::removeAllowedToolPath(const QString& path, QString* errorMessage)
{
    const QString normalizedPath = normalizedAbsolutePath(path);
    if (normalizedPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Pfad fuer die Allowlist darf nicht leer sein.";
        }
        return false;
    }

    QStringList configuredPaths = customAllowedToolPaths();
    const QString foldedPath = normalizedPath.toCaseFolded();
    configuredPaths.erase(
        std::remove_if(
            configuredPaths.begin(),
            configuredPaths.end(),
            [&foldedPath](const QString& configuredPath) {
                return configuredPath.toCaseFolded() == foldedPath;
            }
        ),
        configuredPaths.end()
    );

    m_settings.setValue("tools/allowedPaths", configuredPaths);
    m_settings.sync();

    if (m_settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = "Allowlist konnte nicht gespeichert werden.";
        }
        return false;
    }

    return true;
}

bool SettingsService::isPathAllowed(const QString& path) const
{
    const QString normalizedPath = normalizedAbsolutePath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    const QString foldedPath = normalizedPath.toCaseFolded();
    const QStringList allowedPaths = effectiveAllowedToolPaths();
    for (const QString& allowedPath : allowedPaths) {
        const QString foldedAllowedPath = allowedPath.toCaseFolded();
        if (foldedPath == foldedAllowedPath
            || foldedPath.startsWith(foldedAllowedPath + "/")
            || foldedPath.startsWith(foldedAllowedPath + "\\")) {
            return true;
        }
    }

    return false;
}

QString SettingsService::suggestedAllowedToolPath(const QString& path, const bool preferParentDirectory) const
{
    const QString normalizedPath = normalizedAbsolutePath(path);
    if (normalizedPath.isEmpty()) {
        return {};
    }

    if (!preferParentDirectory) {
        return normalizedPath;
    }

    const QFileInfo fileInfo(normalizedPath);
    if (fileInfo.isDir()) {
        return normalizedPath;
    }

    return fileInfo.absolutePath();
}

QString SettingsService::normalizedAbsolutePath(const QString& path) const
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    const QFileInfo fileInfo(trimmedPath);
    const QString absolutePath = fileInfo.isAbsolute()
        ? fileInfo.absoluteFilePath()
        : QDir(workspaceRoot()).absoluteFilePath(trimmedPath);
    return QDir::cleanPath(absolutePath);
}

} // namespace privateclaw::services
