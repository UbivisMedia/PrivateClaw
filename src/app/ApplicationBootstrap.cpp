#include "app/ApplicationBootstrap.h"

#include "providers/LmStudioProvider.h"
#include "providers/OllamaProvider.h"
#include "ui/MainWindow.h"
#include "utils/Logger.h"

#include <QApplication>
#include <QMessageBox>

namespace privateclaw::app {

ApplicationBootstrap::ApplicationBootstrap()
    : m_databaseManager("privateclaw-main")
    , m_projectService(m_databaseManager)
    , m_memoryService(m_databaseManager)
{
}

ApplicationBootstrap::~ApplicationBootstrap() = default;

bool ApplicationBootstrap::initialize(QString* errorMessage)
{
    utils::Logger::initialize();
    utils::Logger::info("bootstrap", "Initializing application services");

    if (!m_settingsService.initialize()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Die Anwendungseinstellungen konnten nicht initialisiert werden.";
        }
        return false;
    }

    if (!m_databaseManager.initialize(errorMessage)) {
        return false;
    }

    registerProviders();

    m_mainWindow = std::make_unique<ui::MainWindow>(
        m_databaseManager,
        m_settingsService,
        m_providerManager
    );

    return true;
}

int ApplicationBootstrap::run()
{
    if (m_mainWindow) {
        m_mainWindow->show();
    }

    return QApplication::exec();
}

void ApplicationBootstrap::showStartupError(const QString& message) const
{
    QMessageBox::critical(
        nullptr,
        "PrivateClaw",
        message.isEmpty() ? "Die Anwendung konnte nicht gestartet werden." : message
    );
}

void ApplicationBootstrap::registerProviders()
{
    m_providerManager.addProvider(std::make_unique<providers::OllamaProvider>(
        m_settingsService.ollamaBaseUrl()
    ));
    m_providerManager.addProvider(std::make_unique<providers::LmStudioProvider>(
        m_settingsService.lmStudioBaseUrl()
    ));
}

} // namespace privateclaw::app

