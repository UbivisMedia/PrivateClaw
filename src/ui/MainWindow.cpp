#include "ui/MainWindow.h"

#include "providers/ProviderManager.h"
#include "services/MemoryService.h"
#include "services/ProjectService.h"
#include "services/SettingsService.h"
#include "services/WorkflowService.h"
#include "storage/DatabaseManager.h"
#include "ui/MemoryPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/RunLogPanel.h"
#include "ui/SchedulePanel.h"
#include "ui/WorkflowPanel.h"

#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace privateclaw::ui {

MainWindow::MainWindow(
    storage::DatabaseManager& databaseManager,
    services::SettingsService& settingsService,
    services::ProjectService& projectService,
    services::MemoryService& memoryService,
    services::WorkflowService& workflowService,
    providers::ProviderManager& providerManager,
    QWidget* parent
)
    : QMainWindow(parent)
    , m_databaseManager(databaseManager)
    , m_settingsService(settingsService)
    , m_projectService(projectService)
    , m_memoryService(memoryService)
    , m_workflowService(workflowService)
    , m_providerManager(providerManager)
{
    setWindowTitle("PrivateClaw");
    resize(1360, 840);
    buildUi();
    updateStatusBar();
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
    m_schedulePanel = new SchedulePanel(m_pages);

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
        updateStatusBar();
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
}

void MainWindow::updateStatusBar()
{
    statusBar()->showMessage(
        QString("Datenbank: %1 | Provider: %2 | Projekte: %3 | Workflows: %4 | Memory: %5")
            .arg(
                m_databaseManager.databasePath(),
                QString::number(m_providerManager.providers().size()),
                QString::number(m_projectService.projectCount()),
                QString::number(m_workflowService.workflowCount()),
                QString::number(m_memoryService.memoryCount())
            )
    );
}

} // namespace privateclaw::ui
