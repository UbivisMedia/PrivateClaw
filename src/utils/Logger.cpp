#include "utils/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMessageLogger>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdlib>
#include <cstdio>

namespace privateclaw::utils {

namespace {

QFile& sharedLogFile()
{
    static QFile file;
    return file;
}

QMutex& sharedMutex()
{
    static QMutex mutex;
    return mutex;
}

QString levelToString(const QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }

    return "INFO";
}

} // namespace

void Logger::initialize()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);

    QFile& file = sharedLogFile();
    if (!file.isOpen()) {
        file.setFileName(QDir(directory).filePath("privateclaw.log"));
        file.open(QIODevice::Append | QIODevice::Text);
    }

    qInstallMessageHandler(&Logger::messageHandler);
}

void Logger::info(const QString& category, const QString& message)
{
    QMessageLogger logger(nullptr, 0, nullptr, category.toUtf8().constData());
    logger.info().noquote() << message;
}

QString Logger::logFilePath()
{
    return sharedLogFile().fileName();
}

void Logger::messageHandler(
    const QtMsgType type,
    const QMessageLogContext& context,
    const QString& message
)
{
    Q_UNUSED(context);

    const QString line = QString("[%1] [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), levelToString(type), message);

    QMutexLocker locker(&sharedMutex());

    QFile& file = sharedLogFile();
    if (file.isOpen()) {
        QTextStream stream(&file);
        stream << line;
        stream.flush();
    }

    std::fputs(line.toUtf8().constData(), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace privateclaw::utils
