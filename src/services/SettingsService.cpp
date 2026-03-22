#include "services/SettingsService.h"

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

    if (!m_settings.contains("providers/defaultModel")) {
        m_settings.setValue("providers/defaultModel", "llama3");
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

QString SettingsService::defaultModel() const
{
    return m_settings.value("providers/defaultModel").toString();
}

} // namespace privateclaw::services

