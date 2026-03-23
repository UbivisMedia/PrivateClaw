#include "ui/WorkflowPanel.h"

#include "providers/ILlmProvider.h"
#include "providers/OllamaProvider.h"
#include "providers/ProviderManager.h"
#include "services/MemoryService.h"
#include "services/ProjectService.h"
#include "services/SettingsService.h"
#include "services/WorkflowService.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace privateclaw::ui {

namespace {

QString defaultWorkflowJson()
{
    return QString::fromUtf8(
        "{\n"
        "  \"version\": 1,\n"
        "  \"steps\": [\n"
        "    {\n"
        "      \"id\": \"draft_summary\",\n"
        "      \"type\": \"prompt\",\n"
        "      \"name\": \"Projektstatus entwerfen\",\n"
        "      \"config\": {\n"
        "        \"prompt\": \"Fasse den aktuellen Projektstand in drei Stichpunkten zusammen.\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n"
    );
}

QString prefixRunLog(const int runId, const QString& logText)
{
    const QStringList lines = logText.split('\n', Qt::SkipEmptyParts);
    QStringList prefixed;
    prefixed.reserve(lines.size());

    for (const QString& line : lines) {
        prefixed.append(QString("[run:%1] %2").arg(runId).arg(line));
    }

    return prefixed.join('\n');
}

QString formatMemorySnippet(const domain::MemoryEntry& entry)
{
    QStringList parts;
    parts.append(QString("Typ: %1").arg(entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed()));

    if (!entry.source.trimmed().isEmpty()) {
        parts.append(QString("Quelle: %1").arg(entry.source.trimmed()));
    }

    if (!entry.tags.isEmpty()) {
        parts.append(QString("Tags: %1").arg(entry.tags.join(", ")));
    }

    QString content = entry.content.simplified();
    if (content.size() > 260) {
        content = content.left(257) + "...";
    }
    parts.append(QString("Inhalt: %1").arg(content));

    return QString("- %1").arg(parts.join(" | "));
}

} // namespace

WorkflowPanel::WorkflowPanel(
    services::ProjectService& projectService,
    services::SettingsService& settingsService,
    services::MemoryService& memoryService,
    services::WorkflowService& workflowService,
    providers::ProviderManager& providerManager,
    QWidget* parent
)
    : QWidget(parent)
    , m_projectService(projectService)
    , m_settingsService(settingsService)
    , m_memoryService(memoryService)
    , m_workflowService(workflowService)
    , m_providerManager(providerManager)
{
    buildUi();
    refreshData();
}

int WorkflowPanel::workflowCount() const
{
    return m_workflowList != nullptr ? m_workflowList->count() : 0;
}

void WorkflowPanel::reloadData()
{
    refreshData(m_currentWorkflowId);
}

void WorkflowPanel::setOnWorkflowDataChanged(std::function<void()> callback)
{
    m_onWorkflowDataChanged = std::move(callback);
}

void WorkflowPanel::setOnExecutionLogChanged(std::function<void(const QString&)> callback)
{
    m_onExecutionLogChanged = std::move(callback);
}

void WorkflowPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* infoCard = new QFrame(this);
    infoCard->setProperty("panelCard", true);
    auto* infoLayout = new QVBoxLayout(infoCard);
    auto* infoTitle = new QLabel("Workflow-Editor", infoCard);
    infoTitle->setProperty("sectionTitle", true);

    auto* infoBody = new QLabel(
        "Workflows werden als JSON gespeichert. Erwartet wird ein Objekt mit einem Array 'steps'. "
        "Jeder Schritt braucht mindestens 'id' und 'type'. "
        "Aktuell werden 'prompt', 'save_memory' und 'decision' unterstuetzt.",
        infoCard
    );
    infoBody->setProperty("sectionBody", true);
    infoBody->setWordWrap(true);

    infoLayout->addWidget(infoTitle);
    infoLayout->addWidget(infoBody);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);

    auto* listCard = new QFrame(contentSplitter);
    listCard->setProperty("panelCard", true);
    auto* listLayout = new QVBoxLayout(listCard);
    auto* listTitle = new QLabel("Workflows", listCard);
    listTitle->setProperty("sectionTitle", true);

    auto* listBody = new QLabel(
        "Gespeicherte Workflow-Definitionen pro Projekt. Auswahl laedt den JSON-Inhalt in den Editor.",
        listCard
    );
    listBody->setProperty("sectionBody", true);
    listBody->setWordWrap(true);

    m_workflowCountLabel = new QLabel("0 Workflows", listCard);
    m_workflowCountLabel->setProperty("sectionBody", true);

    m_workflowList = new QListWidget(listCard);
    m_workflowList->setAlternatingRowColors(true);

    auto* listActions = new QHBoxLayout();
    auto* refreshButton = new QPushButton("Liste aktualisieren", listCard);
    auto* newButton = new QPushButton("Neuer Workflow", listCard);
    listActions->addWidget(refreshButton);
    listActions->addWidget(newButton);
    listActions->addStretch();

    listLayout->addWidget(listTitle);
    listLayout->addWidget(listBody);
    listLayout->addWidget(m_workflowCountLabel);
    listLayout->addWidget(m_workflowList, 1);
    listLayout->addLayout(listActions);

    auto* editorCard = new QFrame(contentSplitter);
    editorCard->setProperty("panelCard", true);
    auto* editorLayout = new QVBoxLayout(editorCard);
    auto* editorTitle = new QLabel("JSON-Definition bearbeiten", editorCard);
    editorTitle->setProperty("sectionTitle", true);

    auto* editorBody = new QLabel(
        "Projekt, Name und JSON werden gemeinsam gespeichert. Bereits vorhandene Eintraege koennen ueberschrieben werden.",
        editorCard
    );
    editorBody->setProperty("sectionBody", true);
    editorBody->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);

    m_projectCombo = new QComboBox(editorCard);
    m_nameEdit = new QLineEdit(editorCard);
    m_nameEdit->setPlaceholderText("z. B. Projektstatus zusammenfassen");

    m_descriptionEdit = new QTextEdit(editorCard);
    m_descriptionEdit->setMinimumHeight(90);
    m_descriptionEdit->setPlaceholderText("Kurzbeschreibung des Workflows");

    m_activeCheckBox = new QCheckBox("Workflow ist aktiv", editorCard);
    m_activeCheckBox->setChecked(true);

    formLayout->addRow("Projekt", m_projectCombo);
    formLayout->addRow("Name", m_nameEdit);
    formLayout->addRow("Beschreibung", m_descriptionEdit);
    formLayout->addRow("", m_activeCheckBox);

    auto* jsonLabel = new QLabel("JSON-Definition", editorCard);
    jsonLabel->setProperty("sectionBody", true);

    m_definitionEdit = new QPlainTextEdit(editorCard);
    m_definitionEdit->setMinimumHeight(320);
    m_definitionEdit->setPlainText(defaultWorkflowJson());

    auto* outputLabel = new QLabel("Letzte Ausfuehrung", editorCard);
    outputLabel->setProperty("sectionBody", true);

    m_executionOutputView = new QPlainTextEdit(editorCard);
    m_executionOutputView->setReadOnly(true);
    m_executionOutputView->setMinimumHeight(160);
    m_executionOutputView->setPlaceholderText("Hier erscheint die letzte Workflow-Ausgabe.");

    m_executionStatusLabel = new QLabel("Status: Bereit.", editorCard);
    m_executionStatusLabel->setProperty("sectionBody", true);
    m_executionStatusLabel->setWordWrap(true);

    auto* editorActions = new QHBoxLayout();
    auto* saveButton = new QPushButton("Workflow speichern", editorCard);
    auto* runButton = new QPushButton("Mit Ollama ausfuehren", editorCard);
    auto* resetButton = new QPushButton("Editor zuruecksetzen", editorCard);
    editorActions->addWidget(saveButton);
    editorActions->addWidget(runButton);
    editorActions->addWidget(resetButton);
    editorActions->addStretch();

    m_feedbackLabel = new QLabel(editorCard);
    m_feedbackLabel->setProperty("sectionBody", true);
    m_feedbackLabel->setWordWrap(true);

    editorLayout->addWidget(editorTitle);
    editorLayout->addWidget(editorBody);
    editorLayout->addLayout(formLayout);
    editorLayout->addWidget(jsonLabel);
    editorLayout->addWidget(m_definitionEdit, 1);
    editorLayout->addLayout(editorActions);
    editorLayout->addWidget(outputLabel);
    editorLayout->addWidget(m_executionStatusLabel);
    editorLayout->addWidget(m_executionOutputView);
    editorLayout->addWidget(m_feedbackLabel);

    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 5);

    layout->addWidget(infoCard);
    layout->addWidget(contentSplitter, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refreshData(m_currentWorkflowId);
    });

    connect(newButton, &QPushButton::clicked, this, [this]() {
        m_currentWorkflowId = -1;
        m_workflowList->setCurrentRow(-1);
        resetEditor();
    });

    connect(saveButton, &QPushButton::clicked, this, [this]() {
        saveWorkflow();
    });

    connect(runButton, &QPushButton::clicked, this, [this]() {
        executeWorkflow();
    });

    connect(resetButton, &QPushButton::clicked, this, [this]() {
        resetEditor();
    });

    connect(m_workflowList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadWorkflowFromRow(row);
    });

    m_executionStatusTimer = new QTimer(this);
    m_executionStatusTimer->setInterval(450);
    connect(m_executionStatusTimer, &QTimer::timeout, this, [this]() {
        updateExecutionStatus();
    });
}

void WorkflowPanel::refreshData(const qint64 workflowIdToSelect)
{
    const qint64 selectedProjectId = currentProjectId();
    refreshProjects(selectedProjectId);
    refreshWorkflowList(workflowIdToSelect);
}

void WorkflowPanel::refreshProjects(const qint64 selectedProjectId)
{
    m_projects = m_projectService.listProjects();

    const int targetProjectIndex = selectedProjectId > 0 ? indexOfProject(selectedProjectId) : 0;
    {
        const QSignalBlocker blocker(m_projectCombo);
        m_projectCombo->clear();
        for (const domain::Project& project : m_projects) {
            m_projectCombo->addItem(project.name, project.id);
        }
    }

    m_projectCombo->setEnabled(!m_projects.isEmpty());
    if (!m_projects.isEmpty()) {
        m_projectCombo->setCurrentIndex(targetProjectIndex >= 0 ? targetProjectIndex : 0);
    }
}

void WorkflowPanel::refreshWorkflowList(const qint64 workflowIdToSelect)
{
    const qint64 targetWorkflowId = workflowIdToSelect > 0 ? workflowIdToSelect : m_currentWorkflowId;
    m_workflows = m_workflowService.listWorkflows();

    int rowToSelect = -1;
    {
        const QSignalBlocker blocker(m_workflowList);
        m_workflowList->clear();

        for (int index = 0; index < m_workflows.size(); ++index) {
            const domain::Workflow& workflow = m_workflows.at(index);
            auto* item = new QListWidgetItem(formatWorkflowLabel(workflow), m_workflowList);
            item->setData(Qt::UserRole, workflow.id);
            item->setToolTip(workflow.description);

            if (workflow.id == targetWorkflowId) {
                rowToSelect = index;
            }
        }

        m_workflowCountLabel->setText(QString("%1 Workflows").arg(m_workflows.size()));

        if (rowToSelect < 0 && !m_workflows.isEmpty() && targetWorkflowId <= 0) {
            rowToSelect = 0;
        }

        if (rowToSelect >= 0) {
            m_workflowList->setCurrentRow(rowToSelect);
        } else {
            m_currentWorkflowId = -1;
            m_workflowList->setCurrentRow(-1);
        }
    }

    loadWorkflowFromRow(m_workflowList->currentRow());
}

void WorkflowPanel::loadWorkflowFromRow(const int row)
{
    if (row < 0 || row >= m_workflows.size()) {
        if (m_currentWorkflowId <= 0) {
            resetEditor(true);
        }
        return;
    }

    const domain::Workflow& workflow = m_workflows.at(row);
    m_currentWorkflowId = workflow.id;
    m_nameEdit->setText(workflow.name);
    m_descriptionEdit->setPlainText(workflow.description);
    m_definitionEdit->setPlainText(workflow.definitionJson.isEmpty() ? defaultWorkflowJson() : workflow.definitionJson);
    m_activeCheckBox->setChecked(workflow.active);

    const int projectIndex = indexOfProject(workflow.projectId);
    if (projectIndex >= 0) {
        m_projectCombo->setCurrentIndex(projectIndex);
    }
}

void WorkflowPanel::saveWorkflow()
{
    domain::Workflow workflow;
    workflow.id = m_currentWorkflowId;
    workflow.projectId = currentProjectId();
    workflow.name = m_nameEdit->text();
    workflow.description = m_descriptionEdit->toPlainText();
    workflow.definitionJson = m_definitionEdit->toPlainText();
    workflow.active = m_activeCheckBox->isChecked();

    QString errorMessage;
    if (!m_workflowService.saveWorkflow(&workflow, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Speichern fehlgeschlagen: %1").arg(errorMessage));
        return;
    }

    m_currentWorkflowId = workflow.id;
    m_definitionEdit->setPlainText(workflow.definitionJson);
    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(
        QString("Workflow '%1' wurde gespeichert. %2 Schritt(e) erkannt.")
            .arg(workflow.name, QString::number(workflow.steps.size()))
    );

    refreshProjects(workflow.projectId);
    refreshWorkflowList(workflow.id);

    if (m_onWorkflowDataChanged) {
        m_onWorkflowDataChanged();
    }
}

void WorkflowPanel::executeWorkflow()
{
    const domain::Project* project = currentProject();
    if (project == nullptr) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Zur Ausfuehrung muss zuerst ein Projekt ausgewaehlt werden.");
        publishExecutionLog("[ui] Keine Projektauswahl fuer die Workflow-Ausfuehrung vorhanden.");
        return;
    }

    auto* provider = m_providerManager.providerByName("Ollama");
    if (provider == nullptr) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Ollama-Provider ist nicht registriert.");
        publishExecutionLog("[ui] Ollama-Provider ist nicht registriert.");
        return;
    }

    const QString providerBaseUrl = provider->baseUrl();
    if (providerBaseUrl.trimmed().isEmpty()) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Ollama-URL ist nicht konfiguriert.");
        publishExecutionLog("[ui] Ollama-URL ist nicht konfiguriert.");
        return;
    }

    domain::Workflow workflow;
    workflow.id = m_currentWorkflowId;
    workflow.projectId = project->id;
    workflow.name = m_nameEdit->text();
    workflow.description = m_descriptionEdit->toPlainText();
    workflow.definitionJson = m_definitionEdit->toPlainText();
    workflow.active = m_activeCheckBox->isChecked();

    QString errorMessage;
    if (!m_workflowService.hydrateWorkflowDefinition(&workflow, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Ausfuehrung nicht moeglich: %1").arg(errorMessage));
        publishExecutionLog(QString("[ui] JSON-Validierung fehlgeschlagen: %1").arg(errorMessage));
        return;
    }

    core::RunContext runContext;
    runContext.projectId = project->id;
    runContext.workflowId = workflow.id;
    runContext.projectName = project->name;
    runContext.workflowName = workflow.name;
    runContext.selectedModel = project->defaultModel.trimmed().isEmpty()
        ? m_settingsService.defaultModel()
        : project->defaultModel.trimmed();
    runContext.systemPrompt = project->systemPrompt;
    runContext.variables.insert("project_name", project->name);
    runContext.variables.insert("project_description", project->description);
    runContext.variables.insert("workflow_name", workflow.name);

    const QList<domain::MemoryEntry> memoryEntries = m_memoryService.recentEntries(project->id, 6);
    for (const domain::MemoryEntry& entry : memoryEntries) {
        runContext.memorySnippets.append(formatMemorySnippet(entry));
    }
    runContext.variables.insert("project_memory", runContext.memorySnippets.join("\n"));
    runContext.variables.insert("project_memory_count", QString::number(runContext.memorySnippets.size()));

    const int runId = m_nextExecutionId++;
    ++m_activeRunCount;
    m_executionStatusFrame = 0;
    updateExecutionStatus();
    if (!m_executionStatusTimer->isActive()) {
        m_executionStatusTimer->start();
    }

    m_executionOutputView->setPlainText(
        QString("Lauf #%1 wurde gestartet. Die Ausgabe erscheint nach Abschluss hier.").arg(runId)
    );
    m_feedbackLabel->setStyleSheet("color: #5f5548;");
    m_feedbackLabel->setText(
        QString("Workflow-Lauf #%1 wurde im Hintergrund gestartet. Die UI bleibt nutzbar.").arg(runId)
    );
    publishExecutionLog(
        QString("[run:%1] Workflow '%2' wurde im Hintergrund gestartet.")
            .arg(runId)
            .arg(workflow.name)
    );
    publishExecutionLog(
        QString("[run:%1] %2 Memory-Eintraege als Projektkontext geladen.")
            .arg(runId)
            .arg(runContext.memorySnippets.size())
    );

    auto* watcher = new QFutureWatcher<core::ExecutionResult>(this);
    connect(watcher, &QFutureWatcher<core::ExecutionResult>::finished, this, [this, watcher, runId]() {
        const core::ExecutionResult result = watcher->result();
        watcher->deleteLater();

        m_activeRunCount = qMax(0, m_activeRunCount - 1);
        if (m_activeRunCount == 0) {
            m_executionStatusTimer->stop();
        }

        updateExecutionStatus();
        publishExecutionLog(prefixRunLog(runId, result.logs.join('\n')));
        m_executionOutputView->setPlainText(result.finalOutput);

        int savedMemoryCount = 0;
        bool memoryPersistenceFailed = false;
        QString memoryPersistenceError;
        for (domain::MemoryEntry entry : result.memoryEntriesToPersist) {
            QString saveError;
            if (!m_memoryService.saveEntry(&entry, &saveError)) {
                memoryPersistenceFailed = true;
                if (memoryPersistenceError.isEmpty()) {
                    memoryPersistenceError = saveError;
                }
                publishExecutionLog(
                    QString("[run:%1] Memory-Speichern fehlgeschlagen: %2")
                        .arg(runId)
                        .arg(saveError)
                );
                continue;
            }

            ++savedMemoryCount;
            QString preview = entry.content.simplified();
            if (preview.size() > 120) {
                preview = preview.left(117) + "...";
            }
            publishExecutionLog(
                QString("[run:%1] Memory-Eintrag gespeichert: %2")
                    .arg(runId)
                    .arg(preview)
            );
        }

        if (savedMemoryCount > 0 && m_onWorkflowDataChanged) {
            m_onWorkflowDataChanged();
        }

        if (memoryPersistenceFailed && result.success) {
            m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
            m_feedbackLabel->setText(
                QString("Workflow-Ausfuehrung #%1 abgeschlossen, aber Memory konnte nicht vollstaendig gespeichert werden: %2")
                    .arg(runId)
                    .arg(memoryPersistenceError)
            );
            return;
        }

        if (!result.success) {
            m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
            m_feedbackLabel->setText(
                QString("Workflow-Ausfuehrung #%1 fehlgeschlagen: %2").arg(runId).arg(result.errorMessage)
            );
            return;
        }

        m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
        m_feedbackLabel->setText(
            QString("Workflow-Ausfuehrung #%1 erfolgreich abgeschlossen. Letzte Ausgabezeichen: %2 | Memory: %3")
                .arg(runId)
                .arg(result.finalOutput.size())
                .arg(savedMemoryCount)
        );
    });

    watcher->setFuture(QtConcurrent::run([workflow, runContext, providerBaseUrl]() {
        providers::OllamaProvider provider(providerBaseUrl);
        core::WorkflowEngine workflowEngine;
        return workflowEngine.executeWorkflow(workflow, runContext, provider);
    }));
}

void WorkflowPanel::updateExecutionStatus()
{
    if (m_executionStatusLabel == nullptr) {
        return;
    }

    if (m_activeRunCount <= 0) {
        m_executionStatusLabel->setText("Status: Bereit.");
        return;
    }

    const QString dots((m_executionStatusFrame % 4) + 1, '.');
    ++m_executionStatusFrame;

    if (m_activeRunCount == 1) {
        m_executionStatusLabel->setText(QString("Status: 1 aktiver Lauf - Modell denkt%1").arg(dots));
        return;
    }

    m_executionStatusLabel->setText(
        QString("Status: %1 aktive Laeufe - Modelle denken%2").arg(m_activeRunCount).arg(dots)
    );
}

void WorkflowPanel::resetEditor(const bool keepFeedback)
{
    m_currentWorkflowId = -1;
    m_nameEdit->clear();
    m_descriptionEdit->clear();
    m_definitionEdit->setPlainText(defaultWorkflowJson());
    m_executionOutputView->clear();
    m_activeCheckBox->setChecked(true);

    if (!keepFeedback) {
        m_feedbackLabel->clear();
        m_feedbackLabel->setStyleSheet(QString());
    }
}

int WorkflowPanel::indexOfProject(const qint64 projectId) const
{
    for (int index = 0; index < m_projects.size(); ++index) {
        if (m_projects.at(index).id == projectId) {
            return index;
        }
    }

    return -1;
}

qint64 WorkflowPanel::currentProjectId() const
{
    return m_projectCombo->currentData().toLongLong();
}

const domain::Project* WorkflowPanel::currentProject() const
{
    const qint64 projectId = currentProjectId();
    for (const domain::Project& project : m_projects) {
        if (project.id == projectId) {
            return &project;
        }
    }

    return nullptr;
}

QString WorkflowPanel::projectNameForId(const qint64 projectId) const
{
    for (const domain::Project& project : m_projects) {
        if (project.id == projectId) {
            return project.name;
        }
    }

    return "Unbekanntes Projekt";
}

QString WorkflowPanel::formatWorkflowLabel(const domain::Workflow& workflow) const
{
    return QString("%1 [%2] - %3 Schritt(e)")
        .arg(workflow.name, projectNameForId(workflow.projectId), QString::number(workflow.steps.size()));
}

void WorkflowPanel::publishExecutionLog(const QString& text) const
{
    if (m_onExecutionLogChanged) {
        m_onExecutionLogChanged(text);
    }
}

} // namespace privateclaw::ui
