#pragma once

#include "domain/ProjectVariable.h"

#include <QList>
#include <QString>

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::services {

class ProjectVariableService
{
public:
    explicit ProjectVariableService(storage::DatabaseManager& databaseManager);

    static bool isValidVariableName(const QString& name);
    static QString normalizeValueType(const QString& valueType);
    static bool normalizeValueForType(
        const QString& valueType,
        const QString& rawValue,
        QString* normalizedValue,
        QString* errorMessage = nullptr
    );

    int variableCountForProject(qint64 projectId) const;
    QList<domain::ProjectVariable> listVariables(qint64 projectId) const;
    bool findVariableByName(
        qint64 projectId,
        const QString& name,
        domain::ProjectVariable* variable,
        QString* errorMessage = nullptr
    ) const;
    bool saveVariable(domain::ProjectVariable* variable, QString* errorMessage = nullptr) const;
    bool upsertVariableByName(
        qint64 projectId,
        const QString& name,
        const QString& valueType,
        const QString& rawValue,
        domain::ProjectVariable* savedVariable = nullptr,
        QString* errorMessage = nullptr
    ) const;
    bool deleteVariable(qint64 variableId, QString* errorMessage = nullptr) const;
    bool deleteVariableByName(qint64 projectId, const QString& name, QString* errorMessage = nullptr) const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services
