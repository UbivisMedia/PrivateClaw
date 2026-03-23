#include "ui/ProjectPanel.h"

#include "providers/ILlmProvider.h"
#include "providers/ProviderManager.h"
#include "services/ProjectService.h"

#include <QApplication>
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
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

#include <utility>

namespace privateclaw::ui {

ProjectPanel::ProjectPanel(
    services::ProjectService& projectService,
    providers::ProviderManager& providerManager,
    QWidget* parent
)
    : QWidget(parent)
    , m_projectService(projectService)
    , m_providerManager(providerManager)
{
    buildUi();
    refreshProjects();
}

int ProjectPanel::projectCount() const
{
    return m_projectList != nullptr ? m_projectList->count() : 0;
}

void ProjectPanel::setOnProjectDataChanged(std::function<void()> callback)
{
    m_onProjectDataChanged = std::move(callback);
}

void ProjectPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* providerCard = new QFrame(this);
    providerCard->setProperty("panelCard", true);

    auto* providerLayout = new QVBoxLayout(providerCard);
    auto* providerTitle = new QLabel("Ollama-Verbindung", providerCard);
    providerTitle->setProperty("sectionTitle", true);

    auto* providerBody = new QLabel(
        "Hier pruefen wir, ob der lokale Ollama-Endpoint erreichbar ist und Modelle liefert.",
        providerCard
    );
    providerBody->setProperty("sectionBody", true);
    providerBody->setWordWrap(true);

    m_ollamaEndpointLabel = new QLabel(providerCard);
    m_ollamaEndpointLabel->setProperty("sectionBody", true);

    m_ollamaStatusLabel = new QLabel("Status: Noch nicht getestet.", providerCard);
    m_ollamaStatusLabel->setProperty("sectionBody", true);
    m_ollamaStatusLabel->setWordWrap(true);

    auto* testButton = new QPushButton("Ollama testen", providerCard);
    auto* refreshModelsButton = new QPushButton("Modelle laden", providerCard);

    auto* providerButtonLayout = new QHBoxLayout();
    providerButtonLayout->addWidget(testButton);
    providerButtonLayout->addWidget(refreshModelsButton);
    providerButtonLayout->addStretch();

    providerLayout->addWidget(providerTitle);
    providerLayout->addWidget(providerBody);
    providerLayout->addWidget(m_ollamaEndpointLabel);
    providerLayout->addWidget(m_ollamaStatusLabel);
    providerLayout->addLayout(providerButtonLayout);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);

    auto* listCard = new QFrame(contentSplitter);
    listCard->setProperty("panelCard", true);
    auto* listLayout = new QVBoxLayout(listCard);
    auto* listTitle = new QLabel("Projekte", listCard);
    listTitle->setProperty("sectionTitle", true);

    auto* listBody = new QLabel(
        "Gespeicherte Projekte mit Standardmodell und letztem Bearbeitungsstand.",
        listCard
    );
    listBody->setProperty("sectionBody", true);
    listBody->setWordWrap(true);

    m_projectCountLabel = new QLabel("0 Projekte", listCard);
    m_projectCountLabel->setProperty("sectionBody", true);

    m_projectList = new QListWidget(listCard);
    m_projectList->setAlternatingRowColors(true);

    auto* refreshButton = new QPushButton("Liste aktualisieren", listCard);

    listLayout->addWidget(listTitle);
    listLayout->addWidget(listBody);
    listLayout->addWidget(m_projectCountLabel);
    listLayout->addWidget(m_projectList, 1);
    listLayout->addWidget(refreshButton, 0, Qt::AlignLeft);

    auto* formCard = new QFrame(contentSplitter);
    formCard->setProperty("panelCard", true);
    auto* formOuterLayout = new QVBoxLayout(formCard);
    m_formTitleLabel = new QLabel("Neues Projekt anlegen", formCard);
    m_formTitleLabel->setProperty("sectionTitle", true);

    auto* formBody = new QLabel(
        "Name, Standardmodell und Systemprompt werden direkt in SQLite gespeichert.",
        formCard
    );
    formBody->setProperty("sectionBody", true);
    formBody->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);

    m_nameEdit = new QLineEdit(formCard);
    m_nameEdit->setPlaceholderText("z. B. PrivateClaw MVP");

    m_modelCombo = new QComboBox(formCard);
    m_modelCombo->setEditable(true);
    m_modelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_modelCombo->setPlaceholderText("Ollama-Modell auswaehlen oder eintragen");

    m_descriptionEdit = new QTextEdit(formCard);
    m_descriptionEdit->setPlaceholderText("Kurzbeschreibung des Projekts");
    m_descriptionEdit->setMinimumHeight(90);

    m_systemPromptEdit = new QTextEdit(formCard);
    m_systemPromptEdit->setPlaceholderText("Optionaler Systemprompt fuer das Projekt");
    m_systemPromptEdit->setMinimumHeight(120);

    formLayout->addRow("Name", m_nameEdit);
    formLayout->addRow("Standardmodell", m_modelCombo);
    formLayout->addRow("Beschreibung", m_descriptionEdit);
    formLayout->addRow("Systemprompt", m_systemPromptEdit);

    auto* actionLayout = new QHBoxLayout();
    auto* saveButton = new QPushButton("Projekt speichern", formCard);
    auto* deleteButton = new QPushButton("Projekt loeschen", formCard);
    auto* clearButton = new QPushButton("Formular leeren", formCard);
    actionLayout->addWidget(saveButton);
    actionLayout->addWidget(deleteButton);
    actionLayout->addWidget(clearButton);
    actionLayout->addStretch();

    m_feedbackLabel = new QLabel(formCard);
    m_feedbackLabel->setProperty("sectionBody", true);
    m_feedbackLabel->setWordWrap(true);

    formOuterLayout->addWidget(m_formTitleLabel);
    formOuterLayout->addWidget(formBody);
    formOuterLayout->addLayout(formLayout);
    formOuterLayout->addLayout(actionLayout);
    formOuterLayout->addWidget(m_feedbackLabel);
    formOuterLayout->addStretch();

    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 4);

    layout->addWidget(providerCard);
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

    connect(clearButton, &QPushButton::clicked, this, [this]() {
        clearForm();
    });

    connect(testButton, &QPushButton::clicked, this, [this]() {
        testOllamaConnection();
    });

    connect(refreshModelsButton, &QPushButton::clicked, this, [this]() {
        refreshModelList();
    });

    connect(m_projectList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadProjectFromRow(row);
    });

    if (auto* ollamaProvider = m_providerManager.providerByName("Ollama")) {
        m_ollamaEndpointLabel->setText(QString("Endpoint: %1").arg(ollamaProvider->baseUrl()));
    } else {
        m_ollamaEndpointLabel->setText("Endpoint: Ollama-Provider ist nicht registriert.");
    }
}

void ProjectPanel::refreshProjects(const qint64 projectIdToSelect)
{
    const qint64 targetProjectId = projectIdToSelect > 0 ? projectIdToSelect : m_currentProjectId;
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

void ProjectPanel::refreshModelList()
{
    auto* provider = m_providerManager.providerByName("Ollama");
    if (provider == nullptr) {
        m_ollamaStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_ollamaStatusLabel->setText("Status: Ollama-Provider ist nicht registriert.");
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
        m_ollamaStatusLabel->setStyleSheet("color: #8b5e2f;");
        m_ollamaStatusLabel->setText(
            "Status: Keine Modelle geladen. Bitte Ollama pruefen oder Modellname manuell eintragen."
        );
        return;
    }

    m_ollamaStatusLabel->setStyleSheet("color: #2f6b3a;");
    m_ollamaStatusLabel->setText(
        QString("Status: %1 Modell(e) aus Ollama geladen.").arg(models.size())
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
    m_formTitleLabel->setText("Projekt bearbeiten");
    m_nameEdit->setText(project.name);
    m_modelCombo->setEditText(project.defaultModel);
    m_descriptionEdit->setPlainText(project.description);
    m_systemPromptEdit->setPlainText(project.systemPrompt);
}

void ProjectPanel::saveProject()
{
    domain::Project project;
    project.id = m_currentProjectId;
    project.name = m_nameEdit->text();
    project.defaultModel = m_modelCombo->currentText();
    project.description = m_descriptionEdit->toPlainText();
    project.systemPrompt = m_systemPromptEdit->toPlainText();

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

    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Projekt '%1' wurde geloescht.").arg(projectName));

    clearForm();
    refreshProjects();

    if (m_onProjectDataChanged) {
        m_onProjectDataChanged();
    }
}

void ProjectPanel::testOllamaConnection()
{
    auto* provider = m_providerManager.providerByName("Ollama");
    if (provider == nullptr) {
        m_ollamaStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_ollamaStatusLabel->setText("Status: Ollama-Provider ist nicht registriert.");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const providers::ProviderHealth health = provider->healthCheck();
    QApplication::restoreOverrideCursor();

    if (!health.success) {
        m_ollamaStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_ollamaStatusLabel->setText(QString("Status: Nicht erreichbar. %1").arg(health.message));
        return;
    }

    QString detail = QString("Status: %1").arg(health.message);
    if (!health.models.isEmpty()) {
        detail += QString(" Modelle: %1").arg(health.models.mid(0, 5).join(", "));
    }

    m_ollamaStatusLabel->setStyleSheet("color: #2f6b3a;");
    m_ollamaStatusLabel->setText(detail);
}

void ProjectPanel::clearForm()
{
    m_currentProjectId = -1;
    {
        const QSignalBlocker blocker(m_projectList);
        m_projectList->setCurrentRow(-1);
    }
    m_formTitleLabel->setText("Neues Projekt anlegen");
    m_nameEdit->clear();
    m_modelCombo->setCurrentIndex(-1);
    m_modelCombo->setEditText(QString());
    m_descriptionEdit->clear();
    m_systemPromptEdit->clear();
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
    if (project.defaultModel.trimmed().isEmpty()) {
        return project.name;
    }

    return QString("%1 [%2]").arg(project.name, project.defaultModel);
}

} // namespace privateclaw::ui
