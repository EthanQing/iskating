#include "cameraconfigtemplate.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

constexpr int kTemplateVersion = 1;
constexpr int kDefaultPort = 554;
constexpr int kDefaultPreviewFps = 30;
constexpr int kDefaultMainFps = 120;

QString normalizeStoredPath(const QString &path)
{
    QString normalized = path.trimmed();
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}

CameraSlotSettings defaultCameraSlotSettings(int cameraIndex)
{
    CameraSlotSettings settings;
    const double segmentLengthM = 5.0;
    settings.fieldStartM = cameraIndex * segmentLengthM;
    settings.fieldEndM = (cameraIndex + 1) * segmentLengthM;
    settings.role = QStringLiteral("轨迹分段");
    settings.trajectoryEnabled = true;
    return settings;
}

QString jsonString(const QJsonObject &object, const QString &key, const QString &fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : fallback;
}

QString jsonRawString(const QJsonObject &object, const QString &key, const QString &fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

int jsonInt(const QJsonObject &object, const QString &key, int fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt(fallback) : fallback;
}

double jsonDouble(const QJsonObject &object, const QString &key, double fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble(fallback) : fallback;
}

bool jsonBool(const QJsonObject &object, const QString &key, bool fallback)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool(fallback) : fallback;
}

bool validateTemplateData(const CameraConfigTemplateData &data, QString *errorMessage)
{
    bool ok = false;
    const int port = data.shared.port.trimmed().isEmpty()
                         ? kDefaultPort
                         : data.shared.port.trimmed().toInt(&ok);
    if ((!ok && !data.shared.port.trimmed().isEmpty()) || port <= 0 || port > 65535) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("端口必须是 1 到 65535 之间的数字。");
        }
        return false;
    }

    bool hasConfiguredCamera = false;
    for (const CameraSlotSettings &camera : data.cameras) {
        if (!camera.ip.trimmed().isEmpty()) {
            hasConfiguredCamera = true;
            break;
        }
    }
    if (hasConfiguredCamera && data.shared.previewPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("至少配置一路相机 IP 时，预览路径不能为空。");
        }
        return false;
    }

    const QString nvrTemplate = data.shared.nvrPlaybackTemplate.trimmed();
    if (!nvrTemplate.isEmpty()
        && (!nvrTemplate.contains(QStringLiteral("{ip}"))
            || !nvrTemplate.contains(QStringLiteral("{start}"))
            || !nvrTemplate.contains(QStringLiteral("{end}")))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("NVR 回放模板至少需要包含 {ip}、{start} 和 {end} 占位符。");
        }
        return false;
    }

    for (int i = 0; i < data.cameras.size(); ++i) {
        const CameraSlotSettings &camera = data.cameras.at(i);
        if (camera.trajectoryEnabled && camera.fieldEndM <= camera.fieldStartM) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("CAM %1 的终点距离必须大于起点距离。")
                                    .arg(i + 1, 2, 10, QLatin1Char('0'));
            }
            return false;
        }
    }

    return true;
}

QJsonObject sharedToJson(const SharedCameraSettings &settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("username"), settings.username.trimmed());
    object.insert(QStringLiteral("password"), settings.password);
    object.insert(QStringLiteral("port"), settings.port.trimmed().isEmpty()
                                        ? QString::number(kDefaultPort)
                                        : settings.port.trimmed());
    object.insert(QStringLiteral("previewPath"), normalizeStoredPath(settings.previewPath));
    object.insert(QStringLiteral("previewFps"), settings.previewFps > 0 ? settings.previewFps : kDefaultPreviewFps);
    object.insert(QStringLiteral("mainPath"), normalizeStoredPath(settings.mainPath));
    object.insert(QStringLiteral("mainFps"), settings.mainFps > 0 ? settings.mainFps : kDefaultMainFps);
    object.insert(QStringLiteral("nvrPlaybackTemplate"), settings.nvrPlaybackTemplate.trimmed());
    return object;
}

QJsonObject captureToJson(const CapturePreferenceSettings &settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("modelPrecision"), settings.modelPrecision.trimmed().isEmpty()
                                                    ? QStringLiteral("balanced")
                                                    : settings.modelPrecision.trimmed());
    object.insert(QStringLiteral("analysisSource"), settings.analysisSource == QStringLiteral("main")
                                                     ? QStringLiteral("main")
                                                     : QStringLiteral("preview"));
    object.insert(QStringLiteral("analysisTargetFps"), settings.analysisTargetFps > 0 ? settings.analysisTargetFps : 5);
    object.insert(QStringLiteral("analysisMaxStreams"), settings.analysisMaxStreams > 0 ? settings.analysisMaxStreams : 12);
    object.insert(QStringLiteral("analysisAutoDegrade"), settings.analysisAutoDegrade);
    object.insert(QStringLiteral("fps"), settings.analysisTargetFps > 0 ? settings.analysisTargetFps : 5);
    return object;
}

QJsonObject cameraToJson(const CameraSlotSettings &settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("ip"), settings.ip.trimmed());
    object.insert(QStringLiteral("trajectoryEnabled"), settings.trajectoryEnabled);
    object.insert(QStringLiteral("role"), settings.role.trimmed().isEmpty()
                                          ? QStringLiteral("轨迹分段")
                                          : settings.role.trimmed());
    object.insert(QStringLiteral("fieldStartM"), settings.fieldStartM);
    object.insert(QStringLiteral("fieldEndM"), settings.fieldEndM);
    object.insert(QStringLiteral("lateralOffsetM"), settings.lateralOffsetM);
    object.insert(QStringLiteral("mountHeightM"), settings.mountHeightM);
    object.insert(QStringLiteral("yawDeg"), settings.yawDeg);
    object.insert(QStringLiteral("pitchDeg"), settings.pitchDeg);
    object.insert(QStringLiteral("calibration"), settings.calibrationJson.trimmed());
    object.insert(QStringLiteral("qualityNote"), settings.qualityNote.trimmed());
    object.insert(QStringLiteral("compatibilityNote"), settings.compatibilityNote.trimmed());
    return object;
}

SharedCameraSettings sharedFromJson(const QJsonObject &object)
{
    SharedCameraSettings settings;
    settings.username = jsonString(object, QStringLiteral("username"));
    settings.password = jsonRawString(object, QStringLiteral("password"));
    settings.port = jsonString(object, QStringLiteral("port"), QString::number(kDefaultPort));
    if (settings.port.trimmed().isEmpty()) {
        settings.port = QString::number(kDefaultPort);
    }
    settings.previewPath = normalizeStoredPath(jsonString(object, QStringLiteral("previewPath")));
    settings.previewFps = jsonInt(object, QStringLiteral("previewFps"), kDefaultPreviewFps);
    if (settings.previewFps <= 0) {
        settings.previewFps = kDefaultPreviewFps;
    }
    settings.mainPath = normalizeStoredPath(jsonString(object, QStringLiteral("mainPath")));
    settings.mainFps = jsonInt(object, QStringLiteral("mainFps"), kDefaultMainFps);
    if (settings.mainFps <= 0) {
        settings.mainFps = kDefaultMainFps;
    }
    settings.nvrPlaybackTemplate = jsonString(object, QStringLiteral("nvrPlaybackTemplate"));
    return settings;
}

CapturePreferenceSettings captureFromJson(const QJsonObject &object, int mainFps)
{
    Q_UNUSED(mainFps);
    CapturePreferenceSettings settings;
    settings.modelPrecision = jsonString(object, QStringLiteral("modelPrecision"), QStringLiteral("balanced"));
    if (settings.modelPrecision.isEmpty()) {
        settings.modelPrecision = QStringLiteral("balanced");
    }
    settings.analysisSource = jsonString(object, QStringLiteral("analysisSource"), QStringLiteral("preview"));
    if (settings.analysisSource != QStringLiteral("main")) {
        settings.analysisSource = QStringLiteral("preview");
    }
    settings.analysisTargetFps = jsonInt(object, QStringLiteral("analysisTargetFps"), 5);
    if (settings.analysisTargetFps <= 0) {
        settings.analysisTargetFps = 5;
    }
    settings.analysisMaxStreams = jsonInt(object, QStringLiteral("analysisMaxStreams"), 12);
    if (settings.analysisMaxStreams <= 0) {
        settings.analysisMaxStreams = 12;
    }
    settings.analysisAutoDegrade = jsonBool(object, QStringLiteral("analysisAutoDegrade"), true);
    settings.fps = settings.analysisTargetFps;
    return settings;
}

CameraSlotSettings cameraFromJson(const QJsonObject &object, int cameraIndex)
{
    CameraSlotSettings settings = defaultCameraSlotSettings(cameraIndex);
    settings.ip = jsonString(object, QStringLiteral("ip"));
    settings.trajectoryEnabled = jsonBool(object, QStringLiteral("trajectoryEnabled"), settings.trajectoryEnabled);
    settings.role = jsonString(object, QStringLiteral("role"), settings.role);
    if (settings.role.trimmed().isEmpty()) {
        settings.role = QStringLiteral("轨迹分段");
    }
    settings.fieldStartM = jsonDouble(object, QStringLiteral("fieldStartM"), settings.fieldStartM);
    settings.fieldEndM = jsonDouble(object, QStringLiteral("fieldEndM"), settings.fieldEndM);
    settings.lateralOffsetM = jsonDouble(object, QStringLiteral("lateralOffsetM"), settings.lateralOffsetM);
    settings.mountHeightM = jsonDouble(object, QStringLiteral("mountHeightM"), settings.mountHeightM);
    settings.yawDeg = jsonDouble(object, QStringLiteral("yawDeg"), settings.yawDeg);
    settings.pitchDeg = jsonDouble(object, QStringLiteral("pitchDeg"), settings.pitchDeg);
    settings.calibrationJson = jsonString(object, QStringLiteral("calibration"));
    settings.qualityNote = jsonString(object, QStringLiteral("qualityNote"));
    settings.compatibilityNote = jsonString(object, QStringLiteral("compatibilityNote"), settings.qualityNote);
    return settings;
}

} // namespace

QString cameraConfigTemplateFileFilter()
{
    return QStringLiteral("摄像头配置模板 (*.json);;JSON 文件 (*.json)");
}

CameraConfigTemplateResult loadCameraConfigTemplate(const QString &filePath, int cameraCount)
{
    CameraConfigTemplateResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QStringLiteral("无法打开模板文件：%1").arg(file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("模板 JSON 格式无效：%1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    const int version = jsonInt(root, QStringLiteral("version"), kTemplateVersion);
    if (version > kTemplateVersion) {
        result.error = QStringLiteral("模板版本 %1 高于当前支持版本 %2。").arg(version).arg(kTemplateVersion);
        return result;
    }

    const QJsonValue defaultsValue = root.value(QStringLiteral("cameraDefaults"));
    if (!defaultsValue.isObject()) {
        result.error = QStringLiteral("模板缺少 cameraDefaults 对象。");
        return result;
    }

    result.data.shared = sharedFromJson(defaultsValue.toObject());
    result.data.capture = captureFromJson(root.value(QStringLiteral("capture")).toObject(),
                                          result.data.shared.mainFps);

    const QJsonArray cameraArray = root.value(QStringLiteral("cameras")).toArray();
    result.data.sourceCameraCount = cameraArray.size();
    result.data.cameras.reserve(cameraCount);
    const int importCount = qMin(cameraArray.size(), cameraCount);
    for (int i = 0; i < importCount; ++i) {
        result.data.cameras.append(cameraFromJson(cameraArray.at(i).toObject(), i));
    }
    for (int i = importCount; i < cameraCount; ++i) {
        result.data.cameras.append(defaultCameraSlotSettings(i));
    }

    if (cameraArray.size() < cameraCount) {
        result.data.warnings.append(QStringLiteral("模板只有 %1 路相机，剩余 %2 路已按默认场地段补齐。")
                                        .arg(cameraArray.size())
                                        .arg(cameraCount - cameraArray.size()));
    } else if (cameraArray.size() > cameraCount) {
        result.data.warnings.append(QStringLiteral("模板包含 %1 路相机，当前只导入前 %2 路。")
                                        .arg(cameraArray.size())
                                        .arg(cameraCount));
    }

    QString validationError;
    if (!validateTemplateData(result.data, &validationError)) {
        result.error = validationError;
        return result;
    }

    result.ok = true;
    return result;
}

bool saveCameraConfigTemplate(const QString &filePath,
                              const SharedCameraSettings &shared,
                              const CapturePreferenceSettings &capture,
                              const QVector<CameraSlotSettings> &cameras,
                              QString *errorMessage)
{
    CameraConfigTemplateData data;
    data.shared = shared;
    data.capture = capture;
    data.cameras = cameras;
    if (!validateTemplateData(data, errorMessage)) {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kTemplateVersion);
    root.insert(QStringLiteral("cameraDefaults"), sharedToJson(shared));
    root.insert(QStringLiteral("capture"), captureToJson(capture));

    QJsonArray cameraArray;
    for (const CameraSlotSettings &camera : cameras) {
        cameraArray.append(cameraToJson(camera));
    }
    root.insert(QStringLiteral("cameras"), cameraArray);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入模板文件：%1").arg(file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.flush()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("写入模板文件失败：%1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

QString cameraConfigTemplateSummary(const CameraConfigTemplateData &data)
{
    int configured = 0;
    int trajectoryEnabled = 0;
    int calibrated = 0;
    for (const CameraSlotSettings &camera : data.cameras) {
        if (!camera.ip.trimmed().isEmpty()) {
            ++configured;
        }
        if (camera.trajectoryEnabled) {
            ++trajectoryEnabled;
            if (camera.fieldEndM > camera.fieldStartM) {
                ++calibrated;
            }
        }
    }

    QStringList lines;
    lines.append(QStringLiteral("相机 IP：已配置 %1/%2 路。").arg(configured).arg(data.cameras.size()));
    lines.append(QStringLiteral("RTSP：端口 %1，预览路径 %2，主码流路径 %3。")
                     .arg(data.shared.port.trimmed().isEmpty() ? QString::number(kDefaultPort) : data.shared.port.trimmed(),
                          data.shared.previewPath.trimmed().isEmpty() ? QStringLiteral("未设置") : data.shared.previewPath.trimmed(),
                          data.shared.mainPath.trimmed().isEmpty() ? QStringLiteral("沿用预览路径") : data.shared.mainPath.trimmed()));
    lines.append(QStringLiteral("场地标定：参与轨迹 %1 路，有效标定 %2 路。")
                     .arg(trajectoryEnabled)
                     .arg(calibrated));
    lines.append(QStringLiteral("NVR 回放模板：%1。")
                     .arg(data.shared.nvrPlaybackTemplate.trimmed().isEmpty() ? QStringLiteral("未设置") : QStringLiteral("已设置")));
    if (!data.warnings.isEmpty()) {
        lines.append(QStringLiteral("提示：%1").arg(data.warnings.join(QStringLiteral("；"))));
    }
    return lines.join(QLatin1Char('\n'));
}
