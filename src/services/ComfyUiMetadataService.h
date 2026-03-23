#pragma once

#include <QString>
#include <QStringList>

namespace privateclaw::services {

struct ComfyUiCatalog
{
    bool success = false;
    QString errorMessage;
    QStringList checkpoints;
    QStringList loras;
    QStringList vaes;
    QStringList inputImages;
    QStringList maskChannels;
    QStringList samplers;
    QStringList schedulers;
    int widthDefault = 1024;
    int heightDefault = 1024;
    int batchDefault = 1;
    int stepsDefault = 20;
    double cfgDefault = 8.0;
    double denoiseDefault = 1.0;
    int clipLayerMin = -24;
    int clipLayerMax = -1;
    int clipLayerDefault = -1;
    QString samplerDefault;
    QString schedulerDefault;
    QString filenamePrefixDefault = "PrivateClaw";
};

class ComfyUiMetadataService
{
public:
    static ComfyUiCatalog fetchCatalog(const QString& baseUrl, int timeoutMs = 5000);
};

} // namespace privateclaw::services
