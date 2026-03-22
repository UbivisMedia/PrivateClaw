#pragma once

#include <QString>

namespace privateclaw::utils {

class Logger
{
public:
    static void initialize();
    static void info(const QString& category, const QString& message);
    static QString logFilePath();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
};

} // namespace privateclaw::utils

