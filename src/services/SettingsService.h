#pragma once

#include <QString>
#include <QSettings>

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

private:
    QSettings m_settings;
};

} // namespace privateclaw::services
