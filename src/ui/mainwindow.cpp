#include "mainwindow.h"
#include "animatedbutton.h"
#include "competitionmanagementdialog.h"
#include "athleteanalysismanager.h"
#include "analysistaskcenterdialog.h"
#include "analysistaskmanager.h"
#include "framelessdialog.h"
#include "iconutils.h"
#include "nvrplayback.h"
#include "offlineanalysisdialog.h"
#include "offlinevideoprobe.h"
#include "personmanagementdialog.h"
#include "rtspstream.h"
#include "trainingreviewdialog.h"
#include "trainingrepository.h"
#include "trajectorywidget.h"
#include "ui_mainwindow.h"
#include "videostorageplan.h"
#include "videoopenglwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPageLayout>
#include <QPageSize>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QPushButton>
#include <QDebug>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QScrollArea>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringConverter>
#include <QStringList>
#include <QStorageInfo>
#include <QStyle>
#include <QSet>
#include <QTextDocument>
#include <QTextStream>
#include <QTime>
#include <QThread>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>
#include <QAbstractItemView>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

#include "xlsxdocument.h"
#include "xlsxformat.h"

namespace {

constexpr int kCapturePage = 0;
constexpr int kHistoryPage = 1;
constexpr int kSuggestionPage = 2;
constexpr int kSystemStatusPage = 3;
constexpr int kDefaultFps = 30;
constexpr int kDefaultPreviewStreamFps = 30;
constexpr int kDefaultMainStreamFps = 120;
constexpr int kDefaultAnalysisTargetFps = 5;
constexpr int kDefaultAnalysisMaxStreams = 12;
constexpr int kDefaultRtspPort = 554;
constexpr int kHistoryActionButtonWidth = 92;
constexpr int kScoreTagWidth = 68;
constexpr int kMaxTimelineSamples = 20000;
constexpr qint64 kAthleteTimelineSampleIntervalMs = 200;
constexpr int kSpeedSmoothingWindowMs = 1000;
constexpr const char *kSpeedAlgorithmVersion = "trajectory_speed_v1";
constexpr const char *kPreviousWindowStateProperty = "previousWindowStateBeforeFullScreen";

QString cameraSettingsGroup(int cameraIndex)
{
    return QStringLiteral("cameras/camera%1").arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
}

QString defaultCameraChannelName(int cameraIndex)
{
    return QStringLiteral("CAM %1").arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
}

QString defaultVideoStorageRoot()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appData.trimmed().isEmpty()) {
        return QDir(appData).absoluteFilePath(QStringLiteral("recordings"));
    }
    return QDir::home().absoluteFilePath(QStringLiteral("iSkating/recordings"));
}

QString normalizedAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isPathUnderRoot(const QString &path, const QString &root)
{
    const QString normalizedPath = normalizedAbsolutePath(path);
    QString normalizedRoot = normalizedAbsolutePath(root);
    while (normalizedRoot.endsWith(QLatin1Char('/')) || normalizedRoot.endsWith(QLatin1Char('\\'))) {
        normalizedRoot.chop(1);
    }
    return normalizedPath == normalizedRoot
           || normalizedPath.startsWith(normalizedRoot + QLatin1Char('/'), Qt::CaseInsensitive)
           || normalizedPath.startsWith(normalizedRoot + QLatin1Char('\\'), Qt::CaseInsensitive);
}

QString storageSizeLabel(qint64 bytes)
{
    if (bytes < 0) {
        return QStringLiteral("未知");
    }
    const double gb = static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0;
    if (gb >= 1.0) {
        return QStringLiteral("%1 GB").arg(gb, 0, 'f', 2);
    }
    const double mb = static_cast<double>(bytes) / 1024.0 / 1024.0;
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}

QString configuredCameraChannelName(int cameraIndex, const QString &ip)
{
    const QString base = defaultCameraChannelName(cameraIndex);
    const QString trimmedIp = ip.trimmed();
    return trimmedIp.isEmpty() ? base : QStringLiteral("%1 - %2").arg(base, trimmedIp);
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

bool mapImagePointToField(const CameraSlotSettings &slot, const QPointF &imagePoint, QPointF *fieldPoint)
{
    if (!fieldPoint) {
        return false;
    }
    const QJsonArray points = QJsonDocument::fromJson(slot.calibrationJson.toUtf8()).array();
    if (points.size() != 4) {
        return false;
    }
    double matrix[8][9]{};
    for (int i = 0; i < 4; ++i) {
        const QJsonObject point = points.at(i).toObject();
        const double px = point.value(QStringLiteral("px")).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double py = point.value(QStringLiteral("py")).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double x = point.value(QStringLiteral("x")).toDouble(std::numeric_limits<double>::quiet_NaN());
        const double y = point.value(QStringLiteral("y")).toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(x) || !std::isfinite(y)) {
            return false;
        }
        matrix[i * 2][0] = px; matrix[i * 2][1] = py; matrix[i * 2][2] = 1.0;
        matrix[i * 2][6] = -px * x; matrix[i * 2][7] = -py * x; matrix[i * 2][8] = x;
        matrix[i * 2 + 1][3] = px; matrix[i * 2 + 1][4] = py; matrix[i * 2 + 1][5] = 1.0;
        matrix[i * 2 + 1][6] = -px * y; matrix[i * 2 + 1][7] = -py * y; matrix[i * 2 + 1][8] = y;
    }
    for (int column = 0; column < 8; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 8; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) < 1e-9) {
            return false;
        }
        for (int value = column; value <= 8; ++value) {
            std::swap(matrix[column][value], matrix[pivot][value]);
        }
        const double divisor = matrix[column][column];
        for (int value = column; value <= 8; ++value) {
            matrix[column][value] /= divisor;
        }
        for (int row = 0; row < 8; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = matrix[row][column];
            for (int value = column; value <= 8; ++value) {
                matrix[row][value] -= factor * matrix[column][value];
            }
        }
    }
    const double h[8] = {matrix[0][8], matrix[1][8], matrix[2][8], matrix[3][8], matrix[4][8], matrix[5][8], matrix[6][8], matrix[7][8]};
    const double denominator = h[6] * imagePoint.x() + h[7] * imagePoint.y() + 1.0;
    if (std::abs(denominator) < 1e-9) {
        return false;
    }
    *fieldPoint = QPointF((h[0] * imagePoint.x() + h[1] * imagePoint.y() + h[2]) / denominator,
                          (h[3] * imagePoint.x() + h[4] * imagePoint.y() + h[5]) / denominator);
    return std::isfinite(fieldPoint->x()) && std::isfinite(fieldPoint->y());
}

QString cameraSegmentLabel(const CameraSlotSettings &slot)
{
    if (!slot.trajectoryEnabled || slot.fieldEndM <= slot.fieldStartM) {
        return QStringLiteral("未参与轨迹");
    }
    return QStringLiteral("%1 · %2-%3 m")
        .arg(slot.role.trimmed().isEmpty() ? QStringLiteral("轨迹分段") : slot.role.trimmed())
        .arg(slot.fieldStartM, 0, 'f', 1)
        .arg(slot.fieldEndM, 0, 'f', 1);
}

QString normalizedStoredPath(const QString &path)
{
    QString normalized = path.trimmed();
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}

QString normalizedRtspPath(const QString &path)
{
    const QString normalized = normalizedStoredPath(path);
    return normalized.isEmpty() ? QString() : QStringLiteral("/") + normalized;
}

QString composeLegacyStreamUrl(const QString &ipOrUrl, const QString &port, const QString &path)
{
    const QString trimmedPath = path.trimmed();
    const QString trimmedIp = ipOrUrl.trimmed();
    const QString trimmedPort = port.trimmed();

    if (trimmedPath.contains(QStringLiteral("://"))) {
        return trimmedPath;
    }

    if (trimmedIp.contains(QStringLiteral("://"))) {
        QString url = trimmedIp;
        if (!trimmedPath.isEmpty()) {
            if (!url.endsWith(QLatin1Char('/')) && !trimmedPath.startsWith(QLatin1Char('/'))) {
                url += QLatin1Char('/');
            } else if (url.endsWith(QLatin1Char('/')) && trimmedPath.startsWith(QLatin1Char('/'))) {
                url.chop(1);
            }
            url += trimmedPath;
        }
        return url;
    }

    if (trimmedIp.isEmpty()) {
        return trimmedPath;
    }

    QString url = QStringLiteral("rtsp://") + trimmedIp;
    if (!trimmedPort.isEmpty()) {
        url += QStringLiteral(":") + trimmedPort;
    }
    if (!trimmedPath.isEmpty()) {
        if (!trimmedPath.startsWith(QLatin1Char('/'))) {
            url += QLatin1Char('/');
        }
        url += trimmedPath;
    }
    return url;
}

struct ParsedRtspUrl
{
    bool valid = false;
    QString username;
    QString password;
    QString host;
    QString port;
    QString path;
};

ParsedRtspUrl parseRtspUrl(const QString &source)
{
    ParsedRtspUrl parsed;
    const QString trimmedSource = source.trimmed();
    if (trimmedSource.isEmpty()) {
        return parsed;
    }

    const QUrl url = QUrl::fromEncoded(trimmedSource.toUtf8(), QUrl::TolerantMode);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) != 0) {
        return parsed;
    }
    if (url.host().trimmed().isEmpty()) {
        return parsed;
    }

    parsed.valid = true;
    parsed.username = url.userName(QUrl::FullyDecoded);
    parsed.password = url.password(QUrl::FullyDecoded);
    parsed.host = url.host().trimmed();
    if (url.port() > 0) {
        parsed.port = QString::number(url.port());
    }
    parsed.path = normalizedStoredPath(url.path());
    return parsed;
}

QString extractHostFromLegacyValue(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const ParsedRtspUrl parsed = parseRtspUrl(trimmed);
    return parsed.valid ? parsed.host : trimmed;
}

QString composeCameraUrl(const SharedCameraSettings &sharedSettings, const QString &ip, bool mainStream)
{
    const QString trimmedIp = ip.trimmed();
    if (trimmedIp.isEmpty()) {
        return QString();
    }

    QUrl url;
    url.setScheme(QStringLiteral("rtsp"));
    if (!sharedSettings.username.trimmed().isEmpty()) {
        url.setUserName(sharedSettings.username.trimmed());
    }
    if (!sharedSettings.password.isEmpty()) {
        url.setPassword(sharedSettings.password);
    }
    url.setHost(trimmedIp);

    const QString normalizedPort = sharedSettings.port.trimmed();
    if (normalizedPort.isEmpty()) {
        url.setPort(kDefaultRtspPort);
    } else {
        bool ok = false;
        const int port = normalizedPort.toInt(&ok);
        if (ok && port > 0) {
            url.setPort(port);
        }
    }

    const QString selectedPath = mainStream && !sharedSettings.mainPath.trimmed().isEmpty()
                                     ? sharedSettings.mainPath
                                     : sharedSettings.previewPath;
    url.setPath(normalizedRtspPath(selectedPath));
    return url.toString(QUrl::FullyEncoded);
}

bool hasStructuredCameraDefaults(QSettings &settings)
{
    return settings.contains(QStringLiteral("cameraDefaults/username"))
           || settings.contains(QStringLiteral("cameraDefaults/password"))
           || settings.contains(QStringLiteral("cameraDefaults/port"))
           || settings.contains(QStringLiteral("cameraDefaults/previewPath"))
           || settings.contains(QStringLiteral("cameraDefaults/mainPath"))
           || settings.contains(QStringLiteral("cameraDefaults/nvrPlaybackTemplate"));
}

QString safeUrlForLog(const QString &source)
{
    QUrl url = QUrl::fromEncoded(source.toUtf8(), QUrl::TolerantMode);
    if (!url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
        return url.toString(QUrl::FullyEncoded);
    }
    return source;
}

bool hasMediaUrlScheme(const QString &source)
{
    return source.trimmed().contains(QStringLiteral("://"));
}

QString windowsPathForNasUri(const QString &source)
{
    const QString prefix = QStringLiteral("nas://");
    const QString trimmed = source.trimmed();
    if (!trimmed.startsWith(prefix, Qt::CaseInsensitive)) {
        return QString();
    }

    QString relativePath = QDir::cleanPath(trimmed.mid(prefix.size()));
    if (relativePath.isEmpty()
        || relativePath == QStringLiteral("..")
        || relativePath.startsWith(QStringLiteral("../"))
        || QDir::isAbsolutePath(relativePath)) {
        return QString();
    }

    const QString nasRoot = QSettings().value(QStringLiteral("offlineAnalysis/nasRoot")).toString().trimmed();
    if (nasRoot.isEmpty()) {
        return QString();
    }

    relativePath.replace(QLatin1Char('/'), QDir::separator());
    return QDir(nasRoot).absoluteFilePath(relativePath);
}

QString displayMediaSource(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("未记录");
    }

    if (!hasMediaUrlScheme(trimmed)) {
        const QFileInfo fileInfo(trimmed);
        return fileInfo.exists()
                   ? QStringLiteral("%1（%2）").arg(fileInfo.fileName(), QDir::toNativeSeparators(fileInfo.absoluteFilePath()))
                   : QDir::toNativeSeparators(trimmed);
    }

    QUrl url = QUrl::fromEncoded(trimmed.toUtf8(), QUrl::TolerantMode);
    if (!url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
    }
    return url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QString videoFileStatusLabel(const QString &status)
{
    if (status == QStringLiteral("external")) {
        return QStringLiteral("外部文件");
    }
    if (status == QStringLiteral("recorded")) {
        return QStringLiteral("已录制");
    }
    return QStringLiteral("已规划");
}

QString videoFileDisplayPath(const TrainingVideoFile &file)
{
    if (!file.filePath.trimmed().isEmpty()) {
        return QDir::toNativeSeparators(file.filePath.trimmed());
    }
    if (!file.sourceUrl.trimmed().isEmpty()) {
        return displayMediaSource(file.sourceUrl);
    }
    return QStringLiteral("未记录");
}

QString compactMilliseconds(int milliseconds)
{
    const int safeMs = std::max(0, milliseconds);
    const int totalSeconds = safeMs / 1000;
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    const int millis = safeMs % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString videoFileTimeRange(const TrainingVideoFile &file)
{
    if (file.sessionEndMs <= file.sessionStartMs && file.durationMs <= 0) {
        return QStringLiteral("时间未索引");
    }
    const int endMs = file.sessionEndMs > file.sessionStartMs ? file.sessionEndMs : file.durationMs;
    return QStringLiteral("%1-%2")
        .arg(compactMilliseconds(file.sessionStartMs),
             compactMilliseconds(endMs));
}

QString videoFileSizeLabel(const TrainingVideoFile &file)
{
    if (file.fileSizeBytes < 0) {
        return QStringLiteral("大小未记录");
    }
    const double mb = static_cast<double>(file.fileSizeBytes) / 1024.0 / 1024.0;
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}

QString videoFilesSummary(const QVector<TrainingVideoFile> &files)
{
    if (files.isEmpty()) {
        return QStringLiteral("未登记视频资产");
    }

    QStringList lines;
    for (const TrainingVideoFile &file : files) {
        lines << QStringLiteral("#%1 %2 · %3 · %4 · %5 · %6")
                     .arg(file.videoIndex)
                     .arg(videoFileStatusLabel(file.status))
                     .arg(file.cameraName.trimmed().isEmpty()
                              ? QStringLiteral("CAM %1").arg(file.camera, 2, 10, QLatin1Char('0'))
                              : file.cameraName.trimmed())
                     .arg(videoFileTimeRange(file))
                     .arg(videoFileSizeLabel(file))
                     .arg(videoFileDisplayPath(file));
    }
    return lines.join(QLatin1Char('\n'));
}

QString videoFileIndexes(const QVector<TrainingVideoFile> &files)
{
    QStringList values;
    for (const TrainingVideoFile &file : files) {
        values << QString::number(file.videoIndex);
    }
    return values.join(QLatin1Char('|'));
}

QString videoFileStatuses(const QVector<TrainingVideoFile> &files)
{
    QStringList values;
    for (const TrainingVideoFile &file : files) {
        values << file.status;
    }
    return values.join(QLatin1Char('|'));
}

QString videoFilePaths(const QVector<TrainingVideoFile> &files)
{
    QStringList values;
    for (const TrainingVideoFile &file : files) {
        values << videoFileDisplayPath(file);
    }
    return values.join(QLatin1Char('|'));
}

QString videoFileRanges(const QVector<TrainingVideoFile> &files)
{
    QStringList values;
    for (const TrainingVideoFile &file : files) {
        values << videoFileTimeRange(file);
    }
    return values.join(QLatin1Char('|'));
}

QString videoFileSizes(const QVector<TrainingVideoFile> &files)
{
    QStringList values;
    for (const TrainingVideoFile &file : files) {
        values << (file.fileSizeBytes >= 0 ? QString::number(file.fileSizeBytes) : QString());
    }
    return values.join(QLatin1Char('|'));
}

const TrainingVideoFile *videoFileForRepetition(const SessionHistoryItem &record, const QString &videoFileId, int videoIndex)
{
    for (const TrainingVideoFile &file : record.videoFiles) {
        if (!videoFileId.trimmed().isEmpty() && file.id == videoFileId) {
            return &file;
        }
    }
    for (const TrainingVideoFile &file : record.videoFiles) {
        if (videoIndex > 0 && file.videoIndex == videoIndex) {
            return &file;
        }
    }
    return nullptr;
}

QString offlineVideoDisplayName(const QString &filePath)
{
    const QString fileName = QFileInfo(filePath).fileName();
    return fileName.isEmpty()
               ? QStringLiteral("离线视频")
               : QStringLiteral("离线视频 · %1").arg(fileName);
}

QString formatMediaDuration(qint64 durationMs)
{
    if (durationMs <= 0) {
        return QStringLiteral("未知时长");
    }
    const qint64 totalSeconds = durationMs / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QString offlineProbeSummary(const OfflineVideoProbeResult &probe)
{
    QStringList parts;
    if (!probe.codecName.trimmed().isEmpty()) {
        parts << QStringLiteral("编码 %1").arg(probe.codecName);
    }
    if (!probe.resolution.trimmed().isEmpty()) {
        parts << QStringLiteral("分辨率 %1").arg(probe.resolution);
    }
    if (probe.durationMs > 0) {
        parts << QStringLiteral("时长 %1").arg(formatMediaDuration(probe.durationMs));
    }
    return parts.join(QStringLiteral("，"));
}

QString offlineProbeMetadataJson(const OfflineVideoProbeResult &probe)
{
    QJsonObject object{
        {QStringLiteral("codecName"), probe.codecName},
        {QStringLiteral("resolution"), probe.resolution},
        {QStringLiteral("durationMs"), QString::number(probe.durationMs)},
        {QStringLiteral("seekable"), probe.seekable},
        {QStringLiteral("d3d11vaReady"), probe.d3d11vaReady},
        {QStringLiteral("fileSize"), QString::number(probe.fileSize)},
        {QStringLiteral("lastModified"), probe.lastModified.isValid() ? probe.lastModified.toUTC().toString(Qt::ISODateWithMs) : QString()},
        {QStringLiteral("message"), probe.message}
    };
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString analysisTaskSummary(const SessionHistoryItem &record)
{
    if (record.analysisTaskId.trimmed().isEmpty()) {
        return QStringLiteral("未关联离线任务");
    }
    QStringList parts{
        QStringLiteral("任务 %1").arg(record.analysisTaskId.left(8)),
        QStringLiteral("状态 %1").arg(record.analysisTaskStatus.trimmed().isEmpty()
                                     ? QStringLiteral("未知")
                                     : record.analysisTaskStatus.trimmed())
    };
    if (!record.analysisTaskBatchId.trimmed().isEmpty()) {
        parts << QStringLiteral("批次 %1").arg(record.analysisTaskBatchId.left(8));
    }
    if (record.analysisTaskCameraId > 0) {
        parts << QStringLiteral("机位 %1").arg(record.analysisTaskCameraId);
    }
    if (record.analysisTaskTimeOffsetMs != 0) {
        parts << QStringLiteral("偏移 %1ms").arg(record.analysisTaskTimeOffsetMs);
    }
    return parts.join(QStringLiteral(" · "));
}

bool sameOfflineProbeFile(const OfflineVideoProbeResult &probe, const QFileInfo &fileInfo)
{
    return probe.success
           && QFileInfo(probe.filePath).absoluteFilePath() == fileInfo.absoluteFilePath()
           && probe.fileSize == fileInfo.size()
           && probe.lastModified.isValid()
           && probe.lastModified == fileInfo.lastModified();
}

QString issueSummary(const QString &errorCodes)
{
    const QStringList issues = errorCodes.split(QStringLiteral("|"), Qt::SkipEmptyParts);
    return issues.isEmpty() ? QStringLiteral("未触发关键错误") : issues.join(QStringLiteral("、"));
}

QString qualityLabel(int score, bool valid)
{
    if (valid) {
        return QStringLiteral("达标");
    }
    if (score >= 70) {
        return QStringLiteral("接近");
    }
    return QStringLiteral("待纠正");
}

QString identityStatusLabel(const QString &status)
{
    if (status == QStringLiteral("identified")) {
        return QStringLiteral("已标识");
    }
    return QStringLiteral("未标识");
}

QString repetitionIdentityLabel(const ActionRepetition &repetition)
{
    QStringList parts;
    if (!repetition.athleteName.trimmed().isEmpty()) {
        parts.append(repetition.athleteName.trimmed());
    } else if (!repetition.athleteId.trimmed().isEmpty()) {
        parts.append(repetition.athleteId.trimmed());
    } else {
        parts.append(identityStatusLabel(repetition.identityStatus));
    }
    if (repetition.trackId >= 0) {
        parts.append(QStringLiteral("T%1").arg(repetition.trackId));
    }
    if (repetition.cameraId >= 0) {
        parts.append(QStringLiteral("C%1").arg(repetition.cameraId));
    }
    return parts.join(QStringLiteral(" · "));
}

QString repetitionReportLine(const ActionRepetition &repetition, int index, const std::function<QString(int)> &formatMs)
{
    const QString reviewTag = repetition.hasManualReview() ? QStringLiteral("  复核") : QString();
    return QStringLiteral("%1. 视频#%2  %3-%4  %5分  %6%7  错误：%8  反馈：%9")
        .arg(index + 1)
        .arg(repetition.videoIndex)
        .arg(formatMs(repetition.effectiveStartedMs()))
        .arg(formatMs(repetition.effectiveEndedMs()))
        .arg(repetition.effectiveScore())
        .arg(qualityLabel(repetition.effectiveScore(), repetition.effectiveValid()))
        .arg(reviewTag)
        .arg(issueSummary(repetition.effectiveErrorCodes()))
        .arg(QStringLiteral("%1；身份：%2")
                 .arg(repetition.effectiveFeedback().trimmed().isEmpty() ? QStringLiteral("无") : repetition.effectiveFeedback().trimmed(),
                      repetitionIdentityLabel(repetition)));
}

QString csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString boolText(bool value)
{
    return value ? QStringLiteral("是") : QStringLiteral("否");
}

QString sourceLabel(const ActionRepetition &repetition)
{
    return repetition.source == QStringLiteral("coach") ? QStringLiteral("教练手动") : QStringLiteral("AI");
}

QString sessionSourceTypeLabel(const QString &sourceType)
{
    if (sourceType == QStringLiteral("competition")) {
        return QStringLiteral("比赛");
    }
    if (sourceType == QStringLiteral("offline_import")) {
        return QStringLiteral("导入视频");
    }
    return QStringLiteral("训练");
}

QString sessionSourceDisplay(const SessionHistoryItem &record)
{
    if (record.sourceType == QStringLiteral("competition")) {
        if (!record.raceName.trimmed().isEmpty()) {
            return record.raceName.trimmed();
        }
        if (!record.competitionName.trimmed().isEmpty()) {
            return record.competitionName.trimmed();
        }
        return QStringLiteral("比赛");
    }
    if (record.sourceType == QStringLiteral("offline_import")) {
        if (!record.analysisTaskId.trimmed().isEmpty()) {
            return QStringLiteral("离线任务 %1 · %2")
                .arg(record.analysisTaskId.left(8),
                     displayMediaSource(record.videoSource));
        }
        return displayMediaSource(record.videoSource);
    }
    if (!record.sourceLabel.trimmed().isEmpty()) {
        return record.sourceLabel.trimmed();
    }
    return QStringLiteral("训练");
}

QString reviewStatusLabel(const ActionRepetition &repetition)
{
    if (repetition.source == QStringLiteral("coach")) {
        return QStringLiteral("教练新增");
    }
    if (repetition.hasManualReview()) {
        return QStringLiteral("已复核");
    }
    return QStringLiteral("未复核");
}

QString sanitizedFilePart(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return QStringLiteral("training");
    }
    const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (const QChar ch : invalid) {
        value.replace(ch, QLatin1Char('_'));
    }
    value.replace(QLatin1Char(' '), QLatin1Char('_'));
    return value.left(80);
}

QString withFileSuffix(QString path, const QString &suffix)
{
    QFileInfo info(path);
    if (info.suffix().isEmpty()) {
        path += QStringLiteral(".") + suffix;
    }
    return path;
}

QString htmlParagraph(const QString &text)
{
    const QString escaped = text.trimmed().isEmpty()
                                ? QStringLiteral("未填写")
                                : text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return QStringLiteral("<p>%1</p>").arg(escaped);
}

QStringList repetitionExportHeaders()
{
    return {
        QStringLiteral("session_id"),
        QStringLiteral("repetition_id"),
        QStringLiteral("time"),
        QStringLiteral("athlete"),
        QStringLiteral("identity_status"),
        QStringLiteral("identity_source"),
        QStringLiteral("track_id"),
        QStringLiteral("camera_id"),
        QStringLiteral("frame_time_ms"),
        QStringLiteral("coach"),
        QStringLiteral("competition"),
        QStringLiteral("race_name"),
        QStringLiteral("event_name"),
        QStringLiteral("heat_name"),
        QStringLiteral("group_name"),
        QStringLiteral("bib_number"),
        QStringLiteral("lane_number"),
        QStringLiteral("action"),
        QStringLiteral("video_index"),
        QStringLiteral("video_file_id"),
        QStringLiteral("video_file_status"),
        QStringLiteral("video_file_path"),
        QStringLiteral("clip_start_ms"),
        QStringLiteral("clip_end_ms"),
        QStringLiteral("effective_start_ms"),
        QStringLiteral("effective_end_ms"),
        QStringLiteral("effective_valid"),
        QStringLiteral("effective_score"),
        QStringLiteral("ai_score"),
        QStringLiteral("manual_score"),
        QStringLiteral("review_status"),
        QStringLiteral("source"),
        QStringLiteral("effective_errors"),
        QStringLiteral("effective_feedback"),
        QStringLiteral("coach_note"),
        QStringLiteral("video_source")
    };
}

QStringList repetitionExportRow(const RepetitionSearchItem &item)
{
    return {
        item.sessionId,
        item.id,
        item.time,
        item.athleteName,
        item.identityStatus,
        item.identitySource,
        item.trackId >= 0 ? QString::number(item.trackId) : QString(),
        item.cameraId >= 0 ? QString::number(item.cameraId) : QString(),
        item.frameTimeMs >= 0 ? QString::number(item.frameTimeMs) : QString(),
        item.coachName.isEmpty() ? QStringLiteral("未指定") : item.coachName,
        item.competitionName,
        item.raceName,
        item.eventName,
        item.heatName,
        item.groupName,
        item.bibNumber,
        item.laneNumber,
        QStringLiteral("%1/%2").arg(item.actionCategory, item.actionName),
        QString::number(item.videoIndex),
        item.videoFileId,
        item.videoFileStatus,
        item.videoFilePath,
        QString::number(item.videoClipStartMs),
        QString::number(item.videoClipEndMs),
        QString::number(item.effectiveStartedMsValue),
        QString::number(item.effectiveEndedMsValue),
        boolText(item.effectiveValidValue),
        QString::number(item.effectiveScoreValue),
        QString::number(item.score),
        item.manualScore >= 0 ? QString::number(item.manualScore) : QString(),
        reviewStatusLabel(item),
        sourceLabel(item),
        issueSummary(item.effectiveErrorCodesValue),
        item.effectiveFeedbackValue,
        item.coachNote,
        displayMediaSource(item.videoSource)
    };
}

void configureStableButton(QPushButton *button,
                           int width,
                           int height,
                           const QSize &iconSize)
{
    if (!button) {
        return;
    }

    button->setMinimumSize(width, height);
    button->setMaximumSize(width, height);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setIconSize(iconSize);
    button->setFocusPolicy(Qt::NoFocus);
}

// 清空布局中的子项；保留该工具函数供动态重建列表类界面时复用。
void clearLayout(QLayout *layout)
{
    if (!layout) {
        return;
    }

    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        } else if (QLayout *childLayout = item->layout()) {
            clearLayout(childLayout);
            delete childLayout;
        }

        delete item;
    }
}

// 设置控件的样式角色属性，配合 QSS 中的属性选择器刷新外观。
void setRole(QWidget *widget, const char *role)
{
    if (widget) {
        widget->setProperty("role", role);
    }
}

} // namespace

// 初始化主窗口：装配 UI、视频控件、顶部状态栏、快捷键、右侧动作按钮和默认布局状态。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_trajectoryWidget = new TrajectoryWidget(ui->trajectoryCard);
    if (ui->trajectoryCardLayout) {
        ui->trajectoryCardLayout->replaceWidget(ui->trajectoryViewFrame, m_trajectoryWidget);
        ui->trajectoryViewFrame->hide();
        ui->trajectoryViewFrame->deleteLater();
    }
    m_cameraButtons = {ui->cameraButton01, ui->cameraButton02, ui->cameraButton03, ui->cameraButton04,
                       ui->cameraButton05, ui->cameraButton06, ui->cameraButton07, ui->cameraButton08,
                       ui->cameraButton09, ui->cameraButton10, ui->cameraButton11, ui->cameraButton12};
    for (QLabel *label : {ui->brandTitleLabel, ui->brandSubtitleLabel, ui->sidebarSectionLabel, ui->sidebarToolsLabel, ui->sidebarSystemLabel}) {
        auto *effect = new QGraphicsOpacityEffect(label);
        effect->setOpacity(1.0);
        label->setGraphicsEffect(effect);
    }
    ui->sessionTimeLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm")));
    ui->systemStatusLabel->setText(QStringLiteral("服务 · 检查中"));
    ui->systemStatusLabel->setProperty("state", "muted");
    if (ui->topbarLayout && ui->modelStatusLabel && ui->inspectorContentLayout) {
        ui->topbarLayout->removeWidget(ui->modelStatusLabel);
        ui->inspectorContentLayout->insertWidget(0, ui->modelStatusLabel);
        ui->modelStatusLabel->setVisible(true);
    }
    if (ui->topbarLayout && ui->storageStatusLabel) {
        ui->topbarLayout->removeWidget(ui->storageStatusLabel);
        ui->storageStatusLabel->deleteLater();
    }
    if (ui->inspectorContentLayout) {
        ui->inspectorContentLayout->setAlignment(Qt::AlignTop);
    }
    if (ui->opsCard) {
        for (QLabel *label : ui->opsCard->findChildren<QLabel *>()) {
            label->setWordWrap(true);
            label->setMinimumWidth(0);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        }
    }
    if (ui->inspectorContentLayout && ui->actionValueLabel) {
        m_trainingStateLabel = new QLabel(QStringLiteral("训练状态 · 未开始"), ui->inspectorContent);
        m_trainingStateLabel->setObjectName(QStringLiteral("trainingStateLabel"));
        m_trainingStateLabel->setProperty("role", "status");
        m_trainingStateLabel->setProperty("state", "muted");
        m_trainingStateLabel->setWordWrap(true);
        m_trainingStateLabel->setMinimumWidth(0);
        m_trainingStateLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        const int modelStatusIndex = ui->inspectorContentLayout->indexOf(ui->modelStatusLabel);
        ui->inspectorContentLayout->insertWidget(modelStatusIndex >= 0 ? modelStatusIndex + 1 : 0,
                                                  m_trainingStateLabel);
        m_speedStatusLabel = new QLabel(QStringLiteral("速度 · —"), ui->inspectorContent);
        m_speedStatusLabel->setObjectName(QStringLiteral("speedStatusLabel"));
        m_speedStatusLabel->setProperty("role", "metric");
        m_speedStatusLabel->setProperty("state", "muted");
        m_speedStatusLabel->setWordWrap(true);
        m_speedStatusLabel->setMinimumWidth(0);
        m_speedStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        const int actionValueIndex = ui->inspectorContentLayout->indexOf(ui->actionValueLabel);
        ui->inspectorContentLayout->insertWidget(actionValueIndex >= 0
                                                      ? actionValueIndex + 1
                                                      : 0,
                                                  m_speedStatusLabel);
    }
    if (ui->opsCard && ui->saveTipLabel) {
        auto *opsLayout = qobject_cast<QVBoxLayout *>(ui->opsCard->layout());
        if (opsLayout) {
            const int tipIndex = opsLayout->indexOf(ui->saveTipLabel);
            m_saveTipScrollArea = new QScrollArea(ui->opsCard);
            m_saveTipScrollArea->setObjectName(QStringLiteral("saveTipScrollArea"));
            m_saveTipScrollArea->setFrameShape(QFrame::NoFrame);
            m_saveTipScrollArea->setWidgetResizable(true);
            m_saveTipScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_saveTipScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            m_saveTipScrollArea->setMinimumHeight(56);
            m_saveTipScrollArea->setMaximumHeight(72);
            auto *tipContent = new QWidget(m_saveTipScrollArea);
            auto *tipLayout = new QVBoxLayout(tipContent);
            tipLayout->setContentsMargins(8, 6, 8, 6);
            tipLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
            tipLayout->setAlignment(Qt::AlignTop);
            ui->saveTipLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            ui->saveTipLabel->setTextFormat(Qt::PlainText);
            tipLayout->addWidget(ui->saveTipLabel);
            m_saveTipScrollArea->setWidget(tipContent);
            if (tipIndex >= 0) {
                opsLayout->removeWidget(ui->saveTipLabel);
                opsLayout->insertWidget(tipIndex, m_saveTipScrollArea);
            } else {
                opsLayout->addWidget(m_saveTipScrollArea);
            }
            m_saveTipScrollArea->setVisible(!ui->saveTipLabel->isHidden());
            ui->saveTipLabel->installEventFilter(this);
        }
    }

    m_trainingRepository = std::make_unique<TrainingRepository>();
    m_athleteAnalysisManager = std::make_unique<AthleteAnalysisManager>(this);
    m_athleteAnalysisManager->setResultCallback([this](const AthleteFrameResult &athleteFrame) {
            const bool selectedFrame = athleteFrame.cameraId == m_selectedCamera
                                       || (m_selectedCamera == 0 && athleteFrame.cameraId == 0);
        if (selectedFrame) {
            m_lastAthleteFrame = athleteFrame;
            if (athleteFrame.instances.isEmpty()) {
                clearRealtimeAnalysisFrame();
            } else {
                m_lastSelectedFrameReceivedAtMsec = QDateTime::currentMSecsSinceEpoch();
                ui->mainImageLabel->setAthleteFrame(athleteFrame);
                refreshStats();
            }
        }
        recordAthleteFrames(athleteFrame);
    });
    m_athleteAnalysisManager->setStatusCallback([this](const QString &statusText) {
        refreshModelStatus(statusText);
    });
    m_analysisOverlayTimer.setInterval(33);
    connect(&m_analysisOverlayTimer, &QTimer::timeout, this, [this]() { refreshOfflineAnalysisOverlay(); });
    ui->trajectoryCard->show();

    // 在“隐藏侧栏”按钮旁边动态增加全屏按钮，避免修改 .ui 后生成头文件不同步。
    m_fullScreenButton = new AnimatedButton(ui->toggleSidebarButton->parentWidget());
    m_fullScreenButton->setObjectName(QStringLiteral("fullScreenButton"));
    m_fullScreenButton->setProperty("role", "icon");
    configureStableButton(ui->toggleSidebarButton, 36, 36, QSize(20, 20));
    configureStableButton(m_fullScreenButton, 36, 36, QSize(20, 20));
    ui->toggleSidebarButton->installEventFilter(this);
    m_fullScreenButton->installEventFilter(this);
    refreshSidebarButton();
    refreshFullScreenButton();
    const int sidebarButtonIndex = ui->topbarLayout->indexOf(ui->toggleSidebarButton);
    if (sidebarButtonIndex >= 0) {
        ui->topbarLayout->insertWidget(sidebarButtonIndex + 1, m_fullScreenButton);
    } else {
        ui->topbarLayout->addWidget(m_fullScreenButton);
    }

    auto *importVideoButton = new AnimatedButton(ui->leftCard);
    importVideoButton->setIconSource(QStringLiteral(":/icons/video.svg"));
    m_importVideoButton = importVideoButton;
    m_importVideoButton->setObjectName(QStringLiteral("importOfflineVideoButton"));
    m_importVideoButton->setProperty("role", "secondary");
    m_importVideoButton->setText(QStringLiteral("导入视频"));
    m_importVideoButton->setIconSize(QSize(18, 18));
    m_importVideoButton->setToolTip(QStringLiteral("导入本地视频用于运动员检测、身份识别和训练复盘"));
    m_importVideoButton->setStatusTip(m_importVideoButton->toolTip());
    m_importVideoButton->setAccessibleName(QStringLiteral("导入离线视频"));
    configureStableButton(m_importVideoButton, 112, 38, QSize(18, 18));
    ui->topbarLayout->insertWidget(std::max(0, ui->topbarLayout->indexOf(ui->systemStatusLabel)),
                                   m_importVideoButton, 0, Qt::AlignVCenter);
    connect(m_importVideoButton, &QPushButton::clicked, this, [this]() { importOfflineVideo(); });

    m_fullRateAnalysisButton = new AnimatedButton(ui->leftCard);
    m_fullRateAnalysisButton->setObjectName(QStringLiteral("fullRateAnalysisButton"));
    m_fullRateAnalysisButton->setProperty("role", "secondary");
    m_fullRateAnalysisButton->setText(QStringLiteral("完整分析"));
    m_fullRateAnalysisButton->setToolTip(QStringLiteral("创建和管理 12 路完整帧率离线分析任务"));
    m_fullRateAnalysisButton->setAccessibleName(QStringLiteral("12 路完整帧率离线分析"));
    configureStableButton(m_fullRateAnalysisButton, 112, 38, QSize(18, 18));
    ui->topbarLayout->insertWidget(std::max(0, ui->topbarLayout->indexOf(m_importVideoButton)),
                                   m_fullRateAnalysisButton, 0, Qt::AlignVCenter);
    connect(m_fullRateAnalysisButton, &QPushButton::clicked, this, [this]() { openOfflineAnalysisManager(); });

    m_taskCenterButton = new AnimatedButton(ui->leftCard);
    m_taskCenterButton->setObjectName(QStringLiteral("analysisTaskCenterButton"));
    m_taskCenterButton->setProperty("role", "secondary");
    m_taskCenterButton->setText(QStringLiteral("任务中心"));
    m_taskCenterButton->setToolTip(QStringLiteral("查看、暂停、继续或取消后台分析任务"));
    configureStableButton(m_taskCenterButton, 112, 38, QSize(18, 18));
    ui->topbarLayout->insertWidget(std::max(0, ui->topbarLayout->indexOf(m_fullRateAnalysisButton)),
                                   m_taskCenterButton, 0, Qt::AlignVCenter);
    connect(m_taskCenterButton, &QPushButton::clicked, this, [this]() { openAnalysisTaskCenter(); });

    m_environmentCheckButton = new AnimatedButton(this);
    m_environmentCheckButton->setObjectName(QStringLiteral("environmentCheckButton"));
    m_environmentCheckButton->setProperty("variant", "primary");
    m_environmentCheckButton->setText(QStringLiteral("执行检查"));
    configureStableButton(m_environmentCheckButton, 144, 38, QSize(18, 18));
    m_environmentCheckButton->hide();
    m_environmentCheckLabel = new QLabel(this);
    m_environmentCheckLabel->setProperty("role", "muted");
    m_environmentCheckLabel->hide();
    const int checkButtonIndex = ui->topbarLayout->indexOf(m_taskCenterButton);
    ui->topbarLayout->insertWidget(checkButtonIndex, m_environmentCheckLabel);
    ui->topbarLayout->insertWidget(checkButtonIndex + 1, m_environmentCheckButton);
    connect(m_environmentCheckButton, &QPushButton::clicked, this, &MainWindow::startEnvironmentCheck);

    ui->mainImageLabel->setPlaceholderText(QStringLiteral("主视频\n未播放"));
    ui->mainImageLabel->setOverlayControlsVisible(false);
    ui->mainImageLabel->setStreamChangedHandler([this](VideoOpenGLWidget *) {
        syncAnalysisStreams();
    });
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        const QString cameraName = defaultCameraChannelName(i);
        cameraWidget->setSourceTile(true);
        cameraWidget->setChannelName(cameraName);
        cameraWidget->setPlaceholderText(cameraName);
        cameraWidget->setOverlayControlsVisible(false);
        cameraWidget->setConfigButtonVisible(false);
        cameraWidget->setClickHandler([this, i](VideoOpenGLWidget *) {
            selectCamera(i + 1);
        });
        cameraWidget->setStreamChangedHandler([this](VideoOpenGLWidget *) {
            syncAnalysisStreams();
        });
    }
    loadCameraSettings();
    initializeTrainingRepository();
    m_analysisTaskManager = std::make_unique<AnalysisTaskManager>(this);
    connect(m_analysisTaskManager.get(), &AnalysisTaskManager::offlineImportReady, this,
            [this](const OfflineAnalysisTask &task, const OfflineVideoProbeResult &probe) {
        m_offlineVideoPath = task.videoPath;
        m_offlineVideoName = offlineVideoDisplayName(task.videoPath);
        m_offlineVideoProbe = probe;
        m_offlineAnalysisTask = task;
        for (auto *videoWidget : m_cameraButtons) {
            if (videoWidget && videoWidget->isPlaying()) videoWidget->stopPlayback();
        }
        showOfflineVideoInMainView(true);
        ui->saveTipLabel->setText(QStringLiteral("离线视频已准备完成：%1。任务 %2 已入库。")
                                      .arg(QDir::toNativeSeparators(task.videoPath), task.id.left(8)));
        ui->saveTipLabel->show();
    }, Qt::QueuedConnection);
    connect(m_analysisTaskManager.get(), &AnalysisTaskManager::taskError, this,
            [this](const QString &, const QString &message) {
        if (!message.isEmpty()) ui->saveTipLabel->setText(QStringLiteral("后台任务失败：%1").arg(message));
        refreshAnalysisTaskStatus();
    }, Qt::QueuedConnection);
    connect(m_analysisTaskManager.get(), &AnalysisTaskManager::taskUpdated, this,
            [this](const QString &) { refreshAnalysisTaskStatus(); }, Qt::QueuedConnection);
    m_analysisTaskManager->restorePendingTasks();

    applyStyleSheet();
    installTrainingContextPanel();
    installHistorySearchPanel();
    rebuildWorkspaceLayout();

    auto *fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullScreenShortcut->setContext(Qt::WindowShortcut);
    connect(fullScreenShortcut, &QShortcut::activated, this, [this]() { toggleFullScreen(); });

    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WindowShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() { exitFullScreenMode(); });

    connect(ui->startCaptureButton, &QPushButton::clicked, this, [this]() { startCapture(); });
    connect(ui->pauseCaptureButton, &QPushButton::clicked, this, [this]() { pauseCapture(); });
    connect(ui->stopCaptureButton, &QPushButton::clicked, this, [this]() { stopCapture(); });
    connect(ui->saveRecordButton, &QPushButton::clicked, this, [this]() { saveRecord(); });
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
    m_statusTimer.setInterval(1000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this]() { refreshStats(); });
    m_statusTimer.start();

    setupUiState();
    setupConnections();
    refreshStats();
}

// 释放由 Qt Designer 生成的界面对象。
MainWindow::~MainWindow()
{
    if (m_environmentCheckThread) {
        disconnect(m_environmentCheckThread, nullptr, this, nullptr);
        m_environmentCheckThread->requestInterruption();
        m_environmentCheckThread->quit();
        m_environmentCheckThread->wait();
        delete m_environmentCheckThread;
        m_environmentCheckThread = nullptr;
    }
    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->stop();
        m_athleteAnalysisManager.reset();
    }
    saveCameraSettings();
    delete ui;
}

// 监听图标按钮状态，保持固定尺寸并将辅助提示交给工具提示。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->saveTipLabel
        && m_saveTipScrollArea
        && (event->type() == QEvent::Show || event->type() == QEvent::ShowToParent
            || event->type() == QEvent::Hide || event->type() == QEvent::HideToParent)) {
        const bool explicitShow = event->type() == QEvent::Show || event->type() == QEvent::ShowToParent;
        const bool explicitHide = event->type() == QEvent::Hide || event->type() == QEvent::HideToParent;
        if (explicitShow) {
            m_saveTipScrollArea->setVisible(true);
        } else if (explicitHide && ui->saveTipLabel->isHidden()) {
            m_saveTipScrollArea->setVisible(false);
        }
    } else if (watched == ui->toggleSidebarButton
        && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshSidebarButton();
    } else if (watched == m_fullScreenButton
               && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshFullScreenButton();
    } else if ((event->type() == QEvent::Enter || event->type() == QEvent::Leave)
               && watched->property("showTipTextOnHover").toBool()
               && qobject_cast<QPushButton *>(watched)) {
        repolish(qobject_cast<QWidget *>(watched));
    } else if ((watched == m_cameraGridContainer || watched == m_cameraTrajectoryRow)
               && event->type() == QEvent::Resize) {
        updateCameraGrid();
    }

    return QMainWindow::eventFilter(watched, event);
}

// 监听窗口状态变化：程序启动全屏、F11 切换或 Esc 退出后，同步刷新右上角全屏按钮。
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        refreshFullScreenButton();
    }
}

// 初始化运行时 UI 状态；当前界面主要在构造函数中完成初始化，保留该入口便于后续扩展。
void MainWindow::setupUiState()
{
    m_navButtons = {ui->navCaptureButton, ui->navHistoryButton, ui->navSuggestionButton, ui->navSystemStatusButton};
    const QVector<QString> navigationLabels = {
        QStringLiteral("实时训练"),
        QStringLiteral("历史复盘"),
        QStringLiteral("训练洞察"),
        QStringLiteral("系统状态")
    };
    for (int index = 0; index < m_navButtons.size(); ++index) {
        auto *button = m_navButtons.at(index);
        setRole(button, "nav");
        button->setFocusPolicy(Qt::StrongFocus);
        button->setToolTip(navigationLabels.at(index));
        button->setStatusTip(navigationLabels.at(index));
        button->setAccessibleName(navigationLabels.at(index));
    }
    const QVector<QPair<QPushButton *, QString>> navigationIcons = {
        {ui->navCaptureButton, QStringLiteral(":/icons/live.svg")},
        {ui->navHistoryButton, QStringLiteral(":/icons/history.svg")},
        {ui->navSuggestionButton, QStringLiteral(":/icons/suggestion.svg")},
        {ui->navSystemStatusButton, QStringLiteral(":/icons/system-status.svg")},
        {ui->personManagementButton, QStringLiteral(":/icons/person.svg")},
        {ui->competitionManagementButton, QStringLiteral(":/icons/competition.svg")},
        {ui->settingsButton, QStringLiteral(":/icons/settings.svg")}
    };
    for (const auto &[button, source] : navigationIcons) {
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        if (auto *animatedButton = qobject_cast<AnimatedButton *>(button)) {
            animatedButton->setIconSource(source);
        }
    }
    const QVector<QPair<QPushButton *, QString>> utilityLabels = {
        {ui->personManagementButton, QStringLiteral("人员管理")},
        {ui->competitionManagementButton, QStringLiteral("比赛管理")},
        {ui->settingsButton, QStringLiteral("系统设置")}
    };
    for (const auto &[button, label] : utilityLabels) {
        if (!button) {
            continue;
        }
        button->setFocusPolicy(Qt::StrongFocus);
        button->setToolTip(label);
        button->setStatusTip(label);
        button->setAccessibleName(label);
    }

    m_summaryValues = {
        ui->summarySessionsValue,
        ui->summaryActionsValue,
        ui->summaryAvgValue,
        ui->summaryBestValue
    };

    if (ui->precisionComboBox && ui->precisionComboBox->count() == 0) {
        ui->precisionComboBox->addItem(precisionLabel(QStringLiteral("fast")), QStringLiteral("fast"));
        ui->precisionComboBox->addItem(precisionLabel(QStringLiteral("balanced")), QStringLiteral("balanced"));
        ui->precisionComboBox->addItem(precisionLabel(QStringLiteral("high")), QStringLiteral("high"));
    }

    if (ui->fpsComboBox && ui->fpsComboBox->count() == 0) {
        for (int fps : {5, 8, 10, 15, 25, 30, 50, 60, 90, 120}) {
            ui->fpsComboBox->addItem(QStringLiteral("%1 FPS").arg(fps), fps);
        }
    }
    for (QLabel *label : {ui->saveTipLabel}) {
        if (label) {
            label->setWordWrap(true);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        }
    }
    applyCapturePreferencesToUi();
    reloadTrainingContext();
    ui->settingsBox->setMaximumHeight(QWIDGETSIZE_MAX);
    if (m_trainingRepository && !m_trainingRepository->isOpen() && !m_trainingRepository->lastError().isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("训练数据库初始化失败：%1").arg(m_trainingRepository->lastError()));
        ui->saveTipLabel->show();
    }

    loadTrainingRecords();
    const bool hasDatabaseError = m_trainingRepository && !m_trainingRepository->isOpen() && !m_trainingRepository->lastError().isEmpty();
    if (!m_lastSavedAt.isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("最近保存：%1").arg(m_lastSavedAt));
        ui->saveTipLabel->show();
    } else if (!hasDatabaseError) {
        ui->saveTipLabel->clear();
        ui->saveTipLabel->hide();
    }

    m_activePage = kCapturePage;
    ui->pages->setCurrentIndex(kCapturePage);
    refreshNavButtons();
    refreshCameraButtons();
    refreshHistory();
    refreshSuggestions();
}

// 绑定页面上的交互信号：侧栏开关、全屏切换和实时训练工具。
void MainWindow::setupConnections()
{
    connect(ui->navCaptureButton, &QPushButton::clicked, this, [this]() {
        switchPage(kCapturePage);
    });
    connect(ui->navHistoryButton, &QPushButton::clicked, this, [this]() {
        switchPage(kHistoryPage);
    });
    connect(ui->navSuggestionButton, &QPushButton::clicked, this, [this]() {
        switchPage(kSuggestionPage);
    });
    connect(ui->suggestionHistoryButton, &QPushButton::clicked, this, [this]() {
        switchPage(kHistoryPage);
    });
    connect(ui->suggestionAthleteComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        refreshSuggestions();
    });
    connect(ui->suggestionActionComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        refreshSuggestions();
    });
    connect(ui->navSystemStatusButton, &QPushButton::clicked, this, [this]() {
        switchPage(kSystemStatusPage);
    });
    connect(ui->toggleSidebarButton, &QPushButton::clicked, this, [this]() {
        toggleSidebar();
    });
    connect(ui->personManagementButton, &QPushButton::clicked, this, [this]() {
        openPersonManagement();
    });
    connect(ui->competitionManagementButton, &QPushButton::clicked, this, [this]() {
        openCompetitionManagement();
    });
    connect(ui->settingsButton, &QPushButton::clicked, this, [this]() {
        openSystemSettings();
    });
    connect(ui->manualIdentityButton, &QPushButton::clicked, this, [this]() {
        editManualIdentityBindings();
    });
    connect(ui->advancedSettingsButton, &QPushButton::clicked, this, [this]() {
        m_settingsExpanded ? closeTrainingSettings() : openTrainingSettings();
    });
    if (m_fullScreenButton) {
        connect(m_fullScreenButton, &QPushButton::clicked, this, [this]() {
            toggleFullScreen();
        });
    }
}

void MainWindow::installTrainingContextPanel()
{
    if (m_trainingContextPanel || !ui->settingsBox || !ui->settingsLayout) {
        return;
    }

    m_trainingContextPanel = new QFrame(ui->settingsBox);
    m_trainingContextPanel->setObjectName(QStringLiteral("trainingContextPanel"));
    setRole(m_trainingContextPanel, "trainingContextPanel");
    auto *panelLayout = new QVBoxLayout(m_trainingContextPanel);
    panelLayout->setContentsMargins(0, 8, 0, 0);
    panelLayout->setSpacing(8);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    auto *titleLabel = new QLabel(QStringLiteral("训练上下文"), m_trainingContextPanel);
    titleLabel->setProperty("role", "sectionTitle");
    auto *editStandardButton = new QPushButton(QStringLiteral("编辑标准"), m_trainingContextPanel);
    editStandardButton->setProperty("role", "secondaryButton");
    m_standardDetailLabel = new QLabel(QStringLiteral("动作标准仅用于保存训练上下文"), m_trainingContextPanel);
    m_standardDetailLabel->setProperty("role", "muted");
    m_standardDetailLabel->setWordWrap(true);
    m_standardDetailLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleRow->addWidget(titleLabel, 0);
    titleRow->addStretch(1);
    titleRow->addWidget(editStandardButton, 0);
    panelLayout->addLayout(titleRow);
    m_standardDetailLabel->setToolTip(QStringLiteral("当前训练只生成运动员检测、身份识别以及满足条件时的轨迹与速度数据。"));
    panelLayout->addWidget(m_standardDetailLabel);

    m_athleteComboBox = ui->athleteComboBox;
    m_coachComboBox = new QComboBox(m_trainingContextPanel);
    m_competitionComboBox = new QComboBox(m_trainingContextPanel);
    m_competitionEventComboBox = new QComboBox(m_trainingContextPanel);
    m_actionStandardComboBox = new QComboBox(m_trainingContextPanel);
    m_siteLineEdit = new QLineEdit(m_trainingContextPanel);
    m_trainingPhaseComboBox = new QComboBox(m_trainingContextPanel);
    m_goalLineEdit = new QLineEdit(m_trainingContextPanel);
    m_targetRepsSpinBox = new QSpinBox(m_trainingContextPanel);
    m_targetScoreSpinBox = new QSpinBox(m_trainingContextPanel);
    m_setCountSpinBox = new QSpinBox(m_trainingContextPanel);
    m_restSecondsSpinBox = new QSpinBox(m_trainingContextPanel);
    m_trainingNotesEdit = new QPlainTextEdit(m_trainingContextPanel);

    for (QComboBox *combo : {m_coachComboBox,
                             m_competitionComboBox,
                             m_competitionEventComboBox,
                             m_actionStandardComboBox,
                             m_trainingPhaseComboBox}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
    }

    m_siteLineEdit->setPlaceholderText(QStringLiteral("训练场地"));
    m_goalLineEdit->setPlaceholderText(QStringLiteral("本次训练目标"));
    m_trainingNotesEdit->setPlaceholderText(QStringLiteral("训练备注：主观感受、疲劳程度、冰面情况、训练重点"));
    m_trainingNotesEdit->setMinimumHeight(58);
    m_trainingNotesEdit->setMaximumHeight(82);
    m_trainingPhaseComboBox->addItems({QStringLiteral("热身"), QStringLiteral("基础训练"), QStringLiteral("专项训练"), QStringLiteral("复盘测试")});
    m_targetRepsSpinBox->setRange(1, 999);
    m_targetScoreSpinBox->setRange(1, 100);
    m_setCountSpinBox->setRange(1, 20);
    m_restSecondsSpinBox->setRange(0, 600);
    m_restSecondsSpinBox->setSingleStep(15);

    auto *addAthleteButton = new QPushButton(QStringLiteral("新增运动员"), m_trainingContextPanel);
    auto *addCoachButton = new QPushButton(QStringLiteral("新增教练"), m_trainingContextPanel);
    addAthleteButton->setProperty("role", "secondaryButton");
    addCoachButton->setProperty("role", "secondaryButton");

    auto *athleteActions = new QHBoxLayout();
    athleteActions->setContentsMargins(0, 0, 0, 0);
    athleteActions->setSpacing(8);
    auto *athleteHint = new QLabel(QStringLiteral("运动员选择会与实时检查器同步"), m_trainingContextPanel);
    athleteHint->setProperty("role", "muted");
    athleteActions->addWidget(athleteHint, 1);
    athleteActions->addWidget(addAthleteButton);
    panelLayout->addLayout(athleteActions);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);

    m_drawerAthleteComboBox = new QComboBox(m_trainingContextPanel);
    m_drawerAthleteComboBox->setModel(m_athleteComboBox->model());
    m_drawerAthleteComboBox->setCurrentIndex(m_athleteComboBox->currentIndex());
    m_drawerAthleteComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_drawerAthleteComboBox->setMinimumContentsLength(12);
    form->addRow(QStringLiteral("运动员"), m_drawerAthleteComboBox);

    auto *coachRow = new QWidget(m_trainingContextPanel);
    auto *coachLayout = new QHBoxLayout(coachRow);
    coachLayout->setContentsMargins(0, 0, 0, 0);
    coachLayout->setSpacing(6);
    coachLayout->addWidget(m_coachComboBox, 1);
    coachLayout->addWidget(addCoachButton);
    form->addRow(QStringLiteral("教练"), coachRow);

    for (int i = 0; i < 3; ++i) {
        auto *combo = new QComboBox(m_trainingContextPanel);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
        m_participantComboBoxes.append(combo);
        form->addRow(QStringLiteral("参与%1").arg(i + 2), combo);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            refreshTrainingContextDetails();
            reloadAthleteIdentityGallery();
        });
    }

    m_trainingTargetLabel = new QLabel(QStringLiteral("目标完成度：0/0"), m_trainingContextPanel);
    m_trainingTargetLabel->setProperty("role", "muted");
    m_trainingTargetLabel->setWordWrap(true);
    m_trainingTargetLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    form->addRow(QStringLiteral("比赛"), m_competitionComboBox);
    form->addRow(QStringLiteral("场次"), m_competitionEventComboBox);
    form->addRow(QStringLiteral("场地"), m_siteLineEdit);
    form->addRow(QStringLiteral("动作标准"), m_actionStandardComboBox);
    form->addRow(QStringLiteral("训练阶段"), m_trainingPhaseComboBox);
    form->addRow(QStringLiteral("本次目标"), m_goalLineEdit);
    auto *legacyMetadataLabel = new QLabel(QStringLiteral("历史兼容元数据"), m_trainingContextPanel);
    legacyMetadataLabel->setProperty("role", "sectionTitle");
    form->addRow(legacyMetadataLabel);
    form->addRow(QStringLiteral("目标次数"), m_targetRepsSpinBox);
    form->addRow(QStringLiteral("目标评分"), m_targetScoreSpinBox);
    form->addRow(QStringLiteral("组数"), m_setCountSpinBox);
    form->addRow(QStringLiteral("休息秒数"), m_restSecondsSpinBox);
    form->addRow(QStringLiteral("记录提示"), m_trainingTargetLabel);

    panelLayout->addLayout(form);
    panelLayout->addWidget(m_trainingNotesEdit);
    ui->settingsLayout->addWidget(m_trainingContextPanel);

    connect(addAthleteButton, &QPushButton::clicked, this, [this]() { addAthleteFromDialog(); });
    connect(addCoachButton, &QPushButton::clicked, this, [this]() { addCoachFromDialog(); });
    connect(editStandardButton, &QPushButton::clicked, this, [this]() { editActionStandard(); });
    connect(m_actionStandardComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshTrainingContextDetails();
    });
    connect(m_athleteComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (m_drawerAthleteComboBox) {
            const QSignalBlocker blocker(m_drawerAthleteComboBox);
            m_drawerAthleteComboBox->setCurrentIndex(m_athleteComboBox->currentIndex());
        }
        refreshTrainingContextDetails();
        reloadAthleteIdentityGallery();
    });
    connect(m_drawerAthleteComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_athleteComboBox || m_athleteComboBox->currentIndex() == index) {
            return;
        }
        m_athleteComboBox->setCurrentIndex(index);
    });
    connect(m_competitionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        reloadTrainingContext();
    });
    connect(m_competitionEventComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshTrainingContextDetails();
    });
    connect(m_targetRepsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshStats(); });
    connect(m_targetScoreSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshStats(); });
}

void MainWindow::installHistorySearchPanel()
{
    if (m_historySearchPanel || !ui->historyPageLayout) {
        return;
    }

    m_historySearchPanel = new QFrame(ui->historyPage);
    m_historySearchPanel->setObjectName(QStringLiteral("historySearchPanel"));
    auto *panelLayout = new QVBoxLayout(m_historySearchPanel);
    panelLayout->setContentsMargins(12, 10, 12, 10);
    panelLayout->setSpacing(8);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    auto *titleLabel = new QLabel(QStringLiteral("历史复盘"), m_historySearchPanel);
    titleLabel->setProperty("role", "sectionTitle");
    titleRow->addWidget(titleLabel, 0);
    titleRow->addStretch(1);
    panelLayout->addLayout(titleRow);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    m_historyAthleteComboBox = new QComboBox(m_historySearchPanel);
    m_historyCoachComboBox = new QComboBox(m_historySearchPanel);
    m_historyActionComboBox = new QComboBox(m_historySearchPanel);
    m_historyCompetitionComboBox = new QComboBox(m_historySearchPanel);
    m_historyCompetitionEventComboBox = new QComboBox(m_historySearchPanel);
    m_historySourceTypeComboBox = new QComboBox(m_historySearchPanel);
    m_historySortComboBox = new QComboBox(m_historySearchPanel);
    m_historyCompetitionLineEdit = new QLineEdit(m_historySearchPanel);
    m_historyMinScoreSpinBox = new QSpinBox(m_historySearchPanel);
    m_historyMaxScoreSpinBox = new QSpinBox(m_historySearchPanel);
    m_historyFromCheckBox = new QCheckBox(QStringLiteral("开始"), m_historySearchPanel);
    m_historyToCheckBox = new QCheckBox(QStringLiteral("结束"), m_historySearchPanel);
    m_historyFromDateEdit = new QDateEdit(QDate::currentDate().addMonths(-1), m_historySearchPanel);
    m_historyToDateEdit = new QDateEdit(QDate::currentDate(), m_historySearchPanel);
    for (QComboBox *combo : {m_historyAthleteComboBox,
                             m_historyCoachComboBox,
                             m_historyActionComboBox,
                             m_historyCompetitionComboBox,
                             m_historyCompetitionEventComboBox,
                             m_historySourceTypeComboBox,
                             m_historySortComboBox}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
    }

    m_historyCompetitionLineEdit->setPlaceholderText(QStringLiteral("比赛/场次/分组/场地/阶段/目标/备注关键词"));
    for (QDateEdit *dateEdit : {m_historyFromDateEdit, m_historyToDateEdit}) {
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        dateEdit->setEnabled(false);
    }
    for (QSpinBox *scoreSpinBox : {m_historyMinScoreSpinBox, m_historyMaxScoreSpinBox}) {
        scoreSpinBox->setRange(-1, 100);
        scoreSpinBox->setSpecialValueText(QStringLiteral("不限"));
        scoreSpinBox->setValue(-1);
    }

    m_historySortComboBox->addItem(QStringLiteral("保存时间 · 新到旧"), 0);
    m_historySortComboBox->addItem(QStringLiteral("保存时间 · 旧到新"), 1);
    m_historySortComboBox->addItem(QStringLiteral("平均分 · 高到低"), 2);
    m_historySortComboBox->addItem(QStringLiteral("平均分 · 低到高"), 3);
    m_historySortComboBox->addItem(QStringLiteral("最佳分 · 高到低"), 4);
    m_historySortComboBox->addItem(QStringLiteral("有效动作 · 多到少"), 5);
    m_historySortComboBox->addItem(QStringLiteral("训练时长 · 长到短"), 6);
    m_historySourceTypeComboBox->addItem(QStringLiteral("全部来源"), QString());
    m_historySourceTypeComboBox->addItem(QStringLiteral("训练"), QStringLiteral("training"));
    m_historySourceTypeComboBox->addItem(QStringLiteral("比赛"), QStringLiteral("competition"));
    m_historySourceTypeComboBox->addItem(QStringLiteral("导入视频"), QStringLiteral("offline_import"));

    auto *searchButton = new QPushButton(QStringLiteral("查询"), m_historySearchPanel);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), m_historySearchPanel);
    auto *repetitionSearchButton = new QPushButton(QStringLiteral("动作检索"), m_historySearchPanel);
    auto *manageCompetitionsButton = new QPushButton(QStringLiteral("比赛管理"), m_historySearchPanel);
    auto *reportCenterButton = new QPushButton(QStringLiteral("报告中心"), m_historySearchPanel);
    m_historyPreviousPageButton = new QPushButton(QStringLiteral("上一页"), m_historySearchPanel);
    m_historyNextPageButton = new QPushButton(QStringLiteral("下一页"), m_historySearchPanel);
    for (QPushButton *button : {searchButton, resetButton, repetitionSearchButton, manageCompetitionsButton, reportCenterButton, m_historyPreviousPageButton, m_historyNextPageButton}) {
        button->setProperty("role", "secondaryButton");
        configureStableButton(button, kHistoryActionButtonWidth, 32, QSize(0, 0));
    }
    titleRow->addWidget(repetitionSearchButton);
    titleRow->addWidget(manageCompetitionsButton);
    titleRow->addWidget(reportCenterButton);
    auto *subtitleLabel = new QLabel(QStringLiteral("查看、筛选和复盘训练记录"), m_historySearchPanel);
    subtitleLabel->setObjectName(QStringLiteral("historySubtitleLabel"));
    subtitleLabel->setProperty("role", "muted");
    panelLayout->addWidget(subtitleLabel);

    auto *dateRange = new QWidget(m_historySearchPanel);
    auto *dateLayout = new QVBoxLayout(dateRange);
    dateLayout->setContentsMargins(0, 0, 0, 0);
    dateLayout->setSpacing(4);
    for (const auto &dateRow : {qMakePair(m_historyFromCheckBox, m_historyFromDateEdit),
                                qMakePair(m_historyToCheckBox, m_historyToDateEdit)}) {
        auto *row = new QWidget(dateRange);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);
        rowLayout->addWidget(dateRow.first);
        rowLayout->addWidget(dateRow.second, 1);
        dateLayout->addWidget(row);
    }

    auto addFilterCell = [this, grid](const QString &labelText, QWidget *field, int row, int column) {
        auto *cell = new QWidget(m_historySearchPanel);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(4);
        auto *label = new QLabel(labelText, cell);
        label->setProperty("role", "muted");
        cellLayout->addWidget(label);
        cellLayout->addWidget(field);
        grid->addWidget(cell, row, column);
        grid->setColumnStretch(column, 1);
    };
    addFilterCell(QStringLiteral("运动员"), m_historyAthleteComboBox, 0, 0);
    addFilterCell(QStringLiteral("来源"), m_historySourceTypeComboBox, 0, 1);
    addFilterCell(QStringLiteral("比赛"), m_historyCompetitionComboBox, 0, 2);
    addFilterCell(QStringLiteral("日期"), dateRange, 0, 3);
    panelLayout->addLayout(grid);

    auto *moreFiltersButton = new QPushButton(QStringLiteral("更多筛选"), m_historySearchPanel);
    moreFiltersButton->setProperty("variant", "subtle");
    auto *moreFilters = new QFrame(m_historySearchPanel);
    moreFilters->setObjectName(QStringLiteral("historyMoreFilters"));
    auto *moreGrid = new QGridLayout(moreFilters);
    moreGrid->setContentsMargins(0, 0, 0, 0);
    moreGrid->setHorizontalSpacing(8);
    moreGrid->setVerticalSpacing(6);
    moreGrid->addWidget(new QLabel(QStringLiteral("教练"), moreFilters), 0, 0);
    moreGrid->addWidget(m_historyCoachComboBox, 0, 1);
    moreGrid->addWidget(new QLabel(QStringLiteral("动作"), moreFilters), 0, 2);
    moreGrid->addWidget(m_historyActionComboBox, 0, 3);
    moreGrid->addWidget(new QLabel(QStringLiteral("比赛场次"), moreFilters), 1, 0);
    moreGrid->addWidget(m_historyCompetitionEventComboBox, 1, 1);
    moreGrid->addWidget(new QLabel(QStringLiteral("比赛文字"), moreFilters), 1, 2);
    moreGrid->addWidget(m_historyCompetitionLineEdit, 1, 3);
    moreGrid->addWidget(new QLabel(QStringLiteral("最低分"), moreFilters), 2, 0);
    moreGrid->addWidget(m_historyMinScoreSpinBox, 2, 1);
    moreGrid->addWidget(new QLabel(QStringLiteral("最高分"), moreFilters), 2, 2);
    moreGrid->addWidget(m_historyMaxScoreSpinBox, 2, 3);
    moreGrid->setColumnStretch(1, 1);
    moreGrid->setColumnStretch(3, 1);
    moreFilters->hide();
    connect(moreFiltersButton, &QPushButton::clicked, moreFilters, [moreFilters, moreFiltersButton]() {
        const bool expanded = !moreFilters->isVisible();
        moreFilters->setVisible(expanded);
        moreFiltersButton->setText(expanded ? QStringLiteral("收起筛选") : QStringLiteral("更多筛选"));
    });
    panelLayout->addWidget(moreFiltersButton, 0, Qt::AlignLeft);
    panelLayout->addWidget(moreFilters);

    auto *actionsWidget = new QWidget(m_historySearchPanel);
    auto *actions = new QHBoxLayout(actionsWidget);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    actions->addWidget(resetButton);
    searchButton->setProperty("variant", "primary");
    actions->addWidget(searchButton);
    grid->addWidget(actionsWidget, 0, 4, Qt::AlignBottom);
    ui->historyPageLayout->insertWidget(1, m_historySearchPanel);

    ui->historyScrollArea->hide();

    m_historyWorkspace = new QSplitter(Qt::Horizontal, ui->historyPage);
    m_historyWorkspace->setObjectName(QStringLiteral("historyWorkspace"));
    m_historyWorkspace->setChildrenCollapsible(false);

    auto *listPanel = new QFrame(m_historyWorkspace);
    listPanel->setObjectName(QStringLiteral("historyListPanel"));
    auto *listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(12, 12, 12, 12);
    listLayout->setSpacing(10);
    auto *listHeader = new QHBoxLayout();
    auto *listTitle = new QLabel(QStringLiteral("训练记录"), listPanel);
    listTitle->setProperty("role", "sectionTitle");
    listHeader->addWidget(listTitle);
    listHeader->addStretch(1);
    listHeader->addWidget(new QLabel(QStringLiteral("排序"), listPanel));
    listHeader->addWidget(m_historySortComboBox);
    listLayout->addLayout(listHeader);

    m_historySessionList = new QListWidget(listPanel);
    m_historySessionList->setObjectName(QStringLiteral("historySessionList"));
    m_historySessionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historySessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historySessionList->setTextElideMode(Qt::ElideRight);
    m_historySessionList->setUniformItemSizes(true);
    m_historySessionList->setSpacing(4);
    listLayout->addWidget(m_historySessionList, 1);

    auto *pager = new QWidget(listPanel);
    pager->setObjectName(QStringLiteral("historyPager"));
    auto *pagerLayout = new QHBoxLayout(pager);
    pagerLayout->setContentsMargins(0, 2, 0, 0);
    pagerLayout->setSpacing(8);
    m_historyPageLabel = new QLabel(QStringLiteral("第 1 / 1 页\n本页 0 · 共 0"), pager);
    m_historyPageLabel->setProperty("role", "muted");
    pagerLayout->addWidget(m_historyPageLabel, 1);
    pagerLayout->addWidget(m_historyPreviousPageButton);
    pagerLayout->addWidget(m_historyNextPageButton);
    listLayout->addWidget(pager);

    auto *detailPanel = new QFrame(m_historyWorkspace);
    detailPanel->setObjectName(QStringLiteral("historyDetailPanel"));
    auto *detailPanelLayout = new QVBoxLayout(detailPanel);
    detailPanelLayout->setContentsMargins(0, 0, 0, 0);
    auto *detailScroll = new QScrollArea(detailPanel);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setWidgetResizable(true);
    detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailPanelLayout->addWidget(detailScroll);
    auto *detailRoot = new QWidget(detailScroll);
    auto *detailLayout = new QVBoxLayout(detailRoot);
    detailLayout->setContentsMargins(18, 16, 18, 16);
    detailLayout->setSpacing(12);

    m_historyDetailEmptyState = new QWidget(detailRoot);
    auto *emptyLayout = new QVBoxLayout(m_historyDetailEmptyState);
    emptyLayout->addStretch(1);
    m_historyEmptyTitleLabel = new QLabel(QStringLiteral("没有符合条件的训练记录"), m_historyDetailEmptyState);
    m_historyEmptyTitleLabel->setProperty("role", "sectionTitle");
    m_historyEmptyTitleLabel->setAlignment(Qt::AlignCenter);
    m_historyEmptyTitleLabel->setWordWrap(true);
    m_historyEmptyBodyLabel = new QLabel(QStringLiteral("尝试调整筛选条件后重新查询。"), m_historyDetailEmptyState);
    m_historyEmptyBodyLabel->setProperty("role", "muted");
    m_historyEmptyBodyLabel->setAlignment(Qt::AlignCenter);
    m_historyEmptyBodyLabel->setWordWrap(true);
    m_historyEmptyResetButton = new QPushButton(QStringLiteral("重置筛选"), m_historyDetailEmptyState);
    m_historyEmptyResetButton->setProperty("role", "secondaryButton");
    emptyLayout->addWidget(m_historyEmptyTitleLabel);
    emptyLayout->addWidget(m_historyEmptyBodyLabel);
    emptyLayout->addWidget(m_historyEmptyResetButton, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    detailLayout->addWidget(m_historyDetailEmptyState, 1);

    m_historyDetailContent = new QWidget(detailRoot);
    auto *contentLayout = new QVBoxLayout(m_historyDetailContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    auto *currentTitle = new QLabel(QStringLiteral("当前训练详情"), m_historyDetailContent);
    currentTitle->setProperty("role", "sectionTitle");
    contentLayout->addWidget(currentTitle);
    m_historyDetailAthleteLabel = new QLabel(m_historyDetailContent);
    m_historyDetailAthleteLabel->setProperty("role", "sectionTitle");
    m_historyDetailAthleteLabel->setTextFormat(Qt::PlainText);
    m_historyDetailAthleteLabel->setWordWrap(true);
    m_historyDetailAthleteLabel->setMinimumWidth(0);
    contentLayout->addWidget(m_historyDetailAthleteLabel);
    m_historyDetailTimeLabel = new QLabel(m_historyDetailContent);
    m_historyDetailTimeLabel->setProperty("role", "muted");
    m_historyDetailTimeLabel->setTextFormat(Qt::PlainText);
    contentLayout->addWidget(m_historyDetailTimeLabel);
    m_historyDetailSourceLabel = new QLabel(m_historyDetailContent);
    m_historyDetailSourceLabel->setProperty("role", "scoreTag");
    m_historyDetailSourceLabel->setTextFormat(Qt::PlainText);
    m_historyDetailSourceLabel->setWordWrap(true);
    m_historyDetailSourceLabel->setMinimumWidth(0);
    m_historyDetailSourceLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    contentLayout->addWidget(m_historyDetailSourceLabel, 0, Qt::AlignLeft);

    auto *primaryActions = new QHBoxLayout();
    m_historyPlayButton = new QPushButton(QStringLiteral("播放录像"), m_historyDetailContent);
    m_historyReviewButton = new QPushButton(QStringLiteral("复盘校准"), m_historyDetailContent);
    m_historyPlayButton->setProperty("variant", "primary");
    m_historyReviewButton->setProperty("role", "secondaryButton");
    primaryActions->addWidget(m_historyPlayButton);
    primaryActions->addWidget(m_historyReviewButton);
    primaryActions->addStretch(1);
    contentLayout->addLayout(primaryActions);

    auto *secondaryActions = new QHBoxLayout();
    m_historyTrackButton = new QPushButton(QStringLiteral("轨迹 / 速度"), m_historyDetailContent);
    m_historyCommentButton = new QPushButton(QStringLiteral("编辑批注"), m_historyDetailContent);
    m_historyExportButton = new QPushButton(QStringLiteral("导出报告"), m_historyDetailContent);
    for (QPushButton *button : {m_historyTrackButton, m_historyCommentButton, m_historyExportButton}) {
        button->setProperty("role", "secondaryButton");
        secondaryActions->addWidget(button);
    }
    secondaryActions->addStretch(1);
    contentLayout->addLayout(secondaryActions);

    auto addDetailSection = [contentLayout, this](const QString &title, QLabel **valueLabel) {
        auto *section = new QFrame(m_historyDetailContent);
        setRole(section, "historyItem");
        auto *sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(12, 12, 12, 12);
        auto *titleLabel = new QLabel(title, section);
        titleLabel->setProperty("role", "sectionTitle");
        sectionLayout->addWidget(titleLabel);
        *valueLabel = new QLabel(section);
        (*valueLabel)->setProperty("role", "muted");
        (*valueLabel)->setTextFormat(Qt::PlainText);
        (*valueLabel)->setWordWrap(true);
        sectionLayout->addWidget(*valueLabel);
        contentLayout->addWidget(section);
    };
    addDetailSection(QStringLiteral("训练摘要"), &m_historyDetailSummaryLabel);
    addDetailSection(QStringLiteral("训练信息"), &m_historyDetailTrainingInfoLabel);
    addDetailSection(QStringLiteral("来源"), &m_historyDetailSourceInfoLabel);
    addDetailSection(QStringLiteral("教练批注"), &m_historyDetailCommentLabel);
    m_historyDetailCommentLabel->setMaximumHeight(m_historyDetailCommentLabel->fontMetrics().lineSpacing() * 3 + 4);

    contentLayout->addStretch(1);
    detailLayout->addWidget(m_historyDetailContent, 1);
    detailScroll->setWidget(detailRoot);

    m_historyWorkspace->addWidget(listPanel);
    m_historyWorkspace->addWidget(detailPanel);
    m_historyWorkspace->setStretchFactor(0, 1);
    m_historyWorkspace->setStretchFactor(1, 1);
    m_historyWorkspace->setSizes({380, 620});
    ui->historyPageLayout->addWidget(m_historyWorkspace, 1);

    m_historySearchButton = searchButton;
    m_historyRepetitionSearchButton = repetitionSearchButton;
    m_historyCompetitionManagementButton = manageCompetitionsButton;
    m_historyReportCenterButton = reportCenterButton;

    connect(m_historyFromCheckBox, &QCheckBox::toggled, m_historyFromDateEdit, &QDateEdit::setEnabled);
    connect(m_historyToCheckBox, &QCheckBox::toggled, m_historyToDateEdit, &QDateEdit::setEnabled);
    connect(searchButton, &QPushButton::clicked, this, [this]() {
        m_historyPageNumber = 1;
        loadTrainingRecords();
        refreshHistory();
        refreshSuggestions();
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() { resetHistorySearch(); });
    connect(reportCenterButton, &QPushButton::clicked, this, &MainWindow::openMetricReportCenter);
    connect(repetitionSearchButton, &QPushButton::clicked, this, [this]() { openRepetitionSearchDialog(); });
    connect(manageCompetitionsButton, &QPushButton::clicked, this, [this]() { openCompetitionManagement(); });
    connect(m_historyCompetitionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        reloadHistorySearchOptions();
    });
    connect(m_historyPreviousPageButton, &QPushButton::clicked, this, [this]() {
        m_historyPageNumber = std::max(1, m_historyPageNumber - 1);
        loadTrainingRecords();
        refreshHistory();
        refreshSuggestions();
    });
    connect(m_historyNextPageButton, &QPushButton::clicked, this, [this]() {
        m_historyPageNumber = std::min(historyMaxPage(), m_historyPageNumber + 1);
        loadTrainingRecords();
        refreshHistory();
        refreshSuggestions();
    });
    connect(m_historySortComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_historyPageNumber = 1;
        loadTrainingRecords();
        refreshHistory();
        refreshSuggestions();
    });
    connect(m_historySessionList, &QListWidget::currentRowChanged, this, [this](int row) {
        const QListWidgetItem *item = row >= 0 ? m_historySessionList->item(row) : nullptr;
        selectHistorySession(item ? item->data(Qt::UserRole).toString() : QString());
    });
    connect(m_historyEmptyResetButton, &QPushButton::clicked, this, [this]() { resetHistorySearch(); });
    connect(m_historyPlayButton, &QPushButton::clicked, this, [this]() {
        if (const SessionHistoryItem *record = historySessionById(m_selectedHistorySessionId)) {
            const SessionHistoryItem selected = *record;
            openSessionVideo(selected);
        }
    });
    connect(m_historyReviewButton, &QPushButton::clicked, this, [this]() {
        if (const SessionHistoryItem *record = historySessionById(m_selectedHistorySessionId)) {
            const SessionHistoryItem selected = *record;
            openTrainingReview(selected);
        }
    });
    connect(m_historyTrackButton, &QPushButton::clicked, this, [this]() {
        if (const SessionHistoryItem *record = historySessionById(m_selectedHistorySessionId)) {
            const SessionHistoryItem selected = *record;
            openTrackPointReview(selected);
        }
    });
    connect(m_historyCommentButton, &QPushButton::clicked, this, [this]() {
        if (const SessionHistoryItem *record = historySessionById(m_selectedHistorySessionId)) {
            const QString sessionId = record->id;
            editCoachComment(sessionId);
        }
    });
    connect(m_historyExportButton, &QPushButton::clicked, this, [this]() {
        if (const SessionHistoryItem *record = historySessionById(m_selectedHistorySessionId)) {
            const QString sessionId = record->id;
            exportTrainingReport(sessionId);
        }
    });

    reloadHistorySearchOptions();
    refreshHistoryPager();
}

void MainWindow::rebuildWorkspaceLayout()
{
    ui->sidebar->setMinimumWidth(220);
    ui->sidebar->setMaximumWidth(220);
    ui->navSuggestionButton->hide();
    ui->mainViewTitleLabel->hide();
    ui->leftCardLayout->removeItem(ui->headlineLayout);
    delete ui->headlineLayout;
    ui->headlineLayout = nullptr;

    ui->capturePageLayout->removeWidget(ui->leftCard);
    ui->capturePageLayout->removeWidget(ui->opsCard);
    m_workspaceSplitter = new QSplitter(Qt::Horizontal, ui->capturePage);
    m_workspaceSplitter->setObjectName(QStringLiteral("workspaceSplitter"));
    m_workspaceSplitter->setChildrenCollapsible(false);
    m_workspaceSplitter->setHandleWidth(5);
    m_workspaceSplitter->addWidget(ui->leftCard);
    m_workspaceSplitter->addWidget(ui->opsCard);
    m_workspaceSplitter->setStretchFactor(0, 1);
    m_workspaceSplitter->setStretchFactor(1, 0);
    ui->opsCard->setMinimumWidth(300);
    ui->opsCard->setMaximumWidth(420);
    m_workspaceSplitter->setSizes({1100, 340});
    connect(m_workspaceSplitter, &QSplitter::splitterMoved, this, [this]() {
        updateCameraGrid();
    });
    ui->capturePageLayout->addWidget(m_workspaceSplitter);

    const int cameraAreaIndex = ui->leftCardLayout->indexOf(ui->cameraScrollArea);
    ui->cameraScrollArea->hide();
    ui->leftCardLayout->removeWidget(ui->trajectoryCard);
    m_cameraTrajectoryRow = new QWidget(ui->leftCard);
    m_cameraTrajectoryRow->setObjectName(QStringLiteral("cameraTrajectoryRow"));
    m_cameraTrajectoryRow->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_cameraTrajectoryRow->installEventFilter(this);
    m_cameraTrajectoryLayout = new QHBoxLayout(m_cameraTrajectoryRow);
    m_cameraTrajectoryLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraTrajectoryLayout->setSpacing(12);

    m_cameraGridContainer = new QWidget(m_cameraTrajectoryRow);
    m_cameraGridContainer->setObjectName(QStringLiteral("cameraGridContainer"));
    m_cameraGridContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_cameraGridContainer->installEventFilter(this);
    m_cameraGridLayout = new QGridLayout(m_cameraGridContainer);
    m_cameraGridLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraGridLayout->setHorizontalSpacing(8);
    m_cameraGridLayout->setVerticalSpacing(8);
    m_cameraGridLayout->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_cameraTrajectoryLayout->addWidget(m_cameraGridContainer, 1);
    ui->trajectoryCard->setParent(m_cameraTrajectoryRow);
    ui->trajectoryCard->setMinimumWidth(280);
    ui->trajectoryCard->setMaximumWidth(400);
    ui->trajectoryCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_cameraTrajectoryLayout->addWidget(ui->trajectoryCard);
    ui->trajectoryCard->show();
    ui->leftCardLayout->insertWidget(std::max(0, cameraAreaIndex), m_cameraTrajectoryRow);

    ui->trajectoryCard->setMinimumHeight(0);
    ui->trajectoryCard->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->trajectoryCardLayout->setContentsMargins(12, 8, 12, 10);
    ui->trajectoryCardLayout->setSpacing(5);
    ui->trajectoryTitleLabel->setText(QStringLiteral("二维滑行轨迹"));
    ui->legendTitleLabel->setText(QStringLiteral("当前运动员："));
    updateCameraGrid();

    ui->athleteSectionLabel->setText(QStringLiteral("当前运动员"));
    ui->athleteIdentityLabel->setText(QStringLiteral("—"));
    ui->athleteDetectionLabel->setText(QStringLiteral("—"));
    ui->athleteSourceLabel->setText(QStringLiteral("—"));
    if (auto *athleteLayout = qobject_cast<QVBoxLayout *>(ui->athleteInspector->layout())) {
        athleteLayout->removeWidget(ui->athleteComboBox);
        athleteLayout->removeWidget(ui->athleteIdentityLabel);
        athleteLayout->removeWidget(ui->athleteDetectionLabel);
        athleteLayout->removeWidget(ui->athleteSourceLabel);
        athleteLayout->setSpacing(8);
        athleteLayout->addWidget(ui->athleteComboBox);
        m_athleteNameLabel = new QLabel(QStringLiteral("未选择运动员"), ui->athleteInspector);
        m_athleteNameLabel->setObjectName(QStringLiteral("currentAthleteNameLabel"));
        m_athleteNameLabel->setProperty("role", "athleteName");
        m_athleteNameLabel->setWordWrap(true);
        athleteLayout->addWidget(m_athleteNameLabel);
        athleteLayout->addWidget(ui->athleteIdentityLabel);
        if (m_trainingStateLabel) {
            ui->inspectorContentLayout->removeWidget(m_trainingStateLabel);
            athleteLayout->addWidget(m_trainingStateLabel);
        }
    }
    ui->inspectorContentLayout->removeWidget(ui->modelStatusLabel);
    ui->inspectorContentLayout->removeWidget(ui->durationLabel);
    ui->inspectorContentLayout->removeWidget(ui->durationValueLabel);
    ui->inspectorContentLayout->removeWidget(ui->actionLabel);
    ui->inspectorContentLayout->removeWidget(ui->actionValueLabel);
    ui->durationLabel->hide();
    ui->actionLabel->hide();
    ui->actionValueLabel->hide();
    if (m_speedStatusLabel) {
        ui->inspectorContentLayout->removeWidget(m_speedStatusLabel);
    }
    auto *liveTitle = new QLabel(QStringLiteral("实时状态"), ui->inspectorContent);
    liveTitle->setProperty("role", "sectionTitle");
    ui->inspectorContentLayout->addWidget(liveTitle);
    auto *liveGrid = new QGridLayout();
    liveGrid->setContentsMargins(0, 0, 0, 0);
    liveGrid->setHorizontalSpacing(16);
    liveGrid->setVerticalSpacing(10);
    const QStringList liveLabels = {
        QStringLiteral("当前机位"), QStringLiteral("检测目标"),
        QStringLiteral("训练时长"), QStringLiteral("当前速度")
    };
    const QVector<QLabel *> liveValues = {
        ui->athleteSourceLabel, ui->athleteDetectionLabel,
        ui->durationValueLabel, m_speedStatusLabel
    };
    for (int row = 0; row < liveValues.size(); ++row) {
        auto *label = new QLabel(liveLabels.at(row), ui->inspectorContent);
        label->setProperty("role", "muted");
        liveGrid->addWidget(label, row, 0);
        liveGrid->addWidget(liveValues.at(row), row, 1);
    }
    liveGrid->setColumnStretch(1, 1);
    ui->inspectorContentLayout->addLayout(liveGrid);

    auto *aiTitle = new QLabel(QStringLiteral("AI 状态"), ui->inspectorContent);
    aiTitle->setProperty("role", "sectionTitle");
    ui->inspectorContentLayout->addWidget(aiTitle);
    auto *aiGrid = new QGridLayout();
    aiGrid->setContentsMargins(0, 0, 0, 0);
    aiGrid->setHorizontalSpacing(16);
    aiGrid->setVerticalSpacing(10);
    auto *analysisLabel = new QLabel(QStringLiteral("AI 分析"), ui->inspectorContent);
    analysisLabel->setProperty("role", "muted");
    auto *identityLabel = new QLabel(QStringLiteral("身份识别"), ui->inspectorContent);
    identityLabel->setProperty("role", "muted");
    m_identityAvailabilityLabel = new QLabel(QStringLiteral("—"), ui->inspectorContent);
    aiGrid->addWidget(analysisLabel, 0, 0);
    aiGrid->addWidget(ui->modelStatusLabel, 0, 1);
    aiGrid->addWidget(identityLabel, 1, 0);
    aiGrid->addWidget(m_identityAvailabilityLabel, 1, 1);
    aiGrid->setColumnStretch(1, 1);
    ui->inspectorContentLayout->addLayout(aiGrid);
    ui->advancedSettingsButton->setMinimumHeight(40);
    ui->startCaptureButton->setMinimumHeight(44);
    ui->pauseCaptureButton->setMinimumHeight(40);
    ui->stopCaptureButton->setMinimumHeight(40);
    ui->saveRecordButton->setMinimumHeight(40);

    ui->inspectorContentLayout->removeWidget(ui->settingsBox);
    m_trainingSettingsDialog = new FramelessDialog(this);
    m_trainingSettingsDialog->setObjectName(QStringLiteral("trainingSettingsDrawer"));
    m_trainingSettingsDialog->setWindowTitle(QStringLiteral("训练设置"));
    m_trainingSettingsDialog->setDialogTitle(QStringLiteral("训练设置"));
    m_trainingSettingsDialog->setModal(false);
    m_trainingSettingsDialog->setMinimumSize(420, 560);
    auto *settingsScroll = new QScrollArea(m_trainingSettingsDialog);
    settingsScroll->setObjectName(QStringLiteral("trainingSettingsScroll"));
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *settingsContent = new QWidget(settingsScroll);
    auto *settingsContentLayout = new QVBoxLayout(settingsContent);
    settingsContentLayout->setContentsMargins(18, 14, 18, 18);
    settingsContentLayout->addWidget(ui->settingsBox);
    settingsContentLayout->addStretch(1);
    settingsScroll->setWidget(settingsContent);
    m_trainingSettingsDialog->contentLayout()->addWidget(settingsScroll);
    ui->settingsBox->show();
    connect(m_trainingSettingsDialog, &QDialog::rejected, this, [this]() {
        if (m_settingsAnimation) {
            m_settingsAnimation->stop();
        }
        m_settingsExpanded = false;
        ui->advancedSettingsButton->setProperty("active", false);
        repolish(ui->advancedSettingsButton);
    });

    if (auto *bestCard = ui->summaryBestValue->parentWidget()) {
        bestCard->hide();
    }
    if (auto *label = ui->historyPage->findChild<QLabel *>(QStringLiteral("summarySessionsLabel"))) {
        label->setText(QStringLiteral("记录"));
    }
    if (auto *label = ui->historyPage->findChild<QLabel *>(QStringLiteral("summaryActionsLabel"))) {
        label->setText(QStringLiteral("训练"));
    }
    if (auto *label = ui->historyPage->findChild<QLabel *>(QStringLiteral("summaryAvgLabel"))) {
        label->setText(QStringLiteral("比赛"));
    }
    auto *summaryInline = new QFrame(ui->historyPage);
    summaryInline->setObjectName(QStringLiteral("historySummaryInline"));
    auto *summaryInlineLayout = new QHBoxLayout(summaryInline);
    summaryInlineLayout->setContentsMargins(12, 8, 12, 8);
    summaryInlineLayout->setSpacing(18);
    const QVector<QPair<QLabel *, QLabel *>> summaryItems = {
        {ui->summarySessionsLabel, ui->summarySessionsValue},
        {ui->summaryActionsLabel, ui->summaryActionsValue},
        {ui->summaryAvgLabel, ui->summaryAvgValue}
    };
    for (const auto &item : summaryItems) {
        item.first->setParent(summaryInline);
        item.second->setParent(summaryInline);
        summaryInlineLayout->addWidget(item.first);
        summaryInlineLayout->addWidget(item.second);
    }
    summaryInlineLayout->addStretch(1);
    ui->summarySessionsCard->hide();
    ui->summaryActionsCard->hide();
    ui->summaryAvgCard->hide();
    ui->summaryBestCard->hide();
    ui->historyPageLayout->insertWidget(2, summaryInline);
    for (int index = ui->historyPageLayout->count() - 1; index >= 0; --index) {
        QLayoutItem *item = ui->historyPageLayout->itemAt(index);
        if (item && item->spacerItem()) {
            delete ui->historyPageLayout->takeAt(index);
        }
    }
    ui->historyTitleLabel->hide();
}

void MainWindow::updateCameraGrid()
{
    if (!m_cameraGridLayout || !m_cameraGridContainer) {
        return;
    }
    if (m_cameraTrajectoryRow) {
        const int rowWidth = m_cameraTrajectoryRow->contentsRect().width();
        const int trajectoryWidth = std::clamp(qRound(rowWidth * 0.28), 280, 400);
        if (ui->trajectoryCard->width() != trajectoryWidth) {
            ui->trajectoryCard->setFixedWidth(trajectoryWidth);
        }
    }
    const int columns = m_cameraGridContainer->contentsRect().width() >= 840 ? 6 : 4;
    const int rows = (m_cameraButtons.size() + columns - 1) / columns;
    const int availableWidth = std::max(0, m_cameraGridContainer->contentsRect().width()
                                           - (columns - 1) * 8);
    int tileWidth = columns > 0 ? availableWidth / columns : 0;
    const QMargins margins = ui->leftCardLayout->contentsMargins();
    const int layoutSpacing = ui->leftCardLayout->spacing()
                              * std::max(0, ui->leftCardLayout->count() - 1);
    const int remainingHeight = std::max(0, ui->leftCard->height()
        - margins.top() - margins.bottom() - layoutSpacing
        - ui->mainImageLabel->minimumHeight());
    // 两种列数都限制网格高度，为主视频保留空间。
    const int preferredHeight = ui->leftCard->height() * 35 / 100;
    const int heightLimit = std::min(preferredHeight, remainingHeight);
    const int maxTileHeight = std::max(0, heightLimit - (rows - 1) * 8)
                               / std::max(1, rows);
    int tileHeight = qRound(tileWidth * 9.0 / 16.0);
    if (tileHeight > maxTileHeight) {
        tileWidth = std::min(tileWidth, maxTileHeight * 16 / 9);
        tileHeight = qRound(tileWidth * 9.0 / 16.0);
    }
    const QSize tileSize(tileWidth, tileHeight);
    const int naturalGridHeight = rows * tileHeight + std::max(0, rows - 1) * 8;
    const int rowHeight = std::min(std::max(naturalGridHeight, 210), heightLimit);
    if (m_cameraGridContainer->height() != rowHeight
        || m_cameraGridContainer->minimumHeight() != rowHeight
        || m_cameraGridContainer->maximumHeight() != rowHeight) {
        m_cameraGridContainer->setFixedHeight(rowHeight);
    }
    if (m_cameraTrajectoryRow && m_cameraTrajectoryRow->height() != rowHeight) {
        m_cameraTrajectoryRow->setFixedHeight(rowHeight);
    }
    if (ui->trajectoryCard->height() != rowHeight) {
        ui->trajectoryCard->setFixedHeight(rowHeight);
    }
    const bool columnsChanged = m_cameraGridColumns != columns;
    const bool sizeChanged = m_cameraTileSize != tileSize;
    if (!columnsChanged && !sizeChanged) {
        return;
    }

    if (columnsChanged) {
        while (QLayoutItem *item = m_cameraGridLayout->takeAt(0)) {
            delete item;
        }
        m_cameraGridColumns = columns;
        for (int row = 0; row < 3; ++row) {
            m_cameraGridLayout->setRowStretch(row, row < rows ? 1 : 0);
        }
        for (int index = 0; index < m_cameraButtons.size(); ++index) {
            VideoOpenGLWidget *camera = m_cameraButtons.at(index);
            if (camera->parentWidget() != m_cameraGridContainer) {
                camera->setParent(m_cameraGridContainer);
            }
            camera->setMinimumSize(0, 0);
            camera->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            if (!camera->isVisible()) {
                camera->show();
            }
            m_cameraGridLayout->addWidget(camera,
                                          index / columns,
                                          index % columns,
                                          Qt::AlignHCenter | Qt::AlignVCenter);
        }
    }

    if (columnsChanged || sizeChanged) {
        m_cameraTileSize = tileSize;
        for (VideoOpenGLWidget *camera : m_cameraButtons) {
            if (camera->size() != tileSize
                || camera->minimumSize() != tileSize
                || camera->maximumSize() != tileSize) {
                camera->setFixedSize(tileSize);
            }
        }
    }
}

void MainWindow::openTrainingSettings()
{
    if (!m_trainingSettingsDialog) {
        return;
    }
    m_settingsExpanded = true;
    ui->advancedSettingsButton->setProperty("active", true);
    repolish(ui->advancedSettingsButton);

    const int drawerWidth = width() >= 1500 ? 460 : 420;
    const int drawerHeight = std::max(560, ui->opsCard->height());
    const QPoint inspectorTopLeft = ui->opsCard->mapToGlobal(QPoint(0, 0));
    QScreen *screen = QGuiApplication::screenAt(inspectorTopLeft);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen ? screen->availableGeometry()
                                   : QRect(inspectorTopLeft, QSize(drawerWidth, drawerHeight));
    const int actualHeight = std::min(drawerHeight, available.height());
    const int endX = std::clamp(inspectorTopLeft.x() - drawerWidth - 12,
                                available.left(),
                                std::max(available.left(), available.right() - drawerWidth + 1));
    const int endY = std::clamp(inspectorTopLeft.y(),
                                available.top(),
                                std::max(available.top(), available.bottom() - actualHeight + 1));
    const QRect endGeometry(endX, endY, drawerWidth, actualHeight);
    const QRect startGeometry(inspectorTopLeft.x(), endGeometry.y(), drawerWidth, actualHeight);
    m_trainingSettingsDialog->setGeometry(startGeometry);
    m_trainingSettingsDialog->show();
    m_trainingSettingsDialog->raise();

    if (!m_settingsAnimation) {
        m_settingsAnimation = new QVariantAnimation(this);
    }
    m_settingsAnimation->stop();
    m_settingsAnimation->setDuration(200);
    m_settingsAnimation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::disconnect(m_settingsAnimation, nullptr, this, nullptr);
    connect(m_settingsAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (m_trainingSettingsDialog) {
            m_trainingSettingsDialog->setGeometry(value.toRect());
        }
    });
    m_settingsAnimation->setStartValue(startGeometry);
    m_settingsAnimation->setEndValue(endGeometry);
    m_settingsAnimation->start();
}

void MainWindow::closeTrainingSettings()
{
    if (!m_trainingSettingsDialog || !m_trainingSettingsDialog->isVisible()) {
        m_settingsExpanded = false;
        return;
    }
    m_settingsExpanded = false;
    ui->advancedSettingsButton->setProperty("active", false);
    repolish(ui->advancedSettingsButton);
    const QRect startGeometry = m_trainingSettingsDialog->geometry();
    const QRect endGeometry(startGeometry.right() + 24,
                            startGeometry.y(),
                            startGeometry.width(),
                            startGeometry.height());
    if (!m_settingsAnimation) {
        m_settingsAnimation = new QVariantAnimation(this);
    }
    m_settingsAnimation->stop();
    m_settingsAnimation->setDuration(200);
    m_settingsAnimation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::disconnect(m_settingsAnimation, nullptr, this, nullptr);
    connect(m_settingsAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (m_trainingSettingsDialog) {
            m_trainingSettingsDialog->setGeometry(value.toRect());
        }
    });
    connect(m_settingsAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_trainingSettingsDialog && !m_settingsExpanded) {
            m_trainingSettingsDialog->hide();
        }
    });
    m_settingsAnimation->setStartValue(startGeometry);
    m_settingsAnimation->setEndValue(endGeometry);
    m_settingsAnimation->start();
}

// 从资源系统读取 QSS，统一应用暗色仪表盘主题样式。
void MainWindow::applyStyleSheet()
{
    QFile qssFile(QStringLiteral(":/styles/iskating.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStyleSheet(QString::fromUtf8(qssFile.readAll()));
    }
}

// 从 QSettings 读取 12 路摄像头配置；预览使用子码流，主视图使用主码流，兼容旧版 url/IP 配置。
void MainWindow::loadCameraSettings()
{
    m_sharedCameraSettings = {};
    m_sharedCameraSettings.port = QString::number(kDefaultRtspPort);
    m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
    m_sharedCameraSettings.mainFps = kDefaultMainStreamFps;
    m_cameraSlotSettings = QVector<CameraSlotSettings>(m_cameraButtons.size());
    for (int i = 0; i < m_cameraSlotSettings.size(); ++i) {
        m_cameraSlotSettings[i] = defaultCameraSlotSettings(i);
    }
    m_capturePreferenceSettings = {};

    QSettings settings;
    settings.beginGroup(QStringLiteral("capture"));
    const int legacyCaptureFps = settings.value(QStringLiteral("fps"), kDefaultFps).toInt();
    m_capturePreferenceSettings.modelPrecision = settings.value(QStringLiteral("modelPrecision"),
                                                                QStringLiteral("balanced")).toString().trimmed();
    m_capturePreferenceSettings.analysisSource = settings.value(QStringLiteral("analysisSource"),
                                                                QStringLiteral("preview")).toString().trimmed();
    m_capturePreferenceSettings.analysisTargetFps = settings.value(QStringLiteral("analysisTargetFps"),
                                                                   kDefaultAnalysisTargetFps).toInt();
    m_capturePreferenceSettings.analysisMaxStreams = settings.value(QStringLiteral("analysisMaxStreams"),
                                                                    kDefaultAnalysisMaxStreams).toInt();
    m_capturePreferenceSettings.analysisAutoDegrade = settings.value(QStringLiteral("analysisAutoDegrade"),
                                                                     true).toBool();
    settings.endGroup();
    if (m_capturePreferenceSettings.modelPrecision.isEmpty()) {
        m_capturePreferenceSettings.modelPrecision = QStringLiteral("balanced");
    }
    if (m_capturePreferenceSettings.analysisSource != QStringLiteral("main")) {
        m_capturePreferenceSettings.analysisSource = QStringLiteral("preview");
    }
    if (m_capturePreferenceSettings.analysisTargetFps <= 0) {
        m_capturePreferenceSettings.analysisTargetFps = kDefaultAnalysisTargetFps;
    }
    m_capturePreferenceSettings.analysisMaxStreams = std::clamp(m_capturePreferenceSettings.analysisMaxStreams,
                                                                1,
                                                                kDefaultAnalysisMaxStreams);
    m_capturePreferenceSettings.fps = m_capturePreferenceSettings.analysisTargetFps;

    if (hasStructuredCameraDefaults(settings)) {
        settings.beginGroup(QStringLiteral("cameraDefaults"));
        m_sharedCameraSettings.username = settings.value(QStringLiteral("username")).toString().trimmed();
        m_sharedCameraSettings.password = settings.value(QStringLiteral("password")).toString();
        m_sharedCameraSettings.port = settings.value(QStringLiteral("port"), QString::number(kDefaultRtspPort)).toString().trimmed();
        if (m_sharedCameraSettings.port.isEmpty()) {
            m_sharedCameraSettings.port = QString::number(kDefaultRtspPort);
        }
        m_sharedCameraSettings.previewPath = normalizedStoredPath(settings.value(QStringLiteral("previewPath")).toString());
        m_sharedCameraSettings.previewFps = settings.value(QStringLiteral("previewFps"),
                                                           kDefaultPreviewStreamFps).toInt();
        m_sharedCameraSettings.mainPath = normalizedStoredPath(settings.value(QStringLiteral("mainPath")).toString());
        m_sharedCameraSettings.mainFps = settings.value(QStringLiteral("mainFps"),
                                                        legacyCaptureFps > kDefaultPreviewStreamFps
                                                            ? legacyCaptureFps
                                                            : kDefaultMainStreamFps).toInt();
        m_sharedCameraSettings.nvrPlaybackTemplate = settings.value(QStringLiteral("nvrPlaybackTemplate")).toString().trimmed();
        settings.endGroup();

        for (int i = 0; i < m_cameraButtons.size(); ++i) {
            settings.beginGroup(cameraSettingsGroup(i));
            m_cameraSlotSettings[i].ip = settings.value(QStringLiteral("ip")).toString().trimmed();
            m_cameraSlotSettings[i].trajectoryEnabled = settings.value(QStringLiteral("trajectoryEnabled"),
                                                                       m_cameraSlotSettings[i].trajectoryEnabled).toBool();
            m_cameraSlotSettings[i].role = settings.value(QStringLiteral("role"),
                                                          m_cameraSlotSettings[i].role).toString().trimmed();
            m_cameraSlotSettings[i].fieldStartM = settings.value(QStringLiteral("fieldStartM"),
                                                                 m_cameraSlotSettings[i].fieldStartM).toDouble();
            m_cameraSlotSettings[i].fieldEndM = settings.value(QStringLiteral("fieldEndM"),
                                                               m_cameraSlotSettings[i].fieldEndM).toDouble();
            m_cameraSlotSettings[i].lateralOffsetM = settings.value(QStringLiteral("lateralOffsetM"),
                                                                    m_cameraSlotSettings[i].lateralOffsetM).toDouble();
            m_cameraSlotSettings[i].mountHeightM = settings.value(QStringLiteral("mountHeightM"),
                                                                  m_cameraSlotSettings[i].mountHeightM).toDouble();
            m_cameraSlotSettings[i].yawDeg = settings.value(QStringLiteral("yawDeg"),
                                                            m_cameraSlotSettings[i].yawDeg).toDouble();
            m_cameraSlotSettings[i].pitchDeg = settings.value(QStringLiteral("pitchDeg"),
                                                              m_cameraSlotSettings[i].pitchDeg).toDouble();
            m_cameraSlotSettings[i].calibrationJson = settings.value(QStringLiteral("calibration")).toString();
            m_cameraSlotSettings[i].qualityNote = settings.value(QStringLiteral("qualityNote")).toString().trimmed();
            m_cameraSlotSettings[i].compatibilityNote = settings.value(QStringLiteral("compatibilityNote")).toString().trimmed();
            settings.endGroup();
        }
    } else {
        bool sharedInitialized = false;
        for (int i = 0; i < m_cameraButtons.size(); ++i) {
            settings.beginGroup(cameraSettingsGroup(i));
            const QString previewUrl = settings.value(QStringLiteral("previewUrl")).toString().trimmed();
            const QString mainUrl = settings.value(QStringLiteral("mainUrl")).toString().trimmed();
            const QString legacyUrl = settings.value(QStringLiteral("url")).toString().trimmed();
            const QString legacyIp = settings.value(QStringLiteral("ip")).toString().trimmed();
            const QString legacyPort = settings.value(QStringLiteral("port")).toString().trimmed();
            const QString legacyPath = settings.value(QStringLiteral("path")).toString().trimmed();
            settings.endGroup();

            const QString previewSource = !previewUrl.isEmpty()
                                              ? previewUrl
                                              : (!legacyUrl.isEmpty() ? legacyUrl
                                                                      : composeLegacyStreamUrl(legacyIp, legacyPort, legacyPath));
            const QString mainSource = !mainUrl.isEmpty() ? mainUrl : previewSource;

            if (!previewSource.isEmpty()) {
                m_cameraSlotSettings[i].ip = extractHostFromLegacyValue(previewSource);
            } else {
                m_cameraSlotSettings[i].ip = extractHostFromLegacyValue(legacyIp);
            }

            if (!sharedInitialized) {
                const ParsedRtspUrl previewParsed = parseRtspUrl(previewSource);
                const ParsedRtspUrl mainParsed = parseRtspUrl(mainSource);
                const ParsedRtspUrl baseParsed = previewParsed.valid ? previewParsed : mainParsed;
                if (baseParsed.valid) {
                    m_sharedCameraSettings.username = baseParsed.username;
                    m_sharedCameraSettings.password = baseParsed.password;
                    m_sharedCameraSettings.port = baseParsed.port.isEmpty()
                                                      ? QString::number(kDefaultRtspPort)
                                                      : baseParsed.port;
                    m_sharedCameraSettings.previewPath = previewParsed.valid
                                                             ? previewParsed.path
                                                             : normalizedStoredPath(legacyPath);
                    m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
                    m_sharedCameraSettings.mainPath = mainParsed.valid ? mainParsed.path : QString();
                    m_sharedCameraSettings.mainFps = legacyCaptureFps > kDefaultPreviewStreamFps
                                                        ? legacyCaptureFps
                                                        : kDefaultMainStreamFps;
                    sharedInitialized = true;
                } else if (!legacyIp.isEmpty() || !legacyPort.isEmpty() || !legacyPath.isEmpty()) {
                    m_sharedCameraSettings.port = legacyPort.isEmpty()
                                                      ? QString::number(kDefaultRtspPort)
                                                      : legacyPort;
                    m_sharedCameraSettings.previewPath = normalizedStoredPath(legacyPath);
                    m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
                    m_sharedCameraSettings.mainPath.clear();
                    m_sharedCameraSettings.mainFps = legacyCaptureFps > kDefaultPreviewStreamFps
                                                        ? legacyCaptureFps
                                                        : kDefaultMainStreamFps;
                    sharedInitialized = true;
                }
            }
        }
    }

    if (m_sharedCameraSettings.previewFps <= 0) {
        m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
    }
    if (m_sharedCameraSettings.mainFps <= 0) {
        m_sharedCameraSettings.mainFps = legacyCaptureFps > kDefaultPreviewStreamFps
                                             ? legacyCaptureFps
                                             : kDefaultMainStreamFps;
    }
    m_capturePreferenceSettings.fps = m_capturePreferenceSettings.analysisTargetFps;

    settings.beginGroup(QStringLiteral("videoStorage"));
    m_videoStorageSettings.rootDir = settings.value(QStringLiteral("rootDir")).toString().trimmed();
    m_videoStorageSettings.capacityLimitGb = settings.value(QStringLiteral("capacityLimitGb"), 50).toInt();
    m_videoStorageSettings.retentionDays = settings.value(QStringLiteral("retentionDays"), 60).toInt();
    settings.endGroup();
    if (m_videoStorageSettings.capacityLimitGb <= 0) {
        m_videoStorageSettings.capacityLimitGb = 50;
    }
    if (m_videoStorageSettings.retentionDays <= 0) {
        m_videoStorageSettings.retentionDays = 60;
    }

    applyCameraSettingsToWidgets(false);
    refreshCameraConfigurationStatus();
    refreshVideoStorageStatus();
}

void MainWindow::refreshCameraConfigurationStatus()
{
    int configuredCount = 0;
    for (const CameraSlotSettings &slot : m_cameraSlotSettings) {
        if (!slot.ip.trimmed().isEmpty()) {
            ++configuredCount;
        }
    }
    ui->cameraStatusValue1->setText(QStringLiteral("%1 / %2")
                                      .arg(configuredCount)
                                      .arg(m_cameraSlotSettings.size()));
    ui->cameraStatusValue1->setProperty("state", configuredCount > 0 ? "success" : "muted");
    repolish(ui->cameraStatusValue1);
    refreshCameraRuntimeStatus();
}

void MainWindow::startEnvironmentCheck()
{
    if (m_environmentCheckThread) {
        return;
    }

    refreshTrainingServiceStatus(QDateTime());
    refreshAiCapabilityStatus();
    refreshVideoStorageStatus();
    refreshAnalysisTaskStatus();
    refreshCameraConfigurationStatus();

    const int configuredCount = std::count_if(m_cameraSlotSettings.cbegin(), m_cameraSlotSettings.cend(),
                                             [](const CameraSlotSettings &slot) { return !slot.ip.trimmed().isEmpty(); });
    const int revision = m_cameraConfigurationRevision;
    const QString password = m_sharedCameraSettings.password;
    m_environmentCheckButton->setEnabled(false);
    m_environmentCheckButton->setText(QStringLiteral("检查中 0 / %1").arg(configuredCount));
    m_environmentCheckLabel->setText(QStringLiteral("正在检查"));

    auto *thread = new QThread(this);
    auto *tester = new CameraConnectivityTester(m_sharedCameraSettings, m_cameraSlotSettings);
    m_environmentCheckThread = thread;
    tester->moveToThread(thread);
    connect(thread, &QThread::started, tester, &CameraConnectivityTester::run);
    connect(tester, &CameraConnectivityTester::progress, this,
            [this, revision, configuredCount, completedCount = 0](int, int, const CameraConnectivityResult &result) mutable {
        if (revision != m_cameraConfigurationRevision || result.skipped) {
            return;
        }
        ++completedCount;
        m_environmentCheckButton->setText(QStringLiteral("检查中 %1 / %2").arg(completedCount).arg(configuredCount));
        m_environmentCheckLabel->setText(QStringLiteral("已检查 CAM %1")
                                           .arg(result.cameraIndex + 1, 2, 10, QLatin1Char('0')));
    });
    connect(tester, &CameraConnectivityTester::finished, this,
            [this, revision, password](const QVector<CameraConnectivityResult> &results) {
        if (revision != m_cameraConfigurationRevision) {
            m_environmentCheckLabel->setText(QStringLiteral("配置已更新，请重新检查"));
            return;
        }

        m_lastCameraConnectivityResults = results;
        // Keep credentials out of stored display details, including URL-encoded passwords.
        if (!password.isEmpty()) {
            const QString encodedPassword = QString::fromLatin1(QUrl::toPercentEncoding(password));
            for (CameraConnectivityResult &result : m_lastCameraConnectivityResults) {
                for (QString *field : {&result.ip, &result.status, &result.addressStatus, &result.transport,
                                       &result.resolution, &result.frameRate, &result.failureStage,
                                       &result.errorCode, &result.ffmpegErrorCode, &result.message}) {
                    field->replace(encodedPassword, QStringLiteral("***"));
                    field->replace(password, QStringLiteral("***"));
                }
            }
        }
        refreshVideoStorageStatus();
        refreshAnalysisTaskStatus();
        refreshAiCapabilityStatus();
        m_lastEnvironmentCheckAt = QDateTime::currentDateTime();
        m_cameraConnectivityChecked = true;
        m_cameraConnectivityInvalidated = false;
        refreshCameraConnectivityStatus();
        refreshTrainingServiceStatus(m_lastEnvironmentCheckAt);
        ui->trainingServiceValue3->setToolTip(QStringLiteral("环境检查完成时间；训练服务和 AI 使用当前已知状态"));
        m_environmentCheckLabel->setText(QStringLiteral("检查完成"));
    });
    connect(tester, &CameraConnectivityTester::finished, tester, &QObject::deleteLater);
    // quit() must not depend on the UI event loop: the destructor may be waiting.
    connect(tester, &CameraConnectivityTester::finished, thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, this, [this, thread]() {
        m_environmentCheckThread = nullptr;
        m_environmentCheckButton->setText(QStringLiteral("重新检查"));
        m_environmentCheckButton->setEnabled(true);
        thread->deleteLater();
    });
    thread->start();
}

void MainWindow::invalidateCameraConnectivityResults()
{
    ++m_cameraConfigurationRevision;
    m_lastCameraConnectivityResults.clear();
    m_cameraConnectivityChecked = false;
    m_cameraConnectivityInvalidated = true;
    if (m_environmentCheckThread) {
        m_environmentCheckThread->requestInterruption();
    }
    m_environmentCheckLabel->setText(QStringLiteral("配置已更新，请执行检查"));
}

void MainWindow::refreshCameraConnectivityStatus()
{
    if (!m_cameraConnectivityChecked) {
        ui->cameraStatusValue2->setText(QStringLiteral("未检测"));
        ui->cameraStatusValue3->setText(QStringLiteral("—"));
        for (QLabel *label : {ui->cameraStatusValue2, ui->cameraStatusValue3}) {
            label->setProperty("state", "muted");
            label->setToolTip(QStringLiteral("配置已更新，请重新执行连通检查"));
            repolish(label);
        }
        return;
    }

    int configuredCount = 0;
    int successCount = 0;
    int failedCount = 0;
    int skippedCount = 0;
    QStringList details;
    for (const CameraConnectivityResult &result : m_lastCameraConnectivityResults) {
        if (result.skipped) {
            ++skippedCount;
            continue;
        }
        ++configuredCount;
        if (result.success) {
            ++successCount;
        } else {
            ++failedCount;
        }
        QString detail = QStringLiteral("CAM %1\n状态：%2")
                             .arg(result.cameraIndex + 1, 2, 10, QLatin1Char('0'))
                             .arg(result.success ? QStringLiteral("成功") : QStringLiteral("失败"));
        if (result.success) {
            detail += QStringLiteral("\n传输：%1\n分辨率：%2\n帧率：%3")
                          .arg(result.transport, result.resolution, result.frameRate);
        }
        if (result.openElapsedMs >= 0) {
            detail += QStringLiteral("\n打开耗时：%1 ms").arg(result.openElapsedMs);
        }
        if (result.firstFrameElapsedMs >= 0) {
            detail += QStringLiteral("\n首帧：%1 ms").arg(result.firstFrameElapsedMs);
        }
        if (!result.success) {
            detail += QStringLiteral("\n阶段：%1\n错误码：%2\n错误：%3")
                          .arg(result.failureStage, result.errorCode, result.message);
            if (!result.ffmpegErrorCode.isEmpty()) {
                detail += QStringLiteral("\nFFmpeg 错误码：%1").arg(result.ffmpegErrorCode);
            }
        }
        details.append(detail);
    }
    ui->cameraStatusValue2->setText(configuredCount > 0
                                      ? QStringLiteral("%1 / %2").arg(successCount).arg(configuredCount)
                                      : QStringLiteral("—"));
    ui->cameraStatusValue2->setProperty("state", configuredCount == 0 ? "muted"
                                                  : failedCount == 0 ? "success"
                                                  : successCount > 0 ? "warning" : "error");
    ui->cameraStatusValue3->setText(configuredCount > 0 ? QString::number(failedCount) : QStringLiteral("—"));
    ui->cameraStatusValue3->setProperty("state", configuredCount == 0 ? "muted"
                                                  : failedCount > 0 ? "error" : "success");
    const QString summary = QStringLiteral("最近检查：%1\n已配置：%2\n成功：%3\n失败：%4\n跳过：%5\n\n%6")
                                .arg(m_lastEnvironmentCheckAt.toString(QStringLiteral("hh:mm:ss")))
                                .arg(configuredCount).arg(successCount).arg(failedCount).arg(skippedCount)
                                .arg(details.join(QStringLiteral("\n\n")));
    const QString tooltip = QStringLiteral("<qt>%1</qt>").arg(summary.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    for (QLabel *label : {ui->cameraStatusValue2, ui->cameraStatusValue3}) {
        label->setToolTip(tooltip);
        repolish(label);
    }
}

void MainWindow::refreshCameraRuntimeStatus()
{
    if (m_cameraConnectivityChecked || m_cameraConnectivityInvalidated) {
        refreshCameraConnectivityStatus();
        return;
    }
    int configuredCount = 0;
    int onlineCount = 0;
    int connectingCount = 0;
    int reconnectingCount = 0;
    int errorCount = 0;
    int streamCount = 0;
    int inactiveCount = 0;
    QStringList errorCameras;

    for (int i = 0; i < m_cameraSlotSettings.size(); ++i) {
        if (m_cameraSlotSettings.at(i).ip.trimmed().isEmpty()) {
            continue;
        }
        ++configuredCount;

        std::shared_ptr<RtspStream> stream;
        if (i < m_cameraButtons.size() && m_cameraButtons.at(i)) {
            stream = m_cameraButtons.at(i)->activeStream();
        }
        if (!stream && m_offlineVideoPath.trimmed().isEmpty() && m_selectedCamera == i + 1
            && ui->mainImageLabel) {
            stream = ui->mainImageLabel->activeStream();
        }
        if (!stream) {
            ++inactiveCount;
            continue;
        }
        ++streamCount;

        switch (stream->state()) {
        case RtspStream::State::Playing:
            ++onlineCount;
            break;
        case RtspStream::State::Connecting:
            ++connectingCount;
            break;
        case RtspStream::State::Reconnecting:
            ++reconnectingCount;
            break;
        case RtspStream::State::Error:
            ++errorCount;
            errorCameras.append(QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0')));
            break;
        case RtspStream::State::Idle:
        case RtspStream::State::Stopped:
            ++inactiveCount;
            break;
        }
    }

    const bool hasRuntimeState = streamCount > 0;
    ui->cameraStatusValue2->setText(hasRuntimeState
                                        ? QStringLiteral("%1 / %2").arg(onlineCount).arg(configuredCount)
                                        : QStringLiteral("未检测"));
    ui->cameraStatusValue2->setProperty("state", onlineCount > 0
                                                    ? "success"
                                                    : (connectingCount > 0 || reconnectingCount > 0)
                                                          ? "warning"
                                                          : "muted");
    ui->cameraStatusValue3->setText(hasRuntimeState ? QString::number(errorCount) : QStringLiteral("—"));
    ui->cameraStatusValue3->setProperty("state", !hasRuntimeState ? "muted"
                                                    : errorCount > 0 ? "error" : "success");

    QString tooltip = QStringLiteral("已配置：%1\n在线：%2\n连接中：%3\n重连中：%4\n异常：%5\n未活动：%6")
                          .arg(configuredCount)
                          .arg(onlineCount)
                          .arg(connectingCount)
                          .arg(reconnectingCount)
                          .arg(errorCount)
                          .arg(inactiveCount);
    if (!errorCameras.isEmpty()) {
        tooltip += QStringLiteral("\n异常摄像头：\n%1").arg(errorCameras.join(QLatin1Char('\n')));
    }
    ui->cameraStatusValue2->setToolTip(tooltip);
    ui->cameraStatusValue3->setToolTip(tooltip);
    repolish(ui->cameraStatusValue2);
    repolish(ui->cameraStatusValue3);
}

void MainWindow::refreshAnalysisTaskStatus()
{
    if (!m_analysisTaskManager) {
        ui->localResourcesValue2->setText(QStringLiteral("未就绪"));
        ui->localResourcesValue2->setToolTip(QString());
        ui->localResourcesValue2->setProperty("state", "muted");
        repolish(ui->localResourcesValue2);
        return;
    }

    int queuedCount = 0;
    int runningCount = 0;
    int pausedCount = 0;
    int completedCount = 0;
    int failedCount = 0;
    int cancelledCount = 0;
    const QVector<AnalysisTask> tasks = m_analysisTaskManager->tasks();
    for (const AnalysisTask &task : tasks) {
        const QString status = task.status.trimmed().toLower();
        if (status == QStringLiteral("queued")) ++queuedCount;
        else if (status == QStringLiteral("running")) ++runningCount;
        else if (status == QStringLiteral("paused")) ++pausedCount;
        else if (status == QStringLiteral("completed")) ++completedCount;
        else if (status == QStringLiteral("failed")) ++failedCount;
        else if (status == QStringLiteral("cancelled")) ++cancelledCount;
    }

    if (tasks.isEmpty() && (!m_trainingRepository || !m_trainingRepository->isOpen())) {
        ui->localResourcesValue2->setText(QStringLiteral("未同步"));
        ui->localResourcesValue2->setProperty("state", "muted");
    } else if (queuedCount + runningCount > 0) {
        ui->localResourcesValue2->setText(QStringLiteral("%1 个进行中").arg(queuedCount + runningCount));
        ui->localResourcesValue2->setProperty("state", "success");
    } else if (pausedCount > 0) {
        ui->localResourcesValue2->setText(QStringLiteral("%1 个已暂停").arg(pausedCount));
        ui->localResourcesValue2->setProperty("state", "warning");
    } else {
        ui->localResourcesValue2->setText(QStringLiteral("空闲"));
        ui->localResourcesValue2->setProperty("state", "muted");
    }
    ui->localResourcesValue2->setToolTip(
        QStringLiteral("运行中：%1\n排队：%2\n暂停：%3\n已完成：%4\n失败：%5\n已取消：%6")
            .arg(runningCount)
            .arg(queuedCount)
            .arg(pausedCount)
            .arg(completedCount)
            .arg(failedCount)
            .arg(cancelledCount));
    repolish(ui->localResourcesValue2);
}

// 程序退出时保存全部摄像头配置，确保未触发单路保存的变更也会落盘。
void MainWindow::saveCameraSettings()
{
    m_capturePreferenceSettings = capturePreferenceSettingsFromUi();
    persistSystemSettings();
}

// 保存指定摄像头配置：名称、预览子码流、主画面码流；同时保留旧字段以兼容已有代码。
void MainWindow::saveCameraSetting(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= m_cameraButtons.size()) {
        return;
    }

    saveCameraSettings();
}

void MainWindow::persistSystemSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("cameraDefaults"));
    settings.setValue(QStringLiteral("username"), m_sharedCameraSettings.username.trimmed());
    settings.setValue(QStringLiteral("password"), m_sharedCameraSettings.password);
    settings.setValue(QStringLiteral("port"),
                      m_sharedCameraSettings.port.trimmed().isEmpty()
                          ? QString::number(kDefaultRtspPort)
                          : m_sharedCameraSettings.port.trimmed());
    settings.setValue(QStringLiteral("previewPath"), normalizedStoredPath(m_sharedCameraSettings.previewPath));
    settings.setValue(QStringLiteral("previewFps"), m_sharedCameraSettings.previewFps > 0
                                                      ? m_sharedCameraSettings.previewFps
                                                      : kDefaultPreviewStreamFps);
    settings.setValue(QStringLiteral("mainPath"), normalizedStoredPath(m_sharedCameraSettings.mainPath));
    settings.setValue(QStringLiteral("mainFps"), m_sharedCameraSettings.mainFps > 0
                                                   ? m_sharedCameraSettings.mainFps
                                                   : kDefaultMainStreamFps);
    settings.setValue(QStringLiteral("nvrPlaybackTemplate"), m_sharedCameraSettings.nvrPlaybackTemplate.trimmed());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("capture"));
    settings.setValue(QStringLiteral("modelPrecision"), m_capturePreferenceSettings.modelPrecision.trimmed().isEmpty()
                                                            ? QStringLiteral("balanced")
                                                            : m_capturePreferenceSettings.modelPrecision.trimmed());
    settings.setValue(QStringLiteral("analysisSource"), m_capturePreferenceSettings.analysisSource == QStringLiteral("main")
                                                           ? QStringLiteral("main")
                                                           : QStringLiteral("preview"));
    settings.setValue(QStringLiteral("analysisTargetFps"), m_capturePreferenceSettings.analysisTargetFps > 0
                                                            ? m_capturePreferenceSettings.analysisTargetFps
                                                            : kDefaultAnalysisTargetFps);
    settings.setValue(QStringLiteral("analysisMaxStreams"), std::clamp(m_capturePreferenceSettings.analysisMaxStreams,
                                                                       1,
                                                                       kDefaultAnalysisMaxStreams));
    settings.setValue(QStringLiteral("analysisAutoDegrade"), m_capturePreferenceSettings.analysisAutoDegrade);
    settings.setValue(QStringLiteral("fps"), m_capturePreferenceSettings.analysisTargetFps > 0
                                                ? m_capturePreferenceSettings.analysisTargetFps
                                                : kDefaultAnalysisTargetFps);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("videoStorage"));
    settings.setValue(QStringLiteral("rootDir"), m_videoStorageSettings.rootDir.trimmed());
    settings.setValue(QStringLiteral("capacityLimitGb"), m_videoStorageSettings.capacityLimitGb > 0
                                                            ? m_videoStorageSettings.capacityLimitGb
                                                            : 50);
    settings.setValue(QStringLiteral("retentionDays"), m_videoStorageSettings.retentionDays > 0
                                                          ? m_videoStorageSettings.retentionDays
                                                          : 60);
    settings.endGroup();

    const QString normalizedPort = m_sharedCameraSettings.port.trimmed().isEmpty()
                                       ? QString::number(kDefaultRtspPort)
                                       : m_sharedCameraSettings.port.trimmed();
    const QString normalizedPreviewPath = normalizedStoredPath(m_sharedCameraSettings.previewPath);
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        const QString ip = i < m_cameraSlotSettings.size() ? m_cameraSlotSettings.at(i).ip.trimmed() : QString();
        const QString previewUrl = composeCameraUrl(m_sharedCameraSettings, ip, false);
        const QString mainUrl = composeCameraUrl(m_sharedCameraSettings, ip, true);
        settings.beginGroup(cameraSettingsGroup(i));
        settings.setValue(QStringLiteral("name"), configuredCameraChannelName(i, ip));
        settings.setValue(QStringLiteral("previewUrl"), previewUrl);
        settings.setValue(QStringLiteral("mainUrl"), mainUrl);
        settings.setValue(QStringLiteral("url"), previewUrl);
        settings.setValue(QStringLiteral("ip"), ip);
        settings.setValue(QStringLiteral("port"), normalizedPort);
        settings.setValue(QStringLiteral("path"), normalizedPreviewPath);
        settings.setValue(QStringLiteral("trajectoryEnabled"), m_cameraSlotSettings.at(i).trajectoryEnabled);
        settings.setValue(QStringLiteral("role"), m_cameraSlotSettings.at(i).role.trimmed());
        settings.setValue(QStringLiteral("fieldStartM"), m_cameraSlotSettings.at(i).fieldStartM);
        settings.setValue(QStringLiteral("fieldEndM"), m_cameraSlotSettings.at(i).fieldEndM);
        settings.setValue(QStringLiteral("lateralOffsetM"), m_cameraSlotSettings.at(i).lateralOffsetM);
        settings.setValue(QStringLiteral("mountHeightM"), m_cameraSlotSettings.at(i).mountHeightM);
        settings.setValue(QStringLiteral("yawDeg"), m_cameraSlotSettings.at(i).yawDeg);
        settings.setValue(QStringLiteral("pitchDeg"), m_cameraSlotSettings.at(i).pitchDeg);
        settings.setValue(QStringLiteral("calibration"), m_cameraSlotSettings.at(i).calibrationJson);
        settings.setValue(QStringLiteral("qualityNote"), m_cameraSlotSettings.at(i).qualityNote.trimmed());
        settings.setValue(QStringLiteral("compatibilityNote"), m_cameraSlotSettings.at(i).compatibilityNote.trimmed());
        settings.endGroup();
    }
    settings.sync();
}

CapturePreferenceSettings MainWindow::capturePreferenceSettingsFromUi() const
{
    CapturePreferenceSettings settings = m_capturePreferenceSettings;
    if (ui->precisionComboBox) {
        settings.modelPrecision = ui->precisionComboBox->currentData().toString().trimmed();
    }
    if (settings.modelPrecision.isEmpty()) {
        settings.modelPrecision = QStringLiteral("balanced");
    }

    if (ui->fpsComboBox) {
        settings.analysisTargetFps = ui->fpsComboBox->currentData().toInt();
    }
    if (settings.analysisTargetFps <= 0) {
        settings.analysisTargetFps = kDefaultAnalysisTargetFps;
    }
    settings.fps = settings.analysisTargetFps;
    if (settings.analysisSource != QStringLiteral("main")) {
        settings.analysisSource = QStringLiteral("preview");
    }
    settings.analysisMaxStreams = std::clamp(settings.analysisMaxStreams, 1, kDefaultAnalysisMaxStreams);
    return settings;
}

void MainWindow::applyCapturePreferencesToUi()
{
    if (ui->precisionComboBox) {
        const QString precisionValue = m_capturePreferenceSettings.modelPrecision.trimmed().isEmpty()
                                           ? QStringLiteral("balanced")
                                           : m_capturePreferenceSettings.modelPrecision.trimmed();
        const int precisionIndex = ui->precisionComboBox->findData(precisionValue);
        ui->precisionComboBox->setCurrentIndex(precisionIndex >= 0 ? precisionIndex : 1);
    }

    if (ui->fpsComboBox) {
        const int fpsValue = m_capturePreferenceSettings.analysisTargetFps > 0
                                 ? m_capturePreferenceSettings.analysisTargetFps
                                 : kDefaultAnalysisTargetFps;
        const int fpsIndex = ui->fpsComboBox->findData(fpsValue);
        ui->fpsComboBox->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : ui->fpsComboBox->findData(kDefaultAnalysisTargetFps));
    }
    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->setAnalysisProfile(m_capturePreferenceSettings.modelPrecision);
    }
}

void MainWindow::applyCameraSettingsToWidgets(bool restorePlayback)
{
    if (m_cameraSlotSettings.size() < m_cameraButtons.size()) {
        const int oldSize = m_cameraSlotSettings.size();
        m_cameraSlotSettings.resize(m_cameraButtons.size());
        for (int i = oldSize; i < m_cameraSlotSettings.size(); ++i) {
            m_cameraSlotSettings[i] = defaultCameraSlotSettings(i);
        }
    }

    const bool offlineMode = !m_offlineVideoPath.trimmed().isEmpty();
    QVector<bool> previewWasPlaying;
    previewWasPlaying.reserve(m_cameraButtons.size());
    for (auto *cameraWidget : m_cameraButtons) {
        previewWasPlaying.append(restorePlayback && cameraWidget && cameraWidget->isPlaying());
    }
    const bool mainWasPlaying = restorePlayback && ui->mainImageLabel && ui->mainImageLabel->isPlaying();
    const bool shouldResumeStreams = restorePlayback && m_isRecording && !m_isPaused;

    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        const QString ip = m_cameraSlotSettings.at(i).ip.trimmed();
        const QString channelName = configuredCameraChannelName(i, ip);
        const QString previewUrl = composeCameraUrl(m_sharedCameraSettings, ip, false);
        const QString mainUrl = composeCameraUrl(m_sharedCameraSettings, ip, true);
        cameraWidget->setChannelName(channelName);
        cameraWidget->setPlaceholderText(QStringLiteral("%1\n%2")
                                             .arg(channelName, cameraSegmentLabel(m_cameraSlotSettings.at(i))));
        cameraWidget->setStreamUrls(previewUrl, mainUrl);

        if (offlineMode) {
            cameraWidget->stopPlayback();
            continue;
        }

        if (!restorePlayback) {
            continue;
        }

        if (previewUrl.trimmed().isEmpty()) {
            cameraWidget->stopPlayback();
        } else if (shouldResumeStreams || previewWasPlaying.value(i)) {
            cameraWidget->playDefaultVideo();
        }
    }

    if (offlineMode) {
        showOfflineVideoInMainView(shouldResumeStreams || mainWasPlaying);
    } else if (!m_cameraButtons.isEmpty()) {
        int selectedIndex = std::max(0, m_selectedCamera - 1);
        if (selectedIndex >= m_cameraButtons.size()) {
            selectedIndex = m_cameraButtons.size() - 1;
        }
        showCameraInMainView(selectedIndex, shouldResumeStreams || mainWasPlaying);
    }
}

void MainWindow::syncAnalysisStreams()
{
    if (!m_athleteAnalysisManager) {
        return;
    }

    m_capturePreferenceSettings = capturePreferenceSettingsFromUi();
    m_athleteAnalysisManager->setAnalysisProfile(m_capturePreferenceSettings.modelPrecision);
    if (!m_isRecording || m_isPaused) {
        m_athleteAnalysisManager->setPaused(true);
        m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
        return;
    }

    QVector<AthleteAnalysisManager::AnalysisStream> streams;
    const int targetFps = m_capturePreferenceSettings.analysisTargetFps > 0
                              ? m_capturePreferenceSettings.analysisTargetFps
                              : kDefaultAnalysisTargetFps;
    if (!m_offlineVideoPath.trimmed().isEmpty()) {
        AthleteAnalysisManager::AnalysisStream stream;
        stream.cameraId = 0;
        stream.sourceName = m_offlineVideoName.trimmed().isEmpty()
                                ? offlineVideoDisplayName(m_offlineVideoPath)
                                : m_offlineVideoName.trimmed();
        stream.targetFps = targetFps;
        stream.priority = 0;
        stream.autoDegrade = false;
        stream.stream = ui->mainImageLabel->activeStream();
        if (stream.stream) {
            streams.append(stream);
        }
    } else {
        const int cameraCount = static_cast<int>(m_cameraButtons.size());
        const int maxStreams = std::clamp(m_capturePreferenceSettings.analysisMaxStreams,
                                          1,
                                          std::max(1, cameraCount));
        QVector<int> cameraOrder;
        cameraOrder.reserve(m_cameraButtons.size());
        if (m_selectedCamera > 0 && m_selectedCamera <= m_cameraButtons.size()) {
            cameraOrder.append(m_selectedCamera - 1);
        }
        for (int i = 0; i < m_cameraButtons.size(); ++i) {
            if (!cameraOrder.contains(i)) {
                cameraOrder.append(i);
            }
        }

        for (int i : cameraOrder) {
            if (streams.size() >= maxStreams) {
                break;
            }
            if (i >= m_cameraSlotSettings.size()) {
                continue;
            }
            const CameraSlotSettings &slot = m_cameraSlotSettings.at(i);
            if (!slot.trajectoryEnabled || slot.ip.trimmed().isEmpty()) {
                continue;
            }
            VideoOpenGLWidget *cameraWidget = m_cameraButtons.at(i);
            const bool primaryCamera = i + 1 == m_selectedCamera;
            AthleteAnalysisManager::AnalysisStream stream;
            stream.cameraId = i + 1;
            stream.sourceName = cameraWidget->channelName();
            stream.targetFps = targetFps;
            stream.priority = primaryCamera ? 0 : 1;
            stream.autoDegrade = m_capturePreferenceSettings.analysisAutoDegrade;
            if (primaryCamera && m_capturePreferenceSettings.analysisSource == QStringLiteral("main")) {
                stream.stream = ui->mainImageLabel->activeStream();
            }
            if (!stream.stream) {
                stream.stream = cameraWidget->activeStream();
            }
            if (stream.stream) {
                streams.append(stream);
            }
        }
    }

    if (streams.isEmpty() && ui->mainImageLabel->activeStream()) {
        AthleteAnalysisManager::AnalysisStream fallback;
        fallback.cameraId = m_selectedCamera;
        fallback.sourceName = m_selectedCamera > 0
                                  ? QStringLiteral("CAM %1").arg(m_selectedCamera, 2, 10, QLatin1Char('0'))
                                  : m_offlineVideoName;
        fallback.targetFps = targetFps;
        fallback.priority = 0;
        fallback.autoDegrade = false;
        fallback.stream = ui->mainImageLabel->activeStream();
        streams.append(fallback);
    }

    m_athleteAnalysisManager->setActiveStreams(streams);
    m_athleteAnalysisManager->setPaused(streams.isEmpty());
}

QString MainWindow::cameraReadinessSummary() const
{
    int configured = 0;
    int trajectoryEnabled = 0;
    int calibrated = 0;
    QStringList missing;
    for (int i = 0; i < m_cameraSlotSettings.size(); ++i) {
        const CameraSlotSettings &slot = m_cameraSlotSettings.at(i);
        if (!slot.ip.trimmed().isEmpty()) {
            ++configured;
        }
        if (!slot.trajectoryEnabled) {
            continue;
        }
        ++trajectoryEnabled;
        if (slot.fieldEndM > slot.fieldStartM && !slot.ip.trimmed().isEmpty()) {
            ++calibrated;
        } else {
            missing.append(QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0')));
        }
    }

    QString summary = QStringLiteral("相机就绪：已配置 %1/12，参与轨迹 %2 路，已标定 %3 路。")
                          .arg(configured)
                          .arg(trajectoryEnabled)
                          .arg(calibrated);
    if (!missing.isEmpty()) {
        summary += QStringLiteral(" 需补齐：%1。").arg(missing.join(QStringLiteral("、")));
    }
    return summary;
}

void MainWindow::openSystemSettings()
{
    SystemSettingsDialog dialog(m_cameraButtons.size(), this);
    dialog.setSharedCameraSettings(m_sharedCameraSettings);
    dialog.setCameraSlotSettings(m_cameraSlotSettings);
    dialog.setCapturePreferenceSettings(m_capturePreferenceSettings);
    dialog.setVideoStorageSettings(m_videoStorageSettings);
    dialog.setVideoStorageStatus(videoStorageStatusSummary(m_videoStorageSettings));
    dialog.setVideoStorageActionsEnabled(m_trainingRepository && m_trainingRepository->isOpen());
    dialog.onBrowseVideoStorageRoot = [&dialog]() {
        const QString selected = QFileDialog::getExistingDirectory(&dialog,
                                                                   QStringLiteral("选择录像根目录"),
                                                                   dialog.videoStorageSettings().rootDir);
        if (selected.trimmed().isEmpty()) {
            return;
        }
        VideoStorageSettings settings = dialog.videoStorageSettings();
        settings.rootDir = QFileInfo(selected).absoluteFilePath();
        dialog.setVideoStorageSettings(settings);
    };
    dialog.onScanVideoStorage = [this, &dialog]() {
        dialog.setVideoStorageStatus(videoStorageStatusSummary(dialog.videoStorageSettings()));
    };
    dialog.onShowVideoCleanupCandidates = [this, &dialog]() {
        showVideoCleanupCandidates(&dialog);
    };

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_sharedCameraSettings = dialog.sharedCameraSettings();
    invalidateCameraConnectivityResults();
    m_cameraSlotSettings = dialog.cameraSlotSettings();
    m_capturePreferenceSettings = dialog.capturePreferenceSettings();
    m_videoStorageSettings = dialog.videoStorageSettings();
    m_capturePreferenceSettings.fps = m_capturePreferenceSettings.analysisTargetFps;
    applyCapturePreferencesToUi();
    applyCameraSettingsToWidgets(true);
    saveCameraSettings();
    refreshCameraConfigurationStatus();
    refreshVideoStorageStatus();
}

QString MainWindow::videoStorageRootDir() const
{
    const QString configured = m_videoStorageSettings.rootDir.trimmed();
    return configured.isEmpty() ? defaultVideoStorageRoot() : QFileInfo(configured).absoluteFilePath();
}

void MainWindow::refreshVideoStorageStatus()
{
    if (m_videoStorageSettings.rootDir.trimmed().isEmpty()) {
        ui->localResourcesValue1->setText(QStringLiteral("未配置"));
        ui->localResourcesValue1->setProperty("state", "muted");
        ui->localResourcesValue1->setToolTip(QStringLiteral("尚未配置视频存储目录"));
    } else {
        const QString root = videoStorageRootDir();
        const QFileInfo directory(root);
        const QStorageInfo storage(root);
        const bool volumeReady = storage.isValid() && storage.isReady();
        const bool available = directory.exists() && directory.isDir() && directory.isWritable()
                               && volumeReady && !storage.isReadOnly();
        ui->localResourcesValue1->setText(available ? QStringLiteral("可用") : QStringLiteral("不可用"));
        ui->localResourcesValue1->setProperty("state", available ? "success" : "error");
        const QString diskText = volumeReady
                                     ? QStringLiteral("磁盘可用 %1 / 总计 %2")
                                           .arg(storageSizeLabel(storage.bytesAvailable()),
                                                storageSizeLabel(storage.bytesTotal()))
                                     : QStringLiteral("磁盘不可用");
        ui->localResourcesValue1->setToolTip(QStringLiteral("存储路径：%1\n%2")
                                               .arg(QDir::toNativeSeparators(root), diskText));
    }
    repolish(ui->localResourcesValue1);
}

QVector<VideoFileCleanupCandidate> MainWindow::videoCleanupCandidates(const VideoStorageSettings &settings,
                                                                      qint64 *existingBytes) const
{
    if (existingBytes) {
        *existingBytes = 0;
    }
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        return {};
    }

    const QString root = settings.rootDir.trimmed().isEmpty()
                             ? defaultVideoStorageRoot()
                             : QFileInfo(settings.rootDir.trimmed()).absoluteFilePath();
    const QVector<VideoFileCleanupCandidate> indexedFiles = m_trainingRepository->videoFiles(QString(), true);
    QVector<VideoFileCleanupCandidate> existingFiles;
    existingFiles.reserve(indexedFiles.size());
    for (VideoFileCleanupCandidate item : indexedFiles) {
        if (item.filePath.trimmed().isEmpty() || !isPathUnderRoot(item.filePath, root)) {
            continue;
        }
        const QFileInfo fileInfo(item.filePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }
        item.filePath = fileInfo.absoluteFilePath();
        item.fileSizeBytes = fileInfo.size();
        item.fileModifiedAt = fileInfo.lastModified();
        if (existingBytes) {
            *existingBytes += item.fileSizeBytes;
        }
        existingFiles.append(item);
    }

    const QDateTime retentionCutoff = QDateTime::currentDateTime().addDays(-std::max(1, settings.retentionDays));
    QVector<VideoFileCleanupCandidate> candidates;
    qint64 candidateBytes = 0;
    for (const VideoFileCleanupCandidate &item : std::as_const(existingFiles)) {
        const QDateTime modifiedAt = item.fileModifiedAt.isValid() ? item.fileModifiedAt : item.sessionStartedAt;
        if (modifiedAt.isValid() && modifiedAt < retentionCutoff) {
            candidates.append(item);
            candidateBytes += item.fileSizeBytes;
        }
    }

    const qint64 capacityBytes = static_cast<qint64>(std::max(1, settings.capacityLimitGb)) * 1024LL * 1024LL * 1024LL;
    if (existingBytes && *existingBytes > capacityBytes) {
        QVector<VideoFileCleanupCandidate> sortedFiles = existingFiles;
        std::sort(sortedFiles.begin(), sortedFiles.end(), [](const VideoFileCleanupCandidate &left,
                                                             const VideoFileCleanupCandidate &right) {
            return left.fileModifiedAt < right.fileModifiedAt;
        });
        QSet<QString> selectedIds;
        for (const VideoFileCleanupCandidate &item : std::as_const(candidates)) {
            selectedIds.insert(item.id);
        }
        for (const VideoFileCleanupCandidate &item : std::as_const(sortedFiles)) {
            if (*existingBytes - candidateBytes <= capacityBytes) {
                break;
            }
            if (selectedIds.contains(item.id)) {
                continue;
            }
            candidates.append(item);
            selectedIds.insert(item.id);
            candidateBytes += item.fileSizeBytes;
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const VideoFileCleanupCandidate &left,
                                                       const VideoFileCleanupCandidate &right) {
        return left.fileModifiedAt < right.fileModifiedAt;
    });
    return candidates;
}

QString MainWindow::videoStorageStatusSummary(const VideoStorageSettings &settings) const
{
    qint64 indexedBytes = 0;
    const QVector<VideoFileCleanupCandidate> candidates = videoCleanupCandidates(settings, &indexedBytes);
    const QString root = settings.rootDir.trimmed().isEmpty()
                             ? defaultVideoStorageRoot()
                             : QFileInfo(settings.rootDir.trimmed()).absoluteFilePath();
    QStorageInfo storage(root);
    const QString diskText = storage.isValid()
                                 ? QStringLiteral("磁盘可用 %1 / 总计 %2")
                                       .arg(storageSizeLabel(storage.bytesAvailable()),
                                            storageSizeLabel(storage.bytesTotal()))
                                 : QStringLiteral("磁盘容量未知");
    return QStringLiteral("容量状态：已登记本机文件 %1，清理候选 %2 个；阈值 %3 GB，保留 %4 天；%5。")
        .arg(storageSizeLabel(indexedBytes))
        .arg(candidates.size())
        .arg(settings.capacityLimitGb)
        .arg(settings.retentionDays)
        .arg(diskText);
}

void MainWindow::showVideoCleanupCandidates(SystemSettingsDialog *dialog)
{
    if (!dialog || !m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(dialog, QStringLiteral("训练服务不可用"), QStringLiteral("训练服务未连接，无法读取视频资产索引。"));
        return;
    }

    const VideoStorageSettings settings = dialog->videoStorageSettings();
    qint64 existingBytes = 0;
    const QVector<VideoFileCleanupCandidate> candidates = videoCleanupCandidates(settings, &existingBytes);
    if (candidates.isEmpty()) {
        dialog->setVideoStorageStatus(videoStorageStatusSummary(settings));
        QMessageBox::information(dialog, QStringLiteral("暂无清理候选"), QStringLiteral("当前没有符合保留天数或容量阈值的已登记本机视频文件。"));
        return;
    }

    QDialog cleanupDialog(dialog);
    cleanupDialog.setWindowTitle(QStringLiteral("视频清理候选"));
    cleanupDialog.resize(980, 520);
    auto *layout = new QVBoxLayout(&cleanupDialog);
    auto *tipLabel = new QLabel(QStringLiteral("只会删除勾选的本机视频文件；训练记录、动作片段索引和报告数据会保留。"), &cleanupDialog);
    tipLabel->setWordWrap(true);
    layout->addWidget(tipLabel);

    auto *table = new QTableWidget(candidates.size(), 8, &cleanupDialog);
    table->setHorizontalHeaderLabels({
        QStringLiteral("删除"),
        QStringLiteral("训练时间"),
        QStringLiteral("运动员"),
        QStringLiteral("状态"),
        QStringLiteral("视频序号"),
        QStringLiteral("文件路径"),
        QStringLiteral("大小"),
        QStringLiteral("动作片段")
    });
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int row = 0; row < candidates.size(); ++row) {
        const VideoFileCleanupCandidate &item = candidates.at(row);
        auto *checkItem = new QTableWidgetItem();
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        checkItem->setCheckState(Qt::Checked);
        table->setItem(row, 0, checkItem);
        table->setItem(row, 1, new QTableWidgetItem(item.sessionStartedAt.isValid()
                                                        ? item.sessionStartedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                                        : QStringLiteral("-")));
        table->setItem(row, 2, new QTableWidgetItem(item.athleteName));
        table->setItem(row, 3, new QTableWidgetItem(videoFileStatusLabel(item.status)));
        table->setItem(row, 4, new QTableWidgetItem(QStringLiteral("#%1").arg(item.videoIndex)));
        table->setItem(row, 5, new QTableWidgetItem(QDir::toNativeSeparators(item.filePath)));
        table->setItem(row, 6, new QTableWidgetItem(storageSizeLabel(item.fileSizeBytes)));
        table->setItem(row, 7, new QTableWidgetItem(QString::number(item.actionCount)));
    }
    table->resizeColumnsToContents();
    layout->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &cleanupDialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("删除勾选文件"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &cleanupDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &cleanupDialog, &QDialog::reject);
    if (cleanupDialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<VideoFileCleanupCandidate> selected;
    qint64 selectedBytes = 0;
    for (int row = 0; row < candidates.size(); ++row) {
        const QTableWidgetItem *checkItem = table->item(row, 0);
        if (!checkItem || checkItem->checkState() != Qt::Checked) {
            continue;
        }
        selected.append(candidates.at(row));
        selectedBytes += candidates.at(row).fileSizeBytes;
    }
    if (selected.isEmpty()) {
        return;
    }

    const int answer = QMessageBox::warning(dialog,
                                            QStringLiteral("确认删除视频文件"),
                                            QStringLiteral("将删除 %1 个本机视频文件，合计约 %2。训练记录不会删除。是否继续？")
                                                .arg(selected.size())
                                                .arg(storageSizeLabel(selectedBytes)),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    int deleted = 0;
    QStringList failures;
    for (const VideoFileCleanupCandidate &item : std::as_const(selected)) {
        const QFileInfo fileInfo(item.filePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }
        if (!QFile::remove(fileInfo.absoluteFilePath())) {
            failures << QDir::toNativeSeparators(fileInfo.absoluteFilePath());
            continue;
        }
        QString error;
        if (!m_trainingRepository->markVideoFileCleaned(item.id, QStringLiteral("manual_cleanup"), &error)) {
            failures << QStringLiteral("%1（索引标记失败：%2）")
                            .arg(QDir::toNativeSeparators(fileInfo.absoluteFilePath()), error);
            continue;
        }
        ++deleted;
    }

    dialog->setVideoStorageStatus(videoStorageStatusSummary(settings));
    if (!failures.isEmpty()) {
        QMessageBox::warning(dialog,
                             QStringLiteral("部分清理失败"),
                             QStringLiteral("已删除 %1 个文件，以下项目失败：\n%2")
                                 .arg(deleted)
                                 .arg(failures.join(QLatin1Char('\n'))));
    } else {
        QMessageBox::information(dialog,
                                 QStringLiteral("清理完成"),
                                 QStringLiteral("已删除 %1 个本机视频文件，训练记录已保留。").arg(deleted));
    }
}

void MainWindow::loadTrainingRecords()
{
    m_historyServiceAvailable = m_trainingRepository && m_trainingRepository->isOpen();
    if (!m_historyServiceAvailable) {
        refreshHistoryPager();
        return;
    }

    SessionSearchPage page;
    page.pageNumber = m_historyPageNumber;
    page.pageSize = m_historyPageSize;
    const SessionSearchResult result = m_trainingRepository->searchSessions(currentHistorySearchFilters(),
                                                                            page,
                                                                            currentHistorySearchSort());
    m_records = result.items;
    m_historyPageNumber = result.pageNumber;
    m_historyPageSize = result.pageSize;
    m_historyTotalCount = result.totalCount;
    m_lastSavedAt = m_records.isEmpty() ? QString() : m_records.first().time;
    refreshHistoryPager();
}

void MainWindow::reloadHistorySearchOptions()
{
    if (!m_historySearchPanel) {
        return;
    }

    const QString previousAthleteId = m_historyAthleteComboBox ? m_historyAthleteComboBox->currentData().toString() : QString();
    const QString previousCoachId = m_historyCoachComboBox ? m_historyCoachComboBox->currentData().toString() : QString();
    const QString previousActionId = m_historyActionComboBox ? m_historyActionComboBox->currentData().toString() : QString();
    const QString previousCompetitionId = m_historyCompetitionComboBox ? m_historyCompetitionComboBox->currentData().toString() : QString();
    const QString previousCompetitionEventId = m_historyCompetitionEventComboBox ? m_historyCompetitionEventComboBox->currentData().toString() : QString();

    if (m_historyAthleteComboBox) {
        QSignalBlocker blocker(m_historyAthleteComboBox);
        m_historyAthleteComboBox->clear();
        m_historyAthleteComboBox->addItem(QStringLiteral("全部运动员"), QString());
        for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
            m_historyAthleteComboBox->addItem(athlete.name, athlete.id);
        }
        const int index = m_historyAthleteComboBox->findData(previousAthleteId);
        m_historyAthleteComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_historyCoachComboBox) {
        QSignalBlocker blocker(m_historyCoachComboBox);
        m_historyCoachComboBox->clear();
        m_historyCoachComboBox->addItem(QStringLiteral("全部教练"), QString());
        for (const CoachProfile &coach : std::as_const(m_coaches)) {
            m_historyCoachComboBox->addItem(coach.name, coach.id);
        }
        const int index = m_historyCoachComboBox->findData(previousCoachId);
        m_historyCoachComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_historyActionComboBox) {
        QSignalBlocker blocker(m_historyActionComboBox);
        m_historyActionComboBox->clear();
        m_historyActionComboBox->addItem(QStringLiteral("全部动作"), QString());
        for (const ActionStandard &standard : std::as_const(m_actionStandards)) {
            m_historyActionComboBox->addItem(QStringLiteral("%1 · %2").arg(standard.categoryName, standard.name),
                                             standard.id);
        }
        const int index = m_historyActionComboBox->findData(previousActionId);
        m_historyActionComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_historyCompetitionComboBox) {
        QSignalBlocker blocker(m_historyCompetitionComboBox);
        m_historyCompetitionComboBox->clear();
        m_historyCompetitionComboBox->addItem(QStringLiteral("全部比赛"), QString());
        for (const Competition &competition : std::as_const(m_competitions)) {
            m_historyCompetitionComboBox->addItem(competition.name, competition.id);
        }
        const int index = m_historyCompetitionComboBox->findData(previousCompetitionId);
        m_historyCompetitionComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_historyCompetitionEventComboBox) {
        QSignalBlocker blocker(m_historyCompetitionEventComboBox);
        m_historyCompetitionEventComboBox->clear();
        m_historyCompetitionEventComboBox->addItem(QStringLiteral("全部场次"), QString());
        const QString competitionId = m_historyCompetitionComboBox ? m_historyCompetitionComboBox->currentData().toString() : QString();
        for (const CompetitionEvent &event : std::as_const(m_competitionEvents)) {
            if (!competitionId.isEmpty() && event.competitionId != competitionId) {
                continue;
            }
            const QString label = QStringLiteral("%1 / %2 / %3 / %4")
                                      .arg(event.raceName.isEmpty() ? QStringLiteral("未填场次") : event.raceName,
                                           event.eventName.isEmpty() ? QStringLiteral("未填项目") : event.eventName,
                                           event.heatName.isEmpty() ? QStringLiteral("未填轮次") : event.heatName,
                                           event.groupName.isEmpty() ? QStringLiteral("未填分组") : event.groupName);
            m_historyCompetitionEventComboBox->addItem(label, event.id);
        }
        const int index = m_historyCompetitionEventComboBox->findData(previousCompetitionEventId);
        m_historyCompetitionEventComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }
}

SessionSearchFilters MainWindow::currentHistorySearchFilters() const
{
    SessionSearchFilters filters;
    if (m_historyAthleteComboBox) {
        filters.athleteId = m_historyAthleteComboBox->currentData().toString();
    }
    if (m_historyCoachComboBox) {
        filters.coachId = m_historyCoachComboBox->currentData().toString();
    }
    if (m_historyActionComboBox) {
        filters.actionStandardId = m_historyActionComboBox->currentData().toString();
    }
    if (m_historyCompetitionComboBox) {
        filters.competitionId = m_historyCompetitionComboBox->currentData().toString();
    }
    if (m_historyCompetitionEventComboBox) {
        filters.competitionEventId = m_historyCompetitionEventComboBox->currentData().toString();
    }
    if (m_historySourceTypeComboBox) {
        filters.sourceType = m_historySourceTypeComboBox->currentData().toString();
    }
    if (m_historyFromCheckBox && m_historyFromCheckBox->isChecked() && m_historyFromDateEdit) {
        filters.savedFrom = QDateTime(m_historyFromDateEdit->date(), QTime(0, 0, 0));
    }
    if (m_historyToCheckBox && m_historyToCheckBox->isChecked() && m_historyToDateEdit) {
        filters.savedTo = QDateTime(m_historyToDateEdit->date(), QTime(23, 59, 59, 999));
    }
    if (m_historyCompetitionLineEdit) {
        filters.competitionText = m_historyCompetitionLineEdit->text();
    }
    if (m_historyMinScoreSpinBox && m_historyMinScoreSpinBox->value() >= 0) {
        filters.minScore = m_historyMinScoreSpinBox->value();
    }
    if (m_historyMaxScoreSpinBox && m_historyMaxScoreSpinBox->value() >= 0) {
        filters.maxScore = m_historyMaxScoreSpinBox->value();
    }
    if (filters.minScore >= 0 && filters.maxScore >= 0 && filters.minScore > filters.maxScore) {
        std::swap(filters.minScore, filters.maxScore);
    }
    return filters;
}

SessionSearchSort MainWindow::currentHistorySearchSort() const
{
    SessionSearchSort sort;
    const int value = m_historySortComboBox ? m_historySortComboBox->currentData().toInt() : 0;
    switch (value) {
    case 1:
        sort.field = SessionSearchSortField::SavedAt;
        sort.descending = false;
        break;
    case 2:
        sort.field = SessionSearchSortField::AverageScore;
        sort.descending = true;
        break;
    case 3:
        sort.field = SessionSearchSortField::AverageScore;
        sort.descending = false;
        break;
    case 4:
        sort.field = SessionSearchSortField::BestScore;
        sort.descending = true;
        break;
    case 5:
        sort.field = SessionSearchSortField::ValidReps;
        sort.descending = true;
        break;
    case 6:
        sort.field = SessionSearchSortField::DurationSec;
        sort.descending = true;
        break;
    case 0:
    default:
        sort.field = SessionSearchSortField::SavedAt;
        sort.descending = true;
        break;
    }
    return sort;
}

void MainWindow::resetHistorySearch()
{
    if (m_historyAthleteComboBox) {
        m_historyAthleteComboBox->setCurrentIndex(0);
    }
    if (m_historyCoachComboBox) {
        m_historyCoachComboBox->setCurrentIndex(0);
    }
    if (m_historyActionComboBox) {
        m_historyActionComboBox->setCurrentIndex(0);
    }
    if (m_historyCompetitionComboBox) {
        m_historyCompetitionComboBox->setCurrentIndex(0);
    }
    if (m_historyCompetitionEventComboBox) {
        m_historyCompetitionEventComboBox->setCurrentIndex(0);
    }
    if (m_historySourceTypeComboBox) {
        m_historySourceTypeComboBox->setCurrentIndex(0);
    }
    if (m_historySortComboBox) {
        m_historySortComboBox->setCurrentIndex(0);
    }
    if (m_historyCompetitionLineEdit) {
        m_historyCompetitionLineEdit->clear();
    }
    if (m_historyMinScoreSpinBox) {
        m_historyMinScoreSpinBox->setValue(-1);
    }
    if (m_historyMaxScoreSpinBox) {
        m_historyMaxScoreSpinBox->setValue(-1);
    }
    if (m_historyFromCheckBox) {
        m_historyFromCheckBox->setChecked(false);
    }
    if (m_historyToCheckBox) {
        m_historyToCheckBox->setChecked(false);
    }
    m_historyPageNumber = 1;
    loadTrainingRecords();
    refreshHistory();
    refreshSuggestions();
}

int MainWindow::historyMaxPage() const
{
    return std::max(1, (m_historyTotalCount + m_historyPageSize - 1) / std::max(1, m_historyPageSize));
}

void MainWindow::refreshHistoryPager()
{
    const int maxPage = historyMaxPage();
    if (m_historyPageLabel) {
        m_historyPageLabel->setText(QStringLiteral("第 %1 / %2 页\n本页 %3 · 共 %4")
                                        .arg(m_historyPageNumber)
                                        .arg(maxPage)
                                        .arg(m_records.size())
                                        .arg(m_historyTotalCount));
    }
    if (m_historyPreviousPageButton) {
        m_historyPreviousPageButton->setEnabled(m_historyServiceAvailable && m_historyPageNumber > 1);
    }
    if (m_historyNextPageButton) {
        m_historyNextPageButton->setEnabled(m_historyServiceAvailable && m_historyPageNumber < maxPage);
    }
}

void MainWindow::initializeTrainingRepository()
{
    if (!m_trainingRepository) {
        return;
    }

    QString errorMessage;
    const bool connected = m_trainingRepository->open(&errorMessage);
    refreshTrainingServiceStatus(QDateTime::currentDateTime());
    if (!connected) {
        ui->saveTipLabel->setText(QStringLiteral("训练服务连接失败：%1").arg(errorMessage));
        ui->saveTipLabel->show();
        qWarning() << "[MainWindow] training service open failed" << errorMessage;
        return;
    }

    reloadTrainingContext();
}

void MainWindow::refreshTrainingServiceStatus(const QDateTime &checkedAt)
{
    const bool connected = m_trainingRepository->isOpen();
    ui->systemStatusLabel->setText(connected ? QStringLiteral("服务 · 已连接")
                                           : QStringLiteral("服务 · 未连接"));
    ui->systemStatusLabel->setProperty("state", connected ? "online" : "error");

    ui->trainingServiceValue1->setText(connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    ui->trainingServiceValue1->setProperty("state", connected ? "success" : "error");
    ui->trainingServiceValue1->setToolTip(connected ? QString() : m_trainingRepository->lastError());
    ui->trainingServiceValue2->setText(connected ? QStringLiteral("正常") : QStringLiteral("未知"));
    ui->trainingServiceValue2->setProperty("state", connected ? "success" : "muted");
    if (checkedAt.isValid()) {
        ui->trainingServiceValue3->setText(checkedAt.toString(QStringLiteral("hh:mm:ss")));
    }
    ui->systemStatusEmptyLabel->hide();

    for (QLabel *label : {ui->systemStatusLabel, ui->trainingServiceValue1, ui->trainingServiceValue2}) {
        repolish(label);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_trainingSettingsDialog && m_trainingSettingsDialog->isVisible()) {
        if (m_settingsAnimation) {
            m_settingsAnimation->stop();
        }
        m_trainingSettingsDialog->hide();
        m_settingsExpanded = false;
        ui->advancedSettingsButton->setProperty("active", false);
    }
    const bool compactHeight = height() < 820;
    ui->mainImageLabel->setMinimumHeight(compactHeight ? 200 : 280);
    updateCameraGrid();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    if (!m_trainingSettingsDialog || !m_trainingSettingsDialog->isVisible()) {
        return;
    }
    if (m_settingsAnimation) {
        m_settingsAnimation->stop();
    }
    m_trainingSettingsDialog->hide();
    m_settingsExpanded = false;
    ui->advancedSettingsButton->setProperty("active", false);
    repolish(ui->advancedSettingsButton);
}

void MainWindow::reloadTrainingContext()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }

    const QString previousAthleteId = selectedAthleteId();
    const QString previousCoachId = selectedCoachId();
    const QString previousCompetitionId = selectedCompetitionId();
    const QString previousCompetitionEventId = selectedCompetitionEventId();
    const QString previousActionId = selectedActionStandard().id;
    QStringList previousParticipantIds;
    for (QComboBox *combo : std::as_const(m_participantComboBoxes)) {
        previousParticipantIds.append(combo ? combo->currentData().toString() : QString());
    }

    m_athletes = m_trainingRepository->athletes();
    m_coaches = m_trainingRepository->coaches();
    m_competitions = m_trainingRepository->competitions();
    m_competitionEvents = m_trainingRepository->competitionEvents();
    m_eventAthletes = m_trainingRepository->eventAthletes();
    m_actionStandards = m_trainingRepository->actionStandards();

    if (m_athleteComboBox) {
        QSignalBlocker blocker(m_athleteComboBox);
        m_athleteComboBox->clear();
        for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
            m_athleteComboBox->addItem(athlete.name, athlete.id);
        }
        const int index = m_athleteComboBox->findData(previousAthleteId);
        if (index >= 0) {
            m_athleteComboBox->setCurrentIndex(index);
        }
        if (m_drawerAthleteComboBox) {
            const QSignalBlocker drawerBlocker(m_drawerAthleteComboBox);
            m_drawerAthleteComboBox->setCurrentIndex(m_athleteComboBox->currentIndex());
        }
    }

    if (m_coachComboBox) {
        QSignalBlocker blocker(m_coachComboBox);
        m_coachComboBox->clear();
        for (const CoachProfile &coach : std::as_const(m_coaches)) {
            m_coachComboBox->addItem(coach.name, coach.id);
        }
        const int index = m_coachComboBox->findData(previousCoachId);
        if (index >= 0) {
            m_coachComboBox->setCurrentIndex(index);
        }
    }

    if (m_competitionComboBox) {
        QSignalBlocker blocker(m_competitionComboBox);
        m_competitionComboBox->clear();
        m_competitionComboBox->addItem(QStringLiteral("未关联比赛"), QString());
        for (const Competition &competition : std::as_const(m_competitions)) {
            m_competitionComboBox->addItem(competition.name, competition.id);
        }
        const int index = m_competitionComboBox->findData(previousCompetitionId);
        m_competitionComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_competitionEventComboBox) {
        QSignalBlocker blocker(m_competitionEventComboBox);
        m_competitionEventComboBox->clear();
        m_competitionEventComboBox->addItem(QStringLiteral("未关联场次"), QString());
        const QString competitionId = selectedCompetitionId();
        for (const CompetitionEvent &event : std::as_const(m_competitionEvents)) {
            if (!competitionId.isEmpty() && event.competitionId != competitionId) {
                continue;
            }
            const QString label = QStringLiteral("%1 / %2 / %3 / %4")
                                      .arg(event.raceName.isEmpty() ? QStringLiteral("未填场次") : event.raceName,
                                           event.eventName.isEmpty() ? QStringLiteral("未填项目") : event.eventName,
                                           event.heatName.isEmpty() ? QStringLiteral("未填轮次") : event.heatName,
                                           event.groupName.isEmpty() ? QStringLiteral("未填分组") : event.groupName);
            m_competitionEventComboBox->addItem(label, event.id);
        }
        const int index = m_competitionEventComboBox->findData(previousCompetitionEventId);
        m_competitionEventComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_actionStandardComboBox) {
        QSignalBlocker blocker(m_actionStandardComboBox);
        m_actionStandardComboBox->clear();
        for (const ActionStandard &standard : std::as_const(m_actionStandards)) {
            m_actionStandardComboBox->addItem(QStringLiteral("%1 · %2").arg(standard.categoryName, standard.name),
                                              standard.id);
        }
        const int index = m_actionStandardComboBox->findData(previousActionId);
        if (index >= 0) {
            m_actionStandardComboBox->setCurrentIndex(index);
        }
    }

    for (int i = 0; i < m_participantComboBoxes.size(); ++i) {
        QComboBox *combo = m_participantComboBoxes.at(i);
        if (!combo) {
            continue;
        }
        QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(QStringLiteral("未选择"), QString());
        const QString primaryId = selectedAthleteId();
        for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
            if (athlete.id == primaryId) {
                continue;
            }
            combo->addItem(athlete.name, athlete.id);
        }
        const QString previousId = i < previousParticipantIds.size() ? previousParticipantIds.at(i) : QString();
        const int index = combo->findData(previousId);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    }

    reloadHistorySearchOptions();
    reloadAthleteIdentityGallery();
    refreshTrainingContextDetails();
}

void MainWindow::reloadAthleteIdentityGallery()
{
    if (!m_athleteAnalysisManager || !m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }
    const QString modelVersion = QStringLiteral("personvit-msmt17-vit-base-v1");
    const QString preprocessingVersion = QStringLiteral("rgb-256x128-mean0.5-std0.5-l2-v1");
    QVector<AthleteGalleryEntry> gallery;
    for (const TrainingSessionParticipant &participant : currentSessionParticipants()) {
        const QVector<AthleteIdentityEmbedding> embeddings = m_trainingRepository->identityGallery(participant.athleteId,
                                                                                                    modelVersion,
                                                                                                    preprocessingVersion);
        for (const AthleteIdentityEmbedding &embedding : embeddings) {
            AthleteGalleryEntry entry;
            entry.athleteId = embedding.athleteId;
            entry.participantId = participant.id;
            entry.label = participant.athleteName;
            entry.embedding = embedding.embedding;
            gallery.append(entry);
        }
    }
    m_athleteAnalysisManager->setGallery(gallery);
    m_manualIdentityBindings.clear();
    m_athleteAnalysisManager->setManualBindings({});
    m_athleteAnalysisManager->resetTracking();
}

void MainWindow::editManualIdentityBindings()
{
    const AthleteFrameResult bindingFrame = m_lastAthleteFrame;
    if (bindingFrame.instances.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("暂无可绑定目标"),
                                 QStringLiteral("请先开始采集并等待运动员检测结果。"));
        return;
    }

    const QVector<TrainingSessionParticipant> participants = currentSessionParticipants();
    if (participants.isEmpty()) {
        QMessageBox::information(this,
                                 QStringLiteral("未选择参与者"),
                                 QStringLiteral("请先在训练上下文中选择 session 参与运动员。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("人工绑定运动员身份"));
    dialog.resize(760, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *tip = new QLabel(QStringLiteral("人工绑定只作用于当前机位的 track，优先用于低置信度或未知身份。"), &dialog);
    tip->setProperty("role", "muted");
    tip->setWordWrap(true);
    layout->addWidget(tip);

    auto *table = new QTableWidget(bindingFrame.instances.size(), 4, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("机位"), QStringLiteral("trackId"), QStringLiteral("当前身份"), QStringLiteral("绑定到")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    for (int row = 0; row < bindingFrame.instances.size(); ++row) {
        const AthleteInstance &instance = bindingFrame.instances.at(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(bindingFrame.cameraId)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(instance.trackId)));
        table->setItem(row, 2, new QTableWidgetItem(instance.label.isEmpty() ? QStringLiteral("未识别") : instance.label));
        auto *combo = new QComboBox(table);
        combo->addItem(QStringLiteral("自动 / 取消绑定"), QString());
        for (const TrainingSessionParticipant &participant : participants) {
            combo->addItem(participant.athleteName.isEmpty() ? participant.athleteId : participant.athleteName,
                           participant.athleteId);
        }
        for (const AthleteIdentityBinding &binding : std::as_const(m_manualIdentityBindings)) {
            if (binding.cameraId == bindingFrame.cameraId && binding.trackId == instance.trackId) {
                const int index = combo->findData(binding.athleteId);
                if (index >= 0) {
                    combo->setCurrentIndex(index);
                }
                break;
            }
        }
        table->setCellWidget(row, 3, combo);
    }
    table->resizeColumnsToContents();
    layout->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<AthleteIdentityBinding> bindings;
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *combo = qobject_cast<QComboBox *>(table->cellWidget(row, 3));
        if (!combo || combo->currentData().toString().isEmpty()) {
            continue;
        }
        const QString athleteId = combo->currentData().toString();
        QString label = combo->currentText();
        AthleteIdentityBinding binding;
        binding.cameraId = bindingFrame.cameraId;
        binding.trackId = table->item(row, 1)->text().toInt();
        binding.athleteId = athleteId;
        binding.label = label;
        for (const TrainingSessionParticipant &participant : participants) {
            if (participant.athleteId == athleteId) {
                binding.participantId = participant.id;
                break;
            }
        }
        bindings.append(binding);
    }
    m_manualIdentityBindings = bindings;
    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->setManualBindings(m_manualIdentityBindings);
    }
    ui->saveTipLabel->setText(QStringLiteral("人工身份绑定已更新。"));
    ui->saveTipLabel->show();
}

void MainWindow::refreshTrainingContextDetails()
{
    const ActionStandard standard = selectedActionStandard();
    if (!standard.id.isEmpty()) {
        if (m_targetRepsSpinBox && m_targetRepsSpinBox->value() <= 1) {
            m_targetRepsSpinBox->setValue(std::max(1, standard.targetReps));
        } else if (m_targetRepsSpinBox && !m_isRecording) {
            m_targetRepsSpinBox->setValue(std::max(1, standard.targetReps));
        }
        if (m_targetScoreSpinBox && !m_isRecording) {
            m_targetScoreSpinBox->setValue(std::clamp(standard.targetScore, 1, 100));
        }
        if (m_setCountSpinBox && !m_isRecording) {
            m_setCountSpinBox->setValue(std::max(1, standard.setCount));
        }
        if (m_restSecondsSpinBox && !m_isRecording) {
            m_restSecondsSpinBox->setValue(std::max(0, standard.restSeconds));
        }
        if (m_standardDetailLabel) {
            m_standardDetailLabel->setText(QStringLiteral("v%1 · %2 · %3 · %4")
                                               .arg(standard.version)
                                               .arg(standard.level)
                                               .arg(standard.purpose)
                                               .arg(standard.keyPoints));
        }
    } else if (m_standardDetailLabel) {
        m_standardDetailLabel->setText(QStringLiteral("暂无可用动作标准"));
    }
    refreshStats();
}

void MainWindow::addAthleteFromDialog()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("新增运动员"),
                                               QStringLiteral("运动员姓名"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    QString athleteId;
    QString errorMessage;
    if (!m_trainingRepository->createAthlete(name, &athleteId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), errorMessage);
        return;
    }
    reloadTrainingContext();
    if (m_athleteComboBox) {
        const int index = m_athleteComboBox->findData(athleteId);
        if (index >= 0) {
            m_athleteComboBox->setCurrentIndex(index);
        }
    }
}

void MainWindow::addCoachFromDialog()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("新增教练"),
                                               QStringLiteral("教练姓名"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    QString coachId;
    QString errorMessage;
    if (!m_trainingRepository->createCoach(name, &coachId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), errorMessage);
        return;
    }
    reloadTrainingContext();
    if (m_coachComboBox) {
        const int index = m_coachComboBox->findData(coachId);
        if (index >= 0) {
            m_coachComboBox->setCurrentIndex(index);
        }
    }
}

void MainWindow::openPersonManagement()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("训练数据库未就绪，无法管理人员。"));
        return;
    }

    const QString previousAthleteId = selectedAthleteId();
    const QString previousCoachId = selectedCoachId();
    PersonManagementDialog dialog(m_trainingRepository.get(), this);
    installModalScrim(&dialog);
    dialog.exec();
    if (!dialog.changed()) {
        return;
    }

    reloadTrainingContext();
    loadTrainingRecords();
    refreshHistory();
    refreshSuggestions();
    if (m_athleteComboBox) {
        const int index = m_athleteComboBox->findData(previousAthleteId);
        if (index >= 0) {
            m_athleteComboBox->setCurrentIndex(index);
        }
    }
    if (m_coachComboBox) {
        const int index = m_coachComboBox->findData(previousCoachId);
        if (index >= 0) {
            m_coachComboBox->setCurrentIndex(index);
        }
    }
    ui->saveTipLabel->setText(QStringLiteral("人员档案已更新。"));
    ui->saveTipLabel->show();
}

void MainWindow::openCompetitionManagement()
{
    const QString previousCompetitionId = selectedCompetitionId();
    const QString previousCompetitionEventId = selectedCompetitionEventId();
    CompetitionManagementDialog managementDialog(m_trainingRepository.get(), this);
    managementDialog.exec();
    if (!managementDialog.changed()) {
        return;
    }

    reloadTrainingContext();
    loadTrainingRecords();
    refreshHistory();
    refreshSuggestions();
    if (m_competitionComboBox) {
        const int index = m_competitionComboBox->findData(previousCompetitionId);
        m_competitionComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_competitionEventComboBox) {
        const int index = m_competitionEventComboBox->findData(previousCompetitionEventId);
        m_competitionEventComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }
    ui->saveTipLabel->setText(QStringLiteral("比赛信息已更新。"));
    ui->saveTipLabel->show();
    return;
}

ActionStandard MainWindow::selectedActionStandard() const
{
    if (!m_actionStandardComboBox) {
        return {};
    }
    const QString selectedId = m_actionStandardComboBox->currentData().toString();
    for (const ActionStandard &standard : m_actionStandards) {
        if (standard.id == selectedId) {
            return standard;
        }
    }
    return {};
}

QString MainWindow::selectedAthleteId() const
{
    return m_athleteComboBox ? m_athleteComboBox->currentData().toString() : QString();
}

QString MainWindow::selectedCoachId() const
{
    return m_coachComboBox ? m_coachComboBox->currentData().toString() : QString();
}

QVector<TrainingSessionParticipant> MainWindow::currentSessionParticipants() const
{
    QVector<TrainingSessionParticipant> participants;
    auto appendParticipant = [&](const QString &athleteId, int slotIndex, const QString &role) {
        if (athleteId.trimmed().isEmpty()) {
            return;
        }
        for (const TrainingSessionParticipant &existing : std::as_const(participants)) {
            if (existing.athleteId == athleteId) {
                return;
            }
        }
        TrainingSessionParticipant participant;
        participant.athleteId = athleteId;
        participant.slotIndex = slotIndex;
        participant.role = role;
        for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
            if (athlete.id == athleteId) {
                participant.athleteName = athlete.name;
                break;
            }
        }
        participants.append(participant);
    };

    appendParticipant(selectedAthleteId(), 1, QStringLiteral("primary"));
    for (QComboBox *combo : m_participantComboBoxes) {
        appendParticipant(combo ? combo->currentData().toString() : QString(), participants.size() + 1, QStringLiteral("participant"));
        if (participants.size() >= 4) {
            break;
        }
    }
    return participants;
}

QString MainWindow::selectedCompetitionId() const
{
    return m_competitionComboBox ? m_competitionComboBox->currentData().toString() : QString();
}

QString MainWindow::selectedCompetitionEventId() const
{
    return m_competitionEventComboBox ? m_competitionEventComboBox->currentData().toString() : QString();
}

QString MainWindow::selectedEventAthleteId() const
{
    const QString eventId = selectedCompetitionEventId();
    const QString athleteId = selectedAthleteId();
    if (eventId.isEmpty() || athleteId.isEmpty()) {
        return {};
    }
    for (const EventAthlete &eventAthlete : std::as_const(m_eventAthletes)) {
        if (eventAthlete.eventId == eventId && eventAthlete.athleteId == athleteId) {
            return eventAthlete.id;
        }
    }
    return {};
}

void MainWindow::resetCurrentTrainingSession()
{
    m_durationSec = 0;
    m_actionCount = 0;
    m_validActionCount = 0;
    m_bestActionScore = 0;
    m_actionScoreTotal = 0;
    m_currentTrackPoints.clear();
    m_currentSpeedMetrics.clear();
    m_trackPointSampleTimes.clear();
    m_latestTrackPoints.clear();
    refreshTrajectoryView();
    m_manualIdentityBindings.clear();
    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->setManualBindings({});
        m_athleteAnalysisManager->resetTracking();
    }
    m_recordingStartedAtMsec = QDateTime::currentMSecsSinceEpoch();
    m_recordingStartedAt = QDateTime::currentDateTime();
}

void MainWindow::recordAthleteFrames(const AthleteFrameResult &athleteFrame)
{
    if (!m_isRecording || m_isPaused || athleteFrame.instances.isEmpty()) {
        return;
    }
    const qint64 timestampMs = athleteFrame.timestampMs > 0
                                   ? athleteFrame.timestampMs
                                   : std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - m_recordingStartedAtMsec);
    for (const AthleteInstance &instance : athleteFrame.instances) {
        if (instance.athleteId.trimmed().isEmpty()
            || athleteFrame.cameraId <= 0
            || athleteFrame.cameraId > m_cameraSlotSettings.size()) {
            continue;
        }
        const QString pointKey = QStringLiteral("p:%1:c:%2").arg(instance.athleteId).arg(athleteFrame.cameraId);
        const qint64 previousPointTime = m_trackPointSampleTimes.value(pointKey, -kAthleteTimelineSampleIntervalMs);
        if (timestampMs - previousPointTime < kAthleteTimelineSampleIntervalMs) {
            continue;
        }
        QPointF fieldPoint;
        const QPointF iceContact(instance.box.center().x(), instance.box.bottom());
        if (!mapImagePointToField(m_cameraSlotSettings.at(athleteFrame.cameraId - 1), iceContact, &fieldPoint)) {
            continue;
        }
        m_trackPointSampleTimes.insert(pointKey, timestampMs);
        TrackPoint point;
        point.participantId = instance.athleteId;
        point.timestampMs = timestampMs;
        point.x = fieldPoint.x();
        point.y = fieldPoint.y();
        point.z = 0.0;
        point.speedSource = QStringLiteral("position_delta");
        point.cameraId = athleteFrame.cameraId;
        point.confidence = instance.detectionConfidence;
        m_currentTrackPoints.append(point);

        const TrackPoint previousPoint = m_latestTrackPoints.value(pointKey);
        const qint64 elapsedMs = point.timestampMs - previousPoint.timestampMs;
        SpeedMetric speed;
        speed.participantId = point.participantId;
        speed.timestampMs = point.timestampMs;
        speed.cameraId = point.cameraId;
        speed.smoothingWindowMs = kSpeedSmoothingWindowMs;
        speed.algorithmVersion = QString::fromLatin1(kSpeedAlgorithmVersion);
        speed.valid = previousPoint.timestampMs > 0
                      && elapsedMs >= kAthleteTimelineSampleIntervalMs
                      && elapsedMs <= 2000
                      && point.confidence >= 0.0;
        if (speed.valid) {
            speed.instantaneousSpeedMps = std::hypot(point.x - previousPoint.x, point.y - previousPoint.y)
                                          / (static_cast<double>(elapsedMs) / 1000.0);
            double speedTotal = speed.instantaneousSpeedMps;
            int speedCount = 1;
            for (auto metric = m_currentSpeedMetrics.crbegin(); metric != m_currentSpeedMetrics.crend(); ++metric) {
                if (metric->participantId != speed.participantId || metric->cameraId != speed.cameraId) {
                    continue;
                }
                if (speed.timestampMs - metric->timestampMs > speed.smoothingWindowMs) {
                    break;
                }
                if (metric->valid) {
                    speedTotal += metric->instantaneousSpeedMps;
                    ++speedCount;
                }
            }
            speed.smoothedSpeedMps = speedTotal / speedCount;
        }
        m_currentSpeedMetrics.append(speed);
        m_latestTrackPoints.insert(pointKey, point);
    }
    while (m_currentSpeedMetrics.size() > kMaxTimelineSamples) {
        m_currentSpeedMetrics.removeFirst();
    }
    refreshTrajectoryView();
}

void MainWindow::refreshTrajectoryView()
{
    if (!m_trajectoryWidget) {
        return;
    }
    QVector<TrajectoryWidget::CameraSegment> segments;
    segments.reserve(m_cameraSlotSettings.size());
    for (int index = 0; index < m_cameraSlotSettings.size(); ++index) {
        const CameraSlotSettings &slot = m_cameraSlotSettings.at(index);
        TrajectoryWidget::CameraSegment segment;
        segment.cameraId = index + 1;
        segment.enabled = slot.trajectoryEnabled;
        segment.role = slot.role;
        segment.fieldStartM = slot.fieldStartM;
        segment.fieldEndM = slot.fieldEndM;
        segments.append(segment);
    }
    m_trajectoryWidget->setCameraSegments(segments);
    const QString primaryParticipantId = selectedAthleteId();
    QVector<TrackPoint> points;
    for (const TrackPoint &point : std::as_const(m_currentTrackPoints)) {
        if (primaryParticipantId.isEmpty() || point.participantId == primaryParticipantId) {
            points.append(point);
        }
    }
    m_trajectoryWidget->setTrackPoints(points);
}

void MainWindow::importOfflineVideo()
{
    if (m_isRecording) {
        QMessageBox::information(this,
                                 QStringLiteral("正在训练"),
                                 QStringLiteral("请先保存或停止当前训练，再导入离线视频。"));
        return;
    }

    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("offlineVideo/lastDir"), QDir::homePath()).toString();
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          QStringLiteral("导入离线视频"),
                                                          lastDir,
                                                          QStringLiteral("视频文件 (*.mp4 *.mov *.avi *.mkv *.m4v *.wmv *.flv *.webm);;所有文件 (*.*)"));
    if (filePath.trimmed().isEmpty()) {
        return;
    }

    if (!m_analysisTaskManager) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("后台任务服务未就绪。"));
        return;
    }
    const QFileInfo fileInfo(filePath);
    settings.setValue(QStringLiteral("offlineVideo/lastDir"), fileInfo.absolutePath());
    const QString taskId = m_analysisTaskManager->enqueueOfflineImport(fileInfo.absoluteFilePath());
    ui->saveTipLabel->setText(QStringLiteral("离线视频已进入后台队列：任务 %1。完成准备后将自动切换到该视频。")
                                  .arg(taskId.left(8)));
    ui->saveTipLabel->show();
}

void MainWindow::openOfflineAnalysisManager()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("服务不可用"), QStringLiteral("训练服务未连接，无法管理完整帧率分析任务。"));
        return;
    }
    QVector<QString> athleteIds;
    for (const TrainingSessionParticipant &participant : currentSessionParticipants()) {
        if (!participant.athleteId.trimmed().isEmpty() && !athleteIds.contains(participant.athleteId)) {
            athleteIds.append(participant.athleteId);
        }
    }
    OfflineAnalysisDialog dialog(m_trainingRepository.get(), athleteIds, m_analysisTaskManager.get(), this);
    dialog.exec();
    if (!dialog.batchId().isEmpty()) {
        m_offlineAnalysisBatchId = dialog.batchId();
        m_offlineAnalysisRunId = dialog.activeRunId();
    }
}

void MainWindow::openAnalysisTaskCenter()
{
    if (!m_analysisTaskManager) return;
    AnalysisTaskCenterDialog dialog(m_analysisTaskManager.get(), this);
    connect(&dialog, &AnalysisTaskCenterDialog::openSessionRequested, this, [this](const QString &) {
        switchPage(kHistoryPage);
        refreshHistory();
    });
    dialog.exec();
}

void MainWindow::showOfflineVideoInMainView(bool autoPlay)
{
    clearOfflineAnalysisOverlay();
    if (m_offlineVideoPath.trimmed().isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(m_offlineVideoPath);
    m_offlineVideoName = offlineVideoDisplayName(m_offlineVideoPath);
    m_selectedCamera = 0;
    clearRealtimeAnalysisFrame();

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(QStringLiteral("离线视频\n文件不存在"));
        ui->saveTipLabel->setText(QStringLiteral("离线视频文件不存在：%1").arg(QDir::toNativeSeparators(m_offlineVideoPath)));
        ui->saveTipLabel->show();
        if (m_athleteAnalysisManager) {
            m_athleteAnalysisManager->setPaused(true);
            m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
        }
    } else if (!autoPlay) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(m_offlineVideoName);
        if (m_athleteAnalysisManager) {
            m_athleteAnalysisManager->setPaused(true);
            m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
        }
    } else {
        ui->mainImageLabel->setPlaceholderText(m_offlineVideoName);
        ui->mainImageLabel->playFile(m_offlineVideoPath);
        syncAnalysisStreams();
    }

    ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(m_offlineVideoName));
    refreshCameraButtons();
    refreshStats();
}

// 将指定摄像头主码流显示到主视图；小窗预览仍保持子码流。
void MainWindow::showCameraInMainView(int cameraIndex, bool autoPlay)
{
    clearOfflineAnalysisOverlay();
    if (cameraIndex < 0 || cameraIndex >= m_cameraButtons.size()) {
        return;
    }

    m_offlineVideoPath.clear();
    m_offlineVideoName.clear();
    m_offlineVideoProbe = OfflineVideoProbeResult();
    m_offlineAnalysisTask = OfflineAnalysisTask();
    auto *cameraWidget = m_cameraButtons.at(cameraIndex);
    m_selectedCamera = cameraIndex + 1;
    const QString source = cameraWidget->mainUrl();
    clearRealtimeAnalysisFrame();
    if (source.trimmed().isEmpty()) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(QStringLiteral("主视频\n未配置"));
        if (m_athleteAnalysisManager) {
            m_athleteAnalysisManager->setPaused(true);
            m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
        }
    } else if (!autoPlay) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(cameraWidget->channelName());
        if (m_athleteAnalysisManager) {
            m_athleteAnalysisManager->setPaused(true);
            m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
        }
    } else {
        ui->mainImageLabel->setPlaceholderText(cameraWidget->channelName());
        ui->mainImageLabel->playMainUrlWithFallback(source, cameraWidget->previewUrl());
        syncAnalysisStreams();
    }

    ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(cameraWidget->channelName()));
    refreshCameraButtons();
    refreshStats();
}

// 切换主页面堆栈；pageIndex 对应实时采集、历史分析和纠正建议等页面。
void MainWindow::switchPage(int pageIndex)
{
    if (!ui->pages || pageIndex < 0 || pageIndex >= ui->pages->count()) {
        return;
    }
    if (m_activePage == pageIndex) {
        return;
    }
    if (m_trainingSettingsDialog && m_trainingSettingsDialog->isVisible()) {
        m_trainingSettingsDialog->hide();
        m_settingsExpanded = false;
        ui->advancedSettingsButton->setProperty("active", false);
    }

    if (pageIndex == kHistoryPage) {
        refreshHistory();
    }

    ui->pages->setCurrentIndex(pageIndex);
    m_activePage = pageIndex;
    const QString pageTitle = pageIndex == kHistoryPage
                                  ? QStringLiteral("历史复盘")
                                  : (pageIndex == kSuggestionPage
                                         ? QStringLiteral("训练洞察")
                                         : QStringLiteral("实时训练"));
    ui->sessionRoundLabel->setText(pageIndex == kSystemStatusPage ? QStringLiteral("系统状态") : pageTitle);
    ui->systemStatusSubtitleLabel->setVisible(pageIndex == kSystemStatusPage);
    m_environmentCheckButton->setVisible(pageIndex == kSystemStatusPage);
    m_environmentCheckLabel->setVisible(pageIndex == kSystemStatusPage);
    ui->focusTitleLabel->setVisible(pageIndex != kSystemStatusPage);
    ui->brandLogoLabelShell->setVisible(pageIndex == kCapturePage && m_isRecording);
    refreshNavButtons();

    auto *transition = new QFrame(ui->pages);
    transition->setObjectName(QStringLiteral("pageTransitionOverlay"));
    transition->setAttribute(Qt::WA_TransparentForMouseEvents);
    transition->setStyleSheet(QStringLiteral("background: #080B10; border: none;"));
    transition->setGeometry(ui->pages->rect());
    transition->raise();
    transition->show();
    auto *effect = new QGraphicsOpacityEffect(transition);
    effect->setOpacity(1.0);
    transition->setGraphicsEffect(effect);
    auto *fade = new QVariantAnimation(transition);
    fade->setDuration(140);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(fade, &QVariantAnimation::valueChanged, transition, [effect](const QVariant &value) {
        effect->setOpacity(value.toReal());
    });
    connect(fade, &QVariantAnimation::finished, transition, &QWidget::deleteLater);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->start();
    if (pageIndex == kSuggestionPage) {
        if (!m_refreshingSuggestions) {
            prepareSuggestionContext(true);
        }
        refreshSuggestions();
    }

}

// 在固定的展开/折叠宽度之间过渡，导航文字保持布局位置只淡出。
void MainWindow::toggleSidebar()
{
    m_sidebarVisible = !m_sidebarVisible;
    if (!m_sidebarAnimation) {
        m_sidebarAnimation = new QVariantAnimation(this);
        m_sidebarAnimation->setDuration(200);
        m_sidebarAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_sidebarAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            const int width = value.toInt();
            if (ui->sidebar->minimumWidth() != width || ui->sidebar->maximumWidth() != width) {
                ui->sidebar->setFixedWidth(width);
            }
            const qreal textOpacity = (width - 64.0) / 156.0;
            for (QPushButton *button : {ui->navCaptureButton,
                                        ui->navHistoryButton,
                                        ui->navSuggestionButton,
                                        ui->navSystemStatusButton,
                                        ui->personManagementButton,
                                        ui->competitionManagementButton,
                                        ui->settingsButton}) {
                button->setProperty("textOpacity", textOpacity);
            }
            for (QLabel *label : {ui->brandTitleLabel, ui->brandSubtitleLabel, ui->sidebarSectionLabel, ui->sidebarToolsLabel, ui->sidebarSystemLabel}) {
                if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(label->graphicsEffect())) {
                    effect->setOpacity(textOpacity);
                }
            }
        });
    }
    m_sidebarAnimation->stop();
    m_sidebarAnimation->setStartValue(ui->sidebar->width());
    m_sidebarAnimation->setEndValue(m_sidebarVisible ? 220 : 64);
    m_sidebarAnimation->start();
    refreshSidebarButton();
}

// 切换全屏状态：未全屏时记录原窗口状态并进入全屏，已全屏时恢复原状态。
void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        exitFullScreenMode();
        return;
    }

    setProperty(kPreviousWindowStateProperty,
                static_cast<int>((windowState() & ~Qt::WindowFullScreen).toInt()));
    setWindowState(windowState() | Qt::WindowFullScreen);
    refreshFullScreenButton();
}

// 退出全屏：Esc 快捷键和全屏按钮都会复用这里，保证恢复逻辑一致。
void MainWindow::exitFullScreenMode()
{
    if (!isFullScreen()) {
        refreshFullScreenButton();
        return;
    }

    const auto previousState = static_cast<Qt::WindowStates>(
        property(kPreviousWindowStateProperty).toInt());
    setWindowState(previousState & ~Qt::WindowFullScreen);
    refreshFullScreenButton();
}

// 记录当前选择的摄像头编号，并在后续采集/保存时作为当前通道使用。
void MainWindow::selectCamera(int cameraId)
{
    if (cameraId < 1 || cameraId > m_cameraButtons.size()) {
        return;
    }

    showCameraInMainView(cameraId - 1);
}

// 开始采集：主视图接入第一路视频流，12 路预览分别接入各自配置的真实视频流。
void MainWindow::startCapture()
{
    qDebug() << "[MainWindow] startCapture clicked";
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        ui->saveTipLabel->setText(QStringLiteral("训练数据库未就绪，无法开始标准闭环采集。"));
        ui->saveTipLabel->show();
        return;
    }
    if (selectedAthleteId().isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("请先选择或新增运动员。"));
        ui->saveTipLabel->show();
        return;
    }
    if (selectedActionStandard().id.isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("请先选择训练动作标准。"));
        ui->saveTipLabel->show();
        return;
    }
    const bool useOfflineVideo = !m_offlineVideoPath.trimmed().isEmpty();
    if (useOfflineVideo) {
        const QFileInfo offlineFile(m_offlineVideoPath);
        if (!offlineFile.exists() || !offlineFile.isFile()) {
            ui->saveTipLabel->setText(QStringLiteral("离线视频文件不存在，请重新导入。"));
            ui->saveTipLabel->show();
            return;
        }
        if (!sameOfflineProbeFile(m_offlineVideoProbe, offlineFile)) {
            ui->saveTipLabel->setText(QStringLiteral("离线视频文件已变化或尚未通过校验，请重新导入。"));
            ui->saveTipLabel->show();
            return;
        }
    } else {
        const bool hasCamera = std::any_of(m_cameraSlotSettings.cbegin(),
                                           m_cameraSlotSettings.cend(),
                                           [](const CameraSlotSettings &slot) {
                                               return !slot.ip.trimmed().isEmpty();
                                           });
        if (!hasCamera) {
            ui->saveTipLabel->setText(QStringLiteral("请先在系统设置中至少配置一路相机 IP。"));
            ui->saveTipLabel->show();
            return;
        }
    }
    if (!m_isRecording) {
        resetCurrentTrainingSession();
    }
    m_isRecording = true;
    m_isPaused = false;
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->setPaused(false);
    }
    if (useOfflineVideo) {
        qDebug() << "[MainWindow] offline video stream" << QDir::toNativeSeparators(m_offlineVideoPath);
        for (auto *videoWidget : m_cameraButtons) {
            videoWidget->stopPlayback();
        }
        showOfflineVideoInMainView(true);
        syncAnalysisStreams();
        ui->saveTipLabel->setText(QStringLiteral("离线视频分析中：%1").arg(QFileInfo(m_offlineVideoPath).fileName()));
        ui->saveTipLabel->show();
        refreshStats();
        return;
    }
    if (!m_cameraButtons.isEmpty()) {
        qDebug() << "[MainWindow] main view stream" << safeUrlForLog(m_cameraButtons.first()->mainUrl());
        showCameraInMainView(0);
    }
    for (auto *videoWidget : m_cameraButtons) {
        qDebug() << "[MainWindow] camera stream"
                 << videoWidget->channelName()
                 << safeUrlForLog(videoWidget->previewUrl());
        videoWidget->playDefaultVideo();
    }
    syncAnalysisStreams();
    ui->saveTipLabel->setText(cameraReadinessSummary());
    ui->saveTipLabel->show();
    refreshStats();
}

// 暂停采集：暂停主视图和全部摄像头预览的播放器状态。
void MainWindow::pauseCapture()
{
    qDebug() << "[MainWindow] pauseCapture clicked";
    if (!m_isRecording || m_isPaused) {
        return;
    }
    m_isPaused = true;
    m_timer.stop();
    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->setPaused(true);
        m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
    }
    clearRealtimeAnalysisFrame();
    ui->mainImageLabel->pausePlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->pausePlayback();
    }
    refreshStats();
}

// 停止采集：停止主视图和全部摄像头预览，并恢复未播放占位状态。
void MainWindow::stopCapture()
{
    qDebug() << "[MainWindow] stopCapture clicked";
    m_isRecording = false;
    m_isPaused = false;
    m_timer.stop();
    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->setPaused(true);
        m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
    }
    clearRealtimeAnalysisFrame();
    ui->mainImageLabel->stopPlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->stopPlayback();
    }
    refreshCameraButtons();
    refreshStats();
}

void MainWindow::openRepetitionSearchDialog()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("动作检索"), QStringLiteral("训练数据库未就绪。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("动作明细检索"));
    dialog.resize(1180, 720);
    auto *layout = new QVBoxLayout(&dialog);
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    auto *athleteCombo = new QComboBox(&dialog);
    auto *coachCombo = new QComboBox(&dialog);
    auto *actionCombo = new QComboBox(&dialog);
    auto *competitionCombo = new QComboBox(&dialog);
    auto *eventCombo = new QComboBox(&dialog);
    auto *sourceTypeCombo = new QComboBox(&dialog);
    auto *validCombo = new QComboBox(&dialog);
    auto *reviewCombo = new QComboBox(&dialog);
    auto *repSourceCombo = new QComboBox(&dialog);
    auto *errorEdit = new QLineEdit(&dialog);
    auto *minScore = new QSpinBox(&dialog);
    auto *maxScore = new QSpinBox(&dialog);
    auto *clipFrom = new QSpinBox(&dialog);
    auto *clipTo = new QSpinBox(&dialog);
    auto *fromCheck = new QCheckBox(QStringLiteral("开始"), &dialog);
    auto *toCheck = new QCheckBox(QStringLiteral("结束"), &dialog);
    auto *fromDate = new QDateEdit(QDate::currentDate().addMonths(-1), &dialog);
    auto *toDate = new QDateEdit(QDate::currentDate(), &dialog);
    auto *statusLabel = new QLabel(QStringLiteral("共 0 条"), &dialog);
    auto *table = new QTableWidget(&dialog);
    auto *queryButton = new QPushButton(QStringLiteral("查询"), &dialog);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), &dialog);
    auto *exportCsvButton = new QPushButton(QStringLiteral("导出 CSV"), &dialog);
    auto *exportXlsxButton = new QPushButton(QStringLiteral("导出 XLSX"), &dialog);

    athleteCombo->addItem(QStringLiteral("全部运动员"), QString());
    for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
        athleteCombo->addItem(athlete.name, athlete.id);
    }
    coachCombo->addItem(QStringLiteral("全部教练"), QString());
    for (const CoachProfile &coach : std::as_const(m_coaches)) {
        coachCombo->addItem(coach.name, coach.id);
    }
    actionCombo->addItem(QStringLiteral("全部动作"), QString());
    for (const ActionStandard &standard : std::as_const(m_actionStandards)) {
        actionCombo->addItem(QStringLiteral("%1 · %2").arg(standard.categoryName, standard.name), standard.id);
    }
    competitionCombo->addItem(QStringLiteral("全部比赛"), QString());
    for (const Competition &competition : std::as_const(m_competitions)) {
        competitionCombo->addItem(competition.name, competition.id);
    }
    sourceTypeCombo->addItem(QStringLiteral("全部来源"), QString());
    sourceTypeCombo->addItem(QStringLiteral("训练"), QStringLiteral("training"));
    sourceTypeCombo->addItem(QStringLiteral("比赛"), QStringLiteral("competition"));
    sourceTypeCombo->addItem(QStringLiteral("导入视频"), QStringLiteral("offline_import"));
    validCombo->addItem(QStringLiteral("全部有效性"), QStringLiteral("all"));
    validCombo->addItem(QStringLiteral("有效"), QStringLiteral("valid"));
    validCombo->addItem(QStringLiteral("无效"), QStringLiteral("invalid"));
    reviewCombo->addItem(QStringLiteral("全部复核"), QString());
    reviewCombo->addItem(QStringLiteral("未复核"), QStringLiteral("unreviewed"));
    reviewCombo->addItem(QStringLiteral("已复核"), QStringLiteral("reviewed"));
    reviewCombo->addItem(QStringLiteral("已调整"), QStringLiteral("adjusted"));
    repSourceCombo->addItem(QStringLiteral("全部动作来源"), QString());
    repSourceCombo->addItem(QStringLiteral("AI"), QStringLiteral("ai"));
    repSourceCombo->addItem(QStringLiteral("教练手动"), QStringLiteral("coach"));
    errorEdit->setPlaceholderText(QStringLiteral("错误项/反馈/教练备注关键词"));
    for (QSpinBox *spin : {minScore, maxScore}) {
        spin->setRange(-1, 100);
        spin->setSpecialValueText(QStringLiteral("不限"));
        spin->setValue(-1);
    }
    for (QSpinBox *spin : {clipFrom, clipTo}) {
        spin->setRange(-1, 24 * 60 * 60 * 1000);
        spin->setSpecialValueText(QStringLiteral("不限"));
        spin->setValue(-1);
        spin->setSuffix(QStringLiteral(" ms"));
    }
    for (QDateEdit *dateEdit : {fromDate, toDate}) {
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        dateEdit->setEnabled(false);
    }

    auto reloadEvents = [&]() {
        const QString previous = eventCombo->currentData().toString();
        eventCombo->clear();
        eventCombo->addItem(QStringLiteral("全部场次"), QString());
        const QString competitionId = competitionCombo->currentData().toString();
        for (const CompetitionEvent &event : std::as_const(m_competitionEvents)) {
            if (!competitionId.isEmpty() && event.competitionId != competitionId) {
                continue;
            }
            eventCombo->addItem(QStringLiteral("%1 / %2 / %3 / %4")
                                    .arg(event.raceName.isEmpty() ? QStringLiteral("未填场次") : event.raceName,
                                         event.eventName.isEmpty() ? QStringLiteral("未填项目") : event.eventName,
                                         event.heatName.isEmpty() ? QStringLiteral("未填轮次") : event.heatName,
                                         event.groupName.isEmpty() ? QStringLiteral("未填分组") : event.groupName),
                                event.id);
        }
        const int index = eventCombo->findData(previous);
        eventCombo->setCurrentIndex(index >= 0 ? index : 0);
    };
    reloadEvents();

    grid->addWidget(new QLabel(QStringLiteral("运动员"), &dialog), 0, 0);
    grid->addWidget(athleteCombo, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("教练"), &dialog), 0, 2);
    grid->addWidget(coachCombo, 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("动作"), &dialog), 0, 4);
    grid->addWidget(actionCombo, 0, 5);
    grid->addWidget(new QLabel(QStringLiteral("比赛"), &dialog), 1, 0);
    grid->addWidget(competitionCombo, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("场次"), &dialog), 1, 2);
    grid->addWidget(eventCombo, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("来源"), &dialog), 1, 4);
    grid->addWidget(sourceTypeCombo, 1, 5);
    grid->addWidget(new QLabel(QStringLiteral("有效性"), &dialog), 2, 0);
    grid->addWidget(validCombo, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("复核"), &dialog), 2, 2);
    grid->addWidget(reviewCombo, 2, 3);
    grid->addWidget(new QLabel(QStringLiteral("动作来源"), &dialog), 2, 4);
    grid->addWidget(repSourceCombo, 2, 5);
    grid->addWidget(new QLabel(QStringLiteral("分数"), &dialog), 3, 0);
    grid->addWidget(minScore, 3, 1);
    grid->addWidget(maxScore, 3, 2);
    grid->addWidget(new QLabel(QStringLiteral("片段"), &dialog), 3, 3);
    grid->addWidget(clipFrom, 3, 4);
    grid->addWidget(clipTo, 3, 5);
    grid->addWidget(fromCheck, 4, 0);
    grid->addWidget(fromDate, 4, 1);
    grid->addWidget(toCheck, 4, 2);
    grid->addWidget(toDate, 4, 3);
    grid->addWidget(new QLabel(QStringLiteral("关键词"), &dialog), 4, 4);
    grid->addWidget(errorEdit, 4, 5);
    layout->addLayout(grid);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addWidget(statusLabel, 1);
    buttonRow->addWidget(queryButton);
    buttonRow->addWidget(resetButton);
    buttonRow->addWidget(exportCsvButton);
    buttonRow->addWidget(exportXlsxButton);
    layout->addLayout(buttonRow);

    table->setColumnCount(14);
    table->setHorizontalHeaderLabels({QStringLiteral("时间"),
                                      QStringLiteral("运动员"),
                                      QStringLiteral("身份"),
                                      QStringLiteral("轨迹"),
                                      QStringLiteral("机位"),
                                      QStringLiteral("动作"),
                                      QStringLiteral("比赛/场次"),
                                      QStringLiteral("视频"),
                                      QStringLiteral("片段"),
                                      QStringLiteral("有效分"),
                                      QStringLiteral("有效性"),
                                      QStringLiteral("错误项"),
                                      QStringLiteral("反馈"),
                                      QStringLiteral("视频源")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);

    QVector<RepetitionSearchItem> currentItems;
    RepetitionSearchFilters currentFilters;
    const int pageSize = 200;

    auto filtersFromUi = [&]() {
        RepetitionSearchFilters filters;
        filters.athleteId = athleteCombo->currentData().toString();
        filters.coachId = coachCombo->currentData().toString();
        filters.actionStandardId = actionCombo->currentData().toString();
        filters.competitionId = competitionCombo->currentData().toString();
        filters.competitionEventId = eventCombo->currentData().toString();
        filters.sourceType = sourceTypeCombo->currentData().toString();
        filters.validState = validCombo->currentData().toString();
        filters.reviewStatus = reviewCombo->currentData().toString();
        filters.repetitionSource = repSourceCombo->currentData().toString();
        filters.errorText = errorEdit->text();
        if (fromCheck->isChecked()) {
            filters.savedFrom = QDateTime(fromDate->date(), QTime(0, 0, 0));
        }
        if (toCheck->isChecked()) {
            filters.savedTo = QDateTime(toDate->date(), QTime(23, 59, 59, 999));
        }
        filters.minScore = minScore->value();
        filters.maxScore = maxScore->value();
        filters.clipFromMs = clipFrom->value();
        filters.clipToMs = clipTo->value();
        if (filters.minScore >= 0 && filters.maxScore >= 0 && filters.minScore > filters.maxScore) {
            std::swap(filters.minScore, filters.maxScore);
        }
        if (filters.clipFromMs >= 0 && filters.clipToMs >= 0 && filters.clipFromMs > filters.clipToMs) {
            std::swap(filters.clipFromMs, filters.clipToMs);
        }
        return filters;
    };

    auto fillTable = [&]() {
        table->setRowCount(currentItems.size());
        for (int row = 0; row < currentItems.size(); ++row) {
            const RepetitionSearchItem &item = currentItems.at(row);
            const QString competition = QStringLiteral("%1 / %2")
                                            .arg(item.competitionName.trimmed().isEmpty() ? QStringLiteral("未关联比赛") : item.competitionName.trimmed(),
                                                 item.raceName.trimmed().isEmpty() ? QStringLiteral("未关联场次") : item.raceName.trimmed());
            const QString clip = QStringLiteral("%1-%2")
                                     .arg(formatMilliseconds(item.effectiveStartedMsValue),
                                          formatMilliseconds(item.effectiveEndedMsValue));
            const QString videoIndex = QStringLiteral("#%1 %2")
                                           .arg(item.videoIndex)
                                           .arg(item.videoFileStatus.trimmed().isEmpty()
                                                    ? QStringLiteral("未登记")
                                                    : videoFileStatusLabel(item.videoFileStatus));
            const QStringList values = {item.time,
                                        item.athleteName,
                                        identityStatusLabel(item.identityStatus),
                                        item.trackId >= 0 ? QString::number(item.trackId) : QStringLiteral("-"),
                                        item.cameraId >= 0 ? QString::number(item.cameraId) : QStringLiteral("-"),
                                        QStringLiteral("%1/%2").arg(item.actionCategory, item.actionName),
                                        competition,
                                        videoIndex,
                                        clip,
                                        QString::number(item.effectiveScoreValue),
                                        boolText(item.effectiveValidValue),
                                        issueSummary(item.effectiveErrorCodesValue),
                                        item.effectiveFeedbackValue,
                                        displayMediaSource(item.videoSource)};
            for (int col = 0; col < values.size(); ++col) {
                auto *cell = new QTableWidgetItem(values.at(col));
                if (col == 9) {
                    cell->setTextAlignment(Qt::AlignCenter);
                }
                table->setItem(row, col, cell);
            }
        }
        table->resizeColumnsToContents();
    };

    auto runQuery = [&]() {
        currentFilters = filtersFromUi();
        SessionSearchPage page;
        page.pageNumber = 1;
        page.pageSize = pageSize;
        const RepetitionSearchResult result = m_trainingRepository->searchRepetitions(currentFilters, page);
        currentItems = result.items;
        statusLabel->setText(QStringLiteral("显示 %1 条 · 共 %2 条").arg(currentItems.size()).arg(result.totalCount));
        fillTable();
    };

    auto fetchAll = [&]() {
        QVector<RepetitionSearchItem> all;
        SessionSearchPage page;
        page.pageSize = 1000;
        for (page.pageNumber = 1; page.pageNumber <= 1000; ++page.pageNumber) {
            const RepetitionSearchResult result = m_trainingRepository->searchRepetitions(currentFilters, page);
            all += result.items;
            if (all.size() >= result.totalCount || result.items.isEmpty()) {
                break;
            }
        }
        return all;
    };

    auto writeCsv = [&](const QString &filePath, const QVector<RepetitionSearchItem> &items) {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        QStringList headers;
        for (const QString &header : repetitionExportHeaders()) {
            headers << csvField(header);
        }
        out << headers.join(QLatin1Char(',')) << "\n";
        for (const RepetitionSearchItem &item : items) {
            QStringList row;
            for (const QString &value : repetitionExportRow(item)) {
                row << csvField(value);
            }
            out << row.join(QLatin1Char(',')) << "\n";
        }
        return true;
    };

    auto writeXlsx = [&](const QString &filePath, const QVector<RepetitionSearchItem> &items) {
        QXlsx::Document xlsx;
        QXlsx::Format headerFormat;
        headerFormat.setFontBold(true);
        headerFormat.setPatternBackgroundColor(QColor(QStringLiteral("#eef3f9")));
        const QStringList headers = repetitionExportHeaders();
        for (int col = 0; col < headers.size(); ++col) {
            xlsx.write(1, col + 1, headers.at(col), headerFormat);
            xlsx.setColumnWidth(col + 1, 18);
        }
        for (int row = 0; row < items.size(); ++row) {
            const QStringList values = repetitionExportRow(items.at(row));
            for (int col = 0; col < values.size(); ++col) {
                xlsx.write(row + 2, col + 1, values.at(col));
            }
        }
        return xlsx.saveAs(filePath);
    };

    auto exportData = [&](const QString &suffix) {
        currentFilters = filtersFromUi();
        const QVector<RepetitionSearchItem> all = fetchAll();
        const QString filter = suffix == QStringLiteral("xlsx") ? QStringLiteral("Excel 工作簿 (*.xlsx)") : QStringLiteral("CSV (*.csv)");
        QString filePath = QFileDialog::getSaveFileName(&dialog,
                                                        QStringLiteral("导出动作明细"),
                                                        QDir::homePath() + QStringLiteral("/repetitions.") + suffix,
                                                        filter);
        if (filePath.isEmpty()) {
            return;
        }
        filePath = withFileSuffix(filePath, suffix);
        const bool ok = suffix == QStringLiteral("xlsx") ? writeXlsx(filePath, all) : writeCsv(filePath, all);
        if (!ok) {
            QMessageBox::warning(&dialog, QStringLiteral("导出失败"), QStringLiteral("无法写入文件。"));
            return;
        }
        statusLabel->setText(QStringLiteral("已导出 %1 条：%2").arg(all.size()).arg(QDir::toNativeSeparators(filePath)));
    };

    connect(competitionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, reloadEvents);
    connect(fromCheck, &QCheckBox::toggled, fromDate, &QDateEdit::setEnabled);
    connect(toCheck, &QCheckBox::toggled, toDate, &QDateEdit::setEnabled);
    connect(queryButton, &QPushButton::clicked, &dialog, runQuery);
    connect(resetButton, &QPushButton::clicked, &dialog, [&]() {
        athleteCombo->setCurrentIndex(0);
        coachCombo->setCurrentIndex(0);
        actionCombo->setCurrentIndex(0);
        competitionCombo->setCurrentIndex(0);
        sourceTypeCombo->setCurrentIndex(0);
        validCombo->setCurrentIndex(0);
        reviewCombo->setCurrentIndex(0);
        repSourceCombo->setCurrentIndex(0);
        minScore->setValue(-1);
        maxScore->setValue(-1);
        clipFrom->setValue(-1);
        clipTo->setValue(-1);
        errorEdit->clear();
        fromCheck->setChecked(false);
        toCheck->setChecked(false);
        runQuery();
    });
    connect(exportCsvButton, &QPushButton::clicked, &dialog, [&]() { exportData(QStringLiteral("csv")); });
    connect(exportXlsxButton, &QPushButton::clicked, &dialog, [&]() { exportData(QStringLiteral("xlsx")); });
    connect(table, &QTableWidget::cellDoubleClicked, &dialog, [&](int row, int) {
        if (row < 0 || row >= currentItems.size()) {
            return;
        }
        const RepetitionSearchItem item = currentItems.at(row);
        SessionHistoryItem record;
        record.id = item.sessionId;
        record.startedAt = item.startedAt;
        record.videoSource = item.videoSource;
        record.videoFallbackSource = item.videoFallbackSource;
        record.videoCameraName = item.videoCameraName;
        TrainingVideoFile videoFile;
        videoFile.id = item.videoFileId;
        videoFile.videoIndex = item.videoIndex;
        videoFile.status = item.videoFileStatus;
        videoFile.filePath = item.videoFilePath;
        record.videoFiles.append(videoFile);
        openSessionVideo(record, item.videoClipStartMs, item.videoClipEndMs, item.videoFileId, item.videoIndex);
    });

    runQuery();
    dialog.exec();
}

// 保存当前训练记录；后续可在此持久化训练时长、动作数量和模型评分。
void MainWindow::saveRecord()
{
    if (m_durationSec <= 0) {
        ui->saveTipLabel->setText(QStringLiteral("暂无可保存的训练记录，请先开始采集并累计训练时长。"));
        ui->saveTipLabel->show();
        return;
    }

    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        ui->saveTipLabel->setText(QStringLiteral("训练数据库未就绪，无法保存记录。"));
        ui->saveTipLabel->show();
        return;
    }

    const QString athleteId = selectedAthleteId();
    const QString coachId = selectedCoachId();
    QString competitionId = selectedCompetitionId();
    const QString competitionEventId = selectedCompetitionEventId();
    const QString eventAthleteId = selectedEventAthleteId();
    const ActionStandard standard = selectedActionStandard();
    if (athleteId.isEmpty() || standard.id.isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("保存前需要选择运动员和动作标准。"));
        ui->saveTipLabel->show();
        return;
    }

    QString errorMessage;
    const int targetReps = m_targetRepsSpinBox ? m_targetRepsSpinBox->value() : standard.targetReps;
    const int targetScore = m_targetScoreSpinBox ? m_targetScoreSpinBox->value() : standard.targetScore;
    const int setCount = m_setCountSpinBox ? m_setCountSpinBox->value() : standard.setCount;
    const int restSeconds = m_restSecondsSpinBox ? m_restSecondsSpinBox->value() : standard.restSeconds;
    const QString site = m_siteLineEdit ? m_siteLineEdit->text().trimmed() : QString();
    const QString trainingPhase = m_trainingPhaseComboBox ? m_trainingPhaseComboBox->currentText() : QString();
    const QString goal = m_goalLineEdit ? m_goalLineEdit->text().trimmed() : QString();
    const QString trainingNotes = m_trainingNotesEdit ? m_trainingNotesEdit->toPlainText().trimmed() : QString();
    for (const CompetitionEvent &event : std::as_const(m_competitionEvents)) {
        if (event.id == competitionEventId) {
            competitionId = event.competitionId;
            break;
        }
    }

    TrainingSession session;
    session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.athleteId = athleteId;
    session.coachId = coachId;
    session.competitionId = competitionId;
    session.competitionEventId = competitionEventId;
    session.eventAthleteId = eventAthleteId;
    session.actionStandardId = standard.id;
    session.standardVersion = standard.version;
    session.startedAt = m_recordingStartedAt.isValid() ? m_recordingStartedAt : QDateTime::currentDateTime().addSecs(-m_durationSec);
    session.savedAt = QDateTime::currentDateTime();
    session.durationSec = m_durationSec;
    session.totalReps = 0;
    session.validReps = 0;
    session.averageScore = 0;
    session.bestScore = 0;
    const bool offlineSession = !m_offlineVideoPath.trimmed().isEmpty() && m_selectedCamera == 0;
    session.camera = offlineSession ? 0 : m_selectedCamera;
    session.modelPrecision = ui->precisionComboBox
                                 ? ui->precisionComboBox->currentData().toString()
                                 : QStringLiteral("balanced");
    if (session.modelPrecision.isEmpty()) {
        session.modelPrecision = QStringLiteral("balanced");
    }
    session.fps = m_capturePreferenceSettings.analysisTargetFps > 0
                      ? m_capturePreferenceSettings.analysisTargetFps
                      : kDefaultAnalysisTargetFps;
    if (session.fps <= 0) {
        session.fps = kDefaultAnalysisTargetFps;
    }
    session.detectionScore = 0;
    session.symmetryScore = 0;
    session.balanceScore = 0;
    session.stabilityScore = 0;
    session.depthScore = 0;
    session.site = site;
    session.trainingPhase = trainingPhase;
    session.goal = goal;
    session.targetReps = targetReps;
    session.targetScore = targetScore;
    session.setCount = setCount;
    session.restSeconds = restSeconds;
    if (offlineSession) {
        session.videoSource = m_offlineVideoPath.trimmed();
        session.videoFallbackSource.clear();
        session.videoCameraName = m_offlineVideoName.trimmed().isEmpty()
                                      ? offlineVideoDisplayName(m_offlineVideoPath)
                                      : m_offlineVideoName.trimmed();
        session.analysisTaskId = m_offlineAnalysisTask.id;
    } else if (m_selectedCamera > 0 && m_selectedCamera <= m_cameraButtons.size()) {
        const VideoOpenGLWidget *cameraWidget = m_cameraButtons.at(m_selectedCamera - 1);
        session.videoSource = cameraWidget->mainUrl().trimmed();
        session.videoFallbackSource = cameraWidget->previewUrl().trimmed();
        session.videoCameraName = cameraWidget->channelName();
    }
    session.analysisBatchId = m_offlineAnalysisBatchId;
    session.analysisRunId = m_offlineAnalysisRunId;
    QString athleteName;
    for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
        if (athlete.id == athleteId) {
            athleteName = athlete.name;
            break;
        }
    }
    VideoStoragePlanInput videoPlan;
    videoPlan.sessionId = session.id;
    videoPlan.athleteId = athleteId;
    videoPlan.athleteName = athleteName;
    videoPlan.startedAt = session.startedAt;
    videoPlan.camera = session.camera;
    videoPlan.cameraName = session.videoCameraName;
    videoPlan.sourceUrl = session.videoSource;
    videoPlan.fallbackUrl = session.videoFallbackSource;
    videoPlan.externalFile = offlineSession;
    videoPlan.durationSec = session.durationSec;
    session.videoFiles = {buildTrainingVideoFilePlan(videoPlan)};
    if (!session.analysisBatchId.isEmpty() && m_trainingRepository) {
        QString batchError;
        const OfflineAnalysisBatch analysisBatch = m_trainingRepository->offlineAnalysisBatch(session.analysisBatchId, &batchError);
        if (analysisBatch.sources.size() == 12) {
            session.videoFiles.clear();
            for (const OfflineAnalysisBatchSource &source : analysisBatch.sources) {
                TrainingVideoFile file;
                file.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                file.sessionId = session.id;
                file.videoIndex = source.cameraId;
                file.camera = source.cameraId;
                file.cameraName = QStringLiteral("CAM %1").arg(source.cameraId, 2, 10, QLatin1Char('0'));
                file.sourceUrl = source.sourceUri;
                file.fileName = source.fileName;
                file.status = QStringLiteral("external");
                file.durationMs = source.durationMs;
                file.sessionEndMs = source.durationMs;
                file.fileSizeBytes = source.fileSizeBytes;
                file.fileModifiedAt = source.fileModifiedAt;
                file.metadataJson = QString::fromUtf8(QJsonDocument(QJsonObject{
                    {QStringLiteral("analysisBatchId"), session.analysisBatchId},
                    {QStringLiteral("cameraId"), source.cameraId},
                    {QStringLiteral("sourceUri"), source.sourceUri}
                }).toJson(QJsonDocument::Compact));
                session.videoFiles.append(file);
            }
            session.videoSource = analysisBatch.sources.first().sourceUri;
            session.videoCameraName = QStringLiteral("12 路同步录像");
            session.camera = 1;
            session.sourceType = QStringLiteral("offline_import");
            session.sourceRef = session.analysisBatchId;
        }
    }
    if (!competitionEventId.isEmpty()) {
        session.sourceType = QStringLiteral("competition");
        session.sourceRef = competitionEventId;
    } else if (!competitionId.isEmpty()) {
        session.sourceType = QStringLiteral("competition");
        session.sourceRef = competitionId;
    } else if (!session.analysisBatchId.isEmpty()) {
        session.sourceType = QStringLiteral("offline_import");
        session.sourceRef = session.analysisBatchId;
    } else if (offlineSession && !session.videoSource.trimmed().isEmpty()) {
        session.sourceType = QStringLiteral("offline_import");
        session.sourceRef = session.analysisTaskId.trimmed().isEmpty()
                                ? session.videoSource
                                : session.analysisTaskId;
    } else {
        session.sourceType = QStringLiteral("training");
        session.sourceRef.clear();
    }
    session.feedback = QStringLiteral("运动员检测与身份识别");
    session.notes = trainingNotes;
    session.participants = currentSessionParticipants();

    const QVector<ActionRepetition> repetitions;
    session.participantRepetitions.clear();
    session.trackPoints = m_currentTrackPoints;
    session.speedMetrics = m_currentSpeedMetrics;
    if (!m_trainingRepository->saveTrainingSession(&session, repetitions, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    loadTrainingRecords();
    m_lastSavedAt = session.savedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    ui->saveTipLabel->setText(QStringLiteral("最近保存：%1").arg(m_lastSavedAt));
    ui->saveTipLabel->show();

    refreshHistory();
    refreshSuggestions();
    refreshStats();
}

// 训练计时器回调：用于累计训练时长、刷新统计数据和实时反馈。
void MainWindow::tick()
{
    if (!m_isRecording || m_isPaused) {
        return;
    }
    ++m_durationSec;
    refreshStats();
}

// 根据当前页面刷新左侧导航按钮的选中/普通状态。
void MainWindow::refreshNavButtons()
{
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (QPushButton *button = m_navButtons.at(i)) {
            button->setProperty("active", i == m_activePage);
            repolish(button);
        }
    }
}

// 根据当前选择的摄像头刷新 12 路预览控件的选中状态。
void MainWindow::refreshCameraButtons()
{
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        cameraWidget->setSelected(i + 1 == m_selectedCamera);
        repolish(cameraWidget);
    }
}

// 刷新实时训练状态、身份检查器和训练操作可用性。
void MainWindow::refreshStats()
{
    refreshAnalysisTaskStatus();
    refreshCameraRuntimeStatus();
    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    const bool streamPlaying = ui->mainImageLabel && ui->mainImageLabel->isPlaying();
    const bool selectedFrameFresh = m_lastSelectedFrameReceivedAtMsec > 0
                                    && nowMsec - m_lastSelectedFrameReceivedAtMsec <= 2000;
    if (!m_lastAthleteFrame.instances.isEmpty() && (!selectedFrameFresh || !streamPlaying)) {
        clearRealtimeAnalysisFrame();
        return;
    }

    ui->actionValueLabel->setText(QString::number(m_lastAthleteFrame.instances.size()));
    ui->durationValueLabel->setText(formatTime(m_durationSec));
    if (m_trainingTargetLabel) {
        m_trainingTargetLabel->setText(QStringLiteral("训练记录仅保存检测框、身份状态、trackId 和置信度"));
    }
    ui->sessionTimeLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm")));
    QString source;
    if (m_selectedCamera > 0 && m_selectedCamera <= m_cameraButtons.size()) {
        source = m_cameraButtons.at(m_selectedCamera - 1)->channelName().trimmed();
        if (source.isEmpty()) {
            source = QStringLiteral("CAM %1").arg(pad(m_selectedCamera));
        }
    } else {
        source = m_offlineVideoName.trimmed();
        if (source.isEmpty()) {
            source = QStringLiteral("离线视频");
        }
    }
    ui->athleteSourceLabel->setText(source);
    if (ui->mainImageLabel && ui->mainImageLabel->channelName() != source) {
        ui->mainImageLabel->setChannelName(source);
    }
    const QString athleteName = m_athleteComboBox && m_athleteComboBox->currentIndex() >= 0
                                    ? m_athleteComboBox->currentText()
                                    : QStringLiteral("未选择");
    ui->focusTitleLabel->setText(QStringLiteral("%1 · %2").arg(athleteName, source));
    ui->legendContentLabel->setText(athleteName);
    if (m_athleteNameLabel) {
        m_athleteNameLabel->setText(athleteName);
    }

    const QString selectedId = selectedAthleteId();
    const AthleteInstance *selectedInstance = nullptr;
    for (const AthleteInstance &instance : std::as_const(m_lastAthleteFrame.instances)) {
        if (!selectedId.isEmpty() && instance.athleteId == selectedId) {
            selectedInstance = &instance;
            break;
        }
    }
    bool awaitingCalibration = false;
    if (selectedInstance
        && m_lastAthleteFrame.cameraId > 0
        && m_lastAthleteFrame.cameraId <= m_cameraSlotSettings.size()) {
        QPointF fieldPoint;
        const QPointF iceContact(selectedInstance->box.center().x(), selectedInstance->box.bottom());
        awaitingCalibration = !mapImagePointToField(
            m_cameraSlotSettings.at(m_lastAthleteFrame.cameraId - 1), iceContact, &fieldPoint);
    }
    if (m_trajectoryWidget) {
        m_trajectoryWidget->setAwaitingCalibration(awaitingCalibration);
    }
    if (m_lastAthleteFrame.instances.isEmpty()) {
        ui->athleteIdentityLabel->setText(QStringLiteral("● 尚未识别"));
        ui->athleteDetectionLabel->setText(QStringLiteral("0"));
        ui->athleteIdentityLabel->setProperty("state", "muted");
        ui->athleteDetectionLabel->setProperty("state", "muted");
        if (m_speedStatusLabel) {
            m_speedStatusLabel->setText(QStringLiteral("—"));
            m_speedStatusLabel->setProperty("state", "muted");
        }
    } else {
        if (selectedInstance) {
            const QString identity = !selectedInstance->label.trimmed().isEmpty()
                                          ? selectedInstance->label.trimmed()
                                          : identityStatusLabel(selectedInstance->identityStatus);
            ui->athleteIdentityLabel->setText(QStringLiteral("● %1").arg(identity));
            ui->athleteIdentityLabel->setProperty("state", "online");
            ui->athleteDetectionLabel->setText(QStringLiteral("%1")
                                                    .arg(m_lastAthleteFrame.instances.size()));
            ui->athleteDetectionLabel->setToolTip(QStringLiteral("检测置信度 %1%")
                                                       .arg(qRound(selectedInstance->detectionConfidence * 100.0f)));
            ui->athleteDetectionLabel->setProperty("state", "online");
        } else {
            ui->athleteIdentityLabel->setText(QStringLiteral("● 尚未识别"));
            ui->athleteIdentityLabel->setProperty("state", "muted");
            ui->athleteDetectionLabel->setText(QStringLiteral("%1")
                                                    .arg(m_lastAthleteFrame.instances.size()));
            ui->athleteDetectionLabel->setProperty("state", "warning");
        }
        if (m_speedStatusLabel) {
            const SpeedMetric *latestSpeed = nullptr;
            if (selectedInstance) {
                for (auto metric = m_currentSpeedMetrics.crbegin();
                     metric != m_currentSpeedMetrics.crend();
                     ++metric) {
                    if (metric->valid && metric->participantId == selectedId
                        && metric->cameraId == m_lastAthleteFrame.cameraId
                        && m_lastAthleteFrame.timestampMs > 0
                        && metric->timestampMs > 0
                        && m_lastAthleteFrame.timestampMs >= metric->timestampMs
                        && m_lastAthleteFrame.timestampMs - metric->timestampMs <= 2000) {
                        latestSpeed = &(*metric);
                        break;
                    }
                }
            }
            if (latestSpeed) {
                m_speedStatusLabel->setText(QStringLiteral("%1 m/s")
                                                .arg(latestSpeed->smoothedSpeedMps, 0, 'f', 2));
                m_speedStatusLabel->setProperty("state", "online");
            } else {
                m_speedStatusLabel->setText(QStringLiteral("—"));
                m_speedStatusLabel->setProperty("state", "muted");
            }
        }
    }
    const QString trainingState = !m_isRecording
                                      ? (m_durationSec > 0 ? QStringLiteral("训练状态 · 已停止")
                                                          : QStringLiteral("训练状态 · 未开始"))
                                      : (m_isPaused ? QStringLiteral("训练状态 · 已暂停")
                                                    : QStringLiteral("训练状态 · 训练中"));
    const char *stateRole = !m_isRecording ? "muted" : (m_isPaused ? "warning" : "online");
    if (m_trainingStateLabel) {
        m_trainingStateLabel->setText(trainingState);
        m_trainingStateLabel->setProperty("state", stateRole);
    }
    ui->brandLogoLabelShell->setVisible(m_activePage == kCapturePage && m_isRecording);
    ui->startCaptureButton->setText(m_isRecording && m_isPaused
                                         ? QStringLiteral("继续训练")
                                         : QStringLiteral("开始训练"));
    ui->startCaptureButton->setEnabled(!m_isRecording || m_isPaused);
    ui->pauseCaptureButton->setEnabled(m_isRecording && !m_isPaused);
    ui->stopCaptureButton->setEnabled(m_isRecording);
    ui->saveRecordButton->setEnabled(m_durationSec > 0
                                     && m_trainingRepository
                                     && m_trainingRepository->isOpen());
    repolish(ui->athleteIdentityLabel);
    repolish(ui->athleteDetectionLabel);
    if (m_trainingStateLabel) {
        repolish(m_trainingStateLabel);
    }
    if (m_speedStatusLabel) {
        repolish(m_speedStatusLabel);
    }
}

const SessionHistoryItem *MainWindow::historySessionById(const QString &sessionId) const
{
    for (const SessionHistoryItem &record : m_records) {
        if (record.id == sessionId) {
            return &record;
        }
    }
    return nullptr;
}

bool MainWindow::historySessionHasPlayableVideo(const SessionHistoryItem &record) const
{
    const NvrPlaybackResult nvr = buildNvrPlaybackUrl(m_sharedCameraSettings,
                                                      m_cameraSlotSettings,
                                                      record,
                                                      -1,
                                                      -1);
    if (!nvr.url.trimmed().isEmpty()
        || !record.videoSource.trimmed().isEmpty()
        || !record.videoFallbackSource.trimmed().isEmpty()) {
        return true;
    }
    if (!record.analysisBatchId.trimmed().isEmpty() && !record.videoFiles.isEmpty()) {
        const TrainingVideoFile &video = record.videoFiles.first();
        return !video.filePath.trimmed().isEmpty() || !video.sourceUrl.trimmed().isEmpty();
    }
    return false;
}

void MainWindow::selectHistorySession(const QString &sessionId)
{
    m_selectedHistorySessionId = historySessionById(sessionId) ? sessionId : QString();
    refreshHistorySessionDetail();
}

void MainWindow::refreshHistorySessionDetail()
{
    if (!m_historyDetailContent || !m_historyDetailEmptyState) {
        return;
    }

    const SessionHistoryItem *record = historySessionById(m_selectedHistorySessionId);
    m_historyDetailContent->setVisible(record != nullptr);
    m_historyDetailEmptyState->setVisible(record == nullptr);
    if (!record) {
        m_historyDetailAthleteLabel->clear();
        m_historyDetailTimeLabel->clear();
        m_historyDetailSourceLabel->clear();
        m_historyDetailSummaryLabel->clear();
        m_historyDetailTrainingInfoLabel->clear();
        m_historyDetailSourceInfoLabel->clear();
        m_historyDetailCommentLabel->clear();
        m_historyDetailCommentLabel->setToolTip(QString());
        for (QPushButton *button : {m_historyPlayButton,
                                    m_historyReviewButton,
                                    m_historyTrackButton,
                                    m_historyCommentButton,
                                    m_historyExportButton}) {
            button->setEnabled(false);
        }
        m_historyPlayButton->setToolTip(QStringLiteral("当前训练没有可用录像"));
        const bool serviceUnavailable = !m_historyServiceAvailable && m_records.isEmpty();
        m_historyEmptyTitleLabel->setText(serviceUnavailable
                                              ? QStringLiteral("训练服务未连接，暂时无法读取历史记录。")
                                              : QStringLiteral("没有符合条件的训练记录"));
        m_historyEmptyBodyLabel->setText(serviceUnavailable
                                             ? QStringLiteral("服务恢复后可继续查询历史训练。")
                                             : QStringLiteral("尝试调整筛选条件后重新查询。"));
        m_historyEmptyResetButton->setVisible(!serviceUnavailable);
        return;
    }

    auto valueOrDash = [](const QString &value) {
        const QString trimmed = value.trimmed();
        return trimmed.isEmpty() ? QStringLiteral("—") : trimmed;
    };
    auto sourceDetails = [this, &valueOrDash](const SessionHistoryItem &item) {
        QString auxiliary = item.sourceLabel.trimmed();
        if (auxiliary.isEmpty() && item.sourceType == QStringLiteral("competition")) {
            auxiliary = !item.raceName.trimmed().isEmpty() ? item.raceName.trimmed() : item.competitionName.trimmed();
        }
        if (auxiliary.isEmpty()) {
            auxiliary = item.videoCameraName.trimmed();
        }
        if (auxiliary.isEmpty() && item.camera > 0) {
            auxiliary = QStringLiteral("CAM %1").arg(pad(item.camera));
        }
        return auxiliary;
    };

    const QString sourceType = sessionSourceTypeLabel(record->sourceType);
    const QString auxiliarySource = sourceDetails(*record);
    const QString dateTime = record->startedAt.isValid()
                                 ? record->startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                 : valueOrDash(record->time);
    m_historyDetailAthleteLabel->setText(valueOrDash(record->athleteName));
    m_historyDetailTimeLabel->setText(dateTime);
    m_historyDetailSourceLabel->setText(auxiliarySource.isEmpty()
                                            ? sourceType
                                            : QStringLiteral("%1 · %2").arg(sourceType, auxiliarySource));
    m_historyDetailSummaryLabel->setText(
        QStringLiteral("训练时长  %1\n总动作数  %2\n有效动作数  %3\n平均分  %4\n最佳分  %5")
            .arg(formatTime(record->duration))
            .arg(record->totalReps)
            .arg(record->validReps)
            .arg(record->score)
            .arg(record->bestScore));

    const QString actionStandard = record->actionName.trimmed().isEmpty()
                                       ? QStringLiteral("—")
                                       : QStringLiteral("%1 · v%2").arg(record->actionName.trimmed()).arg(record->standardVersion);
    m_historyDetailTrainingInfoLabel->setText(
        QStringLiteral("教练  %1\n场地  %2\n训练阶段  %3\n训练目标  %4\n动作标准  %5\n比赛  %6\n比赛场次  %7")
            .arg(valueOrDash(record->coachName),
                 valueOrDash(record->site),
                 valueOrDash(record->trainingPhase),
                 valueOrDash(record->goal),
                 actionStandard,
                 valueOrDash(record->competitionName),
                 valueOrDash(record->raceName)));
    m_historyDetailSourceInfoLabel->setText(
        QStringLiteral("类型  %1\n来源说明  %2\n录像机位  %3\n摄像头  %4")
            .arg(sourceType,
                 valueOrDash(record->sourceLabel),
                 valueOrDash(record->videoCameraName),
                 record->camera > 0 ? QStringLiteral("CAM %1").arg(pad(record->camera)) : QStringLiteral("—")));

    const QString fullComment = record->coachComment.trimmed();
    if (fullComment.isEmpty()) {
        m_historyDetailCommentLabel->setText(QStringLiteral("暂无教练批注"));
        m_historyDetailCommentLabel->setToolTip(QString());
    } else {
        QString preview = fullComment;
        preview.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (preview.size() > 180) {
            preview = preview.left(180).trimmed() + QStringLiteral("…");
        }
        m_historyDetailCommentLabel->setText(preview);
        m_historyDetailCommentLabel->setToolTip(fullComment);
    }

    const bool repositoryAvailable = m_trainingRepository && m_trainingRepository->isOpen();
    const bool playable = historySessionHasPlayableVideo(*record);
    m_historyPlayButton->setEnabled(playable);
    m_historyPlayButton->setToolTip(playable ? QString() : QStringLiteral("当前训练没有可用录像"));
    m_historyReviewButton->setEnabled(repositoryAvailable);
    m_historyTrackButton->setEnabled(repositoryAvailable);
    m_historyCommentButton->setEnabled(repositoryAvailable);
    m_historyExportButton->setEnabled(repositoryAvailable);
}

// 重新生成训练历史列表和当前记录详情。
void MainWindow::refreshHistory()
{
    if (!m_historySessionList) {
        return;
    }

    int trainingCount = 0;
    int competitionCount = 0;
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        trainingCount += record.sourceType == QStringLiteral("training") ? 1 : 0;
        competitionCount += record.sourceType == QStringLiteral("competition") ? 1 : 0;
    }
    if (m_summaryValues.size() >= 4) {
        m_summaryValues.at(0)->setText(QString::number(m_records.size()));
        m_summaryValues.at(1)->setText(QString::number(trainingCount));
        m_summaryValues.at(2)->setText(QString::number(competitionCount));
        m_summaryValues.at(3)->clear();
    }
    for (QPushButton *button : {m_historySearchButton,
                                m_historyRepetitionSearchButton,
                                m_historyCompetitionManagementButton,
                                m_historyReportCenterButton}) {
        if (button) {
            button->setEnabled(m_historyServiceAvailable);
        }
    }

    const QString previousSelection = m_selectedHistorySessionId;
    QSignalBlocker blocker(m_historySessionList);
    m_historySessionList->clear();
    int selectedRow = -1;
    for (int row = 0; row < m_records.size(); ++row) {
        const SessionHistoryItem &record = m_records.at(row);
        QString auxiliary = record.sourceLabel.trimmed();
        if (auxiliary.isEmpty() && record.sourceType == QStringLiteral("competition")) {
            auxiliary = !record.raceName.trimmed().isEmpty() ? record.raceName.trimmed() : record.competitionName.trimmed();
        }
        if (auxiliary.isEmpty()) {
            auxiliary = record.videoCameraName.trimmed();
        }
        if (auxiliary.isEmpty() && record.camera > 0) {
            auxiliary = QStringLiteral("CAM %1").arg(pad(record.camera));
        }
        const QString source = auxiliary.isEmpty()
                                   ? sessionSourceTypeLabel(record.sourceType)
                                   : QStringLiteral("%1 · %2").arg(sessionSourceTypeLabel(record.sourceType), auxiliary);
        const QString listTime = record.startedAt.isValid()
                                     ? record.startedAt.toString(QStringLiteral("MM-dd HH:mm"))
                                     : (record.time.trimmed().isEmpty() ? QStringLiteral("—") : record.time.trimmed());
        const QString athlete = record.athleteName.trimmed().isEmpty() ? QStringLiteral("未指定运动员") : record.athleteName.trimmed();
        const QString text = QStringLiteral("%1\n%2\n%3\n%4 分 · %5")
                                 .arg(athlete, listTime, source)
                                 .arg(record.score)
                                 .arg(formatTime(record.duration));
        auto *item = new QListWidgetItem(text, m_historySessionList);
        item->setData(Qt::UserRole, record.id);
        item->setToolTip(text);
        item->setSizeHint(QSize(0, 88));
        if (record.id == previousSelection) {
            selectedRow = row;
        }
    }

    if (selectedRow < 0 && !m_records.isEmpty()) {
        selectedRow = 0;
    }
    if (selectedRow >= 0) {
        m_historySessionList->setCurrentRow(selectedRow);
        m_selectedHistorySessionId = m_records.at(selectedRow).id;
    } else {
        m_selectedHistorySessionId.clear();
    }
    refreshHistorySessionDetail();
    refreshHistoryPager();
}


void MainWindow::prepareSuggestionContext(bool resetToDefault)
{
    const QString previousAthleteId = ui->suggestionAthleteComboBox->currentData().toString();
    const QString previousActionId = ui->suggestionActionComboBox->currentData().toString();

    SessionHistoryItem newestRecord;
    bool hasNewestRecord = false;
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        if (!hasNewestRecord || record.startedAt > newestRecord.startedAt) {
            newestRecord = record;
            hasNewestRecord = true;
        }
    }
    if (m_hasSuggestionLatestRecord
        && (!hasNewestRecord || m_suggestionLatestRecord.startedAt > newestRecord.startedAt)) {
        newestRecord = m_suggestionLatestRecord;
        hasNewestRecord = true;
    }

    QString athleteId = resetToDefault ? selectedAthleteId() : previousAthleteId;
    QString actionId = resetToDefault ? selectedActionStandard().id : previousActionId;
    if (athleteId.isEmpty() && hasNewestRecord) {
        athleteId = newestRecord.athleteId;
    }
    if (actionId.isEmpty() && hasNewestRecord) {
        actionId = newestRecord.actionStandardId;
    }

    const QSignalBlocker athleteBlocker(ui->suggestionAthleteComboBox);
    const QSignalBlocker actionBlocker(ui->suggestionActionComboBox);
    ui->suggestionAthleteComboBox->clear();
    ui->suggestionActionComboBox->clear();
    QSet<QString> athleteIds;
    for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
        ui->suggestionAthleteComboBox->addItem(athlete.name, athlete.id);
        athleteIds.insert(athlete.id);
    }
    QSet<QString> actionIds;
    for (const ActionStandard &standard : std::as_const(m_actionStandards)) {
        ui->suggestionActionComboBox->addItem(standard.name, standard.id);
        actionIds.insert(standard.id);
    }
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        if (!record.athleteId.isEmpty() && !athleteIds.contains(record.athleteId)) {
            ui->suggestionAthleteComboBox->addItem(record.athleteName.isEmpty() ? record.athleteId : record.athleteName,
                                                   record.athleteId);
            athleteIds.insert(record.athleteId);
        }
        if (!record.actionStandardId.isEmpty() && !actionIds.contains(record.actionStandardId)) {
            ui->suggestionActionComboBox->addItem(record.actionName.isEmpty() ? record.actionStandardId : record.actionName,
                                                  record.actionStandardId);
            actionIds.insert(record.actionStandardId);
        }
    }
    if (m_hasSuggestionLatestRecord) {
        const SessionHistoryItem &record = m_suggestionLatestRecord;
        if (!record.athleteId.isEmpty() && !athleteIds.contains(record.athleteId)) {
            ui->suggestionAthleteComboBox->addItem(record.athleteName.isEmpty() ? record.athleteId : record.athleteName,
                                                   record.athleteId);
        }
        if (!record.actionStandardId.isEmpty() && !actionIds.contains(record.actionStandardId)) {
            ui->suggestionActionComboBox->addItem(record.actionName.isEmpty() ? record.actionStandardId : record.actionName,
                                                  record.actionStandardId);
        }
    }
    int index = ui->suggestionAthleteComboBox->findData(athleteId);
    ui->suggestionAthleteComboBox->setCurrentIndex(index >= 0 ? index : (ui->suggestionAthleteComboBox->count() > 0 ? 0 : -1));
    index = ui->suggestionActionComboBox->findData(actionId);
    ui->suggestionActionComboBox->setCurrentIndex(index >= 0 ? index : (ui->suggestionActionComboBox->count() > 0 ? 0 : -1));
}

// 只根据已保存的训练记录、趋势和复盘内容整理训练洞察。
void MainWindow::refreshSuggestions()
{
    if (m_activePage != kSuggestionPage || !ui->suggestionListLayout || m_refreshingSuggestions) {
        return;
    }
    m_refreshingSuggestions = true;
    struct RefreshGuard { bool &value; ~RefreshGuard() { value = false; } } refreshGuard{m_refreshingSuggestions};
    prepareSuggestionContext(false);
    ui->suggestionAthleteComboBox->setEnabled(false);
    ui->suggestionActionComboBox->setEnabled(false);
    struct ContextControlGuard {
        QComboBox *athlete = nullptr;
        QComboBox *action = nullptr;
        ~ContextControlGuard() { athlete->setEnabled(true); action->setEnabled(true); }
    } contextControlGuard{ui->suggestionAthleteComboBox, ui->suggestionActionComboBox};

    const QString athleteId = ui->suggestionAthleteComboBox->currentData().toString();
    const QString actionId = ui->suggestionActionComboBox->currentData().toString();
    const bool repositoryAvailable = m_trainingRepository && m_trainingRepository->isOpen();
    bool querySynchronized = repositoryAvailable;
    bool hasLatest = false;
    SessionHistoryItem latest;

    if (repositoryAvailable && !athleteId.isEmpty() && !actionId.isEmpty()) {
        SessionSearchFilters filters;
        filters.athleteId = athleteId;
        filters.actionStandardId = actionId;
        SessionSearchPage page;
        page.pageSize = 1;
        const SessionSearchResult result = m_trainingRepository->searchSessions(filters, page, {});
        // The repository currently has no per-request error result; successful responses echo pageSize.
        querySynchronized = result.pageSize == page.pageSize;
        if (querySynchronized) {
            if (!result.items.isEmpty()) {
                latest = result.items.first();
                hasLatest = true;
                m_suggestionLatestRecord = latest;
                m_hasSuggestionLatestRecord = true;
            } else {
                m_hasSuggestionLatestRecord = false;
            }
        }
    }
    if (!querySynchronized || !repositoryAvailable) {
        if (m_hasSuggestionLatestRecord
            && m_suggestionLatestRecord.athleteId == athleteId
            && m_suggestionLatestRecord.actionStandardId == actionId) {
            latest = m_suggestionLatestRecord;
            hasLatest = true;
        }
        for (const SessionHistoryItem &record : std::as_const(m_records)) {
            if (record.athleteId == athleteId && record.actionStandardId == actionId
                && (!hasLatest || record.startedAt > latest.startedAt)) {
                latest = record;
                hasLatest = true;
            }
        }
    }

    clearLayout(ui->suggestionListLayout);
    auto addTitle = [this](QVBoxLayout *layout, const QString &text) {
        auto *label = new QLabel(text, ui->suggestionScrollContent);
        setRole(label, "sectionTitle");
        layout->addWidget(label);
    };
    auto addText = [this](QVBoxLayout *layout, const QString &text, bool muted = false) {
        auto *label = new QLabel(text, ui->suggestionScrollContent);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        if (muted) setRole(label, "muted");
        layout->addWidget(label);
    };
    auto addGridRow = [this](QGridLayout *grid, int row, const QString &name, const QString &value, int column = 0) {
        auto *nameLabel = new QLabel(name, ui->suggestionScrollContent);
        setRole(nameLabel, "muted");
        auto *valueLabel = new QLabel(value, ui->suggestionScrollContent);
        valueLabel->setTextFormat(Qt::PlainText);
        valueLabel->setWordWrap(true);
        grid->addWidget(nameLabel, row, column * 2);
        grid->addWidget(valueLabel, row, column * 2 + 1);
    };

    if (!querySynchronized) {
        addText(ui->suggestionListLayout, QStringLiteral("训练服务未同步，以下最近记录来自本地缓存。"), true);
    }

    if (!hasLatest) {
        auto *empty = new QFrame(ui->suggestionScrollContent);
        setRole(empty, "panel");
        auto *layout = new QVBoxLayout(empty);
        layout->setContentsMargins(20, 20, 20, 20);
        addTitle(layout, QStringLiteral("暂无可用训练数据"));
        addText(layout, QStringLiteral("完成并保存训练后，这里会根据历史记录整理趋势和复盘关注点。"), true);
        auto *actions = new QHBoxLayout();
        auto *liveButton = new QPushButton(QStringLiteral("前往实时训练"), empty);
        setRole(liveButton, "primary");
        auto *historyButton = new QPushButton(QStringLiteral("查看历史复盘"), empty);
        setRole(historyButton, "subtle");
        connect(liveButton, &QPushButton::clicked, this, [this]() { switchPage(kCapturePage); });
        connect(historyButton, &QPushButton::clicked, this, [this]() { switchPage(kHistoryPage); });
        actions->addWidget(liveButton);
        actions->addWidget(historyButton);
        actions->addStretch();
        layout->addLayout(actions);
        ui->suggestionListLayout->addWidget(empty);
    } else {
        auto *recentPanel = new QFrame(ui->suggestionScrollContent);
        setRole(recentPanel, "panel");
        auto *recentLayout = new QVBoxLayout(recentPanel);
        recentLayout->setContentsMargins(20, 16, 20, 16);
        recentLayout->setSpacing(10);
        addTitle(recentLayout, QStringLiteral("最近训练"));
        auto *summary = new QGridLayout();
        summary->setHorizontalSpacing(18);
        addGridRow(summary, 0, QStringLiteral("日期"), latest.startedAt.isValid() ? latest.startedAt.toString(QStringLiteral("MM-dd HH:mm")) : QStringLiteral("—"));
        addGridRow(summary, 0, QStringLiteral("平均分"), latest.score > 0 ? QString::number(latest.score) : QStringLiteral("—"), 1);
        addGridRow(summary, 0, QStringLiteral("最佳分"), latest.bestScore > 0 ? QString::number(latest.bestScore) : QStringLiteral("—"), 2);
        addGridRow(summary, 1, QStringLiteral("总动作"), latest.totalReps >= 0 ? QString::number(latest.totalReps) : QStringLiteral("—"));
        addGridRow(summary, 1, QStringLiteral("有效动作"), latest.validReps >= 0 ? QStringLiteral("%1 / %2").arg(latest.validReps).arg(latest.totalReps) : QStringLiteral("—"), 1);
        addGridRow(summary, 1, QStringLiteral("训练时长"), latest.duration > 0 ? formatTime(latest.duration) : QStringLiteral("—"), 2);
        recentLayout->addLayout(summary);
        auto *recentActions = new QHBoxLayout();
        auto *reviewButton = new QPushButton(QStringLiteral("复盘最近训练"), recentPanel);
        auto *trackButton = new QPushButton(QStringLiteral("查看轨迹 / 速度"), recentPanel);
        setRole(reviewButton, "primary");
        setRole(trackButton, "subtle");
        reviewButton->setEnabled(repositoryAvailable && querySynchronized);
        trackButton->setEnabled(repositoryAvailable && querySynchronized);
        connect(reviewButton, &QPushButton::clicked, this, [this, latest]() { openTrainingReview(latest); });
        connect(trackButton, &QPushButton::clicked, this, [this, latest]() { openTrackPointReview(latest); });
        recentActions->addWidget(reviewButton);
        recentActions->addWidget(trackButton);
        recentActions->addStretch();
        recentLayout->addLayout(recentActions);
        ui->suggestionListLayout->addWidget(recentPanel);
    }

    auto *detailsPanel = new QFrame(ui->suggestionScrollContent);
    setRole(detailsPanel, "panel");
    auto *details = new QVBoxLayout(detailsPanel);
    details->setContentsMargins(20, 18, 20, 18);
    details->setSpacing(12);

    addTitle(details, QStringLiteral("近期趋势"));
    addText(details, QStringLiteral("全部训练（现有统计范围，不随上方筛选变化）"), true);
    auto *trendGrid = new QGridLayout();
    trendGrid->setHorizontalSpacing(24);
    trendGrid->addWidget(new QLabel(QStringLiteral("近 7 天"), detailsPanel), 0, 1);
    trendGrid->addWidget(new QLabel(QStringLiteral("近 30 天"), detailsPanel), 0, 3);
    TrainingTrendWindow sevenDayTrend;
    TrainingTrendWindow thirtyDayTrend;
    bool trendsSynchronized = false;
    if (repositoryAvailable) {
        sevenDayTrend = m_trainingRepository->trendForRecentDays(7);
        thirtyDayTrend = m_trainingRepository->trendForRecentDays(30);
        trendsSynchronized = sevenDayTrend.days == 7 && thirtyDayTrend.days == 30;
    }
    const auto trendCount = [trendsSynchronized](int value) { return trendsSynchronized ? QString::number(value) : QStringLiteral("未同步"); };
    const auto trendScore = [trendsSynchronized](const TrainingTrendWindow &trend, int value) {
        return trendsSynchronized ? (trend.sessionCount > 0 && value > 0 ? QString::number(value) : QStringLiteral("—"))
                                  : QStringLiteral("未同步");
    };
    addGridRow(trendGrid, 1, QStringLiteral("训练次数"), trendCount(sevenDayTrend.sessionCount));
    addGridRow(trendGrid, 1, QString(), trendCount(thirtyDayTrend.sessionCount), 1);
    addGridRow(trendGrid, 2, QStringLiteral("平均分"), trendScore(sevenDayTrend, sevenDayTrend.averageScore));
    addGridRow(trendGrid, 2, QString(), trendScore(thirtyDayTrend, thirtyDayTrend.averageScore), 1);
    addGridRow(trendGrid, 3, QStringLiteral("最佳分"), trendScore(sevenDayTrend, sevenDayTrend.bestScore));
    addGridRow(trendGrid, 3, QString(), trendScore(thirtyDayTrend, thirtyDayTrend.bestScore), 1);
    addGridRow(trendGrid, 4, QStringLiteral("完成动作"), trendCount(sevenDayTrend.completedReps));
    addGridRow(trendGrid, 4, QString(), trendCount(thirtyDayTrend.completedReps), 1);
    addGridRow(trendGrid, 5, QStringLiteral("相对低项"), trendsSynchronized && sevenDayTrend.sessionCount > 0 && sevenDayTrend.weakestMetricScore > 0 ? sevenDayTrend.weakestMetricName : (trendsSynchronized ? QStringLiteral("—") : QStringLiteral("未同步")));
    addGridRow(trendGrid, 5, QString(), trendsSynchronized && thirtyDayTrend.sessionCount > 0 && thirtyDayTrend.weakestMetricScore > 0 ? thirtyDayTrend.weakestMetricName : (trendsSynchronized ? QStringLiteral("—") : QStringLiteral("未同步")), 1);
    details->addLayout(trendGrid);
    if (trendsSynchronized && (sevenDayTrend.sessionCount < 2 || thirtyDayTrend.sessionCount < 3)) {
        addText(details, QStringLiteral("数据不足，暂不判断趋势。"), true);
    }

    addTitle(details, QStringLiteral("历史基线"));
    if (!repositoryAvailable || !querySynchronized || !trendsSynchronized) {
        addText(details, QStringLiteral("未同步"), true);
    } else if (athleteId.isEmpty() || actionId.isEmpty()) {
        addText(details, QStringLiteral("请选择运动员和动作项目"), true);
    } else {
        const TrainingBaseline baseline = m_trainingRepository->baselineFor(athleteId, actionId);
        if (baseline.sessionCount <= 0) {
            addText(details, QStringLiteral("暂无可用历史基线"), true);
        } else {
            auto *baselineGrid = new QGridLayout();
            addGridRow(baselineGrid, 0, QStringLiteral("历史训练次数"), QString::number(baseline.sessionCount));
            addGridRow(baselineGrid, 0, QStringLiteral("历史平均分"), QString::number(baseline.averageScore), 1);
            addGridRow(baselineGrid, 0, QStringLiteral("平均有效动作数"), QString::number(baseline.averageValidReps), 2);
            details->addLayout(baselineGrid);
        }
    }

    if (hasLatest) {
        struct Metric { QString name; int value; QString advice; };
        const QVector<Metric> metrics = {
            {QStringLiteral("关键点"), latest.detectionScore, QStringLiteral("建议优先复盘该项相关的低分片段，确认检测完整性和动作记录质量。")},
            {QStringLiteral("对称"), latest.symmetryScore, QStringLiteral("建议结合训练录像和教练复核，比较左右动作表现。")},
            {QStringLiteral("重心"), latest.balanceScore, QStringLiteral("建议结合已有轨迹 / 速度记录和录像进行复盘。")},
            {QStringLiteral("稳定"), latest.stabilityScore, QStringLiteral("建议查看连续训练片段和已有教练批注，确认低分出现的具体阶段。")},
            {QStringLiteral("3D"), latest.depthScore, QStringLiteral("该数值仅为历史已有评分，当前不提供 3D 姿态原因诊断。")}
        };
        const Metric *weakest = nullptr;
        for (const Metric &metric : metrics) {
            if (metric.value > 0 && (!weakest || metric.value < weakest->value)) weakest = &metric;
        }
        addTitle(details, QStringLiteral("当前关注点"));
        if (weakest) {
            addText(details, QStringLiteral("%1 · %2 分\n当前该项评分在本次已有评分维度中相对较低。\n\n建议：%3")
                                 .arg(weakest->name).arg(weakest->value).arg(weakest->advice));
        } else {
            addText(details, QStringLiteral("暂无可用评分"), true);
        }
        if (!latest.coachComment.trimmed().isEmpty()) {
            addTitle(details, QStringLiteral("最近教练批注"));
            addText(details, QStringLiteral("来自人工复核"), true);
            addText(details, latest.coachComment);
        }
        if (!latest.feedback.trimmed().isEmpty()) {
            addTitle(details, QStringLiteral("最近复盘反馈"));
            addText(details, latest.feedback);
        }
    }

    addTitle(details, QStringLiteral("下次训练准备"));
    const ActionStandard *selectedStandard = nullptr;
    for (const ActionStandard &standard : std::as_const(m_actionStandards)) {
        if (standard.id == actionId) { selectedStandard = &standard; break; }
    }
    if (selectedStandard) {
        addText(details, QStringLiteral("现有训练配置"), true);
        auto *standardGrid = new QGridLayout();
        addGridRow(standardGrid, 0, QStringLiteral("动作标准"), selectedStandard->name);
        addGridRow(standardGrid, 0, QStringLiteral("目标次数"), QString::number(selectedStandard->targetReps), 1);
        addGridRow(standardGrid, 1, QStringLiteral("目标分"), QString::number(selectedStandard->targetScore));
        addGridRow(standardGrid, 1, QStringLiteral("组数"), QString::number(selectedStandard->setCount), 1);
        addGridRow(standardGrid, 1, QStringLiteral("休息时间"), QStringLiteral("%1 秒").arg(selectedStandard->restSeconds), 2);
        details->addLayout(standardGrid);
    }
    QStringList checklist;
    if (selectedStandard) checklist << QStringLiteral("• 继续使用当前动作标准");
    if (hasLatest && (latest.detectionScore > 0 || latest.symmetryScore > 0 || latest.balanceScore > 0
                      || latest.stabilityScore > 0 || latest.depthScore > 0)) {
        checklist << QStringLiteral("• 关注最近低分片段");
    }
    if (hasLatest && !latest.coachComment.trimmed().isEmpty()) checklist << QStringLiteral("• 检查已有教练批注");
    if (hasLatest && (latest.targetReps > 0 || latest.targetScore > 0)) checklist << QStringLiteral("• 根据历史目标完成下一次训练");
    if (checklist.isEmpty()) {
        checklist << (actionId.isEmpty() ? QStringLiteral("请选择动作项目查看现有训练配置")
                                         : QStringLiteral("保存训练后可整理下次训练准备信息"));
    }
    addText(details, checklist.join(QLatin1Char('\n')));
    ui->suggestionListLayout->addWidget(detailsPanel);
    addText(ui->suggestionListLayout,
            QStringLiteral("洞察来自历史训练记录和已保存复盘数据，仅用于辅助训练复盘。"), true);
    ui->suggestionListLayout->addStretch();
}

void MainWindow::openSessionVideo(const SessionHistoryItem &record,
                                  int offsetMs,
                                  int endOffsetMs,
                                  const QString &videoFileId,
                                  int videoIndex)
{
    clearOfflineAnalysisOverlay();
    const NvrPlaybackResult nvr = buildNvrPlaybackUrl(m_sharedCameraSettings,
                                                      m_cameraSlotSettings,
                                                      record,
                                                      offsetMs > 0 ? offsetMs : -1,
                                                      endOffsetMs);
    const TrainingVideoFile *indexedVideo = videoFileForRepetition(record, videoFileId, videoIndex);
    if (!indexedVideo && !record.analysisBatchId.isEmpty() && !record.videoFiles.isEmpty()) {
        indexedVideo = &record.videoFiles.first();
    }
    QString indexedLocalFile;
    QString indexedMissingFile;
    QString indexedRemoteSource;
    QString unresolvedNasUri;
    if (indexedVideo) {
        QString candidateSource = indexedVideo->filePath.trimmed();
        if (candidateSource.isEmpty()) {
            candidateSource = indexedVideo->sourceUrl.trimmed();
        }
        const QString mappedNasPath = windowsPathForNasUri(candidateSource);
        if (!mappedNasPath.isEmpty()) {
            candidateSource = mappedNasPath;
        } else if (candidateSource.startsWith(QStringLiteral("nas://"), Qt::CaseInsensitive)) {
            unresolvedNasUri = candidateSource;
            candidateSource.clear();
        } else if (hasMediaUrlScheme(candidateSource)
                   && !candidateSource.startsWith(QStringLiteral("nas://"), Qt::CaseInsensitive)) {
            indexedRemoteSource = candidateSource;
            candidateSource.clear();
        }

        const QFileInfo candidate(candidateSource);
        if (!candidateSource.isEmpty() && candidate.exists() && candidate.isFile()) {
            indexedLocalFile = candidate.absoluteFilePath();
        } else if (!candidateSource.isEmpty()) {
            indexedMissingFile = candidate.absoluteFilePath();
        }
    }
    const bool useIndexedLocal = !indexedLocalFile.isEmpty();
    const bool useIndexedRemote = !useIndexedLocal && !indexedRemoteSource.isEmpty();
    const bool useNvr = !useIndexedLocal
                        && !useIndexedRemote
                        && unresolvedNasUri.isEmpty()
                        && !nvr.url.trimmed().isEmpty();
    const QString source = useIndexedLocal
                               ? indexedLocalFile
                               : (useIndexedRemote
                                      ? indexedRemoteSource
                                      : (useNvr
                                             ? nvr.url.trimmed()
                                             : (!indexedMissingFile.isEmpty()
                                                    ? indexedMissingFile
                                                    : (!record.videoSource.trimmed().isEmpty()
                                                           ? record.videoSource.trimmed()
                                                           : record.videoFallbackSource.trimmed()))));
    if (!unresolvedNasUri.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("未配置 NAS 映射"),
                             QStringLiteral("无法在 Windows 上打开 %1。请在“完整分析”中设置该 nas:// 根目录对应的 Windows 盘符或 UNC 路径。")
                                 .arg(unresolvedNasUri));
        return;
    }
    if (source.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("暂无视频引用"), QStringLiteral("这条训练记录没有保存可回看的视频引用。"));
        return;
    }

    const bool sourceIsUrl = hasMediaUrlScheme(source);
    const QString indexedTitle = indexedVideo && indexedVideo->videoIndex > 0
                                     ? QStringLiteral("视频 #%1").arg(indexedVideo->videoIndex)
                                     : QString();
    const QString sourceTitle = record.videoCameraName.trimmed().isEmpty()
                                    ? (sourceIsUrl ? QStringLiteral("训练回看") : offlineVideoDisplayName(source))
                                    : (indexedTitle.isEmpty()
                                           ? record.videoCameraName.trimmed()
                                           : QStringLiteral("%1 · %2").arg(record.videoCameraName.trimmed(), indexedTitle));
    ui->mainImageLabel->setPlaceholderText(sourceTitle);

    if (!sourceIsUrl) {
        const QFileInfo fileInfo(source);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            ui->mainImageLabel->stopPlayback();
            ui->mainImageLabel->setPlaceholderText(QStringLiteral("训练回看\n文件不存在"));
            if (ui->focusTitleLabel) {
                ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(sourceTitle));
            }
            if (m_athleteAnalysisManager) {
                m_athleteAnalysisManager->setPaused(true);
                m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
            }
            switchPage(kCapturePage);

            const QString message = QStringLiteral("视频文件不存在：%1。请恢复原文件或重新导入后再回看。")
                                        .arg(QDir::toNativeSeparators(source));
            ui->saveTipLabel->setText(message);
            ui->saveTipLabel->show();
            QMessageBox::warning(this, QStringLiteral("视频文件不存在"), message);
            return;
        }

        ui->mainImageLabel->playFile(fileInfo.absoluteFilePath(), offsetMs);
    } else {
        ui->mainImageLabel->playMainUrlWithFallback(source, record.videoFallbackSource);
    }

    if (m_athleteAnalysisManager) {
        m_athleteAnalysisManager->setPaused(true);
        m_athleteAnalysisManager->setActiveStreams(QVector<AthleteAnalysisManager::AnalysisStream>());
    }
    const int analysisCameraId = indexedVideo && indexedVideo->camera > 0 ? indexedVideo->camera : record.camera;
    startOfflineAnalysisOverlay(record, analysisCameraId);
    if (ui->focusTitleLabel) {
        ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(sourceTitle));
    }
    switchPage(kCapturePage);

    QString offsetTip;
    if (!indexedMissingFile.isEmpty() && !useIndexedLocal) {
        offsetTip += QStringLiteral("。索引视频文件已清理或移动，请恢复文件后可精确定位：%1")
                         .arg(QDir::toNativeSeparators(indexedMissingFile));
    }
    if (offsetMs > 0) {
        if (useNvr) {
            offsetTip += QStringLiteral("。已打开 NVR 片段窗口 %1-%2；RTSP 回放无法保证精确 seek。")
                             .arg(formatMilliseconds(nvr.startOffsetMs),
                                  formatMilliseconds(nvr.endOffsetMs));
        } else if (sourceIsUrl) {
            offsetTip += QStringLiteral("。RTSP/网络视频暂不支持自动定位，已打开视频源；片段起点 %1 可作为人工回看参考。")
                             .arg(formatMilliseconds(offsetMs));
        } else {
            offsetTip += QStringLiteral("。已请求定位到片段起点 %1。").arg(formatMilliseconds(offsetMs));
        }
    }
    const QString sourceKind = useNvr ? QStringLiteral("NVR 回放") : displayMediaSource(source);
    ui->saveTipLabel->setText(QStringLiteral("正在回看：%1%2").arg(sourceKind, offsetTip));
    ui->saveTipLabel->show();
}

void MainWindow::openTrainingReview(const SessionHistoryItem &record)
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("训练数据库未就绪，无法打开复盘校准。"));
        return;
    }

    ActionStandard standard;
    for (const ActionStandard &candidate : std::as_const(m_actionStandards)) {
        if (candidate.id == record.actionStandardId) {
            standard = candidate;
            break;
        }
    }
    if (standard.id.isEmpty()) {
        standard.id = record.actionStandardId;
        standard.name = record.actionName;
        standard.categoryName = record.actionCategory;
        standard.version = record.standardVersion;
        standard.targetReps = record.targetReps;
        standard.targetScore = record.targetScore;
    }

    TrainingReviewDialog dialog(record, standard, m_trainingRepository.get(), this);
    dialog.exec();

    reloadTrainingContext();
    loadTrainingRecords();
    refreshHistory();
    refreshSuggestions();
}

void MainWindow::openTrackPointReview(const SessionHistoryItem &record)
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("训练数据库未就绪，无法打开轨迹复盘。"));
        return;
    }
    const QVector<TrackPoint> allPoints = m_trainingRepository->trackPointsForSession(record.id);
    const QVector<SpeedMetric> allSpeedMetrics = m_trainingRepository->speedMetricsForSession(record.id);
    if (allPoints.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("暂无轨迹"), QStringLiteral("这条训练记录没有已标定的场地轨迹点。旧记录仍可使用动作和姿态复盘。"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("二维滑行轨迹复盘"));
    dialog.resize(900, 640);
    auto *layout = new QVBoxLayout(&dialog);
    auto *top = new QHBoxLayout();
    auto *participantCombo = new QComboBox(&dialog);
    participantCombo->addItem(QStringLiteral("全部参与者"), QString());
    for (const TrainingSessionParticipant &participant : record.participants) {
        participantCombo->addItem(participant.athleteName.trimmed().isEmpty() ? participant.athleteId : participant.athleteName,
                                  participant.id);
    }
    auto *exportCsv = new QPushButton(QStringLiteral("导出 CSV"), &dialog);
    auto *exportXlsx = new QPushButton(QStringLiteral("导出 XLSX"), &dialog);
    top->addWidget(new QLabel(QStringLiteral("参与者"), &dialog));
    top->addWidget(participantCombo);
    top->addStretch();
    top->addWidget(exportCsv);
    top->addWidget(exportXlsx);
    layout->addLayout(top);
    auto *trajectory = new TrajectoryWidget(&dialog);
    layout->addWidget(trajectory, 1);
    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(11);
    table->setHorizontalHeaderLabels({QStringLiteral("时间(ms)"), QStringLiteral("X(m)"), QStringLiteral("Y(m)"), QStringLiteral("Z(m)"), QStringLiteral("瞬时速度(m/s)"), QStringLiteral("平滑速度(m/s)"), QStringLiteral("有效"), QStringLiteral("平滑窗口(ms)"), QStringLiteral("算法版本"), QStringLiteral("机位"), QStringLiteral("置信度")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);
    QVector<TrackPoint> current;
    QHash<QString, SpeedMetric> metricByPoint;
    for (const SpeedMetric &metric : allSpeedMetrics) {
        metricByPoint.insert(metric.trackPointId, metric);
    };
    auto reload = [&]() {
        current.clear();
        const QString participantId = participantCombo->currentData().toString();
        for (const TrackPoint &point : allPoints) {
            if (participantId.isEmpty() || point.participantId == participantId) current.append(point);
        }
        trajectory->setTrackPoints(current);
        table->setRowCount(current.size());
        for (int row = 0; row < current.size(); ++row) {
            const TrackPoint &point = current.at(row);
            const SpeedMetric metric = metricByPoint.value(point.id);
            const QStringList values{QString::number(point.timestampMs), QString::number(point.x, 'f', 3), QString::number(point.y, 'f', 3), QString::number(point.z, 'f', 3), metric.id.isEmpty() ? QStringLiteral("-") : QString::number(metric.instantaneousSpeedMps, 'f', 3), metric.id.isEmpty() ? QStringLiteral("-") : QString::number(metric.smoothedSpeedMps, 'f', 3), metric.id.isEmpty() ? QStringLiteral("-") : (metric.valid ? QStringLiteral("是") : QStringLiteral("否")), metric.id.isEmpty() ? QStringLiteral("-") : QString::number(metric.smoothingWindowMs), metric.id.isEmpty() ? QStringLiteral("-") : metric.algorithmVersion, QString::number(point.cameraId), point.confidence < 0 ? QStringLiteral("-") : QString::number(point.confidence, 'f', 3)};
            for (int column = 0; column < values.size(); ++column) table->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
        table->resizeColumnsToContents();
    };
    auto exportPoints = [&](const QString &suffix) {
        if (current.isEmpty()) return;
        QString path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("导出滑行轨迹"), QDir::homePath() + QStringLiteral("/track_points.") + suffix, suffix == QStringLiteral("xlsx") ? QStringLiteral("Excel 工作簿 (*.xlsx)") : QStringLiteral("CSV 文件 (*.csv)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".") + suffix, Qt::CaseInsensitive)) path += QStringLiteral(".") + suffix;
        const QStringList headers{QStringLiteral("t_ms"), QStringLiteral("x_m"), QStringLiteral("y_m"), QStringLiteral("z_m"), QStringLiteral("instantaneous_speed_mps"), QStringLiteral("smoothed_speed_mps"), QStringLiteral("speed_valid"), QStringLiteral("smoothing_window_ms"), QStringLiteral("speed_unit"), QStringLiteral("algorithm_version"), QStringLiteral("speed_source"), QStringLiteral("camera_id"), QStringLiteral("confidence")};
        if (suffix == QStringLiteral("xlsx")) {
            QXlsx::Document workbook;
            for (int column = 0; column < headers.size(); ++column) workbook.write(1, column + 1, headers.at(column));
            for (int row = 0; row < current.size(); ++row) {
                const TrackPoint &point = current.at(row);
                workbook.write(row + 2, 1, QString::number(point.timestampMs)); workbook.write(row + 2, 2, point.x); workbook.write(row + 2, 3, point.y); workbook.write(row + 2, 4, point.z);
                const SpeedMetric metric = metricByPoint.value(point.id);
                if (!metric.id.isEmpty()) { workbook.write(row + 2, 5, metric.instantaneousSpeedMps); workbook.write(row + 2, 6, metric.smoothedSpeedMps); workbook.write(row + 2, 7, metric.valid); workbook.write(row + 2, 8, metric.smoothingWindowMs); workbook.write(row + 2, 9, metric.unit); workbook.write(row + 2, 10, metric.algorithmVersion); }
                workbook.write(row + 2, 11, point.speedSource); workbook.write(row + 2, 12, point.cameraId); if (point.confidence >= 0) workbook.write(row + 2, 13, point.confidence);
            }
            if (!workbook.saveAs(path)) QMessageBox::warning(&dialog, QStringLiteral("导出失败"), QStringLiteral("无法写入文件。"));
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&file); out.setEncoding(QStringConverter::Utf8); out << headers.join(',') << '\n';
        for (int row = 0; row < current.size(); ++row) {
            const TrackPoint &point = current.at(row); const SpeedMetric metric = metricByPoint.value(point.id);
            out << point.timestampMs << ',' << point.x << ',' << point.y << ',' << point.z << ',' << (metric.id.isEmpty() ? QString() : QString::number(metric.instantaneousSpeedMps)) << ',' << (metric.id.isEmpty() ? QString() : QString::number(metric.smoothedSpeedMps)) << ',' << (metric.id.isEmpty() ? QString() : (metric.valid ? QStringLiteral("true") : QStringLiteral("false"))) << ',' << (metric.id.isEmpty() ? QString() : QString::number(metric.smoothingWindowMs)) << ',' << (metric.id.isEmpty() ? QString() : metric.unit) << ',' << (metric.id.isEmpty() ? QString() : metric.algorithmVersion) << ',' << point.speedSource << ',' << point.cameraId << ',' << (point.confidence >= 0 ? QString::number(point.confidence) : QString()) << '\n';
        }
    };
    connect(participantCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, reload);
    connect(exportCsv, &QPushButton::clicked, &dialog, [&]() { exportPoints(QStringLiteral("csv")); });
    connect(exportXlsx, &QPushButton::clicked, &dialog, [&]() { exportPoints(QStringLiteral("xlsx")); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    reload();
    dialog.exec();
}

void MainWindow::editActionStandard()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("训练数据库未就绪，无法编辑动作标准。"));
        return;
    }

    ActionStandard standard = selectedActionStandard();
    if (standard.id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("暂无动作标准"), QStringLiteral("当前没有可编辑的动作标准。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑动作标准"));
    dialog.resize(760, 720);
    auto *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral("%1 · %2 · 当前 v%3")
                                      .arg(standard.categoryName, standard.name)
                                      .arg(standard.version),
                                  &dialog);
    titleLabel->setWordWrap(true);
    titleLabel->setProperty("role", "sectionTitle");
    root->addWidget(titleLabel);

    auto *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    auto *formWidget = new QWidget(scrollArea);
    auto *form = new QFormLayout(formWidget);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    auto *nameEdit = new QLineEdit(standard.name, &dialog);
    auto *levelEdit = new QLineEdit(standard.level, &dialog);
    auto *purposeEdit = new QLineEdit(standard.purpose, &dialog);
    auto *targetRepsSpin = new QSpinBox(&dialog);
    auto *targetScoreSpin = new QSpinBox(&dialog);
    auto *setCountSpin = new QSpinBox(&dialog);
    auto *restSecondsSpin = new QSpinBox(&dialog);
    targetRepsSpin->setRange(1, 999);
    targetScoreSpin->setRange(1, 100);
    setCountSpin->setRange(1, 20);
    restSecondsSpin->setRange(0, 600);
    restSecondsSpin->setSingleStep(15);
    targetRepsSpin->setValue(std::max(1, standard.targetReps));
    targetScoreSpin->setValue(std::clamp(standard.targetScore, 1, 100));
    setCountSpin->setValue(std::max(1, standard.setCount));
    restSecondsSpin->setValue(std::max(0, standard.restSeconds));

    auto makeDoubleSpin = [&dialog](double value, double min, double max, double step) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(3);
        spin->setValue(value);
        return spin;
    };
    auto *armSpin = makeDoubleSpin(standard.armThreshold, 0.01, 2.0, 0.01);
    auto *releaseSpin = makeDoubleSpin(standard.releaseThreshold, 0.01, 2.0, 0.01);
    auto *debounceSpin = new QSpinBox(&dialog);
    debounceSpin->setRange(100, 5000);
    debounceSpin->setSingleStep(50);
    debounceSpin->setValue(std::max(100, standard.debounceMs));

    auto *detectionWeightSpin = makeDoubleSpin(standard.detectionWeight, 0.0, 1.0, 0.01);
    auto *symmetryWeightSpin = makeDoubleSpin(standard.symmetryWeight, 0.0, 1.0, 0.01);
    auto *balanceWeightSpin = makeDoubleSpin(standard.balanceWeight, 0.0, 1.0, 0.01);
    auto *stabilityWeightSpin = makeDoubleSpin(standard.stabilityWeight, 0.0, 1.0, 0.01);
    auto *depthWeightSpin = makeDoubleSpin(standard.depthWeight, 0.0, 1.0, 0.01);

    auto makeScoreSpin = [&dialog](int value) {
        auto *spin = new QSpinBox(&dialog);
        spin->setRange(0, 100);
        spin->setValue(std::clamp(value, 0, 100));
        return spin;
    };
    auto *detectionMinSpin = makeScoreSpin(standard.detectionMin);
    auto *symmetryMinSpin = makeScoreSpin(standard.symmetryMin);
    auto *balanceMinSpin = makeScoreSpin(standard.balanceMin);
    auto *stabilityMinSpin = makeScoreSpin(standard.stabilityMin);
    auto *depthMinSpin = makeScoreSpin(standard.depthMin);

    auto *phasesEdit = new QPlainTextEdit(standard.phases, &dialog);
    auto *keyPointsEdit = new QPlainTextEdit(standard.keyPoints, &dialog);
    auto *issueTitleEdit = new QLineEdit(standard.issueTitle, &dialog);
    auto *issueBodyPartEdit = new QLineEdit(standard.issueBodyPart, &dialog);
    auto *issueCauseEdit = new QPlainTextEdit(standard.issueCause, &dialog);
    auto *issueCorrectionEdit = new QPlainTextEdit(standard.issueCorrection, &dialog);
    auto *issuePrioritySpin = new QSpinBox(&dialog);
    issuePrioritySpin->setRange(0, 3);
    issuePrioritySpin->setValue(std::clamp(standard.issuePriority, 0, 3));
    auto *referencePathEdit = new QLineEdit(standard.referenceVideoSource, &dialog);
    auto *referenceNotesEdit = new QPlainTextEdit(standard.referenceNotes, &dialog);
    for (auto *editor : {phasesEdit, keyPointsEdit, issueCauseEdit, issueCorrectionEdit, referenceNotesEdit}) {
        editor->setMaximumHeight(72);
    }

    auto *referenceRow = new QWidget(&dialog);
    auto *referenceLayout = new QHBoxLayout(referenceRow);
    referenceLayout->setContentsMargins(0, 0, 0, 0);
    referenceLayout->setSpacing(6);
    auto *chooseReferenceButton = new QPushButton(QStringLiteral("选择"), referenceRow);
    chooseReferenceButton->setProperty("role", "secondaryButton");
    referenceLayout->addWidget(referencePathEdit, 1);
    referenceLayout->addWidget(chooseReferenceButton, 0);

    form->addRow(QStringLiteral("名称"), nameEdit);
    form->addRow(QStringLiteral("等级"), levelEdit);
    form->addRow(QStringLiteral("目的"), purposeEdit);
    form->addRow(QStringLiteral("目标次数"), targetRepsSpin);
    form->addRow(QStringLiteral("目标分"), targetScoreSpin);
    form->addRow(QStringLiteral("组数"), setCountSpin);
    form->addRow(QStringLiteral("休息秒"), restSecondsSpin);
    form->addRow(QStringLiteral("屈膝触发阈值"), armSpin);
    form->addRow(QStringLiteral("释放阈值"), releaseSpin);
    form->addRow(QStringLiteral("防抖 ms"), debounceSpin);
    form->addRow(QStringLiteral("权重 关键点"), detectionWeightSpin);
    form->addRow(QStringLiteral("权重 对称"), symmetryWeightSpin);
    form->addRow(QStringLiteral("权重 重心"), balanceWeightSpin);
    form->addRow(QStringLiteral("权重 稳定"), stabilityWeightSpin);
    form->addRow(QStringLiteral("权重 3D"), depthWeightSpin);
    form->addRow(QStringLiteral("最低分 关键点"), detectionMinSpin);
    form->addRow(QStringLiteral("最低分 对称"), symmetryMinSpin);
    form->addRow(QStringLiteral("最低分 重心"), balanceMinSpin);
    form->addRow(QStringLiteral("最低分 稳定"), stabilityMinSpin);
    form->addRow(QStringLiteral("最低分 3D"), depthMinSpin);
    form->addRow(QStringLiteral("动作阶段"), phasesEdit);
    form->addRow(QStringLiteral("关键要求"), keyPointsEdit);
    form->addRow(QStringLiteral("错误标题"), issueTitleEdit);
    form->addRow(QStringLiteral("问题部位"), issueBodyPartEdit);
    form->addRow(QStringLiteral("触发原因"), issueCauseEdit);
    form->addRow(QStringLiteral("纠正提示"), issueCorrectionEdit);
    form->addRow(QStringLiteral("优先级"), issuePrioritySpin);
    form->addRow(QStringLiteral("参考视频"), referenceRow);
    form->addRow(QStringLiteral("参考说明"), referenceNotesEdit);
    scrollArea->setWidget(formWidget);
    root->addWidget(scrollArea, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(buttons);

    connect(chooseReferenceButton, &QPushButton::clicked, &dialog, [&dialog, referencePathEdit]() {
        const QString path = QFileDialog::getOpenFileName(&dialog,
                                                          QStringLiteral("选择标准参考视频"),
                                                          QDir::homePath(),
                                                          QStringLiteral("视频文件 (*.mp4 *.mov *.avi *.mkv *.m4v *.wmv *.flv *.webm);;所有文件 (*.*)"));
        if (!path.trimmed().isEmpty()) {
            referencePathEdit->setText(QFileInfo(path).absoluteFilePath());
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("名称为空"), QStringLiteral("动作标准名称不能为空。"));
            nameEdit->setFocus();
            return;
        }
        const double totalWeight = detectionWeightSpin->value()
                                   + symmetryWeightSpin->value()
                                   + balanceWeightSpin->value()
                                   + stabilityWeightSpin->value()
                                   + depthWeightSpin->value();
        if (totalWeight <= 0.0) {
            QMessageBox::warning(&dialog, QStringLiteral("权重无效"), QStringLiteral("至少需要一个评分权重大于 0。"));
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    standard.name = nameEdit->text().trimmed();
    standard.level = levelEdit->text().trimmed();
    standard.purpose = purposeEdit->text().trimmed();
    standard.targetReps = targetRepsSpin->value();
    standard.targetScore = targetScoreSpin->value();
    standard.setCount = setCountSpin->value();
    standard.restSeconds = restSecondsSpin->value();
    standard.armThreshold = armSpin->value();
    standard.releaseThreshold = releaseSpin->value();
    standard.debounceMs = debounceSpin->value();
    standard.detectionWeight = detectionWeightSpin->value();
    standard.symmetryWeight = symmetryWeightSpin->value();
    standard.balanceWeight = balanceWeightSpin->value();
    standard.stabilityWeight = stabilityWeightSpin->value();
    standard.depthWeight = depthWeightSpin->value();
    standard.detectionMin = detectionMinSpin->value();
    standard.symmetryMin = symmetryMinSpin->value();
    standard.balanceMin = balanceMinSpin->value();
    standard.stabilityMin = stabilityMinSpin->value();
    standard.depthMin = depthMinSpin->value();
    standard.phases = phasesEdit->toPlainText().trimmed();
    standard.keyPoints = keyPointsEdit->toPlainText().trimmed();
    standard.issueTitle = issueTitleEdit->text().trimmed();
    standard.issueBodyPart = issueBodyPartEdit->text().trimmed();
    standard.issueCause = issueCauseEdit->toPlainText().trimmed();
    standard.issueCorrection = issueCorrectionEdit->toPlainText().trimmed();
    standard.issuePriority = issuePrioritySpin->value();
    standard.referenceVideoSource = referencePathEdit->text().trimmed();
    standard.referenceNotes = referenceNotesEdit->toPlainText().trimmed();

    QString errorMessage;
    const QString standardId = standard.id;
    if (!m_trainingRepository->saveActionStandard(&standard, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    reloadTrainingContext();
    if (m_actionStandardComboBox) {
        const int index = m_actionStandardComboBox->findData(standardId);
        if (index >= 0) {
            m_actionStandardComboBox->setCurrentIndex(index);
        }
    }
    refreshSuggestions();
    ui->saveTipLabel->setText(QStringLiteral("动作标准已保存为 v%1。").arg(standard.version));
    ui->saveTipLabel->show();
}

void MainWindow::editCoachComment(const QString &sessionId)
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("训练数据库未就绪，无法保存教练批注。"));
        return;
    }

    SessionHistoryItem record;
    for (const SessionHistoryItem &item : std::as_const(m_records)) {
        if (item.id == sessionId) {
            record = item;
            break;
        }
    }
    if (record.id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("记录不存在"), QStringLiteral("未找到对应训练记录。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("教练批注"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral("%1 · %2 · %3")
                                      .arg(record.time, record.athleteName, record.actionName),
                                  &dialog);
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(record.coachComment);
    editor->setPlaceholderText(QStringLiteral("记录教练观察、人工纠正、下次训练重点"));
    editor->setMinimumSize(520, 180);
    layout->addWidget(editor);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString errorMessage;
    if (!m_trainingRepository->saveCoachComment(sessionId, editor->toPlainText(), &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    loadTrainingRecords();
    refreshHistory();
    refreshSuggestions();
}

void MainWindow::exportTrainingReport(const QString &sessionId)
{
    SessionHistoryItem record;
    for (const SessionHistoryItem &item : std::as_const(m_records)) {
        if (item.id == sessionId) {
            record = item;
            break;
        }
    }
    if (record.id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("记录不存在"), QStringLiteral("未找到对应训练记录。"));
        return;
    }

    const QVector<ActionRepetition> repetitions = m_trainingRepository && m_trainingRepository->isOpen()
                                                      ? m_trainingRepository->reviewedRepetitionsForSession(record.id)
                                                      : QVector<ActionRepetition>();

    const QString defaultFileName = QStringLiteral("iSkating-%1-%2.md")
                                        .arg(sanitizedFilePart(record.athleteName))
                                        .arg(sanitizedFilePart(record.time.left(10)));
    QString selectedFilter;
    QString filePath = QFileDialog::getSaveFileName(this,
                                                    QStringLiteral("导出训练报告"),
                                                    QDir::home().absoluteFilePath(defaultFileName),
                                                    QStringLiteral("Markdown (*.md);;CSV 明细 (*.csv);;PDF 复盘报告 (*.pdf)"),
                                                    &selectedFilter);
    if (filePath.trimmed().isEmpty()) {
        return;
    }

    QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix.isEmpty()) {
        if (selectedFilter.contains(QStringLiteral("CSV"))) {
            suffix = QStringLiteral("csv");
        } else if (selectedFilter.contains(QStringLiteral("PDF"))) {
            suffix = QStringLiteral("pdf");
        } else {
            suffix = QStringLiteral("md");
        }
        filePath = withFileSuffix(filePath, suffix);
    }

    auto writeTextFile = [this](const QString &path, const QString &content) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
            return false;
        }
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << content;
        return true;
    };

    auto buildMarkdown = [&]() {
        QString content;
        QTextStream out(&content);
        out << "# iSkating 训练复盘报告\n\n";
        out << "- 时间：" << record.time << "\n";
        out << "- 运动员：" << record.athleteName << "\n";
        out << "- 教练：" << (record.coachName.isEmpty() ? QStringLiteral("未指定") : record.coachName) << "\n";
        out << "- 动作：" << record.actionCategory << " / " << record.actionName << " v" << record.standardVersion << "\n";
        out << "- 比赛：" << (record.competitionName.trimmed().isEmpty() ? QStringLiteral("未关联比赛") : record.competitionName.trimmed())
            << " / " << (record.competitionLocation.trimmed().isEmpty() ? QStringLiteral("未填写地点") : record.competitionLocation.trimmed())
            << " / " << (record.competitionDate.isValid() ? record.competitionDate.toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("未填写日期"))
            << " / " << (record.competitionType.trimmed().isEmpty() ? QStringLiteral("未填写类型") : record.competitionType.trimmed()) << "\n";
        out << "- 场次/项目/轮次/分组：" << (record.raceName.trimmed().isEmpty() ? QStringLiteral("未关联场次") : record.raceName.trimmed())
            << " / " << (record.eventName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.eventName.trimmed())
            << " / " << (record.heatName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.heatName.trimmed())
            << " / " << (record.groupName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.groupName.trimmed()) << "\n";
        out << "- 参赛号/道次/成绩/名次：" << (record.bibNumber.trimmed().isEmpty() ? QStringLiteral("未填写") : record.bibNumber.trimmed())
            << " / " << (record.laneNumber.trimmed().isEmpty() ? QStringLiteral("未填写") : record.laneNumber.trimmed())
            << " / " << (record.resultScore >= 0 ? QString::number(record.resultScore) : QStringLiteral("未填"))
            << " / " << (record.resultRank >= 0 ? QString::number(record.resultRank) : QStringLiteral("未填")) << "\n";
        out << "- 分析归属：" << sessionSourceTypeLabel(record.sourceType)
            << " / " << sessionSourceDisplay(record) << "\n";
        out << "- 场地/阶段/目标：" << (record.site.isEmpty() ? QStringLiteral("未填写") : record.site)
            << " / " << (record.trainingPhase.isEmpty() ? QStringLiteral("未填写") : record.trainingPhase)
            << " / " << (record.goal.isEmpty() ? QStringLiteral("未填写") : record.goal) << "\n";
        out << "- 训练备注：" << (record.notes.trimmed().isEmpty() ? QStringLiteral("未填写") : record.notes.trimmed()) << "\n";
        out << "- 时长：" << formatTime(record.duration) << "\n";
        out << "- 完成度：" << record.validReps << "/" << record.totalReps << "，目标 "
            << record.targetReps << " 次/" << record.targetScore << " 分\n";
        out << "- 复核后平均/最佳分：" << record.score << "/" << record.bestScore << "\n";
        out << "- 分项均分：关键点 " << record.detectionScore
            << "，对称 " << record.symmetryScore
            << "，重心 " << record.balanceScore
            << "，稳定 " << record.stabilityScore
            << "，3D " << record.depthScore << "\n";
        out << "- 视频：" << displayMediaSource(record.videoSource) << "\n";
        if (!record.videoFallbackSource.trimmed().isEmpty()) {
            out << "- 回退视频：" << displayMediaSource(record.videoFallbackSource) << "\n";
        }
        out << "- 视频资产：\n" << videoFilesSummary(record.videoFiles) << "\n";
        out << "- 离线分析任务：" << analysisTaskSummary(record) << "\n";
        out << "\n## 综合反馈\n\n" << (record.feedback.trimmed().isEmpty() ? QStringLiteral("未填写") : record.feedback.trimmed()) << "\n\n";
        out << "## 教练批注\n\n"
            << (record.coachComment.trimmed().isEmpty() ? QStringLiteral("未填写") : record.coachComment.trimmed())
            << "\n\n";
        out << "## 动作明细\n\n";
        if (repetitions.isEmpty()) {
            out << "本次未保存动作实例。\n";
        } else {
            out << "| # | 视频 | 来源 | 复核状态 | 时间 | AI原始 | 复核后 | 错误项 | 反馈 | 教练备注 |\n";
            out << "|---|---|---|---|---|---|---|---|---|---|\n";
            for (int i = 0; i < repetitions.size(); ++i) {
                const ActionRepetition &rep = repetitions.at(i);
                out << "| " << (i + 1)
                    << " | #" << rep.videoIndex
                    << " | " << sourceLabel(rep)
                    << " | " << reviewStatusLabel(rep)
                    << " | " << formatMilliseconds(rep.effectiveStartedMs()) << "-" << formatMilliseconds(rep.effectiveEndedMs())
                    << " | " << rep.score << "分/" << boolText(rep.valid)
                    << " | " << rep.effectiveScore() << "分/" << boolText(rep.effectiveValid())
                    << " | " << issueSummary(rep.effectiveErrorCodes())
                    << " | " << (rep.effectiveFeedback().trimmed().isEmpty() ? QStringLiteral("无") : rep.effectiveFeedback().trimmed()).replace(QLatin1Char('|'), QLatin1Char('/'))
                    << " | " << (rep.coachNote.trimmed().isEmpty() ? QStringLiteral("无") : rep.coachNote.trimmed()).replace(QLatin1Char('|'), QLatin1Char('/'))
                    << " |\n";
            }
        }
        return content;
    };

    auto buildCsv = [&]() {
        QString content;
        QTextStream out(&content);
        const QStringList headers = {
            QStringLiteral("session_id"),
            QStringLiteral("time"),
            QStringLiteral("athlete"),
            QStringLiteral("coach"),
            QStringLiteral("competition"),
            QStringLiteral("competition_location"),
            QStringLiteral("competition_date"),
            QStringLiteral("competition_type"),
            QStringLiteral("race_name"),
            QStringLiteral("event_name"),
            QStringLiteral("heat_name"),
            QStringLiteral("group_name"),
            QStringLiteral("bib_number"),
            QStringLiteral("lane_number"),
            QStringLiteral("result_score"),
            QStringLiteral("result_rank"),
            QStringLiteral("session_source_type"),
            QStringLiteral("session_source_label"),
            QStringLiteral("session_source_ref"),
            QStringLiteral("analysis_task_id"),
            QStringLiteral("analysis_task_status"),
            QStringLiteral("analysis_task_batch_id"),
            QStringLiteral("analysis_task_camera_id"),
            QStringLiteral("analysis_task_time_offset_ms"),
            QStringLiteral("action"),
            QStringLiteral("standard_version"),
            QStringLiteral("duration"),
            QStringLiteral("total_reps"),
            QStringLiteral("valid_reps"),
            QStringLiteral("session_average_score"),
            QStringLiteral("session_best_score"),
            QStringLiteral("training_notes"),
            QStringLiteral("video_source"),
            QStringLiteral("video_file_indexes"),
            QStringLiteral("video_file_statuses"),
            QStringLiteral("video_file_paths"),
            QStringLiteral("video_file_ranges"),
            QStringLiteral("video_file_size_bytes"),
            QStringLiteral("rep_index"),
            QStringLiteral("rep_video_index"),
            QStringLiteral("rep_video_file_id"),
            QStringLiteral("source"),
            QStringLiteral("review_status"),
            QStringLiteral("ai_start_ms"),
            QStringLiteral("ai_end_ms"),
            QStringLiteral("effective_start_ms"),
            QStringLiteral("effective_end_ms"),
            QStringLiteral("ai_valid"),
            QStringLiteral("effective_valid"),
            QStringLiteral("ai_score"),
            QStringLiteral("effective_score"),
            QStringLiteral("ai_detection"),
            QStringLiteral("effective_detection"),
            QStringLiteral("ai_symmetry"),
            QStringLiteral("effective_symmetry"),
            QStringLiteral("ai_balance"),
            QStringLiteral("effective_balance"),
            QStringLiteral("ai_stability"),
            QStringLiteral("effective_stability"),
            QStringLiteral("ai_depth"),
            QStringLiteral("effective_depth"),
            QStringLiteral("ai_errors"),
            QStringLiteral("effective_errors"),
            QStringLiteral("ai_feedback"),
            QStringLiteral("effective_feedback"),
            QStringLiteral("coach_note"),
            QStringLiteral("key_frame_ms"),
            QStringLiteral("clip_start_ms"),
            QStringLiteral("clip_end_ms")
        };
        out << headers.join(QLatin1Char(',')) << "\n";

        auto writeRow = [&](int index, const ActionRepetition *rep) {
            QStringList row;
            row << csvField(record.id)
                << csvField(record.time)
                << csvField(record.athleteName)
                << csvField(record.coachName.isEmpty() ? QStringLiteral("未指定") : record.coachName)
                << csvField(record.competitionName.trimmed().isEmpty() ? QStringLiteral("未关联比赛") : record.competitionName.trimmed())
                << csvField(record.competitionLocation)
                << csvField(record.competitionDate.isValid() ? record.competitionDate.toString(Qt::ISODate) : QString())
                << csvField(record.competitionType)
                << csvField(record.raceName)
                << csvField(record.eventName)
                << csvField(record.heatName)
                << csvField(record.groupName)
                << csvField(record.bibNumber)
                << csvField(record.laneNumber)
                << csvField(record.resultScore >= 0 ? QString::number(record.resultScore) : QString())
                << csvField(record.resultRank >= 0 ? QString::number(record.resultRank) : QString())
                << csvField(record.sourceType)
                << csvField(sessionSourceDisplay(record))
                << csvField(record.sourceRef)
                << csvField(record.analysisTaskId)
                << csvField(record.analysisTaskStatus)
                << csvField(record.analysisTaskBatchId)
                << csvField(record.analysisTaskCameraId > 0 ? QString::number(record.analysisTaskCameraId) : QString())
                << csvField(record.analysisTaskTimeOffsetMs != 0 ? QString::number(record.analysisTaskTimeOffsetMs) : QString())
                << csvField(QStringLiteral("%1/%2").arg(record.actionCategory, record.actionName))
                << csvField(QString::number(record.standardVersion))
                << csvField(formatTime(record.duration))
                << csvField(QString::number(record.totalReps))
                << csvField(QString::number(record.validReps))
                << csvField(QString::number(record.score))
                << csvField(QString::number(record.bestScore))
                << csvField(record.notes)
                << csvField(displayMediaSource(record.videoSource))
                << csvField(videoFileIndexes(record.videoFiles))
                << csvField(videoFileStatuses(record.videoFiles))
                << csvField(videoFilePaths(record.videoFiles))
                << csvField(videoFileRanges(record.videoFiles))
                << csvField(videoFileSizes(record.videoFiles));
            if (!rep) {
                for (int i = 0; i < 31; ++i) {
                    row << csvField(QString());
                }
                out << row.join(QLatin1Char(',')) << "\n";
                return;
            }
            row << csvField(QString::number(index + 1))
                << csvField(QString::number(rep->videoIndex))
                << csvField(rep->videoFileId)
                << csvField(sourceLabel(*rep))
                << csvField(reviewStatusLabel(*rep))
                << csvField(QString::number(rep->startedMs))
                << csvField(QString::number(rep->endedMs))
                << csvField(QString::number(rep->effectiveStartedMs()))
                << csvField(QString::number(rep->effectiveEndedMs()))
                << csvField(boolText(rep->valid))
                << csvField(boolText(rep->effectiveValid()))
                << csvField(QString::number(rep->score))
                << csvField(QString::number(rep->effectiveScore()))
                << csvField(QString::number(rep->detectionScore))
                << csvField(QString::number(rep->effectiveDetectionScore()))
                << csvField(QString::number(rep->symmetryScore))
                << csvField(QString::number(rep->effectiveSymmetryScore()))
                << csvField(QString::number(rep->balanceScore))
                << csvField(QString::number(rep->effectiveBalanceScore()))
                << csvField(QString::number(rep->stabilityScore))
                << csvField(QString::number(rep->effectiveStabilityScore()))
                << csvField(QString::number(rep->depthScore))
                << csvField(QString::number(rep->effectiveDepthScore()))
                << csvField(rep->errorCodes)
                << csvField(rep->effectiveErrorCodes())
                << csvField(rep->feedback)
                << csvField(rep->effectiveFeedback())
                << csvField(rep->coachNote)
                << csvField(QString::number(rep->keyFrameMs))
                << csvField(QString::number(rep->videoClipStartMs))
                << csvField(QString::number(rep->videoClipEndMs));
            out << row.join(QLatin1Char(',')) << "\n";
        };

        if (repetitions.isEmpty()) {
            writeRow(0, nullptr);
        } else {
            for (int i = 0; i < repetitions.size(); ++i) {
                writeRow(i, &repetitions[i]);
            }
        }
        return content;
    };

    bool ok = false;
    if (suffix == QStringLiteral("csv")) {
        ok = writeTextFile(filePath, buildCsv());
    } else if (suffix == QStringLiteral("pdf")) {
        QTextDocument document;
        QString html;
        QTextStream out(&html);
        out << "<html><head><meta charset='utf-8'><style>"
            << "body{font-family:'Microsoft YaHei',sans-serif;font-size:10pt;color:#20242a;}"
            << "h1{font-size:20pt;} h2{font-size:14pt;margin-top:18px;}"
            << "table{border-collapse:collapse;width:100%;} th,td{border:1px solid #c9d1dc;padding:5px;vertical-align:top;}"
            << "th{background:#eef3f9;} .muted{color:#5f6b7a;}"
            << "</style></head><body>";
        out << "<h1>iSkating 训练复盘报告</h1>";
        out << "<p class='muted'>" << record.time.toHtmlEscaped() << " · "
            << record.athleteName.toHtmlEscaped() << " · "
            << record.actionName.toHtmlEscaped() << "</p>";
        out << "<h2>训练摘要</h2><table>";
        const QVector<std::pair<QString, QString>> summaryRows = {
            {QStringLiteral("教练"), record.coachName.isEmpty() ? QStringLiteral("未指定") : record.coachName},
            {QStringLiteral("比赛"), QStringLiteral("%1 / %2 / %3 / %4")
                                      .arg(record.competitionName.trimmed().isEmpty() ? QStringLiteral("未关联比赛") : record.competitionName.trimmed(),
                                           record.competitionLocation.trimmed().isEmpty() ? QStringLiteral("未填写地点") : record.competitionLocation.trimmed(),
                                           record.competitionDate.isValid() ? record.competitionDate.toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("未填写日期"),
                                           record.competitionType.trimmed().isEmpty() ? QStringLiteral("未填写类型") : record.competitionType.trimmed())},
            {QStringLiteral("场次/项目/轮次/分组"), QStringLiteral("%1 / %2 / %3 / %4")
                                                   .arg(record.raceName.trimmed().isEmpty() ? QStringLiteral("未关联场次") : record.raceName.trimmed(),
                                                        record.eventName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.eventName.trimmed(),
                                                        record.heatName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.heatName.trimmed(),
                                                        record.groupName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.groupName.trimmed())},
            {QStringLiteral("参赛号/道次/成绩/名次"), QStringLiteral("%1 / %2 / %3 / %4")
                                                    .arg(record.bibNumber.trimmed().isEmpty() ? QStringLiteral("未填写") : record.bibNumber.trimmed(),
                                                         record.laneNumber.trimmed().isEmpty() ? QStringLiteral("未填写") : record.laneNumber.trimmed(),
                                                         record.resultScore >= 0 ? QString::number(record.resultScore) : QStringLiteral("未填"),
                                                         record.resultRank >= 0 ? QString::number(record.resultRank) : QStringLiteral("未填"))},
            {QStringLiteral("分析归属"), QStringLiteral("%1 / %2")
                                      .arg(sessionSourceTypeLabel(record.sourceType),
                                           sessionSourceDisplay(record))},
            {QStringLiteral("动作"), QStringLiteral("%1 / %2 v%3").arg(record.actionCategory, record.actionName).arg(record.standardVersion)},
            {QStringLiteral("场地/阶段/目标"), QStringLiteral("%1 / %2 / %3")
                                              .arg(record.site.isEmpty() ? QStringLiteral("未填写") : record.site,
                                                   record.trainingPhase.isEmpty() ? QStringLiteral("未填写") : record.trainingPhase,
                                                   record.goal.isEmpty() ? QStringLiteral("未填写") : record.goal)},
            {QStringLiteral("训练备注"), record.notes.trimmed().isEmpty() ? QStringLiteral("未填写") : record.notes.trimmed()},
            {QStringLiteral("时长"), formatTime(record.duration)},
            {QStringLiteral("完成度"), QStringLiteral("%1/%2，目标 %3 次/%4 分")
                                       .arg(record.validReps)
                                       .arg(record.totalReps)
                                       .arg(record.targetReps)
                                       .arg(record.targetScore)},
            {QStringLiteral("复核后平均/最佳分"), QStringLiteral("%1/%2").arg(record.score).arg(record.bestScore)},
            {QStringLiteral("视频"), displayMediaSource(record.videoSource)},
            {QStringLiteral("视频资产"), videoFilesSummary(record.videoFiles)},
            {QStringLiteral("离线分析任务"), analysisTaskSummary(record)}
        };
        for (const auto &row : summaryRows) {
            out << "<tr><th>" << row.first.toHtmlEscaped() << "</th><td>" << row.second.toHtmlEscaped() << "</td></tr>";
        }
        out << "</table>";
        out << "<h2>综合反馈</h2>" << htmlParagraph(record.feedback);
        out << "<h2>教练批注</h2>" << htmlParagraph(record.coachComment);
        out << "<h2>动作明细</h2>";
        if (repetitions.isEmpty()) {
            out << "<p>本次未保存动作实例。</p>";
        } else {
            out << "<table><tr><th>#</th><th>视频</th><th>来源</th><th>复核</th><th>时间</th><th>AI原始</th><th>复核后</th><th>错误项</th><th>反馈/备注</th></tr>";
            for (int i = 0; i < repetitions.size(); ++i) {
                const ActionRepetition &rep = repetitions.at(i);
                out << "<tr><td>" << (i + 1) << "</td>"
                    << "<td>#" << rep.videoIndex << "</td>"
                    << "<td>" << sourceLabel(rep).toHtmlEscaped() << "</td>"
                    << "<td>" << reviewStatusLabel(rep).toHtmlEscaped() << "</td>"
                    << "<td>" << QStringLiteral("%1-%2")
                                      .arg(formatMilliseconds(rep.effectiveStartedMs()),
                                           formatMilliseconds(rep.effectiveEndedMs()))
                                      .toHtmlEscaped()
                    << "</td>"
                    << "<td>" << QStringLiteral("%1分/%2").arg(rep.score).arg(boolText(rep.valid)).toHtmlEscaped() << "</td>"
                    << "<td>" << QStringLiteral("%1分/%2").arg(rep.effectiveScore()).arg(boolText(rep.effectiveValid())).toHtmlEscaped() << "</td>"
                    << "<td>" << issueSummary(rep.effectiveErrorCodes()).toHtmlEscaped() << "</td>"
                    << "<td>" << QStringLiteral("%1<br>%2")
                                      .arg(rep.effectiveFeedback().trimmed().isEmpty() ? QStringLiteral("无反馈") : rep.effectiveFeedback().trimmed(),
                                           rep.coachNote.trimmed().isEmpty() ? QStringLiteral("无备注") : rep.coachNote.trimmed())
                                      .toHtmlEscaped()
                                      .replace(QStringLiteral("&lt;br&gt;"), QStringLiteral("<br>"))
                    << "</td></tr>";
            }
            out << "</table>";
        }
        out << "</body></html>";
        document.setHtml(html);
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(filePath);
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
        document.print(&printer);
        ok = true;
    } else {
        filePath = withFileSuffix(filePath, QStringLiteral("md"));
        ok = writeTextFile(filePath, buildMarkdown());
    }

    if (!ok) {
        return;
    }

    ui->saveTipLabel->setText(QStringLiteral("训练报告已导出：%1").arg(QDir::toNativeSeparators(filePath)));
    ui->saveTipLabel->show();
}

void MainWindow::openMetricReportCenter()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("无法生成专项指标报告。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("报告中心 · 专项指标"));
    dialog.resize(680, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *fromDate = new QDateEdit(QDate::currentDate().addMonths(-1), &dialog);
    auto *toDate = new QDateEdit(QDate::currentDate(), &dialog);
    auto *participant = new QComboBox(&dialog);
    auto *fromMs = new QSpinBox(&dialog);
    auto *toMs = new QSpinBox(&dialog);
    for (QDateEdit *edit : {fromDate, toDate}) { edit->setCalendarPopup(true); edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd")); }
    for (QSpinBox *spin : {fromMs, toMs}) { spin->setRange(0, 24 * 60 * 60 * 1000); spin->setSingleStep(1000); }
    toMs->setValue(24 * 60 * 60 * 1000);
    participant->addItem(QStringLiteral("全部参与者"), QString());
    QSet<QString> participantIds;
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        for (const TrainingSessionParticipant &item : record.participants) {
            if (!item.id.isEmpty() && !participantIds.contains(item.id)) {
                participantIds.insert(item.id);
                participant->addItem(item.athleteName.isEmpty() ? item.athleteId : item.athleteName, item.id);
            }
        }
    }
    form->addRow(QStringLiteral("训练保存日期"), [&]() { auto *row = new QWidget(&dialog); auto *line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0); line->addWidget(fromDate); line->addWidget(new QLabel(QStringLiteral("至"), row)); line->addWidget(toDate); return row; }());
    form->addRow(QStringLiteral("参与者"), participant);
    form->addRow(QStringLiteral("训练内时间(ms)"), [&]() { auto *row = new QWidget(&dialog); auto *line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0); line->addWidget(fromMs); line->addWidget(new QLabel(QStringLiteral("至"), row)); line->addWidget(toMs); return row; }());
    layout->addLayout(form);
    auto *track = new QCheckBox(QStringLiteral("轨迹"), &dialog);
    auto *speed = new QCheckBox(QStringLiteral("速度"), &dialog);
    auto *angle = new QCheckBox(QStringLiteral("关节角"), &dialog);
    auto *angularVelocity = new QCheckBox(QStringLiteral("角速度"), &dialog);
    for (QCheckBox *box : {track, speed, angle, angularVelocity}) box->setChecked(true);
    auto *types = new QHBoxLayout();
    types->addWidget(new QLabel(QStringLiteral("导出指标"), &dialog));
    for (QCheckBox *box : {track, speed, angle, angularVelocity}) types->addWidget(box);
    types->addStretch();
    layout->addLayout(types);
    auto *hint = new QLabel(QStringLiteral("CSV 为逐点明细；PDF 为所选 session 的验收汇总。旧记录没有专项数据时会明确标记。"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *csvButton = buttons->addButton(QStringLiteral("导出 CSV"), QDialogButtonBox::ActionRole);
    auto *pdfButton = buttons->addButton(QStringLiteral("导出 PDF"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto exportReport = [&](const QString &suffix) {
        if (!track->isChecked() && !speed->isChecked() && !angle->isChecked() && !angularVelocity->isChecked()) {
            QMessageBox::information(&dialog, QStringLiteral("请选择指标"), QStringLiteral("至少选择一种专项指标。")); return;
        }
        QString path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("导出专项指标报告"),
                                                    QDir::homePath() + QStringLiteral("/iSkating-metrics.") + suffix,
                                                    suffix == QStringLiteral("pdf") ? QStringLiteral("PDF 报告 (*.pdf)") : QStringLiteral("CSV 明细 (*.csv)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".") + suffix, Qt::CaseInsensitive)) path += QStringLiteral(".") + suffix;
        struct Row { QString sessionId; QString participantId; QString athlete; QString savedDate; qint64 t; int camera; QString type; QString name; QString value; QString unit; bool valid; QString version; double confidence; };
        QVector<Row> rows;
        const QString wantedParticipant = participant->currentData().toString();
        const int start = fromMs->value(), end = toMs->value();
        for (const SessionHistoryItem &record : std::as_const(m_records)) {
            const QDate date = QDate::fromString(record.time.left(10), QStringLiteral("yyyy-MM-dd"));
            if (date.isValid() && (date < fromDate->date() || date > toDate->date())) continue;
            QHash<QString, QString> names;
            for (const TrainingSessionParticipant &item : record.participants) names.insert(item.id, item.athleteName);
            if (track->isChecked()) for (const TrackPoint &point : m_trainingRepository->trackPointsForSession(record.id, wantedParticipant, start, end)) rows.append({record.id, point.participantId, names.value(point.participantId), record.time, point.timestampMs, point.cameraId, QStringLiteral("trajectory"), QStringLiteral("position"), QStringLiteral("%1;%2;%3").arg(point.x).arg(point.y).arg(point.z), QStringLiteral("m"), true, point.speedSource, point.confidence});
            if (speed->isChecked()) for (const SpeedMetric &metric : m_trainingRepository->speedMetricsForSession(record.id, wantedParticipant, start, end)) rows.append({record.id, metric.participantId, names.value(metric.participantId), record.time, metric.timestampMs, metric.cameraId, QStringLiteral("speed"), QStringLiteral("smoothed_speed"), QString::number(metric.smoothedSpeedMps), metric.unit, metric.valid, metric.algorithmVersion, -1});
            if (angle->isChecked() || angularVelocity->isChecked()) for (const JointMetric &metric : m_trainingRepository->jointMetricsForSession(record.id, wantedParticipant, start, end)) {
                const QString label = metric.side + QStringLiteral("_") + metric.joint;
                if (angle->isChecked()) rows.append({record.id, metric.participantId, names.value(metric.participantId), record.time, metric.timestampMs, metric.cameraId, QStringLiteral("joint_angle"), label, QString::number(metric.angleDeg), QStringLiteral("deg"), metric.valid, metric.algorithmVersion, metric.confidence});
                if (angularVelocity->isChecked()) rows.append({record.id, metric.participantId, names.value(metric.participantId), record.time, metric.timestampMs, metric.cameraId, QStringLiteral("angular_velocity"), label, QString::number(metric.angularVelocityDegPerSec), QStringLiteral("deg/s"), metric.valid, metric.algorithmVersion, metric.confidence});
            }
        }
        if (suffix == QStringLiteral("csv")) {
            QFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(&dialog, QStringLiteral("导出失败"), file.errorString()); return; }
            QTextStream out(&file); out.setEncoding(QStringConverter::Utf8);
            out << "session_id,participant_id,athlete,saved_at,t_ms,camera_id,metric_type,metric_name,value,unit,valid,algorithm_version,confidence\n";
            for (const Row &row : rows) out << csvField(row.sessionId) << ',' << csvField(row.participantId) << ',' << csvField(row.athlete) << ',' << csvField(row.savedDate) << ',' << row.t << ',' << row.camera << ',' << csvField(row.type) << ',' << csvField(row.name) << ',' << csvField(row.value) << ',' << csvField(row.unit) << ',' << (row.valid ? "true" : "false") << ',' << csvField(row.version) << ',' << (row.confidence >= 0 ? QString::number(row.confidence) : QString()) << '\n';
        } else {
            QHash<QString, int> counts; for (const Row &row : rows) ++counts[row.type];
            QString html = QStringLiteral("<html><meta charset='utf-8'><style>body{font-family:'Microsoft YaHei';font-size:10pt}table{border-collapse:collapse;width:100%%}td,th{border:1px solid #c9d1dc;padding:5px}th{background:#eef3f9}</style><body><h1>iSkating 专项指标验收报告</h1><p>保存日期：%1 至 %2；训练内时间：%3-%4 ms；参与者：%5</p><h2>指标覆盖量</h2><table><tr><th>指标</th><th>有效数据点</th></tr>").arg(fromDate->date().toString(Qt::ISODate), toDate->date().toString(Qt::ISODate)).arg(start).arg(end).arg(participant->currentText());
            for (const QString &type : {QStringLiteral("trajectory"), QStringLiteral("speed"), QStringLiteral("joint_angle"), QStringLiteral("angular_velocity")}) html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(type, QString::number(counts.value(type)));
            html += rows.isEmpty() ? QStringLiteral("</table><p>所选范围没有可用专项指标数据。</p></body></html>") : QStringLiteral("</table><p>逐点明细请导出 CSV。</p></body></html>");
            QTextDocument document; document.setHtml(html); QPrinter printer(QPrinter::HighResolution); printer.setOutputFormat(QPrinter::PdfFormat); printer.setOutputFileName(path); printer.setPageSize(QPageSize(QPageSize::A4)); document.print(&printer);
        }
        ui->saveTipLabel->setText(QStringLiteral("专项指标报告已导出：%1（%2 条）").arg(QDir::toNativeSeparators(path)).arg(rows.size())); ui->saveTipLabel->show();
    };
    connect(csvButton, &QPushButton::clicked, &dialog, [&]() { exportReport(QStringLiteral("csv")); });
    connect(pdfButton, &QPushButton::clicked, &dialog, [&]() { exportReport(QStringLiteral("pdf")); });
    dialog.exec();
}

// 根据侧栏显示状态刷新顶部侧栏图标按钮，并同步无障碍提示。
void MainWindow::refreshSidebarButton()
{
    const QString label = m_sidebarVisible ? QStringLiteral("隐藏侧栏") : QStringLiteral("显示侧栏");
    ui->toggleSidebarButton->setText(QString());
    if (auto *button = qobject_cast<AnimatedButton *>(ui->toggleSidebarButton)) {
        button->setIconSource(QStringLiteral(":/icons/sidebar.svg"));
    }
    ui->toggleSidebarButton->setToolTip(label);
    ui->toggleSidebarButton->setStatusTip(label);
    ui->toggleSidebarButton->setAccessibleName(label);
    configureStableButton(ui->toggleSidebarButton, 36, 36, QSize(20, 20));
}

// 根据当前窗口状态刷新顶部全屏图标按钮，并同步无障碍提示。
void MainWindow::refreshFullScreenButton()
{
    if (!m_fullScreenButton) {
        return;
    }

    const bool fullScreen = isFullScreen();
    const QString label = fullScreen ? QStringLiteral("退出全屏") : QStringLiteral("F11全屏");
    const QString iconPath = fullScreen
                                 ? QStringLiteral(":/icons/exit_fullscreen.svg")
                                 : QStringLiteral(":/icons/fullscreen.svg");

    m_fullScreenButton->setText(QString());
    if (auto *button = qobject_cast<AnimatedButton *>(m_fullScreenButton)) {
        button->setIconSource(iconPath);
    }
    m_fullScreenButton->setToolTip(fullScreen
                                       ? QStringLiteral("退出全屏显示（Esc）")
                                       : QStringLiteral("进入全屏显示（F11）"));
    m_fullScreenButton->setStatusTip(m_fullScreenButton->toolTip());
    m_fullScreenButton->setAccessibleName(label);
    configureStableButton(m_fullScreenButton, 36, 36, QSize(20, 20));
}

void MainWindow::refreshModelStatus(const QString &statusText)
{
    if (!ui || !ui->modelStatusLabel) {
        return;
    }
    const QString status = statusText.trimmed();
    const bool warning = status.contains(QStringLiteral("error"), Qt::CaseInsensitive)
                         || status.contains(QStringLiteral("fail"), Qt::CaseInsensitive)
                         || status.contains(QStringLiteral("错误"))
                         || status.contains(QStringLiteral("失败"));
    const bool ready = status.contains(QStringLiteral("已就绪")) || status.contains(QStringLiteral("运行中"));
    const bool initializing = status.contains(QStringLiteral("初始化中"));
    const bool modelFailure = warning && !ready && !status.contains(QStringLiteral("取帧失败"));
    if (ready) {
        m_aiAnalysisReady = true;
    } else if (initializing || modelFailure) {
        m_aiAnalysisReady = false;
        m_identityRecognitionKnown = false;
        m_identityRecognitionAvailable = false;
    }
    if (m_aiAnalysisReady && status.contains(QStringLiteral("PersonViT"))) {
        m_identityRecognitionKnown = true;
        m_identityRecognitionAvailable = !status.contains(QStringLiteral("不可用")) && !warning;
    }
    ui->modelStatusLabel->setText(m_aiAnalysisReady ? QStringLiteral("已就绪")
                                                     : (modelFailure ? QStringLiteral("不可用") : QStringLiteral("—")));
    ui->modelStatusLabel->setToolTip(status);
    ui->modelStatusLabel->setProperty("state", m_aiAnalysisReady ? "online" : (modelFailure ? "warning" : "muted"));
    if (m_identityAvailabilityLabel) {
        m_identityAvailabilityLabel->setText(m_identityRecognitionAvailable
                                                 ? QStringLiteral("可用")
                                                 : (m_identityRecognitionKnown ? QStringLiteral("不可用")
                                                                               : QStringLiteral("—")));
        m_identityAvailabilityLabel->setProperty("state", m_identityRecognitionAvailable ? "online"
                                                                                           : (warning ? "warning" : "muted"));
        m_identityAvailabilityLabel->setToolTip(status);
        repolish(m_identityAvailabilityLabel);
    }
    repolish(ui->modelStatusLabel);

    m_aiModelFailed = modelFailure;
    m_lastModelStatusText = statusText;
    refreshAiCapabilityStatus();
}

void MainWindow::refreshAiCapabilityStatus()
{
    ui->aiAnalysisValue1->setText(m_aiAnalysisReady ? QStringLiteral("可用")
                                                  : (m_aiModelFailed ? QStringLiteral("不可用") : QStringLiteral("初始化中")));
    ui->aiAnalysisValue1->setProperty("state", m_aiAnalysisReady ? "success" : (m_aiModelFailed ? "error" : "muted"));
    ui->aiAnalysisValue2->setText(m_identityRecognitionKnown
                                    ? (m_identityRecognitionAvailable ? QStringLiteral("可用") : QStringLiteral("不可用"))
                                    : QStringLiteral("—"));
    ui->aiAnalysisValue2->setProperty("state", m_identityRecognitionKnown
                                                 ? (m_identityRecognitionAvailable ? "success" : "warning")
                                                 : "muted");
    ui->aiAnalysisValue3->setText(m_aiAnalysisReady ? QStringLiteral("已就绪")
                                                  : (m_aiModelFailed ? QStringLiteral("不可用") : QStringLiteral("初始化中")));
    ui->aiAnalysisValue3->setProperty("state", m_aiAnalysisReady ? "success" : (m_aiModelFailed ? "error" : "muted"));
    ui->aiAnalysisValue3->setToolTip(m_lastModelStatusText);
    for (QLabel *label : {ui->aiAnalysisValue1, ui->aiAnalysisValue2, ui->aiAnalysisValue3}) {
        repolish(label);
    }
}

void MainWindow::startOfflineAnalysisOverlay(const SessionHistoryItem &record, int cameraId)
{
    clearOfflineAnalysisOverlay();
    if (record.analysisRunId.trimmed().isEmpty() || !m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }
    m_analysisOverlayRunId = record.analysisRunId.trimmed();
    m_analysisOverlayCameraId = std::clamp(cameraId > 0 ? cameraId : 1, 1, 12);
    m_analysisOverlayTimer.start();
    refreshOfflineAnalysisOverlay();
}

void MainWindow::refreshOfflineAnalysisOverlay()
{
    if (m_analysisOverlayRunId.isEmpty() || !ui->mainImageLabel || !ui->mainImageLabel->isPlaying()) {
        return;
    }
    const qint64 positionMs = ui->mainImageLabel->positionMs();
    if (positionMs < 0) {
        return;
    }
    const bool needsWindow = m_analysisOverlayWindow.runId != m_analysisOverlayRunId
                             || positionMs < m_analysisOverlayWindow.fromMs + 1000
                             || positionMs > m_analysisOverlayWindow.toMs - 1000;
    if (needsWindow) {
        const qint64 fromMs = std::max<qint64>(0, positionMs - 5000);
        const qint64 toMs = fromMs + 10000;
        QString error;
        m_analysisOverlayWindow = m_trainingRepository->offlineAnalysisFrames(m_analysisOverlayRunId,
                                                                               m_analysisOverlayCameraId,
                                                                               fromMs,
                                                                               toMs,
                                                                               &error);
        if (m_analysisOverlayWindow.runId.isEmpty()) {
            m_analysisOverlayWindow.runId = m_analysisOverlayRunId;
            m_analysisOverlayWindow.fromMs = fromMs;
            m_analysisOverlayWindow.toMs = toMs;
            ui->mainImageLabel->setAthleteFrame({});
            ui->saveTipLabel->setText(QStringLiteral("完整帧率分析结果暂不可用：%1").arg(error));
            ui->saveTipLabel->show();
            return;
        }
    }
    for (const auto &gap : std::as_const(m_analysisOverlayWindow.gaps)) {
        if (positionMs >= gap.first && positionMs <= gap.second) {
            ui->mainImageLabel->setAthleteFrame({});
            ui->saveTipLabel->setText(QStringLiteral("当前回放区间尚未完成完整帧率分析。"));
            ui->saveTipLabel->show();
            return;
        }
    }
    const OfflineAnalysisFrame *closest = nullptr;
    qint64 closestDistance = std::numeric_limits<qint64>::max();
    for (const OfflineAnalysisFrame &frame : std::as_const(m_analysisOverlayWindow.frames)) {
        const qint64 distance = std::abs(frame.batchTimeMs - positionMs);
        if (distance < closestDistance) {
            closest = &frame;
            closestDistance = distance;
        }
    }
    if (!closest || closestDistance > 50) {
        ui->mainImageLabel->setAthleteFrame({});
        return;
    }
    AthleteFrameResult result;
    result.cameraId = closest->cameraId;
    result.timestampMs = closest->batchTimeMs;
    result.frameSize = QSize(closest->width, closest->height);
    for (const OfflineAnalysisObject &object : closest->objects) {
        AthleteInstance instance;
        instance.classId = object.classId;
        instance.trackId = static_cast<int>(std::min<qint64>(object.trackId, std::numeric_limits<int>::max()));
        instance.box = QRectF(object.bboxX, object.bboxY, object.bboxWidth, object.bboxHeight);
        instance.detectionConfidence = static_cast<float>(object.detectionConfidence);
        instance.athleteId = object.athleteId;
        instance.label = object.label;
        instance.identityStatus = object.identityStatus;
        instance.identityConfidence = static_cast<float>(object.identityConfidence);
        instance.identitySource = object.identitySource;
        result.instances.append(instance);
    }
    ui->mainImageLabel->setAthleteFrame(result);
}

void MainWindow::clearOfflineAnalysisOverlay()
{
    m_analysisOverlayTimer.stop();
    m_analysisOverlayRunId.clear();
    m_analysisOverlayCameraId = 0;
    m_analysisOverlayWindow = {};
}

void MainWindow::clearRealtimeAnalysisFrame()
{
    ui->mainImageLabel->setAthleteFrame({});
    m_lastAthleteFrame = {};
    m_lastSelectedFrameReceivedAtMsec = 0;
    refreshStats();
}

// 重新应用指定控件的 QSS，用于动态属性变化后立即刷新外观。
void MainWindow::repolish(QWidget *widget) const
{
    if (!widget || !widget->style()) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// 将整数补齐为两位文本，常用于摄像头编号或时间格式。
QString MainWindow::pad(int num) const
{
    return QStringLiteral("%1").arg(num, 2, 10, QLatin1Char('0'));
}

// 将秒数格式化为 mm:ss，显示在训练时长统计中。
QString MainWindow::formatTime(int seconds) const
{
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString MainWindow::formatMilliseconds(int milliseconds) const
{
    const int clamped = std::max(0, milliseconds);
    const int totalSeconds = clamped / 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg((clamped % 1000) / 100, 1, 10, QLatin1Char('0'));
}


// 将模型精度配置值转换成界面上显示的中文名称。
QString MainWindow::precisionLabel(const QString &value) const
{
    if (value == QLatin1String("high")) {
        return QStringLiteral("高精度");
    }
    if (value == QLatin1String("balanced")) {
        return QStringLiteral("均衡");
    }
    if (value == QLatin1String("fast")) {
        return QStringLiteral("高速");
    }
    return value;
}
