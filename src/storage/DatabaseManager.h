#pragma once

#include <QSqlDatabase>
#include <QString>

namespace privateclaw::storage {

class DatabaseManager
{
public:
    explicit DatabaseManager(QString connectionName = "privateclaw");
    ~DatabaseManager();

    bool initialize(QString* errorMessage = nullptr);
    QSqlDatabase database() const;
    QString databasePath() const;

private:
    bool executeSchema(QString* errorMessage);

    QString m_connectionName;
    QString m_databasePath;
};

} // namespace privateclaw::storage

