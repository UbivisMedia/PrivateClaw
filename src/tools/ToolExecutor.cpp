#include "tools/ToolExecutor.h"

namespace privateclaw::tools {

QStringList ToolExecutor::availableTools() const
{
    return {
        "file.read",
        "file.write",
        "shell.command"
    };
}

} // namespace privateclaw::tools

