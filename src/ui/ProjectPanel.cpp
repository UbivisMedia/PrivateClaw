#include "ui/ProjectPanel.h"

#include "ui/CollapsibleCard.h"
#include "providers/ILlmProvider.h"
#include "providers/LmStudioProvider.h"
#include "providers/OllamaProvider.h"
#include "providers/ProviderManager.h"
#include "services/ProjectService.h"
#include "services/SecretsService.h"
#include "services/SettingsService.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

#include <utility>

namespace privateclaw::ui {

ProjectPanel::ProjectPanel(
    services::ProjectService& projectService,
    services::SettingsService& settingsService,
    services::SecretsService& secretsService,
    providers::ProviderManager& providerManager,
    QWidget* parent
)
    : QWidget(parent)
    , m_projectService(projectService)
    , m_settingsService(settingsService)
    , m_secretsService(secretsService)
    , m_providerManager(providerManager)
{
    buildUi();
    refreshProjects();
    refreshAllowedToolPaths();
    refreshProjectSecrets();
}

int ProjectPanel::projectCount() const
{
    return m_projectList != nullptr ? m_projectList->count() : 0;
}

void ProjectPanel::reloadData()
{
    refreshProjects(m_currentProjectId);
    refreshAllowedToolPaths();
}

void ProjectPanel::setOnProjectDataChanged(std::function<void()> callback)
{
    m_onProjectDataChanged = std::move(callback);
}

void ProjectPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    const CollapsibleCardParts providerCard = createCollapsibleCard(this, "Provider-Verbindung", true);
    auto* providerLayout = providerCard.bodyLayout;
    auto* providerBody = new QLabel(
        "Hier pruefen wir den aktuell gewaehlten Provider und laden dessen verfuegbare Modelle.",
        providerCard.bodyFrame
    );
    providerBody->setProperty("sectionBody", true);
    providerBody->setWordWrap(true);

    m_providerEndpointLabel = new QLabel(providerCard.bodyFrame);
    m_providerEndpointLabel->setProperty("sectionBody", true);

    m_providerStatusLabel = new QLabel("Status: Noch nicht getestet.", providerCard.bodyFrame);
    m_providerStatusLabel->setProperty("sectionBody", true);
    m_providerStatusLabel->setWordWrap(true);

    auto* testButton = new QPushButton("Provider testen", providerCard.bodyFrame);
    auto* refreshModelsButton = new QPushButton("Modelle laden", providerCard.bodyFrame);

    auto* providerButtonLayout = new QHBoxLayout();
    providerButtonLayout->addWidget(testButton);
    providerButtonLayout->addWidget(refreshModelsButton);
    providerButtonLayout->addStretch();

    providerLayout->addWidget(providerBody);
    providerLayout->addWidget(m_providerEndpointLabel);
    providerLayout->addWidget(m_providerStatusLabel);
    providerLayout->addLayout(providerButtonLayout);

    const CollapsibleCardParts allowedPathsCard = createCollapsibleCard(this, "Erlaubte Dateipfade", true);
    auto* allowedPathsLayout = allowedPathsCard.bodyLayout;
    auto* allowedPathsBody = new QLabel(
        "Datei-Tools duerfen ausserhalb des Workspace nur auf hier freigegebene Pfade zugreifen. "
        "Der Workspace selbst bleibt immer erlaubt.",
        allowedPathsCard.bodyFrame
    );
    allowedPathsBody->setProperty("sectionBody", true);
    allowedPathsBody->setWordWrap(true);

    m_allowedPathInfoLabel = new QLabel(allowedPathsCard.bodyFrame);
    m_allowedPathInfoLabel->setProperty("sectionBody", true);
    m_allowedPathInfoLabel->setWordWrap(true);

    m_allowedPathList = new QListWidget(allowedPathsCard.bodyFrame);
    m_allowedPathList->setAlternatingRowColors(true);

    auto* allowedPathInputLayout = new QHBoxLayout();
    m_allowedPathEdit = new QLineEdit(allowedPathsCard.bodyFrame);
    m_allowedPathEdit->setPlaceholderText("z. B. D:/ComfyUI/output");
    auto* addAllowedPathButton = new QPushButton("Pfad hinzufuegen", allowedPathsCard.bodyFrame);
    auto* removeAllowedPathButton = new QPushButton("Auswahl entfernen", allowedPathsCard.bodyFrame);
    auto* refreshAllowedPathsButton = new QPushButton("Liste aktualisieren", allowedPathsCard.bodyFrame);
    allowedPathInputLayout->addWidget(m_allowedPathEdit, 1);
    allowedPathInputLayout->addWidget(addAllowedPathButton);
    allowedPathInputLayout->addWidget(removeAllowedPathButton);
    allowedPathInputLayout->addWidget(refreshAllowedPathsButton);

    allowedPathsLayout->addWidget(allowedPathsBody);
    allowedPathsLayout->addWidget(m_allowedPathInfoLabel);
    allowedPathsLayout->addWidget(m_allowedPathList);
    allowedPathsLayout->addLayout(allowedPathInputLayout);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);

    const CollapsibleCardParts listCard = createCollapsibleCard(contentSplitter, "Projekte", true);
    auto* listLayout = listCard.bodyLayout;
    auto* listBody = new QLabel(
        "Gespeicherte Projekte mit Standardmodell und letztem Bearbeitungsstand.",
        listCard.bodyFrame
    );
    listBody->setProperty("sectionBody", true);
    listBody->setWordWrap(true);

    m_projectCountLabel = new QLabel("0 Projekte", listCard.bodyFrame);
    m_projectCountLabel->setProperty("sectionBody", true);

    m_projectList = new QListWidget(listCard.bodyFrame);
    m_projectList->setAlternatingRowColors(true);

    auto* refreshButton = new QPushButton("Liste aktualisieren", listCard.bodyFrame);

    listLayout->addWidget(listBody);
    listLayout->addWidget(m_projectCountLabel);
    listLayout->addWidget(m_projectList, 1);
    listLayout->addWidget(refreshButton, 0, Qt::AlignLeft);

    auto* formScrollArea = new QScrollArea(contentSplitter);
    formScrollArea->setWidgetResizable(true);
    formScrollArea->setFrameShape(QFrame::NoFrame);
    formScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    const CollapsibleCardParts formCard = createCollapsibleCard(formScrollArea, "Neues Projekt anlegen", true);
    auto* formOuterLayout = formCard.bodyLayout;
    formOuterLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_formTitleButton = formCard.toggleButton;

    auto* formBody = new QLabel(
        "Name, Provider, Standardmodell und Systemprompt werden direkt in SQLite gespeichert.",
        formCard.bodyFrame
    );
    formBody->setProperty("sectionBody", true);
    formBody->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);

    m_nameEdit = new QLineEdit(formCard.bodyFrame);
    m_nameEdit->setPlaceholderText("z. B. PrivateClaw MVP");

    m_providerCombo = new QComboBox(formCard.bodyFrame);

    m_providerBaseUrlEdit = new QLineEdit(formCard.bodyFrame);
    m_providerBaseUrlEdit->setPlaceholderText("http://127.0.0.1:11434");

    m_modelCombo = new QComboBox(formCard.bodyFrame);
    m_modelCombo->setEditable(true);
    m_modelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_modelCombo->setPlaceholderText("Modell des gewaehlten Providers auswaehlen oder eintragen");

    m_descriptionEdit = new QTextEdit(formCard.bodyFrame);
    m_descriptionEdit->setPlaceholderText("Kurzbeschreibung des Projekts");
    m_descriptionEdit->setMinimumHeight(90);

    m_systemPromptEdit = new QTextEdit(formCard.bodyFrame);
    m_systemPromptEdit->setPlaceholderText("Optionaler Systemprompt fuer das Projekt");
    m_systemPromptEdit->setMinimumHeight(120);

    auto* policyWidget = new QWidget(formCard.bodyFrame);
    auto* policyLayout = new QVBoxLayout(policyWidget);
    policyLayout->setContentsMargins(0, 0, 0, 0);
    policyLayout->setSpacing(6);

    m_confirmShellRunCheck = new QCheckBox("shell.run nur nach manueller Bestaetigung erlauben", policyWidget);
    m_confirmFileEditDiffCheck = new QCheckBox(
        "file.edit_diff nur nach manueller Bestaetigung erlauben",
        policyWidget
    );
    m_confirmHttpRequestCheck = new QCheckBox(
        "http.request nur nach manueller Bestaetigung erlauben",
        policyWidget
    );
    m_allowUnattendedRiskyToolsCheck = new QCheckBox(
        "Riskante Tools fuer automatische Zeitplaene ohne Rueckfrage erlauben",
        policyWidget
    );

    m_confirmShellRunCheck->setChecked(true);
    m_confirmFileEditDiffCheck->setChecked(true);
    m_confirmHttpRequestCheck->setChecked(true);
    m_allowUnattendedRiskyToolsCheck->setChecked(false);

    policyLayout->addWidget(m_confirmShellRunCheck);
    policyLayout->addWidget(m_confirmFileEditDiffCheck);
    policyLayout->addWidget(m_confirmHttpRequestCheck);
    policyLayout->addWidget(m_allowUnattendedRiskyToolsCheck);

    formLayout->addRow("Name", m_nameEdit);
    formLayout->addRow("Provider", m_providerCombo);
    formLayout->addRow("Provider-URL", m_providerBaseUrlEdit);
    formLayout->addRow("Standardmodell", m_modelCombo);
    formLayout->addRow("Beschreibung", m_descriptionEdit);
    formLayout->addRow("Systemprompt", m_systemPromptEdit);
    formLayout->addRow("Sicherheitsrichtlinien", policyWidget);

    auto* actionLayout = new QHBoxLayout();
    auto* saveButton = new QPushButton("Projekt speichern", formCard.bodyFrame);
    auto* deleteButton = new QPushButton("Projekt loeschen", formCard.bodyFrame);
    auto* clearButton = new QPushButton("Formular leeren", formCard.bodyFrame);
    actionLayout->addWidget(saveButton);
    actionLayout->addWidget(deleteButton);
    actionLayout->addWidget(clearButton);
    actionLayout->addStretch();

    auto* secretBody = new QLabel(
        "API-Keys und andere Zugangsdaten werden lokal verschluesselt gespeichert. "
        "Im Workflow kannst du sie als {{secret.name}} verwenden.",
        formCard.bodyFrame
    );
    secretBody->setProperty("sectionBody", true);
    secretBody->setWordWrap(true);

    m_secretInfoLabel = new QLabel(formCard.bodyFrame);
    m_secretInfoLabel->setProperty("sectionBody", true);
    m_secretInfoLabel->setWordWrap(true);

    m_secretList = new QListWidget(formCard.bodyFrame);
    m_secretList->setAlternatingRowColors(true);
    m_secretList->setMinimumHeight(120);

    auto* secretInputLayout = new QHBoxLayout();
    m_secretNameEdit = new QLineEdit(formCard.bodyFrame);
    m_secretNameEdit->setPlaceholderText("z. B. comfy_api_key");
    m_secretValueEdit = new QLineEdit(formCard.bodyFrame);
    m_secretValueEdit->setEchoMode(QLineEdit::Password);
    m_secretValueEdit->setPlaceholderText("Secret-Wert eingeben oder zum Ueberschreiben neu setzen");
    secretInputLayout->addWidget(m_secretNameEdit, 1);
    secretInputLayout->addWidget(m_secretValueEdit, 1);

    auto* secretButtonLayout = new QHBoxLayout();
    auto* saveSecretButton = new QPushButton("Secret speichern", formCard.bodyFrame);
    auto* deleteSecretButton = new QPushButton("Secret loeschen", formCard.bodyFrame);
    auto* clearSecretButton = new QPushButton("Secret leeren", formCard.bodyFrame);
    secretButtonLayout->addWidget(saveSecretButton);
    secretButtonLayout->addWidget(deleteSecretButton);
    secretButtonLayout->addWidget(clearSecretButton);
    secretButtonLayout->addStretch();

    m_feedbackLabel = new QLabel(formCard.bodyFrame);
    m_feedbackLabel->setProperty("sectionBody", true);
    m_feedbackLabel->setWordWrap(true);

    formOuterLayout->addWidget(formBody);

    const CollapsibleCardParts projectDetailsCard = createCollapsibleCard(formCard.bodyFrame, "Projektdetails", true);
    projectDetailsCard.bodyLayout->addLayout(formLayout);
    projectDetailsCard.bodyLayout->addLayout(actionLayout);

    const CollapsibleCardParts projectSecretsCard = createCollapsibleCard(formCard.bodyFrame, "Projekt-Secrets", true);
    projectSecretsCard.bodyLayout->addWidget(secretBody);
    projectSecretsCard.bodyLayout->addWidget(m_secretInfoLabel);
    projectSecretsCard.bodyLayout->addWidget(m_secretList);
    projectSecretsCard.bodyLayout->addLayout(secretInputLayout);
    projectSecretsCard.bodyLayout->addLayout(secretButtonLayout);

    formOuterLayout->addWidget(projectDetailsCard.frame);
    formOuterLayout->addWidget(projectSecretsCard.frame);
    formOuterLayout->addWidget(m_feedbackLabel);
    formOuterLayout->addStretch();
    formScrollArea->setWidget(formCard.frame);

    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 4);

    layout->addWidget(providerCard.frame);
    layout->addWidget(allowedPathsCard.frame);
    layout->addWidget(contentSplitter, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refreshProjects();
    });

    connect(saveButton, &QPushButton::clicked, this, [this]() {
        saveProject();
    });

    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        deleteProject();
    });

    connect(addAllowedPathButton, &QPushButton::clicked, this, [this]() {
        addAllowedToolPath();
    });

    connect(removeAllowedPathButton, &QPushButton::clicked, this, [this]() {
        removeSelectedAllowedToolPath();
    });

    connect(m_allowedPathEdit, &QLineEdit::returnPressed, this, [this]() {
        addAllowedToolPath();
    });

    connect(refreshAllowedPathsButton, &QPushButton::clicked, this, [this]() {
        refreshAllowedToolPaths();
    });

    connect(clearButton, &QPushButton::clicked, this, [this]() {
        clearForm();
    });

    connect(testButton, &QPushButton::clicked, this, [this]() {
        testSelectedProviderConnection();
    });

    connect(refreshModelsButton, &QPushButton::clicked, this, [this]() {
        refreshModelList();
    });

    connect(saveSecretButton, &QPushButton::clicked, this, [this]() {
        saveProjectSecret();
    });

    connect(deleteSecretButton, &QPushButton::clicked, this, [this]() {
        deleteSelectedProjectSecret();
    });

    connect(clearSecretButton, &QPushButton::clicked, this, [this]() {
        if (m_secretList != nullptr) {
            m_secretList->setCurrentRow(-1);
        }
        if (m_secretNameEdit != nullptr) {
            m_secretNameEdit->clear();
        }
        if (m_secretValueEdit != nullptr) {
            m_secretValueEdit->clear();
        }
    });

    connect(m_secretValueEdit, &QLineEdit::returnPressed, this, [this]() {
        saveProjectSecret();
    });

    connect(m_secretList, &QListWidget::currentRowChanged, this, [this](const int) {
        loadSecretFromSelection();
    });

    connect(m_providerCombo, &QComboBox::currentTextChanged, this, [this]() {
        const QString previousProviderName = m_lastProviderName.trimmed();
        const QString currentProviderNameValue = currentProviderName();
        const QString previousDefaultBaseUrl = defaultBaseUrlForProvider(previousProviderName);
        const QString currentBaseUrl = m_providerBaseUrlEdit != nullptr
            ? m_providerBaseUrlEdit->text().trimmed()
            : QString();

        if (m_providerBaseUrlEdit != nullptr
            && (currentBaseUrl.isEmpty()
                || (!previousDefaultBaseUrl.isEmpty() && currentBaseUrl == previousDefaultBaseUrl))) {
            m_providerBaseUrlEdit->setText(defaultBaseUrlForProvider(currentProviderNameValue));
        }

        m_lastProviderName = currentProviderNameValue;
        updateSelectedProviderUi();
    });

    connect(m_providerBaseUrlEdit, &QLineEdit::textChanged, this, [this]() {
        updateSelectedProviderUi();
    });

    connect(m_projectList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadProjectFromRow(row);
    });

    populateProviderChoices();
    updateSelectedProviderUi();
}

void ProjectPanel::refreshProjects(const qint64 projectIdToSelect)
{
    const qint64 targetProjectId = projectIdToSelect > 0 ? projectIdToSelect : m_currentProjectId;
    populateProviderChoices(currentProviderName());
    m_projects = m_projectService.listProjects();

    int rowToSelect = indexOfProject(targetProjectId);
    {
        const QSignalBlocker blocker(m_projectList);
        m_projectList->clear();

        for (int index = 0; index < m_projects.size(); ++index) {
            const domain::Project& project = m_projects.at(index);
            auto* item = new QListWidgetItem(formatProjectLabel(project), m_projectList);
            item->setData(Qt::UserRole, project.id);
            item->setToolTip(project.description);
        }

        if (rowToSelect >= 0) {
            m_projectList->setCurrentRow(rowToSelect);
        } else {
            m_currentProjectId = -1;
            m_projectList->setCurrentRow(-1);
        }
    }

    m_projectCountLabel->setText(QString("%1 Projekte").arg(m_projects.size()));
    loadProjectFromRow(m_projectList->currentRow());
}

void ProjectPanel::populateProviderChoices(const QString& providerToSelect)
{
    const QString desiredProvider = providerToSelect.trimmed();
    QString fallbackProvider = desiredProvider;
    if (fallbackProvider.isEmpty()) {
        fallbackProvider = currentProviderName().trimmed();
    }
    if (fallbackProvider.isEmpty()) {
        fallbackProvider = "Ollama";
    }

    {
        const QSignalBlocker blocker(m_providerCombo);
        m_providerCombo->clear();
        for (providers::ILlmProvider* provider : m_providerManager.providers()) {
            if (provider != nullptr) {
                m_providerCombo->addItem(provider->name());
            }
        }
    }

    if (m_providerCombo->count() == 0) {
        m_providerCombo->setEnabled(false);
        return;
    }

    m_providerCombo->setEnabled(true);
    const int targetIndex = m_providerCombo->findText(fallbackProvider);
    if (targetIndex >= 0) {
        m_providerCombo->setCurrentIndex(targetIndex);
    } else {
        m_providerCombo->setCurrentIndex(0);
    }
}

void ProjectPanel::refreshModelList()
{
    const QString providerName = currentProviderName();
    std::unique_ptr<providers::ILlmProvider> provider = buildProvider(providerName, currentProviderBaseUrl());
    if (provider == nullptr) {
        m_providerStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_providerStatusLabel->setText(
            QString("Status: Provider '%1' ist nicht registriert.").arg(providerName)
        );
        return;
    }

    const QString currentModel = m_modelCombo->currentText().trimmed();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QStringList models = provider->listModels();
    QApplication::restoreOverrideCursor();

    const QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();
    m_modelCombo->addItems(models);

    if (!currentModel.isEmpty()) {
        m_modelCombo->setEditText(currentModel);
    } else if (!models.isEmpty()) {
        m_modelCombo->setCurrentIndex(0);
    }

    if (models.isEmpty()) {
        m_providerStatusLabel->setStyleSheet("color: #8b5e2f;");
        m_providerStatusLabel->setText(
            QString(
                "Status: Keine Modelle aus %1 geladen. Bitte Provider pruefen oder Modellname manuell eintragen."
            ).arg(provider->name())
        );
        return;
    }

    m_providerStatusLabel->setStyleSheet("color: #2f6b3a;");
    m_providerStatusLabel->setText(
        QString("Status: %1 Modell(e) aus %2 geladen.").arg(models.size()).arg(provider->name())
    );
}

void ProjectPanel::loadProjectFromRow(const int row)
{
    if (row < 0 || row >= m_projects.size()) {
        if (m_currentProjectId <= 0) {
            clearForm();
        }
        return;
    }

    const domain::Project& project = m_projects.at(row);
    m_currentProjectId = project.id;
    m_formTitleButton->setText("Projekt bearbeiten");
    m_nameEdit->setText(project.name);
    populateProviderChoices(project.providerName);
    const QString providerBaseUrl = project.providerBaseUrl.trimmed().isEmpty()
        ? defaultBaseUrlForProvider(project.providerName)
        : project.providerBaseUrl.trimmed();
    if (m_providerBaseUrlEdit != nullptr) {
        m_providerBaseUrlEdit->setText(providerBaseUrl);
    }
    m_lastProviderName = currentProviderName();
    updateSelectedProviderUi();
    m_modelCombo->setEditText(project.defaultModel);
    m_descriptionEdit->setPlainText(project.description);
    m_systemPromptEdit->setPlainText(project.systemPrompt);
    if (m_confirmShellRunCheck != nullptr) {
        m_confirmShellRunCheck->setChecked(project.confirmShellRun);
    }
    if (m_confirmFileEditDiffCheck != nullptr) {
        m_confirmFileEditDiffCheck->setChecked(project.confirmFileEditDiff);
    }
    if (m_confirmHttpRequestCheck != nullptr) {
        m_confirmHttpRequestCheck->setChecked(project.confirmHttpRequest);
    }
    if (m_allowUnattendedRiskyToolsCheck != nullptr) {
        m_allowUnattendedRiskyToolsCheck->setChecked(project.allowUnattendedRiskyTools);
    }
    refreshProjectSecrets();
}

void ProjectPanel::saveProject()
{
    domain::Project project;
    project.id = m_currentProjectId;
    project.name = m_nameEdit->text();
    project.providerName = currentProviderName();
    project.providerBaseUrl = currentProviderBaseUrl();
    project.defaultModel = m_modelCombo->currentText();
    project.description = m_descriptionEdit->toPlainText();
    project.systemPrompt = m_systemPromptEdit->toPlainText();
    project.confirmShellRun = m_confirmShellRunCheck != nullptr && m_confirmShellRunCheck->isChecked();
    project.confirmFileEditDiff = m_confirmFileEditDiffCheck != nullptr && m_confirmFileEditDiffCheck->isChecked();
    project.confirmHttpRequest = m_confirmHttpRequestCheck != nullptr && m_confirmHttpRequestCheck->isChecked();
    project.allowUnattendedRiskyTools =
        m_allowUnattendedRiskyToolsCheck != nullptr && m_allowUnattendedRiskyToolsCheck->isChecked();

    QString errorMessage;
    const bool isUpdate = project.id > 0;
    const bool success = isUpdate
        ? m_projectService.updateProject(project, &errorMessage)
        : m_projectService.createProject(&project, &errorMessage);

    if (!success) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Speichern fehlgeschlagen: %1").arg(errorMessage));
        return;
    }

    m_currentProjectId = project.id;
    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(
        QString("Projekt '%1' wurde %2.")
            .arg(project.name.trimmed(), isUpdate ? "aktualisiert" : "gespeichert")
    );

    refreshProjects(project.id);
    refreshProjectSecrets();

    if (m_onProjectDataChanged) {
        m_onProjectDataChanged();
    }
}

void ProjectPanel::deleteProject()
{
    if (m_currentProjectId <= 0) {
        m_feedbackLabel->setStyleSheet("color: #8b5e2f;");
        m_feedbackLabel->setText("Zum Loeschen bitte zuerst ein Projekt aus der Liste auswaehlen.");
        return;
    }

    const QString projectName = m_nameEdit->text().trimmed();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        "Projekt loeschen",
        QString(
            "Projekt '%1' wirklich loeschen? Zugehoerige Workflows, Runs, Erinnerungen und Zeitplaene werden ebenfalls entfernt."
        ).arg(projectName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (answer != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_projectService.deleteProject(m_currentProjectId, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Loeschen fehlgeschlagen: %1").arg(errorMessage));
        return;
    }

    QString secretCleanupError;
    const bool secretsRemoved = m_secretsService.deleteSecretsForProject(m_currentProjectId, &secretCleanupError);
    m_feedbackLabel->setStyleSheet(secretsRemoved ? "color: #2f6b3a;" : "color: #8b5e2f;");
    m_feedbackLabel->setText(
        secretsRemoved
            ? QString("Projekt '%1' wurde geloescht.").arg(projectName)
            : QString(
                  "Projekt '%1' wurde geloescht, aber die zugehoerigen Secrets konnten nicht vollstaendig entfernt werden: %2"
              )
                  .arg(projectName)
                  .arg(secretCleanupError)
    );

    clearForm();
    refreshProjects();

    if (m_onProjectDataChanged) {
        m_onProjectDataChanged();
    }
}

void ProjectPanel::refreshAllowedToolPaths(const QString& pathToSelect)
{
    if (m_allowedPathList == nullptr || m_allowedPathInfoLabel == nullptr) {
        return;
    }

    const QString workspaceRoot = m_settingsService.workspaceRoot().trimmed();
    const QStringList allowedPaths = m_settingsService.customAllowedToolPaths();
    m_allowedPathInfoLabel->setText(
        QString("Workspace (immer erlaubt): %1 | Zusaetzliche Freigaben: %2")
            .arg(workspaceRoot.isEmpty() ? "-" : workspaceRoot)
            .arg(allowedPaths.size())
    );

    int rowToSelect = -1;
    {
        const QSignalBlocker blocker(m_allowedPathList);
        m_allowedPathList->clear();
        for (int index = 0; index < allowedPaths.size(); ++index) {
            auto* item = new QListWidgetItem(allowedPaths.at(index), m_allowedPathList);
            item->setData(Qt::UserRole, allowedPaths.at(index));
            if (!pathToSelect.trimmed().isEmpty()
                && allowedPaths.at(index).compare(pathToSelect.trimmed(), Qt::CaseInsensitive) == 0) {
                rowToSelect = index;
            }
        }
    }

    m_allowedPathList->setCurrentRow(rowToSelect);
}

void ProjectPanel::refreshProjectSecrets(const QString& secretToSelect)
{
    if (m_secretList == nullptr || m_secretInfoLabel == nullptr) {
        return;
    }

    int rowToSelect = -1;
    QStringList secretNames;
    if (m_currentProjectId > 0) {
        secretNames = m_secretsService.listSecretNames(m_currentProjectId);
    }

    {
        const QSignalBlocker blocker(m_secretList);
        m_secretList->clear();
        for (int index = 0; index < secretNames.size(); ++index) {
            auto* item = new QListWidgetItem(secretNames.at(index), m_secretList);
            item->setData(Qt::UserRole, secretNames.at(index));
            item->setToolTip(QString("Workflow-Platzhalter: {{secret.%1}}").arg(secretNames.at(index)));
            if (!secretToSelect.trimmed().isEmpty()
                && secretNames.at(index).compare(secretToSelect.trimmed(), Qt::CaseInsensitive) == 0) {
                rowToSelect = index;
            }
        }
    }

    if (m_currentProjectId <= 0) {
        m_secretInfoLabel->setText("Secrets koennen gepflegt werden, sobald das Projekt einmal gespeichert wurde.");
    } else {
        m_secretInfoLabel->setText(
            QString("Gespeicherte Secrets: %1 | Platzhalterformat: {{secret.name}}").arg(secretNames.size())
        );
    }

    m_secretList->setCurrentRow(rowToSelect);
}

void ProjectPanel::addAllowedToolPath()
{
    if (m_allowedPathEdit == nullptr) {
        return;
    }

    const QString requestedPath = m_allowedPathEdit->text().trimmed();
    if (requestedPath.isEmpty()) {
        m_feedbackLabel->setStyleSheet("color: #8b5e2f;");
        m_feedbackLabel->setText("Bitte zuerst einen Pfad fuer die Allowlist eintragen.");
        return;
    }

    const QString suggestedPath = m_settingsService.suggestedAllowedToolPath(requestedPath, false);
    QString errorMessage;
    if (!m_settingsService.addAllowedToolPath(suggestedPath, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Allowlist konnte nicht erweitert werden: %1").arg(errorMessage));
        return;
    }

    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Pfad freigegeben: %1").arg(suggestedPath));
    m_allowedPathEdit->clear();
    refreshAllowedToolPaths(suggestedPath);

    if (m_onProjectDataChanged) {
        m_onProjectDataChanged();
    }
}

void ProjectPanel::removeSelectedAllowedToolPath()
{
    if (m_allowedPathList == nullptr || m_allowedPathList->currentItem() == nullptr) {
        m_feedbackLabel->setStyleSheet("color: #8b5e2f;");
        m_feedbackLabel->setText("Zum Entfernen bitte zuerst einen freigegebenen Pfad auswaehlen.");
        return;
    }

    const QString selectedPath = m_allowedPathList->currentItem()->data(Qt::UserRole).toString();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        "Dateipfad entfernen",
        QString(
            "Den freigegebenen Pfad '%1' wirklich aus der Allowlist entfernen? "
            "Datei-Tools koennen ihn danach nur noch nutzen, wenn er erneut freigegeben wird."
        ).arg(selectedPath),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (answer != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_settingsService.removeAllowedToolPath(selectedPath, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Allowlist konnte nicht aktualisiert werden: %1").arg(errorMessage));
        return;
    }

    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Pfad aus der Allowlist entfernt: %1").arg(selectedPath));
    refreshAllowedToolPaths();

    if (m_onProjectDataChanged) {
        m_onProjectDataChanged();
    }
}

void ProjectPanel::loadSecretFromSelection()
{
    if (m_secretList == nullptr || m_secretNameEdit == nullptr || m_secretValueEdit == nullptr) {
        return;
    }

    if (m_secretList->currentItem() == nullptr) {
        m_secretNameEdit->clear();
        m_secretValueEdit->clear();
        return;
    }

    const QString secretName = m_secretList->currentItem()->data(Qt::UserRole).toString();
    m_secretNameEdit->setText(secretName);
    m_secretValueEdit->clear();
}

void ProjectPanel::saveProjectSecret()
{
    if (m_currentProjectId <= 0) {
        m_feedbackLabel->setStyleSheet("color: #8b5e2f;");
        m_feedbackLabel->setText("Secrets koennen erst nach dem ersten Speichern des Projekts gepflegt werden.");
        return;
    }

    if (m_secretNameEdit == nullptr || m_secretValueEdit == nullptr) {
        return;
    }

    const QString secretName = m_secretNameEdit->text().trimmed();
    const QString secretValue = m_secretValueEdit->text();
    QString errorMessage;
    if (!m_secretsService.saveSecret(m_currentProjectId, secretName, secretValue, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Secret konnte nicht gespeichert werden: %1").arg(errorMessage));
        return;
    }

    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(
        QString("Secret '%1' wurde sicher gespeichert und steht als {{secret.%1}} zur Verfuegung.").arg(secretName)
    );
    m_secretValueEdit->clear();
    refreshProjectSecrets(secretName);
}

void ProjectPanel::deleteSelectedProjectSecret()
{
    if (m_currentProjectId <= 0 || m_secretList == nullptr || m_secretList->currentItem() == nullptr) {
        m_feedbackLabel->setStyleSheet("color: #8b5e2f;");
        m_feedbackLabel->setText("Zum Entfernen bitte zuerst ein Projekt und ein Secret auswaehlen.");
        return;
    }

    const QString secretName = m_secretList->currentItem()->data(Qt::UserRole).toString();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        "Secret loeschen",
        QString("Secret '%1' wirklich entfernen? Danach ist {{secret.%1}} nicht mehr verfuegbar.").arg(secretName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_secretsService.deleteSecret(m_currentProjectId, secretName, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Secret konnte nicht entfernt werden: %1").arg(errorMessage));
        return;
    }

    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Secret '%1' wurde entfernt.").arg(secretName));
    if (m_secretNameEdit != nullptr) {
        m_secretNameEdit->clear();
    }
    if (m_secretValueEdit != nullptr) {
        m_secretValueEdit->clear();
    }
    refreshProjectSecrets();
}

void ProjectPanel::testSelectedProviderConnection()
{
    const QString providerName = currentProviderName();
    std::unique_ptr<providers::ILlmProvider> provider = buildProvider(providerName, currentProviderBaseUrl());
    if (provider == nullptr) {
        m_providerStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_providerStatusLabel->setText(
            QString("Status: Provider '%1' ist nicht registriert.").arg(providerName)
        );
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const providers::ProviderHealth health = provider->healthCheck();
    QApplication::restoreOverrideCursor();

    if (!health.success) {
        m_providerStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_providerStatusLabel->setText(
            QString("Status: %1 ist nicht erreichbar. %2").arg(provider->name(), health.message)
        );
        return;
    }

    QString detail = QString("Status: %1").arg(health.message);
    if (!health.models.isEmpty()) {
        detail += QString(" Modelle: %1").arg(health.models.mid(0, 5).join(", "));
    }

    m_providerStatusLabel->setStyleSheet("color: #2f6b3a;");
    m_providerStatusLabel->setText(detail);
}

void ProjectPanel::updateSelectedProviderUi(const bool resetStatusMessage)
{
    const QString providerName = currentProviderName().trimmed();
    const QString providerBaseUrl = currentProviderBaseUrl();
    std::unique_ptr<providers::ILlmProvider> provider = buildProvider(providerName, providerBaseUrl);

    if (provider == nullptr) {
        m_providerEndpointLabel->setText(
            QString("Endpoint: Provider '%1' ist nicht registriert.").arg(providerName.isEmpty() ? "Unbekannt" : providerName)
        );
        if (resetStatusMessage) {
            m_providerStatusLabel->setStyleSheet("color: #8b2f2f;");
            m_providerStatusLabel->setText("Status: Provider ist nicht verfuegbar.");
        }
        return;
    }

    const QString endpoint = providerBaseUrl.trimmed().isEmpty() ? "nicht konfiguriert" : providerBaseUrl.trimmed();
    m_providerEndpointLabel->setText(QString("Endpoint (%1): %2").arg(provider->name(), endpoint));
    if (m_providerBaseUrlEdit != nullptr) {
        m_providerBaseUrlEdit->setPlaceholderText(defaultBaseUrlForProvider(provider->name()));
    }
    m_modelCombo->setPlaceholderText(QString("%1-Modell auswaehlen oder eintragen").arg(provider->name()));

    const QString currentModel = m_modelCombo->currentText();
    {
        const QSignalBlocker blocker(m_modelCombo);
        m_modelCombo->clear();
        if (!currentModel.trimmed().isEmpty()) {
            m_modelCombo->setEditText(currentModel);
        }
    }

    if (!resetStatusMessage) {
        return;
    }

    if (!provider->isConfigured()) {
        m_providerStatusLabel->setStyleSheet("color: #8b5e2f;");
        m_providerStatusLabel->setText(
            QString("Status: %1 ist ausgewaehlt, aber noch nicht konfiguriert.").arg(provider->name())
        );
        return;
    }

    m_providerStatusLabel->setStyleSheet(QString());
    m_providerStatusLabel->setText(
        QString("Status: %1 ist ausgewaehlt. Verbindung kann jetzt getestet werden.").arg(provider->name())
    );
}

QString ProjectPanel::currentProviderName() const
{
    return m_providerCombo != nullptr ? m_providerCombo->currentText().trimmed() : QString();
}

QString ProjectPanel::currentProviderBaseUrl() const
{
    const QString providerName = currentProviderName();
    const QString baseUrl = m_providerBaseUrlEdit != nullptr ? m_providerBaseUrlEdit->text().trimmed() : QString();
    return baseUrl.isEmpty() ? defaultBaseUrlForProvider(providerName) : baseUrl;
}

QString ProjectPanel::defaultBaseUrlForProvider(const QString& providerName) const
{
    if (providerName.trimmed().isEmpty()) {
        return QString();
    }

    if (providers::ILlmProvider* provider = m_providerManager.providerByName(providerName.trimmed()); provider != nullptr) {
        return provider->baseUrl().trimmed();
    }

    return QString();
}

std::unique_ptr<providers::ILlmProvider> ProjectPanel::buildProvider(
    const QString& providerName,
    const QString& baseUrl
) const
{
    if (providerName.compare("Ollama", Qt::CaseInsensitive) == 0) {
        return std::make_unique<providers::OllamaProvider>(baseUrl);
    }

    if (providerName.compare("LM Studio", Qt::CaseInsensitive) == 0) {
        return std::make_unique<providers::LmStudioProvider>(baseUrl);
    }

    return nullptr;
}

void ProjectPanel::clearForm()
{
    m_currentProjectId = -1;
    {
        const QSignalBlocker blocker(m_projectList);
        m_projectList->setCurrentRow(-1);
    }
    m_formTitleButton->setText("Neues Projekt anlegen");
    m_nameEdit->clear();
    populateProviderChoices("Ollama");
    if (m_providerBaseUrlEdit != nullptr) {
        m_providerBaseUrlEdit->setText(defaultBaseUrlForProvider("Ollama"));
    }
    m_lastProviderName = "Ollama";
    m_modelCombo->setCurrentIndex(-1);
    m_modelCombo->setEditText(QString());
    m_descriptionEdit->clear();
    m_systemPromptEdit->clear();
    if (m_confirmShellRunCheck != nullptr) {
        m_confirmShellRunCheck->setChecked(true);
    }
    if (m_confirmFileEditDiffCheck != nullptr) {
        m_confirmFileEditDiffCheck->setChecked(true);
    }
    if (m_confirmHttpRequestCheck != nullptr) {
        m_confirmHttpRequestCheck->setChecked(true);
    }
    if (m_allowUnattendedRiskyToolsCheck != nullptr) {
        m_allowUnattendedRiskyToolsCheck->setChecked(false);
    }
    if (m_secretNameEdit != nullptr) {
        m_secretNameEdit->clear();
    }
    if (m_secretValueEdit != nullptr) {
        m_secretValueEdit->clear();
    }
    refreshProjectSecrets();
    updateSelectedProviderUi();
}

int ProjectPanel::indexOfProject(const qint64 projectId) const
{
    for (int index = 0; index < m_projects.size(); ++index) {
        if (m_projects.at(index).id == projectId) {
            return index;
        }
    }

    return -1;
}

QString ProjectPanel::formatProjectLabel(const domain::Project& project) const
{
    const QString providerName = project.providerName.trimmed().isEmpty()
        ? "Ollama"
        : project.providerName.trimmed();

    if (project.defaultModel.trimmed().isEmpty()) {
        return QString("%1 [%2]").arg(project.name, providerName);
    }

    return QString("%1 [%2 | %3]").arg(project.name, providerName, project.defaultModel);
}

} // namespace privateclaw::ui
