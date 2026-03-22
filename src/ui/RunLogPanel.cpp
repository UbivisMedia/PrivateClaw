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

    auto* logView = new QPlainTextEdit(card);
    logView->setReadOnly(true);
    logView->setPlainText(
        "[bootstrap] UI-Grundgeruest initialisiert.\n"
        "[todo] Workflow-Engine und Live-Logs anbinden."
    );

    cardLayout->addWidget(title);
    cardLayout->addWidget(body);
    cardLayout->addWidget(logView);
    layout->addWidget(card);
}

} // namespace privateclaw::ui

