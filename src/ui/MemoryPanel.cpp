#include "ui/MemoryPanel.h"

#include "services/MemoryService.h"
#include "services/ProjectService.h"

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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <utility>

namespace privateclaw::ui {

namespace {

QString previewText(QString text)
{
    text = text.simplified();
    if (text.size() > 96) {
        text = text.left(93) + "...";
    }
    return text;
}

QString formatTimestamp(const QDateTime& timestamp)
{
    return timestamp.isValid() ? timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm") : "Noch nicht gespeichert";
}

} // namespace

MemoryPanel::MemoryPanel(
    services::ProjectService& projectService,
    services::MemoryService& memoryService,
    QWidget* parent
)
    : QWidget(parent)
    , m_projectService(projectService)
    , m_memoryService(memoryService)
{
    buildUi();
    refreshData();
}

void MemoryPanel::reloadData()
{
    refreshData(m_currentEntryId);
}

void MemoryPanel::setOnMemoryDataChanged(std::function<void()> callback)
{
    m_onMemoryDataChanged = std::move(callback);
}

void MemoryPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* infoCard = new QFrame(this);
    infoCard->setProperty("panelCard", true);
    auto* infoLayout = new QVBoxLayout(infoCard);
    auto* infoTitle = new QLabel("Projekt-Erinnerung", infoCard);
    infoTitle->setProperty("sectionTitle", true);

    auto* infoBody = new QLabel(
        "Hier werden projektbezogene Fakten, Notizen, Entscheidungen und Kontextbausteine gespeichert. "
        "Angepinnte Eintraege werden bei Workflow-Runs bevorzugt beruecksichtigt, waehrend aeltere Inhalte "
        "automatisch verdichtet in den Kontext einfliessen koennen.",
        infoCard
    );
    infoBody->setWordWrap(true);
    infoBody->setProperty("sectionBody", true);

    infoLayout->addWidget(infoTitle);
    infoLayout->addWidget(infoBody);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);

    auto* listCard = new QFrame(contentSplitter);
    listCard->setProperty("panelCard", true);
    auto* listLayout = new QVBoxLayout(listCard);
    auto* listTitle = new QLabel("Memory-Eintraege", listCard);
    listTitle->setProperty("sectionTitle", true);

    auto* listBody = new QLabel(
        "Die Liste wird nach Relevanz und Aktualitaet sortiert. Suche filtert Inhalt, Quelle, Typ und Tags.",
        listCard
    );
    listBody->setProperty("sectionBody", true);
    listBody->setWordWrap(true);

    m_projectCombo = new QComboBox(listCard);
    m_searchEdit = new QLineEdit(listCard);
    m_searchEdit->setPlaceholderText("Eintraege durchsuchen");
    m_entryCountLabel = new QLabel("0 Eintraege", listCard);
    m_entryCountLabel->setProperty("sectionBody", true);

    m_entryList = new QListWidget(listCard);
    m_entryList->setAlternatingRowColors(true);

    auto* listActions = new QHBoxLayout();
    auto* refreshButton = new QPushButton("Liste aktualisieren", listCard);
    auto* newButton = new QPushButton("Neuer Eintrag", listCard);
    listActions->addWidget(refreshButton);
    listActions->addWidget(newButton);
    listActions->addStretch();

    listLayout->addWidget(listTitle);
    listLayout->addWidget(listBody);
    listLayout->addWidget(new QLabel("Projekt", listCard));
    listLayout->addWidget(m_projectCombo);
    listLayout->addWidget(m_searchEdit);
    listLayout->addWidget(m_entryCountLabel);
    listLayout->addWidget(m_entryList, 1);
    listLayout->addLayout(listActions);

    auto* editorCard = new QFrame(contentSplitter);
    editorCard->setProperty("panelCard", true);
    auto* editorLayout = new QVBoxLayout(editorCard);
    auto* editorTitle = new QLabel("Eintrag bearbeiten", editorCard);
    editorTitle->setProperty("sectionTitle", true);

    auto* editorBody = new QLabel(
        "Jeder Eintrag bleibt einem Projekt zugeordnet und kann spaeter von Workflows als Erinnerungskontext genutzt werden.",
        editorCard
    );
    editorBody->setProperty("sectionBody", true);
    editorBody->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);

    m_typeCombo = new QComboBox(editorCard);
    m_typeCombo->setEditable(true);
    m_typeCombo->addItems({ "note", "fact", "decision", "context", "todo" });

    m_sourceEdit = new QLineEdit(editorCard);
    m_sourceEdit->setPlaceholderText("z. B. Meeting, Ticket #12, Handnotiz");

    m_tagsEdit = new QLineEdit(editorCard);
    m_tagsEdit->setPlaceholderText("Tags mit Komma trennen");

    m_relevanceSpin = new QSpinBox(editorCard);
    m_relevanceSpin->setRange(0, 100);
    m_relevanceSpin->setValue(50);

    m_pinnedCheckBox = new QCheckBox("Wichtige Erinnerung anpinnen", editorCard);

    m_createdAtLabel = new QLabel("Noch nicht gespeichert", editorCard);
    m_createdAtLabel->setProperty("sectionBody", true);

    formLayout->addRow("Typ", m_typeCombo);
    formLayout->addRow("Quelle", m_sourceEdit);
    formLayout->addRow("Tags", m_tagsEdit);
    formLayout->addRow("Relevanz", m_relevanceSpin);
    formLayout->addRow("", m_pinnedCheckBox);
    formLayout->addRow("Erstellt", m_createdAtLabel);

    auto* contentLabel = new QLabel("Inhalt", editorCard);
    contentLabel->setProperty("sectionBody", true);

    m_contentEdit = new QPlainTextEdit(editorCard);
    m_contentEdit->setMinimumHeight(320);
    m_contentEdit->setPlaceholderText("Wichtige Projektinformationen, Entscheidungen oder offene Punkte");

    auto* editorActions = new QHBoxLayout();
    auto* saveButton = new QPushButton("Eintrag speichern", editorCard);
    auto* deleteButton = new QPushButton("Eintrag loeschen", editorCard);
    editorActions->addWidget(saveButton);
    editorActions->addWidget(deleteButton);
    editorActions->addStretch();

    m_feedbackLabel = new QLabel(editorCard);
    m_feedbackLabel->setProperty("sectionBody", true);
    m_feedbackLabel->setWordWrap(true);

    editorLayout->addWidget(editorTitle);
    editorLayout->addWidget(editorBody);
    editorLayout->addLayout(formLayout);
    editorLayout->addWidget(contentLabel);
    editorLayout->addWidget(m_contentEdit, 1);
    editorLayout->addLayout(editorActions);
    editorLayout->addWidget(m_feedbackLabel);

    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 5);

    layout->addWidget(infoCard);
    layout->addWidget(contentSplitter, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refreshData(m_currentEntryId);
    });

    connect(newButton, &QPushButton::clicked, this, [this]() {
        m_currentEntryId = -1;
        m_entryList->setCurrentRow(-1);
        clearEditor();
    });

    connect(m_projectCombo, &QComboBox::currentIndexChanged, this, [this]() {
        m_currentEntryId = -1;
        refreshEntryList();
        clearEditor(true);
    });

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        refreshEntryList(m_currentEntryId);
    });

    connect(m_entryList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadEntryFromRow(row);
    });

    connect(saveButton, &QPushButton::clicked, this, [this]() {
        saveEntry();
    });

    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        deleteEntry();
    });
}

void MemoryPanel::refreshData(const qint64 entryIdToSelect)
{
    const qint64 selectedProjectId = currentProjectId();
    refreshProjects(selectedProjectId);
    refreshEntryList(entryIdToSelect);
}

void MemoryPanel::refreshProjects(const qint64 selectedProjectId)
{
    m_projects = m_projectService.listProjects();

    int targetProjectIndex = -1;
    if (selectedProjectId > 0) {
        targetProjectIndex = indexOfProject(selectedProjectId);
    }
    if (targetProjectIndex < 0 && !m_projects.isEmpty()) {
        targetProjectIndex = 0;
    }

    {
        const QSignalBlocker blocker(m_projectCombo);
        m_projectCombo->clear();
        for (const domain::Project& project : m_projects) {
            m_projectCombo->addItem(project.name, project.id);
        }
    }

    const bool hasProjects = !m_projects.isEmpty();
    m_projectCombo->setEnabled(hasProjects);
    m_searchEdit->setEnabled(hasProjects);

    if (hasProjects && targetProjectIndex >= 0) {
        m_projectCombo->setCurrentIndex(targetProjectIndex);
    }

    if (!hasProjects) {
        m_currentEntryId = -1;
        m_entries.clear();
        m_entryList->clear();
        m_entryCountLabel->setText("0 Eintraege");
        clearEditor(true);
        setEditorEnabled(false);
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Bitte zuerst ein Projekt anlegen, bevor Erinnerung gespeichert werden kann.");
        return;
    }

    setEditorEnabled(true);
    if (m_feedbackLabel->text().contains("Bitte zuerst ein Projekt anlegen")) {
        m_feedbackLabel->clear();
        m_feedbackLabel->setStyleSheet(QString());
    }
}

void MemoryPanel::refreshEntryList(const qint64 entryIdToSelect)
{
    const qint64 projectId = currentProjectId();
    const qint64 targetEntryId = entryIdToSelect > 0 ? entryIdToSelect : m_currentEntryId;
    m_entries = m_memoryService.listEntries(projectId, m_searchEdit->text(), 300);
    int pinnedCount = 0;
    for (const domain::MemoryEntry& entry : m_entries) {
        if (entry.pinned) {
            ++pinnedCount;
        }
    }

    int rowToSelect = -1;
    {
        const QSignalBlocker blocker(m_entryList);
        m_entryList->clear();
        for (int index = 0; index < m_entries.size(); ++index) {
            const domain::MemoryEntry& entry = m_entries.at(index);
            auto* item = new QListWidgetItem(formatEntryLabel(entry), m_entryList);
            item->setData(Qt::UserRole, entry.id);
            item->setToolTip(entry.content);

            if (entry.id == targetEntryId) {
                rowToSelect = index;
            }
        }

        m_entryCountLabel->setText(QString("%1 Eintraege | %2 angepinnt").arg(m_entries.size()).arg(pinnedCount));
        if (rowToSelect < 0 && !m_entries.isEmpty() && targetEntryId <= 0) {
            rowToSelect = 0;
        }

        if (rowToSelect >= 0) {
            m_entryList->setCurrentRow(rowToSelect);
        } else {
            m_currentEntryId = -1;
            m_entryList->setCurrentRow(-1);
        }
    }

    loadEntryFromRow(m_entryList->currentRow());
}

void MemoryPanel::loadEntryFromRow(const int row)
{
    if (row < 0 || row >= m_entries.size()) {
        if (m_currentEntryId <= 0) {
            clearEditor(true);
        }
        return;
    }

    const domain::MemoryEntry& entry = m_entries.at(row);
    m_currentEntryId = entry.id;
    m_typeCombo->setEditText(entry.type);
    m_sourceEdit->setText(entry.source);
    m_tagsEdit->setText(entry.tags.join(", "));
    m_relevanceSpin->setValue(entry.relevance);
    m_pinnedCheckBox->setChecked(entry.pinned);
    m_createdAtLabel->setText(formatTimestamp(entry.createdAt));
    m_contentEdit->setPlainText(entry.content);
}

void MemoryPanel::saveEntry()
{
    domain::MemoryEntry entry;
    entry.id = m_currentEntryId;
    entry.projectId = currentProjectId();
    entry.type = m_typeCombo->currentText();
    entry.source = m_sourceEdit->text();
    entry.tags = m_tagsEdit->text().split(',', Qt::SkipEmptyParts);
    entry.relevance = m_relevanceSpin->value();
    entry.pinned = m_pinnedCheckBox->isChecked();
    entry.content = m_contentEdit->toPlainText();

    QString errorMessage;
    if (!m_memoryService.saveEntry(&entry, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Speichern fehlgeschlagen: %1").arg(errorMessage));
        return;
    }

    m_currentEntryId = entry.id;
    m_createdAtLabel->setText(formatTimestamp(entry.createdAt));
    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Memory-Eintrag '%1' wurde gespeichert.").arg(previewText(entry.content)));
    refreshEntryList(entry.id);

    if (m_onMemoryDataChanged) {
        m_onMemoryDataChanged();
    }
}

void MemoryPanel::deleteEntry()
{
    if (m_currentEntryId <= 0) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Bitte zuerst einen vorhandenen Memory-Eintrag auswaehlen.");
        return;
    }

    const QString entryLabel = previewText(m_contentEdit->toPlainText());
    const int answer = QMessageBox::question(
        this,
        "Memory-Eintrag loeschen",
        QString("Soll der Eintrag '%1' wirklich geloescht werden?").arg(entryLabel)
    );
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_memoryService.deleteEntry(m_currentEntryId, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Loeschen fehlgeschlagen: %1").arg(errorMessage));
        return;
    }

    m_currentEntryId = -1;
    refreshEntryList();
    clearEditor(true);
    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Memory-Eintrag '%1' wurde geloescht.").arg(entryLabel));

    if (m_onMemoryDataChanged) {
        m_onMemoryDataChanged();
    }
}

void MemoryPanel::clearEditor(const bool keepFeedback)
{
    m_currentEntryId = -1;
    m_typeCombo->setCurrentText("note");
    m_sourceEdit->clear();
    m_tagsEdit->clear();
    m_relevanceSpin->setValue(50);
    m_pinnedCheckBox->setChecked(false);
    m_createdAtLabel->setText("Noch nicht gespeichert");
    m_contentEdit->clear();

    if (!keepFeedback) {
        m_feedbackLabel->clear();
        m_feedbackLabel->setStyleSheet(QString());
    }
}

void MemoryPanel::setEditorEnabled(const bool enabled)
{
    m_typeCombo->setEnabled(enabled);
    m_sourceEdit->setEnabled(enabled);
    m_tagsEdit->setEnabled(enabled);
    m_relevanceSpin->setEnabled(enabled);
    m_pinnedCheckBox->setEnabled(enabled);
    m_contentEdit->setEnabled(enabled);
    m_entryList->setEnabled(enabled);
}

int MemoryPanel::indexOfProject(const qint64 projectId) const
{
    for (int index = 0; index < m_projects.size(); ++index) {
        if (m_projects.at(index).id == projectId) {
            return index;
        }
    }

    return -1;
}

qint64 MemoryPanel::currentProjectId() const
{
    return m_projectCombo->currentData().toLongLong();
}

QString MemoryPanel::formatEntryLabel(const domain::MemoryEntry& entry) const
{
    const QString timestamp = entry.createdAt.isValid()
        ? entry.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm")
        : "ohne Datum";
    const QString type = entry.type.trimmed().isEmpty() ? "note" : entry.type.trimmed();
    const QString pinMarker = entry.pinned ? "[PIN] " : QString();
    return QString("%1%2 | %3 | R%4 | %5")
        .arg(pinMarker, timestamp, type, QString::number(entry.relevance), previewText(entry.content));
}

} // namespace privateclaw::ui
