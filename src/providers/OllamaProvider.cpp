#include "providers/OllamaProvider.h"

#include <utility>

namespace privateclaw::providers {

OllamaProvider::OllamaProvider(QString baseUrl)
    : m_baseUrl(std::move(baseUrl))
{
}

QString OllamaProvider::name() const
{
    return "Ollama";
}

QString OllamaProvider::baseUrl() const
{
    return m_baseUrl;
}

bool OllamaProvider::isConfigured() const
{
    return !m_baseUrl.trimmed().isEmpty();
}

QStringList OllamaProvider::listModels()
{
    return {};
}

ChatResponse OllamaProvider::chat(const ChatRequest& request)
{
    ChatResponse response;
    response.success = false;
    response.errorMessage = QString(
        "Ollama-Integration fuer Modell '%1' ist im Grundgeruest noch nicht implementiert."
    ).arg(request.model);
    return response;
}

} // namespace privateclaw::providers
