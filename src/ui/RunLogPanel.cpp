#include "ui/RunLogPanel.h"

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextCursor>
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
        "Hier erscheinen Live-Streaming, Schrittstatus und Fehlerprotokolle laufender Workflow-Ausfuehrungen.",
        card
    );
    body->setProperty("sectionBody", true);
    body->setWordWrap(true);

    m_logView = new QPlainTextEdit(card);
    m_logView->setReadOnly(true);
    m_logView->setPlainText(
        "[bootstrap] UI-Grundgeruest initialisiert.\n"
        "[ready] Live-Logs werden bei manuellen und geplanten Runs hier angehaengt."
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
        m_activeStreamId.clear();
    }
}

void RunLogPanel::appendLogLine(const QString& line)
{
    if (m_logView != nullptr) {
        finishActiveStream();
        if (!m_hasUserLog) {
            m_logView->clear();
            m_hasUserLog = true;
        }
        m_logView->appendPlainText(line);
    }
}

void RunLogPanel::appendStreamingChunk(const QString& streamId, const QString& prefix, const QString& chunk)
{
    if (m_logView == nullptr || streamId.trimmed().isEmpty() || chunk.isEmpty()) {
        return;
    }

    if (!m_hasUserLog) {
        m_logView->clear();
        m_hasUserLog = true;
    }

    if (m_activeStreamId != streamId) {
        finishActiveStream();
        appendRawText(prefix);
        m_activeStreamId = streamId;
    }

    appendRawText(chunk);
}

void RunLogPanel::setLogText(const QString& text)
{
    if (m_logView != nullptr) {
        m_logView->setPlainText(text);
        m_hasUserLog = !text.trimmed().isEmpty();
        m_activeStreamId.clear();
    }
}

void RunLogPanel::appendRawText(const QString& text)
{
    if (m_logView == nullptr || text.isEmpty()) {
        return;
    }

    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_logView->setTextCursor(cursor);
    m_logView->ensureCursorVisible();
}

void RunLogPanel::finishActiveStream()
{
    if (m_logView == nullptr || m_activeStreamId.isEmpty()) {
        return;
    }

    const QString currentText = m_logView->toPlainText();
    if (!currentText.isEmpty() && !currentText.endsWith('\n')) {
        appendRawText("\n");
    }

    m_activeStreamId.clear();
}

} // namespace privateclaw::ui
