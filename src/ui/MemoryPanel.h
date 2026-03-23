#pragma once

#include "domain/MemoryEntry.h"
#include "domain/Project.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;

namespace privateclaw::services {
class MemoryService;
class ProjectService;
}

namespace privateclaw::ui {

class MemoryPanel : public QWidget
{
public:
    MemoryPanel(
        services::ProjectService& projectService,
        services::MemoryService& memoryService,
        QWidget* parent = nullptr
    );

    void reloadData();
    void setOnMemoryDataChanged(std::function<void()> callback);

private:
    void buildUi();
    void refreshData(qint64 entryIdToSelect = -1);
    void refreshProjects(qint64 selectedProjectId = -1);
    void refreshEntryList(qint64 entryIdToSelect = -1);
    void loadEntryFromRow(int row);
    void saveEntry();
    void deleteEntry();
    void clearEditor(bool keepFeedback = false);
    void setEditorEnabled(bool enabled);
    int indexOfProject(qint64 projectId) const;
    qint64 currentProjectId() const;
    QString formatEntryLabel(const domain::MemoryEntry& entry) const;

    services::ProjectService& m_projectService;
    services::MemoryService& m_memoryService;

    QList<domain::Project> m_projects;
    QList<domain::MemoryEntry> m_entries;
    qint64 m_currentEntryId = -1;
    std::function<void()> m_onMemoryDataChanged;

    QLabel* m_entryCountLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QComboBox* m_projectCombo = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_entryList = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QLineEdit* m_sourceEdit = nullptr;
    QLineEdit* m_tagsEdit = nullptr;
    QSpinBox* m_relevanceSpin = nullptr;
    QLabel* m_createdAtLabel = nullptr;
    QPlainTextEdit* m_contentEdit = nullptr;
};

} // namespace privateclaw::ui
