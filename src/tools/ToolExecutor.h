#pragma once

#include <QStringList>

namespace privateclaw::tools {

class ToolExecutor
{
public:
    QStringList availableTools() const;
};

} // namespace privateclaw::tools

