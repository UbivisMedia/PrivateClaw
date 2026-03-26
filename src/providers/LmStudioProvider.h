#pragma once

#include "providers/ILlmProvider.h"

#include <QHash>

namespace privateclaw::providers {

class LmStudioProvider : public ILlmProvider
{
public:
    explicit LmStudioProvider(QString baseUrl, std::function<bool()> shouldCancel = {});

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
    QHash<QString, int> m_contextLengthCache;
    QHash<QString, int> m_maxContextLengthCache;
};

} // namespace privateclaw::providers
