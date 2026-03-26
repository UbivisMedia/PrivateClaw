#include "ui/ProjectVariablePanel.h"

#include "ui/CollapsibleCard.h"
#include "services/ProjectService.h"
#include "services/ProjectVariableService.h"

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

ProjectVariablePanel::ProjectVariablePanel(
    services::ProjectService& projectService,
    services::ProjectVariableService& projectVariableService,
    QWidget* parent
)
    : QWidget(parent)
    , m_projectService(projectService)
    , m_projectVariableService(projectVariableService)
{
    buildUi();
    refreshData();
}

void ProjectVariablePanel::reloadData()
{
    refreshData(m_currentVariableId);
}

void ProjectVariablePanel::setOnVariableDataChanged(std::function<void()> callback)
{
    m_onVariableDataChanged = std::move(callback);
}

void ProjectVariablePanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    const CollapsibleCardParts infoCard = createCollapsibleCard(this, "Projektvariablen", true);
    auto* infoLayout = infoCard.bodyLayout;
    auto* infoBody = new QLabel(
        "Projektvariablen sind persistente Werte wie Zaehler, Slugs oder einfache Statuswerte. "
        "Im Workflow stehen sie immer als {{project_var.name}} bereit und, wenn kein interner Name kollidiert, "
        "zusaetzlich direkt als {{name}}. Mit dem Tool variables.set kannst du sie ueber scope=project "
        "direkt aus Workflows heraus aktualisieren.",
        infoCard.bodyFrame
    );
    infoBody->setWordWrap(true);
    infoBody->setProperty("sectionBody", true);
    infoLayout->addWidget(infoBody);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);

    const CollapsibleCardParts listCard = createCollapsibleCard(contentSplitter, "Variablenliste", true);
    auto* listLayout = listCard.bodyLayout;
    auto* listBody = new QLabel(
        "Waehle zuerst ein Projekt. Die Liste zeigt alle persistenten Variablen des Projekts nach letzter Aktualisierung.",
        listCard.bodyFrame
    );
    listBody->setProperty("sectionBody", true);
    listBody->setWordWrap(true);

    m_projectCombo = new QComboBox(listCard.bodyFrame);
    m_variableCountLabel = new QLabel("0 Variablen", listCard.bodyFrame);
    m_variableCountLabel->setProperty("sectionBody", true);

    m_variableList = new QListWidget(listCard.bodyFrame);
    m_variableList->setAlternatingRowColors(true);

    auto* listActions = new QHBoxLayout();
    auto* refreshButton = new QPushButton("Liste aktualisieren", listCard.bodyFrame);
    auto* newButton = new QPushButton("Neue Variable", listCard.bodyFrame);
    listActions->addWidget(refreshButton);
    listActions->addWidget(newButton);
    listActions->addStretch();

    listLayout->addWidget(listBody);
    listLayout->addWidget(new QLabel("Projekt", listCard.bodyFrame));
    listLayout->addWidget(m_projectCombo);
    listLayout->addWidget(m_variableCountLabel);
    listLayout->addWidget(m_variableList, 1);
    listLayout->addLayout(listActions);

    const CollapsibleCardParts editorCard = createCollapsibleCard(contentSplitter, "Variable bearbeiten", true);
    auto* editorLayout = editorCard.bodyLayout;
    auto* editorBody = new QLabel(
        "Strings, Integer und Floats werden typisiert gespeichert. Fuer numerische Typen wird der Wert bereits beim Speichern validiert.",
        editorCard.bodyFrame
    );
    editorBody->setProperty("sectionBody", true);
    editorBody->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);

    m_nameEdit = new QLineEdit(editorCard.bodyFrame);
    m_nameEdit->setPlaceholderText("z. B. kapitel_nummer");

    m_typeCombo = new QComboBox(editorCard.bodyFrame);
    m_typeCombo->addItem("String", "string");
    m_typeCombo->addItem("Integer", "int");
    m_typeCombo->addItem("Float", "float");

    m_createdAtLabel = new QLabel("Noch nicht gespeichert", editorCard.bodyFrame);
    m_createdAtLabel->setProperty("sectionBody", true);
    m_updatedAtLabel = new QLabel("Noch nicht gespeichert", editorCard.bodyFrame);
    m_updatedAtLabel->setProperty("sectionBody", true);

    formLayout->addRow("Name", m_nameEdit);
    formLayout->addRow("Typ", m_typeCombo);
    formLayout->addRow("Erstellt", m_createdAtLabel);
    formLayout->addRow("Aktualisiert", m_updatedAtLabel);

    auto* valueLabel = new QLabel("Wert", editorCard.bodyFrame);
    valueLabel->setProperty("sectionBody", true);

    m_valueEdit = new QPlainTextEdit(editorCard.bodyFrame);
    m_valueEdit->setMinimumHeight(220);
    m_valueEdit->setPlaceholderText("Persistenter Wert fuer dieses Projekt");

    auto* actions = new QHBoxLayout();
    auto* saveButton = new QPushButton("Variable speichern", editorCard.bodyFrame);
    auto* deleteButton = new QPushButton("Variable loeschen", editorCard.bodyFrame);
    actions->addWidget(saveButton);
    actions->addWidget(deleteButton);
    actions->addStretch();

    m_feedbackLabel = new QLabel(editorCard.bodyFrame);
    m_feedbackLabel->setProperty("sectionBody", true);
    m_feedbackLabel->setWordWrap(true);

    editorLayout->addWidget(editorBody);
    editorLayout->addLayout(formLayout);
    editorLayout->addWidget(valueLabel);
    editorLayout->addWidget(m_valueEdit, 1);
    editorLayout->addLayout(actions);
    editorLayout->addWidget(m_feedbackLabel);

    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 5);

    layout->addWidget(infoCard.frame);
    layout->addWidget(contentSplitter, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refreshData(m_currentVariableId);
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        m_currentVariableId = -1;
        if (m_variableList != nullptr) {
            m_variableList->setCurrentRow(-1);
        }
        clearEditor();
    });
    connect(m_projectCombo, &QComboBox::currentIndexChanged, this, [this]() {
        m_currentVariableId = -1;
        refreshVariableList();
        clearEditor(true);
    });
    connect(m_variableList, &QListWidget::currentRowChanged, this, [this](const int row) {
        loadVariableFromRow(row);
    });
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        saveVariable();
    });
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        deleteVariable();
    });
}

void ProjectVariablePanel::refreshData(const qint64 variableIdToSelect)
{
    const qint64 selectedProjectId = currentProjectId();
    refreshProjects(selectedProjectId);
    refreshVariableList(variableIdToSelect);
    if (m_currentVariableId <= 0) {
        clearEditor(true);
    }
}

void ProjectVariablePanel::refreshProjects(const qint64 selectedProjectId)
{
    m_projects = m_projectService.listProjects();

    if (m_projectCombo == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_projectCombo);
    m_projectCombo->clear();
    for (const domain::Project& project : m_projects) {
        m_projectCombo->addItem(project.name, project.id);
    }

    int indexToSelect = indexOfProject(selectedProjectId);
    if (indexToSelect < 0 && !m_projects.isEmpty()) {
        indexToSelect = 0;
    }
    m_projectCombo->setCurrentIndex(indexToSelect);
}

void ProjectVariablePanel::refreshVariableList(const qint64 variableIdToSelect)
{
    m_variables = m_projectVariableService.listVariables(currentProjectId());

    if (m_variableCountLabel != nullptr) {
        m_variableCountLabel->setText(QString("%1 Variablen").arg(m_variables.size()));
    }

    if (m_variableList == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_variableList);
    m_variableList->clear();

    int rowToSelect = -1;
    for (int row = 0; row < m_variables.size(); ++row) {
        const domain::ProjectVariable& variable = m_variables.at(row);
        auto* item = new QListWidgetItem(formatVariableLabel(variable), m_variableList);
        item->setData(Qt::UserRole, variable.id);
        if (variable.id == variableIdToSelect) {
            rowToSelect = row;
        }
    }

    if (rowToSelect >= 0) {
        m_variableList->setCurrentRow(rowToSelect);
        return;
    }

    m_currentVariableId = -1;
    if (!m_variables.isEmpty()) {
        m_variableList->setCurrentRow(0);
        loadVariableFromRow(0);
        return;
    }

    clearEditor(true);
}

void ProjectVariablePanel::loadVariableFromRow(const int row)
{
    if (row < 0 || row >= m_variables.size()) {
        m_currentVariableId = -1;
        clearEditor(true);
        return;
    }

    const domain::ProjectVariable& variable = m_variables.at(row);
    m_currentVariableId = variable.id;

    m_nameEdit->setText(variable.name);
    const int typeIndex = m_typeCombo->findData(variable.valueType.trimmed().isEmpty() ? "string" : variable.valueType);
    m_typeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
    m_createdAtLabel->setText(formatTimestamp(variable.createdAt));
    m_updatedAtLabel->setText(formatTimestamp(variable.updatedAt));
    m_valueEdit->setPlainText(variable.valueText);

    m_feedbackLabel->clear();
}

void ProjectVariablePanel::saveVariable()
{
    const qint64 projectId = currentProjectId();
    if (projectId <= 0) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText("Zum Speichern muss zuerst ein Projekt ausgewaehlt werden.");
        return;
    }

    domain::ProjectVariable variable;
    variable.id = m_currentVariableId;
    variable.projectId = projectId;
    variable.name = m_nameEdit->text();
    variable.valueType = m_typeCombo->currentData().toString();
    variable.valueText = m_valueEdit->toPlainText();

    QString errorMessage;
    if (!m_projectVariableService.saveVariable(&variable, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Projektvariable konnte nicht gespeichert werden: %1").arg(errorMessage));
        return;
    }

    m_currentVariableId = variable.id;
    refreshVariableList(variable.id);
    m_createdAtLabel->setText(formatTimestamp(variable.createdAt));
    m_updatedAtLabel->setText(formatTimestamp(variable.updatedAt));
    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Projektvariable '%1' wurde gespeichert.").arg(variable.name));

    if (m_onVariableDataChanged) {
        m_onVariableDataChanged();
    }
}

void ProjectVariablePanel::deleteVariable()
{
    if (m_currentVariableId <= 0) {
        m_feedbackLabel->setStyleSheet("color: #8b5e2f;");
        m_feedbackLabel->setText("Es ist noch keine gespeicherte Projektvariable ausgewaehlt.");
        return;
    }

    const int row = m_variableList != nullptr ? m_variableList->currentRow() : -1;
    const QString variableName = (row >= 0 && row < m_variables.size()) ? m_variables.at(row).name : "diese Variable";
    const QMessageBox::StandardButton confirmation = QMessageBox::question(
        this,
        "Projektvariable loeschen",
        QString("Soll '%1' wirklich geloescht werden?").arg(variableName)
    );
    if (confirmation != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_projectVariableService.deleteVariable(m_currentVariableId, &errorMessage)) {
        m_feedbackLabel->setStyleSheet("color: #8b2f2f;");
        m_feedbackLabel->setText(QString("Projektvariable konnte nicht geloescht werden: %1").arg(errorMessage));
        return;
    }

    m_currentVariableId = -1;
    refreshVariableList();
    clearEditor(true);
    m_feedbackLabel->setStyleSheet("color: #2f6b3a;");
    m_feedbackLabel->setText(QString("Projektvariable '%1' wurde geloescht.").arg(variableName));

    if (m_onVariableDataChanged) {
        m_onVariableDataChanged();
    }
}

void ProjectVariablePanel::clearEditor(const bool keepFeedback)
{
    if (m_nameEdit != nullptr) {
        m_nameEdit->clear();
    }
    if (m_typeCombo != nullptr) {
        m_typeCombo->setCurrentIndex(0);
    }
    if (m_createdAtLabel != nullptr) {
        m_createdAtLabel->setText("Noch nicht gespeichert");
    }
    if (m_updatedAtLabel != nullptr) {
        m_updatedAtLabel->setText("Noch nicht gespeichert");
    }
    if (m_valueEdit != nullptr) {
        m_valueEdit->clear();
    }
    if (!keepFeedback && m_feedbackLabel != nullptr) {
        m_feedbackLabel->clear();
    }
}

int ProjectVariablePanel::indexOfProject(const qint64 projectId) const
{
    for (int index = 0; index < m_projects.size(); ++index) {
        if (m_projects.at(index).id == projectId) {
            return index;
        }
    }
    return -1;
}

qint64 ProjectVariablePanel::currentProjectId() const
{
    if (m_projectCombo == nullptr || m_projectCombo->currentIndex() < 0) {
        return -1;
    }
    return m_projectCombo->currentData().toLongLong();
}

QString ProjectVariablePanel::formatVariableLabel(const domain::ProjectVariable& variable) const
{
    return QString("%1 [%2] - %3").arg(variable.name, variable.valueType, previewText(variable.valueText));
}

} // namespace privateclaw::ui
