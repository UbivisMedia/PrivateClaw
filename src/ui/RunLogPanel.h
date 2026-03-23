#pragma once

#include <QString>
#include <QWidget>

class QPlainTextEdit;

namespace privateclaw::ui {

class RunLogPanel : public QWidget
{
public:
    explicit RunLogPanel(QWidget* parent = nullptr);

    void clearLog();
    void appendLogLine(const QString& line);
    void setLogText(const QString& text);

private:
    bool m_hasUserLog = false;
    QPlainTextEdit* m_logView = nullptr;
};

} // namespace privateclaw::ui
