#pragma once

#include <QList>
#include <QString>

#include <memory>
#include <vector>

namespace privateclaw::providers {

class ILlmProvider;

class ProviderManager
{
public:
    void addProvider(std::unique_ptr<ILlmProvider> provider);
    QList<ILlmProvider*> providers() const;
    ILlmProvider* providerByName(const QString& name) const;

private:
    std::vector<std::unique_ptr<ILlmProvider>> m_providers;
};

} // namespace privateclaw::providers

