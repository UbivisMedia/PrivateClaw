#include "ui/WorkflowPanel.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace privateclaw::ui {

WorkflowPanel::WorkflowPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* card = new QFrame(this);
    card->setProperty("panelCard", true);

    auto* cardLayout = new QVBoxLayout(card);
    auto* title = new QLabel("Workflow-Editor", card);
    title->setProperty("sectionTitle", true);

    auto* body = new QLabel(
        "Dieser Bereich wird fuer JSON- oder visuell definierte Mehrschritt-Workflows vorbereitet.",
        card
    );
    body->setWordWrap(true);
    body->setProperty("sectionBody", true);

    cardLayout->addWidget(title);
    cardLayout->addWidget(body);
    layout->addWidget(card);
    layout->addStretch();
}

} // namespace privateclaw::ui

