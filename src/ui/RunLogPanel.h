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
    void appendStreamingChunk(const QString& streamId, const QString& prefix, const QString& chunk);
    void setLogText(const QString& text);

private:
    void appendRawText(const QString& text);
    void finishActiveStream();

    bool m_hasUserLog = false;
    QString m_activeStreamId;
    QPlainTextEdit* m_logView = nullptr;
};

} // namespace privateclaw::ui
