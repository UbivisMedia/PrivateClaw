#include "services/SettingsService.h"

#include <QCoreApplication>
#include <QDir>

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

} // namespace privateclaw::services
