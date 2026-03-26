#include "ui/RunPanel.h"

#include "ui/CollapsibleCard.h"
#include "services/MemoryService.h"
#include "services/ProjectService.h"
#include "services/RunService.h"
#include "services/SettingsService.h"
#include "services/WorkflowService.h"
#include "utils/ModelResponseSanitizer.h"
#include "utils/PersistenceGuards.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace privateclaw::ui {

namespace {

QString formatDateTime(const QDateTime& dateTime)
{
    return dateTime.isValid() ? dateTime.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : "-";
}

QString previewText(QString text)
{
    text = text.simplified();
    if (text.size() > 88) {
        text = text.left(85) + "...";
    }
    return text;
}

QString normalizeLabelValue(const QString& value, const QString& fallback = "-")
{
    return value.trimmed().isEmpty() ? fallback : value.trimmed();
}

int warningCountFromLogText(const QString& logText)
{
    int count = 0;
    const QStringList lines = logText.split('\n');
    for (const QString& line : lines) {
        if (line.contains("[warn]", Qt::CaseInsensitive)) {
            ++count;
        }
    }

    return count;
}

} // namespace

RunPanel::RunPanel(
    services::SettingsService& settingsService,
    services::ProjectService& projectService,
    services::MemoryService& memoryService,
    services::WorkflowService& workflowService,
    services::RunService& runService,
    QWidget* parent
)
    : QWidget(parent)
    , m_settingsService(settingsService)
    , m_projectService(projectService)
    , m_memoryService(memoryService)
    , m_workflowService(workflowService)
    , m_runService(runService)
{
    buildUi();
    refreshData();
}

void RunPanel::reloadData()
{
    refreshData(m_currentRunId);
}

void RunPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    const CollapsibleCardParts infoCard = createCollapsibleCard(this, "Run-Historie", true);
    auto* infoLayout = infoCard.bodyLayout;
    auto* body = new QLabel(
        "Hier findest du gespeicherte manuelle und geplante Workflow-Laeufe inklusive Status, Ausgabe, Logs und Metadaten.",
        infoCard.bodyFrame
    );
    body->setProperty("sectionBody", true);
    body->setWordWrap(true);

    infoLayout->addWidget(body);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);

    const CollapsibleCardParts listCard = createCollapsibleCard(contentSplitter, "Gespeicherte Runs", true);
    auto* listLayout = listCard.bodyLayout;
    auto* listBody = new QLabel(
        "Gefiltert wird projektbezogen. Die neuesten Laeufe stehen oben und koennen direkt im Detailbereich geprueft werden.",
        listCard.bodyFrame
    );
    listBody->setProperty("sectionBody", true);
    listBody->setWordWrap(true);

    m_projectCombo = new QComboBox(listCard.bodyFrame);
    m_runCountLabel = new QLabel("0 Runs", listCard.bodyFrame);
    m_runCountLabel->setProperty("sectionBody", true);
    m_runList = new QListWidget(listCard.bodyFrame);
    m_runList->setAlternatingRowColors(true);

    auto* listActions = new QHBoxLayout();
    auto* refreshButton = new QPushButton("Liste aktualisieren", listCard.bodyFrame);
    auto* cleanupButton = new QPushButton("Kontamination bereinigen", listCard.bodyFrame);
    listActions->addWidget(refreshButton);
    listActions->addWidget(cleanupButton);
    listActions->addStretch();

    listLayout->addWidget(listBody);
    listLayout->addWidget(new QLabel("Projekt", listCard.bodyFrame));
    listLayout->addWidget(m_projectCombo);
    listLayout->addWidget(m_runCountLabel);
    listLayout->addWidget(m_runList, 1);
    listLayout->addLayout(listActions);

    const CollapsibleCardParts detailCard = createCollapsibleCard(contentSplitter, "Run-Details", true);
    auto* detailLayout = detailCard.bodyLayout;
    auto* detailBody = new QLabel(
        "Die Details zeigen den gespeicherten Laufzustand. Live-Streaming bleibt weiterhin im unteren Run-Protokoll sichtbar.",
        detailCard.bodyFrame
    );
    detailBody->setProperty("sectionBody", true);
    detailBody->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);
    m_statusValueLabel = new QLabel(detailCard.bodyFrame);
    m_originValueLabel = new QLabel(detailCard.bodyFrame);
    m_projectValueLabel = new QLabel(detailCard.bodyFrame);
    m_workflowValueLabel = new QLabel(detailCard.bodyFrame);
    m_providerValueLabel = new QLabel(detailCard.bodyFrame);
    m_modelValueLabel = new QLabel(detailCard.bodyFrame);
    m_startedAtValueLabel = new QLabel(detailCard.bodyFrame);
    m_finishedAtValueLabel = new QLabel(detailCard.bodyFrame);
    m_memoryCountValueLabel = new QLabel(detailCard.bodyFrame);
    m_warningCountValueLabel = new QLabel(detailCard.bodyFrame);
    m_summaryValueLabel = new QLabel(detailCard.bodyFrame);
    m_errorValueLabel = new QLabel(detailCard.bodyFrame);
    m_summaryValueLabel->setWordWrap(true);
    m_errorValueLabel->setWordWrap(true);

    formLayout->addRow("Status", m_statusValueLabel);
    formLayout->addRow("Herkunft", m_originValueLabel);
    formLayout->addRow("Projekt", m_projectValueLabel);
    formLayout->addRow("Workflow", m_workflowValueLabel);
    formLayout->addRow("Provider", m_providerValueLabel);
    formLayout->addRow("Modell", m_modelValueLabel);
    formLayout->addRow("Gestartet", m_startedAtValueLabel);
    formLayout->addRow("Beendet", m_finishedAtValueLabel);
    formLayout->addRow("Gespeichertes Memory", m_memoryCountValueLabel);
    formLayout->addRow("Warnungen", m_warningCountValueLabel);
    formLayout->addRow("Zusammenfassung", m_summaryValueLabel);
    formLayout->addRow("Fehler", m_errorValueLabel);

    auto* tabs = new QTabWidget(detailCard.bodyFrame);
    m_outputView = new QPlainTextEdit(tabs);
    m_outputView->setReadOnly(true);
    m_outputView->setPlaceholderText("Hier erscheint die gespeicherte Workflow-Ausgabe.");
    m_logView = new QPlainTextEdit(tabs);
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText("Hier erscheinen die gespeicherten Logs des Laufs.");
    tabs->addTab(m_outputView, "Ausgabe");
    tabs->addTab(m_logView, "Logs");

    detailLayout->addWidget(detailBody);
    detailLayout->addLayout(formLayout);
    detailLayout->addWidget(tabs, 1);

    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 5);

    layout->addWidget(infoCard.frame);
    layout->addWidget(contentSplitter, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refreshData(m_currentRunId);
    });
    connect(cleanupButton, &QPushButton::clicked, this, [this]() {
        cleanupContaminatedArtifacts();
    });

    connect(m_projectCombo, &QComboBox::currentIndexChanged, this, [this]() {
        refreshRuns();
    });

    connect(m_runList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadRunFromRow(row);
    });
}

void RunPanel::refreshData(const qint64 runIdToSelect)
{
    m_workflows = m_workflowService.listWorkflows();
    refreshProjects();
    refreshRuns(runIdToSelect);
}

void RunPanel::refreshProjects()
{
    const qint64 selectedProjectId = currentProjectId();
    m_projects = m_projectService.listProjects();

    {
        const QSignalBlocker blocker(m_projectCombo);
        m_projectCombo->clear();
        m_projectCombo->addItem("Alle Projekte", -1);
        for (const domain::Project& project : m_projects) {
            m_projectCombo->addItem(project.name, project.id);
        }
    }

    const int targetIndex = m_projectCombo->findData(selectedProjectId > 0 ? selectedProjectId : -1);
    m_projectCombo->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
}

void RunPanel::refreshRuns(const qint64 runIdToSelect)
{
    const qint64 projectId = currentProjectId();
    m_runs = m_runService.listRuns(projectId, 300);

    int rowToSelect = indexOfRun(runIdToSelect);
    {
        const QSignalBlocker blocker(m_runList);
        m_runList->clear();
        for (const domain::Run& run : m_runs) {
            auto* item = new QListWidgetItem(formatRunLabel(run), m_runList);
            item->setData(Qt::UserRole, run.id);
            item->setToolTip(normalizeLabelValue(run.summary, "Keine Zusammenfassung"));
        }

        if (rowToSelect >= 0) {
            m_runList->setCurrentRow(rowToSelect);
        } else {
            m_runList->setCurrentRow(m_runs.isEmpty() ? -1 : 0);
        }
    }

    m_runCountLabel->setText(QString("%1 Runs").arg(m_runs.size()));
    loadRunFromRow(m_runList->currentRow());
}

void RunPanel::loadRunFromRow(const int row)
{
    if (row < 0 || row >= m_runs.size()) {
        m_currentRunId = -1;
        clearDetails();
        return;
    }

    const domain::Run& run = m_runs.at(row);
    m_currentRunId = run.id;
    m_statusValueLabel->setText(normalizeLabelValue(run.status));
    m_originValueLabel->setText(normalizeLabelValue(run.origin));
    m_projectValueLabel->setText(projectNameForId(run.projectId));
    m_workflowValueLabel->setText(workflowNameForId(run.workflowId));
    m_providerValueLabel->setText(normalizeLabelValue(run.providerName));
    m_modelValueLabel->setText(normalizeLabelValue(run.modelName));
    m_startedAtValueLabel->setText(formatDateTime(run.startedAt));
    m_finishedAtValueLabel->setText(formatDateTime(run.finishedAt));
    m_memoryCountValueLabel->setText(QString::number(run.savedMemoryCount));
    m_warningCountValueLabel->setText(QString::number(warningCountForRun(run)));
    m_summaryValueLabel->setText(normalizeLabelValue(run.summary));
    m_errorValueLabel->setText(normalizeLabelValue(run.errorMessage));
    m_outputView->setPlainText(run.outputText);
    m_logView->setPlainText(run.logText);
}

int RunPanel::indexOfRun(const qint64 runId) const
{
    if (runId <= 0) {
        return -1;
    }

    for (int index = 0; index < m_runs.size(); ++index) {
        if (m_runs.at(index).id == runId) {
            return index;
        }
    }

    return -1;
}

qint64 RunPanel::currentProjectId() const
{
    return m_projectCombo != nullptr ? m_projectCombo->currentData().toLongLong() : -1;
}

qint64 RunPanel::effectiveCleanupProjectId() const
{
    const qint64 filteredProjectId = currentProjectId();
    if (filteredProjectId > 0) {
        return filteredProjectId;
    }

    const int row = m_runList != nullptr ? m_runList->currentRow() : -1;
    if (row >= 0 && row < m_runs.size()) {
        return m_runs.at(row).projectId;
    }

    return -1;
}

QString RunPanel::projectNameForId(const qint64 projectId) const
{
    for (const domain::Project& project : m_projects) {
        if (project.id == projectId) {
            return project.name;
        }
    }

    return QString("Projekt #%1").arg(projectId);
}

QString RunPanel::workflowNameForId(const qint64 workflowId) const
{
    for (const domain::Workflow& workflow : m_workflows) {
        if (workflow.id == workflowId) {
            return workflow.name;
        }
    }

    return QString("Workflow #%1").arg(workflowId);
}

QString RunPanel::formatRunLabel(const domain::Run& run) const
{
    const int warningCount = warningCountForRun(run);
    return QString("[%1%2] %3 | %4 | %5")
        .arg(run.status.trimmed().isEmpty() ? "unknown" : run.status.trimmed())
        .arg(warningCount > 0 ? QString(" | warn:%1").arg(warningCount) : QString())
        .arg(formatDateTime(run.startedAt))
        .arg(workflowNameForId(run.workflowId))
        .arg(previewText(run.summary.trimmed().isEmpty() ? run.outputText : run.summary));
}

int RunPanel::warningCountForRun(const domain::Run& run) const
{
    return warningCountFromLogText(run.logText);
}

void RunPanel::cleanupContaminatedArtifacts()
{
    const qint64 projectId = effectiveCleanupProjectId();
    if (projectId <= 0) {
        QMessageBox::information(
            this,
            "Bereinigung",
            "Waehle fuer die Bereinigung bitte ein Projekt im Filter oder einen konkreten Run aus."
        );
        return;
    }

    const QString projectName = projectNameForId(projectId);
    const QString workspaceRoot = m_settingsService.workspaceRoot().trimmed();
    const QString artifactsDirectory = workspaceRoot.isEmpty()
        ? QString()
        : QDir(workspaceRoot).absoluteFilePath("artifacts");

    QMessageBox confirmation(this);
    confirmation.setIcon(QMessageBox::Question);
    confirmation.setWindowTitle("Kontamination bereinigen");
    confirmation.setText(
        QString(
            "Die Bereinigung prueft Projekt-Runs, Memories und Artefakt-Dateien auf offensichtliche Thinking-/Meta-Leaks.\n\nProjekt: %1"
        ).arg(projectName)
    );
    confirmation.setInformativeText(
        artifactsDirectory.isEmpty()
            ? "Artefakt-Dateien werden nur bereinigt, wenn ein Workspace-Root konfiguriert ist."
            : QString("Artefakt-Dateien werden zusaetzlich unter '%1' durchsucht.").arg(artifactsDirectory)
    );
    confirmation.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    confirmation.setDefaultButton(QMessageBox::Ok);
    if (confirmation.exec() != QMessageBox::Ok) {
        return;
    }

    int updatedRuns = 0;
    int updatedMemories = 0;
    int deletedMemories = 0;
    int updatedFiles = 0;
    QStringList issues;

    const QList<domain::Run> runs = m_runService.listRuns(projectId, 0);
    for (domain::Run run : runs) {
        bool changed = false;

        const utils::SanitizedResponseContent sanitizedSummary = utils::sanitizeModelVisibleText(run.summary);
        if (sanitizedSummary.hadReasoningContent
            && !sanitizedSummary.visibleText.trimmed().isEmpty()
            && sanitizedSummary.visibleText != utils::cleanupSanitizedText(run.summary)) {
            run.summary = sanitizedSummary.visibleText;
            changed = true;
        }

        const utils::SanitizedResponseContent sanitizedOutput = utils::sanitizeModelVisibleText(run.outputText);
        if (sanitizedOutput.hadReasoningContent
            && !sanitizedOutput.visibleText.trimmed().isEmpty()
            && sanitizedOutput.visibleText != utils::cleanupSanitizedText(run.outputText)) {
            run.outputText = sanitizedOutput.visibleText;
            changed = true;
        }

        if (!changed) {
            continue;
        }

        const QString cleanupLogLine =
            QString("[%1] [warn] Run-Ausgabe wurde nachtraeglich von offensichtlichem Thinking-/Meta-Inhalt bereinigt.")
                .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        run.logText = run.logText.trimmed().isEmpty()
            ? cleanupLogLine
            : run.logText + '\n' + cleanupLogLine;

        QString errorMessage;
        if (!m_runService.finishRun(&run, &errorMessage)) {
            issues.append(QString("Run #%1: %2").arg(run.id).arg(errorMessage));
            continue;
        }

        ++updatedRuns;
    }

    const QList<domain::MemoryEntry> entries = m_memoryService.listEntries(projectId, QString(), 0);
    for (domain::MemoryEntry entry : entries) {
        const utils::StructuredFieldSummary structuredFields = utils::analyzeStructuredFields(entry.content);
        if (utils::isEntityLikeMemoryType(entry.type)
            && structuredFields.totalFieldCount >= 2
            && structuredFields.nonEmptyFieldCount == 0) {
            QString errorMessage;
            if (!m_memoryService.deleteEntry(entry.id, &errorMessage)) {
                issues.append(QString("Memory #%1: %2").arg(entry.id).arg(errorMessage));
            } else {
                ++deletedMemories;
            }
            continue;
        }

        const utils::SanitizedResponseContent sanitized = utils::sanitizeModelVisibleText(entry.content);
        if (!sanitized.hadReasoningContent
            || sanitized.visibleText.trimmed().isEmpty()
            || sanitized.visibleText == utils::cleanupSanitizedText(entry.content)) {
            continue;
        }

        entry.content = sanitized.visibleText;
        QString errorMessage;
        if (!m_memoryService.saveEntry(&entry, &errorMessage)) {
            issues.append(QString("Memory #%1: %2").arg(entry.id).arg(errorMessage));
            continue;
        }

        ++updatedMemories;
    }

    if (!artifactsDirectory.trimmed().isEmpty() && QDir(artifactsDirectory).exists()) {
        QDirIterator iterator(
            artifactsDirectory,
            QStringList{ "*.md", "*.txt", "*.json" },
            QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories
        );
        while (iterator.hasNext()) {
            const QString filePath = iterator.next();
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                issues.append(QString("Artefakt-Datei nicht lesbar: %1").arg(filePath));
                continue;
            }

            const QString content = QString::fromUtf8(file.readAll());
            file.close();

            const utils::SanitizedResponseContent sanitized = utils::sanitizeModelVisibleText(content);
            if (!sanitized.hadReasoningContent
                || sanitized.visibleText.trimmed().isEmpty()
                || sanitized.visibleText == utils::cleanupSanitizedText(content)) {
                continue;
            }

            QSaveFile saveFile(filePath);
            if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                issues.append(QString("Artefakt-Datei nicht schreibbar: %1").arg(filePath));
                continue;
            }

            saveFile.write(sanitized.visibleText.toUtf8());
            if (!saveFile.commit()) {
                issues.append(QString("Artefakt-Datei konnte nicht gespeichert werden: %1").arg(filePath));
                continue;
            }

            ++updatedFiles;
        }
    }

    reloadData();

    QString summary = QString(
        "Bereinigung abgeschlossen.\n\nRuns aktualisiert: %1\nMemories aktualisiert: %2\nMemories geloescht: %3\nArtefakt-Dateien aktualisiert: %4"
    ).arg(updatedRuns).arg(updatedMemories).arg(deletedMemories).arg(updatedFiles);
    if (!issues.isEmpty()) {
        summary += QString("\n\nMit Hinweisen:\n- %1").arg(issues.join("\n- "));
    }

    QMessageBox::information(this, "Bereinigung", summary);
}

void RunPanel::clearDetails()
{
    m_statusValueLabel->setText("-");
    m_originValueLabel->setText("-");
    m_projectValueLabel->setText("-");
    m_workflowValueLabel->setText("-");
    m_providerValueLabel->setText("-");
    m_modelValueLabel->setText("-");
    m_startedAtValueLabel->setText("-");
    m_finishedAtValueLabel->setText("-");
    m_memoryCountValueLabel->setText("0");
    m_warningCountValueLabel->setText("0");
    m_summaryValueLabel->setText("-");
    m_errorValueLabel->setText("-");
    m_outputView->clear();
    m_logView->clear();
}

} // namespace privateclaw::ui
