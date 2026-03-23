#include "ui/WorkflowPanel.h"

#include "providers/ILlmProvider.h"
#include "providers/LmStudioProvider.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
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

QString normalizedStepType(QString type)
{
    return type.trimmed().toLower();
}

void setJsonTextValue(QJsonObject* object, const QString& key, const QString& value)
{
    if (object == nullptr) {
        return;
    }

    const QString normalizedValue = value.trimmed();
    if (normalizedValue.isEmpty()) {
        object->remove(key);
        return;
    }

    object->insert(key, normalizedValue);
}

void removeConfigKeys(QJsonObject* config, const QStringList& keys)
{
    if (config == nullptr) {
        return;
    }

    for (const QString& key : keys) {
        config->remove(key);
    }
}

QJsonObject defaultStepObject(const QString& stepType, const QString& stepId)
{
    QJsonObject step;
    step.insert("id", stepId);
    step.insert("type", stepType);

    QJsonObject config;
    if (stepType == "save_memory") {
        step.insert("name", "Memory speichern");
        config.insert("content", "{{last_response}}");
        config.insert("entry_type", "note");
        config.insert("relevance", 50);
    } else if (stepType == "decision") {
        step.insert("name", "Entscheidung treffen");
        config.insert("input", "{{last_response}}");
        config.insert("operator", "contains");
        config.insert("value", ",");
        config.insert("true_result", "true");
        config.insert("false_result", "false");
    } else {
        step.insert("name", "Prompt ausfuehren");
        config.insert("prompt", "Beschreibe hier die Aufgabe.");
    }

    step.insert("config", config);
    return step;
}

core::ExecutionResult executeWorkflowWithProvider(
    const QString& providerName,
    const QString& providerBaseUrl,
    const domain::Workflow& workflow,
    const core::RunContext& runContext
)
{
    core::WorkflowEngine workflowEngine;

    if (providerName.compare("Ollama", Qt::CaseInsensitive) == 0) {
        providers::OllamaProvider provider(providerBaseUrl);
        return workflowEngine.executeWorkflow(workflow, runContext, provider);
    }

    if (providerName.compare("LM Studio", Qt::CaseInsensitive) == 0) {
        providers::LmStudioProvider provider(providerBaseUrl);
        return workflowEngine.executeWorkflow(workflow, runContext, provider);
    }

    core::ExecutionResult result;
    result.errorMessage = QString("Provider '%1' wird aktuell nicht unterstuetzt.").arg(providerName);
    result.logs.append(QString("[engine] Abbruch: %1").arg(result.errorMessage));
    return result;
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
        "Aktuell werden 'prompt', 'save_memory' und 'decision' unterstuetzt. "
        "Optional hilft ein visueller Editor beim Bearbeiten und haelt das JSON live synchron.",
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

    m_visualEditorToggle = new QCheckBox("Visuellen Editor anzeigen", editorCard);
    m_visualEditorToggle->setChecked(true);

    m_visualEditorFrame = new QFrame(editorCard);
    auto* visualEditorLayout = new QVBoxLayout(m_visualEditorFrame);
    visualEditorLayout->setContentsMargins(0, 0, 0, 0);

    auto* visualEditorTitle = new QLabel("Visueller Editor (optional)", m_visualEditorFrame);
    visualEditorTitle->setProperty("sectionBody", true);

    auto* visualEditorBody = new QLabel(
        "Schritte lassen sich hier ohne JSON direkt bearbeiten. Aenderungen im JSON werden nach kurzer Zeit hier sichtbar und umgekehrt.",
        m_visualEditorFrame
    );
    visualEditorBody->setProperty("sectionBody", true);
    visualEditorBody->setWordWrap(true);

    auto* visualSplitter = new QSplitter(Qt::Horizontal, m_visualEditorFrame);

    auto* visualListCard = new QFrame(visualSplitter);
    auto* visualListLayout = new QVBoxLayout(visualListCard);
    visualListLayout->setContentsMargins(0, 0, 0, 0);
    auto* visualListTitle = new QLabel("Schritte", visualListCard);
    visualListTitle->setProperty("sectionBody", true);

    m_visualStepList = new QListWidget(visualListCard);
    m_visualStepList->setAlternatingRowColors(true);

    auto* visualAddLayout = new QHBoxLayout();
    auto* addPromptButton = new QPushButton("Prompt", visualListCard);
    auto* addMemoryButton = new QPushButton("Memory", visualListCard);
    auto* addDecisionButton = new QPushButton("Decision", visualListCard);
    visualAddLayout->addWidget(addPromptButton);
    visualAddLayout->addWidget(addMemoryButton);
    visualAddLayout->addWidget(addDecisionButton);

    auto* visualMoveLayout = new QHBoxLayout();
    auto* moveUpButton = new QPushButton("Hoch", visualListCard);
    auto* moveDownButton = new QPushButton("Runter", visualListCard);
    auto* removeStepButton = new QPushButton("Loeschen", visualListCard);
    visualMoveLayout->addWidget(moveUpButton);
    visualMoveLayout->addWidget(moveDownButton);
    visualMoveLayout->addWidget(removeStepButton);

    visualListLayout->addWidget(visualListTitle);
    visualListLayout->addWidget(m_visualStepList, 1);
    visualListLayout->addLayout(visualAddLayout);
    visualListLayout->addLayout(visualMoveLayout);

    auto* visualDetailCard = new QFrame(visualSplitter);
    auto* visualDetailLayout = new QVBoxLayout(visualDetailCard);
    visualDetailLayout->setContentsMargins(0, 0, 0, 0);

    auto* visualFormLayout = new QFormLayout();
    visualFormLayout->setLabelAlignment(Qt::AlignLeft);

    m_visualStepIdEdit = new QLineEdit(visualDetailCard);
    m_visualStepTypeCombo = new QComboBox(visualDetailCard);
    m_visualStepTypeCombo->addItem("prompt");
    m_visualStepTypeCombo->addItem("save_memory");
    m_visualStepTypeCombo->addItem("decision");
    m_visualStepNameEdit = new QLineEdit(visualDetailCard);

    visualFormLayout->addRow("Schritt-ID", m_visualStepIdEdit);
    visualFormLayout->addRow("Typ", m_visualStepTypeCombo);
    visualFormLayout->addRow("Name", m_visualStepNameEdit);

    m_visualStepConfigStack = new QStackedWidget(visualDetailCard);

    auto* promptPage = new QWidget(m_visualStepConfigStack);
    auto* promptLayout = new QFormLayout(promptPage);
    promptLayout->setLabelAlignment(Qt::AlignLeft);
    m_promptTextEdit = new QTextEdit(promptPage);
    m_promptTextEdit->setMinimumHeight(120);
    m_promptSystemPromptEdit = new QTextEdit(promptPage);
    m_promptSystemPromptEdit->setMinimumHeight(90);
    m_promptOutputEdit = new QLineEdit(promptPage);
    m_promptModelEdit = new QLineEdit(promptPage);
    promptLayout->addRow("Prompt", m_promptTextEdit);
    promptLayout->addRow("Systemprompt", m_promptSystemPromptEdit);
    promptLayout->addRow("Output-Variable", m_promptOutputEdit);
    promptLayout->addRow("Modell-Override", m_promptModelEdit);
    m_visualStepConfigStack->addWidget(promptPage);

    auto* memoryPage = new QWidget(m_visualStepConfigStack);
    auto* memoryLayout = new QFormLayout(memoryPage);
    memoryLayout->setLabelAlignment(Qt::AlignLeft);
    m_memoryContentEdit = new QTextEdit(memoryPage);
    m_memoryContentEdit->setMinimumHeight(110);
    m_memoryTypeEdit = new QLineEdit(memoryPage);
    m_memorySourceEdit = new QLineEdit(memoryPage);
    m_memoryTagsEdit = new QLineEdit(memoryPage);
    m_memoryRelevanceSpin = new QSpinBox(memoryPage);
    m_memoryRelevanceSpin->setRange(0, 100);
    m_memoryRelevanceSpin->setValue(50);
    memoryLayout->addRow("Inhalt", m_memoryContentEdit);
    memoryLayout->addRow("Memory-Typ", m_memoryTypeEdit);
    memoryLayout->addRow("Quelle", m_memorySourceEdit);
    memoryLayout->addRow("Tags", m_memoryTagsEdit);
    memoryLayout->addRow("Relevanz", m_memoryRelevanceSpin);
    m_visualStepConfigStack->addWidget(memoryPage);

    auto* decisionPage = new QWidget(m_visualStepConfigStack);
    auto* decisionLayout = new QFormLayout(decisionPage);
    decisionLayout->setLabelAlignment(Qt::AlignLeft);
    m_decisionAdvancedLabel = new QLabel(
        "Hinweis: Decision-Schritte mit 'rules' werden hier nur gelesen. Fuer komplexe Regeln bitte direkt das JSON bearbeiten.",
        decisionPage
    );
    m_decisionAdvancedLabel->setProperty("sectionBody", true);
    m_decisionAdvancedLabel->setWordWrap(true);
    m_decisionInputEdit = new QLineEdit(decisionPage);
    m_decisionOperatorCombo = new QComboBox(decisionPage);
    m_decisionOperatorCombo->addItems(QStringList{
        "equals",
        "not_equals",
        "contains",
        "not_contains",
        "starts_with",
        "ends_with",
        "empty",
        "not_empty",
        "regex"
    });
    m_decisionValueEdit = new QLineEdit(decisionPage);
    m_decisionOutputEdit = new QLineEdit(decisionPage);
    m_decisionTrueResultEdit = new QLineEdit(decisionPage);
    m_decisionFalseResultEdit = new QLineEdit(decisionPage);
    m_decisionIfTrueEdit = new QLineEdit(decisionPage);
    m_decisionIfFalseEdit = new QLineEdit(decisionPage);
    m_decisionCaseSensitiveCheckBox = new QCheckBox("Gross-/Kleinschreibung beachten", decisionPage);
    decisionLayout->addRow("", m_decisionAdvancedLabel);
    decisionLayout->addRow("Input", m_decisionInputEdit);
    decisionLayout->addRow("Operator", m_decisionOperatorCombo);
    decisionLayout->addRow("Vergleichswert", m_decisionValueEdit);
    decisionLayout->addRow("Output-Variable", m_decisionOutputEdit);
    decisionLayout->addRow("True-Result", m_decisionTrueResultEdit);
    decisionLayout->addRow("False-Result", m_decisionFalseResultEdit);
    decisionLayout->addRow("if_true", m_decisionIfTrueEdit);
    decisionLayout->addRow("if_false", m_decisionIfFalseEdit);
    decisionLayout->addRow("", m_decisionCaseSensitiveCheckBox);
    m_visualStepConfigStack->addWidget(decisionPage);

    m_visualEditorStatusLabel = new QLabel("Status: Visueller Editor bereit.", visualDetailCard);
    m_visualEditorStatusLabel->setProperty("sectionBody", true);
    m_visualEditorStatusLabel->setWordWrap(true);

    visualDetailLayout->addLayout(visualFormLayout);
    visualDetailLayout->addWidget(m_visualStepConfigStack, 1);
    visualDetailLayout->addWidget(m_visualEditorStatusLabel);

    visualSplitter->setStretchFactor(0, 2);
    visualSplitter->setStretchFactor(1, 3);

    visualEditorLayout->addWidget(visualEditorTitle);
    visualEditorLayout->addWidget(visualEditorBody);
    visualEditorLayout->addWidget(visualSplitter, 1);

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
    auto* runButton = new QPushButton("Workflow ausfuehren", editorCard);
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
    editorLayout->addWidget(m_visualEditorToggle);
    editorLayout->addWidget(m_visualEditorFrame);
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

    connect(m_visualEditorToggle, &QCheckBox::toggled, this, [this]() {
        updateVisualEditorVisibility();
    });

    connect(m_visualStepList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadVisualStepFromRow(row);
    });

    connect(addPromptButton, &QPushButton::clicked, this, [this]() {
        addVisualStep("prompt");
    });

    connect(addMemoryButton, &QPushButton::clicked, this, [this]() {
        addVisualStep("save_memory");
    });

    connect(addDecisionButton, &QPushButton::clicked, this, [this]() {
        addVisualStep("decision");
    });

    connect(removeStepButton, &QPushButton::clicked, this, [this]() {
        removeSelectedVisualStep();
    });

    connect(moveUpButton, &QPushButton::clicked, this, [this]() {
        moveSelectedVisualStep(-1);
    });

    connect(moveDownButton, &QPushButton::clicked, this, [this]() {
        moveSelectedVisualStep(1);
    });

    connect(m_definitionEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualSyncFromJson();
    });

    connect(m_visualStepIdEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_visualStepNameEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_visualStepTypeCombo, &QComboBox::currentTextChanged, this, [this]() {
        updateVisualConfigPage();
        scheduleVisualStepApply();
    });
    connect(m_promptTextEdit, &QTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_promptSystemPromptEdit, &QTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_promptOutputEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_promptModelEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_memoryContentEdit, &QTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_memoryTypeEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_memorySourceEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_memoryTagsEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_memoryRelevanceSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_decisionInputEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionOperatorCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionValueEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionOutputEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionTrueResultEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionFalseResultEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionIfTrueEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionIfFalseEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_decisionCaseSensitiveCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });

    m_executionStatusTimer = new QTimer(this);
    m_executionStatusTimer->setInterval(450);
    connect(m_executionStatusTimer, &QTimer::timeout, this, [this]() {
        updateExecutionStatus();
    });

    m_visualSyncTimer = new QTimer(this);
    m_visualSyncTimer->setInterval(250);
    m_visualSyncTimer->setSingleShot(true);
    connect(m_visualSyncTimer, &QTimer::timeout, this, [this]() {
        syncVisualEditorFromJson();
    });

    m_visualApplyTimer = new QTimer(this);
    m_visualApplyTimer->setInterval(120);
    m_visualApplyTimer->setSingleShot(true);
    connect(m_visualApplyTimer, &QTimer::timeout, this, [this]() {
        applyVisualStepChanges();
    });

    updateVisualEditorVisibility();
    syncVisualEditorFromJson();
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

    syncVisualEditorFromJson();
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
    syncVisualEditorFromJson();
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

    const QString providerName = project->providerName.trimmed().isEmpty()
        ? "Ollama"
        : project->providerName.trimmed();
    auto* provider = m_providerManager.providerByName(providerName);
    if (provider == nullptr) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Provider '%1' ist nicht registriert.").arg(providerName));
        publishExecutionLog(QString("[ui] Provider '%1' ist nicht registriert.").arg(providerName));
        return;
    }

    const QString providerBaseUrl = provider->baseUrl();
    if (providerBaseUrl.trimmed().isEmpty()) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Provider-URL fuer '%1' ist nicht konfiguriert.").arg(provider->name()));
        publishExecutionLog(QString("[ui] Provider-URL fuer '%1' ist nicht konfiguriert.").arg(provider->name()));
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
        QString("[run:%1] Workflow '%2' wurde im Hintergrund ueber Provider '%3' gestartet.")
            .arg(runId)
            .arg(workflow.name)
            .arg(provider->name())
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

    watcher->setFuture(QtConcurrent::run([workflow, runContext, providerBaseUrl, providerName]() {
        return executeWorkflowWithProvider(providerName, providerBaseUrl, workflow, runContext);
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

void WorkflowPanel::updateVisualEditorVisibility()
{
    if (m_visualEditorFrame == nullptr || m_visualEditorToggle == nullptr) {
        return;
    }

    m_visualEditorFrame->setVisible(m_visualEditorToggle->isChecked());
}

void WorkflowPanel::scheduleVisualSyncFromJson()
{
    if (m_isSyncingVisualEditor || m_visualSyncTimer == nullptr) {
        return;
    }

    m_visualSyncTimer->start();
}

void WorkflowPanel::syncVisualEditorFromJson()
{
    if (m_definitionEdit == nullptr || m_visualStepList == nullptr) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(m_definitionEdit->toPlainText().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        m_visualEditorHasValidJson = false;
        m_visualEditorStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_visualEditorStatusLabel->setText(
            QString("Status: JSON aktuell nicht lesbar. %1").arg(parseError.errorString())
        );
        m_visualStepList->clear();
        clearVisualStepEditor();
        return;
    }

    if (!json.isObject()) {
        m_visualEditorHasValidJson = false;
        m_visualEditorStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_visualEditorStatusLabel->setText("Status: Der visuelle Editor erwartet ein JSON-Objekt.");
        m_visualStepList->clear();
        clearVisualStepEditor();
        return;
    }

    const QJsonObject root = json.object();
    if (!root.value("steps").isArray()) {
        m_visualEditorHasValidJson = false;
        m_visualEditorStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_visualEditorStatusLabel->setText("Status: Im JSON fehlt ein Array 'steps'.");
        m_visualStepList->clear();
        clearVisualStepEditor();
        return;
    }

    const QString stepIdToSelect = selectedVisualStepId();
    m_visualDefinitionRoot = root;
    m_visualEditorHasValidJson = true;
    m_visualEditorStatusLabel->setStyleSheet("color: #5f5548;");
    m_visualEditorStatusLabel->setText(
        QString("Status: %1 Schritt(e) aus dem JSON synchronisiert.")
            .arg(root.value("steps").toArray().size())
    );
    rebuildVisualStepList(stepIdToSelect);
}

void WorkflowPanel::rebuildVisualStepList(const QString& stepIdToSelect)
{
    if (m_visualStepList == nullptr) {
        return;
    }

    const QJsonArray steps = m_visualDefinitionRoot.value("steps").toArray();
    int rowToSelect = -1;

    {
        const QSignalBlocker blocker(m_visualStepList);
        m_visualStepList->clear();
        for (int index = 0; index < steps.size(); ++index) {
            const QJsonObject stepObject = steps.at(index).toObject();
            auto* item = new QListWidgetItem(visualStepLabel(stepObject), m_visualStepList);
            item->setData(Qt::UserRole, stepObject.value("id").toString());
            item->setToolTip(stepObject.value("type").toString());

            if (!stepIdToSelect.isEmpty() && stepObject.value("id").toString() == stepIdToSelect) {
                rowToSelect = index;
            }
        }
    }

    if (rowToSelect < 0 && !steps.isEmpty()) {
        rowToSelect = 0;
    }

    m_visualStepList->setCurrentRow(rowToSelect);
    loadVisualStepFromRow(rowToSelect);
}

void WorkflowPanel::loadVisualStepFromRow(const int row)
{
    if (m_isSyncingVisualEditor) {
        return;
    }

    const QJsonArray steps = m_visualDefinitionRoot.value("steps").toArray();
    if (row < 0 || row >= steps.size()) {
        clearVisualStepEditor();
        return;
    }

    const QJsonObject stepObject = steps.at(row).toObject();
    const QJsonObject config = stepObject.value("config").toObject();
    const QString stepType = normalizedStepType(stepObject.value("type").toString());
    const bool usesAdvancedDecisionRules = stepType == "decision"
        && config.value("rules").isArray()
        && !config.value("rules").toArray().isEmpty();

    m_isSyncingVisualEditor = true;

    {
        const QSignalBlocker idBlocker(m_visualStepIdEdit);
        const QSignalBlocker typeBlocker(m_visualStepTypeCombo);
        const QSignalBlocker nameBlocker(m_visualStepNameEdit);
        const QSignalBlocker promptBlocker(m_promptTextEdit);
        const QSignalBlocker promptSystemBlocker(m_promptSystemPromptEdit);
        const QSignalBlocker promptOutputBlocker(m_promptOutputEdit);
        const QSignalBlocker promptModelBlocker(m_promptModelEdit);
        const QSignalBlocker memoryContentBlocker(m_memoryContentEdit);
        const QSignalBlocker memoryTypeBlocker(m_memoryTypeEdit);
        const QSignalBlocker memorySourceBlocker(m_memorySourceEdit);
        const QSignalBlocker memoryTagsBlocker(m_memoryTagsEdit);
        const QSignalBlocker memoryRelevanceBlocker(m_memoryRelevanceSpin);
        const QSignalBlocker decisionInputBlocker(m_decisionInputEdit);
        const QSignalBlocker decisionOperatorBlocker(m_decisionOperatorCombo);
        const QSignalBlocker decisionValueBlocker(m_decisionValueEdit);
        const QSignalBlocker decisionOutputBlocker(m_decisionOutputEdit);
        const QSignalBlocker decisionTrueBlocker(m_decisionTrueResultEdit);
        const QSignalBlocker decisionFalseBlocker(m_decisionFalseResultEdit);
        const QSignalBlocker decisionIfTrueBlocker(m_decisionIfTrueEdit);
        const QSignalBlocker decisionIfFalseBlocker(m_decisionIfFalseEdit);
        const QSignalBlocker decisionCaseBlocker(m_decisionCaseSensitiveCheckBox);

        m_visualStepIdEdit->setText(stepObject.value("id").toString());
        m_visualStepNameEdit->setText(stepObject.value("name").toString());

        const int typeIndex = m_visualStepTypeCombo->findText(stepType);
        m_visualStepTypeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);

        m_promptTextEdit->setPlainText(config.value("prompt").toString());
        m_promptSystemPromptEdit->setPlainText(config.value("system_prompt").toString());
        m_promptOutputEdit->setText(config.value("output").toString());
        m_promptModelEdit->setText(config.value("model").toString());

        m_memoryContentEdit->setPlainText(config.value("content").toString());
        m_memoryTypeEdit->setText(
            config.value("entry_type").toString().trimmed().isEmpty()
                ? config.value("memory_type").toString()
                : config.value("entry_type").toString()
        );
        m_memorySourceEdit->setText(config.value("source").toString());
        if (config.value("tags").isArray()) {
            QStringList tags;
            const QJsonArray tagArray = config.value("tags").toArray();
            for (const QJsonValue& value : tagArray) {
                const QString tag = value.toString().trimmed();
                if (!tag.isEmpty()) {
                    tags.append(tag);
                }
            }
            m_memoryTagsEdit->setText(tags.join(", "));
        } else {
            m_memoryTagsEdit->setText(config.value("tags").toString());
        }
        m_memoryRelevanceSpin->setValue(config.value("relevance").toInt(50));

        m_decisionInputEdit->setText(config.value("input").toString());
        const QString operatorName = config.value("operator").toString();
        const int operatorIndex = m_decisionOperatorCombo->findText(operatorName);
        m_decisionOperatorCombo->setCurrentIndex(operatorIndex >= 0 ? operatorIndex : 0);
        m_decisionValueEdit->setText(config.value("value").toString());
        m_decisionOutputEdit->setText(config.value("output").toString());
        m_decisionTrueResultEdit->setText(config.value("true_result").toString());
        m_decisionFalseResultEdit->setText(config.value("false_result").toString());
        m_decisionIfTrueEdit->setText(config.value("if_true").toString());
        m_decisionIfFalseEdit->setText(config.value("if_false").toString());
        m_decisionCaseSensitiveCheckBox->setChecked(config.value("case_sensitive").toBool(false));
    }

    updateVisualConfigPage();

    m_decisionAdvancedLabel->setVisible(stepType == "decision");
    if (stepType == "decision" && usesAdvancedDecisionRules) {
        const QString warningText =
            "Hinweis: Dieser Decision-Schritt nutzt ein 'rules'-Array. Der visuelle Editor zeigt ihn nur an; Aenderungen bitte im JSON vornehmen.";
        m_decisionAdvancedLabel->setText(warningText);
        m_decisionInputEdit->setEnabled(false);
        m_decisionOperatorCombo->setEnabled(false);
        m_decisionValueEdit->setEnabled(false);
        m_decisionOutputEdit->setEnabled(false);
        m_decisionTrueResultEdit->setEnabled(false);
        m_decisionFalseResultEdit->setEnabled(false);
        m_decisionIfTrueEdit->setEnabled(false);
        m_decisionIfFalseEdit->setEnabled(false);
        m_decisionCaseSensitiveCheckBox->setEnabled(false);
    } else {
        m_decisionAdvancedLabel->setText(
            "Hinweis: Decision-Schritte mit 'rules' werden hier nur gelesen. Fuer komplexe Regeln bitte direkt das JSON bearbeiten."
        );
        m_decisionInputEdit->setEnabled(true);
        m_decisionOperatorCombo->setEnabled(true);
        m_decisionValueEdit->setEnabled(true);
        m_decisionOutputEdit->setEnabled(true);
        m_decisionTrueResultEdit->setEnabled(true);
        m_decisionFalseResultEdit->setEnabled(true);
        m_decisionIfTrueEdit->setEnabled(true);
        m_decisionIfFalseEdit->setEnabled(true);
        m_decisionCaseSensitiveCheckBox->setEnabled(true);
    }

    m_isSyncingVisualEditor = false;
}

void WorkflowPanel::clearVisualStepEditor()
{
    m_isSyncingVisualEditor = true;

    if (m_visualStepIdEdit != nullptr) {
        m_visualStepIdEdit->clear();
    }
    if (m_visualStepNameEdit != nullptr) {
        m_visualStepNameEdit->clear();
    }
    if (m_visualStepTypeCombo != nullptr) {
        m_visualStepTypeCombo->setCurrentIndex(0);
    }
    if (m_promptTextEdit != nullptr) {
        m_promptTextEdit->clear();
        m_promptSystemPromptEdit->clear();
        m_promptOutputEdit->clear();
        m_promptModelEdit->clear();
    }
    if (m_memoryContentEdit != nullptr) {
        m_memoryContentEdit->clear();
        m_memoryTypeEdit->clear();
        m_memorySourceEdit->clear();
        m_memoryTagsEdit->clear();
        m_memoryRelevanceSpin->setValue(50);
    }
    if (m_decisionInputEdit != nullptr) {
        m_decisionInputEdit->clear();
        m_decisionOperatorCombo->setCurrentIndex(0);
        m_decisionValueEdit->clear();
        m_decisionOutputEdit->clear();
        m_decisionTrueResultEdit->clear();
        m_decisionFalseResultEdit->clear();
        m_decisionIfTrueEdit->clear();
        m_decisionIfFalseEdit->clear();
        m_decisionCaseSensitiveCheckBox->setChecked(false);
        m_decisionInputEdit->setEnabled(false);
        m_decisionOperatorCombo->setEnabled(false);
        m_decisionValueEdit->setEnabled(false);
        m_decisionOutputEdit->setEnabled(false);
        m_decisionTrueResultEdit->setEnabled(false);
        m_decisionFalseResultEdit->setEnabled(false);
        m_decisionIfTrueEdit->setEnabled(false);
        m_decisionIfFalseEdit->setEnabled(false);
        m_decisionCaseSensitiveCheckBox->setEnabled(false);
    }

    updateVisualConfigPage();
    m_isSyncingVisualEditor = false;
}

void WorkflowPanel::updateVisualConfigPage()
{
    if (m_visualStepConfigStack == nullptr || m_visualStepTypeCombo == nullptr) {
        return;
    }

    const QString stepType = normalizedStepType(m_visualStepTypeCombo->currentText());
    if (stepType == "save_memory") {
        m_visualStepConfigStack->setCurrentIndex(1);
        return;
    }

    if (stepType == "decision") {
        m_visualStepConfigStack->setCurrentIndex(2);
        return;
    }

    m_visualStepConfigStack->setCurrentIndex(0);
}

void WorkflowPanel::scheduleVisualStepApply()
{
    if (m_isSyncingVisualEditor || !m_visualEditorHasValidJson || m_visualApplyTimer == nullptr) {
        return;
    }

    m_visualApplyTimer->start();
}

void WorkflowPanel::applyVisualStepChanges()
{
    if (m_isSyncingVisualEditor || !m_visualEditorHasValidJson || m_visualStepList == nullptr) {
        return;
    }

    const int row = m_visualStepList->currentRow();
    QJsonArray steps = m_visualDefinitionRoot.value("steps").toArray();
    if (row < 0 || row >= steps.size()) {
        return;
    }

    QJsonObject stepObject = steps.at(row).toObject();
    const QString originalType = normalizedStepType(stepObject.value("type").toString());
    const QString stepType = normalizedStepType(m_visualStepTypeCombo->currentText());
    const bool decisionUsesRules = originalType == "decision"
        && stepObject.value("config").toObject().value("rules").isArray()
        && !stepObject.value("config").toObject().value("rules").toArray().isEmpty();

    const QString resolvedStepId = m_visualStepIdEdit->text().trimmed().isEmpty()
        ? generateVisualStepId(stepType)
        : m_visualStepIdEdit->text().trimmed();
    stepObject.insert("id", resolvedStepId);
    stepObject.insert("type", stepType);
    setJsonTextValue(&stepObject, "name", m_visualStepNameEdit->text());

    QJsonObject config = stepObject.value("config").toObject();

    if (stepType == "prompt") {
        removeConfigKeys(
            &config,
            {
                "content",
                "entry_type",
                "memory_type",
                "source",
                "tags",
                "relevance",
                "input",
                "operator",
                "value",
                "true_result",
                "false_result",
                "if_true",
                "if_false",
                "case_sensitive",
                "rules",
                "default_result",
                "default_next"
            }
        );
        setJsonTextValue(&config, "prompt", m_promptTextEdit->toPlainText());
        setJsonTextValue(&config, "system_prompt", m_promptSystemPromptEdit->toPlainText());
        setJsonTextValue(&config, "output", m_promptOutputEdit->text());
        setJsonTextValue(&config, "model", m_promptModelEdit->text());
    } else if (stepType == "save_memory") {
        removeConfigKeys(
            &config,
            {
                "prompt",
                "system_prompt",
                "output",
                "model",
                "input",
                "operator",
                "value",
                "true_result",
                "false_result",
                "if_true",
                "if_false",
                "case_sensitive",
                "rules",
                "default_result",
                "default_next"
            }
        );
        setJsonTextValue(&config, "content", m_memoryContentEdit->toPlainText());
        setJsonTextValue(&config, "entry_type", m_memoryTypeEdit->text());
        config.remove("memory_type");
        setJsonTextValue(&config, "source", m_memorySourceEdit->text());
        setJsonTextValue(&config, "tags", m_memoryTagsEdit->text());
        config.insert("relevance", m_memoryRelevanceSpin->value());
    } else if (!decisionUsesRules) {
        removeConfigKeys(
            &config,
            {
                "prompt",
                "system_prompt",
                "model",
                "content",
                "entry_type",
                "memory_type",
                "source",
                "tags",
                "relevance",
                "rules",
                "default_result",
                "default_next"
            }
        );
        setJsonTextValue(&config, "input", m_decisionInputEdit->text());
        setJsonTextValue(&config, "operator", m_decisionOperatorCombo->currentText());
        setJsonTextValue(&config, "value", m_decisionValueEdit->text());
        setJsonTextValue(&config, "output", m_decisionOutputEdit->text());
        setJsonTextValue(&config, "true_result", m_decisionTrueResultEdit->text());
        setJsonTextValue(&config, "false_result", m_decisionFalseResultEdit->text());
        setJsonTextValue(&config, "if_true", m_decisionIfTrueEdit->text());
        setJsonTextValue(&config, "if_false", m_decisionIfFalseEdit->text());
        if (m_decisionCaseSensitiveCheckBox->isChecked()) {
            config.insert("case_sensitive", true);
        } else {
            config.remove("case_sensitive");
        }
    }

    stepObject.insert("config", config);
    steps.replace(row, stepObject);
    m_visualDefinitionRoot.insert("steps", steps);

    if (auto* item = m_visualStepList->item(row); item != nullptr) {
        item->setText(visualStepLabel(stepObject));
        item->setData(Qt::UserRole, stepObject.value("id").toString());
    }

    if (m_visualStepIdEdit->text().trimmed().isEmpty()) {
        const QSignalBlocker blocker(m_visualStepIdEdit);
        m_visualStepIdEdit->setText(resolvedStepId);
    }

    m_isSyncingVisualEditor = true;
    m_definitionEdit->setPlainText(QString::fromUtf8(QJsonDocument(m_visualDefinitionRoot).toJson(QJsonDocument::Indented)));
    m_isSyncingVisualEditor = false;

    m_visualEditorStatusLabel->setStyleSheet("color: #2f6b3a;");
    m_visualEditorStatusLabel->setText(
        QString("Status: Visueller Editor hat Schritt '%1' ins JSON geschrieben.")
            .arg(stepObject.value("id").toString())
    );
}

void WorkflowPanel::addVisualStep(const QString& stepType)
{
    if (!m_visualEditorHasValidJson) {
        m_visualEditorStatusLabel->setStyleSheet("color: #8b2f2f;");
        m_visualEditorStatusLabel->setText("Status: Bitte erst ein gueltiges Workflow-JSON herstellen.");
        return;
    }

    QJsonArray steps = m_visualDefinitionRoot.value("steps").toArray();
    const QString newStepId = generateVisualStepId(stepType);
    steps.append(defaultStepObject(stepType, newStepId));
    m_visualDefinitionRoot.insert("steps", steps);

    m_isSyncingVisualEditor = true;
    m_definitionEdit->setPlainText(QString::fromUtf8(QJsonDocument(m_visualDefinitionRoot).toJson(QJsonDocument::Indented)));
    m_isSyncingVisualEditor = false;

    rebuildVisualStepList(newStepId);
    m_visualEditorStatusLabel->setStyleSheet("color: #2f6b3a;");
    m_visualEditorStatusLabel->setText(
        QString("Status: Neuer Schritt '%1' wurde angelegt.").arg(newStepId)
    );
}

void WorkflowPanel::removeSelectedVisualStep()
{
    if (!m_visualEditorHasValidJson || m_visualStepList == nullptr) {
        return;
    }

    const int row = m_visualStepList->currentRow();
    QJsonArray steps = m_visualDefinitionRoot.value("steps").toArray();
    if (row < 0 || row >= steps.size()) {
        return;
    }

    steps.removeAt(row);
    m_visualDefinitionRoot.insert("steps", steps);

    m_isSyncingVisualEditor = true;
    m_definitionEdit->setPlainText(QString::fromUtf8(QJsonDocument(m_visualDefinitionRoot).toJson(QJsonDocument::Indented)));
    m_isSyncingVisualEditor = false;

    const int nextRow = steps.isEmpty() ? -1 : qMin(row, static_cast<int>(steps.size()) - 1);
    const QString nextStepId = nextRow >= 0 ? steps.at(nextRow).toObject().value("id").toString() : QString();
    rebuildVisualStepList(nextStepId);
    m_visualEditorStatusLabel->setStyleSheet("color: #5f5548;");
    m_visualEditorStatusLabel->setText("Status: Schritt wurde entfernt.");
}

void WorkflowPanel::moveSelectedVisualStep(const int offset)
{
    if (!m_visualEditorHasValidJson || m_visualStepList == nullptr) {
        return;
    }

    const int row = m_visualStepList->currentRow();
    QJsonArray steps = m_visualDefinitionRoot.value("steps").toArray();
    const int targetRow = row + offset;
    if (row < 0 || row >= steps.size() || targetRow < 0 || targetRow >= steps.size()) {
        return;
    }

    const QJsonObject movedStep = steps.at(row).toObject();
    steps.removeAt(row);
    steps.insert(targetRow, movedStep);
    m_visualDefinitionRoot.insert("steps", steps);

    m_isSyncingVisualEditor = true;
    m_definitionEdit->setPlainText(QString::fromUtf8(QJsonDocument(m_visualDefinitionRoot).toJson(QJsonDocument::Indented)));
    m_isSyncingVisualEditor = false;

    rebuildVisualStepList(movedStep.value("id").toString());
    m_visualEditorStatusLabel->setStyleSheet("color: #5f5548;");
    m_visualEditorStatusLabel->setText(
        QString("Status: Schritt '%1' wurde verschoben.").arg(movedStep.value("id").toString())
    );
}

QString WorkflowPanel::selectedVisualStepId() const
{
    if (m_visualStepList == nullptr || m_visualStepList->currentItem() == nullptr) {
        return {};
    }

    return m_visualStepList->currentItem()->data(Qt::UserRole).toString();
}

QString WorkflowPanel::generateVisualStepId(const QString& stepType) const
{
    const QString prefix = stepType == "save_memory"
        ? "save_memory"
        : (stepType == "decision" ? "decision" : "prompt");
    const QJsonArray steps = m_visualDefinitionRoot.value("steps").toArray();

    int suffix = steps.size() + 1;
    while (true) {
        const QString candidate = QString("%1_%2").arg(prefix).arg(suffix);
        bool exists = false;
        for (const QJsonValue& value : steps) {
            if (value.toObject().value("id").toString() == candidate) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            return candidate;
        }

        ++suffix;
    }
}

QString WorkflowPanel::visualStepLabel(const QJsonObject& stepObject) const
{
    const QString stepId = stepObject.value("id").toString();
    const QString stepType = stepObject.value("type").toString();
    const QString stepName = stepObject.value("name").toString().trimmed();
    if (stepName.isEmpty()) {
        return QString("%1 (%2)").arg(stepId, stepType);
    }

    return QString("%1 [%2] - %3").arg(stepId, stepType, stepName);
}

void WorkflowPanel::resetEditor(const bool keepFeedback)
{
    m_currentWorkflowId = -1;
    m_nameEdit->clear();
    m_descriptionEdit->clear();
    m_definitionEdit->setPlainText(defaultWorkflowJson());
    m_executionOutputView->clear();
    m_activeCheckBox->setChecked(true);
    syncVisualEditorFromJson();

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
