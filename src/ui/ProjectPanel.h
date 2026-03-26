#pragma once

#include "domain/Project.h"

#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextEdit;
class QToolButton;

namespace privateclaw::providers {
class ILlmProvider;
class ProviderManager;
}

namespace privateclaw::services {
class ProjectService;
class SecretsService;
class SettingsService;
}

namespace privateclaw::ui {

class ProjectPanel : public QWidget
{
public:
    ProjectPanel(
        services::ProjectService& projectService,
        services::SettingsService& settingsService,
        services::SecretsService& secretsService,
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
    void refreshProjectSecrets(const QString& secretToSelect = QString());
    void addAllowedToolPath();
    void removeSelectedAllowedToolPath();
    void loadSecretFromSelection();
    void saveProjectSecret();
    void deleteSelectedProjectSecret();
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
    services::SecretsService& m_secretsService;
    providers::ProviderManager& m_providerManager;

    QList<domain::Project> m_projects;
    qint64 m_currentProjectId = -1;

    QListWidget* m_projectList = nullptr;
    QListWidget* m_allowedPathList = nullptr;
    QListWidget* m_secretList = nullptr;
    QLabel* m_projectCountLabel = nullptr;
    QLabel* m_allowedPathInfoLabel = nullptr;
    QLabel* m_providerEndpointLabel = nullptr;
    QLabel* m_providerStatusLabel = nullptr;
    QToolButton* m_formTitleButton = nullptr;
    QLabel* m_secretInfoLabel = nullptr;
    QLineEdit* m_allowedPathEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_secretNameEdit = nullptr;
    QLineEdit* m_secretValueEdit = nullptr;
    QComboBox* m_providerCombo = nullptr;
    QLineEdit* m_providerBaseUrlEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QTextEdit* m_descriptionEdit = nullptr;
    QTextEdit* m_systemPromptEdit = nullptr;
    QCheckBox* m_confirmShellRunCheck = nullptr;
    QCheckBox* m_confirmFileEditDiffCheck = nullptr;
    QCheckBox* m_confirmHttpRequestCheck = nullptr;
    QCheckBox* m_allowUnattendedRiskyToolsCheck = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QString m_lastProviderName;
    std::function<void()> m_onProjectDataChanged;
};

} // namespace privateclaw::ui
