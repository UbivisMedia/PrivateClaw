#pragma once

#include "domain/Project.h"
#include "domain/ProjectVariable.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

namespace privateclaw::services {
class ProjectService;
class ProjectVariableService;
}

namespace privateclaw::ui {

class ProjectVariablePanel : public QWidget
{
public:
    ProjectVariablePanel(
        services::ProjectService& projectService,
        services::ProjectVariableService& projectVariableService,
        QWidget* parent = nullptr
    );

    void reloadData();
    void setOnVariableDataChanged(std::function<void()> callback);

private:
    void buildUi();
    void refreshData(qint64 variableIdToSelect = -1);
    void refreshProjects(qint64 selectedProjectId = -1);
    void refreshVariableList(qint64 variableIdToSelect = -1);
    void loadVariableFromRow(int row);
    void saveVariable();
    void deleteVariable();
    void clearEditor(bool keepFeedback = false);
    int indexOfProject(qint64 projectId) const;
    qint64 currentProjectId() const;
    QString formatVariableLabel(const domain::ProjectVariable& variable) const;

    services::ProjectService& m_projectService;
    services::ProjectVariableService& m_projectVariableService;

    QList<domain::Project> m_projects;
    QList<domain::ProjectVariable> m_variables;
    qint64 m_currentVariableId = -1;
    std::function<void()> m_onVariableDataChanged;

    QComboBox* m_projectCombo = nullptr;
    QLabel* m_variableCountLabel = nullptr;
    QListWidget* m_variableList = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QLabel* m_createdAtLabel = nullptr;
    QLabel* m_updatedAtLabel = nullptr;
    QPlainTextEdit* m_valueEdit = nullptr;
    QLabel* m_feedbackLabel = nullptr;
};

} // namespace privateclaw::ui
