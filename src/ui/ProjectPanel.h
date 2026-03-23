#pragma once

#include "domain/Project.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextEdit;

namespace privateclaw::providers {
class ProviderManager;
}

namespace privateclaw::services {
class ProjectService;
}

namespace privateclaw::ui {

class ProjectPanel : public QWidget
{
public:
    ProjectPanel(
        services::ProjectService& projectService,
        providers::ProviderManager& providerManager,
        QWidget* parent = nullptr
    );

    int projectCount() const;
    void setOnProjectDataChanged(std::function<void()> callback);

private:
    void buildUi();
    void refreshProjects(qint64 projectIdToSelect = -1);
    void refreshModelList();
    void loadProjectFromRow(int row);
    void saveProject();
    void deleteProject();
    void testOllamaConnection();
    void clearForm();
    int indexOfProject(qint64 projectId) const;
    QString formatProjectLabel(const domain::Project& project) const;

    services::ProjectService& m_projectService;
    providers::ProviderManager& m_providerManager;

    QList<domain::Project> m_projects;
    qint64 m_currentProjectId = -1;

    QListWidget* m_projectList = nullptr;
    QLabel* m_projectCountLabel = nullptr;
    QLabel* m_ollamaEndpointLabel = nullptr;
    QLabel* m_ollamaStatusLabel = nullptr;
    QLabel* m_formTitleLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QTextEdit* m_descriptionEdit = nullptr;
    QTextEdit* m_systemPromptEdit = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    std::function<void()> m_onProjectDataChanged;
};

} // namespace privateclaw::ui
