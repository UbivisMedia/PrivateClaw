#include "services/ComfyUiMetadataService.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace privateclaw::services {

namespace {

QStringList optionListFromNode(const QJsonObject& nodeObject, const QString& fieldName)
{
    const QJsonArray requiredArray = nodeObject.value("input").toObject().value("required").toObject().value(fieldName).toArray();
    if (requiredArray.isEmpty() || !requiredArray.at(0).isArray()) {
        return {};
    }

    QStringList values;
    const QJsonArray valuesArray = requiredArray.at(0).toArray();
    values.reserve(valuesArray.size());
    for (const QJsonValue& value : valuesArray) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

QJsonObject fieldMetadataFromNode(const QJsonObject& nodeObject, const QString& fieldName)
{
    const QJsonArray requiredArray = nodeObject.value("input").toObject().value("required").toObject().value(fieldName).toArray();
    if (requiredArray.size() < 2 || !requiredArray.at(1).isObject()) {
        return {};
    }
    return requiredArray.at(1).toObject();
}

int intDefaultFromNode(
    const QJsonObject& nodeObject,
    const QString& fieldName,
    const int fallback
)
{
    const QJsonObject metadata = fieldMetadataFromNode(nodeObject, fieldName);
    return metadata.value("default").toInt(fallback);
}

double doubleDefaultFromNode(
    const QJsonObject& nodeObject,
    const QString& fieldName,
    const double fallback
)
{
    const QJsonObject metadata = fieldMetadataFromNode(nodeObject, fieldName);
    if (metadata.value("default").isDouble()) {
        return metadata.value("default").toDouble(fallback);
    }
    return fallback;
}

QUrl buildEndpoint(const QString& baseUrl, const QString& suffix)
{
    QUrl url(baseUrl);
    QString path = url.path().trimmed();
    const QString normalizedSuffix = suffix.startsWith('/') ? suffix : QString("/%1").arg(suffix);

    if (path.isEmpty() || path == "/") {
        path = normalizedSuffix;
    } else {
        if (path.endsWith('/')) {
            path.chop(1);
        }

        if (!path.endsWith(normalizedSuffix)) {
            path += normalizedSuffix;
        }
    }

    url.setPath(path);
    return url;
}

} // namespace

ComfyUiCatalog ComfyUiMetadataService::fetchCatalog(const QString& baseUrl, const int timeoutMs)
{
    ComfyUiCatalog catalog;
    if (baseUrl.trimmed().isEmpty()) {
        catalog.errorMessage = "Keine ComfyUI-URL konfiguriert.";
        return catalog;
    }

    QNetworkAccessManager networkManager;
    QNetworkReply* reply = networkManager.get(QNetworkRequest(buildEndpoint(baseUrl, "/object_info")));
    if (reply == nullptr) {
        catalog.errorMessage = "ComfyUI-Reply fehlt.";
        return catalog;
    }

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(timeoutMs > 0 ? timeoutMs : 5000);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start();
    loop.exec();

    if (!timeoutTimer.isActive()) {
        reply->abort();
        catalog.errorMessage = "Zeitueberschreitung beim Laden der ComfyUI-Metadaten.";
        reply->deleteLater();
        return catalog;
    }
    timeoutTimer.stop();

    const QByteArray payload = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        catalog.errorMessage = reply->errorString();
        reply->deleteLater();
        return catalog;
    }
    reply->deleteLater();

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        catalog.errorMessage = "ComfyUI lieferte kein gueltiges object_info-JSON.";
        return catalog;
    }

    const QJsonObject rootObject = document.object();
    const QJsonObject checkpointNode = rootObject.value("CheckpointLoaderSimple").toObject();
    const QJsonObject samplerNode = rootObject.value("KSampler").toObject();
    const QJsonObject latentNode = rootObject.value("EmptyLatentImage").toObject();
    const QJsonObject saveImageNode = rootObject.value("SaveImage").toObject();
    const QJsonObject clipLayerNode = rootObject.value("CLIPSetLastLayer").toObject();
    const QJsonObject vaeLoaderNode = rootObject.value("VAELoader").toObject();
    const QJsonObject loraNode = rootObject.value("LoraLoader").toObject();
    const QJsonObject loadImageNode = rootObject.value("LoadImage").toObject();
    const QJsonObject loadImageMaskNode = rootObject.value("LoadImageMask").toObject();

    if (checkpointNode.isEmpty() || samplerNode.isEmpty() || latentNode.isEmpty() || saveImageNode.isEmpty()) {
        catalog.errorMessage = "ComfyUI object_info enthaelt nicht die benoetigten Standard-Nodes.";
        return catalog;
    }

    catalog.checkpoints = optionListFromNode(checkpointNode, "ckpt_name");
    catalog.loras = optionListFromNode(loraNode, "lora_name");
    catalog.vaes = optionListFromNode(vaeLoaderNode, "vae_name");
    catalog.inputImages = optionListFromNode(loadImageNode, "image");
    catalog.maskChannels = optionListFromNode(loadImageMaskNode, "channel");
    catalog.samplers = optionListFromNode(samplerNode, "sampler_name");
    catalog.schedulers = optionListFromNode(samplerNode, "scheduler");

    catalog.widthDefault = intDefaultFromNode(latentNode, "width", catalog.widthDefault);
    catalog.heightDefault = intDefaultFromNode(latentNode, "height", catalog.heightDefault);
    catalog.batchDefault = intDefaultFromNode(latentNode, "batch_size", catalog.batchDefault);
    catalog.stepsDefault = intDefaultFromNode(samplerNode, "steps", catalog.stepsDefault);
    catalog.cfgDefault = doubleDefaultFromNode(samplerNode, "cfg", catalog.cfgDefault);
    catalog.denoiseDefault = doubleDefaultFromNode(samplerNode, "denoise", catalog.denoiseDefault);
    catalog.filenamePrefixDefault = fieldMetadataFromNode(saveImageNode, "filename_prefix").value("default").toString(catalog.filenamePrefixDefault);

    const QJsonObject clipMetadata = fieldMetadataFromNode(clipLayerNode, "stop_at_clip_layer");
    if (!clipMetadata.isEmpty()) {
        catalog.clipLayerMin = clipMetadata.value("min").toInt(catalog.clipLayerMin);
        catalog.clipLayerMax = clipMetadata.value("max").toInt(catalog.clipLayerMax);
        catalog.clipLayerDefault = clipMetadata.value("default").toInt(catalog.clipLayerDefault);
    }

    if (!catalog.samplers.isEmpty()) {
        catalog.samplerDefault = catalog.samplers.first();
    }
    if (!catalog.schedulers.isEmpty()) {
        catalog.schedulerDefault = catalog.schedulers.first();
    }

    catalog.success = true;
    return catalog;
}

} // namespace privateclaw::services
