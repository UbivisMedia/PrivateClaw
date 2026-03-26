#pragma once

#include <QFrame>
#include <QLayout>
#include <QMargins>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace privateclaw::ui {

struct CollapsibleCardParts
{
    QFrame* frame = nullptr;
    QToolButton* toggleButton = nullptr;
    QFrame* bodyFrame = nullptr;
    QVBoxLayout* bodyLayout = nullptr;
};

inline CollapsibleCardParts createCollapsibleCard(QWidget* parent, const QString& title, const bool expanded = true)
{
    CollapsibleCardParts parts;
    parts.frame = new QFrame(parent);
    parts.frame->setProperty("panelCard", true);

    auto* frameLayout = new QVBoxLayout(parts.frame);
    frameLayout->setContentsMargins(12, 10, 12, 10);
    frameLayout->setSpacing(8);

    parts.toggleButton = new QToolButton(parts.frame);
    parts.toggleButton->setCheckable(true);
    parts.toggleButton->setChecked(expanded);
    parts.toggleButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    parts.toggleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    parts.toggleButton->setText(title);
    parts.toggleButton->setCursor(Qt::PointingHandCursor);
    parts.toggleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    parts.toggleButton->setStyleSheet(
        "QToolButton {"
        " border: none;"
        " font-weight: 600;"
        " padding: 2px 0;"
        " text-align: left;"
        "}"
    );

    parts.bodyFrame = new QFrame(parts.frame);
    parts.bodyFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    parts.bodyLayout = new QVBoxLayout(parts.bodyFrame);
    parts.bodyLayout->setContentsMargins(0, 0, 0, 0);
    parts.bodyLayout->setSpacing(8);

    const auto applyExpandedState = [frame = parts.frame, frameLayout, button = parts.toggleButton, body = parts.bodyFrame](const bool checked) {
        body->setVisible(checked);
        button->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        frame->setSizePolicy(QSizePolicy::Preferred, checked ? QSizePolicy::Preferred : QSizePolicy::Fixed);
        const QMargins margins = frameLayout->contentsMargins();
        const int collapsedHeight = button->sizeHint().height()
            + margins.top()
            + margins.bottom()
            + frameLayout->spacing();
        frame->setMinimumHeight(0);
        frame->setMaximumHeight(checked ? QWIDGETSIZE_MAX : collapsedHeight);
        frame->updateGeometry();
        if (QWidget* parentWidget = frame->parentWidget(); parentWidget != nullptr) {
            if (QLayout* parentLayout = parentWidget->layout(); parentLayout != nullptr) {
                parentLayout->invalidate();
                parentLayout->activate();
            }
        }
    };

    QObject::connect(parts.toggleButton, &QToolButton::toggled, parts.frame, [applyExpandedState](const bool checked) {
        applyExpandedState(checked);
    });

    frameLayout->addWidget(parts.toggleButton);
    frameLayout->addWidget(parts.bodyFrame);
    applyExpandedState(expanded);

    return parts;
}

} // namespace privateclaw::ui
