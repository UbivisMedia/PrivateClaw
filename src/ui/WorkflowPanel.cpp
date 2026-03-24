#include "ui/WorkflowPanel.h"

#include "providers/ILlmProvider.h"
#include "providers/LmStudioProvider.h"
#include "providers/OllamaProvider.h"
#include "providers/ProviderManager.h"
#include "services/MemoryService.h"
#include "services/ProjectService.h"
#include "services/SettingsService.h"
#include "services/WorkflowService.h"
#include "tools/ToolExecutor.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QFrame>
#include <QHeaderView>
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
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <limits>
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

void setComboItemsWithEditableText(
    QComboBox* comboBox,
    const QStringList& items,
    const QString& currentText,
    const bool includeEmptyOption = false
)
{
    if (comboBox == nullptr) {
        return;
    }

    const QSignalBlocker blocker(comboBox);
    comboBox->clear();
    if (includeEmptyOption) {
        comboBox->addItem("<Standard>");
    }
    comboBox->addItems(items);

    const QString desiredText = currentText.trimmed();
    if (desiredText.isEmpty()) {
        comboBox->setCurrentIndex(0);
        return;
    }

    const int index = comboBox->findText(desiredText);
    if (index >= 0) {
        comboBox->setCurrentIndex(index);
    } else {
        comboBox->setEditText(desiredText);
    }
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
    } else if (stepType == "tool") {
        step.insert("name", "Tool ausfuehren");
        config.insert("tool", "file.read");
        config.insert("path", "docs/entwicklungsplan_llm_desktop_app.md");
        config.insert("max_chars", 4000);
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
    const QString& workspaceRoot,
    const QString& comfyUiBaseUrl,
    const domain::Workflow& workflow,
    const core::RunContext& runContext
)
{
    const tools::ToolExecutor toolExecutor(workspaceRoot, comfyUiBaseUrl);
    core::WorkflowEngine workflowEngine(&toolExecutor);

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
        "Aktuell werden 'prompt', 'save_memory', 'decision' und 'tool' unterstuetzt. "
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
    auto* addToolButton = new QPushButton("Tool", visualListCard);
    visualAddLayout->addWidget(addPromptButton);
    visualAddLayout->addWidget(addMemoryButton);
    visualAddLayout->addWidget(addDecisionButton);
    visualAddLayout->addWidget(addToolButton);

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

    auto* visualDetailScrollArea = new QScrollArea(visualSplitter);
    visualDetailScrollArea->setWidgetResizable(true);
    visualDetailScrollArea->setFrameShape(QFrame::NoFrame);
    visualDetailScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* visualDetailCard = new QFrame(visualDetailScrollArea);
    auto* visualDetailLayout = new QVBoxLayout(visualDetailCard);
    visualDetailLayout->setContentsMargins(0, 0, 0, 0);
    visualDetailLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    auto* visualFormLayout = new QFormLayout();
    visualFormLayout->setLabelAlignment(Qt::AlignLeft);

    m_visualStepIdEdit = new QLineEdit(visualDetailCard);
    m_visualStepTypeCombo = new QComboBox(visualDetailCard);
    m_visualStepTypeCombo->addItem("prompt");
    m_visualStepTypeCombo->addItem("save_memory");
    m_visualStepTypeCombo->addItem("decision");
    m_visualStepTypeCombo->addItem("tool");
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

    auto* toolPage = new QWidget(m_visualStepConfigStack);
    auto* toolLayout = new QFormLayout(toolPage);
    toolLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolNameCombo = new QComboBox(toolPage);
    m_toolNameCombo->addItems(QStringList{
        "file.read",
        "json.extract",
        "csv.read",
        "csv.write",
        "directory.read_recursive",
        "directory.read_changed",
        "directory.list",
        "memory.search",
        "memory.summarize",
        "memory.delete_old",
        "memory.ingest_directory",
        "file.write_text",
        "file.edit_diff",
        "http.request",
        "shell.run",
        "comfyui.workflow"
    });
    m_toolOutputEdit = new QLineEdit(toolPage);

    m_toolConfigStack = new QStackedWidget(toolPage);

    auto* fileReadPage = new QWidget(m_toolConfigStack);
    auto* fileReadLayout = new QFormLayout(fileReadPage);
    fileReadLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolFileReadPathEdit = new QLineEdit(fileReadPage);
    m_toolFileReadLineStartSpin = new QSpinBox(fileReadPage);
    m_toolFileReadLineStartSpin->setRange(1, 1000000);
    m_toolFileReadLineStartSpin->setValue(1);
    m_toolFileReadLineEndSpin = new QSpinBox(fileReadPage);
    m_toolFileReadLineEndSpin->setRange(0, 1000000);
    m_toolFileReadLineEndSpin->setValue(0);
    m_toolFileReadLineEndSpin->setSpecialValueText("Bis Dateiende");
    m_toolFileReadMaxCharsSpin = new QSpinBox(fileReadPage);
    m_toolFileReadMaxCharsSpin->setRange(0, 2000000);
    m_toolFileReadMaxCharsSpin->setValue(20000);
    m_toolFileReadMaxCharsSpin->setSpecialValueText("Unbegrenzt");
    fileReadLayout->addRow("Pfad", m_toolFileReadPathEdit);
    fileReadLayout->addRow("Startzeile", m_toolFileReadLineStartSpin);
    fileReadLayout->addRow("Endzeile", m_toolFileReadLineEndSpin);
    fileReadLayout->addRow("Max. Zeichen", m_toolFileReadMaxCharsSpin);
    m_toolConfigStack->addWidget(fileReadPage);

    auto* jsonExtractPage = new QWidget(m_toolConfigStack);
    auto* jsonExtractLayout = new QFormLayout(jsonExtractPage);
    jsonExtractLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolJsonInputEdit = new QLineEdit(jsonExtractPage);
    m_toolJsonInputEdit->setPlaceholderText("{{last_response}}");
    m_toolJsonPathEdit = new QLineEdit(jsonExtractPage);
    m_toolJsonPathEdit->setPlaceholderText("items[0].title");
    m_toolJsonPrettyCheckBox = new QCheckBox("Objekte und Arrays formatiert ausgeben", jsonExtractPage);
    m_toolJsonPrettyCheckBox->setChecked(true);
    jsonExtractLayout->addRow("JSON-Quelle", m_toolJsonInputEdit);
    jsonExtractLayout->addRow("Pfad", m_toolJsonPathEdit);
    jsonExtractLayout->addRow("", m_toolJsonPrettyCheckBox);
    m_toolConfigStack->addWidget(jsonExtractPage);

    auto* csvPage = new QWidget(m_toolConfigStack);
    auto* csvLayout = new QFormLayout(csvPage);
    csvLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolCsvPathEdit = new QLineEdit(csvPage);
    m_toolCsvDelimiterCombo = new QComboBox(csvPage);
    m_toolCsvDelimiterCombo->setEditable(true);
    m_toolCsvDelimiterCombo->addItems(QStringList{ ",", ";", "\\t" });
    m_toolCsvHasHeaderCheckBox = new QCheckBox("Erste Zeile als Header behandeln / schreiben", csvPage);
    m_toolCsvHasHeaderCheckBox->setChecked(true);
    m_toolCsvMaxRowsSpin = new QSpinBox(csvPage);
    m_toolCsvMaxRowsSpin->setRange(1, 500000);
    m_toolCsvMaxRowsSpin->setValue(200);
    m_toolCsvOutputFormatCombo = new QComboBox(csvPage);
    m_toolCsvOutputFormatCombo->addItem("JSON", "json");
    m_toolCsvOutputFormatCombo->addItem("Text", "text");
    m_toolCsvSourceFormatCombo = new QComboBox(csvPage);
    m_toolCsvSourceFormatCombo->addItem("JSON-Zeilenarray", "rows_json");
    m_toolCsvSourceFormatCombo->addItem("Rohes CSV", "csv_text");
    m_toolCsvCreateDirsCheckBox = new QCheckBox("Fehlende Zielordner automatisch anlegen", csvPage);
    m_toolCsvCreateDirsCheckBox->setChecked(true);
    m_toolCsvReturnContentCheckBox = new QCheckBox("Geschriebenen CSV-Inhalt zurueckgeben", csvPage);
    m_toolCsvContentEdit = new QPlainTextEdit(csvPage);
    m_toolCsvContentEdit->setMinimumHeight(140);
    m_toolCsvContentEdit->setPlaceholderText("[\n  {\"name\": \"Alice\", \"score\": \"42\"}\n]");
    csvLayout->addRow("Pfad", m_toolCsvPathEdit);
    csvLayout->addRow("Delimiter", m_toolCsvDelimiterCombo);
    csvLayout->addRow("", m_toolCsvHasHeaderCheckBox);
    csvLayout->addRow("Max. Zeilen (read)", m_toolCsvMaxRowsSpin);
    csvLayout->addRow("Ausgabeformat (read)", m_toolCsvOutputFormatCombo);
    csvLayout->addRow("Quellformat (write)", m_toolCsvSourceFormatCombo);
    csvLayout->addRow("Inhalt (write)", m_toolCsvContentEdit);
    csvLayout->addRow("", m_toolCsvCreateDirsCheckBox);
    csvLayout->addRow("", m_toolCsvReturnContentCheckBox);
    m_toolConfigStack->addWidget(csvPage);

    auto* directoryReadPage = new QWidget(m_toolConfigStack);
    auto* directoryReadLayout = new QFormLayout(directoryReadPage);
    directoryReadLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolDirectoryReadPathEdit = new QLineEdit(directoryReadPage);
    m_toolDirectoryReadExtensionsEdit = new QLineEdit(directoryReadPage);
    m_toolDirectoryReadExtensionsEdit->setPlaceholderText(".cpp,.h,.md,.txt");
    m_toolDirectoryReadExcludeEdit = new QLineEdit(directoryReadPage);
    m_toolDirectoryReadExcludeEdit->setPlaceholderText(".git,build,node_modules,__pycache__");
    m_toolDirectoryReadModifiedAfterEdit = new QLineEdit(directoryReadPage);
    m_toolDirectoryReadModifiedAfterEdit->setPlaceholderText("2026-03-23T10:15:00Z");
    m_toolMemoryIngestModeCombo = new QComboBox(directoryReadPage);
    m_toolMemoryIngestModeCombo->addItem("Vollimport", "recursive");
    m_toolMemoryIngestModeCombo->addItem("Nur geaenderte Dateien", "changed");
    m_toolMemoryIngestTypeEdit = new QLineEdit(directoryReadPage);
    m_toolMemoryIngestTypeEdit->setText("artifact");
    m_toolMemoryIngestSourceEdit = new QLineEdit(directoryReadPage);
    m_toolMemoryIngestSourceEdit->setPlaceholderText("workflow:projekt/ingest");
    m_toolMemoryIngestTagsEdit = new QLineEdit(directoryReadPage);
    m_toolMemoryIngestTagsEdit->setPlaceholderText("codebase,kontext,ingest");
    m_toolMemoryIngestRelevanceSpin = new QSpinBox(directoryReadPage);
    m_toolMemoryIngestRelevanceSpin->setRange(0, 100);
    m_toolMemoryIngestRelevanceSpin->setValue(80);
    m_toolDirectoryReadMaxFilesSpin = new QSpinBox(directoryReadPage);
    m_toolDirectoryReadMaxFilesSpin->setRange(1, 5000);
    m_toolDirectoryReadMaxFilesSpin->setValue(40);
    m_toolDirectoryReadWithinMinutesSpin = new QSpinBox(directoryReadPage);
    m_toolDirectoryReadWithinMinutesSpin->setRange(0, 525600);
    m_toolDirectoryReadWithinMinutesSpin->setValue(0);
    m_toolDirectoryReadWithinMinutesSpin->setSpecialValueText("Deaktiviert");
    m_toolDirectoryReadMaxCharsPerFileSpin = new QSpinBox(directoryReadPage);
    m_toolDirectoryReadMaxCharsPerFileSpin->setRange(0, 2000000);
    m_toolDirectoryReadMaxCharsPerFileSpin->setValue(8000);
    m_toolDirectoryReadMaxCharsPerFileSpin->setSpecialValueText("Unbegrenzt");
    m_toolDirectoryReadMaxTotalCharsSpin = new QSpinBox(directoryReadPage);
    m_toolDirectoryReadMaxTotalCharsSpin->setRange(0, 5000000);
    m_toolDirectoryReadMaxTotalCharsSpin->setValue(120000);
    m_toolDirectoryReadMaxTotalCharsSpin->setSpecialValueText("Unbegrenzt");
    m_toolDirectoryReadIncludeHiddenCheckBox = new QCheckBox("Versteckte Dateien einbeziehen", directoryReadPage);
    m_toolDirectoryReadSkipBinaryCheckBox = new QCheckBox("Binaerdateien ueberspringen", directoryReadPage);
    m_toolDirectoryReadSkipBinaryCheckBox->setChecked(true);
    directoryReadLayout->addRow("Verzeichnis", m_toolDirectoryReadPathEdit);
    directoryReadLayout->addRow("Extensions", m_toolDirectoryReadExtensionsEdit);
    directoryReadLayout->addRow("Ausschliessen", m_toolDirectoryReadExcludeEdit);
    directoryReadLayout->addRow("Geaendert seit (ISO)", m_toolDirectoryReadModifiedAfterEdit);
    directoryReadLayout->addRow("Oder letzte Minuten", m_toolDirectoryReadWithinMinutesSpin);
    directoryReadLayout->addRow("Ingest-Modus", m_toolMemoryIngestModeCombo);
    directoryReadLayout->addRow("Memory-Typ", m_toolMemoryIngestTypeEdit);
    directoryReadLayout->addRow("Quelle (optional)", m_toolMemoryIngestSourceEdit);
    directoryReadLayout->addRow("Tags", m_toolMemoryIngestTagsEdit);
    directoryReadLayout->addRow("Relevanz", m_toolMemoryIngestRelevanceSpin);
    directoryReadLayout->addRow("Max. Dateien", m_toolDirectoryReadMaxFilesSpin);
    directoryReadLayout->addRow("Max. Zeichen/Datei", m_toolDirectoryReadMaxCharsPerFileSpin);
    directoryReadLayout->addRow("Max. Gesamtzeichen", m_toolDirectoryReadMaxTotalCharsSpin);
    directoryReadLayout->addRow("", m_toolDirectoryReadIncludeHiddenCheckBox);
    directoryReadLayout->addRow("", m_toolDirectoryReadSkipBinaryCheckBox);
    m_toolConfigStack->addWidget(directoryReadPage);

    auto* directoryListPage = new QWidget(m_toolConfigStack);
    auto* directoryListLayout = new QFormLayout(directoryListPage);
    directoryListLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolDirectoryListPathEdit = new QLineEdit(directoryListPage);
    m_toolDirectoryListExtensionsEdit = new QLineEdit(directoryListPage);
    m_toolDirectoryListExtensionsEdit->setPlaceholderText(".cpp,.h,.md,.txt");
    m_toolDirectoryListExcludeEdit = new QLineEdit(directoryListPage);
    m_toolDirectoryListExcludeEdit->setPlaceholderText(".git,build,node_modules,__pycache__");
    m_toolDirectoryListMaxEntriesSpin = new QSpinBox(directoryListPage);
    m_toolDirectoryListMaxEntriesSpin->setRange(1, 20000);
    m_toolDirectoryListMaxEntriesSpin->setValue(200);
    m_toolDirectoryListRecursiveCheckBox = new QCheckBox("Unterverzeichnisse einbeziehen", directoryListPage);
    m_toolDirectoryListRecursiveCheckBox->setChecked(true);
    m_toolDirectoryListIncludeHiddenCheckBox = new QCheckBox("Versteckte Dateien einbeziehen", directoryListPage);
    m_toolDirectoryListDirectoriesOnlyCheckBox = new QCheckBox("Nur Verzeichnisse ausgeben", directoryListPage);
    directoryListLayout->addRow("Verzeichnis", m_toolDirectoryListPathEdit);
    directoryListLayout->addRow("Extensions", m_toolDirectoryListExtensionsEdit);
    directoryListLayout->addRow("Ausschliessen", m_toolDirectoryListExcludeEdit);
    directoryListLayout->addRow("Max. Eintraege", m_toolDirectoryListMaxEntriesSpin);
    directoryListLayout->addRow("", m_toolDirectoryListRecursiveCheckBox);
    directoryListLayout->addRow("", m_toolDirectoryListIncludeHiddenCheckBox);
    directoryListLayout->addRow("", m_toolDirectoryListDirectoriesOnlyCheckBox);
    m_toolConfigStack->addWidget(directoryListPage);

    auto* memoryPage = new QWidget(m_toolConfigStack);
    auto* memoryLayout = new QFormLayout(memoryPage);
    memoryLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolMemoryQueryEdit = new QLineEdit(memoryPage);
    m_toolMemoryQueryEdit->setPlaceholderText("Fehlermeldung, Kapitel 3, Architektur...");
    m_toolMemoryTypeFilterEdit = new QLineEdit(memoryPage);
    m_toolMemoryTypeFilterEdit->setPlaceholderText("note, artifact, summary");
    m_toolMemoryTagsFilterEdit = new QLineEdit(memoryPage);
    m_toolMemoryTagsFilterEdit->setPlaceholderText("codebase, plot, design");
    m_toolMemoryLimitSpin = new QSpinBox(memoryPage);
    m_toolMemoryLimitSpin->setRange(1, 10000);
    m_toolMemoryLimitSpin->setValue(10);
    m_toolMemoryMaxCharsSpin = new QSpinBox(memoryPage);
    m_toolMemoryMaxCharsSpin->setRange(0, 5000000);
    m_toolMemoryMaxCharsSpin->setValue(16000);
    m_toolMemoryMaxCharsSpin->setSpecialValueText("Unbegrenzt");
    m_toolMemoryFormatCombo = new QComboBox(memoryPage);
    m_toolMemoryFormatCombo->addItem("Snippets", "snippets");
    m_toolMemoryFormatCombo->addItem("Volltext", "full");
    m_toolMemorySummaryPromptEdit = new QPlainTextEdit(memoryPage);
    m_toolMemorySummaryPromptEdit->setMinimumHeight(110);
    m_toolMemorySummaryPromptEdit->setPlaceholderText("Optionale eigene Zusammenfassungsanweisung.");
    m_toolMemorySummarySystemPromptEdit = new QLineEdit(memoryPage);
    m_toolMemorySummarySystemPromptEdit->setPlaceholderText("Optionaler Systemprompt fuer die Zusammenfassung");
    m_toolMemorySaveSummaryCheckBox = new QCheckBox("Zusammenfassung als neuen Memory-Eintrag speichern", memoryPage);
    m_toolMemorySaveSummaryCheckBox->setChecked(true);
    m_toolMemorySummaryTypeEdit = new QLineEdit(memoryPage);
    m_toolMemorySummaryTypeEdit->setText("summary");
    m_toolMemorySummarySourceEdit = new QLineEdit(memoryPage);
    m_toolMemorySummarySourceEdit->setPlaceholderText("workflow:projekt/summary");
    m_toolMemorySummaryTagsEdit = new QLineEdit(memoryPage);
    m_toolMemorySummaryTagsEdit->setPlaceholderText("summary,context");
    m_toolMemorySummaryRelevanceSpin = new QSpinBox(memoryPage);
    m_toolMemorySummaryRelevanceSpin->setRange(0, 100);
    m_toolMemorySummaryRelevanceSpin->setValue(75);
    m_toolMemoryDeleteOlderThanDaysSpin = new QSpinBox(memoryPage);
    m_toolMemoryDeleteOlderThanDaysSpin->setRange(1, 3650);
    m_toolMemoryDeleteOlderThanDaysSpin->setValue(30);
    m_toolMemoryDeleteKeepLatestSpin = new QSpinBox(memoryPage);
    m_toolMemoryDeleteKeepLatestSpin->setRange(0, 10000);
    m_toolMemoryDeleteKeepLatestSpin->setValue(0);
    m_toolMemoryDeleteKeepRelevanceSpin = new QSpinBox(memoryPage);
    m_toolMemoryDeleteKeepRelevanceSpin->setRange(0, 100);
    m_toolMemoryDeleteKeepRelevanceSpin->setValue(90);
    m_toolMemoryDeleteDryRunCheckBox = new QCheckBox("Nur simulieren, nichts wirklich loeschen", memoryPage);
    m_toolMemoryDeleteDryRunCheckBox->setChecked(true);
    memoryLayout->addRow("Suche", m_toolMemoryQueryEdit);
    memoryLayout->addRow("Typfilter", m_toolMemoryTypeFilterEdit);
    memoryLayout->addRow("Tagfilter", m_toolMemoryTagsFilterEdit);
    memoryLayout->addRow("Limit", m_toolMemoryLimitSpin);
    memoryLayout->addRow("Max. Zeichen", m_toolMemoryMaxCharsSpin);
    memoryLayout->addRow("Format (search)", m_toolMemoryFormatCombo);
    memoryLayout->addRow("Prompt (summarize)", m_toolMemorySummaryPromptEdit);
    memoryLayout->addRow("Systemprompt (summarize)", m_toolMemorySummarySystemPromptEdit);
    memoryLayout->addRow("", m_toolMemorySaveSummaryCheckBox);
    memoryLayout->addRow("Summary-Typ", m_toolMemorySummaryTypeEdit);
    memoryLayout->addRow("Summary-Quelle", m_toolMemorySummarySourceEdit);
    memoryLayout->addRow("Summary-Tags", m_toolMemorySummaryTagsEdit);
    memoryLayout->addRow("Summary-Relevanz", m_toolMemorySummaryRelevanceSpin);
    memoryLayout->addRow("Aelter als Tage", m_toolMemoryDeleteOlderThanDaysSpin);
    memoryLayout->addRow("Neueste behalten", m_toolMemoryDeleteKeepLatestSpin);
    memoryLayout->addRow("Ab Relevanz behalten", m_toolMemoryDeleteKeepRelevanceSpin);
    memoryLayout->addRow("", m_toolMemoryDeleteDryRunCheckBox);
    m_toolConfigStack->addWidget(memoryPage);

    auto* fileWritePage = new QWidget(m_toolConfigStack);
    auto* fileWriteLayout = new QFormLayout(fileWritePage);
    fileWriteLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolFileWritePathEdit = new QLineEdit(fileWritePage);
    m_toolFileWriteModeCombo = new QComboBox(fileWritePage);
    m_toolFileWriteModeCombo->addItem("Ueberschreiben", "overwrite");
    m_toolFileWriteModeCombo->addItem("Anhaengen", "append");
    m_toolFileWriteCreateDirsCheckBox = new QCheckBox("Fehlende Zielordner automatisch anlegen", fileWritePage);
    m_toolFileWriteCreateDirsCheckBox->setChecked(true);
    m_toolFileWriteReturnContentCheckBox = new QCheckBox("Aktualisierten Dateiinhalt zurueckgeben", fileWritePage);
    m_toolFileWriteContentEdit = new QPlainTextEdit(fileWritePage);
    m_toolFileWriteContentEdit->setMinimumHeight(140);
    m_toolFileWriteContentEdit->setPlaceholderText("Inhalt, der in die Datei geschrieben werden soll.");
    fileWriteLayout->addRow("Pfad", m_toolFileWritePathEdit);
    fileWriteLayout->addRow("Modus", m_toolFileWriteModeCombo);
    fileWriteLayout->addRow("Inhalt", m_toolFileWriteContentEdit);
    fileWriteLayout->addRow("", m_toolFileWriteCreateDirsCheckBox);
    fileWriteLayout->addRow("", m_toolFileWriteReturnContentCheckBox);
    m_toolConfigStack->addWidget(fileWritePage);

    auto* fileEditPage = new QWidget(m_toolConfigStack);
    auto* fileEditLayout = new QFormLayout(fileEditPage);
    fileEditLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolFileEditPathEdit = new QLineEdit(fileEditPage);
    m_toolFileEditReturnContentCheckBox = new QCheckBox("Aktualisierten Dateiinhalt zurueckgeben", fileEditPage);
    m_toolFileEditDiffEdit = new QPlainTextEdit(fileEditPage);
    m_toolFileEditDiffEdit->setMinimumHeight(140);
    m_toolFileEditDiffEdit->setPlaceholderText(
        "--- a/datei.txt\n+++ b/datei.txt\n@@ -1,1 +1,1 @@\n-alter Text\n+neuer Text"
    );
    fileEditLayout->addRow("Pfad (optional)", m_toolFileEditPathEdit);
    fileEditLayout->addRow("Diff/Patch", m_toolFileEditDiffEdit);
    fileEditLayout->addRow("", m_toolFileEditReturnContentCheckBox);
    m_toolConfigStack->addWidget(fileEditPage);

    auto* httpPage = new QWidget(m_toolConfigStack);
    auto* httpLayout = new QFormLayout(httpPage);
    httpLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolHttpUrlEdit = new QLineEdit(httpPage);
    m_toolHttpMethodCombo = new QComboBox(httpPage);
    m_toolHttpMethodCombo->addItems(QStringList{ "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD" });
    m_toolHttpTimeoutSpin = new QSpinBox(httpPage);
    m_toolHttpTimeoutSpin->setRange(0, 3600000);
    m_toolHttpTimeoutSpin->setSingleStep(1000);
    m_toolHttpTimeoutSpin->setValue(30000);
    m_toolHttpTimeoutSpin->setSpecialValueText("Ohne lokales Timeout");
    m_toolHttpTimeoutSpin->setSuffix(" ms");
    m_toolHttpBodyJsonCheckBox = new QCheckBox("Body als JSON interpretieren", httpPage);
    m_toolHttpHeadersEdit = new QPlainTextEdit(httpPage);
    m_toolHttpHeadersEdit->setMinimumHeight(100);
    m_toolHttpHeadersEdit->setPlaceholderText("{\n  \"Authorization\": \"Bearer ...\",\n  \"Accept\": \"application/json\"\n}");
    m_toolHttpBodyEdit = new QPlainTextEdit(httpPage);
    m_toolHttpBodyEdit->setMinimumHeight(140);
    m_toolHttpBodyEdit->setPlaceholderText("{\n  \"example\": true\n}");
    httpLayout->addRow("URL", m_toolHttpUrlEdit);
    httpLayout->addRow("Methode", m_toolHttpMethodCombo);
    httpLayout->addRow("Timeout", m_toolHttpTimeoutSpin);
    httpLayout->addRow("Header-JSON", m_toolHttpHeadersEdit);
    httpLayout->addRow("Request-Body", m_toolHttpBodyEdit);
    httpLayout->addRow("", m_toolHttpBodyJsonCheckBox);
    m_toolConfigStack->addWidget(httpPage);

    auto* shellPage = new QWidget(m_toolConfigStack);
    auto* shellLayout = new QFormLayout(shellPage);
    shellLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolShellCommandEdit = new QLineEdit(shellPage);
    m_toolShellCommandEdit->setPlaceholderText("git status --short");
    m_toolShellWorkingDirEdit = new QLineEdit(shellPage);
    m_toolShellWorkingDirEdit->setPlaceholderText(".");
    m_toolShellTimeoutSpin = new QSpinBox(shellPage);
    m_toolShellTimeoutSpin->setRange(0, 3600000);
    m_toolShellTimeoutSpin->setValue(60000);
    m_toolShellTimeoutSpin->setSpecialValueText("Ohne lokales Timeout");
    m_toolShellTimeoutSpin->setSuffix(" ms");
    m_toolShellMaxOutputCharsSpin = new QSpinBox(shellPage);
    m_toolShellMaxOutputCharsSpin->setRange(0, 5000000);
    m_toolShellMaxOutputCharsSpin->setValue(20000);
    m_toolShellMaxOutputCharsSpin->setSpecialValueText("Unbegrenzt");
    m_toolShellIncludeStderrCheckBox = new QCheckBox("stderr an die Ausgabe anhaengen", shellPage);
    m_toolShellIncludeStderrCheckBox->setChecked(true);
    shellLayout->addRow("Befehl", m_toolShellCommandEdit);
    shellLayout->addRow("Arbeitsverzeichnis", m_toolShellWorkingDirEdit);
    shellLayout->addRow("Timeout", m_toolShellTimeoutSpin);
    shellLayout->addRow("Max. Ausgabe", m_toolShellMaxOutputCharsSpin);
    shellLayout->addRow("", m_toolShellIncludeStderrCheckBox);
    m_toolConfigStack->addWidget(shellPage);

    auto* comfyPage = new QWidget(m_toolConfigStack);
    auto* comfyLayout = new QVBoxLayout(comfyPage);
    comfyLayout->setContentsMargins(0, 0, 0, 0);
    auto* comfyMetaRow = new QHBoxLayout();
    m_toolComfyRefreshButton = new QPushButton("ComfyUI-Daten laden", comfyPage);
    m_toolComfyStatusLabel = new QLabel("ComfyUI-Daten: noch nicht geladen.", comfyPage);
    m_toolComfyStatusLabel->setProperty("sectionBody", true);
    m_toolComfyStatusLabel->setWordWrap(true);
    comfyMetaRow->addWidget(m_toolComfyRefreshButton);
    comfyMetaRow->addWidget(m_toolComfyStatusLabel, 1);

    auto* comfyModeLayout = new QFormLayout();
    comfyModeLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolComfyModeCombo = new QComboBox(comfyPage);
    m_toolComfyModeCombo->addItem("Formular (txt2img)", "txt2img");
    m_toolComfyModeCombo->addItem("Formular (img2img)", "img2img");
    m_toolComfyModeCombo->addItem("Formular (Inpainting)", "inpainting");
    m_toolComfyModeCombo->addItem("Rohes Workflow-JSON", "raw_json");
    comfyModeLayout->addRow("Bearbeitungsmodus", m_toolComfyModeCombo);

    m_toolComfyModeStack = new QStackedWidget(comfyPage);

    auto* comfyFormPage = new QWidget(m_toolComfyModeStack);
    auto* comfyFormLayout = new QFormLayout(comfyFormPage);
    comfyFormLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolComfyCheckpointCombo = new QComboBox(comfyFormPage);
    m_toolComfyCheckpointCombo->setEditable(true);
    m_toolComfyVaeCombo = new QComboBox(comfyFormPage);
    m_toolComfyVaeCombo->setEditable(true);
    m_toolComfyImageCombo = new QComboBox(comfyFormPage);
    m_toolComfyImageCombo->setEditable(true);
    m_toolComfyImageCombo->setToolTip("Entweder vorhandenes ComfyUI-Input-Bild oder lokaler Dateipfad.");
    m_toolComfyMaskImageCombo = new QComboBox(comfyFormPage);
    m_toolComfyMaskImageCombo->setEditable(true);
    m_toolComfyMaskImageCombo->setToolTip("Maskenbild fuer Inpainting. Lokaler Dateipfad wird automatisch hochgeladen.");
    m_toolComfyMaskChannelCombo = new QComboBox(comfyFormPage);
    m_toolComfyMaskChannelCombo->setEditable(true);
    m_toolComfyMaskGrowSpin = new QSpinBox(comfyFormPage);
    m_toolComfyMaskGrowSpin->setRange(0, 64);
    m_toolComfyMaskGrowSpin->setValue(6);
    m_toolComfyPositivePromptEdit = new QTextEdit(comfyFormPage);
    m_toolComfyPositivePromptEdit->setMinimumHeight(90);
    m_toolComfyNegativePromptEdit = new QTextEdit(comfyFormPage);
    m_toolComfyNegativePromptEdit->setMinimumHeight(70);
    m_toolComfyWidthSpin = new QSpinBox(comfyFormPage);
    m_toolComfyWidthSpin->setRange(16, 16384);
    m_toolComfyWidthSpin->setSingleStep(8);
    m_toolComfyWidthSpin->setValue(1024);
    m_toolComfyHeightSpin = new QSpinBox(comfyFormPage);
    m_toolComfyHeightSpin->setRange(16, 16384);
    m_toolComfyHeightSpin->setSingleStep(8);
    m_toolComfyHeightSpin->setValue(1024);
    m_toolComfyBatchSizeSpin = new QSpinBox(comfyFormPage);
    m_toolComfyBatchSizeSpin->setRange(1, 256);
    m_toolComfyBatchSizeSpin->setValue(1);
    m_toolComfyStepsSpin = new QSpinBox(comfyFormPage);
    m_toolComfyStepsSpin->setRange(1, 10000);
    m_toolComfyStepsSpin->setValue(20);
    m_toolComfySeedSpin = new QSpinBox(comfyFormPage);
    m_toolComfySeedSpin->setRange(0, std::numeric_limits<int>::max());
    m_toolComfyRandomizeSeedCheckBox = new QCheckBox("Neuen Seed pro Lauf erzeugen", comfyFormPage);
    m_toolComfyCfgSpin = new QDoubleSpinBox(comfyFormPage);
    m_toolComfyCfgSpin->setRange(0.0, 100.0);
    m_toolComfyCfgSpin->setDecimals(2);
    m_toolComfyCfgSpin->setSingleStep(0.1);
    m_toolComfyCfgSpin->setValue(8.0);
    m_toolComfyDenoiseSpin = new QDoubleSpinBox(comfyFormPage);
    m_toolComfyDenoiseSpin->setRange(0.0, 1.0);
    m_toolComfyDenoiseSpin->setDecimals(2);
    m_toolComfyDenoiseSpin->setSingleStep(0.01);
    m_toolComfyDenoiseSpin->setValue(1.0);
    m_toolComfySamplerCombo = new QComboBox(comfyFormPage);
    m_toolComfySamplerCombo->setEditable(true);
    m_toolComfySchedulerCombo = new QComboBox(comfyFormPage);
    m_toolComfySchedulerCombo->setEditable(true);
    m_toolComfyClipSkipSpin = new QSpinBox(comfyFormPage);
    m_toolComfyClipSkipSpin->setRange(-24, -1);
    m_toolComfyClipSkipSpin->setValue(-1);
    m_toolComfyFilenamePrefixEdit = new QLineEdit(comfyFormPage);
    m_toolComfyFilenamePrefixEdit->setPlaceholderText("PrivateClaw");
    m_toolComfyLoraTable = new QTableWidget(comfyFormPage);
    m_toolComfyLoraTable->setColumnCount(4);
    m_toolComfyLoraTable->setHorizontalHeaderLabels(QStringList{ "Aktiv", "LoRA", "Model", "CLIP" });
    m_toolComfyLoraTable->horizontalHeader()->setStretchLastSection(false);
    m_toolComfyLoraTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_toolComfyLoraTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_toolComfyLoraTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_toolComfyLoraTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_toolComfyLoraTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_toolComfyLoraTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_toolComfyLoraTable->setMinimumHeight(150);
    auto* comfyLoraButtons = new QHBoxLayout();
    m_toolComfyAddLoraButton = new QPushButton("LoRA hinzufuegen", comfyFormPage);
    m_toolComfyRemoveLoraButton = new QPushButton("LoRA entfernen", comfyFormPage);
    comfyLoraButtons->addWidget(m_toolComfyAddLoraButton);
    comfyLoraButtons->addWidget(m_toolComfyRemoveLoraButton);
    comfyLoraButtons->addStretch();
    auto* comfyLoraBox = new QWidget(comfyFormPage);
    auto* comfyLoraBoxLayout = new QVBoxLayout(comfyLoraBox);
    comfyLoraBoxLayout->setContentsMargins(0, 0, 0, 0);
    comfyLoraBoxLayout->addWidget(m_toolComfyLoraTable);
    comfyLoraBoxLayout->addLayout(comfyLoraButtons);
    comfyFormLayout->addRow("Checkpoint", m_toolComfyCheckpointCombo);
    comfyFormLayout->addRow("VAE-Override", m_toolComfyVaeCombo);
    comfyFormLayout->addRow("Startbild", m_toolComfyImageCombo);
    comfyFormLayout->addRow("Maskenbild", m_toolComfyMaskImageCombo);
    comfyFormLayout->addRow("Maskenkanal", m_toolComfyMaskChannelCombo);
    comfyFormLayout->addRow("Maske erweitern", m_toolComfyMaskGrowSpin);
    comfyFormLayout->addRow("Positiver Prompt", m_toolComfyPositivePromptEdit);
    comfyFormLayout->addRow("Negativer Prompt", m_toolComfyNegativePromptEdit);
    comfyFormLayout->addRow("Breite", m_toolComfyWidthSpin);
    comfyFormLayout->addRow("Hoehe", m_toolComfyHeightSpin);
    comfyFormLayout->addRow("Batch-Groesse", m_toolComfyBatchSizeSpin);
    comfyFormLayout->addRow("Sampling Steps", m_toolComfyStepsSpin);
    comfyFormLayout->addRow("CFG", m_toolComfyCfgSpin);
    comfyFormLayout->addRow("Denoise", m_toolComfyDenoiseSpin);
    comfyFormLayout->addRow("Seed", m_toolComfySeedSpin);
    comfyFormLayout->addRow("", m_toolComfyRandomizeSeedCheckBox);
    comfyFormLayout->addRow("Sampler", m_toolComfySamplerCombo);
    comfyFormLayout->addRow("Scheduler", m_toolComfySchedulerCombo);
    comfyFormLayout->addRow("CLIP Stop Layer", m_toolComfyClipSkipSpin);
    comfyFormLayout->addRow("Dateipraefix", m_toolComfyFilenamePrefixEdit);
    comfyFormLayout->addRow("LoRA-Stack", comfyLoraBox);
    m_toolComfyModeStack->addWidget(comfyFormPage);

    auto* comfyRawPage = new QWidget(m_toolComfyModeStack);
    auto* comfyRawLayout = new QFormLayout(comfyRawPage);
    comfyRawLayout->setLabelAlignment(Qt::AlignLeft);
    m_toolComfyWorkflowEdit = new QPlainTextEdit(comfyRawPage);
    m_toolComfyWorkflowEdit->setMinimumHeight(220);
    m_toolComfyWorkflowEdit->setPlaceholderText("{\n  \"3\": { ... }\n}");
    comfyRawLayout->addRow("Workflow-JSON", m_toolComfyWorkflowEdit);
    m_toolComfyModeStack->addWidget(comfyRawPage);

    m_toolComfyOutputDirEdit = new QLineEdit(comfyPage);
    m_toolComfyDownloadImagesCheckBox = new QCheckBox("Bilder herunterladen", comfyPage);
    m_toolComfyIncludeHistoryCheckBox = new QCheckBox("History-JSON in Ausgabe einbetten", comfyPage);
    m_toolComfyPollIntervalSpin = new QSpinBox(comfyPage);
    m_toolComfyPollIntervalSpin->setRange(200, 60000);
    m_toolComfyPollIntervalSpin->setSingleStep(100);
    m_toolComfyPollIntervalSpin->setValue(1500);
    m_toolComfyPollIntervalSpin->setSuffix(" ms");
    m_toolComfyTimeoutSpin = new QSpinBox(comfyPage);
    m_toolComfyTimeoutSpin->setRange(0, 3600000);
    m_toolComfyTimeoutSpin->setSingleStep(1000);
    m_toolComfyTimeoutSpin->setValue(0);
    m_toolComfyTimeoutSpin->setSpecialValueText("Ohne lokales Timeout");
    m_toolComfyTimeoutSpin->setSuffix(" ms");
    auto* comfyCommonLayout = new QFormLayout();
    comfyCommonLayout->setLabelAlignment(Qt::AlignLeft);
    comfyCommonLayout->addRow("Output-Ordner", m_toolComfyOutputDirEdit);
    comfyCommonLayout->addRow("", m_toolComfyDownloadImagesCheckBox);
    comfyCommonLayout->addRow("", m_toolComfyIncludeHistoryCheckBox);
    comfyCommonLayout->addRow("Polling", m_toolComfyPollIntervalSpin);
    comfyCommonLayout->addRow("Timeout", m_toolComfyTimeoutSpin);
    comfyLayout->addLayout(comfyMetaRow);
    comfyLayout->addLayout(comfyModeLayout);
    comfyLayout->addWidget(m_toolComfyModeStack);
    comfyLayout->addLayout(comfyCommonLayout);
    m_toolConfigStack->addWidget(comfyPage);

    toolLayout->addRow("Tool", m_toolNameCombo);
    toolLayout->addRow("Output-Variable", m_toolOutputEdit);
    toolLayout->addRow("Konfiguration", m_toolConfigStack);
    m_visualStepConfigStack->addWidget(toolPage);

    m_visualEditorStatusLabel = new QLabel("Status: Visueller Editor bereit.", visualDetailCard);
    m_visualEditorStatusLabel->setProperty("sectionBody", true);
    m_visualEditorStatusLabel->setWordWrap(true);

    visualDetailLayout->addLayout(visualFormLayout);
    visualDetailLayout->addWidget(m_visualStepConfigStack, 1);
    visualDetailLayout->addWidget(m_visualEditorStatusLabel);
    visualDetailLayout->addStretch();
    visualDetailScrollArea->setWidget(visualDetailCard);

    visualSplitter->setStretchFactor(0, 2);
    visualSplitter->setStretchFactor(1, 3);

    visualEditorLayout->addWidget(visualEditorTitle);
    visualEditorLayout->addWidget(visualEditorBody);
    visualEditorLayout->addWidget(visualSplitter, 1);

    m_jsonDefinitionLabel = new QLabel("JSON-Definition", editorCard);
    m_jsonDefinitionLabel->setProperty("sectionBody", true);

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
    editorLayout->addWidget(m_jsonDefinitionLabel);
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

    connect(addToolButton, &QPushButton::clicked, this, [this]() {
        addVisualStep("tool");
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
    connect(m_toolNameCombo, &QComboBox::currentTextChanged, this, [this]() {
        updateVisualToolConfigPage();
        scheduleVisualStepApply();
    });
    connect(m_toolOutputEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileReadPathEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileReadLineStartSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolFileReadLineEndSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolFileReadMaxCharsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolJsonInputEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolJsonPathEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolJsonPrettyCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvPathEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvDelimiterCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvHasHeaderCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvMaxRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvOutputFormatCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvSourceFormatCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvCreateDirsCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvReturnContentCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolCsvContentEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadPathEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadExtensionsEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadExcludeEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadModifiedAfterEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryIngestModeCombo, &QComboBox::currentTextChanged, this, [this]() {
        updateVisualToolConfigPage();
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryIngestTypeEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryIngestSourceEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryIngestTagsEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryIngestRelevanceSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadMaxFilesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadWithinMinutesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadMaxCharsPerFileSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadMaxTotalCharsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadIncludeHiddenCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryReadSkipBinaryCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryListPathEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryListExtensionsEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryListExcludeEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryListMaxEntriesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryListRecursiveCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryListIncludeHiddenCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolDirectoryListDirectoriesOnlyCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryQueryEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryTypeFilterEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryTagsFilterEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryMaxCharsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryFormatCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemorySummaryPromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemorySummarySystemPromptEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemorySaveSummaryCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemorySummaryTypeEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemorySummarySourceEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemorySummaryTagsEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolMemorySummaryRelevanceSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryDeleteOlderThanDaysSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryDeleteKeepLatestSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryDeleteKeepRelevanceSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolMemoryDeleteDryRunCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileWritePathEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileWriteModeCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileWriteCreateDirsCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileWriteReturnContentCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileWriteContentEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileEditPathEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileEditReturnContentCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolFileEditDiffEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolHttpUrlEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolHttpMethodCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolHttpTimeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolHttpBodyJsonCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolHttpHeadersEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolHttpBodyEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolShellCommandEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolShellWorkingDirEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolShellTimeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolShellMaxOutputCharsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolShellIncludeStderrCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyRefreshButton, &QPushButton::clicked, this, [this]() {
        refreshComfyMetadata(true);
    });
    connect(m_toolComfyModeCombo, &QComboBox::currentTextChanged, this, [this]() {
        updateVisualToolConfigPage();
        scheduleVisualStepApply();
    });
    connect(m_toolComfyCheckpointCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyVaeCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyImageCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyMaskImageCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyMaskChannelCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyMaskGrowSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyPositivePromptEdit, &QTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyNegativePromptEdit, &QTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyBatchSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyStepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfySeedSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyRandomizeSeedCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyCfgSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyDenoiseSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfySamplerCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfySchedulerCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyClipSkipSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyFilenamePrefixEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyAddLoraButton, &QPushButton::clicked, this, [this]() {
        addComfyLoraRow();
        scheduleVisualStepApply();
    });
    connect(m_toolComfyRemoveLoraButton, &QPushButton::clicked, this, [this]() {
        removeSelectedComfyLoraRow();
        scheduleVisualStepApply();
    });
    connect(m_toolComfyLoraTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyWorkflowEdit, &QPlainTextEdit::textChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyOutputDirEdit, &QLineEdit::textEdited, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyDownloadImagesCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyIncludeHistoryCheckBox, &QCheckBox::toggled, this, [this]() {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyPollIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        scheduleVisualStepApply();
    });
    connect(m_toolComfyTimeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
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
    applyComfyCatalogToUi();
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
    runContext.variables.insert("workspace_root", m_settingsService.workspaceRoot());
    runContext.variables.insert("comfyui_base_url", m_settingsService.comfyUiBaseUrl());

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

    const QString workspaceRoot = m_settingsService.workspaceRoot();
    const QString comfyUiBaseUrl = m_settingsService.comfyUiBaseUrl();

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

    watcher->setFuture(QtConcurrent::run([workflow, runContext, providerBaseUrl, providerName, workspaceRoot, comfyUiBaseUrl]() {
        return executeWorkflowWithProvider(
            providerName,
            providerBaseUrl,
            workspaceRoot,
            comfyUiBaseUrl,
            workflow,
            runContext
        );
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

    const bool visualEditorEnabled = m_visualEditorToggle->isChecked();
    m_visualEditorFrame->setVisible(visualEditorEnabled);

    if (m_jsonDefinitionLabel != nullptr) {
        m_jsonDefinitionLabel->setVisible(!visualEditorEnabled);
    }

    if (m_definitionEdit != nullptr) {
        m_definitionEdit->setVisible(!visualEditorEnabled);
    }
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
        const QSignalBlocker toolNameBlocker(m_toolNameCombo);
        const QSignalBlocker toolOutputBlocker(m_toolOutputEdit);
        const QSignalBlocker toolFileReadPathBlocker(m_toolFileReadPathEdit);
        const QSignalBlocker toolFileReadLineStartBlocker(m_toolFileReadLineStartSpin);
        const QSignalBlocker toolFileReadLineEndBlocker(m_toolFileReadLineEndSpin);
        const QSignalBlocker toolFileReadMaxCharsBlocker(m_toolFileReadMaxCharsSpin);
        const QSignalBlocker toolJsonInputBlocker(m_toolJsonInputEdit);
        const QSignalBlocker toolJsonPathBlocker(m_toolJsonPathEdit);
        const QSignalBlocker toolJsonPrettyBlocker(m_toolJsonPrettyCheckBox);
        const QSignalBlocker toolCsvPathBlocker(m_toolCsvPathEdit);
        const QSignalBlocker toolCsvDelimiterBlocker(m_toolCsvDelimiterCombo);
        const QSignalBlocker toolCsvHeaderBlocker(m_toolCsvHasHeaderCheckBox);
        const QSignalBlocker toolCsvMaxRowsBlocker(m_toolCsvMaxRowsSpin);
        const QSignalBlocker toolCsvOutputFormatBlocker(m_toolCsvOutputFormatCombo);
        const QSignalBlocker toolCsvSourceFormatBlocker(m_toolCsvSourceFormatCombo);
        const QSignalBlocker toolCsvCreateDirsBlocker(m_toolCsvCreateDirsCheckBox);
        const QSignalBlocker toolCsvReturnContentBlocker(m_toolCsvReturnContentCheckBox);
        const QSignalBlocker toolCsvContentBlocker(m_toolCsvContentEdit);
        const QSignalBlocker toolDirectoryReadPathBlocker(m_toolDirectoryReadPathEdit);
        const QSignalBlocker toolDirectoryReadExtensionsBlocker(m_toolDirectoryReadExtensionsEdit);
        const QSignalBlocker toolDirectoryReadExcludeBlocker(m_toolDirectoryReadExcludeEdit);
        const QSignalBlocker toolDirectoryReadModifiedAfterBlocker(m_toolDirectoryReadModifiedAfterEdit);
        const QSignalBlocker toolMemoryIngestModeBlocker(m_toolMemoryIngestModeCombo);
        const QSignalBlocker toolMemoryIngestTypeBlocker(m_toolMemoryIngestTypeEdit);
        const QSignalBlocker toolMemoryIngestSourceBlocker(m_toolMemoryIngestSourceEdit);
        const QSignalBlocker toolMemoryIngestTagsBlocker(m_toolMemoryIngestTagsEdit);
        const QSignalBlocker toolMemoryIngestRelevanceBlocker(m_toolMemoryIngestRelevanceSpin);
        const QSignalBlocker toolDirectoryReadMaxFilesBlocker(m_toolDirectoryReadMaxFilesSpin);
        const QSignalBlocker toolDirectoryReadWithinMinutesBlocker(m_toolDirectoryReadWithinMinutesSpin);
        const QSignalBlocker toolDirectoryReadMaxCharsPerFileBlocker(m_toolDirectoryReadMaxCharsPerFileSpin);
        const QSignalBlocker toolDirectoryReadMaxTotalCharsBlocker(m_toolDirectoryReadMaxTotalCharsSpin);
        const QSignalBlocker toolDirectoryReadIncludeHiddenBlocker(m_toolDirectoryReadIncludeHiddenCheckBox);
        const QSignalBlocker toolDirectoryReadSkipBinaryBlocker(m_toolDirectoryReadSkipBinaryCheckBox);
        const QSignalBlocker toolDirectoryListPathBlocker(m_toolDirectoryListPathEdit);
        const QSignalBlocker toolDirectoryListExtensionsBlocker(m_toolDirectoryListExtensionsEdit);
        const QSignalBlocker toolDirectoryListExcludeBlocker(m_toolDirectoryListExcludeEdit);
        const QSignalBlocker toolDirectoryListMaxEntriesBlocker(m_toolDirectoryListMaxEntriesSpin);
        const QSignalBlocker toolDirectoryListRecursiveBlocker(m_toolDirectoryListRecursiveCheckBox);
        const QSignalBlocker toolDirectoryListIncludeHiddenBlocker(m_toolDirectoryListIncludeHiddenCheckBox);
        const QSignalBlocker toolDirectoryListDirectoriesOnlyBlocker(m_toolDirectoryListDirectoriesOnlyCheckBox);
        const QSignalBlocker toolMemoryQueryBlocker(m_toolMemoryQueryEdit);
        const QSignalBlocker toolMemoryTypeFilterBlocker(m_toolMemoryTypeFilterEdit);
        const QSignalBlocker toolMemoryTagsFilterBlocker(m_toolMemoryTagsFilterEdit);
        const QSignalBlocker toolMemoryLimitBlocker(m_toolMemoryLimitSpin);
        const QSignalBlocker toolMemoryMaxCharsBlocker(m_toolMemoryMaxCharsSpin);
        const QSignalBlocker toolMemoryFormatBlocker(m_toolMemoryFormatCombo);
        const QSignalBlocker toolMemorySummaryPromptBlocker(m_toolMemorySummaryPromptEdit);
        const QSignalBlocker toolMemorySummarySystemPromptBlocker(m_toolMemorySummarySystemPromptEdit);
        const QSignalBlocker toolMemorySaveSummaryBlocker(m_toolMemorySaveSummaryCheckBox);
        const QSignalBlocker toolMemorySummaryTypeBlocker(m_toolMemorySummaryTypeEdit);
        const QSignalBlocker toolMemorySummarySourceBlocker(m_toolMemorySummarySourceEdit);
        const QSignalBlocker toolMemorySummaryTagsBlocker(m_toolMemorySummaryTagsEdit);
        const QSignalBlocker toolMemorySummaryRelevanceBlocker(m_toolMemorySummaryRelevanceSpin);
        const QSignalBlocker toolMemoryDeleteOlderThanDaysBlocker(m_toolMemoryDeleteOlderThanDaysSpin);
        const QSignalBlocker toolMemoryDeleteKeepLatestBlocker(m_toolMemoryDeleteKeepLatestSpin);
        const QSignalBlocker toolMemoryDeleteKeepRelevanceBlocker(m_toolMemoryDeleteKeepRelevanceSpin);
        const QSignalBlocker toolMemoryDeleteDryRunBlocker(m_toolMemoryDeleteDryRunCheckBox);
        const QSignalBlocker toolFileWritePathBlocker(m_toolFileWritePathEdit);
        const QSignalBlocker toolFileWriteModeBlocker(m_toolFileWriteModeCombo);
        const QSignalBlocker toolFileWriteCreateDirsBlocker(m_toolFileWriteCreateDirsCheckBox);
        const QSignalBlocker toolFileWriteReturnContentBlocker(m_toolFileWriteReturnContentCheckBox);
        const QSignalBlocker toolFileWriteContentBlocker(m_toolFileWriteContentEdit);
        const QSignalBlocker toolFileEditPathBlocker(m_toolFileEditPathEdit);
        const QSignalBlocker toolFileEditReturnContentBlocker(m_toolFileEditReturnContentCheckBox);
        const QSignalBlocker toolFileEditDiffBlocker(m_toolFileEditDiffEdit);
        const QSignalBlocker toolHttpUrlBlocker(m_toolHttpUrlEdit);
        const QSignalBlocker toolHttpMethodBlocker(m_toolHttpMethodCombo);
        const QSignalBlocker toolHttpTimeoutBlocker(m_toolHttpTimeoutSpin);
        const QSignalBlocker toolHttpBodyJsonBlocker(m_toolHttpBodyJsonCheckBox);
        const QSignalBlocker toolHttpHeadersBlocker(m_toolHttpHeadersEdit);
        const QSignalBlocker toolHttpBodyBlocker(m_toolHttpBodyEdit);
        const QSignalBlocker toolShellCommandBlocker(m_toolShellCommandEdit);
        const QSignalBlocker toolShellWorkingDirBlocker(m_toolShellWorkingDirEdit);
        const QSignalBlocker toolShellTimeoutBlocker(m_toolShellTimeoutSpin);
        const QSignalBlocker toolShellMaxOutputCharsBlocker(m_toolShellMaxOutputCharsSpin);
        const QSignalBlocker toolShellIncludeStderrBlocker(m_toolShellIncludeStderrCheckBox);
        const QSignalBlocker toolComfyModeBlocker(m_toolComfyModeCombo);
        const QSignalBlocker toolComfyCheckpointBlocker(m_toolComfyCheckpointCombo);
        const QSignalBlocker toolComfyVaeBlocker(m_toolComfyVaeCombo);
        const QSignalBlocker toolComfyImageBlocker(m_toolComfyImageCombo);
        const QSignalBlocker toolComfyMaskImageBlocker(m_toolComfyMaskImageCombo);
        const QSignalBlocker toolComfyMaskChannelBlocker(m_toolComfyMaskChannelCombo);
        const QSignalBlocker toolComfyMaskGrowBlocker(m_toolComfyMaskGrowSpin);
        const QSignalBlocker toolComfyPositivePromptBlocker(m_toolComfyPositivePromptEdit);
        const QSignalBlocker toolComfyNegativePromptBlocker(m_toolComfyNegativePromptEdit);
        const QSignalBlocker toolComfyWidthBlocker(m_toolComfyWidthSpin);
        const QSignalBlocker toolComfyHeightBlocker(m_toolComfyHeightSpin);
        const QSignalBlocker toolComfyBatchBlocker(m_toolComfyBatchSizeSpin);
        const QSignalBlocker toolComfyStepsBlocker(m_toolComfyStepsSpin);
        const QSignalBlocker toolComfySeedBlocker(m_toolComfySeedSpin);
        const QSignalBlocker toolComfyRandomizeSeedBlocker(m_toolComfyRandomizeSeedCheckBox);
        const QSignalBlocker toolComfyCfgBlocker(m_toolComfyCfgSpin);
        const QSignalBlocker toolComfyDenoiseBlocker(m_toolComfyDenoiseSpin);
        const QSignalBlocker toolComfySamplerBlocker(m_toolComfySamplerCombo);
        const QSignalBlocker toolComfySchedulerBlocker(m_toolComfySchedulerCombo);
        const QSignalBlocker toolComfyClipSkipBlocker(m_toolComfyClipSkipSpin);
        const QSignalBlocker toolComfyFilenamePrefixBlocker(m_toolComfyFilenamePrefixEdit);
        const QSignalBlocker toolComfyWorkflowBlocker(m_toolComfyWorkflowEdit);
        const QSignalBlocker toolComfyOutputDirBlocker(m_toolComfyOutputDirEdit);
        const QSignalBlocker toolComfyDownloadBlocker(m_toolComfyDownloadImagesCheckBox);
        const QSignalBlocker toolComfyHistoryBlocker(m_toolComfyIncludeHistoryCheckBox);
        const QSignalBlocker toolComfyPollBlocker(m_toolComfyPollIntervalSpin);
        const QSignalBlocker toolComfyTimeoutBlocker(m_toolComfyTimeoutSpin);

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

        const QString toolName = config.value("tool").toString();
        const int toolIndex = m_toolNameCombo->findText(toolName);
        m_toolNameCombo->setCurrentIndex(toolIndex >= 0 ? toolIndex : 0);
        m_toolOutputEdit->setText(config.value("output").toString());
        m_toolFileReadPathEdit->setText(config.value("path").toString());
        m_toolFileReadLineStartSpin->setValue(qMax(1, config.value("line_start").toInt(1)));
        m_toolFileReadLineEndSpin->setValue(qMax(0, config.value("line_end").toInt(0)));
        m_toolFileReadMaxCharsSpin->setValue(qMax(0, config.value("max_chars").toInt(20000)));
        m_toolJsonInputEdit->setText(config.value("input").toString());
        m_toolJsonPathEdit->setText(config.value("path").toString());
        m_toolJsonPrettyCheckBox->setChecked(config.contains("pretty") ? config.value("pretty").toBool(true) : true);
        m_toolCsvPathEdit->setText(config.value("path").toString());
        m_toolCsvDelimiterCombo->setCurrentText(config.value("delimiter").toString().trimmed().isEmpty()
            ? ","
            : config.value("delimiter").toString());
        m_toolCsvHasHeaderCheckBox->setChecked(config.contains("has_header") ? config.value("has_header").toBool(true) : true);
        m_toolCsvMaxRowsSpin->setValue(qMax(1, config.value("max_rows").toInt(200)));
        {
            const QString outputFormat = config.value("output_format").toString().trimmed().toLower();
            const int index = m_toolCsvOutputFormatCombo->findData(outputFormat.isEmpty() ? "json" : outputFormat);
            m_toolCsvOutputFormatCombo->setCurrentIndex(index >= 0 ? index : 0);
        }
        {
            const QString sourceFormat = config.value("source_format").toString().trimmed().toLower();
            const int index = m_toolCsvSourceFormatCombo->findData(sourceFormat.isEmpty() ? "rows_json" : sourceFormat);
            m_toolCsvSourceFormatCombo->setCurrentIndex(index >= 0 ? index : 0);
        }
        m_toolCsvCreateDirsCheckBox->setChecked(config.contains("create_dirs") ? config.value("create_dirs").toBool(true) : true);
        m_toolCsvReturnContentCheckBox->setChecked(config.value("return_content").toBool(false));
        m_toolCsvContentEdit->setPlainText(config.value("content").toString());
        m_toolDirectoryReadPathEdit->setText(config.value("path").toString());
        if (config.value("include_extensions").isArray()) {
            QStringList extensions;
            const QJsonArray extensionArray = config.value("include_extensions").toArray();
            for (const QJsonValue& value : extensionArray) {
                const QString extension = value.toString().trimmed();
                if (!extension.isEmpty()) {
                    extensions.append(extension);
                }
            }
            m_toolDirectoryReadExtensionsEdit->setText(extensions.join(", "));
        } else {
            m_toolDirectoryReadExtensionsEdit->setText(config.value("include_extensions").toString());
        }
        if (config.value("exclude_paths").isArray()) {
            QStringList exclusions;
            const QJsonArray excludeArray = config.value("exclude_paths").toArray();
            for (const QJsonValue& value : excludeArray) {
                const QString exclusion = value.toString().trimmed();
                if (!exclusion.isEmpty()) {
                    exclusions.append(exclusion);
                }
            }
            m_toolDirectoryReadExcludeEdit->setText(exclusions.join(", "));
        } else {
            m_toolDirectoryReadExcludeEdit->setText(config.value("exclude_paths").toString());
        }
        m_toolDirectoryReadModifiedAfterEdit->setText(config.value("modified_after_iso").toString());
        const QString ingestMode = config.value("mode").toString().trimmed().toLower();
        const QString resolvedIngestMode = ingestMode.isEmpty()
            ? ((config.contains("within_minutes") || config.contains("modified_after_iso")) ? "changed" : "recursive")
            : ingestMode;
        const int ingestModeIndex = m_toolMemoryIngestModeCombo->findData(resolvedIngestMode);
        m_toolMemoryIngestModeCombo->setCurrentIndex(ingestModeIndex >= 0 ? ingestModeIndex : 0);
        m_toolMemoryIngestTypeEdit->setText(
            config.value("entry_type").toString().trimmed().isEmpty()
                ? "artifact"
                : config.value("entry_type").toString()
        );
        m_toolMemoryIngestSourceEdit->setText(config.value("source").toString());
        if (config.value("tags").isArray()) {
            QStringList tags;
            const QJsonArray tagArray = config.value("tags").toArray();
            for (const QJsonValue& value : tagArray) {
                const QString tag = value.toString().trimmed();
                if (!tag.isEmpty()) {
                    tags.append(tag);
                }
            }
            m_toolMemoryIngestTagsEdit->setText(tags.join(", "));
        } else {
            m_toolMemoryIngestTagsEdit->setText(config.value("tags").toString());
        }
        m_toolMemoryIngestRelevanceSpin->setValue(qBound(0, config.value("relevance").toInt(80), 100));
        m_toolDirectoryReadMaxFilesSpin->setValue(qMax(1, config.value("max_files").toInt(40)));
        m_toolDirectoryReadWithinMinutesSpin->setValue(qMax(0, config.value("within_minutes").toInt(0)));
        m_toolDirectoryReadMaxCharsPerFileSpin->setValue(qMax(0, config.value("max_chars_per_file").toInt(8000)));
        m_toolDirectoryReadMaxTotalCharsSpin->setValue(qMax(0, config.value("max_total_chars").toInt(120000)));
        m_toolDirectoryReadIncludeHiddenCheckBox->setChecked(config.value("include_hidden").toBool(false));
        m_toolDirectoryReadSkipBinaryCheckBox->setChecked(
            config.contains("skip_binary") ? config.value("skip_binary").toBool(true) : true
        );
        m_toolDirectoryListPathEdit->setText(config.value("path").toString());
        if (config.value("include_extensions").isArray()) {
            QStringList extensions;
            const QJsonArray extensionArray = config.value("include_extensions").toArray();
            for (const QJsonValue& value : extensionArray) {
                const QString extension = value.toString().trimmed();
                if (!extension.isEmpty()) {
                    extensions.append(extension);
                }
            }
            m_toolDirectoryListExtensionsEdit->setText(extensions.join(", "));
        } else {
            m_toolDirectoryListExtensionsEdit->setText(config.value("include_extensions").toString());
        }
        if (config.value("exclude_paths").isArray()) {
            QStringList exclusions;
            const QJsonArray excludeArray = config.value("exclude_paths").toArray();
            for (const QJsonValue& value : excludeArray) {
                const QString exclusion = value.toString().trimmed();
                if (!exclusion.isEmpty()) {
                    exclusions.append(exclusion);
                }
            }
            m_toolDirectoryListExcludeEdit->setText(exclusions.join(", "));
        } else {
            m_toolDirectoryListExcludeEdit->setText(config.value("exclude_paths").toString());
        }
        m_toolDirectoryListMaxEntriesSpin->setValue(qMax(1, config.value("max_entries").toInt(200)));
        m_toolDirectoryListRecursiveCheckBox->setChecked(
            config.contains("recursive") ? config.value("recursive").toBool(true) : true
        );
        m_toolDirectoryListIncludeHiddenCheckBox->setChecked(config.value("include_hidden").toBool(false));
        m_toolDirectoryListDirectoriesOnlyCheckBox->setChecked(config.value("directories_only").toBool(false));
        m_toolMemoryQueryEdit->setText(config.value("query").toString());
        m_toolMemoryTypeFilterEdit->setText(config.value("entry_type").toString());
        if (config.value("tags").isArray()) {
            QStringList tags;
            const QJsonArray tagArray = config.value("tags").toArray();
            for (const QJsonValue& value : tagArray) {
                const QString tag = value.toString().trimmed();
                if (!tag.isEmpty()) {
                    tags.append(tag);
                }
            }
            m_toolMemoryTagsFilterEdit->setText(tags.join(", "));
        } else {
            m_toolMemoryTagsFilterEdit->setText(config.value("tags").toString());
        }
        m_toolMemoryLimitSpin->setValue(qMax(1, config.value("limit").toInt(10)));
        m_toolMemoryMaxCharsSpin->setValue(qMax(0, config.value("max_chars").toInt(16000)));
        {
            const QString format = config.value("format").toString().trimmed().toLower();
            const int index = m_toolMemoryFormatCombo->findData(format.isEmpty() ? "snippets" : format);
            m_toolMemoryFormatCombo->setCurrentIndex(index >= 0 ? index : 0);
        }
        m_toolMemorySummaryPromptEdit->setPlainText(config.value("prompt").toString());
        m_toolMemorySummarySystemPromptEdit->setText(config.value("system_prompt").toString());
        m_toolMemorySaveSummaryCheckBox->setChecked(
            config.contains("save_as_memory") ? config.value("save_as_memory").toBool(true) : true
        );
        m_toolMemorySummaryTypeEdit->setText(
            config.value("summary_entry_type").toString().trimmed().isEmpty()
                ? "summary"
                : config.value("summary_entry_type").toString()
        );
        m_toolMemorySummarySourceEdit->setText(config.value("summary_source").toString());
        if (config.value("summary_tags").isArray()) {
            QStringList summaryTags;
            const QJsonArray tagArray = config.value("summary_tags").toArray();
            for (const QJsonValue& value : tagArray) {
                const QString tag = value.toString().trimmed();
                if (!tag.isEmpty()) {
                    summaryTags.append(tag);
                }
            }
            m_toolMemorySummaryTagsEdit->setText(summaryTags.join(", "));
        } else {
            m_toolMemorySummaryTagsEdit->setText(config.value("summary_tags").toString());
        }
        m_toolMemorySummaryRelevanceSpin->setValue(qBound(0, config.value("summary_relevance").toInt(75), 100));
        m_toolMemoryDeleteOlderThanDaysSpin->setValue(qMax(1, config.value("older_than_days").toInt(30)));
        m_toolMemoryDeleteKeepLatestSpin->setValue(qMax(0, config.value("keep_latest").toInt(0)));
        m_toolMemoryDeleteKeepRelevanceSpin->setValue(
            qBound(0, config.value("keep_relevance_at_or_above").toInt(90), 100)
        );
        m_toolMemoryDeleteDryRunCheckBox->setChecked(
            config.contains("dry_run") ? config.value("dry_run").toBool(true) : true
        );
        m_toolFileWritePathEdit->setText(config.value("path").toString());
        const QString writeMode = config.value("mode").toString().trimmed().toLower();
        const int writeModeIndex = m_toolFileWriteModeCombo->findData(writeMode.isEmpty() ? "overwrite" : writeMode);
        m_toolFileWriteModeCombo->setCurrentIndex(writeModeIndex >= 0 ? writeModeIndex : 0);
        m_toolFileWriteCreateDirsCheckBox->setChecked(
            config.contains("create_dirs") ? config.value("create_dirs").toBool(true) : true
        );
        m_toolFileWriteReturnContentCheckBox->setChecked(config.value("return_content").toBool(false));
        m_toolFileWriteContentEdit->setPlainText(config.value("content").toString());
        m_toolFileEditPathEdit->setText(config.value("path").toString());
        m_toolFileEditReturnContentCheckBox->setChecked(config.value("return_content").toBool(false));
        const QString diffText = config.value("diff").toString().trimmed().isEmpty()
            ? config.value("patch").toString()
            : config.value("diff").toString();
        m_toolFileEditDiffEdit->setPlainText(diffText);
        m_toolHttpUrlEdit->setText(config.value("url").toString());
        const QString httpMethod = config.value("method").toString().trimmed().toUpper();
        const int httpMethodIndex = m_toolHttpMethodCombo->findText(httpMethod.isEmpty() ? "GET" : httpMethod);
        m_toolHttpMethodCombo->setCurrentIndex(httpMethodIndex >= 0 ? httpMethodIndex : 0);
        m_toolHttpTimeoutSpin->setValue(qMax(0, config.value("timeout_ms").toInt(30000)));
        if (config.value("headers").isObject()) {
            m_toolHttpHeadersEdit->setPlainText(
                QString::fromUtf8(QJsonDocument(config.value("headers").toObject()).toJson(QJsonDocument::Indented))
            );
        } else {
            m_toolHttpHeadersEdit->setPlainText(config.value("headers_json").toString());
        }
        if (config.value("body_json").isObject()) {
            m_toolHttpBodyJsonCheckBox->setChecked(true);
            m_toolHttpBodyEdit->setPlainText(
                QString::fromUtf8(QJsonDocument(config.value("body_json").toObject()).toJson(QJsonDocument::Indented))
            );
        } else if (config.value("body_json").isArray()) {
            m_toolHttpBodyJsonCheckBox->setChecked(true);
            m_toolHttpBodyEdit->setPlainText(
                QString::fromUtf8(QJsonDocument(config.value("body_json").toArray()).toJson(QJsonDocument::Indented))
            );
        } else if (!config.value("body_json").toString().trimmed().isEmpty()) {
            m_toolHttpBodyJsonCheckBox->setChecked(true);
            m_toolHttpBodyEdit->setPlainText(config.value("body_json").toString());
        } else {
            m_toolHttpBodyJsonCheckBox->setChecked(false);
            m_toolHttpBodyEdit->setPlainText(config.value("body").toString());
        }
        m_toolShellCommandEdit->setText(config.value("command").toString());
        m_toolShellWorkingDirEdit->setText(config.value("working_directory").toString());
        m_toolShellTimeoutSpin->setValue(qMax(0, config.value("timeout_ms").toInt(60000)));
        m_toolShellMaxOutputCharsSpin->setValue(qMax(0, config.value("max_output_chars").toInt(20000)));
        m_toolShellIncludeStderrCheckBox->setChecked(
            config.contains("include_stderr") ? config.value("include_stderr").toBool(true) : true
        );
        if (config.value("workflow").isObject()) {
            m_toolComfyWorkflowEdit->setPlainText(
                QString::fromUtf8(QJsonDocument(config.value("workflow").toObject()).toJson(QJsonDocument::Indented))
            );
        } else {
            m_toolComfyWorkflowEdit->setPlainText(config.value("workflow_json").toString());
        }
        const QString comfyMode = config.value("builder_mode").toString().trimmed().toLower() == "txt2img"
            ? "txt2img"
            : (config.value("workflow").isObject() || !config.value("workflow_json").toString().trimmed().isEmpty()
                ? "raw_json"
                : "txt2img");
        const int comfyModeIndex = m_toolComfyModeCombo->findData(comfyMode);
        m_toolComfyModeCombo->setCurrentIndex(comfyModeIndex >= 0 ? comfyModeIndex : 0);
        setComboItemsWithEditableText(m_toolComfyCheckpointCombo, m_comfyCatalog.checkpoints, config.value("checkpoint").toString());
        setComboItemsWithEditableText(m_toolComfyVaeCombo, m_comfyCatalog.vaes, config.value("vae_name").toString(), true);
        setComboItemsWithEditableText(m_toolComfyImageCombo, m_comfyCatalog.inputImages, config.value("input_image").toString());
        setComboItemsWithEditableText(m_toolComfyMaskImageCombo, m_comfyCatalog.inputImages, config.value("mask_image").toString());
        setComboItemsWithEditableText(m_toolComfyMaskChannelCombo, m_comfyCatalog.maskChannels, config.value("mask_channel").toString());
        m_toolComfyMaskGrowSpin->setValue(qMax(0, config.value("mask_grow_by").toInt(6)));
        m_toolComfyPositivePromptEdit->setPlainText(config.value("positive_prompt").toString());
        m_toolComfyNegativePromptEdit->setPlainText(config.value("negative_prompt").toString());
        m_toolComfyWidthSpin->setValue(qBound(16, config.value("width").toInt(m_comfyCatalog.widthDefault), 16384));
        m_toolComfyHeightSpin->setValue(qBound(16, config.value("height").toInt(m_comfyCatalog.heightDefault), 16384));
        m_toolComfyBatchSizeSpin->setValue(qMax(1, config.value("batch_size").toInt(m_comfyCatalog.batchDefault)));
        m_toolComfyStepsSpin->setValue(qMax(1, config.value("steps").toInt(m_comfyCatalog.stepsDefault)));
        m_toolComfySeedSpin->setValue(qMax(0, config.value("seed").toInt(0)));
        m_toolComfyRandomizeSeedCheckBox->setChecked(config.value("randomize_seed").toBool(false));
        m_toolComfyCfgSpin->setValue(config.value("cfg").toDouble(m_comfyCatalog.cfgDefault));
        m_toolComfyDenoiseSpin->setValue(config.value("denoise").toDouble(m_comfyCatalog.denoiseDefault));
        setComboItemsWithEditableText(m_toolComfySamplerCombo, m_comfyCatalog.samplers, config.value("sampler_name").toString());
        setComboItemsWithEditableText(m_toolComfySchedulerCombo, m_comfyCatalog.schedulers, config.value("scheduler").toString());
        m_toolComfyClipSkipSpin->setRange(m_comfyCatalog.clipLayerMin, m_comfyCatalog.clipLayerMax);
        m_toolComfyClipSkipSpin->setValue(config.value("clip_skip").toInt(m_comfyCatalog.clipLayerDefault));
        m_toolComfyFilenamePrefixEdit->setText(config.value("filename_prefix").toString());
        m_toolComfyLoraTable->setRowCount(0);
        const QJsonArray loraArray = config.value("loras").toArray();
        for (const QJsonValue& loraValue : loraArray) {
            if (!loraValue.isObject()) {
                continue;
            }
            const QJsonObject loraObject = loraValue.toObject();
            addComfyLoraRow(
                loraObject.value("name").toString(),
                loraObject.value("strength_model").toDouble(1.0),
                loraObject.value("strength_clip").toDouble(1.0),
                !loraObject.contains("enabled") || loraObject.value("enabled").toBool(true)
            );
        }
        m_toolComfyOutputDirEdit->setText(config.value("save_outputs_to").toString());
        m_toolComfyDownloadImagesCheckBox->setChecked(
            config.contains("download_images") ? config.value("download_images").toBool(false) : false
        );
        m_toolComfyIncludeHistoryCheckBox->setChecked(config.value("include_history_json").toBool(false));
        m_toolComfyPollIntervalSpin->setValue(qBound(200, config.value("poll_interval_ms").toInt(1500), 60000));
        m_toolComfyTimeoutSpin->setValue(qMax(0, config.value("timeout_ms").toInt(0)));
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
    if (m_toolNameCombo != nullptr) {
        m_toolNameCombo->setCurrentIndex(0);
        m_toolOutputEdit->clear();
        m_toolFileReadPathEdit->clear();
        m_toolFileReadLineStartSpin->setValue(1);
        m_toolFileReadLineEndSpin->setValue(0);
        m_toolFileReadMaxCharsSpin->setValue(20000);
        m_toolJsonInputEdit->clear();
        m_toolJsonPathEdit->clear();
        m_toolJsonPrettyCheckBox->setChecked(true);
        m_toolCsvPathEdit->clear();
        m_toolCsvDelimiterCombo->setCurrentText(",");
        m_toolCsvHasHeaderCheckBox->setChecked(true);
        m_toolCsvMaxRowsSpin->setValue(200);
        m_toolCsvOutputFormatCombo->setCurrentIndex(0);
        m_toolCsvSourceFormatCombo->setCurrentIndex(0);
        m_toolCsvCreateDirsCheckBox->setChecked(true);
        m_toolCsvReturnContentCheckBox->setChecked(false);
        m_toolCsvContentEdit->clear();
        m_toolDirectoryReadPathEdit->clear();
        m_toolDirectoryReadExtensionsEdit->clear();
        m_toolDirectoryReadExcludeEdit->clear();
        m_toolDirectoryReadModifiedAfterEdit->clear();
        m_toolMemoryIngestModeCombo->setCurrentIndex(0);
        m_toolMemoryIngestTypeEdit->setText("artifact");
        m_toolMemoryIngestSourceEdit->clear();
        m_toolMemoryIngestTagsEdit->clear();
        m_toolMemoryIngestRelevanceSpin->setValue(80);
        m_toolDirectoryReadMaxFilesSpin->setValue(40);
        m_toolDirectoryReadWithinMinutesSpin->setValue(0);
        m_toolDirectoryReadMaxCharsPerFileSpin->setValue(8000);
        m_toolDirectoryReadMaxTotalCharsSpin->setValue(120000);
        m_toolDirectoryReadIncludeHiddenCheckBox->setChecked(false);
        m_toolDirectoryReadSkipBinaryCheckBox->setChecked(true);
        m_toolDirectoryListPathEdit->clear();
        m_toolDirectoryListExtensionsEdit->clear();
        m_toolDirectoryListExcludeEdit->clear();
        m_toolDirectoryListMaxEntriesSpin->setValue(200);
        m_toolDirectoryListRecursiveCheckBox->setChecked(true);
        m_toolDirectoryListIncludeHiddenCheckBox->setChecked(false);
        m_toolDirectoryListDirectoriesOnlyCheckBox->setChecked(false);
        m_toolMemoryQueryEdit->clear();
        m_toolMemoryTypeFilterEdit->clear();
        m_toolMemoryTagsFilterEdit->clear();
        m_toolMemoryLimitSpin->setValue(10);
        m_toolMemoryMaxCharsSpin->setValue(16000);
        m_toolMemoryFormatCombo->setCurrentIndex(0);
        m_toolMemorySummaryPromptEdit->clear();
        m_toolMemorySummarySystemPromptEdit->clear();
        m_toolMemorySaveSummaryCheckBox->setChecked(true);
        m_toolMemorySummaryTypeEdit->setText("summary");
        m_toolMemorySummarySourceEdit->clear();
        m_toolMemorySummaryTagsEdit->clear();
        m_toolMemorySummaryRelevanceSpin->setValue(75);
        m_toolMemoryDeleteOlderThanDaysSpin->setValue(30);
        m_toolMemoryDeleteKeepLatestSpin->setValue(0);
        m_toolMemoryDeleteKeepRelevanceSpin->setValue(90);
        m_toolMemoryDeleteDryRunCheckBox->setChecked(true);
        m_toolFileWritePathEdit->clear();
        m_toolFileWriteModeCombo->setCurrentIndex(0);
        m_toolFileWriteCreateDirsCheckBox->setChecked(true);
        m_toolFileWriteReturnContentCheckBox->setChecked(false);
        m_toolFileWriteContentEdit->clear();
        m_toolFileEditPathEdit->clear();
        m_toolFileEditReturnContentCheckBox->setChecked(false);
        m_toolFileEditDiffEdit->clear();
        m_toolHttpUrlEdit->clear();
        m_toolHttpMethodCombo->setCurrentIndex(0);
        m_toolHttpTimeoutSpin->setValue(30000);
        m_toolHttpBodyJsonCheckBox->setChecked(false);
        m_toolHttpHeadersEdit->clear();
        m_toolHttpBodyEdit->clear();
        m_toolShellCommandEdit->clear();
        m_toolShellWorkingDirEdit->clear();
        m_toolShellTimeoutSpin->setValue(60000);
        m_toolShellMaxOutputCharsSpin->setValue(20000);
        m_toolShellIncludeStderrCheckBox->setChecked(true);
        m_toolComfyModeCombo->setCurrentIndex(0);
        m_toolComfyCheckpointCombo->clearEditText();
        m_toolComfyVaeCombo->clearEditText();
        m_toolComfyImageCombo->clearEditText();
        m_toolComfyMaskImageCombo->clearEditText();
        m_toolComfyMaskChannelCombo->clearEditText();
        m_toolComfyMaskGrowSpin->setValue(6);
        m_toolComfyPositivePromptEdit->clear();
        m_toolComfyNegativePromptEdit->clear();
        m_toolComfyWidthSpin->setValue(m_comfyCatalog.success ? m_comfyCatalog.widthDefault : 1024);
        m_toolComfyHeightSpin->setValue(m_comfyCatalog.success ? m_comfyCatalog.heightDefault : 1024);
        m_toolComfyBatchSizeSpin->setValue(m_comfyCatalog.success ? m_comfyCatalog.batchDefault : 1);
        m_toolComfyStepsSpin->setValue(m_comfyCatalog.success ? m_comfyCatalog.stepsDefault : 20);
        m_toolComfySeedSpin->setValue(0);
        m_toolComfyRandomizeSeedCheckBox->setChecked(false);
        m_toolComfyCfgSpin->setValue(m_comfyCatalog.success ? m_comfyCatalog.cfgDefault : 8.0);
        m_toolComfyDenoiseSpin->setValue(m_comfyCatalog.success ? m_comfyCatalog.denoiseDefault : 1.0);
        m_toolComfySamplerCombo->clearEditText();
        m_toolComfySchedulerCombo->clearEditText();
        if (m_toolComfyClipSkipSpin != nullptr) {
            m_toolComfyClipSkipSpin->setRange(
                m_comfyCatalog.success ? m_comfyCatalog.clipLayerMin : -24,
                m_comfyCatalog.success ? m_comfyCatalog.clipLayerMax : -1
            );
            m_toolComfyClipSkipSpin->setValue(m_comfyCatalog.success ? m_comfyCatalog.clipLayerDefault : -1);
        }
        m_toolComfyFilenamePrefixEdit->setText(m_comfyCatalog.success ? m_comfyCatalog.filenamePrefixDefault : "PrivateClaw");
        m_toolComfyLoraTable->setRowCount(0);
        m_toolComfyWorkflowEdit->clear();
        m_toolComfyOutputDirEdit->clear();
        m_toolComfyDownloadImagesCheckBox->setChecked(false);
        m_toolComfyIncludeHistoryCheckBox->setChecked(false);
        m_toolComfyPollIntervalSpin->setValue(1500);
        m_toolComfyTimeoutSpin->setValue(0);
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

    if (stepType == "tool") {
        m_visualStepConfigStack->setCurrentIndex(3);
        updateVisualToolConfigPage();
        return;
    }

    m_visualStepConfigStack->setCurrentIndex(0);
}

void WorkflowPanel::updateVisualToolConfigPage()
{
    if (m_toolConfigStack == nullptr || m_toolNameCombo == nullptr) {
        return;
    }

    const QString toolName = m_toolNameCombo->currentText().trimmed().toLower();
    const bool isMemoryIngest = toolName == "memory.ingest_directory";
    const bool isMemorySearchTool = toolName == "memory.search";
    const bool isMemorySummarizeTool = toolName == "memory.summarize";
    const bool isMemoryDeleteTool = toolName == "memory.delete_old";
    const bool isComfyTool = toolName == "comfyui.workflow";
    const bool isCsvReadTool = toolName == "csv.read";
    const bool isCsvWriteTool = toolName == "csv.write";
    const bool usesChangedWindow = toolName == "directory.read_changed"
        || (isMemoryIngest && m_toolMemoryIngestModeCombo != nullptr
            && m_toolMemoryIngestModeCombo->currentData().toString().trimmed() == "changed");
    if (m_toolMemoryIngestModeCombo != nullptr) {
        m_toolMemoryIngestModeCombo->setEnabled(isMemoryIngest);
    }
    if (m_toolMemoryIngestTypeEdit != nullptr) {
        m_toolMemoryIngestTypeEdit->setEnabled(isMemoryIngest);
    }
    if (m_toolMemoryIngestSourceEdit != nullptr) {
        m_toolMemoryIngestSourceEdit->setEnabled(isMemoryIngest);
    }
    if (m_toolMemoryIngestTagsEdit != nullptr) {
        m_toolMemoryIngestTagsEdit->setEnabled(isMemoryIngest);
    }
    if (m_toolMemoryIngestRelevanceSpin != nullptr) {
        m_toolMemoryIngestRelevanceSpin->setEnabled(isMemoryIngest);
    }
    if (m_toolDirectoryReadModifiedAfterEdit != nullptr) {
        m_toolDirectoryReadModifiedAfterEdit->setEnabled(usesChangedWindow);
    }
    if (m_toolDirectoryReadWithinMinutesSpin != nullptr) {
        m_toolDirectoryReadWithinMinutesSpin->setEnabled(usesChangedWindow);
    }
    if (m_toolCsvMaxRowsSpin != nullptr) {
        m_toolCsvMaxRowsSpin->setEnabled(isCsvReadTool);
    }
    if (m_toolCsvOutputFormatCombo != nullptr) {
        m_toolCsvOutputFormatCombo->setEnabled(isCsvReadTool);
    }
    if (m_toolCsvSourceFormatCombo != nullptr) {
        m_toolCsvSourceFormatCombo->setEnabled(isCsvWriteTool);
    }
    if (m_toolCsvContentEdit != nullptr) {
        m_toolCsvContentEdit->setEnabled(isCsvWriteTool);
    }
    if (m_toolCsvCreateDirsCheckBox != nullptr) {
        m_toolCsvCreateDirsCheckBox->setEnabled(isCsvWriteTool);
    }
    if (m_toolCsvReturnContentCheckBox != nullptr) {
        m_toolCsvReturnContentCheckBox->setEnabled(isCsvWriteTool);
    }
    const bool usesMemorySearchFields = isMemorySearchTool || isMemorySummarizeTool || isMemoryDeleteTool;
    if (m_toolMemoryQueryEdit != nullptr) {
        m_toolMemoryQueryEdit->setEnabled(usesMemorySearchFields);
    }
    if (m_toolMemoryTypeFilterEdit != nullptr) {
        m_toolMemoryTypeFilterEdit->setEnabled(usesMemorySearchFields);
    }
    if (m_toolMemoryTagsFilterEdit != nullptr) {
        m_toolMemoryTagsFilterEdit->setEnabled(usesMemorySearchFields);
    }
    if (m_toolMemoryLimitSpin != nullptr) {
        m_toolMemoryLimitSpin->setEnabled(isMemorySearchTool || isMemorySummarizeTool);
    }
    if (m_toolMemoryMaxCharsSpin != nullptr) {
        m_toolMemoryMaxCharsSpin->setEnabled(isMemorySearchTool || isMemorySummarizeTool);
    }
    if (m_toolMemoryFormatCombo != nullptr) {
        m_toolMemoryFormatCombo->setEnabled(isMemorySearchTool);
    }
    if (m_toolMemorySummaryPromptEdit != nullptr) {
        m_toolMemorySummaryPromptEdit->setEnabled(isMemorySummarizeTool);
    }
    if (m_toolMemorySummarySystemPromptEdit != nullptr) {
        m_toolMemorySummarySystemPromptEdit->setEnabled(isMemorySummarizeTool);
    }
    if (m_toolMemorySaveSummaryCheckBox != nullptr) {
        m_toolMemorySaveSummaryCheckBox->setEnabled(isMemorySummarizeTool);
    }
    const bool canEditSummaryTarget = isMemorySummarizeTool
        && m_toolMemorySaveSummaryCheckBox != nullptr
        && m_toolMemorySaveSummaryCheckBox->isChecked();
    if (m_toolMemorySummaryTypeEdit != nullptr) {
        m_toolMemorySummaryTypeEdit->setEnabled(canEditSummaryTarget);
    }
    if (m_toolMemorySummarySourceEdit != nullptr) {
        m_toolMemorySummarySourceEdit->setEnabled(canEditSummaryTarget);
    }
    if (m_toolMemorySummaryTagsEdit != nullptr) {
        m_toolMemorySummaryTagsEdit->setEnabled(canEditSummaryTarget);
    }
    if (m_toolMemorySummaryRelevanceSpin != nullptr) {
        m_toolMemorySummaryRelevanceSpin->setEnabled(canEditSummaryTarget);
    }
    if (m_toolMemoryDeleteOlderThanDaysSpin != nullptr) {
        m_toolMemoryDeleteOlderThanDaysSpin->setEnabled(isMemoryDeleteTool);
    }
    if (m_toolMemoryDeleteKeepLatestSpin != nullptr) {
        m_toolMemoryDeleteKeepLatestSpin->setEnabled(isMemoryDeleteTool);
    }
    if (m_toolMemoryDeleteKeepRelevanceSpin != nullptr) {
        m_toolMemoryDeleteKeepRelevanceSpin->setEnabled(isMemoryDeleteTool);
    }
    if (m_toolMemoryDeleteDryRunCheckBox != nullptr) {
        m_toolMemoryDeleteDryRunCheckBox->setEnabled(isMemoryDeleteTool);
    }
    if (m_toolComfyModeStack != nullptr && m_toolComfyModeCombo != nullptr) {
        const QString comfyMode = m_toolComfyModeCombo->currentData().toString().trimmed();
        m_toolComfyModeStack->setCurrentIndex(comfyMode == "raw_json" ? 1 : 0);
        const bool usesInputImage = comfyMode == "img2img" || comfyMode == "inpainting";
        const bool usesMask = comfyMode == "inpainting";
        if (m_toolComfyImageCombo != nullptr) {
            m_toolComfyImageCombo->setEnabled(usesInputImage);
        }
        if (m_toolComfyMaskImageCombo != nullptr) {
            m_toolComfyMaskImageCombo->setEnabled(usesMask);
        }
        if (m_toolComfyMaskChannelCombo != nullptr) {
            m_toolComfyMaskChannelCombo->setEnabled(usesMask);
        }
        if (m_toolComfyMaskGrowSpin != nullptr) {
            m_toolComfyMaskGrowSpin->setEnabled(usesMask);
        }
    }
    if (m_toolComfyRefreshButton != nullptr) {
        m_toolComfyRefreshButton->setEnabled(!m_comfyMetadataLoading);
    }
    if (isComfyTool && !m_comfyMetadataLoaded && !m_comfyMetadataLoading) {
        refreshComfyMetadata(false);
    }
    if (toolName == "json.extract") {
        m_toolConfigStack->setCurrentIndex(1);
        return;
    }

    if (toolName == "csv.read" || toolName == "csv.write") {
        m_toolConfigStack->setCurrentIndex(2);
        return;
    }

    if (toolName == "directory.read_recursive"
        || toolName == "directory.read_changed"
        || toolName == "memory.ingest_directory") {
        m_toolConfigStack->setCurrentIndex(3);
        return;
    }

    if (toolName == "directory.list") {
        m_toolConfigStack->setCurrentIndex(4);
        return;
    }

    if (toolName == "memory.search" || toolName == "memory.summarize" || toolName == "memory.delete_old") {
        m_toolConfigStack->setCurrentIndex(5);
        return;
    }

    if (toolName == "file.write_text") {
        m_toolConfigStack->setCurrentIndex(6);
        return;
    }

    if (toolName == "file.edit_diff") {
        m_toolConfigStack->setCurrentIndex(7);
        return;
    }

    if (toolName == "http.request") {
        m_toolConfigStack->setCurrentIndex(8);
        return;
    }

    if (toolName == "shell.run") {
        m_toolConfigStack->setCurrentIndex(9);
        return;
    }

    if (toolName == "comfyui.workflow") {
        m_toolConfigStack->setCurrentIndex(10);
        return;
    }

    m_toolConfigStack->setCurrentIndex(0);
}

void WorkflowPanel::refreshComfyMetadata(const bool forceReload)
{
    if (m_comfyMetadataLoading) {
        return;
    }
    if (m_comfyMetadataLoaded && !forceReload) {
        applyComfyCatalogToUi();
        return;
    }

    m_comfyMetadataLoading = true;
    if (m_toolComfyRefreshButton != nullptr) {
        m_toolComfyRefreshButton->setEnabled(false);
    }
    if (m_toolComfyStatusLabel != nullptr) {
        m_toolComfyStatusLabel->setStyleSheet("color: #5f5548;");
        m_toolComfyStatusLabel->setText("ComfyUI-Daten werden geladen...");
    }

    const QString baseUrl = m_settingsService.comfyUiBaseUrl();
    auto* watcher = new QFutureWatcher<services::ComfyUiCatalog>(this);
    connect(
        watcher,
        &QFutureWatcher<services::ComfyUiCatalog>::finished,
        this,
        [this, watcher]() {
            m_comfyMetadataLoading = false;
            m_comfyCatalog = watcher->result();
            m_comfyMetadataLoaded = m_comfyCatalog.success;
            if (m_comfyCatalog.success) {
                applyComfyCatalogToUi();
            } else if (m_toolComfyStatusLabel != nullptr) {
                m_toolComfyStatusLabel->setStyleSheet("color: #8b2f2f;");
                m_toolComfyStatusLabel->setText(
                    QString("ComfyUI-Daten konnten nicht geladen werden: %1")
                        .arg(m_comfyCatalog.errorMessage)
                );
            }
            if (m_toolComfyRefreshButton != nullptr) {
                m_toolComfyRefreshButton->setEnabled(true);
            }
            watcher->deleteLater();
        }
    );

    watcher->setFuture(
        QtConcurrent::run([baseUrl]() {
            return services::ComfyUiMetadataService::fetchCatalog(baseUrl);
        })
    );
}

void WorkflowPanel::applyComfyCatalogToUi()
{
    if (!m_comfyCatalog.success) {
        return;
    }

    setComboItemsWithEditableText(m_toolComfyCheckpointCombo, m_comfyCatalog.checkpoints, m_toolComfyCheckpointCombo->currentText());
    setComboItemsWithEditableText(m_toolComfyVaeCombo, m_comfyCatalog.vaes, m_toolComfyVaeCombo->currentText(), true);
    setComboItemsWithEditableText(m_toolComfyImageCombo, m_comfyCatalog.inputImages, m_toolComfyImageCombo->currentText());
    setComboItemsWithEditableText(m_toolComfyMaskImageCombo, m_comfyCatalog.inputImages, m_toolComfyMaskImageCombo->currentText());
    setComboItemsWithEditableText(m_toolComfyMaskChannelCombo, m_comfyCatalog.maskChannels, m_toolComfyMaskChannelCombo->currentText());
    setComboItemsWithEditableText(m_toolComfySamplerCombo, m_comfyCatalog.samplers, m_toolComfySamplerCombo->currentText());
    setComboItemsWithEditableText(m_toolComfySchedulerCombo, m_comfyCatalog.schedulers, m_toolComfySchedulerCombo->currentText());

    if (m_toolComfyWidthSpin != nullptr && m_toolComfyWidthSpin->value() <= 16) {
        m_toolComfyWidthSpin->setValue(m_comfyCatalog.widthDefault);
    }
    if (m_toolComfyHeightSpin != nullptr && m_toolComfyHeightSpin->value() <= 16) {
        m_toolComfyHeightSpin->setValue(m_comfyCatalog.heightDefault);
    }
    if (m_toolComfyBatchSizeSpin != nullptr && m_toolComfyBatchSizeSpin->value() == 1) {
        m_toolComfyBatchSizeSpin->setValue(m_comfyCatalog.batchDefault);
    }
    if (m_toolComfyStepsSpin != nullptr && m_toolComfyStepsSpin->value() == 20) {
        m_toolComfyStepsSpin->setValue(m_comfyCatalog.stepsDefault);
    }
    if (m_toolComfyCfgSpin != nullptr && qFuzzyCompare(m_toolComfyCfgSpin->value(), 8.0)) {
        m_toolComfyCfgSpin->setValue(m_comfyCatalog.cfgDefault);
    }
    if (m_toolComfyDenoiseSpin != nullptr && qFuzzyCompare(m_toolComfyDenoiseSpin->value(), 1.0)) {
        m_toolComfyDenoiseSpin->setValue(m_comfyCatalog.denoiseDefault);
    }
    if (m_toolComfyClipSkipSpin != nullptr) {
        m_toolComfyClipSkipSpin->setRange(m_comfyCatalog.clipLayerMin, m_comfyCatalog.clipLayerMax);
        if (m_toolComfyClipSkipSpin->value() < m_comfyCatalog.clipLayerMin
            || m_toolComfyClipSkipSpin->value() > m_comfyCatalog.clipLayerMax) {
            m_toolComfyClipSkipSpin->setValue(m_comfyCatalog.clipLayerDefault);
        }
    }
    if (m_toolComfyFilenamePrefixEdit != nullptr && m_toolComfyFilenamePrefixEdit->text().trimmed().isEmpty()) {
        m_toolComfyFilenamePrefixEdit->setText(m_comfyCatalog.filenamePrefixDefault);
    }

    if (m_toolComfyLoraTable != nullptr) {
        for (int row = 0; row < m_toolComfyLoraTable->rowCount(); ++row) {
            if (auto* combo = qobject_cast<QComboBox*>(m_toolComfyLoraTable->cellWidget(row, 1)); combo != nullptr) {
                setComboItemsWithEditableText(combo, m_comfyCatalog.loras, combo->currentText());
            }
        }
    }

    if (m_toolComfyStatusLabel != nullptr) {
        m_toolComfyStatusLabel->setStyleSheet("color: #2f6b3a;");
        m_toolComfyStatusLabel->setText(
            QString("ComfyUI-Daten geladen: %1 Checkpoints, %2 LoRAs, %3 Input-Bilder.")
                .arg(m_comfyCatalog.checkpoints.size())
                .arg(m_comfyCatalog.loras.size())
                .arg(m_comfyCatalog.inputImages.size())
        );
    }
}

void WorkflowPanel::addComfyLoraRow(
    const QString& loraName,
    const double modelStrength,
    const double clipStrength,
    const bool enabled
)
{
    if (m_toolComfyLoraTable == nullptr) {
        return;
    }

    const int row = m_toolComfyLoraTable->rowCount();
    m_toolComfyLoraTable->insertRow(row);

    auto* enabledItem = new QTableWidgetItem();
    enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    enabledItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    m_toolComfyLoraTable->setItem(row, 0, enabledItem);

    auto* loraCombo = new QComboBox(m_toolComfyLoraTable);
    loraCombo->setEditable(true);
    setComboItemsWithEditableText(loraCombo, m_comfyCatalog.loras, loraName);
    connect(loraCombo, &QComboBox::currentTextChanged, this, [this]() {
        scheduleVisualStepApply();
    });
    m_toolComfyLoraTable->setCellWidget(row, 1, loraCombo);

    auto* modelSpin = new QDoubleSpinBox(m_toolComfyLoraTable);
    modelSpin->setRange(-100.0, 100.0);
    modelSpin->setDecimals(2);
    modelSpin->setSingleStep(0.05);
    modelSpin->setValue(modelStrength);
    connect(modelSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        scheduleVisualStepApply();
    });
    m_toolComfyLoraTable->setCellWidget(row, 2, modelSpin);

    auto* clipSpin = new QDoubleSpinBox(m_toolComfyLoraTable);
    clipSpin->setRange(-100.0, 100.0);
    clipSpin->setDecimals(2);
    clipSpin->setSingleStep(0.05);
    clipSpin->setValue(clipStrength);
    connect(clipSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        scheduleVisualStepApply();
    });
    m_toolComfyLoraTable->setCellWidget(row, 3, clipSpin);

    m_toolComfyLoraTable->setCurrentCell(row, 1);
}

void WorkflowPanel::removeSelectedComfyLoraRow()
{
    if (m_toolComfyLoraTable == nullptr) {
        return;
    }

    const int row = m_toolComfyLoraTable->currentRow();
    if (row < 0 || row >= m_toolComfyLoraTable->rowCount()) {
        return;
    }

    m_toolComfyLoraTable->removeRow(row);
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
                "default_next",
                "builder_mode",
                "checkpoint",
                "vae_name",
                "positive_prompt",
                "negative_prompt",
                    "width",
                    "height",
                    "batch_size",
                    "input_image",
                    "mask_image",
                    "mask_channel",
                    "mask_grow_by",
                    "steps",
                    "seed",
                "randomize_seed",
                "cfg",
                "denoise",
                "sampler_name",
                "scheduler",
                "clip_skip",
                "filename_prefix",
                "loras",
                "workflow",
                "workflow_json",
                "save_outputs_to",
                "download_images",
                "include_history_json",
                "poll_interval_ms",
                "timeout_ms",
                "mode",
                "modified_after_iso",
                "within_minutes"
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
                "default_next",
                "builder_mode",
                "checkpoint",
                "vae_name",
                "positive_prompt",
                "negative_prompt",
                    "width",
                    "height",
                    "batch_size",
                    "input_image",
                    "mask_image",
                    "mask_channel",
                    "mask_grow_by",
                    "steps",
                    "seed",
                "randomize_seed",
                "cfg",
                "denoise",
                "sampler_name",
                "scheduler",
                "clip_skip",
                "filename_prefix",
                "loras",
                "workflow",
                "workflow_json",
                "save_outputs_to",
                "download_images",
                "include_history_json",
                "poll_interval_ms",
                "timeout_ms",
                "mode",
                "modified_after_iso",
                "within_minutes"
            }
        );
        setJsonTextValue(&config, "content", m_memoryContentEdit->toPlainText());
        setJsonTextValue(&config, "entry_type", m_memoryTypeEdit->text());
        config.remove("memory_type");
        setJsonTextValue(&config, "source", m_memorySourceEdit->text());
        setJsonTextValue(&config, "tags", m_memoryTagsEdit->text());
        config.insert("relevance", m_memoryRelevanceSpin->value());
    } else if (stepType == "tool") {
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
                "default_next",
                "mode",
                "builder_mode",
                "checkpoint",
                "vae_name",
                "positive_prompt",
                "negative_prompt",
                    "width",
                    "height",
                    "batch_size",
                    "input_image",
                    "mask_image",
                    "mask_channel",
                    "mask_grow_by",
                    "steps",
                    "seed",
                "randomize_seed",
                "cfg",
                "denoise",
                "sampler_name",
                "scheduler",
                "clip_skip",
                "filename_prefix",
                "loras",
                "workflow",
                "workflow_json",
                "save_outputs_to",
                "download_images",
                "include_history_json",
                "poll_interval_ms",
                "timeout_ms",
                "path",
                "line_start",
                "line_end",
                "max_chars",
                "input",
                "pretty",
                "delimiter",
                "has_header",
                "max_rows",
                "output_format",
                "source_format",
                "include_extensions",
                "exclude_paths",
                "modified_after_iso",
                "within_minutes",
                "max_files",
                "max_chars_per_file",
                "max_total_chars",
                "include_hidden",
                "skip_binary",
                "recursive",
                "directories_only",
                "max_entries",
                "query",
                "limit",
                "format",
                "save_as_memory",
                "summary_entry_type",
                "summary_source",
                "summary_tags",
                "summary_relevance",
                "older_than_days",
                "keep_latest",
                "keep_relevance_at_or_above",
                "dry_run",
                "create_dirs",
                "return_content",
                "diff",
                "patch",
                "url",
                "method",
                "body",
                "body_json",
                "headers",
                "headers_json",
                "command",
                "working_directory",
                "include_stderr",
                "max_output_chars"
            }
        );
        const QString toolName = m_toolNameCombo->currentText().trimmed();
        setJsonTextValue(&config, "tool", toolName);
        setJsonTextValue(&config, "output", m_toolOutputEdit->text());

        if (toolName == "json.extract") {
            setJsonTextValue(&config, "input", m_toolJsonInputEdit->text());
            setJsonTextValue(&config, "path", m_toolJsonPathEdit->text());
            if (m_toolJsonPrettyCheckBox->isChecked()) {
                config.insert("pretty", true);
            } else {
                config.insert("pretty", false);
            }
        } else if (toolName == "csv.read") {
            setJsonTextValue(&config, "path", m_toolCsvPathEdit->text());
            setJsonTextValue(&config, "delimiter", m_toolCsvDelimiterCombo->currentText());
            if (m_toolCsvHasHeaderCheckBox->isChecked()) {
                config.insert("has_header", true);
            } else {
                config.insert("has_header", false);
            }
            config.insert("max_rows", m_toolCsvMaxRowsSpin->value());
            setJsonTextValue(&config, "output_format", m_toolCsvOutputFormatCombo->currentData().toString());
        } else if (toolName == "csv.write") {
            setJsonTextValue(&config, "path", m_toolCsvPathEdit->text());
            setJsonTextValue(&config, "delimiter", m_toolCsvDelimiterCombo->currentText());
            if (m_toolCsvHasHeaderCheckBox->isChecked()) {
                config.insert("has_header", true);
            } else {
                config.insert("has_header", false);
            }
            setJsonTextValue(&config, "source_format", m_toolCsvSourceFormatCombo->currentData().toString());
            setJsonTextValue(&config, "content", m_toolCsvContentEdit->toPlainText());
            if (m_toolCsvCreateDirsCheckBox->isChecked()) {
                config.insert("create_dirs", true);
            } else {
                config.insert("create_dirs", false);
            }
            if (m_toolCsvReturnContentCheckBox->isChecked()) {
                config.insert("return_content", true);
            } else {
                config.remove("return_content");
            }
        } else if (toolName == "directory.list") {
            removeConfigKeys(
                &config,
                {
                    "line_start",
                    "line_end",
                    "max_chars",
                    "modified_after_iso",
                    "within_minutes",
                    "max_files",
                    "max_chars_per_file",
                    "max_total_chars",
                    "skip_binary",
                    "mode",
                    "entry_type",
                    "source",
                    "relevance",
                    "diff",
                    "patch",
                    "return_content",
                    "workflow",
                    "workflow_json",
                    "save_outputs_to",
                    "download_images",
                    "include_history_json",
                    "poll_interval_ms",
                    "timeout_ms",
                    "create_dirs",
                    "url",
                    "method",
                    "body",
                    "body_json",
                    "headers",
                    "headers_json"
                }
            );
            setJsonTextValue(&config, "path", m_toolDirectoryListPathEdit->text());
            setJsonTextValue(&config, "include_extensions", m_toolDirectoryListExtensionsEdit->text());
            setJsonTextValue(&config, "exclude_paths", m_toolDirectoryListExcludeEdit->text());
            config.insert("max_entries", m_toolDirectoryListMaxEntriesSpin->value());
            if (m_toolDirectoryListRecursiveCheckBox->isChecked()) {
                config.insert("recursive", true);
            } else {
                config.insert("recursive", false);
            }
            if (m_toolDirectoryListIncludeHiddenCheckBox->isChecked()) {
                config.insert("include_hidden", true);
            } else {
                config.remove("include_hidden");
            }
            if (m_toolDirectoryListDirectoriesOnlyCheckBox->isChecked()) {
                config.insert("directories_only", true);
            } else {
                config.remove("directories_only");
            }
        } else if (toolName == "memory.search") {
            setJsonTextValue(&config, "query", m_toolMemoryQueryEdit->text());
            setJsonTextValue(&config, "entry_type", m_toolMemoryTypeFilterEdit->text());
            setJsonTextValue(&config, "tags", m_toolMemoryTagsFilterEdit->text());
            config.insert("limit", m_toolMemoryLimitSpin->value());
            if (m_toolMemoryMaxCharsSpin->value() > 0) {
                config.insert("max_chars", m_toolMemoryMaxCharsSpin->value());
            } else {
                config.remove("max_chars");
            }
            setJsonTextValue(&config, "format", m_toolMemoryFormatCombo->currentData().toString());
        } else if (toolName == "memory.summarize") {
            setJsonTextValue(&config, "query", m_toolMemoryQueryEdit->text());
            setJsonTextValue(&config, "entry_type", m_toolMemoryTypeFilterEdit->text());
            setJsonTextValue(&config, "tags", m_toolMemoryTagsFilterEdit->text());
            config.insert("limit", m_toolMemoryLimitSpin->value());
            if (m_toolMemoryMaxCharsSpin->value() > 0) {
                config.insert("max_chars", m_toolMemoryMaxCharsSpin->value());
            } else {
                config.remove("max_chars");
            }
            setJsonTextValue(&config, "prompt", m_toolMemorySummaryPromptEdit->toPlainText());
            setJsonTextValue(&config, "system_prompt", m_toolMemorySummarySystemPromptEdit->text());
            if (m_toolMemorySaveSummaryCheckBox->isChecked()) {
                config.insert("save_as_memory", true);
                setJsonTextValue(&config, "summary_entry_type", m_toolMemorySummaryTypeEdit->text());
                setJsonTextValue(&config, "summary_source", m_toolMemorySummarySourceEdit->text());
                setJsonTextValue(&config, "summary_tags", m_toolMemorySummaryTagsEdit->text());
                config.insert("summary_relevance", m_toolMemorySummaryRelevanceSpin->value());
            } else {
                config.insert("save_as_memory", false);
                removeConfigKeys(
                    &config,
                    { "summary_entry_type", "summary_source", "summary_tags", "summary_relevance" }
                );
            }
        } else if (toolName == "memory.delete_old") {
            setJsonTextValue(&config, "query", m_toolMemoryQueryEdit->text());
            setJsonTextValue(&config, "entry_type", m_toolMemoryTypeFilterEdit->text());
            setJsonTextValue(&config, "tags", m_toolMemoryTagsFilterEdit->text());
            config.insert("older_than_days", m_toolMemoryDeleteOlderThanDaysSpin->value());
            config.insert("keep_latest", m_toolMemoryDeleteKeepLatestSpin->value());
            config.insert("keep_relevance_at_or_above", m_toolMemoryDeleteKeepRelevanceSpin->value());
            if (m_toolMemoryDeleteDryRunCheckBox->isChecked()) {
                config.insert("dry_run", true);
            } else {
                config.insert("dry_run", false);
            }
        } else if (toolName == "file.write_text") {
            removeConfigKeys(
                &config,
                {
                    "include_extensions",
                    "exclude_paths",
                    "modified_after_iso",
                    "within_minutes",
                    "max_files",
                    "max_chars_per_file",
                    "max_total_chars",
                    "include_hidden",
                    "skip_binary",
                    "recursive",
                    "directories_only",
                    "max_entries",
                    "line_start",
                    "line_end",
                    "max_chars",
                    "diff",
                    "patch",
                    "workflow",
                    "workflow_json",
                    "save_outputs_to",
                    "download_images",
                    "include_history_json",
                    "poll_interval_ms",
                    "timeout_ms",
                    "url",
                    "method",
                    "body",
                    "body_json",
                    "headers",
                    "headers_json"
                }
            );
            setJsonTextValue(&config, "path", m_toolFileWritePathEdit->text());
            setJsonTextValue(&config, "content", m_toolFileWriteContentEdit->toPlainText());
            setJsonTextValue(&config, "mode", m_toolFileWriteModeCombo->currentData().toString());
            if (m_toolFileWriteCreateDirsCheckBox->isChecked()) {
                config.insert("create_dirs", true);
            } else {
                config.insert("create_dirs", false);
            }
            if (m_toolFileWriteReturnContentCheckBox->isChecked()) {
                config.insert("return_content", true);
            } else {
                config.remove("return_content");
            }
        } else if (toolName == "file.edit_diff") {
            removeConfigKeys(
                &config,
                {
                    "include_extensions",
                    "exclude_paths",
                    "mode",
                    "modified_after_iso",
                    "within_minutes",
                    "max_files",
                    "max_chars_per_file",
                    "max_total_chars",
                    "include_hidden",
                    "skip_binary",
                    "line_start",
                    "line_end",
                    "max_chars",
                    "workflow",
                    "workflow_json",
                    "save_outputs_to",
                    "download_images",
                    "include_history_json",
                    "poll_interval_ms",
                    "timeout_ms",
                    "recursive",
                    "directories_only",
                    "max_entries",
                    "create_dirs",
                    "url",
                    "method",
                    "body",
                    "body_json",
                    "headers",
                    "headers_json"
                }
            );
            setJsonTextValue(&config, "path", m_toolFileEditPathEdit->text());
            setJsonTextValue(&config, "diff", m_toolFileEditDiffEdit->toPlainText());
            config.remove("patch");
            if (m_toolFileEditReturnContentCheckBox->isChecked()) {
                config.insert("return_content", true);
            } else {
                config.remove("return_content");
            }
        } else if (toolName == "memory.ingest_directory") {
            removeConfigKeys(
                &config,
                {
                    "line_start",
                    "line_end",
                    "max_chars",
                    "diff",
                    "patch",
                    "return_content",
                    "workflow",
                    "workflow_json",
                    "save_outputs_to",
                    "download_images",
                    "include_history_json",
                    "poll_interval_ms",
                    "timeout_ms",
                    "recursive",
                    "directories_only",
                    "max_entries",
                    "create_dirs",
                    "url",
                    "method",
                    "body",
                    "body_json",
                    "headers",
                    "headers_json"
                }
            );
            setJsonTextValue(&config, "path", m_toolDirectoryReadPathEdit->text());
            setJsonTextValue(&config, "include_extensions", m_toolDirectoryReadExtensionsEdit->text());
            setJsonTextValue(&config, "exclude_paths", m_toolDirectoryReadExcludeEdit->text());
            setJsonTextValue(
                &config,
                "mode",
                m_toolMemoryIngestModeCombo->currentData().toString().trimmed()
            );
            if (m_toolMemoryIngestModeCombo->currentData().toString().trimmed() == "changed") {
                setJsonTextValue(&config, "modified_after_iso", m_toolDirectoryReadModifiedAfterEdit->text());
                if (m_toolDirectoryReadWithinMinutesSpin->value() > 0) {
                    config.insert("within_minutes", m_toolDirectoryReadWithinMinutesSpin->value());
                } else {
                    config.remove("within_minutes");
                }
            } else {
                config.remove("modified_after_iso");
                config.remove("within_minutes");
            }
            setJsonTextValue(&config, "entry_type", m_toolMemoryIngestTypeEdit->text());
            setJsonTextValue(&config, "source", m_toolMemoryIngestSourceEdit->text());
            setJsonTextValue(&config, "tags", m_toolMemoryIngestTagsEdit->text());
            config.insert("relevance", m_toolMemoryIngestRelevanceSpin->value());
            config.insert("max_files", m_toolDirectoryReadMaxFilesSpin->value());
            if (m_toolDirectoryReadMaxCharsPerFileSpin->value() > 0) {
                config.insert("max_chars_per_file", m_toolDirectoryReadMaxCharsPerFileSpin->value());
            } else {
                config.remove("max_chars_per_file");
            }
            if (m_toolDirectoryReadMaxTotalCharsSpin->value() > 0) {
                config.insert("max_total_chars", m_toolDirectoryReadMaxTotalCharsSpin->value());
            } else {
                config.remove("max_total_chars");
            }
            if (m_toolDirectoryReadIncludeHiddenCheckBox->isChecked()) {
                config.insert("include_hidden", true);
            } else {
                config.remove("include_hidden");
            }
            if (m_toolDirectoryReadSkipBinaryCheckBox->isChecked()) {
                config.insert("skip_binary", true);
            } else {
                config.insert("skip_binary", false);
            }
        } else if (toolName == "directory.read_recursive" || toolName == "directory.read_changed") {
            removeConfigKeys(
                &config,
                {
                    "line_start",
                    "line_end",
                    "max_chars",
                    "diff",
                    "patch",
                    "return_content",
                    "workflow",
                    "workflow_json",
                    "save_outputs_to",
                    "download_images",
                    "include_history_json",
                    "poll_interval_ms",
                    "timeout_ms",
                    "mode",
                    "recursive",
                    "directories_only",
                    "max_entries",
                    "create_dirs",
                    "url",
                    "method",
                    "body",
                    "body_json",
                    "headers",
                    "headers_json"
                }
            );
            setJsonTextValue(&config, "path", m_toolDirectoryReadPathEdit->text());
            setJsonTextValue(&config, "include_extensions", m_toolDirectoryReadExtensionsEdit->text());
            setJsonTextValue(&config, "exclude_paths", m_toolDirectoryReadExcludeEdit->text());
            if (toolName == "directory.read_changed") {
                setJsonTextValue(&config, "modified_after_iso", m_toolDirectoryReadModifiedAfterEdit->text());
                if (m_toolDirectoryReadWithinMinutesSpin->value() > 0) {
                    config.insert("within_minutes", m_toolDirectoryReadWithinMinutesSpin->value());
                } else {
                    config.remove("within_minutes");
                }
            } else {
                config.remove("modified_after_iso");
                config.remove("within_minutes");
            }
            config.insert("max_files", m_toolDirectoryReadMaxFilesSpin->value());
            if (m_toolDirectoryReadMaxCharsPerFileSpin->value() > 0) {
                config.insert("max_chars_per_file", m_toolDirectoryReadMaxCharsPerFileSpin->value());
            } else {
                config.remove("max_chars_per_file");
            }
            if (m_toolDirectoryReadMaxTotalCharsSpin->value() > 0) {
                config.insert("max_total_chars", m_toolDirectoryReadMaxTotalCharsSpin->value());
            } else {
                config.remove("max_total_chars");
            }
            if (m_toolDirectoryReadIncludeHiddenCheckBox->isChecked()) {
                config.insert("include_hidden", true);
            } else {
                config.remove("include_hidden");
            }
            if (m_toolDirectoryReadSkipBinaryCheckBox->isChecked()) {
                config.insert("skip_binary", true);
            } else {
                config.insert("skip_binary", false);
            }
        } else if (toolName == "http.request") {
            removeConfigKeys(
                &config,
                {
                    "path",
                    "include_extensions",
                    "exclude_paths",
                    "mode",
                    "modified_after_iso",
                    "within_minutes",
                    "max_files",
                    "max_chars_per_file",
                    "max_total_chars",
                    "include_hidden",
                    "skip_binary",
                    "recursive",
                    "directories_only",
                    "max_entries",
                    "line_start",
                    "line_end",
                    "max_chars",
                    "diff",
                    "patch",
                    "create_dirs",
                    "return_content",
                    "workflow",
                    "workflow_json",
                    "save_outputs_to",
                    "download_images",
                    "include_history_json",
                    "poll_interval_ms"
                }
            );
            setJsonTextValue(&config, "url", m_toolHttpUrlEdit->text());
            setJsonTextValue(&config, "method", m_toolHttpMethodCombo->currentText());
            const QString headersText = m_toolHttpHeadersEdit->toPlainText().trimmed();
            if (headersText.isEmpty()) {
                config.remove("headers_json");
                config.remove("headers");
            } else {
                setJsonTextValue(&config, "headers_json", headersText);
                config.remove("headers");
            }
            const QString bodyText = m_toolHttpBodyEdit->toPlainText();
            if (m_toolHttpBodyJsonCheckBox->isChecked()) {
                config.remove("body");
                setJsonTextValue(&config, "body_json", bodyText);
            } else {
                config.remove("body_json");
                setJsonTextValue(&config, "body", bodyText);
            }
            config.insert("timeout_ms", m_toolHttpTimeoutSpin->value());
        } else if (toolName == "shell.run") {
            setJsonTextValue(&config, "command", m_toolShellCommandEdit->text());
            setJsonTextValue(&config, "working_directory", m_toolShellWorkingDirEdit->text());
            config.insert("timeout_ms", m_toolShellTimeoutSpin->value());
            if (m_toolShellMaxOutputCharsSpin->value() > 0) {
                config.insert("max_output_chars", m_toolShellMaxOutputCharsSpin->value());
            } else {
                config.remove("max_output_chars");
            }
            if (m_toolShellIncludeStderrCheckBox->isChecked()) {
                config.insert("include_stderr", true);
            } else {
                config.insert("include_stderr", false);
            }
        } else if (toolName == "comfyui.workflow") {
            removeConfigKeys(
                &config,
                {
                    "path",
                    "include_extensions",
                    "exclude_paths",
                    "mode",
                    "modified_after_iso",
                    "within_minutes",
                    "max_files",
                    "max_chars_per_file",
                    "max_total_chars",
                    "include_hidden",
                    "skip_binary",
                    "recursive",
                    "directories_only",
                    "max_entries",
                    "line_start",
                    "line_end",
                    "max_chars",
                    "diff",
                    "patch",
                    "return_content",
                    "create_dirs",
                    "url",
                    "method",
                    "body",
                    "body_json",
                    "headers",
                    "headers_json"
                }
            );
            const QString comfyMode = m_toolComfyModeCombo->currentData().toString().trimmed();
            setJsonTextValue(&config, "builder_mode", comfyMode);
            if (comfyMode == "raw_json") {
                removeConfigKeys(
                    &config,
                    {
                        "checkpoint",
                        "vae_name",
                        "positive_prompt",
                        "negative_prompt",
                    "width",
                    "height",
                    "batch_size",
                    "input_image",
                    "mask_image",
                    "mask_channel",
                    "mask_grow_by",
                    "steps",
                    "seed",
                        "randomize_seed",
                        "cfg",
                        "denoise",
                        "sampler_name",
                        "scheduler",
                        "clip_skip",
                        "filename_prefix",
                        "loras"
                    }
                );
                setJsonTextValue(&config, "workflow_json", m_toolComfyWorkflowEdit->toPlainText());
                config.remove("workflow");
            } else {
                removeConfigKeys(&config, { "workflow", "workflow_json" });
                setJsonTextValue(&config, "checkpoint", m_toolComfyCheckpointCombo->currentText());
                const QString vaeName = m_toolComfyVaeCombo->currentText().trimmed() == "<Standard>"
                    ? QString()
                    : m_toolComfyVaeCombo->currentText();
                setJsonTextValue(&config, "vae_name", vaeName);
                setJsonTextValue(&config, "input_image", m_toolComfyImageCombo->currentText());
                setJsonTextValue(&config, "mask_image", m_toolComfyMaskImageCombo->currentText());
                setJsonTextValue(&config, "mask_channel", m_toolComfyMaskChannelCombo->currentText());
                config.insert("mask_grow_by", m_toolComfyMaskGrowSpin->value());
                setJsonTextValue(&config, "positive_prompt", m_toolComfyPositivePromptEdit->toPlainText());
                setJsonTextValue(&config, "negative_prompt", m_toolComfyNegativePromptEdit->toPlainText());
                config.insert("width", m_toolComfyWidthSpin->value());
                config.insert("height", m_toolComfyHeightSpin->value());
                config.insert("batch_size", m_toolComfyBatchSizeSpin->value());
                config.insert("steps", m_toolComfyStepsSpin->value());
                config.insert("seed", m_toolComfySeedSpin->value());
                if (m_toolComfyRandomizeSeedCheckBox->isChecked()) {
                    config.insert("randomize_seed", true);
                } else {
                    config.remove("randomize_seed");
                }
                config.insert("cfg", m_toolComfyCfgSpin->value());
                config.insert("denoise", m_toolComfyDenoiseSpin->value());
                setJsonTextValue(&config, "sampler_name", m_toolComfySamplerCombo->currentText());
                setJsonTextValue(&config, "scheduler", m_toolComfySchedulerCombo->currentText());
                config.insert("clip_skip", m_toolComfyClipSkipSpin->value());
                setJsonTextValue(&config, "filename_prefix", m_toolComfyFilenamePrefixEdit->text());

                QJsonArray loraArray;
                for (int loraRow = 0; loraRow < m_toolComfyLoraTable->rowCount(); ++loraRow) {
                    const auto* enabledItem = m_toolComfyLoraTable->item(loraRow, 0);
                    auto* loraCombo = qobject_cast<QComboBox*>(m_toolComfyLoraTable->cellWidget(loraRow, 1));
                    auto* modelSpin = qobject_cast<QDoubleSpinBox*>(m_toolComfyLoraTable->cellWidget(loraRow, 2));
                    auto* clipSpin = qobject_cast<QDoubleSpinBox*>(m_toolComfyLoraTable->cellWidget(loraRow, 3));
                    if (loraCombo == nullptr || modelSpin == nullptr || clipSpin == nullptr) {
                        continue;
                    }
                    const QString loraName = loraCombo->currentText().trimmed();
                    if (loraName.isEmpty()) {
                        continue;
                    }

                    QJsonObject loraObject{
                        { "name", loraName },
                        { "strength_model", modelSpin->value() },
                        { "strength_clip", clipSpin->value() },
                        { "enabled", enabledItem == nullptr || enabledItem->checkState() == Qt::Checked }
                    };
                    loraArray.append(loraObject);
                }
                if (loraArray.isEmpty()) {
                    config.remove("loras");
                } else {
                    config.insert("loras", loraArray);
                }
            }
            setJsonTextValue(&config, "save_outputs_to", m_toolComfyOutputDirEdit->text());
            if (m_toolComfyDownloadImagesCheckBox->isChecked()) {
                config.insert("download_images", true);
            } else {
                config.remove("download_images");
            }
            if (m_toolComfyIncludeHistoryCheckBox->isChecked()) {
                config.insert("include_history_json", true);
            } else {
                config.remove("include_history_json");
            }
            config.insert("poll_interval_ms", m_toolComfyPollIntervalSpin->value());
            config.insert("timeout_ms", m_toolComfyTimeoutSpin->value());
        } else {
            removeConfigKeys(
                &config,
                {
                    "diff",
                    "patch",
                    "return_content",
                    "include_extensions",
                    "exclude_paths",
                    "mode",
                    "modified_after_iso",
                    "within_minutes",
                    "max_files",
                    "max_chars_per_file",
                    "max_total_chars",
                    "include_hidden",
                    "skip_binary",
                    "workflow",
                    "workflow_json",
                    "save_outputs_to",
                    "download_images",
                    "include_history_json",
                    "poll_interval_ms",
                    "timeout_ms",
                    "recursive",
                    "directories_only",
                    "max_entries",
                    "create_dirs",
                    "url",
                    "method",
                    "body",
                    "body_json",
                    "headers",
                    "headers_json"
                }
            );
            setJsonTextValue(&config, "path", m_toolFileReadPathEdit->text());
            if (m_toolFileReadLineStartSpin->value() > 1) {
                config.insert("line_start", m_toolFileReadLineStartSpin->value());
            } else {
                config.remove("line_start");
            }
            if (m_toolFileReadLineEndSpin->value() > 0) {
                config.insert("line_end", m_toolFileReadLineEndSpin->value());
            } else {
                config.remove("line_end");
            }
            if (m_toolFileReadMaxCharsSpin->value() > 0) {
                config.insert("max_chars", m_toolFileReadMaxCharsSpin->value());
            } else {
                config.remove("max_chars");
            }
        }
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
                "default_next",
                "builder_mode",
                "checkpoint",
                "vae_name",
                "positive_prompt",
                "negative_prompt",
                "width",
                "height",
                "batch_size",
                "steps",
                "seed",
                "randomize_seed",
                "cfg",
                "denoise",
                "sampler_name",
                "scheduler",
                "clip_skip",
                "filename_prefix",
                "loras",
                "workflow",
                "workflow_json",
                "save_outputs_to",
                "download_images",
                "include_history_json",
                "poll_interval_ms",
                "timeout_ms"
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
        : (stepType == "decision" ? "decision" : (stepType == "tool" ? "tool" : "prompt"));
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
