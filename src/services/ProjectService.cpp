#include "services/ProjectService.h"

#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace privateclaw::services {

ProjectService::ProjectService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

bool ProjectService::createProject(domain::Project* project, QString* errorMessage) const
{
    if (project == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Projekt uebergeben.";
        }
        return false;
    }

    if (project->name.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projektname darf nicht leer sein.";
        }
        return false;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "INSERT INTO projects (name, description, default_model, system_prompt, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(project->name.trimmed());
    query.addBindValue(project->description.trimmed());
    query.addBindValue(project->defaultModel.trimmed());
    query.addBindValue(project->systemPrompt.trimmed());
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    project->id = query.lastInsertId().toLongLong();
    project->name = project->name.trimmed();
    project->description = project->description.trimmed();
    project->defaultModel = project->defaultModel.trimmed();
    project->systemPrompt = project->systemPrompt.trimmed();
    project->createdAt = QDateTime::fromString(timestamp, Qt::ISODate);
    project->updatedAt = QDateTime::fromString(timestamp, Qt::ISODate);
    return true;
}

bool ProjectService::updateProject(const domain::Project& project, QString* errorMessage) const
{
    if (project.id <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projekt-Update braucht eine gueltige ID.";
        }
        return false;
    }

    if (project.name.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projektname darf nicht leer sein.";
        }
        return false;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "UPDATE projects "
        "SET name = ?, description = ?, default_model = ?, system_prompt = ?, updated_at = ? "
        "WHERE id = ?"
    );
    query.addBindValue(project.name.trimmed());
    query.addBindValue(project.description.trimmed());
    query.addBindValue(project.defaultModel.trimmed());
    query.addBindValue(project.systemPrompt.trimmed());
    query.addBindValue(timestamp);
    query.addBindValue(project.id);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

bool ProjectService::deleteProject(const qint64 projectId, QString* errorMessage) const
{
    if (projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zum Loeschen wird eine gueltige Projekt-ID benoetigt.";
        }
        return false;
    }

    QSqlDatabase database = m_databaseManager.database();
    if (!database.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    const QStringList statements = {
        "DELETE FROM schedules WHERE project_id = ?",
        "DELETE FROM runs WHERE project_id = ?",
        "DELETE FROM memory_entries WHERE project_id = ?",
        "DELETE FROM workflows WHERE project_id = ?",
        "DELETE FROM projects WHERE id = ?"
    };

    for (const QString& statement : statements) {
        QSqlQuery query(database);
        query.prepare(statement);
        query.addBindValue(projectId);
        if (!query.exec()) {
            database.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
    }

    if (!database.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    return true;
}

int ProjectService::projectCount() const
{
    QSqlQuery query(m_databaseManager.database());
    if (!query.exec("SELECT COUNT(*) FROM projects")) {
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QList<domain::Project> ProjectService::listProjects() const
{
    QList<domain::Project> projects;

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "SELECT id, name, description, default_model, system_prompt, created_at, updated_at "
        "FROM projects "
        "ORDER BY updated_at DESC, id DESC"
    );

    if (!query.exec()) {
        return projects;
    }

    while (query.next()) {
        domain::Project project;
        project.id = query.value(0).toLongLong();
        project.name = query.value(1).toString();
        project.description = query.value(2).toString();
        project.defaultModel = query.value(3).toString();
        project.systemPrompt = query.value(4).toString();
        project.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        project.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        projects.append(project);
    }

    return projects;
}

} // namespace privateclaw::services
