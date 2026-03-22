#include "providers/ProviderManager.h"

#include "providers/ILlmProvider.h"

namespace privateclaw::providers {

void ProviderManager::addProvider(std::unique_ptr<ILlmProvider> provider)
{
    m_providers.push_back(std::move(provider));
}

QList<ILlmProvider*> ProviderManager::providers() const
{
    QList<ILlmProvider*> result;
    result.reserve(static_cast<qsizetype>(m_providers.size()));

    for (const auto& provider : m_providers) {
        result.push_back(provider.get());
    }

    return result;
}

ILlmProvider* ProviderManager::providerByName(const QString& name) const
{
    for (const auto& provider : m_providers) {
        if (provider->name().compare(name, Qt::CaseInsensitive) == 0) {
            return provider.get();
        }
    }

    return nullptr;
}

} // namespace privateclaw::providers

