#pragma once

#include "domain/Project.h"

#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextEdit;

namespace privateclaw::providers {
class ILlmProvider;
class ProviderManager;
}

namespace privateclaw::services {
class ProjectService;
class SettingsService;
}

namespace privateclaw::ui {

class ProjectPanel : public QWidget
{
public:
    ProjectPanel(
        services::ProjectService& projectService,
        services::SettingsService& settingsService,
        providers::ProviderManager& providerManager,
        QWidget* parent = nullptr
    );

    int projectCount() const;
    void reloadData();
    void setOnProjectDataChanged(std::function<void()> callback);

private:
    void buildUi();
    void refreshProjects(qint64 projectIdToSelect = -1);
    void populateProviderChoices(const QString& providerToSelect = QString());
    void refreshModelList();
    void loadProjectFromRow(int row);
    void saveProject();
    void deleteProject();
    void refreshAllowedToolPaths(const QString& pathToSelect = QString());
    void addAllowedToolPath();
    void removeSelectedAllowedToolPath();
    void testSelectedProviderConnection();
    void updateSelectedProviderUi(bool resetStatusMessage = true);
    QString currentProviderName() const;
    QString currentProviderBaseUrl() const;
    QString defaultBaseUrlForProvider(const QString& providerName) const;
    std::unique_ptr<providers::ILlmProvider> buildProvider(
        const QString& providerName,
        const QString& baseUrl
    ) const;
    void clearForm();
    int indexOfProject(qint64 projectId) const;
    QString formatProjectLabel(const domain::Project& project) const;

    services::ProjectService& m_projectService;
    services::SettingsService& m_settingsService;
    providers::ProviderManager& m_providerManager;

    QList<domain::Project> m_projects;
    qint64 m_currentProjectId = -1;

    QListWidget* m_projectList = nullptr;
    QListWidget* m_allowedPathList = nullptr;
    QLabel* m_projectCountLabel = nullptr;
    QLabel* m_allowedPathInfoLabel = nullptr;
    QLabel* m_providerEndpointLabel = nullptr;
    QLabel* m_providerStatusLabel = nullptr;
    QLabel* m_formTitleLabel = nullptr;
    QLineEdit* m_allowedPathEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_providerCombo = nullptr;
    QLineEdit* m_providerBaseUrlEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QTextEdit* m_descriptionEdit = nullptr;
    QTextEdit* m_systemPromptEdit = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QString m_lastProviderName;
    std::function<void()> m_onProjectDataChanged;
};

} // namespace privateclaw::ui
