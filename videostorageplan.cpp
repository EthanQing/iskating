#include "videostorageplan.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString shortId(const QString &value)
{
    const QString normalized = QString(value).remove(QLatin1Char('-')).trimmed();
    return normalized.left(8).toLower();
}

QString defaultStorageRoot()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appData.trimmed().isEmpty()) {
        return QDir(appData).absoluteFilePath(QStringLiteral("recordings"));
    }
    return QDir::home().absoluteFilePath(QStringLiteral("iSkating/recordings"));
}

QString configuredStorageRoot()
{
    QSettings settings;
    const QString configured = settings.value(QStringLiteral("videoStorage/rootDir")).toString().trimmed();
    if (!configured.isEmpty()) {
        return QFileInfo(configured).absoluteFilePath();
    }
    return defaultStorageRoot();
}

QString cameraToken(int camera)
{
    return camera > 0
               ? QStringLiteral("cam%1").arg(camera, 2, 10, QLatin1Char('0'))
               : QStringLiteral("cam00");
}

QDateTime localStartedAt(const QDateTime &startedAt)
{
    return startedAt.isValid() ? startedAt.toLocalTime() : QDateTime::currentDateTime();
}

QString relativeDirectory(const QDateTime &startedAt, const QString &athleteId, const QString &sessionId)
{
    const QDateTime local = localStartedAt(startedAt);
    return QStringLiteral("%1/%2/%3/athlete-%4/session-%5")
        .arg(local.toString(QStringLiteral("yyyy")),
             local.toString(QStringLiteral("MM")),
             local.toString(QStringLiteral("dd")),
             shortId(athleteId),
             shortId(sessionId));
}

QString plannedFileName(const QDateTime &startedAt, int camera, const QString &sessionId)
{
    const QDateTime local = localStartedAt(startedAt);
    return QStringLiteral("%1_%2_v01_session-%3.mp4")
        .arg(local.toString(QStringLiteral("yyyyMMdd-HHmmss")),
             cameraToken(camera),
             shortId(sessionId));
}

QString metadataJson(const VideoStoragePlanInput &input,
                     const QString &storageRoot,
                     const QString &relativeDir,
                     const QString &fileName,
                     const QString &filePath,
                     const QString &metadataPath,
                     const QString &status,
                     int sessionStartMs,
                     int sessionEndMs,
                     int durationMs,
                     qint64 fileSizeBytes,
                     const QDateTime &fileModifiedAt)
{
    QJsonObject object{
        {QStringLiteral("sessionId"), input.sessionId},
        {QStringLiteral("athleteId"), input.athleteId},
        {QStringLiteral("athleteName"), input.athleteName},
        {QStringLiteral("startedAt"), input.startedAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("videoIndex"), 1},
        {QStringLiteral("camera"), input.camera},
        {QStringLiteral("cameraName"), input.cameraName},
        {QStringLiteral("sourceUrl"), input.sourceUrl},
        {QStringLiteral("fallbackUrl"), input.fallbackUrl},
        {QStringLiteral("storageRoot"), storageRoot},
        {QStringLiteral("relativeDir"), relativeDir},
        {QStringLiteral("fileName"), fileName},
        {QStringLiteral("filePath"), filePath},
        {QStringLiteral("metadataPath"), metadataPath},
        {QStringLiteral("status"), status},
        {QStringLiteral("sessionStartMs"), sessionStartMs},
        {QStringLiteral("sessionEndMs"), sessionEndMs},
        {QStringLiteral("durationMs"), durationMs},
        {QStringLiteral("fileSizeBytes"), QString::number(fileSizeBytes)},
        {QStringLiteral("fileModifiedAt"), fileModifiedAt.isValid() ? fileModifiedAt.toUTC().toString(Qt::ISODateWithMs) : QString()}
    };
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

TrainingVideoFile buildTrainingVideoFilePlan(const VideoStoragePlanInput &input)
{
    const QString storageRoot = configuredStorageRoot();
    const QString relativeDir = relativeDirectory(input.startedAt, input.athleteId, input.sessionId);
    const QString fileName = plannedFileName(input.startedAt, input.camera, input.sessionId);
    const QString plannedPath = QDir(storageRoot).absoluteFilePath(relativeDir + QLatin1Char('/') + fileName);
    const QString metadataPath = QDir(storageRoot).absoluteFilePath(relativeDir + QLatin1Char('/') + QFileInfo(fileName).completeBaseName() + QStringLiteral(".metadata.json"));
    const QString status = input.externalFile ? QStringLiteral("external") : QStringLiteral("planned");
    const QString filePath = input.externalFile ? QFileInfo(input.sourceUrl).absoluteFilePath() : plannedPath;
    const QFileInfo sourceFile(filePath);
    const int durationMs = std::max(0, input.durationSec * 1000);
    const qint64 fileSizeBytes = input.externalFile && sourceFile.exists() && sourceFile.isFile() ? sourceFile.size() : -1;
    const QDateTime fileModifiedAt = input.externalFile && sourceFile.exists() && sourceFile.isFile() ? sourceFile.lastModified() : QDateTime();

    TrainingVideoFile file;
    file.sessionId = input.sessionId;
    file.videoIndex = 1;
    file.camera = input.camera;
    file.cameraName = input.cameraName;
    file.sourceUrl = input.sourceUrl;
    file.fallbackUrl = input.fallbackUrl;
    file.storageRoot = storageRoot;
    file.relativeDir = relativeDir;
    file.fileName = fileName;
    file.filePath = filePath;
    file.metadataPath = metadataPath;
    file.status = status;
    file.sessionStartMs = 0;
    file.sessionEndMs = durationMs;
    file.durationMs = durationMs;
    file.fileSizeBytes = fileSizeBytes;
    file.fileModifiedAt = fileModifiedAt;
    file.metadataJson = metadataJson(input,
                                     storageRoot,
                                     relativeDir,
                                     fileName,
                                     filePath,
                                     metadataPath,
                                     status,
                                     file.sessionStartMs,
                                     file.sessionEndMs,
                                     file.durationMs,
                                     file.fileSizeBytes,
                                     file.fileModifiedAt);
    return file;
}
