#pragma once

#include <QString>
#include <QStringList>

namespace privateclaw::providers {

struct ChatRequest
{
    QString model;
    QString systemPrompt;
    QString userPrompt;
};

struct ChatResponse
{
    bool success = false;
    QString text;
    QString errorMessage;
};

struct ProviderHealth
{
    bool success = false;
    int statusCode = 0;
    QString message;
    QStringList models;
};

class ILlmProvider
{
public:
    virtual ~ILlmProvider() = default;

    virtual QString name() const = 0;
    virtual QString baseUrl() const = 0;
    virtual bool isConfigured() const = 0;
    virtual ProviderHealth healthCheck() = 0;
    virtual QStringList listModels() = 0;
    virtual ChatResponse chat(const ChatRequest& request) = 0;
};

} // namespace privateclaw::providers
