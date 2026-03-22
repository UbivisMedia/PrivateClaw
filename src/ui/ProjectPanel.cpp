#include "ui/ProjectPanel.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace privateclaw::ui {

ProjectPanel::ProjectPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* card = new QFrame(this);
    card->setProperty("panelCard", true);

    auto* cardLayout = new QVBoxLayout(card);
    auto* title = new QLabel("Projektverwaltung", card);
    title->setProperty("sectionTitle", true);

    auto* body = new QLabel(
        "Hier verwalten wir Projekte, Standardmodelle, Systemprompts und projektspezifische Metadaten.",
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

