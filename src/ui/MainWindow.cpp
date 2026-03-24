#include "ui/MainWindow.h"

#include "core/WorkflowEngine.h"
#include "providers/ILlmProvider.h"
#include "providers/LmStudioProvider.h"
#include "providers/OllamaProvider.h"
#include "providers/ProviderManager.h"
#include "services/MemoryService.h"
#include "services/ProjectService.h"
#include "services/RunService.h"
#include "services/ScheduleService.h"
#include "services/SecretsService.h"
#include "services/SettingsService.h"
#include "services/UpdateChecker.h"
#include "services/WorkflowService.h"
#include "storage/DatabaseManager.h"
#include "tools/ToolExecutor.h"
#include "ui/ExecutionApproval.h"
#include "ui/MemoryPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/RunPanel.h"
#include "ui/RunLogPanel.h"
#include "ui/SchedulePanel.h"
#include "ui/WorkflowPanel.h"

#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFont>
#include <QIcon>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace privateclaw::ui {

namespace {

struct PreparedMemoryContext
{
    QStringList snippets;
    int totalEntries = 0;
    int directEntries = 0;
    int compressedEntries = 0;
};

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

QString formatCompressedMemorySnippet(const QList<domain::MemoryEntry>& entries)
{
    if (entries.isEmpty()) {
        return {};
    }

    QStringList lines;
    lines.append(QString("- Komprimierte Erinnerung aus %1 weiteren Eintraegen:").arg(entries.size()));

    const int includedEntries = qMin(entries.size(), 8);
    for (int index = 0; index < includedEntries; ++index) {
        const domain::MemoryEntry& entry = entries.at(index);
        QStringList parts;
        parts.append(QString("Typ: %1").arg(entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed()));
        if (!entry.source.trimmed().isEmpty()) {
            parts.append(QString("Quelle: %1").arg(entry.source.trimmed()));
        }
        if (!entry.tags.isEmpty()) {
            parts.append(QString("Tags: %1").arg(entry.tags.join(", ")));
        }

        QString content = entry.content.simplified();
        if (content.size() > 120) {
            content = content.left(117) + "...";
        }
        parts.append(QString("Inhalt: %1").arg(content));
        lines.append("  " + parts.join(" | "));
    }

    if (entries.size() > includedEntries) {
        lines.append(QString("  ... %1 weitere Eintraege komprimiert.").arg(entries.size() - includedEntries));
    }

    return lines.join("\n");
}

services::PreparedMemoryContext prepareMemoryContext(services::MemoryService& memoryService, const qint64 projectId)
{
    return memoryService.prepareRunContext(projectId);
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

QString summarizeRunText(QString text)
{
    text = text.simplified();
    if (text.size() > 220) {
        text = text.left(217) + "...";
    }
    return text;
}

core::ExecutionResult executeWorkflowWithProvider(
    const QString& providerName,
    const QString& providerBaseUrl,
    const QString& workspaceRoot,
    const QString& comfyUiBaseUrl,
    const QStringList& allowedToolPaths,
    const domain::Workflow& workflow,
    const core::RunContext& runContext
)
{
    const tools::ToolExecutor toolExecutor(workspaceRoot, comfyUiBaseUrl, QString(), allowedToolPaths);
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

QIcon buildTrayIcon(const bool updateBadge)
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    painter.setBrush(QColor("#d8c19c"));
    painter.drawRoundedRect(QRectF(4.0, 4.0, 56.0, 56.0), 14.0, 14.0);

    painter.setBrush(QColor("#5b4732"));
    painter.drawRoundedRect(QRectF(12.0, 12.0, 40.0, 40.0), 10.0, 10.0);

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(20);
    painter.setFont(font);
    painter.setPen(QColor("#f7f0e5"));
    painter.drawText(QRect(12, 12, 40, 40), Qt::AlignCenter, "PC");

    if (updateBadge) {
        painter.setBrush(QColor("#d96b2b"));
        painter.drawEllipse(QRectF(38.0, 2.0, 24.0, 24.0));
        font.setPointSize(16);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRect(38, 2, 24, 24), Qt::AlignCenter, "!");
    }

    return QIcon(pixmap);
}

} // namespace

MainWindow::MainWindow(
    storage::DatabaseManager& databaseManager,
    services::SettingsService& settingsService,
    services::ProjectService& projectService,
    services::MemoryService& memoryService,
    services::SecretsService& secretsService,
    services::WorkflowService& workflowService,
    services::RunService& runService,
    services::ScheduleService& scheduleService,
    providers::ProviderManager& providerManager,
    QWidget* parent
)
    : QMainWindow(parent)
    , m_databaseManager(databaseManager)
    , m_settingsService(settingsService)
    , m_projectService(projectService)
    , m_memoryService(memoryService)
    , m_secretsService(secretsService)
    , m_workflowService(workflowService)
    , m_runService(runService)
    , m_scheduleService(scheduleService)
    , m_providerManager(providerManager)
{
    setWindowTitle(QString("PrivateClaw v%1").arg(services::UpdateChecker::currentVersion()));
    resize(1360, 840);
    buildUi();
    buildSystemTray();
    recoverInterruptedRuns();
    updateStatusBar();
    startSchedulePolling();
    startUpdateChecks();
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
        "Runs",
        "Zeitplaene"
    });
    m_navigation->setFixedWidth(220);

    m_pages = new QStackedWidget(topSplitter);
    m_projectPanel = new ProjectPanel(
        m_projectService,
        m_settingsService,
        m_secretsService,
        m_providerManager,
        m_pages
    );
    m_projectPanel->setOnProjectDataChanged([this]() {
        refreshProjectDependentViews();
    });
    m_memoryPanel = new MemoryPanel(m_projectService, m_memoryService, m_pages);
    m_memoryPanel->setOnMemoryDataChanged([this]() {
        updateStatusBar();
    });
    m_runPanel = new RunPanel(m_projectService, m_workflowService, m_runService, m_pages);
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
        m_secretsService,
        m_runService,
        m_workflowService,
        m_providerManager,
        m_pages
    );
    m_workflowPanel->setOnWorkflowDataChanged([this]() {
        refreshProjectDependentViews();
    });
    m_workflowPanel->setOnRunDataChanged([this]() {
        updateStatusBar();
        if (m_memoryPanel != nullptr) {
            m_memoryPanel->reloadData();
        }
        if (m_runPanel != nullptr) {
            m_runPanel->reloadData();
        }
    });
    m_workflowPanel->setOnSettingsDataChanged([this]() {
        updateStatusBar();
        if (m_projectPanel != nullptr) {
            m_projectPanel->reloadData();
        }
    });
    m_workflowPanel->setOnExecutionLogChanged([this](const QString& text) {
        m_runLogPanel->appendLogLine(text);
    });

    m_pages->addWidget(m_projectPanel);
    m_pages->addWidget(m_workflowPanel);
    m_pages->addWidget(m_memoryPanel);
    m_pages->addWidget(m_runPanel);
    m_pages->addWidget(m_schedulePanel);

    topSplitter->setStretchFactor(0, 0);
    topSplitter->setStretchFactor(1, 1);
    rootSplitter->setStretchFactor(0, 5);
    rootSplitter->setStretchFactor(1, 2);

    setCentralWidget(rootSplitter);

    connect(m_navigation, &QListWidget::currentRowChanged, this, [this](const int row) {
        if (row >= 0 && row < m_pages->count()) {
            m_pages->setCurrentIndex(row);
            if (row == 0 && m_projectPanel != nullptr) {
                m_projectPanel->reloadData();
            }
        }
    });

    m_navigation->setCurrentRow(0);
}

void MainWindow::buildSystemTray()
{
    m_updateChecker = new services::UpdateChecker(this);
    connect(
        m_updateChecker,
        &services::UpdateChecker::checkFinished,
        this,
        [this](
            const bool manual,
            const bool success,
            const bool updateAvailable,
            const QString& currentVersion,
            const QString& latestVersion,
            const QString& releaseUrl,
            const QString& message
        ) {
            handleUpdateCheckFinished(
                manual,
                success,
                updateAvailable,
                currentVersion,
                latestVersion,
                releaseUrl,
                message
            );
        }
    );

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    QApplication::setQuitOnLastWindowClosed(false);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(buildTrayIcon(false));
    m_trayIcon->setToolTip(QString("PrivateClaw v%1").arg(services::UpdateChecker::currentVersion()));

    m_trayMenu = new QMenu(this);
    m_hideToTrayAction = m_trayMenu->addAction("Im Hintergrund weiterlaufen");
    m_openWindowAction = m_trayMenu->addAction("Fenster anzeigen");
    m_checkUpdatesAction = m_trayMenu->addAction("Nach Updates suchen");
    m_openReleasePageAction = m_trayMenu->addAction("Release-Seite oeffnen");
    m_trayMenu->addSeparator();
    m_quitAction = m_trayMenu->addAction("Beenden");

    connect(m_hideToTrayAction, &QAction::triggered, this, [this]() {
        hideToBackground();
    });
    connect(m_openWindowAction, &QAction::triggered, this, [this]() {
        showOrRaiseWindow();
    });
    connect(m_checkUpdatesAction, &QAction::triggered, this, [this]() {
        triggerUpdateCheck(true);
    });
    connect(m_openReleasePageAction, &QAction::triggered, this, [this]() {
        openLatestReleasePage();
    });
    connect(m_quitAction, &QAction::triggered, this, [this]() {
        requestApplicationQuit();
    });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](const QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::DoubleClick) {
            return;
        }

        if (m_updateAvailable) {
            openLatestReleasePage();
            return;
        }

        showOrRaiseWindow();
    });

    m_trayBlinkTimer = new QTimer(this);
    m_trayBlinkTimer->setInterval(650);
    connect(m_trayBlinkTimer, &QTimer::timeout, this, [this]() {
        m_trayBlinkHighlighted = !m_trayBlinkHighlighted;
        updateTrayPresentation();
    });

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->show();
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
    if (m_runPanel != nullptr) {
        m_runPanel->reloadData();
    }
    if (m_schedulePanel != nullptr) {
        m_schedulePanel->reloadData();
    }
}

void MainWindow::recoverInterruptedRuns()
{
    QString recoveryError;
    const int recoveredRunCount = m_runService.recoverInterruptedRuns(&recoveryError);
    if (recoveredRunCount < 0) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[runs] Vorherige offene Laeufe konnten nicht wiederhergestellt werden: %1")
                    .arg(recoveryError)
            );
        }
        return;
    }

    if (recoveredRunCount > 0) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[runs] %1 zuvor offene Lauf/Läufe wurden beim Start als 'interrupted' markiert.")
                    .arg(recoveredRunCount)
            );
        }
        if (m_runPanel != nullptr) {
            m_runPanel->reloadData();
        }
    }
}

void MainWindow::hideToBackground()
{
    if (m_trayIcon == nullptr || !m_trayIcon->isVisible()) {
        return;
    }

    hide();

    if (!m_settingsService.backgroundTrayHintShown()) {
        m_trayIcon->showMessage(
            "PrivateClaw laeuft weiter",
            "Die App wurde in den Hintergrund verschoben. Zeitplaene und laufende Aufgaben bleiben aktiv. Ueber das Tray-Menue kannst du das Fenster wieder oeffnen oder die App wirklich beenden.",
            QSystemTrayIcon::Information,
            12000
        );
        m_settingsService.setBackgroundTrayHintShown(true);
    }

    if (m_runLogPanel != nullptr) {
        m_runLogPanel->appendLogLine(
            "[app] Fenster wurde in den Hintergrund verschoben. Zeitplaene laufen weiter."
        );
    }
}

void MainWindow::requestApplicationQuit()
{
    m_forceQuitRequested = true;
    QApplication::setQuitOnLastWindowClosed(true);

    if (m_schedulePollTimer != nullptr) {
        m_schedulePollTimer->stop();
    }
    if (m_updateCheckTimer != nullptr) {
        m_updateCheckTimer->stop();
    }
    if (m_trayBlinkTimer != nullptr) {
        m_trayBlinkTimer->stop();
    }

    if (m_trayMenu != nullptr) {
        m_trayMenu->hide();
    }
    if (m_trayIcon != nullptr) {
        m_trayIcon->hide();
    }

    if (m_runLogPanel != nullptr) {
        m_runLogPanel->appendLogLine("[app] Anwendung wird ueber das Tray-Menue beendet.");
    }

    close();
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MainWindow::startUpdateChecks()
{
    if (m_updateChecker == nullptr) {
        return;
    }

    m_updateCheckTimer = new QTimer(this);
    m_updateCheckTimer->setInterval(4 * 60 * 60 * 1000);
    connect(m_updateCheckTimer, &QTimer::timeout, this, [this]() {
        triggerUpdateCheck(false);
    });
    m_updateCheckTimer->start();

    QTimer::singleShot(4000, this, [this]() {
        triggerUpdateCheck(false);
    });
}

void MainWindow::updateStatusBar()
{
    statusBar()->showMessage(
        QString("Version: %1 | Datenbank: %2 | Provider: %3 | Projekte: %4 | Workflows: %5 | Memory: %6 | Runs: %7 | Zeitplaene: %8 | Laufende Schedules: %9 | Externe Pfade: %10%11")
            .arg(
                services::UpdateChecker::currentVersion(),
                m_databaseManager.databasePath(),
                QString::number(m_providerManager.providers().size()),
                QString::number(m_projectService.projectCount()),
                QString::number(m_workflowService.workflowCount()),
                QString::number(m_memoryService.memoryCount()),
                QString::number(m_runService.runCount()),
                QString::number(m_scheduleService.scheduleCount()),
                QString::number(m_runningScheduleIds.size()),
                QString::number(m_settingsService.customAllowedToolPaths().size()),
                m_updateAvailable && !m_latestReleaseVersion.trimmed().isEmpty()
                    ? QString(" | Update verfuegbar: %1").arg(m_latestReleaseVersion)
                    : QString()
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

void MainWindow::triggerUpdateCheck(const bool manual)
{
    if (m_updateChecker == nullptr) {
        return;
    }

    m_updateChecker->checkForUpdates(manual);
}

void MainWindow::handleUpdateCheckFinished(
    const bool manual,
    const bool success,
    const bool updateAvailable,
    const QString& currentVersion,
    const QString& latestVersion,
    const QString& releaseUrl,
    const QString& message
)
{
    m_updateAvailable = success && updateAvailable;
    m_latestReleaseVersion = latestVersion.trimmed();
    m_latestReleaseUrl = releaseUrl.trimmed();

    if (!m_updateAvailable) {
        m_updateNotificationShown = false;
        m_trayBlinkHighlighted = false;
        if (m_trayBlinkTimer != nullptr) {
            m_trayBlinkTimer->stop();
        }
    } else if (m_trayBlinkTimer != nullptr && !m_trayBlinkTimer->isActive()) {
        m_trayBlinkHighlighted = true;
        m_trayBlinkTimer->start();
    }

    updateTrayPresentation();
    updateStatusBar();

    if (m_trayIcon != nullptr && success && updateAvailable && !m_updateNotificationShown) {
        m_trayIcon->showMessage(
            "PrivateClaw Update",
            QString("Neue Version %1 verfuegbar. Doppelklick auf das Tray-Icon oeffnet die Release-Seite.")
                .arg(latestVersion),
            QSystemTrayIcon::Information,
            12000
        );
        m_updateNotificationShown = true;
    } else if (manual && m_trayIcon != nullptr) {
        m_trayIcon->showMessage(
            success ? "PrivateClaw Update" : "PrivateClaw Update-Check",
            message,
            success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning,
            9000
        );
    }

    if (manual) {
        statusBar()->showMessage(message, 10000);
    }

    if (m_runLogPanel != nullptr && (manual || !success || m_updateAvailable)) {
        const QString updateLog = QString("[update] %1 | Lokal: %2%3")
            .arg(
                message,
                currentVersion,
                latestVersion.trimmed().isEmpty() ? QString() : QString(" | Release: %1").arg(latestVersion)
            );
        m_runLogPanel->appendLogLine(updateLog);
    }
}

void MainWindow::updateTrayPresentation()
{
    if (m_trayIcon == nullptr) {
        return;
    }

    const bool highlight = m_updateAvailable && m_trayBlinkHighlighted;
    m_trayIcon->setIcon(buildTrayIcon(highlight));

    QString toolTip = QString("PrivateClaw v%1").arg(services::UpdateChecker::currentVersion());
    if (m_updateAvailable && !m_latestReleaseVersion.trimmed().isEmpty()) {
        toolTip += QString("\nNeue Version verfuegbar: %1").arg(m_latestReleaseVersion);
    }
    m_trayIcon->setToolTip(toolTip);

    if (m_openReleasePageAction != nullptr) {
        m_openReleasePageAction->setText(
            m_latestReleaseVersion.trimmed().isEmpty()
                ? "Release-Seite oeffnen"
                : QString("Release-Seite oeffnen (%1)").arg(m_latestReleaseVersion)
        );
    }
}

void MainWindow::openLatestReleasePage()
{
    const QString releaseUrl = m_latestReleaseUrl.trimmed().isEmpty()
        ? services::UpdateChecker::releasesPageUrl()
        : m_latestReleaseUrl.trimmed();

    QDesktopServices::openUrl(QUrl(releaseUrl));
}

void MainWindow::showOrRaiseWindow()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_forceQuitRequested && m_trayIcon != nullptr && m_trayIcon->isVisible()) {
        event->ignore();
        hideToBackground();
        return;
    }

    if (m_trayIcon != nullptr) {
        m_trayIcon->hide();
    }
    if (m_schedulePollTimer != nullptr) {
        m_schedulePollTimer->stop();
    }
    if (m_updateCheckTimer != nullptr) {
        m_updateCheckTimer->stop();
    }
    if (m_trayBlinkTimer != nullptr) {
        m_trayBlinkTimer->stop();
    }

    QMainWindow::closeEvent(event);
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
    providers::ILlmProvider* registeredProvider = m_providerManager.providerByName(providerName);
    if (registeredProvider == nullptr) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Provider '%2' ist nicht registriert.")
                    .arg(schedule.id)
                    .arg(providerName)
            );
        }
        return;
    }

    QString providerBaseUrl = project->providerBaseUrl.trimmed();
    if (providerBaseUrl.isEmpty()) {
        providerBaseUrl = registeredProvider->baseUrl().trimmed();
    }
    if (providerBaseUrl.trimmed().isEmpty()) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Provider-URL fuer '%2' ist leer.")
                    .arg(schedule.id)
                    .arg(providerName)
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
    runContext.variables.insert("provider_base_url", providerBaseUrl);
    runContext.variables.insert("schedule_id", QString::number(schedule.id));
    runContext.variables.insert("schedule_trigger_type", schedule.triggerType);
    runContext.variables.insert("schedule_trigger_expression", schedule.triggerExpression);
    runContext.variables.insert("schedule_origin", originLabel);

    const services::PreparedMemoryContext memoryContext = prepareMemoryContext(m_memoryService, project->id);
    runContext.memorySnippets = memoryContext.snippets;
    runContext.memoryEntryCount = memoryContext.totalEntries;
    runContext.directMemoryEntryCount = memoryContext.directEntries;
    runContext.compressedMemoryEntryCount = memoryContext.compressedEntries;
    runContext.pinnedMemoryEntryCount = memoryContext.pinnedEntries;
    runContext.totalPinnedMemoryEntryCount = memoryContext.totalPinnedEntries;
    runContext.variables.insert("project_memory", runContext.memorySnippets.join("\n"));
    runContext.variables.insert("project_memory_count", QString::number(runContext.memoryEntryCount));
    runContext.variables.insert("project_memory_snippet_count", QString::number(runContext.memorySnippets.size()));
    runContext.variables.insert("project_memory_direct_count", QString::number(runContext.directMemoryEntryCount));
    runContext.variables.insert("project_memory_compressed_count", QString::number(runContext.compressedMemoryEntryCount));
    runContext.variables.insert("project_memory_pinned_count", QString::number(runContext.pinnedMemoryEntryCount));
    runContext.variables.insert("project_memory_total_pinned_count", QString::number(runContext.totalPinnedMemoryEntryCount));

    QString secretLoadError;
    const QHash<QString, QString> projectSecrets = m_secretsService.loadSecretsForProject(project->id, &secretLoadError);
    if (!secretLoadError.isEmpty()) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] Projekt-Secrets konnten nicht geladen werden: %2")
                    .arg(schedule.id)
                    .arg(secretLoadError)
            );
        }
        return;
    }

    for (auto it = projectSecrets.constBegin(); it != projectSecrets.constEnd(); ++it) {
        runContext.variables.insert(QString("secret.%1").arg(it.key()), it.value());
    }
    runContext.variables.insert("project_secret_count", QString::number(projectSecrets.size()));

    RiskApprovalOptions riskApprovalOptions;
    riskApprovalOptions.dialogParent = this;
    riskApprovalOptions.unattended = advanceScheduleAfterRun;
    riskApprovalOptions.executionLabel = advanceScheduleAfterRun
        ? "Automatischer Zeitplan"
        : "Manueller Zeitplan";
    const RiskApprovalDecision riskApproval = evaluateRiskyToolExecution(
        *project,
        workflow,
        riskApprovalOptions
    );
    if (!riskApproval.allowed) {
        if (m_runLogPanel != nullptr) {
            m_runLogPanel->appendLogLine(
                QString("[schedule:%1] %2").arg(schedule.id).arg(riskApproval.message)
            );
        }
        return;
    }

    runContext.allowShellRun = riskApproval.allowShellRun;
    runContext.allowFileEditDiff = riskApproval.allowFileEditDiff;
    runContext.allowHttpRequest = riskApproval.allowHttpRequest;

    domain::Run persistedRun;
    persistedRun.projectId = project->id;
    persistedRun.workflowId = workflow.id;
    persistedRun.status = "running";
    persistedRun.origin = advanceScheduleAfterRun ? "schedule:auto" : "schedule:manual";
    persistedRun.providerName = providerName;
    persistedRun.modelName = runContext.selectedModel;
    persistedRun.summary = QString("Zeitplan %1 hat den Workflow gestartet.").arg(originLabel);
    persistedRun.logText = QString(
        "[schedule:%1] Starte %2 Zeitplan fuer Workflow '%3' in Projekt '%4'.\n"
        "[schedule:%1] Provider: %5 | Modell: %6 | Projekt-Memory: %7"
    )
        .arg(schedule.id)
        .arg(originLabel)
        .arg(workflow.name)
        .arg(project->name)
        .arg(providerName)
        .arg(runContext.selectedModel)
        .arg(
            runContext.compressedMemoryEntryCount > 0
                ? QString("%1 (%2 direkt, %3 komprimiert, %4 angepinnt)")
                      .arg(runContext.memoryEntryCount)
                      .arg(runContext.directMemoryEntryCount)
                      .arg(runContext.compressedMemoryEntryCount)
                      .arg(runContext.totalPinnedMemoryEntryCount)
                : QString::number(runContext.memoryEntryCount)
        );
    QString runPersistenceError;
    if (!m_runService.startRun(&persistedRun, &runPersistenceError) && m_runLogPanel != nullptr) {
        m_runLogPanel->appendLogLine(
            QString("[schedule:%1] Run-Historie konnte nicht gestartet werden: %2")
                .arg(schedule.id)
                .arg(runPersistenceError)
        );
    } else if (persistedRun.id > 0 && m_runPanel != nullptr) {
        m_runPanel->reloadData();
    }

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
    const QStringList allowedToolPaths = m_settingsService.effectiveAllowedToolPaths();
    auto* watcher = new QFutureWatcher<core::ExecutionResult>(this);
    connect(watcher, &QFutureWatcher<core::ExecutionResult>::finished, this, [this, watcher, schedule, advanceScheduleAfterRun, persistedRun]() mutable {
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

        persistedRun.status = result.success ? "completed" : "failed";
        persistedRun.summary = summarizeRunText(
            result.success
                ? (!result.finalOutput.trimmed().isEmpty()
                    ? result.finalOutput
                    : QString("Workflow erfolgreich abgeschlossen."))
                : result.errorMessage
        );
        persistedRun.outputText = result.finalOutput;
        persistedRun.logText = result.logs.join('\n');
        persistedRun.errorMessage = result.success ? QString() : result.errorMessage;
        persistedRun.savedMemoryCount = savedMemoryCount;
        persistedRun.finishedAt = QDateTime::currentDateTimeUtc();
        if (persistedRun.id > 0) {
            QString finishRunError;
            if (!m_runService.finishRun(&persistedRun, &finishRunError) && m_runLogPanel != nullptr) {
                m_runLogPanel->appendLogLine(
                    QString("[schedule:%1] Run-Historie konnte nicht abgeschlossen werden: %2")
                        .arg(schedule.id)
                        .arg(finishRunError)
                );
            }
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
            if (m_memoryPanel != nullptr) {
                m_memoryPanel->reloadData();
            }
            if (m_runPanel != nullptr) {
                m_runPanel->reloadData();
            }
            if (m_schedulePanel != nullptr) {
                m_schedulePanel->reloadData();
            }
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

        if (m_memoryPanel != nullptr) {
            m_memoryPanel->reloadData();
        }
        if (m_runPanel != nullptr) {
            m_runPanel->reloadData();
        }
        if (m_schedulePanel != nullptr) {
            m_schedulePanel->reloadData();
        }
        updateStatusBar();
    });

    watcher->setFuture(QtConcurrent::run([workflow, runContext, providerBaseUrl, providerName, workspaceRoot, comfyUiBaseUrl, allowedToolPaths]() {
        return executeWorkflowWithProvider(
            providerName,
            providerBaseUrl,
            workspaceRoot,
            comfyUiBaseUrl,
            allowedToolPaths,
            workflow,
            runContext
        );
    }));
}

} // namespace privateclaw::ui
