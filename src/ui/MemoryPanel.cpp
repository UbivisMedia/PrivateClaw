#include "ui/MemoryPanel.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace privateclaw::ui {

MemoryPanel::MemoryPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* card = new QFrame(this);
    card->setProperty("panelCard", true);

    auto* cardLayout = new QVBoxLayout(card);
    auto* title = new QLabel("Projekt-Erinnerung", card);
    title->setProperty("sectionTitle", true);

    auto* body = new QLabel(
        "Hier landen spaeter Memory-Eintraege, Tags, Volltextsuche und Relevanzansichten pro Projekt.",
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

