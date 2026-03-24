#pragma once

#include "domain/Run.h"

#include <QList>

namespace privateclaw::storage {
class DatabaseManager;
}

namespace privateclaw::services {

class RunService
{
public:
    explicit RunService(storage::DatabaseManager& databaseManager);

    int runCount() const;
    QList<domain::Run> listRuns(qint64 projectId = -1, int limit = 200) const;
    bool startRun(domain::Run* run, QString* errorMessage = nullptr) const;
    bool finishRun(domain::Run* run, QString* errorMessage = nullptr) const;
    int recoverInterruptedRuns(QString* errorMessage = nullptr) const;

private:
    storage::DatabaseManager& m_databaseManager;
};

} // namespace privateclaw::services
