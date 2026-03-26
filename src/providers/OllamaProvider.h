#pragma once

#include "providers/ILlmProvider.h"

namespace privateclaw::providers {

class OllamaProvider : public ILlmProvider
{
public:
    explicit OllamaProvider(QString baseUrl, std::function<bool()> shouldCancel = {});

    QString name() const override;
    QString baseUrl() const override;
    bool isConfigured() const override;
    bool supportsStreaming() const override;
    ProviderHealth healthCheck() override;
    QStringList listModels() override;
    ChatResponse chat(const ChatRequest& request) override;
    ChatResponse chatStream(const ChatRequest& request, const ChatStreamCallback& onChunk) override;

private:
    QString m_baseUrl;
    std::function<bool()> m_shouldCancel;
};

} // namespace privateclaw::providers
