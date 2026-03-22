#include "ui/SchedulePanel.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace privateclaw::ui {

SchedulePanel::SchedulePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* card = new QFrame(this);
    card->setProperty("panelCard", true);

    auto* cardLayout = new QVBoxLayout(card);
    auto* title = new QLabel("Zeitplanung", card);
    title->setProperty("sectionTitle", true);

    auto* body = new QLabel(
        "Hier entstehen Scheduler-Ansichten fuer einmalige und wiederkehrende Workflow-Starts.",
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

