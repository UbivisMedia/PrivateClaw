#include "services/WorkflowService.h"

#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace privateclaw::services {

namespace {

bool parseDefinition(
    const QString& definitionJson,
    QString* normalizedJson,
    QVector<domain::WorkflowStep>* steps,
    QString* errorMessage
)
{
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(definitionJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("JSON-Fehler: %1").arg(parseError.errorString());
        }
        return false;
    }

    if (!json.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Workflow-Definition muss ein JSON-Objekt sein.";
        }
        return false;
    }

    const QJsonObject root = json.object();
    if (!root.value("steps").isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Workflow-Definition braucht ein Array 'steps'.";
        }
        return false;
    }

    QVector<domain::WorkflowStep> parsedSteps;
    const QJsonArray stepArray = root.value("steps").toArray();
    parsedSteps.reserve(stepArray.size());

    for (int index = 0; index < stepArray.size(); ++index) {
        if (!stepArray.at(index).isObject()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Schritt %1 muss ein JSON-Objekt sein.").arg(index + 1);
            }
            return false;
        }

        const QJsonObject stepObject = stepArray.at(index).toObject();
        const QString stepId = stepObject.value("id").toString().trimmed();
        const QString stepType = stepObject.value("type").toString().trimmed();

        if (stepId.isEmpty() || stepType.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Schritt %1 braucht mindestens 'id' und 'type'.").arg(index + 1);
            }
            return false;
        }

        const QJsonValue configValue = stepObject.value("config");
        if (!configValue.isUndefined() && !configValue.isObject()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Schritt %1 hat ein ungueltiges 'config'-Feld.").arg(index + 1);
            }
            return false;
        }

        domain::WorkflowStep step;
        step.id = stepId;
        step.type = stepType;
        step.name = stepObject.value("name").toString();
        step.config = configValue.toObject();
        parsedSteps.append(step);
    }

    if (normalizedJson != nullptr) {
        *normalizedJson = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    if (steps != nullptr) {
        *steps = parsedSteps;
    }

    return true;
}

} // namespace

WorkflowService::WorkflowService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

bool WorkflowService::hydrateWorkflowDefinition(domain::Workflow* workflow, QString* errorMessage) const
{
    if (workflow == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Workflow uebergeben.";
        }
        return false;
    }

    QString normalizedJson;
    QVector<domain::WorkflowStep> steps;
    if (!parseDefinition(workflow->definitionJson, &normalizedJson, &steps, errorMessage)) {
        return false;
    }

    workflow->definitionJson = normalizedJson;
    workflow->steps = steps;
    return true;
}

bool WorkflowService::saveWorkflow(domain::Workflow* workflow, QString* errorMessage) const
{
    if (workflow == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Kein Workflow uebergeben.";
        }
        return false;
    }

    if (workflow->projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Workflow braucht ein zugeordnetes Projekt.";
        }
        return false;
    }

    if (workflow->name.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Workflow-Name darf nicht leer sein.";
        }
        return false;
    }

    if (!hydrateWorkflowDefinition(workflow, errorMessage)) {
        return false;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery query(m_databaseManager.database());
    if (workflow->id > 0) {
        query.prepare(
            "UPDATE workflows "
            "SET project_id = ?, name = ?, description = ?, definition_json = ?, is_active = ?, updated_at = ? "
            "WHERE id = ?"
        );
        query.addBindValue(workflow->projectId);
        query.addBindValue(workflow->name.trimmed());
        query.addBindValue(workflow->description.trimmed());
        query.addBindValue(workflow->definitionJson);
        query.addBindValue(workflow->active ? 1 : 0);
        query.addBindValue(timestamp);
        query.addBindValue(workflow->id);
    } else {
        query.prepare(
            "INSERT INTO workflows (project_id, name, description, definition_json, is_active, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"
        );
        query.addBindValue(workflow->projectId);
        query.addBindValue(workflow->name.trimmed());
        query.addBindValue(workflow->description.trimmed());
        query.addBindValue(workflow->definitionJson);
        query.addBindValue(workflow->active ? 1 : 0);
        query.addBindValue(timestamp);
        query.addBindValue(timestamp);
    }

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (workflow->id <= 0) {
        workflow->id = query.lastInsertId().toLongLong();
        workflow->createdAt = QDateTime::fromString(timestamp, Qt::ISODate);
    }

    workflow->name = workflow->name.trimmed();
    workflow->description = workflow->description.trimmed();
    workflow->updatedAt = QDateTime::fromString(timestamp, Qt::ISODate);
    return true;
}

int WorkflowService::workflowCount() const
{
    QSqlQuery query(m_databaseManager.database());
    if (!query.exec("SELECT COUNT(*) FROM workflows")) {
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QList<domain::Workflow> WorkflowService::listWorkflows() const
{
    QList<domain::Workflow> workflows;

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "SELECT id, project_id, name, description, definition_json, is_active, created_at, updated_at "
        "FROM workflows "
        "ORDER BY updated_at DESC, id DESC"
    );

    if (!query.exec()) {
        return workflows;
    }

    while (query.next()) {
        domain::Workflow workflow;
        workflow.id = query.value(0).toLongLong();
        workflow.projectId = query.value(1).toLongLong();
        workflow.name = query.value(2).toString();
        workflow.description = query.value(3).toString();
        workflow.definitionJson = query.value(4).toString();
        workflow.active = query.value(5).toInt() != 0;
        workflow.createdAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        workflow.updatedAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);

        QString normalizedJson;
        QVector<domain::WorkflowStep> steps;
        if (parseDefinition(workflow.definitionJson, &normalizedJson, &steps, nullptr)) {
            workflow.definitionJson = normalizedJson;
            workflow.steps = steps;
        }

        workflows.append(workflow);
    }

    return workflows;
}

} // namespace privateclaw::services
