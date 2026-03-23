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
    QString description;
    QString defaultModel;
    QString systemPrompt;
    QStringList tags;
    QDateTime createdAt;
    QDateTime updatedAt;
};

} // namespace privateclaw::domain
