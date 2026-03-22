#include "providers/LmStudioProvider.h"

#include <utility>

namespace privateclaw::providers {

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

QStringList LmStudioProvider::listModels()
{
    return {};
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
