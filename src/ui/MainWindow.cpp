#include "ui/MainWindow.h"

#include "core/WorkflowEngine.h"
#include "providers/ILlmProvider.h"
#include "providers/LmStudioProvider.h"
#include "providers/OllamaProvider.h"
#include "providers/ProviderManager.h"
#include "services/MemoryService.h"
#include "services/ProjectService.h"
#include "services/ScheduleService.h"
#include "services/SettingsService.h"
#include "services/WorkflowService.h"
#include "storage/DatabaseManager.h"
#include "tools/ToolExecutor.h"
#include "ui/MemoryPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/RunLogPanel.h"
#include "ui/SchedulePanel.h"
#include "ui/WorkflowPanel.h"

#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace privateclaw::ui {

namespace {

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

QString prefixRunLog(const QString& prefix, const QStringList& lines)
{
    QStringList prefixed;
    prefixed.reserve(lines.size());
    for (const QString& line : lines) {
        if (!line.trimmed().isEmpty()) {
            prefixed.append(QString("%1 %2").arg(prefix, line));
        }
    }

    return prefixed.join('\n');
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

MainWindow::MainWindow(
    storage::DatabaseManager& databaseManager,
    services::SettingsService& settingsService,
    services::ProjectService& projectService,
    services::MemoryService& memoryService,
    services::WorkflowService& workflowService,
    services::ScheduleService& scheduleService,
    providers::ProviderManager& providerManager,
    QWidget* parent
)
    : QMainWindow(parent)
    , m_databaseManager(databaseManager)
    , m_settingsService(settingsService)
    , m_projectService(projectService)
    , m_memoryService(memoryService)
    , m_workflowService(workflowService)
    , m_scheduleService(scheduleService)
    , m_providerManager(providerManager)
{
    setWindowTitle("PrivateClaw");
    resize(1360, 840);
    buildUi();
    updateStatusBar();
    startSchedulePolling();
}

void MainWindow::buildUi()
{
    auto* rootSplitter = new QSplitter(Qt::Vertical, this);
    auto* topSplitter = new QSplitter(Qt::Horizontal, rootSplitter);

    m_navigation = new QListWidget(topSplitter);
    m_navigation->addItems({
        "Projekte",
        "Workflows",
        "Erinnerung",
        "Zeitplaene"
    });
    m_navigation->setFixedWidth(220);

    m_pages = new QStackedWidget(topSplitter);
    m_projectPanel = new ProjectPanel(m_projectService, m_providerManager, m_pages);
    m_projectPanel->setOnProjectDataChanged([this]() {
        refreshProjectDependentViews();
    });
    m_memoryPanel = new MemoryPanel(m_projectService, m_memoryService, m_pages);
    m_memoryPanel->setOnMemoryDataChanged([this]() {
        updateStatusBar();
    });
    m_schedulePanel = new SchedulePanel(
        m_projectService,
        m_workflowService,
        m_scheduleService,
        m_schedulerService,
        m_pages
    );
    m_schedulePanel->setOnScheduleDataChanged([this]() {
        updateStatusBar();
    });
    m_schedulePanel->setOnRunScheduleRequested([this](const domain::Schedule& schedule) {
        executeSchedule(schedule, false, "manuell");
    });

    m_runLogPanel = new RunLogPanel(rootSplitter);

    m_workflowPanel = new WorkflowPanel(
        m_projectService,
        m_settingsService,
        m_memoryService,
        m_workflowService,
        m_providerManager,
        m_pages
    );
    m_workflowPanel->setOnWorkflowDataChanged([this]() {
        refreshProjectDependentViews();
    });
    m_workflowPanel->setOnExecutionLogChanged([this](const QString& text) {
        m_runLogPanel->appendLogLine(text);
    });

    m_pages->addWidget(m_projectPanel);
    m_pages->addWidget(m_workflowPanel);
    m_pages->addWidget(m_memoryPanel);
    m_pages->addWidget(m_schedulePanel);

    topSplitter->setStretchFactor(0, 0);
    topSplitter->setStretchFactor(1, 1);
    rootSplitter->setStretchFactor(0, 5);
    rootSplitter->setStretchFactor(1, 2);

    setCentralWidget(rootSplitter);

    connect(m_navigation, &QListWidget::currentRowChanged, this, [this](const int row) {
        if (row >= 0 && row < m_pages->count()) {
            m_pages->setCurrentIndex(row);
        }
    });

    m_navigation->setCurrentRow(0);
}

void MainWindow::refreshProjectDependentViews()
{
    updateStatusBar();
    if (m_workflowPanel != nullptr) {
        m_workflowPanel->reloadData();
    }
    if (m_memoryPanel != nullptr) {
        m_memoryPanel->reloadData();
    }
    if (m_schedulePanel != nullptr) {
        m_schedulePanel->reloadData();
    }
}

void MainWindow::updateStatusBar()
{
    statusBar()->showMessage(
        QString("Datenbank: %1 | Provider: %2 | Projekte: %3 | Workflows: %4 | Memory: %5 | Zeitplaene: %6 | Laufende Schedules: %7")
            .arg(
                m_databaseManager.databasePath(),
                QString::number(m_providerManager.providers().size()),
                QString::number(m_projectService.projectCount()),
                QString::number(m_workflowService.workflowCount()),
                QString::number(m_memoryService.memoryCount()),
                QString::number(m_scheduleService.scheduleCount()),
                QString::number(m_runningScheduleIds.size())
            )
    );
}

void MainWindow::startSchedulePolling()
{
    m_schedulePollTimer = new QTimer(this);
    m_schedulePollTimer->setInterval(15000);
    connect(m_schedulePollTimer, &QTimer::timeout, this, [this]() {
        pollDueSchedules();
    });
    m_schedulePollTimer->start();

    QTimer::singleShot(1000, this, [this]() {
        pollDueSchedules();
    });
}

void MainWindow::pollDueSchedules()
{
    const QList<domain::Schedule> dueSchedules = m_scheduleService.dueSchedules();
    for (const domain::Schedule& schedule : dueSchedules) {
        if (m_runningScheduleIds.contains(schedule.id)) {
            continue;
        }
        executeSchedule(schedule, true, "automatisch");
    }
}

void MainWindow::executeSchedule(
    const domain::Schedule& schedule,
    const bool advanceScheduleAfterRun,
    const QString& originLabel
)
{
    if (schedule.id <= 0) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine("[scheduler] Ungueltiger Zeitplan, Ausfuehrung abgebrochen.");
        }
        return;
    }

    if (m_runningScheduleIds.contains(schedule.id)) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Zeitplan laeuft bereits, paralleler Start wird uebersprungen.").arg(schedule.id)
            );
        }
        return;
    }

    const QList<domain::Project> projects = m_projectService.listProjects();
    const QList<domain::Workflow> workflows = m_workflowService.listWorkflows();

    const domain::Project* project = nullptr;
    for (const domain::Project& candidate : projects) {
        if (candidate.id == schedule.projectId) {
            project = &candidate;
            break;
        }
    }

    if (project == nullptr) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Projekt %2 nicht gefunden, Zeitplan kann nicht gestartet werden.")
                    .arg(schedule.id)
                    .arg(schedule.projectId)
            );
        }
        return;
    }

    domain::Workflow workflow;
    bool workflowFound = false;
    for (const domain::Workflow& candidate : workflows) {
        if (candidate.id == schedule.workflowId && candidate.projectId == project->id) {
            workflow = candidate;
            workflowFound = true;
            break;
        }
    }

    if (!workflowFound) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Workflow %2 nicht gefunden, Zeitplan kann nicht gestartet werden.")
                    .arg(schedule.id)
                    .arg(schedule.workflowId)
            );
        }
        return;
    }

    QString workflowDefinitionError;
    if (!m_workflowService.hydrateWorkflowDefinition(&workflow, &workflowDefinitionError)) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Workflow-Definition ungueltig: %2")
                    .arg(schedule.id)
                    .arg(workflowDefinitionError)
            );
        }
        return;
    }

    const QString providerName = project->providerName.trimmed().isEmpty()
        ? "Ollama"
        : project->providerName.trimmed();
    auto* provider = m_providerManager.providerByName(providerName);
    if (provider == nullptr) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Provider '%2' ist nicht registriert.")
                    .arg(schedule.id)
                    .arg(providerName)
            );
        }
        return;
    }

    const QString providerBaseUrl = provider->baseUrl();
    if (providerBaseUrl.trimmed().isEmpty()) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Provider-URL fuer '%2' ist leer.")
                    .arg(schedule.id)
                    .arg(provider->name())
            );
        }
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
    runContext.variables.insert("schedule_id", QString::number(schedule.id));
    runContext.variables.insert("schedule_trigger_type", schedule.triggerType);
    runContext.variables.insert("schedule_trigger_expression", schedule.triggerExpression);
    runContext.variables.insert("schedule_origin", originLabel);

    const QList<domain::MemoryEntry> memoryEntries = m_memoryService.recentEntries(project->id, 6);
    for (const domain::MemoryEntry& entry : memoryEntries) {
        runContext.memorySnippets.append(formatMemorySnippet(entry));
    }
    runContext.variables.insert("project_memory", runContext.memorySnippets.join("\n"));
    runContext.variables.insert("project_memory_count", QString::number(runContext.memorySnippets.size()));

    m_runningScheduleIds.insert(schedule.id);
    updateStatusBar();

    if (m_runLogPanel != nullptr) {
        m_runLogPanel->appendLogLine(
            QString("[schedule:%1] Starte %2 Zeitplan fuer Workflow '%3' in Projekt '%4'.")
                .arg(schedule.id)
                .arg(originLabel)
                .arg(workflow.name)
                .arg(project->name)
        );
    }

    const QString workspaceRoot = m_settingsService.workspaceRoot();
    const QString comfyUiBaseUrl = m_settingsService.comfyUiBaseUrl();
    auto* watcher = new QFutureWatcher<core::ExecutionResult>(this);
    connect(watcher, &QFutureWatcher<core::ExecutionResult>::finished, this, [this, watcher, schedule, advanceScheduleAfterRun]() {
        const core::ExecutionResult result = watcher->result();
        watcher->deleteLater();

        m_runningScheduleIds.remove(schedule.id);

        if (m_runLogPanel != nullptr) {
            const QString prefixedLogs = prefixRunLog(QString("[schedule:%1]").arg(schedule.id), result.logs);
            if (!prefixedLogs.isEmpty()) {
                m_runLogPanel->appendLogLine(prefixedLogs);
            }
        }

        int savedMemoryCount = 0;
        for (domain::MemoryEntry entry : result.memoryEntriesToPersist) {
            QString saveError;
            if (!m_memoryService.saveEntry(&entry, &saveError)) {
                if (m_runLogPanel != nullptr) {
                    m_runLogPanel->appendLogLine(
                        QString("[schedule:%1] Memory-Speichern fehlgeschlagen: %2")
                            .arg(schedule.id)
                            .arg(saveError)
                    );
                }
                continue;
            }

            ++savedMemoryCount;
        }

        if (advanceScheduleAfterRun) {
            domain::Schedule updatedSchedule = schedule;
            QString scheduleError;
            if (!m_scheduleService.completeScheduleRun(&updatedSchedule, QDateTime::currentDateTimeUtc(), &scheduleError)) {
                if (m_runLogPanel != nullptr) {
                    m_runLogPanel->appendLogLine(
                        QString("[schedule:%1] Zeitplan konnte nach dem Lauf nicht aktualisiert werden: %2")
                            .arg(schedule.id)
                            .arg(scheduleError)
                    );
                }
            } else if (m_runLogPanel != nullptr) {
                m_runLogPanel->appendLogLine(
                    QString("[schedule:%1] Zeitplan aktualisiert. Naechster Lauf: %2")
                        .arg(schedule.id)
                        .arg(updatedSchedule.nextRunAt.isValid()
                            ? updatedSchedule.nextRunAt.toLocalTime().toString("dd.MM.yyyy HH:mm")
                            : QString("deaktiviert"))
                );
            }
        }

        if (!result.success) {
            if (m_runLogPanel != nullptr) {
                m_runLogPanel->appendLogLine(
                    QString("[schedule:%1] Workflow-Ausfuehrung fehlgeschlagen: %2")
                        .arg(schedule.id)
                        .arg(result.errorMessage)
                );
            }
            refreshProjectDependentViews();
            updateStatusBar();
            return;
        }

        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Workflow erfolgreich abgeschlossen. Memory gespeichert: %2 | Ausgabezeichen: %3")
                    .arg(schedule.id)
                    .arg(savedMemoryCount)
                    .arg(result.finalOutput.size())
            );
        }

        refreshProjectDependentViews();
        updateStatusBar();
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

} // namespace privateclaw::ui
