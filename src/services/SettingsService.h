#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

namespace privateclaw::services {

class SettingsService
{
public:
    SettingsService();

    bool initialize();
    QString ollamaBaseUrl() const;
    QString lmStudioBaseUrl() const;
    QString comfyUiBaseUrl() const;
    QString defaultModel() const;
    QString workspaceRoot() const;
    QStringList customAllowedToolPaths() const;
    QStringList effectiveAllowedToolPaths() const;
    bool backgroundTrayHintShown() const;
    void setBackgroundTrayHintShown(bool shown);
    bool addAllowedToolPath(const QString& path, QString* errorMessage = nullptr);
    bool removeAllowedToolPath(const QString& path, QString* errorMessage = nullptr);
    bool isPathAllowed(const QString& path) const;
    QString suggestedAllowedToolPath(const QString& path, bool preferParentDirectory) const;

private:
    QString normalizedAbsolutePath(const QString& path) const;
    QSettings m_settings;
};

} // namespace privateclaw::services
