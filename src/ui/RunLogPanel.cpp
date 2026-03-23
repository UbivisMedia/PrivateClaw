#include "ui/RunLogPanel.h"

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace privateclaw::ui {

RunLogPanel::RunLogPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* card = new QFrame(this);
    card->setProperty("panelCard", true);

    auto* cardLayout = new QVBoxLayout(card);
    auto* title = new QLabel("Run-Protokoll", card);
    title->setProperty("sectionTitle", true);

    auto* body = new QLabel(
        "Dieser Bereich dient spaeter fuer Streaming-Ausgaben, Schrittstatus und Fehlerprotokolle.",
        card
    );
    body->setProperty("sectionBody", true);
    body->setWordWrap(true);

    m_logView = new QPlainTextEdit(card);
    m_logView->setReadOnly(true);
    m_logView->setPlainText(
        "[bootstrap] UI-Grundgeruest initialisiert.\n"
        "[todo] Workflow-Engine und Live-Logs anbinden."
    );

    cardLayout->addWidget(title);
    cardLayout->addWidget(body);
    cardLayout->addWidget(m_logView);
    layout->addWidget(card);
}

void RunLogPanel::clearLog()
{
    if (m_logView != nullptr) {
        m_logView->clear();
        m_hasUserLog = false;
    }
}

void RunLogPanel::appendLogLine(const QString& line)
{
    if (m_logView != nullptr) {
        if (!m_hasUserLog) {
            m_logView->clear();
            m_hasUserLog = true;
        }
        m_logView->appendPlainText(line);
    }
}

void RunLogPanel::setLogText(const QString& text)
{
    if (m_logView != nullptr) {
        m_logView->setPlainText(text);
        m_hasUserLog = !text.trimmed().isEmpty();
    }
}

} // namespace privateclaw::ui
