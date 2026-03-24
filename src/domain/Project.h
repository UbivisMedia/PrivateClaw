#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace privateclaw::domain {

struct Project
{
    qint64 id = -1;
    QString name;
    QString providerName = "Ollama";
    QString providerBaseUrl;
    QString description;
    QString defaultModel;
    QString systemPrompt;
    bool confirmShellRun = true;
    bool confirmFileEditDiff = true;
    bool confirmHttpRequest = true;
    bool allowUnattendedRiskyTools = false;
    QStringList tags;
    QDateTime createdAt;
    QDateTime updatedAt;
};

} // namespace privateclaw::domain
