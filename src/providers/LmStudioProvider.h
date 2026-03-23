#pragma once

#include "providers/ILlmProvider.h"

namespace privateclaw::providers {

class LmStudioProvider : public ILlmProvider
{
public:
    explicit LmStudioProvider(QString baseUrl);

    QString name() const override;
    QString baseUrl() const override;
    bool isConfigured() const override;
    ProviderHealth healthCheck() override;
    QStringList listModels() override;
    ChatResponse chat(const ChatRequest& request) override;

private:
    QString m_baseUrl;
};

} // namespace privateclaw::providers
