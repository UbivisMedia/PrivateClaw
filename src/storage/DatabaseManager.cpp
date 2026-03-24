#include "storage/DatabaseManager.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace privateclaw::storage {

namespace {

bool tableHasColumn(QSqlDatabase database, const QString& tableName, const QString& columnName)
{
    QSqlQuery query(database);
    query.prepare(QString("PRAGMA table_info(%1)").arg(tableName));
    if (!query.exec()) {
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString().compare(columnName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

bool ensureColumnExists(
    QSqlDatabase database,
    const QString& tableName,
    const QString& columnName,
    const QString& alterStatement,
    QString* errorMessage
)
{
    if (tableHasColumn(database, tableName, columnName)) {
        return true;
    }

    QSqlQuery query(database);
    if (query.exec(alterStatement)) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

} // namespace

DatabaseManager::DatabaseManager(QString connectionName)
    : m_connectionName(std::move(connectionName))
{
}

DatabaseManager::~DatabaseManager()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
        if (database.isValid()) {
            database.close();
        }
    }
}

bool DatabaseManager::initialize(QString* errorMessage)
{
    QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDirectory.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein gueltiger AppData-Pfad verfuegbar.";
        }
        return false;
    }

    QDir directory;
    if (!directory.mkpath(dataDirectory)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Das AppData-Verzeichnis konnte nicht angelegt werden.";
        }
        return false;
    }

    m_databasePath = QDir(dataDirectory).filePath("privateclaw.sqlite");

    QSqlDatabase database = QSqlDatabase::contains(m_connectionName)
        ? QSqlDatabase::database(m_connectionName)
        : QSqlDatabase::addDatabase("QSQLITE", m_connectionName);

    database.setDatabaseName(m_databasePath);
    if (!database.open()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    return executeSchema(errorMessage);
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

QString DatabaseManager::databasePath() const
{
    return m_databasePath;
}

bool DatabaseManager::executeSchema(QString* errorMessage)
{
    static const QStringList statements = {
        "CREATE TABLE IF NOT EXISTS projects ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "provider_name TEXT NOT NULL DEFAULT 'Ollama',"
        "provider_base_url TEXT NOT NULL DEFAULT '',"
        "description TEXT NOT NULL DEFAULT '',"
        "default_model TEXT NOT NULL DEFAULT '',"
        "system_prompt TEXT NOT NULL DEFAULT '',"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL"
        ")",
        "CREATE TABLE IF NOT EXISTS workflows ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER NOT NULL,"
        "name TEXT NOT NULL,"
        "description TEXT NOT NULL DEFAULT '',"
        "definition_json TEXT NOT NULL DEFAULT '{}',"
        "is_active INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL"
        ")",
        "CREATE TABLE IF NOT EXISTS runs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER NOT NULL,"
        "workflow_id INTEGER NOT NULL,"
        "status TEXT NOT NULL,"
        "summary TEXT NOT NULL DEFAULT '',"
        "started_at TEXT NOT NULL,"
        "finished_at TEXT"
        ")",
        "CREATE TABLE IF NOT EXISTS memory_entries ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER NOT NULL,"
        "entry_type TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "source TEXT NOT NULL DEFAULT '',"
        "tags TEXT NOT NULL DEFAULT '',"
        "relevance INTEGER NOT NULL DEFAULT 0,"
        "created_at TEXT NOT NULL"
        ")",
        "CREATE TABLE IF NOT EXISTS schedules ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER NOT NULL,"
        "workflow_id INTEGER NOT NULL,"
        "trigger_type TEXT NOT NULL,"
        "trigger_expression TEXT NOT NULL,"
        "next_run_at TEXT,"
        "last_run_at TEXT,"
        "enabled INTEGER NOT NULL DEFAULT 1"
        ")"
    };

    QSqlQuery query(database());
    for (const QString& statement : statements) {
        if (!query.exec(statement)) {
            if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
    }

    if (!ensureColumnExists(
            database(),
            "projects",
            "provider_name",
            "ALTER TABLE projects ADD COLUMN provider_name TEXT NOT NULL DEFAULT 'Ollama'",
            errorMessage
        )) {
        return false;
    }

    if (!ensureColumnExists(
            database(),
            "projects",
            "provider_base_url",
            "ALTER TABLE projects ADD COLUMN provider_base_url TEXT NOT NULL DEFAULT ''",
            errorMessage
        )) {
        return false;
    }

    if (!ensureColumnExists(
            database(),
            "schedules",
            "last_run_at",
            "ALTER TABLE schedules ADD COLUMN last_run_at TEXT",
            errorMessage
        )) {
        return false;
    }

    return true;
}

} // namespace privateclaw::storage
