#include "ui/SchedulePanel.h"

#include "scheduler/SchedulerService.h"
#include "services/ProjectService.h"
#include "services/ScheduleService.h"
#include "services/WorkflowService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace privateclaw::ui {

namespace {

QString formatDateTimeLabel(const QDateTime& dateTime)
{
    if (!dateTime.isValid()) {
        return "noch nicht gelaufen";
    }

    return dateTime.toLocalTime().toString("dd.MM.yyyy HH:mm");
}

QString parseStoredTimeForEditor(const QString& triggerExpression)
{
    const QTime time = QTime::fromString(triggerExpression.trimmed(), "HH:mm");
    if (time.isValid()) {
        return time.toString("HH:mm");
    }

    return triggerExpression.trimmed();
}

} // namespace

SchedulePanel::SchedulePanel(
    services::ProjectService& projectService,
    services::WorkflowService& workflowService,
    services::ScheduleService& scheduleService,
    scheduler::SchedulerService& schedulerService,
    QWidget* parent
)
    : QWidget(parent)
    , m_projectService(projectService)
    , m_workflowService(workflowService)
    , m_scheduleService(scheduleService)
    , m_schedulerService(schedulerService)
{
    buildUi();
    refreshData();
}

void SchedulePanel::reloadData()
{
    refreshData(m_currentScheduleId);
}

void SchedulePanel::setOnScheduleDataChanged(std::function<void()> callback)
{
    m_onScheduleDataChanged = std::move(callback);
}

void SchedulePanel::setOnRunScheduleRequested(std::function<void(const domain::Schedule&)> callback)
{
    m_onRunScheduleRequested = std::move(callback);
}

void SchedulePanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* infoCard = new QFrame(this);
    infoCard->setProperty("panelCard", true);
    auto* infoLayout = new QVBoxLayout(infoCard);
    auto* title = new QLabel("Zeitplanung", infoCard);
    title->setProperty("sectionTitle", true);

    auto* body = new QLabel(
        "Hier planst du einmalige und wiederkehrende Workflow-Starts. Aktive Zeitplaene werden waehrend der laufenden App automatisch im Hintergrund ausgefuehrt.",
        infoCard
    );
    body->setWordWrap(true);
    body->setProperty("sectionBody", true);

    infoLayout->addWidget(title);
    infoLayout->addWidget(body);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);

    auto* listCard = new QFrame(contentSplitter);
    listCard->setProperty("panelCard", true);
    auto* listLayout = new QVBoxLayout(listCard);
    auto* listTitle = new QLabel("Zeitplaene", listCard);
    listTitle->setProperty("sectionTitle", true);
    auto* listBody = new QLabel(
        "Uebersicht ueber gespeicherte Zeitplaene. Auswahl laedt die Konfiguration in den Editor.",
        listCard
    );
    listBody->setProperty("sectionBody", true);
    listBody->setWordWrap(true);

    m_scheduleCountLabel = new QLabel("0 Zeitplaene", listCard);
    m_scheduleCountLabel->setProperty("sectionBody", true);

    m_scheduleList = new QListWidget(listCard);
    m_scheduleList->setAlternatingRowColors(true);

    auto* listActions = new QHBoxLayout();
    auto* refreshButton = new QPushButton("Liste aktualisieren", listCard);
    auto* newButton = new QPushButton("Neuer Zeitplan", listCard);
    listActions->addWidget(refreshButton);
    listActions->addWidget(newButton);
    listActions->addStretch();

    listLayout->addWidget(listTitle);
    listLayout->addWidget(listBody);
    listLayout->addWidget(m_scheduleCountLabel);
    listLayout->addWidget(m_scheduleList, 1);
    listLayout->addLayout(listActions);

    auto* editorCard = new QFrame(contentSplitter);
    editorCard->setProperty("panelCard", true);
    auto* editorLayout = new QVBoxLayout(editorCard);
    auto* editorTitle = new QLabel("Zeitplan bearbeiten", editorCard);
    editorTitle->setProperty("sectionTitle", true);
    auto* editorBody = new QLabel(
        "Ein Zeitplan referenziert genau ein Projekt und einen Workflow. Wiederkehrende Zeitplaene berechnen den naechsten Lauf automatisch nach jeder Ausfuehrung neu.",
        editorCard
    );
    editorBody->setProperty("sectionBody", true);
    editorBody->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);

    m_projectCombo = new QComboBox(editorCard);
    m_workflowCombo = new QComboBox(editorCard);
    m_enabledCheckBox = new QCheckBox("Zeitplan ist aktiv", editorCard);
    m_enabledCheckBox->setChecked(true);
    m_triggerTypeCombo = new QComboBox(editorCard);
    m_triggerTypeCombo->addItem("Einmalig", "once");
    m_triggerTypeCombo->addItem("Alle X Minuten", "interval_minutes");
    m_triggerTypeCombo->addItem("Taeglich um", "daily_time");

    m_triggerConfigStack = new QStackedWidget(editorCard);

    auto* oncePage = new QWidget(m_triggerConfigStack);
    auto* onceLayout = new QFormLayout(oncePage);
    onceLayout->setLabelAlignment(Qt::AlignLeft);
    m_onceDateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), oncePage);
    m_onceDateTimeEdit->setCalendarPopup(true);
    m_onceDateTimeEdit->setDisplayFormat("dd.MM.yyyy HH:mm");
    onceLayout->addRow("Startzeitpunkt", m_onceDateTimeEdit);
    m_triggerConfigStack->addWidget(oncePage);

    auto* intervalPage = new QWidget(m_triggerConfigStack);
    auto* intervalLayout = new QFormLayout(intervalPage);
    intervalLayout->setLabelAlignment(Qt::AlignLeft);
    m_intervalMinutesSpin = new QSpinBox(intervalPage);
    m_intervalMinutesSpin->setRange(1, 525600);
    m_intervalMinutesSpin->setValue(60);
    m_intervalMinutesSpin->setSuffix(" Min.");
    intervalLayout->addRow("Intervall", m_intervalMinutesSpin);
    m_triggerConfigStack->addWidget(intervalPage);

    auto* dailyPage = new QWidget(m_triggerConfigStack);
    auto* dailyLayout = new QFormLayout(dailyPage);
    dailyLayout->setLabelAlignment(Qt::AlignLeft);
    m_dailyTimeEdit = new QTimeEdit(QTime::currentTime().addSecs(3600), dailyPage);
    m_dailyTimeEdit->setDisplayFormat("HH:mm");
    dailyLayout->addRow("Uhrzeit", m_dailyTimeEdit);
    m_triggerConfigStack->addWidget(dailyPage);

    m_previewLabel = new QLabel("Naechster Lauf: noch nicht berechnet.", editorCard);
    m_previewLabel->setProperty("sectionBody", true);
    m_previewLabel->setWordWrap(true);

    m_lastRunLabel = new QLabel("Letzter Lauf: noch nicht gelaufen", editorCard);
    m_lastRunLabel->setProperty("sectionBody", true);
    m_lastRunLabel->setWordWrap(true);

    formLayout->addRow("Projekt", m_projectCombo);
    formLayout->addRow("Workflow", m_workflowCombo);
    formLayout->addRow("", m_enabledCheckBox);
    formLayout->addRow("Trigger", m_triggerTypeCombo);
    formLayout->addRow("Konfiguration", m_triggerConfigStack);

    auto* actions = new QHBoxLayout();
    auto* saveButton = new QPushButton("Zeitplan speichern", editorCard);
    auto* runNowButton = new QPushButton("Jetzt ausfuehren", editorCard);
    auto* deleteButton = new QPushButton("Zeitplan loeschen", editorCard);
    auto* resetButton = new QPushButton("Editor zuruecksetzen", editorCard);
    actions->addWidget(saveButton);
    actions->addWidget(runNowButton);
    actions->addWidget(deleteButton);
    actions->addWidget(resetButton);
    actions->addStretch();

    m_feedbackLabel = new QLabel("Bereit.", editorCard);
    m_feedbackLabel->setProperty("sectionBody", true);
    m_feedbackLabel->setWordWrap(true);

    editorLayout->addWidget(editorTitle);
    editorLayout->addWidget(editorBody);
    editorLayout->addLayout(formLayout);
    editorLayout->addWidget(m_previewLabel);
    editorLayout->addWidget(m_lastRunLabel);
    editorLayout->addLayout(actions);
    editorLayout->addWidget(m_feedbackLabel);
    editorLayout->addStretch();

    contentSplitter->setStretchFactor(0, 2);
    contentSplitter->setStretchFactor(1, 3);

    layout->addWidget(infoCard);
    layout->addWidget(contentSplitter, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refreshData(m_currentScheduleId);
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        resetEditor();
    });
    connect(m_scheduleList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadScheduleFromRow(row);
    });
    connect(m_projectCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshWorkflows(currentProjectId(), currentWorkflowId());
        updatePreview();
    });
    connect(m_workflowCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updatePreview();
    });
    connect(m_enabledCheckBox, &QCheckBox::toggled, this, [this]() {
        updatePreview();
    });
    connect(m_triggerTypeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateTriggerEditor();
        updatePreview();
    });
    connect(m_onceDateTimeEdit, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime&) {
        updatePreview();
    });
    connect(m_intervalMinutesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        updatePreview();
    });
    connect(m_dailyTimeEdit, &QTimeEdit::timeChanged, this, [this](const QTime&) {
        updatePreview();
    });
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        saveSchedule();
    });
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        deleteSchedule();
    });
    connect(runNowButton, &QPushButton::clicked, this, [this]() {
        requestRunNow();
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        resetEditor();
    });

    updateTriggerEditor();
    updatePreview();
}

void SchedulePanel::refreshData(const qint64 scheduleIdToSelect)
{
    const qint64 selectedProjectId = currentProjectId();
    refreshProjects(selectedProjectId);
    refreshWorkflows(currentProjectId(), currentWorkflowId());
    refreshScheduleList(scheduleIdToSelect);
}

void SchedulePanel::refreshProjects(const qint64 selectedProjectId)
{
    m_projects = m_projectService.listProjects();

    {
        const QSignalBlocker blocker(m_projectCombo);
        m_projectCombo->clear();
        for (const domain::Project& project : m_projects) {
            m_projectCombo->addItem(project.name, project.id);
        }
    }

    m_projectCombo->setEnabled(!m_projects.isEmpty());
    const int targetIndex = indexOfProject(selectedProjectId);
    if (!m_projects.isEmpty()) {
        m_projectCombo->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
    }
}

void SchedulePanel::refreshWorkflows(const qint64 projectId, const qint64 workflowIdToSelect)
{
    m_workflows = m_workflowService.listWorkflows();

    {
        const QSignalBlocker blocker(m_workflowCombo);
        m_workflowCombo->clear();
        for (const domain::Workflow& workflow : m_workflows) {
            if (projectId > 0 && workflow.projectId != projectId) {
                continue;
            }
            m_workflowCombo->addItem(workflow.name, workflow.id);
        }
    }

    m_workflowCombo->setEnabled(m_workflowCombo->count() > 0);
    int targetIndex = -1;
    if (workflowIdToSelect > 0) {
        targetIndex = m_workflowCombo->findData(workflowIdToSelect);
    }
    if (targetIndex < 0 && m_workflowCombo->count() > 0) {
        targetIndex = 0;
    }
    m_workflowCombo->setCurrentIndex(targetIndex);
}

void SchedulePanel::refreshScheduleList(const qint64 scheduleIdToSelect)
{
    const qint64 targetScheduleId = scheduleIdToSelect > 0 ? scheduleIdToSelect : m_currentScheduleId;
    m_schedules = m_scheduleService.listSchedules();

    int rowToSelect = -1;
    {
        const QSignalBlocker blocker(m_scheduleList);
        m_scheduleList->clear();

        for (int index = 0; index < m_schedules.size(); ++index) {
            const domain::Schedule& schedule = m_schedules.at(index);
            auto* item = new QListWidgetItem(formatScheduleLabel(schedule), m_scheduleList);
            item->setData(Qt::UserRole, schedule.id);
            item->setToolTip(
                QString("%1\nNaechster Lauf: %2")
                    .arg(m_schedulerService.describeSchedule(schedule))
                    .arg(formatDateTimeLabel(schedule.nextRunAt))
            );

            if (schedule.id == targetScheduleId) {
                rowToSelect = index;
            }
        }
    }

    m_scheduleCountLabel->setText(QString("%1 Zeitplaene").arg(m_schedules.size()));

    if (rowToSelect < 0 && !m_schedules.isEmpty() && targetScheduleId <= 0) {
        rowToSelect = 0;
    }

    if (rowToSelect >= 0) {
        m_scheduleList->setCurrentRow(rowToSelect);
    } else {
        m_currentScheduleId = -1;
        if (m_scheduleList->currentRow() >= 0) {
            m_scheduleList->setCurrentRow(-1);
        }
        if (m_schedules.isEmpty()) {
            resetEditor(true);
        }
    }

    if (rowToSelect >= 0) {
        loadScheduleFromRow(rowToSelect);
    }
}

void SchedulePanel::loadScheduleFromRow(const int row)
{
    if (row < 0 || row >= m_schedules.size()) {
        if (m_currentScheduleId <= 0) {
            resetEditor(true);
        }
        return;
    }

    const domain::Schedule& schedule = m_schedules.at(row);
    m_currentScheduleId = schedule.id;

    const int projectIndex = indexOfProject(schedule.projectId);
    if (projectIndex >= 0) {
        m_projectCombo->setCurrentIndex(projectIndex);
    }
    refreshWorkflows(schedule.projectId, schedule.workflowId);

    m_enabledCheckBox->setChecked(schedule.enabled);

    const int triggerIndex = m_triggerTypeCombo->findData(m_schedulerService.normalizedTriggerType(schedule.triggerType));
    m_triggerTypeCombo->setCurrentIndex(triggerIndex >= 0 ? triggerIndex : 0);
    updateTriggerEditor();

    if (m_schedulerService.normalizedTriggerType(schedule.triggerType) == "once") {
        const QDateTime dateTime = QDateTime::fromString(schedule.triggerExpression, Qt::ISODate);
        m_onceDateTimeEdit->setDateTime(dateTime.isValid() ? dateTime.toLocalTime() : QDateTime::currentDateTime().addSecs(3600));
    } else if (m_schedulerService.normalizedTriggerType(schedule.triggerType) == "daily_time") {
        const QTime dailyTime = QTime::fromString(parseStoredTimeForEditor(schedule.triggerExpression), "HH:mm");
        m_dailyTimeEdit->setTime(dailyTime.isValid() ? dailyTime : QTime::currentTime());
    } else {
        bool ok = false;
        const int minutes = schedule.triggerExpression.toInt(&ok);
        m_intervalMinutesSpin->setValue(ok && minutes > 0 ? minutes : 60);
    }

    m_lastRunLabel->setText(QString("Letzter Lauf: %1").arg(formatDateTimeLabel(schedule.lastRunAt)));
    updatePreview();
}

void SchedulePanel::saveSchedule()
{
    domain::Schedule schedule;
    schedule.id = m_currentScheduleId;
    schedule.projectId = currentProjectId();
    schedule.workflowId = currentWorkflowId();
    schedule.enabled = m_enabledCheckBox->isChecked();
    schedule.triggerType = m_triggerTypeCombo->currentData().toString();

    if (schedule.triggerType == "once") {
        schedule.triggerExpression = m_onceDateTimeEdit->dateTime().toUTC().toString(Qt::ISODate);
    } else if (schedule.triggerType == "daily_time") {
        schedule.triggerExpression = m_dailyTimeEdit->time().toString("HH:mm");
    } else {
        schedule.triggerExpression = QString::number(m_intervalMinutesSpin->value());
    }

    const domain::Schedule* existingSchedule = currentSchedule();
    if (existingSchedule != nullptr) {
        schedule.lastRunAt = existingSchedule->lastRunAt;
    }

    QString errorMessage;
    if (!m_scheduleService.saveSchedule(&schedule, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Zeitplan konnte nicht gespeichert werden: %1").arg(errorMessage));
        return;
    }

    m_currentScheduleId = schedule.id;
    m_lastRunLabel->setText(QString("Letzter Lauf: %1").arg(formatDateTimeLabel(schedule.lastRunAt)));
    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(
        QString("Zeitplan gespeichert. %1 | Naechster Lauf: %2")
            .arg(m_schedulerService.describeSchedule(schedule))
            .arg(formatDateTimeLabel(schedule.nextRunAt))
    );

    refreshData(schedule.id);
    if (m_onScheduleDataChanged) {
        m_onScheduleDataChanged();
    }
}

void SchedulePanel::deleteSchedule()
{
    if (m_currentScheduleId <= 0) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Zum Loeschen muss zuerst ein gespeicherter Zeitplan ausgewaehlt werden.");
        return;
    }

    QString errorMessage;
    if (!m_scheduleService.deleteSchedule(m_currentScheduleId, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Loeschen fehlgeschlagen: %1").arg(errorMessage));
        return;
    }

    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText("Zeitplan wurde geloescht.");
    m_currentScheduleId = -1;
    refreshData();
    if (m_onScheduleDataChanged) {
        m_onScheduleDataChanged();
    }
}

void SchedulePanel::requestRunNow()
{
    const domain::Schedule* schedule = currentSchedule();
    if (schedule == nullptr) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Bitte zuerst einen gespeicherten Zeitplan auswaehlen.");
        return;
    }

    if (!m_onRunScheduleRequested) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Manuelle Zeitplan-Ausfuehrung ist aktuell nicht verfuegbar.");
        return;
    }

    m_feedbackLabel->setStyleSheet("color: #5f5548;");
    m_feedbackLabel->setText("Zeitplan wird manuell gestartet.");
    m_onRunScheduleRequested(*schedule);
}

void SchedulePanel::resetEditor(const bool keepFeedback)
{
    m_currentScheduleId = -1;
    if (!m_projects.isEmpty()) {
        m_projectCombo->setCurrentIndex(0);
    }
    refreshWorkflows(currentProjectId());
    m_enabledCheckBox->setChecked(true);
    m_triggerTypeCombo->setCurrentIndex(0);
    m_onceDateTimeEdit->setDateTime(QDateTime::currentDateTime().addSecs(3600));
    m_intervalMinutesSpin->setValue(60);
    m_dailyTimeEdit->setTime(QTime::currentTime().addSecs(3600));
    m_lastRunLabel->setText("Letzter Lauf: noch nicht gelaufen");
    updateTriggerEditor();
    updatePreview();

    if (!keepFeedback) {
        m_feedbackLabel->setStyleSheet("color: #5f5548;");
        m_feedbackLabel->setText("Neuer Zeitplan vorbereitet.");
    }
}

void SchedulePanel::updateTriggerEditor()
{
    if (m_triggerConfigStack == nullptr || m_triggerTypeCombo == nullptr) {
        return;
    }

    const QString triggerType = m_triggerTypeCombo->currentData().toString();
    if (triggerType == "interval_minutes") {
        m_triggerConfigStack->setCurrentIndex(1);
        return;
    }

    if (triggerType == "daily_time") {
        m_triggerConfigStack->setCurrentIndex(2);
        return;
    }

    m_triggerConfigStack->setCurrentIndex(0);
}

void SchedulePanel::updatePreview()
{
    domain::Schedule schedule;
    schedule.projectId = currentProjectId();
    schedule.workflowId = currentWorkflowId();
    schedule.enabled = m_enabledCheckBox->isChecked();
    schedule.triggerType = m_triggerTypeCombo->currentData().toString();

    if (schedule.triggerType == "once") {
        schedule.triggerExpression = m_onceDateTimeEdit->dateTime().toUTC().toString(Qt::ISODate);
    } else if (schedule.triggerType == "daily_time") {
        schedule.triggerExpression = m_dailyTimeEdit->time().toString("HH:mm");
    } else {
        schedule.triggerExpression = QString::number(m_intervalMinutesSpin->value());
    }

    QString previewText = m_schedulerService.describeSchedule(schedule);
    const QString validationError = m_schedulerService.validateSchedule(schedule);
    if (!validationError.isEmpty()) {
        m_previewLabel->setText(QString("Vorschau: %1\nHinweis: %2").arg(previewText, validationError));
        return;
    }

    const QDateTime nextRunAt = m_schedulerService.calculateNextRunAtUtc(schedule, QDateTime::currentDateTime());
    const QString nextRunText = formatDateTimeLabel(nextRunAt);
    if (schedule.enabled) {
        m_previewLabel->setText(QString("Vorschau: %1\nNaechster Lauf: %2").arg(previewText, nextRunText));
    } else {
        m_previewLabel->setText(QString("Vorschau: %1\nPausiert. Bei Aktivierung waere der naechste Lauf: %2").arg(previewText, nextRunText));
    }
}

qint64 SchedulePanel::currentProjectId() const
{
    return m_projectCombo != nullptr ? m_projectCombo->currentData().toLongLong() : -1;
}

qint64 SchedulePanel::currentWorkflowId() const
{
    return m_workflowCombo != nullptr ? m_workflowCombo->currentData().toLongLong() : -1;
}

int SchedulePanel::indexOfProject(const qint64 projectId) const
{
    for (int index = 0; index < m_projects.size(); ++index) {
        if (m_projects.at(index).id == projectId) {
            return index;
        }
    }

    return -1;
}

int SchedulePanel::indexOfWorkflow(const qint64 workflowId) const
{
    for (int index = 0; index < m_workflows.size(); ++index) {
        if (m_workflows.at(index).id == workflowId) {
            return index;
        }
    }

    return -1;
}

QString SchedulePanel::projectNameForId(const qint64 projectId) const
{
    for (const domain::Project& project : m_projects) {
        if (project.id == projectId) {
            return project.name;
        }
    }

    return QString("Projekt %1").arg(projectId);
}

QString SchedulePanel::workflowNameForId(const qint64 workflowId) const
{
    for (const domain::Workflow& workflow : m_workflows) {
        if (workflow.id == workflowId) {
            return workflow.name;
        }
    }

    return QString("Workflow %1").arg(workflowId);
}

QString SchedulePanel::formatScheduleLabel(const domain::Schedule& schedule) const
{
    const QString state = schedule.enabled ? "Aktiv" : "Pausiert";
    return QString("%1 [%2] - %3 - %4")
        .arg(
            workflowNameForId(schedule.workflowId),
            projectNameForId(schedule.projectId),
            m_schedulerService.describeSchedule(schedule),
            state
        );
}

const domain::Schedule* SchedulePanel::currentSchedule() const
{
    for (const domain::Schedule& schedule : m_schedules) {
        if (schedule.id == m_currentScheduleId) {
            return &schedule;
        }
    }

    return nullptr;
}

} // namespace privateclaw::ui
