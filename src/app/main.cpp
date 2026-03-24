#include "app/ApplicationBootstrap.h"

#include <QApplication>
#include <QFile>

namespace {

void applyStyleSheet(QApplication& app)
{
    QFile styleFile(":/styles/app.qss");
    if (!styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PrivateClaw");
    QApplication::setOrganizationName("PrivateClaw");
    QApplication::setOrganizationDomain("local.privateclaw");
    QApplication::setApplicationVersion(QString::fromUtf8(PRIVATECLAW_APP_VERSION));

    applyStyleSheet(app);

    privateclaw::app::ApplicationBootstrap bootstrap;
    QString errorMessage;
    if (!bootstrap.initialize(&errorMessage)) {
        bootstrap.showStartupError(errorMessage);
        return 1;
    }

    return bootstrap.run();
}
