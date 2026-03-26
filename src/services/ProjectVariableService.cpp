#include "services/ProjectVariableService.h"

#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include <cmath>

namespace privateclaw::services {

namespace {

domain::ProjectVariable mapProjectVariable(const QSqlQuery& query)
{
    domain::ProjectVariable variable;
    variable.id = query.value(0).toLongLong();
    variable.projectId = query.value(1).toLongLong();
    variable.name = query.value(2).toString();
    variable.valueType = query.value(3).toString();
    variable.valueText = query.value(4).toString();
    variable.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
    variable.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
    return variable;
}

QString valueTextFromDouble(const double value)
{
    return QString::number(value, 'g', 16);
}

} // namespace

ProjectVariableService::ProjectVariableService(storage::DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager)
{
}

bool ProjectVariableService::isValidVariableName(const QString& name)
{
    static const QRegularExpression pattern("^[A-Za-z0-9_.-]+$");
    return pattern.match(name.trimmed()).hasMatch();
}

QString ProjectVariableService::normalizeValueType(const QString& valueType)
{
    const QString normalized = valueType.trimmed().toLower();
    if (normalized == "int" || normalized == "integer") {
        return "int";
    }
    if (normalized == "float" || normalized == "double" || normalized == "number") {
        return "float";
    }
    if (normalized.isEmpty() || normalized == "string" || normalized == "str" || normalized == "text") {
        return "string";
    }
    return {};
}

bool ProjectVariableService::normalizeValueForType(
    const QString& valueType,
    const QString& rawValue,
    QString* normalizedValue,
    QString* errorMessage
)
{
    if (normalizedValue == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Ziel fuer den normalisierten Variablenwert fehlt.";
        }
        return false;
    }

    const QString normalizedType = normalizeValueType(valueType);
    if (normalizedType.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Unbekannter Variablentyp '%1'.").arg(valueType);
        }
        return false;
    }

    if (normalizedType == "string") {
        *normalizedValue = rawValue;
        return true;
    }

    const QString trimmedValue = rawValue.trimmed();
    if (trimmedValue.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Typ '%1' braucht einen numerischen Wert.").arg(normalizedType);
        }
        return false;
    }

    if (normalizedType == "int") {
        bool ok = false;
        const qlonglong parsedValue = trimmedValue.toLongLong(&ok);
        if (!ok) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("'%1' ist kein gueltiger Integer-Wert.").arg(trimmedValue);
            }
            return false;
        }
        *normalizedValue = QString::number(parsedValue);
        return true;
    }

    bool ok = false;
    const double parsedValue = trimmedValue.toDouble(&ok);
    if (!ok || !std::isfinite(parsedValue)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("'%1' ist kein gueltiger Float-Wert.").arg(trimmedValue);
        }
        return false;
    }

    *normalizedValue = valueTextFromDouble(parsedValue);
    return true;
}

int ProjectVariableService::variableCountForProject(const qint64 projectId) const
{
    if (projectId <= 0) {
        return 0;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare("SELECT COUNT(*) FROM project_variables WHERE project_id = ?");
    query.addBindValue(projectId);
    if (!query.exec() || !query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QList<domain::ProjectVariable> ProjectVariableService::listVariables(const qint64 projectId) const
{
    QList<domain::ProjectVariable> variables;
    if (projectId <= 0) {
        return variables;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "SELECT id, project_id, name, value_type, value_text, created_at, updated_at "
        "FROM project_variables "
        "WHERE project_id = ? "
        "ORDER BY updated_at DESC, name COLLATE NOCASE ASC, id DESC"
    );
    query.addBindValue(projectId);
    if (!query.exec()) {
        return variables;
    }

    while (query.next()) {
        variables.append(mapProjectVariable(query));
    }

    return variables;
}

bool ProjectVariableService::findVariableByName(
    const qint64 projectId,
    const QString& name,
    domain::ProjectVariable* variable,
    QString* errorMessage
) const
{
    if (projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projektvariablen brauchen eine gueltige Projekt-ID.";
        }
        return false;
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projektvariablen brauchen einen Namen.";
        }
        return false;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare(
        "SELECT id, project_id, name, value_type, value_text, created_at, updated_at "
        "FROM project_variables "
        "WHERE project_id = ? AND LOWER(name) = LOWER(?) "
        "LIMIT 1"
    );
    query.addBindValue(projectId);
    query.addBindValue(trimmedName);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.next()) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return false;
    }

    if (variable != nullptr) {
        *variable = mapProjectVariable(query);
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

bool ProjectVariableService::saveVariable(domain::ProjectVariable* variable, QString* errorMessage) const
{
    if (variable == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Keine Projektvariable uebergeben.";
        }
        return false;
    }

    if (variable->projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projektvariablen brauchen eine gueltige Projekt-ID.";
        }
        return false;
    }

    variable->name = variable->name.trimmed();
    if (variable->name.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Variablenname darf nicht leer sein.";
        }
        return false;
    }

    if (!isValidVariableName(variable->name)) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "Variablennamen duerfen nur Buchstaben, Zahlen, Punkt, Unterstrich und Bindestrich enthalten.";
        }
        return false;
    }

    const QString normalizedType = normalizeValueType(variable->valueType);
    if (normalizedType.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Unbekannter Variablentyp '%1'.").arg(variable->valueType);
        }
        return false;
    }

    QString normalizedValue;
    if (!normalizeValueForType(normalizedType, variable->valueText, &normalizedValue, errorMessage)) {
        return false;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(m_databaseManager.database());

    if (variable->id > 0) {
        QSqlQuery duplicateQuery(m_databaseManager.database());
        duplicateQuery.prepare(
            "SELECT id FROM project_variables "
            "WHERE project_id = ? AND LOWER(name) = LOWER(?) AND id <> ? "
            "LIMIT 1"
        );
        duplicateQuery.addBindValue(variable->projectId);
        duplicateQuery.addBindValue(variable->name);
        duplicateQuery.addBindValue(variable->id);
        if (!duplicateQuery.exec()) {
            if (errorMessage != nullptr) {
                *errorMessage = duplicateQuery.lastError().text();
            }
            return false;
        }
        if (duplicateQuery.next()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Die Variable '%1' existiert in diesem Projekt bereits.").arg(variable->name);
            }
            return false;
        }

        query.prepare(
            "UPDATE project_variables "
            "SET name = ?, value_type = ?, value_text = ?, updated_at = ? "
            "WHERE id = ?"
        );
        query.addBindValue(variable->name);
        query.addBindValue(normalizedType);
        query.addBindValue(normalizedValue);
        query.addBindValue(timestamp);
        query.addBindValue(variable->id);

        if (!query.exec()) {
            if (errorMessage != nullptr) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }

        QSqlQuery reloadQuery(m_databaseManager.database());
        reloadQuery.prepare(
            "SELECT id, project_id, name, value_type, value_text, created_at, updated_at "
            "FROM project_variables WHERE id = ?"
        );
        reloadQuery.addBindValue(variable->id);
        if (!reloadQuery.exec() || !reloadQuery.next()) {
            if (errorMessage != nullptr) {
                *errorMessage = reloadQuery.lastError().isValid()
                    ? reloadQuery.lastError().text()
                    : "Aktualisierte Projektvariable konnte nicht erneut geladen werden.";
            }
            return false;
        }

        *variable = mapProjectVariable(reloadQuery);
        return true;
    }

    domain::ProjectVariable existingVariable;
    QString lookupError;
    if (findVariableByName(variable->projectId, variable->name, &existingVariable, &lookupError)) {
        variable->id = existingVariable.id;
        variable->createdAt = existingVariable.createdAt;
        variable->updatedAt = existingVariable.updatedAt;
        variable->valueType = normalizedType;
        variable->valueText = normalizedValue;
        return saveVariable(variable, errorMessage);
    }
    if (!lookupError.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = lookupError;
        }
        return false;
    }

    query.prepare(
        "INSERT INTO project_variables ("
        "project_id, name, value_type, value_text, created_at, updated_at"
        ") VALUES (?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(variable->projectId);
    query.addBindValue(variable->name);
    query.addBindValue(normalizedType);
    query.addBindValue(normalizedValue);
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);

    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    variable->id = query.lastInsertId().toLongLong();
    variable->valueType = normalizedType;
    variable->valueText = normalizedValue;
    variable->createdAt = QDateTime::fromString(timestamp, Qt::ISODate);
    variable->updatedAt = QDateTime::fromString(timestamp, Qt::ISODate);
    return true;
}

bool ProjectVariableService::upsertVariableByName(
    const qint64 projectId,
    const QString& name,
    const QString& valueType,
    const QString& rawValue,
    domain::ProjectVariable* savedVariable,
    QString* errorMessage
) const
{
    domain::ProjectVariable variable;
    variable.projectId = projectId;
    variable.name = name;
    variable.valueType = valueType;
    variable.valueText = rawValue;
    const bool success = saveVariable(&variable, errorMessage);
    if (success && savedVariable != nullptr) {
        *savedVariable = variable;
    }
    return success;
}

bool ProjectVariableService::deleteVariable(const qint64 variableId, QString* errorMessage) const
{
    if (variableId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zum Loeschen wird eine gueltige Variablen-ID benoetigt.";
        }
        return false;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare("DELETE FROM project_variables WHERE id = ?");
    query.addBindValue(variableId);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

bool ProjectVariableService::deleteVariableByName(
    const qint64 projectId,
    const QString& name,
    QString* errorMessage
) const
{
    if (projectId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projektvariablen brauchen eine gueltige Projekt-ID.";
        }
        return false;
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zum Loeschen wird ein Variablenname benoetigt.";
        }
        return false;
    }

    QSqlQuery query(m_databaseManager.database());
    query.prepare("DELETE FROM project_variables WHERE project_id = ? AND LOWER(name) = LOWER(?)");
    query.addBindValue(projectId);
    query.addBindValue(trimmedName);
    if (!query.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

} // namespace privateclaw::services
