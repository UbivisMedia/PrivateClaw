#include "tools/ToolExecutor.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <limits>
#include <utility>

namespace privateclaw::tools {

namespace {

struct UnifiedDiffHunk
{
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    QStringList lines;
};

struct NetworkCallResult
{
    bool success = false;
    QByteArray payload;
    QString errorMessage;
    int statusCode = 0;
};

QString configString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

int configInt(const QJsonObject& object, const QString& key, const int fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toInt();
    }

    bool ok = false;
    const int parsed = value.toString().trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

bool configBool(const QJsonObject& object, const QString& key, const bool fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isBool()) {
        return value.toBool();
    }

    const QString text = value.toString().trimmed().toLower();
    if (text == "true" || text == "1" || text == "yes") {
        return true;
    }
    if (text == "false" || text == "0" || text == "no") {
        return false;
    }

    return fallback;
}

double configDouble(const QJsonObject& object, const QString& key, const double fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toDouble(fallback);
    }

    bool ok = false;
    const double parsed = value.toString().trimmed().toDouble(&ok);
    return ok ? parsed : fallback;
}

QStringList jsonArrayToStringList(const QJsonArray& array)
{
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue& value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

QStringList configStringList(const QJsonObject& object, const QString& key)
{
    QStringList values;
    const QJsonValue value = object.value(key);
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                values.append(text);
            }
        }
        return values;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return values;
    }

    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString normalized = part.trimmed();
        if (!normalized.isEmpty()) {
            values.append(normalized);
        }
    }

    return values;
}

QString normalizeLineEndings(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text;
}

QStringList splitLines(const QString& text, bool* trailingNewline = nullptr)
{
    QString normalized = normalizeLineEndings(text);
    const bool hasTrailingNewline = normalized.endsWith('\n');
    if (hasTrailingNewline) {
        normalized.chop(1);
    }

    if (trailingNewline != nullptr) {
        *trailingNewline = hasTrailingNewline;
    }

    if (normalized.isEmpty()) {
        return {};
    }

    return normalized.split('\n');
}

QString joinLines(const QStringList& lines, const bool trailingNewline)
{
    QString text = lines.join('\n');
    if (trailingNewline) {
        text += '\n';
    }
    return text;
}

QString serverErrorMessageFromPayload(const QByteArray& payload)
{
    const QJsonDocument json = QJsonDocument::fromJson(payload);
    if (!json.isObject()) {
        return {};
    }

    const QJsonObject root = json.object();
    const QJsonValue errorValue = root.value("error");
    if (errorValue.isString()) {
        return errorValue.toString().trimmed();
    }

    if (errorValue.isObject()) {
        return errorValue.toObject().value("message").toString().trimmed();
    }

    return root.value("message").toString().trimmed();
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

        if (path.endsWith(normalizedSuffix)) {
            url.setPath(path);
            return url;
        }

        path += normalizedSuffix;
    }

    url.setPath(path);
    return url;
}

NetworkCallResult waitForReply(QNetworkReply* reply, const int timeoutMs)
{
    NetworkCallResult result;
    if (reply == nullptr) {
        result.errorMessage = "Netzwerk-Reply fehlt.";
        return result;
    }

    QEventLoop loop;
    QTimer timeoutTimer;

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (timeoutMs > 0) {
        timeoutTimer.setInterval(timeoutMs);
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeoutTimer.start();
    }

    loop.exec();

    if (timeoutMs > 0 && !timeoutTimer.isActive()) {
        reply->abort();
        result.errorMessage = "Zeitueberschreitung bei der Netzwerkanfrage.";
        reply->deleteLater();
        return result;
    }

    if (timeoutMs > 0) {
        timeoutTimer.stop();
    }

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.payload = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const QString serverError = serverErrorMessageFromPayload(result.payload);
        result.errorMessage = serverError.isEmpty() ? reply->errorString() : serverError;
        reply->deleteLater();
        return result;
    }

    reply->deleteLater();
    result.success = true;
    return result;
}

bool parseUnifiedDiff(
    const QString& diffText,
    QString* targetPathFromPatch,
    QList<UnifiedDiffHunk>* hunks,
    QString* errorMessage
)
{
    if (hunks == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Patch-Speicher fehlt.";
        }
        return false;
    }

    hunks->clear();
    QString parsedTargetPath;
    const QString normalizedDiff = normalizeLineEndings(diffText);
    QStringList lines = normalizedDiff.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }

    static const QRegularExpression hunkPattern("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@");

    int index = 0;
    while (index < lines.size()) {
        const QString& line = lines.at(index);

        if (line.startsWith("+++ ")) {
            QString patchPath = line.mid(4).trimmed();
            if (patchPath.startsWith("b/")) {
                patchPath = patchPath.mid(2);
            }
            parsedTargetPath = patchPath;
            ++index;
            continue;
        }

        if (!line.startsWith("@@")) {
            ++index;
            continue;
        }

        const QRegularExpressionMatch match = hunkPattern.match(line);
        if (!match.hasMatch()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Ungueltiger Hunk-Header: %1").arg(line);
            }
            return false;
        }

        UnifiedDiffHunk hunk;
        hunk.oldStart = match.captured(1).toInt();
        hunk.oldCount = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
        hunk.newStart = match.captured(3).toInt();
        hunk.newCount = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();

        ++index;
        while (index < lines.size()) {
            const QString& hunkLine = lines.at(index);
            if (hunkLine.startsWith("@@")) {
                break;
            }

            if (hunkLine.startsWith("--- ") || hunkLine.startsWith("+++ ")) {
                break;
            }

            if (hunkLine.startsWith('\\')) {
                ++index;
                continue;
            }

            if (!hunkLine.isEmpty()) {
                const QChar prefix = hunkLine.front();
                if (prefix != ' ' && prefix != '+' && prefix != '-') {
                    if (errorMessage != nullptr) {
                        *errorMessage = QString("Ungueltige Diff-Zeile: %1").arg(hunkLine);
                    }
                    return false;
                }
            }

            hunk.lines.append(hunkLine);
            ++index;
        }

        hunks->append(hunk);
    }

    if (hunks->isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Diff enthaelt keine anwendbaren Hunks.";
        }
        return false;
    }

    if (targetPathFromPatch != nullptr) {
        *targetPathFromPatch = parsedTargetPath;
    }
    return true;
}

bool applyUnifiedDiffToText(
    const QString& originalText,
    const QList<UnifiedDiffHunk>& hunks,
    QString* patchedText,
    QString* errorMessage
)
{
    if (patchedText == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Interner Fehler: Zielpuffer fuer Diff fehlt.";
        }
        return false;
    }

    bool hadTrailingNewline = false;
    const QStringList originalLines = splitLines(originalText, &hadTrailingNewline);
    QStringList outputLines;
    int sourceIndex = 0;

    for (const UnifiedDiffHunk& hunk : hunks) {
        const int targetIndex = qMax(0, hunk.oldStart - 1);
        if (targetIndex < sourceIndex) {
            if (errorMessage != nullptr) {
                *errorMessage = "Diff-Hunks ueberlappen oder sind unsortiert.";
            }
            return false;
        }

        while (sourceIndex < targetIndex && sourceIndex < originalLines.size()) {
            outputLines.append(originalLines.at(sourceIndex));
            ++sourceIndex;
        }

        int consumedOldLines = 0;
        int producedNewLines = 0;

        for (const QString& line : hunk.lines) {
            if (line.isEmpty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Diff-Zeile ohne Praefix gefunden.";
                }
                return false;
            }

            const QChar prefix = line.front();
            const QString text = line.mid(1);

            if (prefix == ' ') {
                if (sourceIndex >= originalLines.size() || originalLines.at(sourceIndex) != text) {
                    if (errorMessage != nullptr) {
                        *errorMessage = QString(
                            "Kontextzeile passt nicht zum Dateiinhalt: %1"
                        ).arg(text);
                    }
                    return false;
                }
                outputLines.append(originalLines.at(sourceIndex));
                ++sourceIndex;
                ++consumedOldLines;
                ++producedNewLines;
                continue;
            }

            if (prefix == '-') {
                if (sourceIndex >= originalLines.size() || originalLines.at(sourceIndex) != text) {
                    if (errorMessage != nullptr) {
                        *errorMessage = QString(
                            "Zu entfernende Zeile passt nicht zum Dateiinhalt: %1"
                        ).arg(text);
                    }
                    return false;
                }
                ++sourceIndex;
                ++consumedOldLines;
                continue;
            }

            if (prefix == '+') {
                outputLines.append(text);
                ++producedNewLines;
                continue;
            }

            if (errorMessage != nullptr) {
                *errorMessage = QString("Unbekannter Diff-Praefix '%1'.").arg(prefix);
            }
            return false;
        }

        if (hunk.oldCount >= 0 && consumedOldLines != hunk.oldCount) {
            if (errorMessage != nullptr) {
                *errorMessage = QString(
                    "Diff-Hunk erwartet %1 alte Zeilen, verarbeitet wurden aber %2."
                ).arg(hunk.oldCount).arg(consumedOldLines);
            }
            return false;
        }

        if (hunk.newCount >= 0 && producedNewLines != hunk.newCount) {
            if (errorMessage != nullptr) {
                *errorMessage = QString(
                    "Diff-Hunk erwartet %1 neue Zeilen, erzeugt wurden aber %2."
                ).arg(hunk.newCount).arg(producedNewLines);
            }
            return false;
        }
    }

    while (sourceIndex < originalLines.size()) {
        outputLines.append(originalLines.at(sourceIndex));
        ++sourceIndex;
    }

    *patchedText = joinLines(outputLines, hadTrailingNewline);
    return true;
}

QJsonObject extractComfyHistoryEntry(const QJsonObject& rootObject, const QString& promptId)
{
    const QJsonValue promptValue = rootObject.value(promptId);
    if (promptValue.isObject()) {
        return promptValue.toObject();
    }

    if (rootObject.contains("outputs") || rootObject.contains("status")) {
        return rootObject;
    }

    return {};
}

QString sanitizedUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int generatedSeed()
{
    return static_cast<int>(QRandomGenerator::global()->bounded(std::numeric_limits<int>::max()));
}

QString prepareComfyInputImageReference(
    const QString& imageValue,
    const QString& workspaceRoot,
    const QString& comfyUiBaseUrl,
    QString* errorMessage,
    QStringList* logs
)
{
    const QString trimmedValue = imageValue.trimmed();
    if (trimmedValue.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "ComfyUI-Bildpfad darf nicht leer sein.";
        }
        return {};
    }

    QFileInfo fileInfo(trimmedValue);
    QString absolutePath;
    if (fileInfo.isAbsolute() && fileInfo.exists()) {
        absolutePath = fileInfo.absoluteFilePath();
    } else {
        const QString workspaceCandidate = QDir(workspaceRoot).absoluteFilePath(trimmedValue);
        if (QFileInfo::exists(workspaceCandidate)) {
            absolutePath = workspaceCandidate;
        }
    }

    if (absolutePath.isEmpty()) {
        return trimmedValue;
    }

    auto* file = new QFile(absolutePath);
    if (!file->open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Bild konnte nicht gelesen werden: %1").arg(absolutePath);
        }
        file->deleteLater();
        return {};
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart imagePart;
    imagePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QVariant(QString("form-data; name=\"image\"; filename=\"%1\"").arg(QFileInfo(absolutePath).fileName()))
    );
    imagePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(imagePart);

    QHttpPart overwritePart;
    overwritePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"overwrite\""));
    overwritePart.setBody("true");
    multiPart->append(overwritePart);

    QHttpPart typePart;
    typePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"type\""));
    typePart.setBody("input");
    multiPart->append(typePart);

    QNetworkAccessManager networkManager;
    QNetworkRequest request(buildEndpoint(comfyUiBaseUrl, "/upload/image"));
    QNetworkReply* reply = networkManager.post(request, multiPart);
    multiPart->setParent(reply);

    const NetworkCallResult uploadResult = waitForReply(reply, 15000);
    if (!uploadResult.success) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("ComfyUI-Bildupload fehlgeschlagen: %1").arg(uploadResult.errorMessage);
        }
        return {};
    }

    const QJsonDocument uploadDocument = QJsonDocument::fromJson(uploadResult.payload);
    const QJsonObject uploadObject = uploadDocument.object();
    const QString uploadedName = uploadObject.value("name").toString().trimmed();
    const QString uploadedSubfolder = uploadObject.value("subfolder").toString().trimmed();
    if (uploadedName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "ComfyUI-Bildupload lieferte keinen Dateinamen zurueck.";
        }
        return {};
    }

    if (logs != nullptr) {
        logs->append(QString("ComfyUI-Bild hochgeladen: %1").arg(QFileInfo(absolutePath).fileName()));
    }
    return uploadedSubfolder.isEmpty() ? uploadedName : uploadedSubfolder + "/" + uploadedName;
}

QJsonObject buildComfyGeneratedImageWorkflow(
    const QJsonObject& config,
    const QString& workspaceRoot,
    const QString& comfyUiBaseUrl,
    QString* errorMessage,
    QStringList* logs
)
{
    const QString builderMode = configString(config, "builder_mode").toLower().isEmpty()
        ? "txt2img"
        : configString(config, "builder_mode").toLower();
    const QString checkpoint = configString(config, "checkpoint");
    if (checkpoint.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "ComfyUI txt2img braucht einen Checkpoint.";
        }
        return {};
    }

    const QString positivePrompt = configString(config, "positive_prompt");
    const QString negativePrompt = configString(config, "negative_prompt");
    const int width = qBound(16, configInt(config, "width", 1024), 16384);
    const int height = qBound(16, configInt(config, "height", 1024), 16384);
    const int batchSize = qBound(1, configInt(config, "batch_size", 1), 256);
    const int steps = qBound(1, configInt(config, "steps", 20), 10000);
    const double cfg = qBound(0.0, configDouble(config, "cfg", 8.0), 100.0);
    const double denoise = qBound(0.0, configDouble(config, "denoise", 1.0), 1.0);
    const QString samplerName = configString(config, "sampler_name").isEmpty()
        ? "euler"
        : configString(config, "sampler_name");
    const QString scheduler = configString(config, "scheduler").isEmpty()
        ? "normal"
        : configString(config, "scheduler");
    const QString filenamePrefix = configString(config, "filename_prefix").isEmpty()
        ? "PrivateClaw"
        : configString(config, "filename_prefix");
    const int clipSkip = configInt(config, "clip_skip", -1);
    int seed = qMax(0, configInt(config, "seed", 0));
    if (configBool(config, "randomize_seed", false)) {
        seed = generatedSeed();
        if (logs != nullptr) {
            logs->append(QString("ComfyUI txt2img: Seed zufaellig erzeugt (%1).").arg(seed));
        }
    }

    QJsonObject workflow;
    int nextNodeId = 3;
    const auto makeNodeId = [&nextNodeId]() {
        return QString::number(nextNodeId++);
    };

    const QString checkpointNodeId = makeNodeId();
    workflow.insert(
        checkpointNodeId,
        QJsonObject{
            { "class_type", "CheckpointLoaderSimple" },
            { "inputs", QJsonObject{ { "ckpt_name", checkpoint } } }
        }
    );

    QString currentModelNodeId = checkpointNodeId;
    int currentModelOutputIndex = 0;
    QString currentClipNodeId = checkpointNodeId;
    int currentClipOutputIndex = 1;
    QString currentVaeNodeId = checkpointNodeId;
    int currentVaeOutputIndex = 2;

    const QJsonArray loraArray = config.value("loras").toArray();
    int appliedLoras = 0;
    for (const QJsonValue& loraValue : loraArray) {
        if (!loraValue.isObject()) {
            continue;
        }

        const QJsonObject loraObject = loraValue.toObject();
        if (!configBool(loraObject, "enabled", true)) {
            continue;
        }

        const QString loraName = configString(loraObject, "name");
        if (loraName.isEmpty()) {
            continue;
        }

        const QString loraNodeId = makeNodeId();
        workflow.insert(
            loraNodeId,
            QJsonObject{
                { "class_type", "LoraLoader" },
                { "inputs",
                  QJsonObject{
                      { "model", QJsonArray{ currentModelNodeId, currentModelOutputIndex } },
                      { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } },
                      { "lora_name", loraName },
                      { "strength_model", configDouble(loraObject, "strength_model", 1.0) },
                      { "strength_clip", configDouble(loraObject, "strength_clip", 1.0) }
                  } }
            }
        );
        currentModelNodeId = loraNodeId;
        currentModelOutputIndex = 0;
        currentClipNodeId = loraNodeId;
        currentClipOutputIndex = 1;
        ++appliedLoras;
    }

    if (logs != nullptr) {
        logs->append(QString("ComfyUI txt2img: %1 LoRA(s) aktiv.").arg(appliedLoras));
    }

    if (clipSkip != -1) {
        const QString clipSkipNodeId = makeNodeId();
        workflow.insert(
            clipSkipNodeId,
            QJsonObject{
                { "class_type", "CLIPSetLastLayer" },
                { "inputs",
                  QJsonObject{
                      { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } },
                      { "stop_at_clip_layer", clipSkip }
                  } }
            }
        );
        currentClipNodeId = clipSkipNodeId;
        currentClipOutputIndex = 0;
    }

    const QString positiveNodeId = makeNodeId();
    workflow.insert(
        positiveNodeId,
        QJsonObject{
            { "class_type", "CLIPTextEncode" },
            { "inputs",
              QJsonObject{
                  { "text", positivePrompt },
                  { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } }
              } }
        }
    );

    const QString negativeNodeId = makeNodeId();
    workflow.insert(
        negativeNodeId,
        QJsonObject{
            { "class_type", "CLIPTextEncode" },
            { "inputs",
              QJsonObject{
                  { "text", negativePrompt },
                  { "clip", QJsonArray{ currentClipNodeId, currentClipOutputIndex } }
              } }
        }
    );

    const QString vaeOverride = configString(config, "vae_name");
    if (!vaeOverride.isEmpty()) {
        const QString vaeLoaderNodeId = makeNodeId();
        workflow.insert(
            vaeLoaderNodeId,
            QJsonObject{
                { "class_type", "VAELoader" },
                { "inputs", QJsonObject{ { "vae_name", vaeOverride } } }
            }
        );
        currentVaeNodeId = vaeLoaderNodeId;
        currentVaeOutputIndex = 0;
    }

    QString latentSourceNodeId;
    int latentSourceOutputIndex = 0;
    if (builderMode == "img2img" || builderMode == "inpainting") {
        QString prepareError;
        const QString preparedInputImage = prepareComfyInputImageReference(
            configString(config, "input_image"),
            workspaceRoot,
            comfyUiBaseUrl,
            &prepareError,
            logs
        );
        if (preparedInputImage.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = prepareError.isEmpty()
                    ? "ComfyUI braucht ein gueltiges Startbild."
                    : prepareError;
            }
            return {};
        }

        const QString loadImageNodeId = makeNodeId();
        workflow.insert(
            loadImageNodeId,
            QJsonObject{
                { "class_type", "LoadImage" },
                { "inputs", QJsonObject{ { "image", preparedInputImage } } }
            }
        );

        if (builderMode == "inpainting") {
            QString maskError;
            const QString preparedMaskImage = prepareComfyInputImageReference(
                configString(config, "mask_image"),
                workspaceRoot,
                comfyUiBaseUrl,
                &maskError,
                logs
            );
            if (preparedMaskImage.isEmpty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = maskError.isEmpty()
                        ? "ComfyUI-Inpainting braucht ein Maskenbild."
                        : maskError;
                }
                return {};
            }

            const QString maskNodeId = makeNodeId();
            workflow.insert(
                maskNodeId,
                QJsonObject{
                    { "class_type", "LoadImageMask" },
                    { "inputs",
                      QJsonObject{
                          { "image", preparedMaskImage },
                          { "channel", configString(config, "mask_channel").isEmpty()
                                ? "alpha"
                                : configString(config, "mask_channel") }
                      } }
                }
            );

            const QString encodeNodeId = makeNodeId();
            workflow.insert(
                encodeNodeId,
                QJsonObject{
                    { "class_type", "VAEEncodeForInpaint" },
                    { "inputs",
                      QJsonObject{
                          { "pixels", QJsonArray{ loadImageNodeId, 0 } },
                          { "vae", QJsonArray{ currentVaeNodeId, currentVaeOutputIndex } },
                          { "mask", QJsonArray{ maskNodeId, 0 } },
                          { "grow_mask_by", qMax(0, configInt(config, "mask_grow_by", 6)) }
                      } }
                }
            );
            latentSourceNodeId = encodeNodeId;
        } else {
            const QString encodeNodeId = makeNodeId();
            workflow.insert(
                encodeNodeId,
                QJsonObject{
                    { "class_type", "VAEEncode" },
                    { "inputs",
                      QJsonObject{
                          { "pixels", QJsonArray{ loadImageNodeId, 0 } },
                          { "vae", QJsonArray{ currentVaeNodeId, currentVaeOutputIndex } }
                      } }
                }
            );
            latentSourceNodeId = encodeNodeId;
        }
    } else {
        const QString latentNodeId = makeNodeId();
        workflow.insert(
            latentNodeId,
            QJsonObject{
                { "class_type", "EmptyLatentImage" },
                { "inputs",
                  QJsonObject{
                      { "width", width },
                      { "height", height },
                      { "batch_size", batchSize }
                  } }
            }
        );
        latentSourceNodeId = latentNodeId;
    }

    const QString samplerNodeId = makeNodeId();
    workflow.insert(
        samplerNodeId,
        QJsonObject{
            { "class_type", "KSampler" },
            { "inputs",
              QJsonObject{
                  { "model", QJsonArray{ currentModelNodeId, currentModelOutputIndex } },
                  { "seed", seed },
                  { "steps", steps },
                  { "cfg", cfg },
                  { "sampler_name", samplerName },
                  { "scheduler", scheduler },
                  { "positive", QJsonArray{ positiveNodeId, 0 } },
                  { "negative", QJsonArray{ negativeNodeId, 0 } },
                  { "latent_image", QJsonArray{ latentSourceNodeId, latentSourceOutputIndex } },
                  { "denoise", denoise }
              } }
        }
    );

    const QString decodeNodeId = makeNodeId();
    workflow.insert(
        decodeNodeId,
        QJsonObject{
            { "class_type", "VAEDecode" },
            { "inputs",
              QJsonObject{
                  { "samples", QJsonArray{ samplerNodeId, 0 } },
                  { "vae", QJsonArray{ currentVaeNodeId, currentVaeOutputIndex } }
              } }
        }
    );

    const QString saveNodeId = makeNodeId();
    workflow.insert(
        saveNodeId,
        QJsonObject{
            { "class_type", "SaveImage" },
            { "inputs",
              QJsonObject{
                  { "images", QJsonArray{ decodeNodeId, 0 } },
                  { "filename_prefix", filenamePrefix }
              } }
        }
    );

    if (logs != nullptr) {
        logs->append(QString("ComfyUI %1: Workflow aus Formular-Daten erzeugt (%2x%3, %4 Schritte).")
                         .arg(builderMode)
                         .arg(width)
                         .arg(height)
                         .arg(steps));
    }
    return workflow;
}

bool looksBinary(const QByteArray& payload)
{
    const int inspectionLength = qMin(payload.size(), 4096);
    for (int index = 0; index < inspectionLength; ++index) {
        if (payload.at(index) == '\0') {
            return true;
        }
    }

    return false;
}

QString normalizedExtensionFilter(QString extension)
{
    extension = extension.trimmed().toLower();
    if (extension.startsWith("*.")) {
        extension = extension.mid(1);
    } else if (!extension.startsWith('.')) {
        extension.prepend('.');
    }

    return extension;
}

bool matchesDirectoryReadFilters(
    const QString& relativePath,
    const QStringList& includeExtensions,
    const QStringList& excludeFragments
)
{
    const QString normalizedPath = relativePath.toLower();

    for (const QString& fragment : excludeFragments) {
        if (normalizedPath.contains(fragment.toLower())) {
            return false;
        }
    }

    if (includeExtensions.isEmpty()) {
        return true;
    }

    for (const QString& extension : includeExtensions) {
        if (normalizedPath.endsWith(extension)) {
            return true;
        }
    }

    return false;
}

} // namespace

ToolExecutor::ToolExecutor(QString workspaceRoot, QString comfyUiBaseUrl)
    : m_workspaceRoot(std::move(workspaceRoot))
    , m_comfyUiBaseUrl(std::move(comfyUiBaseUrl))
{
    if (m_workspaceRoot.trimmed().isEmpty()) {
        m_workspaceRoot = QDir::currentPath();
    }

    m_workspaceRoot = QDir(m_workspaceRoot).absolutePath();
}

QStringList ToolExecutor::availableTools() const
{
    return {
        "file.read",
        "directory.read_recursive",
        "directory.read_changed",
        "memory.ingest_directory",
        "file.edit_diff",
        "comfyui.workflow"
    };
}

ToolExecutionResult ToolExecutor::execute(const ToolExecutionRequest& request) const
{
    const QString toolName = request.toolName.trimmed().toLower();
    if (toolName == "file.read") {
        return executeFileRead(request.config);
    }

    if (toolName == "directory.read_recursive") {
        return executeDirectoryReadRecursive(request.config);
    }

    if (toolName == "directory.read_changed") {
        return executeDirectoryReadChanged(request.config);
    }

    if (toolName == "memory.ingest_directory") {
        return executeMemoryIngestDirectory(request);
    }

    if (toolName == "file.edit_diff") {
        return executeFileEditDiff(request.config);
    }

    if (toolName == "comfyui.workflow") {
        return executeComfyUiWorkflow(request.config);
    }

    ToolExecutionResult result;
    result.errorMessage = QString("Unbekanntes Tool '%1'.").arg(request.toolName);
    return result;
}

QString ToolExecutor::workspaceRoot() const
{
    return m_workspaceRoot;
}

QString ToolExecutor::comfyUiBaseUrl() const
{
    return m_comfyUiBaseUrl;
}

ToolExecutionResult ToolExecutor::executeFileRead(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absolutePath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absolutePath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QString("Datei konnte nicht gelesen werden: %1").arg(file.errorString());
        return result;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    bool hadTrailingNewline = false;
    const QStringList lines = splitLines(content, &hadTrailingNewline);
    const int lineStart = qMax(1, configInt(config, "line_start", 1));
    const int lineEnd = qMax(lineStart, configInt(config, "line_end", lines.isEmpty() ? lineStart : lines.size()));

    QString selectedContent;
    if (lines.isEmpty()) {
        selectedContent.clear();
    } else {
        QStringList selectedLines;
        for (int index = lineStart - 1; index < lines.size() && index < lineEnd; ++index) {
            selectedLines.append(lines.at(index));
        }
        selectedContent = joinLines(selectedLines, hadTrailingNewline && lineEnd >= lines.size());
    }

    const int maxChars = qMax(0, configInt(config, "max_chars", 20000));
    bool truncated = false;
    if (maxChars > 0 && selectedContent.size() > maxChars) {
        selectedContent = selectedContent.left(maxChars);
        truncated = true;
    }

    result.success = true;
    result.outputText = selectedContent;
    result.logs.append(QString("Datei gelesen: %1").arg(absolutePath));
    result.logs.append(QString("Zeilenbereich: %1-%2").arg(lineStart).arg(lineEnd));
    if (truncated) {
        result.logs.append(QString("Inhalt auf %1 Zeichen gekuerzt.").arg(maxChars));
    }
    return result;
}

ToolExecutionResult ToolExecutor::executeDirectoryReadRecursive(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absoluteDirectoryPath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absoluteDirectoryPath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QFileInfo directoryInfo(absoluteDirectoryPath);
    if (!directoryInfo.isDir()) {
        result.errorMessage = QString("Pfad ist kein Verzeichnis: %1").arg(absoluteDirectoryPath);
        return result;
    }

    QStringList includeExtensions = configStringList(config, "include_extensions");
    for (QString& extension : includeExtensions) {
        extension = normalizedExtensionFilter(extension);
    }

    QStringList excludeFragments = configStringList(config, "exclude_paths");
    if (excludeFragments.isEmpty()) {
        excludeFragments = {
            ".git",
            "/build",
            "\\build",
            "node_modules",
            "__pycache__"
        };
    }

    const bool includeHidden = configBool(config, "include_hidden", false);
    const bool skipBinary = configBool(config, "skip_binary", true);
    const int maxFiles = qMax(1, configInt(config, "max_files", 40));
    const int maxCharsPerFile = qMax(0, configInt(config, "max_chars_per_file", 8000));
    const int maxTotalChars = qMax(0, configInt(config, "max_total_chars", 120000));

    QDirIterator iterator(
        absoluteDirectoryPath,
        QDir::Files | QDir::NoDotAndDotDot | (includeHidden ? QDir::Hidden : QDir::NoFilter),
        QDirIterator::Subdirectories
    );

    QStringList blocks;
    int fileCount = 0;
    int skippedBinaryFiles = 0;
    int skippedByFilter = 0;
    int totalChars = 0;
    bool totalLimitReached = false;

    while (iterator.hasNext()) {
        const QString absoluteFilePath = iterator.next();
        const QFileInfo fileInfo(absoluteFilePath);
        const QString relativePath = QDir(absoluteDirectoryPath).relativeFilePath(absoluteFilePath);

        if (!matchesDirectoryReadFilters(relativePath, includeExtensions, excludeFragments)) {
            ++skippedByFilter;
            continue;
        }

        QFile file(absoluteFilePath);
        if (!file.open(QIODevice::ReadOnly)) {
            result.logs.append(QString("Datei uebersprungen (nicht lesbar): %1").arg(relativePath));
            continue;
        }

        QByteArray payload = file.readAll();
        file.close();

        if (skipBinary && looksBinary(payload)) {
            ++skippedBinaryFiles;
            continue;
        }

        QString content = QString::fromUtf8(payload);
        bool truncatedPerFile = false;
        if (maxCharsPerFile > 0 && content.size() > maxCharsPerFile) {
            content = content.left(maxCharsPerFile);
            truncatedPerFile = true;
        }

        QString block = QString("### FILE: %1\n%2").arg(relativePath, content);
        if (truncatedPerFile) {
            block += "\n[... Dateiinhalt gekuerzt ...]";
        }

        const int projectedTotal = totalChars + block.size() + 2;
        if (maxTotalChars > 0 && projectedTotal > maxTotalChars) {
            totalLimitReached = true;
            break;
        }

        blocks.append(block);
        totalChars = projectedTotal;
        ++fileCount;

        if (fileCount >= maxFiles) {
            break;
        }
    }

    if (blocks.isEmpty()) {
        result.errorMessage = "Es konnten keine passenden Textdateien aus dem Verzeichnis gelesen werden.";
        return result;
    }

    result.success = true;
    result.outputText = blocks.join("\n\n");
    result.logs.append(QString("Verzeichnis rekursiv gelesen: %1").arg(absoluteDirectoryPath));
    result.logs.append(QString("Dateien im Ergebnis: %1").arg(fileCount));
    result.logs.append(QString("Durch Filter uebersprungen: %1").arg(skippedByFilter));
    if (skipBinary) {
        result.logs.append(QString("Binaerdateien uebersprungen: %1").arg(skippedBinaryFiles));
    }
    if (totalLimitReached) {
        result.logs.append("Abbruch wegen max_total_chars.");
    } else if (fileCount >= maxFiles) {
        result.logs.append("Abbruch wegen max_files.");
    }

    return result;
}

ToolExecutionResult ToolExecutor::executeDirectoryReadChanged(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString errorMessage;
    const QString absoluteDirectoryPath = resolveWorkspacePath(configString(config, "path"), false, &errorMessage);
    if (absoluteDirectoryPath.isEmpty()) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QFileInfo directoryInfo(absoluteDirectoryPath);
    if (!directoryInfo.isDir()) {
        result.errorMessage = QString("Pfad ist kein Verzeichnis: %1").arg(absoluteDirectoryPath);
        return result;
    }

    const int withinMinutes = qMax(0, configInt(config, "within_minutes", 0));
    QDateTime modifiedAfter;
    if (withinMinutes > 0) {
        modifiedAfter = QDateTime::currentDateTimeUtc().addSecs(-withinMinutes * 60);
    } else {
        const QString modifiedAfterIso = configString(config, "modified_after_iso");
        if (!modifiedAfterIso.isEmpty()) {
            modifiedAfter = QDateTime::fromString(modifiedAfterIso, Qt::ISODate);
            if (modifiedAfter.isValid()) {
                modifiedAfter = modifiedAfter.toUTC();
            }
        }
    }

    if (!modifiedAfter.isValid()) {
        result.errorMessage = "Tool 'directory.read_changed' braucht 'within_minutes' oder 'modified_after_iso'.";
        return result;
    }

    QStringList includeExtensions = configStringList(config, "include_extensions");
    for (QString& extension : includeExtensions) {
        extension = normalizedExtensionFilter(extension);
    }

    QStringList excludeFragments = configStringList(config, "exclude_paths");
    if (excludeFragments.isEmpty()) {
        excludeFragments = {
            ".git",
            "/build",
            "\\build",
            "node_modules",
            "__pycache__"
        };
    }

    const bool includeHidden = configBool(config, "include_hidden", false);
    const bool skipBinary = configBool(config, "skip_binary", true);
    const int maxFiles = qMax(1, configInt(config, "max_files", 20));
    const int maxCharsPerFile = qMax(0, configInt(config, "max_chars_per_file", 6000));
    const int maxTotalChars = qMax(0, configInt(config, "max_total_chars", 80000));

    QDirIterator iterator(
        absoluteDirectoryPath,
        QDir::Files | QDir::NoDotAndDotDot | (includeHidden ? QDir::Hidden : QDir::NoFilter),
        QDirIterator::Subdirectories
    );

    QStringList blocks;
    int fileCount = 0;
    int skippedBinaryFiles = 0;
    int skippedByFilter = 0;
    int skippedByAge = 0;
    int totalChars = 0;
    bool totalLimitReached = false;

    while (iterator.hasNext()) {
        const QString absoluteFilePath = iterator.next();
        const QFileInfo fileInfo(absoluteFilePath);
        const QString relativePath = QDir(absoluteDirectoryPath).relativeFilePath(absoluteFilePath);

        if (!matchesDirectoryReadFilters(relativePath, includeExtensions, excludeFragments)) {
            ++skippedByFilter;
            continue;
        }

        if (fileInfo.lastModified().toUTC() < modifiedAfter) {
            ++skippedByAge;
            continue;
        }

        QFile file(absoluteFilePath);
        if (!file.open(QIODevice::ReadOnly)) {
            result.logs.append(QString("Datei uebersprungen (nicht lesbar): %1").arg(relativePath));
            continue;
        }

        QByteArray payload = file.readAll();
        file.close();

        if (skipBinary && looksBinary(payload)) {
            ++skippedBinaryFiles;
            continue;
        }

        QString content = QString::fromUtf8(payload);
        bool truncatedPerFile = false;
        if (maxCharsPerFile > 0 && content.size() > maxCharsPerFile) {
            content = content.left(maxCharsPerFile);
            truncatedPerFile = true;
        }

        QString block = QString("### FILE: %1\n%2").arg(relativePath, content);
        if (truncatedPerFile) {
            block += "\n[... Dateiinhalt gekuerzt ...]";
        }

        const int projectedTotal = totalChars + block.size() + 2;
        if (maxTotalChars > 0 && projectedTotal > maxTotalChars) {
            totalLimitReached = true;
            break;
        }

        blocks.append(block);
        totalChars = projectedTotal;
        ++fileCount;

        if (fileCount >= maxFiles) {
            break;
        }
    }

    if (blocks.isEmpty()) {
        result.errorMessage = "Es wurden keine geaenderten passenden Textdateien gefunden.";
        return result;
    }

    result.success = true;
    result.outputText = blocks.join("\n\n");
    result.logs.append(QString("Geaenderte Dateien rekursiv gelesen: %1").arg(absoluteDirectoryPath));
    result.logs.append(QString("Aenderungsgrenze: %1").arg(modifiedAfter.toString(Qt::ISODate)));
    result.logs.append(QString("Dateien im Ergebnis: %1").arg(fileCount));
    result.logs.append(QString("Durch Filter uebersprungen: %1").arg(skippedByFilter));
    result.logs.append(QString("Wegen Alter uebersprungen: %1").arg(skippedByAge));
    if (skipBinary) {
        result.logs.append(QString("Binaerdateien uebersprungen: %1").arg(skippedBinaryFiles));
    }
    if (totalLimitReached) {
        result.logs.append("Abbruch wegen max_total_chars.");
    } else if (fileCount >= maxFiles) {
        result.logs.append("Abbruch wegen max_files.");
    }

    return result;
}

ToolExecutionResult ToolExecutor::executeMemoryIngestDirectory(const ToolExecutionRequest& request) const
{
    ToolExecutionResult result;
    if (request.projectId <= 0) {
        result.errorMessage = "Tool 'memory.ingest_directory' braucht ein Projekt im Run-Kontext.";
        return result;
    }

    const QJsonObject& config = request.config;
    const QString mode = configString(config, "mode").toLower();
    const bool useChangedMode = mode == "changed"
        || configInt(config, "within_minutes", 0) > 0
        || !configString(config, "modified_after_iso").isEmpty();

    const ToolExecutionResult directoryResult = useChangedMode
        ? executeDirectoryReadChanged(config)
        : executeDirectoryReadRecursive(config);
    for (const QString& logLine : directoryResult.logs) {
        result.logs.append(logLine);
    }

    if (!directoryResult.success) {
        result.errorMessage = directoryResult.errorMessage;
        return result;
    }

    const QString configuredPath = configString(config, "path");
    const QString absoluteDirectoryPath = resolveWorkspacePath(configuredPath, false, nullptr);
    const QString relativeDirectoryPath = absoluteDirectoryPath.isEmpty()
        ? configuredPath
        : QDir(m_workspaceRoot).relativeFilePath(absoluteDirectoryPath);

    domain::MemoryEntry entry;
    entry.projectId = request.projectId;
    entry.type = configString(config, "entry_type");
    if (entry.type.isEmpty()) {
        entry.type = "artifact";
    }

    entry.source = configString(config, "source");
    if (entry.source.isEmpty()) {
        const QString workflowName = request.workflowName.trimmed().isEmpty()
            ? "workflow"
            : request.workflowName.trimmed();
        const QString stepId = request.stepId.trimmed().isEmpty()
            ? "memory_ingest_directory"
            : request.stepId.trimmed();
        entry.source = QString("workflow:%1/%2").arg(workflowName, stepId);
    }

    entry.tags = configStringList(config, "tags");
    entry.relevance = qBound(0, configInt(config, "relevance", 80), 100);

    QStringList headerLines;
    headerLines.append(QString("Memory-Ingest aus Verzeichnis: %1").arg(relativeDirectoryPath));
    headerLines.append(QString("Modus: %1").arg(useChangedMode ? "changed" : "recursive"));
    headerLines.append(QString("Erstellt: %1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    entry.content = headerLines.join("\n") + "\n\n" + directoryResult.outputText;

    result.memoryEntriesToPersist.append(entry);
    result.success = true;

    QJsonObject summary{
        { "tool", "memory.ingest_directory" },
        { "path", relativeDirectoryPath },
        { "mode", useChangedMode ? "changed" : "recursive" },
        { "memory_entries", 1 },
        { "char_count", entry.content.size() },
        { "source", entry.source }
    };
    result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    result.logs.append(QString("Memory-Ingest vorbereitet fuer Projekt %1.").arg(request.projectId));
    result.logs.append(QString("Memory-Quelle: %1").arg(entry.source));
    result.logs.append(QString("Memory-Typ: %1").arg(entry.type));
    result.logs.append(QString("Memory-Zeichen: %1").arg(entry.content.size()));
    return result;
}

ToolExecutionResult ToolExecutor::executeFileEditDiff(const QJsonObject& config) const
{
    ToolExecutionResult result;

    QString patchPath;
    QList<UnifiedDiffHunk> hunks;
    QString parseError;
    const QString diffText = configString(config, "diff").isEmpty()
        ? configString(config, "patch")
        : configString(config, "diff");
    if (diffText.isEmpty()) {
        result.errorMessage = "Tool 'file.edit_diff' braucht ein Feld 'diff' oder 'patch'.";
        return result;
    }

    if (!parseUnifiedDiff(diffText, &patchPath, &hunks, &parseError)) {
        result.errorMessage = parseError;
        return result;
    }

    QString targetPath = configString(config, "path");
    if (targetPath.isEmpty()) {
        targetPath = patchPath;
    }

    QString resolveError;
    const QString absolutePath = resolveWorkspacePath(targetPath, false, &resolveError);
    if (absolutePath.isEmpty()) {
        result.errorMessage = resolveError;
        return result;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QString("Datei konnte nicht geoeffnet werden: %1").arg(file.errorString());
        return result;
    }

    const QString originalContent = QString::fromUtf8(file.readAll());
    file.close();

    QString patchedContent;
    QString applyError;
    if (!applyUnifiedDiffToText(originalContent, hunks, &patchedContent, &applyError)) {
        result.errorMessage = applyError;
        return result;
    }

    QSaveFile saveFile(absolutePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.errorMessage = QString("Datei konnte nicht zum Schreiben geoeffnet werden: %1").arg(saveFile.errorString());
        return result;
    }

    saveFile.write(patchedContent.toUtf8());
    if (!saveFile.commit()) {
        result.errorMessage = QString("Dateiaenderung konnte nicht gespeichert werden: %1").arg(saveFile.errorString());
        return result;
    }

    const bool returnContent = configBool(config, "return_content", false);
    if (returnContent) {
        result.outputText = patchedContent;
    } else {
        QJsonObject summary{
            { "tool", "file.edit_diff" },
            { "path", QDir(m_workspaceRoot).relativeFilePath(absolutePath) },
            { "status", "updated" }
        };
        result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    }

    result.success = true;
    result.logs.append(QString("Datei per Diff aktualisiert: %1").arg(absolutePath));
    return result;
}

ToolExecutionResult ToolExecutor::executeComfyUiWorkflow(const QJsonObject& config) const
{
    ToolExecutionResult result;
    if (m_comfyUiBaseUrl.trimmed().isEmpty()) {
        result.errorMessage = "Keine ComfyUI-URL konfiguriert.";
        return result;
    }

    QJsonObject workflowObject;
    if (config.value("workflow").isObject()) {
        workflowObject = config.value("workflow").toObject();
    } else if ((configString(config, "builder_mode").toLower() == "txt2img"
            || configString(config, "builder_mode").toLower() == "img2img"
            || configString(config, "builder_mode").toLower() == "inpainting")
        || (!configString(config, "checkpoint").isEmpty() && configString(config, "workflow_json").isEmpty())) {
        QString buildError;
        workflowObject = buildComfyGeneratedImageWorkflow(
            config,
            m_workspaceRoot,
            m_comfyUiBaseUrl,
            &buildError,
            &result.logs
        );
        if (workflowObject.isEmpty()) {
            result.errorMessage = buildError.isEmpty()
                ? "ComfyUI-Workflow aus Formular-Daten konnte nicht erzeugt werden."
                : buildError;
            return result;
        }
    } else {
        const QString workflowJson = configString(config, "workflow_json");
        if (workflowJson.isEmpty()) {
            result.errorMessage = "Tool 'comfyui.workflow' braucht 'workflow', 'workflow_json' oder eine txt2img-Konfiguration.";
            return result;
        }

        QJsonParseError parseError;
        const QJsonDocument workflowDocument = QJsonDocument::fromJson(workflowJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !workflowDocument.isObject()) {
            result.errorMessage = QString("ComfyUI-Workflow ist kein gueltiges JSON-Objekt: %1").arg(parseError.errorString());
            return result;
        }
        workflowObject = workflowDocument.object();
    }

    const QString clientId = configString(config, "client_id").isEmpty()
        ? sanitizedUuid()
        : configString(config, "client_id");
    const QString promptId = configString(config, "prompt_id").isEmpty()
        ? sanitizedUuid()
        : configString(config, "prompt_id");

    QJsonObject payload{
        { "prompt", workflowObject },
        { "client_id", clientId },
        { "prompt_id", promptId }
    };

    if (config.value("extra_data").isObject()) {
        payload.insert("extra_data", config.value("extra_data").toObject());
    } else if (!configString(config, "extra_data_json").isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument extraDataDocument = QJsonDocument::fromJson(configString(config, "extra_data_json").toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !extraDataDocument.isObject()) {
            result.errorMessage = QString("ComfyUI extra_data_json ist ungueltig: %1").arg(parseError.errorString());
            return result;
        }
        payload.insert("extra_data", extraDataDocument.object());
    }

    QNetworkAccessManager networkManager;
    QNetworkRequest submitRequest(buildEndpoint(m_comfyUiBaseUrl, "/prompt"));
    submitRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const int submitTimeoutMs = qMax(1000, configInt(config, "submit_timeout_ms", 15000));
    const NetworkCallResult submitResult = waitForReply(
        networkManager.post(submitRequest, QJsonDocument(payload).toJson(QJsonDocument::Compact)),
        submitTimeoutMs
    );
    if (!submitResult.success) {
        result.errorMessage = QString("ComfyUI-Queue fehlgeschlagen: %1").arg(submitResult.errorMessage);
        return result;
    }

    result.logs.append(QString("ComfyUI-Prompt eingereiht: %1").arg(promptId));

    const bool waitForCompletion = configBool(config, "wait_for_completion", true);
    if (!waitForCompletion) {
        QJsonObject summary{
            { "tool", "comfyui.workflow" },
            { "prompt_id", promptId },
            { "client_id", clientId },
            { "queued", true }
        };
        result.success = true;
        result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
        return result;
    }

    const int pollIntervalMs = qBound(200, configInt(config, "poll_interval_ms", 1500), 60000);
    const int timeoutMs = qMax(0, configInt(config, "timeout_ms", 0));
    const bool includeHistoryJson = configBool(config, "include_history_json", false);
    const bool downloadImages = configBool(config, "download_images", !configString(config, "save_outputs_to").isEmpty());

    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    QJsonObject historyEntry;
    while (historyEntry.isEmpty()) {
        if (timeoutMs > 0 && elapsedTimer.elapsed() > timeoutMs) {
            result.errorMessage = "Zeitueberschreitung beim Warten auf ComfyUI-History.";
            return result;
        }

        const NetworkCallResult historyResult = waitForReply(
            networkManager.get(QNetworkRequest(buildEndpoint(m_comfyUiBaseUrl, QString("/history/%1").arg(promptId)))),
            10000
        );
        if (historyResult.success) {
            const QJsonDocument historyDocument = QJsonDocument::fromJson(historyResult.payload);
            if (historyDocument.isObject()) {
                historyEntry = extractComfyHistoryEntry(historyDocument.object(), promptId);
            }
        }

        if (historyEntry.isEmpty()) {
            QThread::msleep(static_cast<unsigned long>(pollIntervalMs));
        }
    }

    QStringList savedFiles;
    if (downloadImages) {
        QString outputDirectory = configString(config, "save_outputs_to");
        if (outputDirectory.isEmpty()) {
            outputDirectory = QString("artifacts/comfyui/%1").arg(promptId);
        }

        QString directoryError;
        const QString absoluteOutputDirectory = resolveWorkspacePath(outputDirectory, true, &directoryError);
        if (absoluteOutputDirectory.isEmpty()) {
            result.errorMessage = directoryError;
            return result;
        }

        QDir().mkpath(absoluteOutputDirectory);

        const QJsonObject outputs = historyEntry.value("outputs").toObject();
        for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
            const QJsonArray images = it.value().toObject().value("images").toArray();
            for (const QJsonValue& imageValue : images) {
                const QJsonObject imageObject = imageValue.toObject();
                QUrl viewUrl = buildEndpoint(m_comfyUiBaseUrl, "/view");
                QUrlQuery query;
                query.addQueryItem("filename", imageObject.value("filename").toString());
                query.addQueryItem("subfolder", imageObject.value("subfolder").toString());
                query.addQueryItem("type", imageObject.value("type").toString());
                viewUrl.setQuery(query);

                const NetworkCallResult imageResult = waitForReply(
                    networkManager.get(QNetworkRequest(viewUrl)),
                    15000
                );
                if (!imageResult.success) {
                    result.logs.append(
                        QString("Bild konnte nicht heruntergeladen werden: %1").arg(imageResult.errorMessage)
                    );
                    continue;
                }

                QString relativeTargetPath = outputDirectory;
                const QString subfolder = imageObject.value("subfolder").toString().trimmed();
                if (!subfolder.isEmpty()) {
                    relativeTargetPath += "/" + subfolder;
                }

                QString targetDirectoryError;
                const QString absoluteImageDirectory = resolveWorkspacePath(relativeTargetPath, true, &targetDirectoryError);
                if (absoluteImageDirectory.isEmpty()) {
                    result.logs.append(targetDirectoryError);
                    continue;
                }

                QDir().mkpath(absoluteImageDirectory);
                const QString fileName = imageObject.value("filename").toString();
                const QString absoluteFilePath = QDir(absoluteImageDirectory).filePath(fileName);

                QSaveFile saveFile(absoluteFilePath);
                if (!saveFile.open(QIODevice::WriteOnly)) {
                    result.logs.append(
                        QString("Bild konnte nicht gespeichert werden: %1").arg(saveFile.errorString())
                    );
                    continue;
                }

                saveFile.write(imageResult.payload);
                if (!saveFile.commit()) {
                    result.logs.append(
                        QString("Bild konnte nicht abgeschlossen gespeichert werden: %1").arg(saveFile.errorString())
                    );
                    continue;
                }

                savedFiles.append(QDir(m_workspaceRoot).relativeFilePath(absoluteFilePath));
            }
        }
    }

    QJsonObject summary{
        { "tool", "comfyui.workflow" },
        { "prompt_id", promptId },
        { "client_id", clientId },
        { "saved_files", QJsonArray::fromStringList(savedFiles) },
        { "output_node_count", historyEntry.value("outputs").toObject().size() }
    };
    if (includeHistoryJson) {
        summary.insert("history", historyEntry);
    }

    result.success = true;
    result.outputText = QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    result.logs.append(QString("ComfyUI-Ausfuehrung abgeschlossen: %1").arg(promptId));
    result.logs.append(QString("Gespeicherte Dateien: %1").arg(savedFiles.size()));
    return result;
}

QString ToolExecutor::resolveWorkspacePath(
    const QString& path,
    const bool allowNonExisting,
    QString* errorMessage
) const
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Tool-Pfad darf nicht leer sein.";
        }
        return {};
    }

    const QFileInfo inputInfo(trimmedPath);
    const QString absolutePath = inputInfo.isAbsolute()
        ? QDir::cleanPath(inputInfo.absoluteFilePath())
        : QDir(m_workspaceRoot).absoluteFilePath(trimmedPath);

    if (!allowNonExisting && !QFileInfo::exists(absolutePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Pfad existiert nicht: %1").arg(absolutePath);
        }
        return {};
    }

    if (!isPathWithinWorkspace(absolutePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Pfad liegt ausserhalb des erlaubten Workspace: %1").arg(absolutePath);
        }
        return {};
    }

    const QString relativePath = QDir(m_workspaceRoot).relativeFilePath(absolutePath);
    const QStringList pathSegments = relativePath.split('/', Qt::SkipEmptyParts);
    if (pathSegments.contains(".git")) {
        if (errorMessage != nullptr) {
            *errorMessage = "Zugriffe auf .git-Verzeichnisse sind nicht erlaubt.";
        }
        return {};
    }

    return absolutePath;
}

bool ToolExecutor::isPathWithinWorkspace(const QString& absolutePath) const
{
    const QString workspacePath = QDir(m_workspaceRoot).absolutePath();
    const QString normalizedWorkspace = QDir::cleanPath(workspacePath).toCaseFolded();
    const QString normalizedCandidate = QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath()).toCaseFolded();

    return normalizedCandidate == normalizedWorkspace
        || normalizedCandidate.startsWith(normalizedWorkspace + "/")
        || normalizedCandidate.startsWith(normalizedWorkspace + "\\");
}

} // namespace privateclaw::tools
