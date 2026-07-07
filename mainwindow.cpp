#include "mainwindow.h"
#include "actionstandardscorer.h"
#include "handanalysismanager.h"
#include "iconutils.h"
#include "nvrplayback.h"
#include "offlinevideoprobe.h"
#include "personmanagementdialog.h"
#include "poseidentityresolver.h"
#include "posestandardnessscorer.h"
#include "skeletonviewwidget.h"
#include "trainingreviewdialog.h"
#include "trainingrepository.h"
#include "trajectorywidget.h"
#include "ui_mainwindow.h"
#include "videoopenglwidget.h"

#include <QAction>
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
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPageLayout>
#include <QPageSize>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QProgressBar>
#include <QPushButton>
#include <QDebug>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStringConverter>
#include <QStringList>
#include <QStyle>
#include <QTextDocument>
#include <QTextStream>
#include <QTime>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QAbstractItemView>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include "xlsxdocument.h"
#include "xlsxformat.h"

namespace {

constexpr int kCapturePage = 0;
constexpr int kHistoryPage = 1;
constexpr int kSuggestionPage = 2;
constexpr int kDefaultFps = 30;
constexpr int kDefaultPreviewStreamFps = 30;
constexpr int kDefaultMainStreamFps = 120;
constexpr int kDefaultRtspPort = 554;
constexpr int kTopIconButtonWidth = 92;
constexpr int kOperationRailWidth = 110;
constexpr int kOperationButtonHeight = 72;
constexpr int kHistoryActionButtonWidth = 92;
constexpr int kScoreTagWidth = 68;
constexpr const char *kPreviousWindowStateProperty = "previousWindowStateBeforeFullScreen";
constexpr const char *kMutedInactiveColor = "#8c8c8c";
constexpr const char *kHoverActionColor = "#3b8dff";

enum TrajectoryMode {
    TrajectoryNormal = 0,   // 只占用三维轨迹原本所在区域。
    TrajectoryExpanded = 1, // 占用自身区域和上方 12 路视频区域。
    TrajectoryMinimized = 2 // 只保留“三维轨迹”标题栏和右侧符号。
};

QString cameraSettingsGroup(int cameraIndex)
{
    return QStringLiteral("cameras/camera%1").arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
}

QString defaultCameraChannelName(int cameraIndex)
{
    return QStringLiteral("CAM %1").arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
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

int trendMetricValue(const TrainingTrendWindow &trend, const QString &metricName)
{
    if (metricName == QStringLiteral("关键点")) {
        return trend.detectionScore;
    }
    if (metricName == QStringLiteral("对称")) {
        return trend.symmetryScore;
    }
    if (metricName == QStringLiteral("重心")) {
        return trend.balanceScore;
    }
    if (metricName == QStringLiteral("稳定")) {
        return trend.stabilityScore;
    }
    if (metricName == QStringLiteral("3D")) {
        return trend.depthScore;
    }
    return 0;
}

QString trendWindowLine(const TrainingTrendWindow &trend)
{
    return QStringLiteral("近 %1 天：训练 %2 次 · 均分 %3 · 最佳 %4 · 完成动作 %5 个")
        .arg(trend.days)
        .arg(trend.sessionCount)
        .arg(trend.averageScore)
        .arg(trend.bestScore)
        .arg(trend.completedReps);
}

bool trendHasMetricData(const TrainingTrendWindow &trend)
{
    return trend.detectionScore > 0
           || trend.symmetryScore > 0
           || trend.balanceScore > 0
           || trend.stabilityScore > 0
           || trend.depthScore > 0;
}

QString weakTrendSummary(const TrainingTrendWindow &recentTrend,
                         const TrainingTrendWindow &baselineTrend)
{
    if (recentTrend.sessionCount <= 0) {
        return QStringLiteral("近 7 天暂无训练记录，先保存一次训练后即可生成弱项变化。");
    }
    if (!trendHasMetricData(recentTrend)) {
        return QStringLiteral("近 7 天暂无可用分项分，继续采集有效动作后会补齐弱项变化。");
    }
    if (recentTrend.weakestMetricName.trimmed().isEmpty()) {
        return QStringLiteral("弱项数据不足，继续采集有效动作后会补齐分项趋势。");
    }

    const int recentScore = recentTrend.weakestMetricScore;
    const int baselineScore = trendMetricValue(baselineTrend, recentTrend.weakestMetricName);
    if (baselineTrend.sessionCount <= 0 || baselineScore <= 0) {
        return QStringLiteral("当前弱项为%1（%2 分），暂无近 30 天对照基线。")
            .arg(recentTrend.weakestMetricName)
            .arg(recentScore);
    }

    const int delta = recentScore - baselineScore;
    if (std::abs(delta) <= 2) {
        return QStringLiteral("当前弱项为%1（%2 分），相对近 30 天基本持平。")
            .arg(recentTrend.weakestMetricName)
            .arg(recentScore);
    }
    if (delta > 0) {
        return QStringLiteral("当前弱项仍是%1，但近 7 天比近 30 天提升 %2 分。")
            .arg(recentTrend.weakestMetricName)
            .arg(delta);
    }
    return QStringLiteral("当前弱项为%1，近 7 天比近 30 天下滑 %2 分，需要优先回看最近动作。")
        .arg(recentTrend.weakestMetricName)
        .arg(-delta);
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
    return QStringLiteral("%1. %2-%3  %4分  %5%6  错误：%7  反馈：%8")
        .arg(index + 1)
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
    if (!record.sourceLabel.trimmed().isEmpty()) {
        return record.sourceLabel.trimmed();
    }
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
        return displayMediaSource(record.sourceRef.trimmed().isEmpty() ? record.videoSource : record.sourceRef);
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

QString topbarModuleHtml(const QString &icon, const QString &title, const QString &value)
{
    return QStringLiteral(
               "<table cellspacing='0' cellpadding='0'>"
               "<tr>"
               "<td rowspan='2' style='padding-right:8px; color:#9fb6d8; font-size:19px; font-weight:800;'>%1</td>"
               "<td style='color:#7fb6ff; font-size:12px; font-weight:700;'>%2</td>"
               "</tr>"
               "<tr>"
               "<td style='color:#8c8c8c; font-size:13px; font-weight:500;'>%3</td>"
               "</tr>"
               "</table>")
        .arg(icon, title, value);
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

// 递归显示/隐藏布局内容；隐藏侧栏时同时压缩 spacer，确保布局真正收窄。
void setLayoutItemsVisible(QLayout *layout, bool visible)
{
    if (!layout) {
        return;
    }

    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (!item) {
            continue;
        }

        if (auto *childWidget = item->widget()) {
            childWidget->setVisible(visible);
        } else if (auto *childLayout = item->layout()) {
            setLayoutItemsVisible(childLayout, visible);
        } else if (auto *spacer = item->spacerItem()) {
            spacer->changeSize(visible ? 20 : 0,
                               visible ? 40 : 0,
                               visible ? QSizePolicy::Minimum : QSizePolicy::Fixed,
                               visible ? QSizePolicy::Expanding : QSizePolicy::Fixed);
        }
    }
}

} // namespace

// 初始化主窗口：装配 UI、视频控件、顶部状态栏、快捷键、右侧动作按钮和默认布局状态。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_cameraButtons = {ui->cameraButton01, ui->cameraButton02, ui->cameraButton03, ui->cameraButton04,
                       ui->cameraButton05, ui->cameraButton06, ui->cameraButton07, ui->cameraButton08,
                       ui->cameraButton09, ui->cameraButton10, ui->cameraButton11, ui->cameraButton12};

    auto setTopbarModule = [](QLabel *label, const QString &icon, const QString &title, const QString &value) {
        label->setTextFormat(Qt::RichText);
        label->setText(QStringLiteral(
                           "<table cellspacing='0' cellpadding='0'>"
                           "<tr>"
                           "<td rowspan='2' style='padding-right:8px; color:#9fb6d8; font-size:19px; font-weight:800;'>%1</td>"
                           "<td style='color:#7fb6ff; font-size:12px; font-weight:700;'>%2</td>"
                           "</tr>"
                           "<tr>"
                           "<td style='color:#8c8c8c; font-size:13px; font-weight:500;'>%3</td>"
                           "</tr>"
                           "</table>")
                           .arg(icon, title, value));
    };

    setTopbarModule(ui->sessionRoundLabel,
                    QStringLiteral("⛸"),
                    QStringLiteral("训练轮次 Session"),
                    QStringLiteral("自由滑训练-第3次"));
    setTopbarModule(ui->sessionTimeLabel,
                    QStringLiteral("🕒"),
                    QStringLiteral("日期/时间 Date/Time"),
                    QStringLiteral("2026-05-20 16:28:34"));
    setTopbarModule(ui->systemStatusLabel,
                    QStringLiteral("⚙"),
                    QStringLiteral("系统状态 System Status"),
                    QStringLiteral("运行中（正常）"));
    setTopbarModule(ui->modelStatusLabel,
                    QStringLiteral("AI"),
                    QStringLiteral("模型状态 Status"),
                    QStringLiteral("人体姿态 AI 初始化中"));
    setTopbarModule(ui->storageStatusLabel,
                    QStringLiteral("DB"),
                    QStringLiteral("存储 Storage"),
                    QStringLiteral("1.82T/4.00TB"));

    m_poseStandardnessScorer = std::make_unique<PoseStandardnessScorer>();
    m_actionStandardScorer = std::make_unique<ActionStandardScorer>();
    m_actionRepetitionTracker = std::make_unique<ActionRepetitionTracker>();
    m_poseIdentityResolver = std::make_unique<PoseIdentityResolver>();
    m_trainingRepository = std::make_unique<TrainingRepository>();
    m_handAnalysisManager = std::make_unique<HandAnalysisManager>(this);
    m_handAnalysisManager->setResultCallback([this](const PoseFrameResult &rawPoseFrame) {
        const PoseFrameResult poseFrame = m_poseIdentityResolver ? m_poseIdentityResolver->resolve(rawPoseFrame) : rawPoseFrame;
        m_lastPoseFrame = poseFrame;
        const bool selectedFrame = poseFrame.cameraId == m_selectedCamera
                                   || (m_selectedCamera == 0 && poseFrame.cameraId == 0);
        if (poseFrame.instances.isEmpty()) {
            if (selectedFrame || poseFrame.cameraId == 0) {
                clearRealtimePose();
            }
            return;
        }

        if (selectedFrame) {
            ui->mainImageLabel->setPoseFrame(poseFrame);
            if (m_skeletonView) {
                m_skeletonView->setPoseFrame(poseFrame);
            }
        }
        if (m_trajectoryWidget) {
            m_trajectoryWidget->setPoseFrame(poseFrame);
        }
        if (m_poseStandardnessScorer) {
            PoseFrameResult scoringFrame = poseFrame;
            const QString primaryAthleteId = selectedAthleteId();
            if (!primaryAthleteId.isEmpty()) {
                for (const PoseInstance &instance : poseFrame.instances) {
                    if (instance.athleteId == primaryAthleteId) {
                        scoringFrame.instances = {instance};
                        break;
                    }
                }
            }
            const PoseStandardnessResult baseStandardness = m_poseStandardnessScorer->scoreFrame(scoringFrame);
            const ActionStandard standard = selectedActionStandard();
            ActionAssessment assessment;
            if (m_actionStandardScorer && !standard.id.isEmpty()) {
                assessment = m_actionStandardScorer->score(baseStandardness, standard);
                m_realtimeScore = assessment.score;
                m_detectionScore = assessment.detectionScore;
                m_symmetryScore = assessment.symmetryScore;
                m_balanceScore = assessment.balanceScore;
                m_stabilityScore = assessment.stabilityScore;
                m_depthScore = assessment.depthScore;
                m_feedbackText = assessment.feedback;
            } else {
                assessment.score = baseStandardness.score;
                assessment.detectionScore = baseStandardness.detectionScore;
                assessment.symmetryScore = baseStandardness.symmetryScore;
                assessment.balanceScore = baseStandardness.balanceScore;
                assessment.stabilityScore = baseStandardness.stabilityScore;
                assessment.depthScore = baseStandardness.depthScore;
                assessment.feedback = baseStandardness.feedback;
                assessment.valid = baseStandardness.valid;
                m_realtimeScore = baseStandardness.score;
                m_detectionScore = baseStandardness.detectionScore;
                m_symmetryScore = baseStandardness.symmetryScore;
                m_balanceScore = baseStandardness.balanceScore;
                m_stabilityScore = baseStandardness.stabilityScore;
                m_depthScore = baseStandardness.depthScore;
                m_feedbackText = baseStandardness.feedback;
            }
            if (m_isRecording && !m_isPaused && m_actionRepetitionTracker) {
                ActionRepetition repetition;
                if (m_actionRepetitionTracker->update(scoringFrame,
                                                      assessment,
                                                      QDateTime::currentMSecsSinceEpoch(),
                                                      m_recordingStartedAtMsec,
                                                      &repetition)) {
                    recordCompletedRepetition(repetition);
                }
            }
            refreshStats();
        }
    });
    m_handAnalysisManager->setStatusCallback([this](const QString &statusText) {
        refreshModelStatus(statusText);
    });

    // 在“隐藏侧栏”按钮旁边动态增加全屏按钮，避免修改 .ui 后生成头文件不同步。
    m_fullScreenButton = new QPushButton(ui->toggleSidebarButton->parentWidget());
    m_fullScreenButton->setObjectName(QStringLiteral("fullScreenButton"));
    m_fullScreenButton->setProperty("role", "plain");
    configureStableButton(ui->toggleSidebarButton, kTopIconButtonWidth, 40, QSize(22, 22));
    configureStableButton(m_fullScreenButton, kTopIconButtonWidth, 40, QSize(22, 22));
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

    m_importVideoButton = new QPushButton(ui->leftCard);
    m_importVideoButton->setObjectName(QStringLiteral("importOfflineVideoButton"));
    m_importVideoButton->setProperty("role", "secondaryButton");
    m_importVideoButton->setText(QStringLiteral("导入视频"));
    m_importVideoButton->setIcon(makeNormalizedTintedSvgIcon(QStringLiteral(":/icons/video.svg"),
                                                             QColor(QString::fromLatin1(kMutedInactiveColor)),
                                                             18,
                                                             16));
    m_importVideoButton->setIconSize(QSize(18, 18));
    m_importVideoButton->setToolTip(QStringLiteral("导入本地视频用于姿态分析和训练复盘"));
    m_importVideoButton->setStatusTip(m_importVideoButton->toolTip());
    m_importVideoButton->setAccessibleName(QStringLiteral("导入离线视频"));
    configureStableButton(m_importVideoButton, kHistoryActionButtonWidth, 32, QSize(18, 18));
    const int focusTitleIndex = ui->headlineLayout->indexOf(ui->focusTitleLabel);
    if (focusTitleIndex >= 0) {
        ui->headlineLayout->insertWidget(focusTitleIndex, m_importVideoButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    } else {
        ui->headlineLayout->addWidget(m_importVideoButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
    connect(m_importVideoButton, &QPushButton::clicked, this, [this]() { importOfflineVideo(); });

    ui->mainImageLabel->setPlaceholderText(QStringLiteral("主视频\n未播放"));
    ui->mainImageLabel->setOverlayControlsVisible(false);
    ui->mainImageLabel->setStreamChangedHandler([this](VideoOpenGLWidget *) {
        syncAnalysisStreams();
    });
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        const QString cameraName = defaultCameraChannelName(i);
        cameraWidget->setChannelName(cameraName);
        cameraWidget->setPlaceholderText(cameraName);
        cameraWidget->setOverlayControlsVisible(true);
        cameraWidget->setConfigButtonVisible(false);
        cameraWidget->setDoubleClickHandler([this, i](VideoOpenGLWidget *) {
            qDebug() << "[MainWindow] camera double clicked, play in main view"
                     << m_cameraButtons.at(i)->channelName()
                     << safeUrlForLog(m_cameraButtons.at(i)->mainUrl());
            showCameraInMainView(i);
        });
        cameraWidget->setStreamChangedHandler([this](VideoOpenGLWidget *) {
            syncAnalysisStreams();
        });
    }
    loadCameraSettings();
    initializeTrainingRepository();

    applyStyleSheet();
    installStaticImages();
    installSkeletonView();
    installTrajectoryWidget();
    installMetricBars();
    installTrainingContextPanel();
    installHistorySearchPanel();

    auto *fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullScreenShortcut->setContext(Qt::WindowShortcut);
    connect(fullScreenShortcut, &QShortcut::activated, this, [this]() { toggleFullScreen(); });

    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WindowShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() { exitFullScreenMode(); });

    if (ui->opsCard) {
        ui->opsCard->setMinimumWidth(kOperationRailWidth);
        ui->opsCard->setMaximumWidth(kOperationRailWidth);
        ui->opsCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }

    auto makeButtonAction = [this](QPushButton *button, const QString &label, const QString &iconPath) {
        auto *action = new QAction(makeNormalizedTintedSvgIcon(iconPath,
                                                               QColor(QString::fromLatin1(kMutedInactiveColor)),
                                                               38,
                                                               30),
                                   label,
                                   this);
        action->setToolTip(label);
        action->setStatusTip(label);

        button->setText(QStringLiteral("\n%1").arg(label));
        button->setIcon(action->icon());
        button->setToolTip(label);
        button->setStatusTip(label);
        button->setAccessibleName(label);
        button->setProperty("compactText", label);
        button->setProperty("showTipTextOnHover", true);
        button->installEventFilter(this);
        configureStableButton(button,
                              kOperationRailWidth - 20,
                              kOperationButtonHeight,
                              QSize(30, 30));

        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        return action;
    };

    auto *startAction = makeButtonAction(ui->startCaptureButton, QStringLiteral("开始采集"), QStringLiteral(":/icons/start_cap.svg"));
    auto *pauseAction = makeButtonAction(ui->pauseCaptureButton, QStringLiteral("暂停"), QStringLiteral(":/icons/suspend.svg"));
    auto *stopAction = makeButtonAction(ui->stopCaptureButton, QStringLiteral("停止"), QStringLiteral(":/icons/stop.svg"));
    auto *saveAction = makeButtonAction(ui->saveRecordButton, QStringLiteral("保存记录"), QStringLiteral(":/icons/save.svg"));
    auto *settingsAction = makeButtonAction(ui->settingsButton, QStringLiteral("系统设置"), QStringLiteral(":/icons/settings.svg"));

    connect(startAction, &QAction::triggered, this, [this]() { startCapture(); });
    connect(pauseAction, &QAction::triggered, this, [this]() { pauseCapture(); });
    connect(stopAction, &QAction::triggered, this, [this]() { stopCapture(); });
    connect(saveAction, &QAction::triggered, this, [this]() { saveRecord(); });
    connect(settingsAction, &QAction::triggered, this, [this]() { openSystemSettings(); });

    QWidget *trajectoryView = m_trajectoryWidget
                                  ? static_cast<QWidget *>(m_trajectoryWidget)
                                  : static_cast<QWidget *>(ui->trajectoryViewFrame);
    m_trajectoryViewNormalMinSize = trajectoryView->minimumSize();
    m_trajectoryViewNormalMaxSize = trajectoryView->maximumSize();
    m_trajectoryCardNormalMinSize = ui->trajectoryCard->minimumSize();
    m_trajectoryCardNormalMaxSize = ui->trajectoryCard->maximumSize();
    m_middleLayoutNormalSpacing = ui->middleLayout->spacing();
    m_middleLayoutNormalStretch0 = ui->middleLayout->stretch(0);
    m_middleLayoutNormalStretch1 = ui->middleLayout->stretch(1);
    m_cameraGridNormalSpacing = ui->cameraGridLayout->spacing();
    // 卡片标题按内容宽度显示，避免标题背景在纵向布局里被拉满整行。
    ui->poseLayout->setAlignment(ui->poseTitleLabel, Qt::AlignLeft);
    ui->metricsLayout->setAlignment(Qt::AlignTop);
    ui->metricsLayout->setAlignment(ui->metricsTitleLabel, Qt::AlignLeft);
    ui->metricsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (auto *captureLayout = qobject_cast<QVBoxLayout *>(ui->capturePage->layout())) {
        captureLayout->setStretch(0, 1);
        captureLayout->setStretch(1, 0);
    }
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });

    setupUiState();
    setupConnections();
    setTrajectoryMode(TrajectoryNormal);
    refreshStats();
}

// 释放由 Qt Designer 生成的界面对象。
MainWindow::~MainWindow()
{
    if (m_handAnalysisManager) {
        m_handAnalysisManager->stop();
        m_handAnalysisManager.reset();
    }
    saveCameraSettings();
    delete ui;
}

// 监听图标按钮的悬停状态：只刷新固定宽度内的视觉提示，避免悬浮时改变布局宽度。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->toggleSidebarButton
        && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshSidebarButton();
    } else if (watched == m_fullScreenButton
               && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshFullScreenButton();
    } else if ((event->type() == QEvent::Enter || event->type() == QEvent::Leave)
               && watched->property("showTipTextOnHover").toBool()
               && qobject_cast<QPushButton *>(watched)) {
        repolish(qobject_cast<QWidget *>(watched));
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
    m_navButtons = {ui->navCaptureButton, ui->navHistoryButton, ui->navSuggestionButton};
    for (auto *button : m_navButtons) {
        setRole(button, "nav");
        button->setFocusPolicy(Qt::NoFocus);
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
        for (int fps : {15, 25, 30, 50, 60, 90, 120}) {
            ui->fpsComboBox->addItem(QStringLiteral("%1 FPS").arg(fps), fps);
        }
    }
    for (QLabel *label : {ui->scoreValueLabel, ui->saveTipLabel}) {
        if (label) {
            label->setWordWrap(true);
            label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }
    }
    applyCapturePreferencesToUi();
    reloadTrainingContext();
    ui->settingsBox->hide();
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

// 绑定页面上的交互信号：侧栏开关、全屏切换、三维轨迹三态切换等。
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
    connect(ui->toggleSidebarButton, &QPushButton::clicked, this, [this]() {
        toggleSidebar();
    });
    if (m_fullScreenButton) {
        connect(m_fullScreenButton, &QPushButton::clicked, this, [this]() {
            toggleFullScreen();
        });
    }
    connect(ui->expandTrajectoryButton, &QPushButton::clicked, this, [this]() {
        cycleTrajectoryMode();
    });
    connect(ui->collapseTrajectoryButton, &QPushButton::clicked, this, [this]() {
        cycleTrajectoryMode();
    });
}

// 将资源文件中的静态示例图片贴到界面对应占位控件上。
void MainWindow::installStaticImages()
{
    // 运动员 3D 姿态区只需要 VideoOpenGLWidget 统一重绘背景，不再默认贴 pose 图片或视频占位图。
    ui->poseImageLabelA->setPlaceholderText(QString());
    ui->poseImageLabelA->setPlaceholderIconVisible(false);
    ui->poseImageLabelA->setOverlayControlsVisible(false);
    ui->poseImageLabelB->setPlaceholderText(QString());
    ui->poseImageLabelB->setPlaceholderIconVisible(false);
    ui->poseImageLabelB->setOverlayControlsVisible(false);
}

// 将实时 AI 骨架结果显示到下方“运动员3D骨架”窗口；主视频叠加仍走 D3D surface。
void MainWindow::installSkeletonView()
{
    if (m_skeletonView) {
        return;
    }

    ui->poseTitleLabel->setText(QStringLiteral("运动员3D骨架"));
    ui->poseImageLayout->removeWidget(ui->poseImageLabelA);
    ui->poseImageLayout->removeWidget(ui->poseImageLabelB);
    ui->poseImageLabelA->hide();
    ui->poseImageLabelB->hide();

    m_skeletonView = new SkeletonViewWidget(ui->poseCard);
    ui->poseImageLayout->insertWidget(0, m_skeletonView, 1);
}

void MainWindow::installTrajectoryWidget()
{
    if (m_trajectoryWidget || !ui->trajectoryCardLayout || !ui->trajectoryViewFrame) {
        return;
    }

    ui->trajectoryCardLayout->removeWidget(ui->trajectoryViewFrame);
    ui->trajectoryViewFrame->hide();

    m_trajectoryWidget = new TrajectoryWidget(ui->trajectoryCard);
    m_trajectoryWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->trajectoryCardLayout->insertWidget(1, m_trajectoryWidget, 1);
    updateTrajectoryCameraSegments();
}

// 在训练统计卡片里装配分项评分条，替代原来的单行文字指标。
void MainWindow::installMetricBars()
{
    if (!ui->metricsLayout || !ui->saveTipLabel || !m_metricBars.isEmpty()) {
        return;
    }

    ui->saveTipLabel->hide();

    auto *metricContainer = new QWidget(ui->metricsCard);
    metricContainer->setObjectName(QStringLiteral("metricBarsContainer"));
    auto *metricLayout = new QVBoxLayout(metricContainer);
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setSpacing(4);

    const QVector<QString> metricNames = {
        QStringLiteral("关键点"),
        QStringLiteral("对称"),
        QStringLiteral("重心"),
        QStringLiteral("稳定"),
        QStringLiteral("3D")
    };

    for (const QString &metricName : metricNames) {
        auto *row = new QWidget(metricContainer);
        row->setObjectName(QStringLiteral("metricBarRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto *nameLabel = new QLabel(metricName, row);
        nameLabel->setProperty("role", "metricName");
        nameLabel->setMinimumWidth(38);
        nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto *bar = new QProgressBar(row);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setProperty("role", "metricBar");
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *valueLabel = new QLabel(QStringLiteral("0"), row);
        valueLabel->setProperty("role", "metricValue");
        valueLabel->setMinimumWidth(24);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        rowLayout->addWidget(nameLabel);
        rowLayout->addWidget(bar, 1);
        rowLayout->addWidget(valueLabel);
        metricLayout->addWidget(row);

        m_metricBars.append(bar);
        m_metricValueLabels.append(valueLabel);
    }

    const int scoreBarIndex = ui->metricsLayout->indexOf(ui->scoreProgressBar);
    ui->metricsLayout->insertWidget(scoreBarIndex >= 0 ? scoreBarIndex : ui->metricsLayout->count(),
                                    metricContainer);
}

void MainWindow::installTrainingContextPanel()
{
    if (m_trainingContextPanel || !ui->capturePage || !ui->capturePage->layout()) {
        return;
    }

    auto *captureLayout = qobject_cast<QVBoxLayout *>(ui->capturePage->layout());
    if (!captureLayout) {
        return;
    }

    m_trainingContextPanel = new QFrame(ui->capturePage);
    m_trainingContextPanel->setObjectName(QStringLiteral("trainingContextPanel"));
    setRole(m_trainingContextPanel, "trainingContextPanel");
    auto *panelLayout = new QVBoxLayout(m_trainingContextPanel);
    panelLayout->setContentsMargins(12, 10, 12, 10);
    panelLayout->setSpacing(8);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    auto *titleLabel = new QLabel(QStringLiteral("训练上下文"), m_trainingContextPanel);
    titleLabel->setProperty("role", "sectionTitle");
    auto *editStandardButton = new QPushButton(QStringLiteral("编辑标准"), m_trainingContextPanel);
    auto *managePersonsButton = new QPushButton(QStringLiteral("人员管理"), m_trainingContextPanel);
    auto *manageCompetitionsButton = new QPushButton(QStringLiteral("比赛管理"), m_trainingContextPanel);
    m_trackBindingButton = new QPushButton(QStringLiteral("轨迹绑定"), m_trainingContextPanel);
    editStandardButton->setProperty("role", "secondaryButton");
    managePersonsButton->setProperty("role", "secondaryButton");
    manageCompetitionsButton->setProperty("role", "secondaryButton");
    m_trackBindingButton->setProperty("role", "secondaryButton");
    m_standardDetailLabel = new QLabel(QStringLiteral("动作标准库初始化中"), m_trainingContextPanel);
    m_standardDetailLabel->setProperty("role", "muted");
    m_standardDetailLabel->setWordWrap(true);
    m_standardDetailLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleRow->addWidget(titleLabel, 0);
    titleRow->addWidget(m_standardDetailLabel, 1);
    titleRow->addWidget(managePersonsButton, 0);
    titleRow->addWidget(manageCompetitionsButton, 0);
    titleRow->addWidget(m_trackBindingButton, 0);
    titleRow->addWidget(editStandardButton, 0);
    panelLayout->addLayout(titleRow);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    m_athleteComboBox = new QComboBox(m_trainingContextPanel);
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

    grid->addWidget(new QLabel(QStringLiteral("运动员"), m_trainingContextPanel), 0, 0);
    grid->addWidget(m_athleteComboBox, 0, 1);
    grid->addWidget(addAthleteButton, 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("教练"), m_trainingContextPanel), 0, 3);
    grid->addWidget(m_coachComboBox, 0, 4);
    grid->addWidget(addCoachButton, 0, 5);

    for (int i = 0; i < 3; ++i) {
        auto *combo = new QComboBox(m_trainingContextPanel);
        m_participantComboBoxes.append(combo);
        grid->addWidget(new QLabel(QStringLiteral("参与%1").arg(i + 2), m_trainingContextPanel), 6, i * 2);
        grid->addWidget(combo, 6, i * 2 + 1);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            refreshTrainingContextDetails();
        });
    }

    grid->addWidget(new QLabel(QStringLiteral("比赛"), m_trainingContextPanel), 1, 0);
    grid->addWidget(m_competitionComboBox, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("场次"), m_trainingContextPanel), 1, 2);
    grid->addWidget(m_competitionEventComboBox, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("场地"), m_trainingContextPanel), 1, 4);
    grid->addWidget(m_siteLineEdit, 1, 5);

    grid->addWidget(new QLabel(QStringLiteral("动作"), m_trainingContextPanel), 2, 0);
    grid->addWidget(m_actionStandardComboBox, 2, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("阶段"), m_trainingContextPanel), 2, 3);
    grid->addWidget(m_trainingPhaseComboBox, 2, 4, 1, 2);

    grid->addWidget(new QLabel(QStringLiteral("目标"), m_trainingContextPanel), 3, 0);
    grid->addWidget(m_goalLineEdit, 3, 1, 1, 5);

    grid->addWidget(new QLabel(QStringLiteral("次数"), m_trainingContextPanel), 4, 0);
    grid->addWidget(m_targetRepsSpinBox, 4, 1);
    grid->addWidget(new QLabel(QStringLiteral("目标分"), m_trainingContextPanel), 4, 2);
    grid->addWidget(m_targetScoreSpinBox, 4, 3);
    grid->addWidget(new QLabel(QStringLiteral("组数"), m_trainingContextPanel), 4, 4);
    grid->addWidget(m_setCountSpinBox, 4, 5);

    grid->addWidget(new QLabel(QStringLiteral("休息秒"), m_trainingContextPanel), 5, 0);
    grid->addWidget(m_restSecondsSpinBox, 5, 1);
    m_trainingTargetLabel = new QLabel(QStringLiteral("目标完成度：0/0"), m_trainingContextPanel);
    m_trainingTargetLabel->setProperty("role", "muted");
    m_trainingTargetLabel->setWordWrap(true);
    m_trainingTargetLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    grid->addWidget(m_trainingTargetLabel, 5, 2, 1, 4);

    panelLayout->addLayout(grid);
    panelLayout->addWidget(m_trainingNotesEdit);
    captureLayout->insertWidget(0, m_trainingContextPanel);

    connect(addAthleteButton, &QPushButton::clicked, this, [this]() { addAthleteFromDialog(); });
    connect(addCoachButton, &QPushButton::clicked, this, [this]() { addCoachFromDialog(); });
    connect(managePersonsButton, &QPushButton::clicked, this, [this]() { openPersonManagement(); });
    connect(manageCompetitionsButton, &QPushButton::clicked, this, [this]() { openCompetitionManagement(); });
    connect(m_trackBindingButton, &QPushButton::clicked, this, [this]() { openTrackBindingDialog(); });
    connect(editStandardButton, &QPushButton::clicked, this, [this]() { editActionStandard(); });
    connect(m_actionStandardComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshTrainingContextDetails();
    });
    connect(m_athleteComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshTrainingContextDetails();
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
    auto *titleLabel = new QLabel(QStringLiteral("历史检索"), m_historySearchPanel);
    titleLabel->setProperty("role", "sectionTitle");
    m_historyPageLabel = new QLabel(QStringLiteral("第 1/1 页 · 共 0 条"), m_historySearchPanel);
    m_historyPageLabel->setProperty("role", "muted");
    titleRow->addWidget(titleLabel, 0);
    titleRow->addWidget(m_historyPageLabel, 1, Qt::AlignVCenter);
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
    m_historyPreviousPageButton = new QPushButton(QStringLiteral("上一页"), m_historySearchPanel);
    m_historyNextPageButton = new QPushButton(QStringLiteral("下一页"), m_historySearchPanel);
    for (QPushButton *button : {searchButton, resetButton, repetitionSearchButton, manageCompetitionsButton, m_historyPreviousPageButton, m_historyNextPageButton}) {
        button->setProperty("role", "secondaryButton");
        configureStableButton(button, kHistoryActionButtonWidth, 32, QSize(0, 0));
    }

    grid->addWidget(new QLabel(QStringLiteral("运动员"), m_historySearchPanel), 0, 0);
    grid->addWidget(m_historyAthleteComboBox, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("教练"), m_historySearchPanel), 0, 2);
    grid->addWidget(m_historyCoachComboBox, 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("动作"), m_historySearchPanel), 0, 4);
    grid->addWidget(m_historyActionComboBox, 0, 5);

    grid->addWidget(m_historyFromCheckBox, 1, 0);
    grid->addWidget(m_historyFromDateEdit, 1, 1);
    grid->addWidget(m_historyToCheckBox, 1, 2);
    grid->addWidget(m_historyToDateEdit, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("分数"), m_historySearchPanel), 1, 4);
    grid->addWidget(m_historyMinScoreSpinBox, 1, 5);
    grid->addWidget(m_historyMaxScoreSpinBox, 1, 6);

    grid->addWidget(new QLabel(QStringLiteral("比赛"), m_historySearchPanel), 2, 0);
    grid->addWidget(m_historyCompetitionComboBox, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("场次"), m_historySearchPanel), 2, 2);
    grid->addWidget(m_historyCompetitionEventComboBox, 2, 3);
    grid->addWidget(new QLabel(QStringLiteral("来源"), m_historySearchPanel), 2, 4);
    grid->addWidget(m_historySourceTypeComboBox, 2, 5);
    grid->addWidget(new QLabel(QStringLiteral("关键词"), m_historySearchPanel), 2, 6);
    grid->addWidget(m_historyCompetitionLineEdit, 2, 7, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("排序"), m_historySearchPanel), 2, 9);
    grid->addWidget(m_historySortComboBox, 2, 10, 1, 2);
    grid->addWidget(searchButton, 2, 12);
    grid->addWidget(resetButton, 2, 13);
    grid->addWidget(repetitionSearchButton, 2, 14);
    grid->addWidget(manageCompetitionsButton, 2, 15);
    grid->addWidget(m_historyPreviousPageButton, 2, 16);
    grid->addWidget(m_historyNextPageButton, 2, 17);

    panelLayout->addLayout(grid);
    ui->historyPageLayout->insertWidget(1, m_historySearchPanel);

    connect(m_historyFromCheckBox, &QCheckBox::toggled, m_historyFromDateEdit, &QDateEdit::setEnabled);
    connect(m_historyToCheckBox, &QCheckBox::toggled, m_historyToDateEdit, &QDateEdit::setEnabled);
    connect(searchButton, &QPushButton::clicked, this, [this]() {
        m_historyPageNumber = 1;
        loadTrainingRecords();
        refreshHistory();
        refreshSuggestions();
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() { resetHistorySearch(); });
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

    reloadHistorySearchOptions();
    refreshHistoryPager();
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
    settings.endGroup();
    if (m_capturePreferenceSettings.modelPrecision.isEmpty()) {
        m_capturePreferenceSettings.modelPrecision = QStringLiteral("balanced");
    }

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
    m_capturePreferenceSettings.fps = m_sharedCameraSettings.mainFps;

    applyCameraSettingsToWidgets(false);
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
    settings.setValue(QStringLiteral("fps"), m_sharedCameraSettings.mainFps > 0
                                                ? m_sharedCameraSettings.mainFps
                                                : kDefaultMainStreamFps);
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
        settings.fps = m_sharedCameraSettings.mainFps > 0 ? m_sharedCameraSettings.mainFps : ui->fpsComboBox->currentData().toInt();
    }
    if (settings.fps <= 0) {
        settings.fps = kDefaultMainStreamFps;
    }
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
        const int fpsValue = m_sharedCameraSettings.mainFps > 0 ? m_sharedCameraSettings.mainFps : kDefaultMainStreamFps;
        const int fpsIndex = ui->fpsComboBox->findData(fpsValue);
        ui->fpsComboBox->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : ui->fpsComboBox->findData(kDefaultMainStreamFps));
    }
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setAnalysisProfile(m_capturePreferenceSettings.modelPrecision);
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
    updateTrajectoryCameraSegments();

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

void MainWindow::updateTrajectoryCameraSegments()
{
    if (!m_trajectoryWidget) {
        return;
    }

    QVector<TrajectoryWidget::CameraSegment> segments;
    segments.reserve(m_cameraSlotSettings.size());
    for (int i = 0; i < m_cameraSlotSettings.size(); ++i) {
        const CameraSlotSettings &slot = m_cameraSlotSettings.at(i);
        TrajectoryWidget::CameraSegment segment;
        segment.cameraId = i + 1;
        segment.enabled = slot.trajectoryEnabled && slot.fieldEndM > slot.fieldStartM;
        segment.role = slot.role;
        segment.fieldStartM = slot.fieldStartM;
        segment.fieldEndM = slot.fieldEndM;
        segment.lateralOffsetM = slot.lateralOffsetM;
        segments.append(segment);
    }
    m_trajectoryWidget->setCameraSegments(segments);
}

void MainWindow::syncAnalysisStreams()
{
    if (!m_handAnalysisManager) {
        return;
    }

    m_capturePreferenceSettings = capturePreferenceSettingsFromUi();
    m_handAnalysisManager->setAnalysisProfile(m_capturePreferenceSettings.modelPrecision);
    if (!m_isRecording || m_isPaused) {
        m_handAnalysisManager->setPaused(true);
        m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
        return;
    }

    QVector<HandAnalysisManager::AnalysisStream> streams;
    if (!m_offlineVideoPath.trimmed().isEmpty()) {
        HandAnalysisManager::AnalysisStream stream;
        stream.cameraId = 0;
        stream.sourceName = m_offlineVideoName.trimmed().isEmpty()
                                ? offlineVideoDisplayName(m_offlineVideoPath)
                                : m_offlineVideoName.trimmed();
        stream.stream = ui->mainImageLabel->activeStream();
        if (stream.stream) {
            streams.append(stream);
        }
    } else {
        for (int i = 0; i < m_cameraButtons.size(); ++i) {
            if (i >= m_cameraSlotSettings.size()) {
                continue;
            }
            const CameraSlotSettings &slot = m_cameraSlotSettings.at(i);
            if (!slot.trajectoryEnabled) {
                continue;
            }
            VideoOpenGLWidget *cameraWidget = m_cameraButtons.at(i);
            HandAnalysisManager::AnalysisStream stream;
            stream.cameraId = i + 1;
            stream.sourceName = cameraWidget->channelName();
            stream.stream = (stream.cameraId == m_selectedCamera && ui->mainImageLabel->activeStream())
                                ? ui->mainImageLabel->activeStream()
                                : cameraWidget->activeStream();
            if (stream.stream) {
                streams.append(stream);
            }
        }
    }

    if (streams.isEmpty() && ui->mainImageLabel->activeStream()) {
        HandAnalysisManager::AnalysisStream fallback;
        fallback.cameraId = m_selectedCamera;
        fallback.sourceName = m_selectedCamera > 0
                                  ? QStringLiteral("CAM %1").arg(m_selectedCamera, 2, 10, QLatin1Char('0'))
                                  : m_offlineVideoName;
        fallback.stream = ui->mainImageLabel->activeStream();
        streams.append(fallback);
    }

    m_handAnalysisManager->setActiveStreams(streams);
    m_handAnalysisManager->setPaused(streams.isEmpty());
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

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_sharedCameraSettings = dialog.sharedCameraSettings();
    m_cameraSlotSettings = dialog.cameraSlotSettings();
    m_capturePreferenceSettings = dialog.capturePreferenceSettings();
    m_capturePreferenceSettings.fps = m_sharedCameraSettings.mainFps;
    applyCapturePreferencesToUi();
    applyCameraSettingsToWidgets(true);
    saveCameraSettings();
}

void MainWindow::loadTrainingRecords()
{
    m_records.clear();
    m_historyTotalCount = 0;
    if (m_trainingRepository && m_trainingRepository->isOpen()) {
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
    }
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
        m_historyPageLabel->setText(QStringLiteral("第 %1/%2 页 · 共 %3 条")
                                        .arg(m_historyPageNumber)
                                        .arg(maxPage)
                                        .arg(m_historyTotalCount));
    }
    if (m_historyPreviousPageButton) {
        m_historyPreviousPageButton->setEnabled(m_historyPageNumber > 1);
    }
    if (m_historyNextPageButton) {
        m_historyNextPageButton->setEnabled(m_historyPageNumber < maxPage);
    }
}

void MainWindow::initializeTrainingRepository()
{
    if (!m_trainingRepository) {
        return;
    }

    QString errorMessage;
    if (!m_trainingRepository->open(&errorMessage)) {
        ui->storageStatusLabel->setText(topbarModuleHtml(QStringLiteral("DB"),
                                                         QStringLiteral("存储 Storage"),
                                                         QStringLiteral("训练服务不可用")));
        ui->saveTipLabel->setText(QStringLiteral("训练服务连接失败：%1").arg(errorMessage));
        ui->saveTipLabel->show();
        qWarning() << "[MainWindow] training service open failed" << errorMessage;
        return;
    }

    ui->storageStatusLabel->setText(topbarModuleHtml(QStringLiteral("DB"),
                                                     QStringLiteral("存储 Storage"),
                                                     QStringLiteral("PostgreSQL 服务已就绪")));
    reloadTrainingContext();
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
    refreshTrainingContextDetails();
}

void MainWindow::refreshTrainingContextDetails()
{
    const ActionStandard standard = selectedActionStandard();
    if (!standard.id.isEmpty()) {
        if (m_targetRepsSpinBox && m_targetRepsSpinBox->value() <= 1) {
            m_targetRepsSpinBox->setValue(std::max(1, standard.targetReps));
        } else if (m_targetRepsSpinBox && !m_isRecording && m_actionCount == 0) {
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
        if (m_actionRepetitionTracker) {
            m_actionRepetitionTracker->reset(standard);
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
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("训练数据库未就绪，无法管理比赛。"));
        return;
    }

    const QString previousCompetitionId = selectedCompetitionId();
    const QString previousCompetitionEventId = selectedCompetitionEventId();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("比赛管理"));
    dialog.resize(980, 680);

    auto *layout = new QVBoxLayout(&dialog);
    auto *searchRow = new QHBoxLayout();
    auto *searchEdit = new QLineEdit(&dialog);
    auto *searchButton = new QPushButton(QStringLiteral("查询"), &dialog);
    searchEdit->setPlaceholderText(QStringLiteral("按名称、地点、类型或备注查询"));
    searchButton->setProperty("role", "secondaryButton");
    searchRow->addWidget(searchEdit, 1);
    searchRow->addWidget(searchButton);
    layout->addLayout(searchRow);

    auto *list = new QListWidget(&dialog);
    auto *eventList = new QListWidget(&dialog);
    auto *eventAthleteList = new QListWidget(&dialog);

    auto *listsRow = new QHBoxLayout();
    auto *competitionColumn = new QVBoxLayout();
    competitionColumn->addWidget(new QLabel(QStringLiteral("比赛"), &dialog));
    competitionColumn->addWidget(list, 1);
    auto *eventColumn = new QVBoxLayout();
    eventColumn->addWidget(new QLabel(QStringLiteral("场次/项目/分组"), &dialog));
    eventColumn->addWidget(eventList, 1);
    auto *athleteColumn = new QVBoxLayout();
    athleteColumn->addWidget(new QLabel(QStringLiteral("参赛运动员"), &dialog));
    athleteColumn->addWidget(eventAthleteList, 1);
    listsRow->addLayout(competitionColumn, 1);
    listsRow->addLayout(eventColumn, 1);
    listsRow->addLayout(athleteColumn, 1);
    layout->addLayout(listsRow, 1);

    auto *form = new QFormLayout();
    auto *nameEdit = new QLineEdit(&dialog);
    auto *locationEdit = new QLineEdit(&dialog);
    auto *dateEdit = new QDateEdit(QDate::currentDate(), &dialog);
    auto *typeEdit = new QLineEdit(&dialog);
    auto *notesEdit = new QPlainTextEdit(&dialog);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    notesEdit->setMinimumHeight(70);
    form->addRow(QStringLiteral("名称"), nameEdit);
    form->addRow(QStringLiteral("地点"), locationEdit);
    form->addRow(QStringLiteral("日期"), dateEdit);
    form->addRow(QStringLiteral("类型"), typeEdit);
    form->addRow(QStringLiteral("备注"), notesEdit);
    layout->addLayout(form);

    auto *eventForm = new QFormLayout();
    auto *raceEdit = new QLineEdit(&dialog);
    auto *eventEdit = new QLineEdit(&dialog);
    auto *heatEdit = new QLineEdit(&dialog);
    auto *groupEdit = new QLineEdit(&dialog);
    auto *scheduledEdit = new QDateTimeEdit(QDateTime::currentDateTime(), &dialog);
    auto *eventNotesEdit = new QPlainTextEdit(&dialog);
    scheduledEdit->setCalendarPopup(true);
    scheduledEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    eventNotesEdit->setMinimumHeight(58);
    eventForm->addRow(QStringLiteral("场次"), raceEdit);
    eventForm->addRow(QStringLiteral("项目"), eventEdit);
    eventForm->addRow(QStringLiteral("轮次"), heatEdit);
    eventForm->addRow(QStringLiteral("分组"), groupEdit);
    eventForm->addRow(QStringLiteral("时间"), scheduledEdit);
    eventForm->addRow(QStringLiteral("场次备注"), eventNotesEdit);
    layout->addLayout(eventForm);

    auto *eventAthleteForm = new QFormLayout();
    auto *athleteComboBox = new QComboBox(&dialog);
    auto *bibEdit = new QLineEdit(&dialog);
    auto *laneEdit = new QLineEdit(&dialog);
    auto *sortSpinBox = new QSpinBox(&dialog);
    auto *resultScoreSpinBox = new QSpinBox(&dialog);
    auto *resultRankSpinBox = new QSpinBox(&dialog);
    auto *eventAthleteNotesEdit = new QPlainTextEdit(&dialog);
    sortSpinBox->setRange(0, 999);
    resultScoreSpinBox->setRange(-1, 100);
    resultScoreSpinBox->setSpecialValueText(QStringLiteral("未填"));
    resultRankSpinBox->setRange(-1, 999);
    resultRankSpinBox->setSpecialValueText(QStringLiteral("未填"));
    eventAthleteNotesEdit->setMinimumHeight(58);
    for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
        athleteComboBox->addItem(athlete.name, athlete.id);
    }
    eventAthleteForm->addRow(QStringLiteral("运动员"), athleteComboBox);
    eventAthleteForm->addRow(QStringLiteral("参赛号"), bibEdit);
    eventAthleteForm->addRow(QStringLiteral("道次"), laneEdit);
    eventAthleteForm->addRow(QStringLiteral("排序"), sortSpinBox);
    eventAthleteForm->addRow(QStringLiteral("成绩分"), resultScoreSpinBox);
    eventAthleteForm->addRow(QStringLiteral("名次"), resultRankSpinBox);
    eventAthleteForm->addRow(QStringLiteral("参赛备注"), eventAthleteNotesEdit);
    layout->addLayout(eventAthleteForm);

    auto *buttonRow = new QHBoxLayout();
    auto *newButton = new QPushButton(QStringLiteral("新建"), &dialog);
    auto *saveButton = new QPushButton(QStringLiteral("保存"), &dialog);
    auto *archiveButton = new QPushButton(QStringLiteral("归档"), &dialog);
    auto *newEventButton = new QPushButton(QStringLiteral("新建场次"), &dialog);
    auto *saveEventButton = new QPushButton(QStringLiteral("保存场次"), &dialog);
    auto *archiveEventButton = new QPushButton(QStringLiteral("归档场次"), &dialog);
    auto *newEventAthleteButton = new QPushButton(QStringLiteral("新增参赛"), &dialog);
    auto *saveEventAthleteButton = new QPushButton(QStringLiteral("保存参赛"), &dialog);
    auto *archiveEventAthleteButton = new QPushButton(QStringLiteral("归档参赛"), &dialog);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), &dialog);
    for (QPushButton *button : {newButton, saveButton, archiveButton, newEventButton, saveEventButton, archiveEventButton, newEventAthleteButton, saveEventAthleteButton, archiveEventAthleteButton, closeButton}) {
        button->setProperty("role", "secondaryButton");
    }
    buttonRow->addWidget(newButton);
    buttonRow->addWidget(saveButton);
    buttonRow->addWidget(archiveButton);
    buttonRow->addWidget(newEventButton);
    buttonRow->addWidget(saveEventButton);
    buttonRow->addWidget(archiveEventButton);
    buttonRow->addWidget(newEventAthleteButton);
    buttonRow->addWidget(saveEventAthleteButton);
    buttonRow->addWidget(archiveEventAthleteButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    QVector<Competition> competitions;
    QVector<CompetitionEvent> events;
    QVector<EventAthlete> eventAthletes;
    Competition current;
    CompetitionEvent currentEvent;
    EventAthlete currentEventAthlete;
    bool changed = false;

    auto fillForm = [&]() {
        nameEdit->setText(current.name);
        locationEdit->setText(current.location);
        dateEdit->setDate(current.competitionDate.isValid() ? current.competitionDate : QDate::currentDate());
        typeEdit->setText(current.competitionType);
        notesEdit->setPlainText(current.notes);
        archiveButton->setEnabled(!current.id.isEmpty());
    };

    auto fillEventForm = [&]() {
        raceEdit->setText(currentEvent.raceName);
        eventEdit->setText(currentEvent.eventName);
        heatEdit->setText(currentEvent.heatName);
        groupEdit->setText(currentEvent.groupName);
        scheduledEdit->setDateTime(currentEvent.scheduledAt.isValid() ? currentEvent.scheduledAt.toLocalTime() : QDateTime::currentDateTime());
        eventNotesEdit->setPlainText(currentEvent.notes);
        archiveEventButton->setEnabled(!currentEvent.id.isEmpty());
        saveEventButton->setEnabled(!current.id.isEmpty());
        newEventAthleteButton->setEnabled(!currentEvent.id.isEmpty());
        saveEventAthleteButton->setEnabled(!currentEvent.id.isEmpty());
    };

    auto fillEventAthleteForm = [&]() {
        const int athleteIndex = athleteComboBox->findData(currentEventAthlete.athleteId);
        athleteComboBox->setCurrentIndex(athleteIndex >= 0 ? athleteIndex : 0);
        bibEdit->setText(currentEventAthlete.bibNumber);
        laneEdit->setText(currentEventAthlete.laneNumber);
        sortSpinBox->setValue(std::max(0, currentEventAthlete.sortOrder));
        resultScoreSpinBox->setValue(currentEventAthlete.resultScore);
        resultRankSpinBox->setValue(currentEventAthlete.resultRank);
        eventAthleteNotesEdit->setPlainText(currentEventAthlete.notes);
        archiveEventAthleteButton->setEnabled(!currentEventAthlete.id.isEmpty());
    };

    auto reloadEventAthletes = [&]() {
        eventAthletes = currentEvent.id.isEmpty()
                            ? QVector<EventAthlete>()
                            : m_trainingRepository->eventAthletes(currentEvent.id);
        eventAthleteList->clear();
        for (const EventAthlete &eventAthlete : std::as_const(eventAthletes)) {
            eventAthleteList->addItem(QStringLiteral("%1 · 参赛号 %2 · 道次 %3")
                                          .arg(eventAthlete.athleteName.isEmpty() ? eventAthlete.athleteId : eventAthlete.athleteName,
                                               eventAthlete.bibNumber.isEmpty() ? QStringLiteral("未填") : eventAthlete.bibNumber,
                                               eventAthlete.laneNumber.isEmpty() ? QStringLiteral("未填") : eventAthlete.laneNumber));
        }
        if (!eventAthletes.isEmpty()) {
            eventAthleteList->setCurrentRow(0);
        } else {
            currentEventAthlete = {};
            currentEventAthlete.eventId = currentEvent.id;
            fillEventAthleteForm();
        }
    };

    auto reloadEvents = [&]() {
        events = current.id.isEmpty()
                     ? QVector<CompetitionEvent>()
                     : m_trainingRepository->competitionEvents(current.id);
        eventList->clear();
        for (const CompetitionEvent &event : std::as_const(events)) {
            eventList->addItem(QStringLiteral("%1 / %2 / %3 / %4")
                                   .arg(event.raceName.isEmpty() ? QStringLiteral("未填场次") : event.raceName,
                                        event.eventName.isEmpty() ? QStringLiteral("未填项目") : event.eventName,
                                        event.heatName.isEmpty() ? QStringLiteral("未填轮次") : event.heatName,
                                        event.groupName.isEmpty() ? QStringLiteral("未填分组") : event.groupName));
        }
        if (!events.isEmpty()) {
            eventList->setCurrentRow(0);
        } else {
            currentEvent = {};
            currentEvent.competitionId = current.id;
            fillEventForm();
            reloadEventAthletes();
        }
    };

    auto reload = [&]() {
        competitions = m_trainingRepository->competitions(false, searchEdit->text().trimmed());
        list->clear();
        for (const Competition &competition : std::as_const(competitions)) {
            const QString dateText = competition.competitionDate.isValid()
                                         ? competition.competitionDate.toString(QStringLiteral("yyyy-MM-dd"))
                                         : QStringLiteral("未填日期");
            list->addItem(QStringLiteral("%1 · %2 · %3")
                              .arg(competition.name,
                                   dateText,
                                   competition.location.trimmed().isEmpty() ? QStringLiteral("未填地点") : competition.location));
        }
        if (!competitions.isEmpty()) {
            list->setCurrentRow(0);
        } else {
            current = {};
            fillForm();
            reloadEvents();
        }
    };

    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        current = row >= 0 && row < competitions.size() ? competitions.at(row) : Competition();
        fillForm();
        reloadEvents();
    });
    QObject::connect(eventList, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        currentEvent = row >= 0 && row < events.size() ? events.at(row) : CompetitionEvent();
        if (currentEvent.competitionId.isEmpty()) {
            currentEvent.competitionId = current.id;
        }
        fillEventForm();
        reloadEventAthletes();
    });
    QObject::connect(eventAthleteList, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        currentEventAthlete = row >= 0 && row < eventAthletes.size() ? eventAthletes.at(row) : EventAthlete();
        if (currentEventAthlete.eventId.isEmpty()) {
            currentEventAthlete.eventId = currentEvent.id;
        }
        fillEventAthleteForm();
    });
    QObject::connect(newButton, &QPushButton::clicked, &dialog, [&]() {
        current = {};
        fillForm();
        reloadEvents();
        nameEdit->setFocus();
    });
    QObject::connect(newEventButton, &QPushButton::clicked, &dialog, [&]() {
        currentEvent = {};
        currentEvent.competitionId = current.id;
        fillEventForm();
        reloadEventAthletes();
        raceEdit->setFocus();
    });
    QObject::connect(newEventAthleteButton, &QPushButton::clicked, &dialog, [&]() {
        currentEventAthlete = {};
        currentEventAthlete.eventId = currentEvent.id;
        fillEventAthleteForm();
        athleteComboBox->setFocus();
    });
    QObject::connect(searchButton, &QPushButton::clicked, &dialog, reload);
    QObject::connect(searchEdit, &QLineEdit::returnPressed, &dialog, reload);
    QObject::connect(saveButton, &QPushButton::clicked, &dialog, [&]() {
        Competition competition = current;
        competition.name = nameEdit->text().trimmed();
        competition.location = locationEdit->text().trimmed();
        competition.competitionDate = dateEdit->date();
        competition.competitionType = typeEdit->text().trimmed();
        competition.notes = notesEdit->toPlainText().trimmed();
        competition.active = true;
        if (competition.name.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), QStringLiteral("比赛名称不能为空。"));
            return;
        }
        QString errorMessage;
        if (!m_trainingRepository->saveCompetition(&competition, &errorMessage)) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), errorMessage);
            return;
        }
        current = competition;
        changed = true;
        reload();
        for (int i = 0; i < competitions.size(); ++i) {
            if (competitions.at(i).id == current.id) {
                list->setCurrentRow(i);
                break;
            }
        }
    });
    QObject::connect(saveEventButton, &QPushButton::clicked, &dialog, [&]() {
        if (current.id.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), QStringLiteral("请先选择或保存比赛。"));
            return;
        }
        CompetitionEvent event = currentEvent;
        event.competitionId = current.id;
        event.raceName = raceEdit->text().trimmed();
        event.eventName = eventEdit->text().trimmed();
        event.heatName = heatEdit->text().trimmed();
        event.groupName = groupEdit->text().trimmed();
        event.scheduledAt = scheduledEdit->dateTime();
        event.notes = eventNotesEdit->toPlainText().trimmed();
        event.active = true;
        if (event.raceName.isEmpty() && event.eventName.isEmpty() && event.groupName.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), QStringLiteral("场次、项目或分组至少填写一项。"));
            return;
        }
        QString errorMessage;
        if (!m_trainingRepository->saveCompetitionEvent(&event, &errorMessage)) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), errorMessage);
            return;
        }
        currentEvent = event;
        changed = true;
        reloadEvents();
        for (int i = 0; i < events.size(); ++i) {
            if (events.at(i).id == currentEvent.id) {
                eventList->setCurrentRow(i);
                break;
            }
        }
    });
    QObject::connect(saveEventAthleteButton, &QPushButton::clicked, &dialog, [&]() {
        if (currentEvent.id.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), QStringLiteral("请先选择或保存场次。"));
            return;
        }
        EventAthlete eventAthlete = currentEventAthlete;
        eventAthlete.eventId = currentEvent.id;
        eventAthlete.athleteId = athleteComboBox->currentData().toString();
        eventAthlete.athleteName = athleteComboBox->currentText();
        eventAthlete.bibNumber = bibEdit->text().trimmed();
        eventAthlete.laneNumber = laneEdit->text().trimmed();
        eventAthlete.sortOrder = sortSpinBox->value();
        eventAthlete.resultScore = resultScoreSpinBox->value();
        eventAthlete.resultRank = resultRankSpinBox->value();
        eventAthlete.notes = eventAthleteNotesEdit->toPlainText().trimmed();
        eventAthlete.active = true;
        if (eventAthlete.athleteId.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), QStringLiteral("请选择参赛运动员。"));
            return;
        }
        QString errorMessage;
        if (!m_trainingRepository->saveEventAthlete(&eventAthlete, &errorMessage)) {
            QMessageBox::warning(&dialog, QStringLiteral("保存失败"), errorMessage);
            return;
        }
        currentEventAthlete = eventAthlete;
        changed = true;
        reloadEventAthletes();
        for (int i = 0; i < eventAthletes.size(); ++i) {
            if (eventAthletes.at(i).id == currentEventAthlete.id) {
                eventAthleteList->setCurrentRow(i);
                break;
            }
        }
    });
    QObject::connect(archiveButton, &QPushButton::clicked, &dialog, [&]() {
        if (current.id.isEmpty()) {
            return;
        }
        if (QMessageBox::question(&dialog, QStringLiteral("归档比赛"), QStringLiteral("归档后不会出现在新训练选择中，历史记录仍保留关联。确认归档？")) != QMessageBox::Yes) {
            return;
        }
        QString errorMessage;
        if (!m_trainingRepository->archiveCompetition(current.id, &errorMessage)) {
            QMessageBox::warning(&dialog, QStringLiteral("归档失败"), errorMessage);
            return;
        }
        changed = true;
        current = {};
        reload();
    });
    QObject::connect(archiveEventButton, &QPushButton::clicked, &dialog, [&]() {
        if (currentEvent.id.isEmpty()) {
            return;
        }
        if (QMessageBox::question(&dialog, QStringLiteral("归档场次"), QStringLiteral("归档后不会出现在新训练选择中，历史记录仍保留关联。确认归档？")) != QMessageBox::Yes) {
            return;
        }
        QString errorMessage;
        if (!m_trainingRepository->archiveCompetitionEvent(currentEvent.id, &errorMessage)) {
            QMessageBox::warning(&dialog, QStringLiteral("归档失败"), errorMessage);
            return;
        }
        changed = true;
        currentEvent = {};
        reloadEvents();
    });
    QObject::connect(archiveEventAthleteButton, &QPushButton::clicked, &dialog, [&]() {
        if (currentEventAthlete.id.isEmpty()) {
            return;
        }
        if (QMessageBox::question(&dialog, QStringLiteral("归档参赛关系"), QStringLiteral("归档后不会自动关联新训练，历史记录仍保留关联。确认归档？")) != QMessageBox::Yes) {
            return;
        }
        QString errorMessage;
        if (!m_trainingRepository->archiveEventAthlete(currentEventAthlete.id, &errorMessage)) {
            QMessageBox::warning(&dialog, QStringLiteral("归档失败"), errorMessage);
            return;
        }
        changed = true;
        currentEventAthlete = {};
        reloadEventAthletes();
    });
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    reload();
    dialog.exec();
    if (!changed) {
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

void MainWindow::openTrackBindingDialog()
{
    const QVector<TrainingSessionParticipant> participants = currentSessionParticipants();
    if (participants.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("轨迹绑定"), QStringLiteral("请先选择主运动员。"));
        return;
    }
    if (m_lastPoseFrame.instances.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("轨迹绑定"), QStringLiteral("当前还没有可绑定的姿态实例。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("轨迹绑定"));
    dialog.resize(760, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("机位"),
                                      QStringLiteral("轨迹ID"),
                                      QStringLiteral("置信度"),
                                      QStringLiteral("当前身份"),
                                      QStringLiteral("绑定运动员"),
                                      QStringLiteral("来源")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);

    auto makeItem = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };
    const QVector<PoseIdentityBinding> existingBindings = m_poseIdentityResolver ? m_poseIdentityResolver->bindings() : QVector<PoseIdentityBinding>();
    const int rowCount = std::min(4, static_cast<int>(m_lastPoseFrame.instances.size()));
    table->setRowCount(rowCount);
    QVector<QComboBox *> bindingCombos;
    for (int row = 0; row < rowCount; ++row) {
        const PoseInstance &instance = m_lastPoseFrame.instances.at(row);
        table->setItem(row, 0, makeItem(QString::number(m_lastPoseFrame.cameraId)));
        table->setItem(row, 1, makeItem(QString::number(instance.trackId)));
        table->setItem(row, 2, makeItem(QString::number(instance.confidence, 'f', 2)));
        QString currentName = QStringLiteral("未标识");
        for (const TrainingSessionParticipant &participant : participants) {
            if (participant.athleteId == instance.athleteId) {
                currentName = participant.athleteName;
                break;
            }
        }
        table->setItem(row, 3, makeItem(currentName));
        auto *combo = new QComboBox(table);
        combo->addItem(QStringLiteral("未绑定"), QString());
        for (const TrainingSessionParticipant &participant : participants) {
            combo->addItem(participant.athleteName.isEmpty() ? participant.athleteId : participant.athleteName,
                           participant.athleteId);
        }
        QString boundAthleteId = instance.athleteId;
        for (const PoseIdentityBinding &binding : existingBindings) {
            if (binding.cameraId == m_lastPoseFrame.cameraId && binding.trackId == instance.trackId) {
                boundAthleteId = binding.athleteId;
                break;
            }
        }
        const int index = combo->findData(boundAthleteId);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        table->setCellWidget(row, 4, combo);
        table->setItem(row, 5, makeItem(instance.identitySource.isEmpty() ? QStringLiteral("unknown") : instance.identitySource));
        bindingCombos.append(combo);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<PoseIdentityBinding> bindings;
    for (int row = 0; row < rowCount; ++row) {
        const QString athleteId = bindingCombos.at(row)->currentData().toString();
        if (athleteId.isEmpty()) {
            continue;
        }
        const PoseInstance &instance = m_lastPoseFrame.instances.at(row);
        PoseIdentityBinding binding;
        binding.cameraId = m_lastPoseFrame.cameraId;
        binding.trackId = instance.trackId;
        binding.athleteId = athleteId;
        for (const TrainingSessionParticipant &participant : participants) {
            if (participant.athleteId == athleteId) {
                binding.participantId = participant.id;
                binding.label = participant.athleteName;
                break;
            }
        }
        bindings.append(binding);
    }
    if (m_poseIdentityResolver) {
        m_poseIdentityResolver->setBindings(bindings);
    }
    ui->saveTipLabel->setText(QStringLiteral("轨迹绑定已更新。"));
    ui->saveTipLabel->show();
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
    m_currentRepetitions.clear();
    m_previousKneeBend = 0.0;
    m_actionArmed = false;
    m_lastActionMsec = 0;
    m_recordingStartedAtMsec = QDateTime::currentMSecsSinceEpoch();
    m_recordingStartedAt = QDateTime::currentDateTime();
    if (m_actionRepetitionTracker) {
        m_actionRepetitionTracker->reset(selectedActionStandard());
    }
}

void MainWindow::recordCompletedRepetition(const ActionRepetition &repetition)
{
    m_currentRepetitions.append(repetition);
    m_actionCount = m_currentRepetitions.size();
    m_validActionCount = 0;
    m_bestActionScore = 0;
    m_actionScoreTotal = 0;
    for (const ActionRepetition &item : std::as_const(m_currentRepetitions)) {
        if (item.valid) {
            ++m_validActionCount;
        }
        m_bestActionScore = std::max(m_bestActionScore, item.score);
        m_actionScoreTotal += item.score;
    }
    refreshStats();
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

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(this,
                             QStringLiteral("导入失败"),
                             QStringLiteral("找不到所选视频文件：%1").arg(QDir::toNativeSeparators(filePath)));
        return;
    }

    const OfflineVideoProbeResult probe = OfflineVideoProbe::probe(fileInfo.absoluteFilePath());
    if (!probe.success) {
        QMessageBox::warning(this,
                             QStringLiteral("导入失败"),
                             QStringLiteral("视频文件校验失败：%1\n\n文件：%2")
                                 .arg(probe.message, QDir::toNativeSeparators(fileInfo.absoluteFilePath())));
        return;
    }

    settings.setValue(QStringLiteral("offlineVideo/lastDir"), fileInfo.absolutePath());
    m_offlineVideoPath = fileInfo.absoluteFilePath();
    m_offlineVideoName = offlineVideoDisplayName(m_offlineVideoPath);
    m_offlineVideoProbe = probe;

    for (auto *videoWidget : m_cameraButtons) {
        if (videoWidget && videoWidget->isPlaying()) {
            videoWidget->stopPlayback();
        }
    }
    showOfflineVideoInMainView(true);
    const QString summary = offlineProbeSummary(m_offlineVideoProbe);
    ui->saveTipLabel->setText(QStringLiteral("已导入离线视频：%1%2。点击“开始采集”后将基于该视频记录训练复盘。")
                                  .arg(QDir::toNativeSeparators(m_offlineVideoPath),
                                       summary.isEmpty() ? QString() : QStringLiteral("（%1）").arg(summary)));
    ui->saveTipLabel->show();
}

void MainWindow::showOfflineVideoInMainView(bool autoPlay)
{
    if (m_offlineVideoPath.trimmed().isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(m_offlineVideoPath);
    m_offlineVideoName = offlineVideoDisplayName(m_offlineVideoPath);
    m_selectedCamera = 0;
    clearRealtimePose();

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(QStringLiteral("离线视频\n文件不存在"));
        ui->saveTipLabel->setText(QStringLiteral("离线视频文件不存在：%1").arg(QDir::toNativeSeparators(m_offlineVideoPath)));
        ui->saveTipLabel->show();
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setPaused(true);
            m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
        }
    } else if (!autoPlay) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(m_offlineVideoName);
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setPaused(true);
            m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
        }
    } else {
        ui->mainImageLabel->setPlaceholderText(m_offlineVideoName);
        ui->mainImageLabel->playFile(m_offlineVideoPath);
        syncAnalysisStreams();
    }

    ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(m_offlineVideoName));
    refreshCameraButtons();
}

// 将指定摄像头主码流显示到主视图；小窗预览仍保持子码流。
void MainWindow::showCameraInMainView(int cameraIndex, bool autoPlay)
{
    if (cameraIndex < 0 || cameraIndex >= m_cameraButtons.size()) {
        return;
    }

    m_offlineVideoPath.clear();
    m_offlineVideoName.clear();
    m_offlineVideoProbe = OfflineVideoProbeResult();
    auto *cameraWidget = m_cameraButtons.at(cameraIndex);
    m_selectedCamera = cameraIndex + 1;
    const QString source = cameraWidget->mainUrl();
    clearRealtimePose();
    if (source.trimmed().isEmpty()) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(QStringLiteral("主视频\n未配置"));
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setPaused(true);
            m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
        }
    } else if (!autoPlay) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(cameraWidget->channelName());
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setPaused(true);
            m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
        }
    } else {
        ui->mainImageLabel->setPlaceholderText(cameraWidget->channelName());
        ui->mainImageLabel->playMainUrlWithFallback(source, cameraWidget->previewUrl());
        syncAnalysisStreams();
    }

    ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(cameraWidget->channelName()));
    refreshCameraButtons();
}

// 切换主页面堆栈；pageIndex 对应实时采集、历史分析和纠正建议等页面。
void MainWindow::switchPage(int pageIndex)
{
    if (!ui->pages || pageIndex < 0 || pageIndex >= ui->pages->count()) {
        return;
    }

    ui->pages->setCurrentIndex(pageIndex);
    m_activePage = pageIndex;
    refreshNavButtons();

    if (pageIndex == kHistoryPage) {
        refreshHistory();
    } else if (pageIndex == kSuggestionPage) {
        refreshSuggestions();
    }
}

// 显示或隐藏左侧侧栏：折叠布局中的控件和间距，避免隐藏后仍占用宽度。
void MainWindow::toggleSidebar()
{
    m_sidebarVisible = !m_sidebarVisible;

    if (!m_sidebarMetricsCaptured) {
        m_sidebarLayoutMargins = ui->sidebarLayout->contentsMargins();
        m_sidebarLayoutSpacing = ui->sidebarLayout->spacing();
        m_sidebarNormalMinimumWidth = ui->sidebar->minimumWidth();
        m_sidebarNormalMaximumWidth = ui->sidebar->maximumWidth();
        m_sidebarMetricsCaptured = true;
    }

    setLayoutItemsVisible(ui->sidebarLayout, m_sidebarVisible);
    ui->sidebarLayout->setContentsMargins(m_sidebarVisible ? m_sidebarLayoutMargins : QMargins(0, 0, 0, 0));
    ui->sidebarLayout->setSpacing(m_sidebarVisible ? m_sidebarLayoutSpacing : 0);
    ui->sidebar->setMinimumWidth(m_sidebarVisible ? m_sidebarNormalMinimumWidth : 0);
    ui->sidebar->setMaximumWidth(m_sidebarVisible ? m_sidebarNormalMaximumWidth : 0);
    ui->sidebar->setVisible(m_sidebarVisible);

    if (auto *sideBarAfter = findChild<QWidget *>(QStringLiteral("sideBarAfter"))) {
        sideBarAfter->setVisible(m_sidebarVisible);
    }

    refreshSidebarButton();

    ui->sidebarLayout->invalidate();
    if (ui->sidebarLayout->parentWidget() && ui->sidebarLayout->parentWidget()->layout()) {
        ui->sidebarLayout->parentWidget()->layout()->invalidate();
        ui->sidebarLayout->parentWidget()->layout()->activate();
    }
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

// 兼容旧的二态调用：true 表示扩展到上方 12 路视频区域，false 表示恢复正常区域。
void MainWindow::setTrajectoryExpanded(bool expanded)
{
    setTrajectoryMode(expanded ? TrajectoryExpanded : TrajectoryNormal);
}

// 按“正常区域 -> 扩展区域 -> 最小化 -> 正常区域”的顺序循环切换三维轨迹显示状态。
void MainWindow::cycleTrajectoryMode()
{
    const int currentMode = m_trajectoryMode < 0 ? TrajectoryNormal : m_trajectoryMode;
    setTrajectoryMode((currentMode + 1) % 3);
}

// 设置三维轨迹的显示模式：正常状态严格恢复启动时布局参数，扩展/最小化只做临时调整。
void MainWindow::setTrajectoryMode(int mode)
{
    mode = ((mode % 3) + 3) % 3;
    if (m_trajectoryMode == mode) {
        refreshTrajectoryModeButton();
        return;
    }

    m_trajectoryMode = mode;
    QWidget *trajectoryView = m_trajectoryWidget
                                  ? static_cast<QWidget *>(m_trajectoryWidget)
                                  : static_cast<QWidget *>(ui->trajectoryViewFrame);

    const bool expanded = (mode == TrajectoryExpanded);
    const bool minimized = (mode == TrajectoryMinimized);

    ui->middleLayout->setSpacing(expanded ? 0 : m_middleLayoutNormalSpacing);
    ui->middleLayout->setStretch(0, expanded ? 0 : m_middleLayoutNormalStretch0);
    ui->middleLayout->setStretch(1, expanded ? 1 : m_middleLayoutNormalStretch1);
    ui->cameraGridLayout->setSpacing(expanded ? 0 : m_cameraGridNormalSpacing);
    for (auto *cameraWidget : m_cameraButtons) {
        cameraWidget->setVisible(!expanded);
        cameraWidget->updateGeometry();
    }
    trajectoryView->setVisible(!minimized);
    ui->legendFrame->setVisible(expanded && !minimized);
    ui->expandTrajectoryButton->setVisible(true);
    ui->collapseTrajectoryButton->setVisible(false);

    if (expanded) {
        ui->trajectoryCard->setMinimumSize(m_trajectoryCardNormalMinSize);
        ui->trajectoryCard->setMaximumSize(m_trajectoryCardNormalMaxSize);
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        trajectoryView->setMinimumSize(m_trajectoryViewNormalMinSize);
        trajectoryView->setMaximumSize(m_trajectoryViewNormalMaxSize);
        trajectoryView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else if (minimized) {
        const int minimizedHeight = std::max(42, ui->trajectoryTitleLabel->sizeHint().height() + 30);
        ui->trajectoryCard->setMinimumSize(QSize(m_trajectoryCardNormalMinSize.width(), minimizedHeight));
        ui->trajectoryCard->setMaximumSize(QSize(QWIDGETSIZE_MAX, minimizedHeight));
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        trajectoryView->setMinimumSize(QSize(0, 0));
        trajectoryView->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        trajectoryView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    } else {
        ui->trajectoryCard->setMinimumSize(m_trajectoryCardNormalMinSize);
        ui->trajectoryCard->setMaximumSize(m_trajectoryCardNormalMaxSize);
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        trajectoryView->setMinimumSize(m_trajectoryViewNormalMinSize);
        trajectoryView->setMaximumSize(m_trajectoryViewNormalMaxSize);
        trajectoryView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    refreshTrajectoryModeButton();
    ui->trajectoryCard->updateGeometry();
    trajectoryView->updateGeometry();
    ui->middleLayout->invalidate();
    if (ui->capturePage->layout()) {
        ui->capturePage->layout()->invalidate();
        ui->capturePage->layout()->activate();
    }
}

// 刷新三维轨迹右侧三角形符号和提示，符号表示下一次点击将进入的状态。
void MainWindow::refreshTrajectoryModeButton()
{
    QString text;
    QString tip;
    switch (m_trajectoryMode) {
    case TrajectoryExpanded:
        text = QStringLiteral("▼");
        tip = QStringLiteral("最小化三维轨迹，只保留标题栏");
        break;
    case TrajectoryMinimized:
        text = QStringLiteral("▲");
        tip = QStringLiteral("恢复三维轨迹到当前区域");
        break;
    case TrajectoryNormal:
    default:
        text = QStringLiteral("▲");
        tip = QStringLiteral("展开三维轨迹，占用上方12路视频区域");
        break;
    }

    ui->expandTrajectoryButton->setText(text);
    ui->expandTrajectoryButton->setToolTip(tip);
    ui->expandTrajectoryButton->setStatusTip(tip);
    ui->collapseTrajectoryButton->setText(text);
    ui->collapseTrajectoryButton->setToolTip(tip);
    ui->collapseTrajectoryButton->setStatusTip(tip);
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
        const bool hasTrajectoryCamera = std::any_of(m_cameraSlotSettings.cbegin(),
                                                     m_cameraSlotSettings.cend(),
                                                     [](const CameraSlotSettings &slot) {
                                                         return slot.trajectoryEnabled && !slot.ip.trimmed().isEmpty();
                                                     });
        if (!hasTrajectoryCamera) {
            ui->saveTipLabel->setText(QStringLiteral("请先在系统设置中至少配置一路参与轨迹的相机 IP。"));
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
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(false);
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
}

// 暂停采集：暂停主视图和全部摄像头预览的播放器状态。
void MainWindow::pauseCapture()
{
    qDebug() << "[MainWindow] pauseCapture clicked";
    m_isPaused = true;
    m_timer.stop();
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(true);
        m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
    }
    clearRealtimePose();
    ui->mainImageLabel->pausePlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->pausePlayback();
    }
}

// 停止采集：停止主视图和全部摄像头预览，并恢复未播放占位状态。
void MainWindow::stopCapture()
{
    qDebug() << "[MainWindow] stopCapture clicked";
    m_isRecording = false;
    m_isPaused = false;
    m_timer.stop();
    m_previousKneeBend = 0.0;
    m_actionArmed = false;
    m_lastActionMsec = 0;
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(true);
        m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
    }
    clearRealtimePose();
    ui->mainImageLabel->stopPlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->stopPlayback();
    }
    refreshCameraButtons();
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

    table->setColumnCount(13);
    table->setHorizontalHeaderLabels({QStringLiteral("时间"),
                                      QStringLiteral("运动员"),
                                      QStringLiteral("身份"),
                                      QStringLiteral("轨迹"),
                                      QStringLiteral("机位"),
                                      QStringLiteral("动作"),
                                      QStringLiteral("比赛/场次"),
                                      QStringLiteral("片段"),
                                      QStringLiteral("有效分"),
                                      QStringLiteral("有效性"),
                                      QStringLiteral("错误项"),
                                      QStringLiteral("反馈"),
                                      QStringLiteral("视频")});
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
            const QStringList values = {item.time,
                                        item.athleteName,
                                        identityStatusLabel(item.identityStatus),
                                        item.trackId >= 0 ? QString::number(item.trackId) : QStringLiteral("-"),
                                        item.cameraId >= 0 ? QString::number(item.cameraId) : QStringLiteral("-"),
                                        QStringLiteral("%1/%2").arg(item.actionCategory, item.actionName),
                                        competition,
                                        clip,
                                        QString::number(item.effectiveScoreValue),
                                        boolText(item.effectiveValidValue),
                                        issueSummary(item.effectiveErrorCodesValue),
                                        item.effectiveFeedbackValue,
                                        displayMediaSource(item.videoSource)};
            for (int col = 0; col < values.size(); ++col) {
                auto *cell = new QTableWidgetItem(values.at(col));
                if (col == 8) {
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
        openSessionVideo(record, item.videoClipStartMs, item.videoClipEndMs);
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

    QString planId;
    QString taskId;
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

    if (!m_trainingRepository->ensureDailyTask(athleteId,
                                               coachId,
                                               standard.id,
                                               standard.version,
                                               targetReps,
                                               targetScore,
                                               setCount,
                                               restSeconds,
                                               site,
                                               trainingPhase,
                                               goal,
                                               &planId,
                                               &taskId,
                                               &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    TrainingSession session;
    session.athleteId = athleteId;
    session.coachId = coachId;
    session.competitionId = competitionId;
    session.competitionEventId = competitionEventId;
    session.eventAthleteId = eventAthleteId;
    session.planId = planId;
    session.taskId = taskId;
    session.actionStandardId = standard.id;
    session.standardVersion = standard.version;
    session.startedAt = m_recordingStartedAt.isValid() ? m_recordingStartedAt : QDateTime::currentDateTime().addSecs(-m_durationSec);
    session.savedAt = QDateTime::currentDateTime();
    session.durationSec = m_durationSec;
    session.totalReps = m_actionCount;
    session.validReps = m_validActionCount;
    session.averageScore = m_actionCount > 0
                               ? std::clamp((m_actionScoreTotal + m_actionCount / 2) / std::max(1, m_actionCount), 0, 100)
                               : m_realtimeScore;
    session.bestScore = m_bestActionScore > 0 ? m_bestActionScore : m_realtimeScore;
    const bool offlineSession = !m_offlineVideoPath.trimmed().isEmpty() && m_selectedCamera == 0;
    session.camera = offlineSession ? 0 : m_selectedCamera;
    session.modelPrecision = ui->precisionComboBox
                                 ? ui->precisionComboBox->currentData().toString()
                                 : QStringLiteral("balanced");
    if (session.modelPrecision.isEmpty()) {
        session.modelPrecision = QStringLiteral("balanced");
    }
    session.fps = m_sharedCameraSettings.mainFps > 0 ? m_sharedCameraSettings.mainFps : kDefaultMainStreamFps;
    if (session.fps <= 0) {
        session.fps = kDefaultMainStreamFps;
    }
    session.detectionScore = m_detectionScore;
    session.symmetryScore = m_symmetryScore;
    session.balanceScore = m_balanceScore;
    session.stabilityScore = m_stabilityScore;
    session.depthScore = m_depthScore;
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
    } else if (m_selectedCamera > 0 && m_selectedCamera <= m_cameraButtons.size()) {
        const VideoOpenGLWidget *cameraWidget = m_cameraButtons.at(m_selectedCamera - 1);
        session.videoSource = cameraWidget->mainUrl().trimmed();
        session.videoFallbackSource = cameraWidget->previewUrl().trimmed();
        session.videoCameraName = cameraWidget->channelName();
    }
    if (!competitionEventId.isEmpty()) {
        session.sourceType = QStringLiteral("competition");
        session.sourceRef = competitionEventId;
    } else if (!competitionId.isEmpty()) {
        session.sourceType = QStringLiteral("competition");
        session.sourceRef = competitionId;
    } else if (offlineSession && !session.videoSource.trimmed().isEmpty()) {
        session.sourceType = QStringLiteral("offline_import");
        session.sourceRef = session.videoSource;
    } else {
        session.sourceType = QStringLiteral("training");
        session.sourceRef = !taskId.isEmpty() ? taskId : planId;
    }
    session.feedback = m_feedbackText.trimmed().isEmpty() ? QStringLiteral("等待姿态") : m_feedbackText.trimmed();
    session.notes = trainingNotes;
    session.participants = currentSessionParticipants();

    QVector<ActionRepetition> repetitions = m_currentRepetitions;
    for (ActionRepetition &repetition : repetitions) {
        repetition.sessionId = session.id;
        repetition.actionStandardId = standard.id;
        repetition.standardVersion = standard.version;
        repetition.videoClipStartMs = std::max(0, repetition.startedMs - 1500);
        repetition.videoClipEndMs = std::max(repetition.endedMs + 1500, repetition.videoClipStartMs);
        if (repetition.identityStatus.trimmed().isEmpty()) {
            repetition.identityStatus = QStringLiteral("unknown");
        }
    }
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

void MainWindow::updateActionCounter(const PoseFrameResult &poseFrame)
{
    if (!m_isRecording || m_isPaused || poseFrame.instances.isEmpty()) {
        return;
    }

    const PoseInstance *person = nullptr;
    for (const PoseInstance &instance : poseFrame.instances) {
        if (!person || instance.confidence > person->confidence) {
            person = &instance;
        }
    }
    if (!person || person->keypoints.size() <= 16) {
        return;
    }

    auto keypoint = [person](int index) -> const PoseKeypoint * {
        if (index < 0 || index >= person->keypoints.size()) {
            return nullptr;
        }
        const PoseKeypoint &kp = person->keypoints.at(index);
        return kp.valid ? &kp : nullptr;
    };

    const PoseKeypoint *leftHip = keypoint(11);
    const PoseKeypoint *rightHip = keypoint(12);
    const PoseKeypoint *leftKnee = keypoint(13);
    const PoseKeypoint *rightKnee = keypoint(14);
    if (!leftHip || !rightHip || !leftKnee || !rightKnee) {
        return;
    }

    const qreal hipY = (leftHip->imagePoint.y() + rightHip->imagePoint.y()) * 0.5;
    const qreal kneeY = (leftKnee->imagePoint.y() + rightKnee->imagePoint.y()) * 0.5;
    const qreal bodyScale = std::max<qreal>(1.0, person->box.height());
    const qreal kneeBend = (kneeY - hipY) / bodyScale;

    if (kneeBend > 0.28) {
        m_actionArmed = true;
    }
    if (m_actionArmed && kneeBend < 0.18) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastActionMsec > 900) {
            ++m_actionCount;
            m_lastActionMsec = now;
            refreshStats();
        }
        m_actionArmed = false;
    }
    m_previousKneeBend = kneeBend;
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
        cameraWidget->setProperty("selected", i + 1 == m_selectedCamera);
        repolish(cameraWidget);
    }
}

// 刷新动作计数、训练时长、实时得分等统计卡片。
void MainWindow::refreshStats()
{
    const QVector<int> metricScores = {
        m_detectionScore,
        m_symmetryScore,
        m_balanceScore,
        m_stabilityScore,
        m_depthScore
    };

    const int targetReps = m_targetRepsSpinBox ? m_targetRepsSpinBox->value() : 0;
    ui->actionValueLabel->setText(targetReps > 0
                                      ? QStringLiteral("%1/%2").arg(m_validActionCount).arg(targetReps)
                                      : QString::number(m_actionCount));
    ui->durationValueLabel->setText(formatTime(m_durationSec));
    ui->scoreValueLabel->setText(QStringLiteral("%1/100 · %2").arg(m_realtimeScore).arg(m_feedbackText));
    ui->scoreProgressBar->setValue(std::clamp(m_realtimeScore, 0, 100));
    if (m_trainingTargetLabel) {
        const int targetScore = m_targetScoreSpinBox ? m_targetScoreSpinBox->value() : 0;
        const int averageActionScore = m_actionCount > 0
                                           ? (m_actionScoreTotal + m_actionCount / 2) / std::max(1, m_actionCount)
                                           : 0;
        m_trainingTargetLabel->setText(QStringLiteral("目标完成度：有效 %1/%2 · 总动作 %3 · 均分 %4/%5 · 最好 %6")
                                           .arg(m_validActionCount)
                                           .arg(targetReps)
                                           .arg(m_actionCount)
                                           .arg(averageActionScore)
                                           .arg(targetScore)
                                           .arg(m_bestActionScore));
    }

    const int metricCount = std::min({metricScores.size(), m_metricBars.size(), m_metricValueLabels.size()});
    for (int i = 0; i < metricCount; ++i) {
        const int value = std::clamp(metricScores.at(i), 0, 100);
        m_metricBars.at(i)->setValue(value);
        m_metricValueLabels.at(i)->setText(QString::number(value));
    }
}

// 重新生成训练历史列表和概要统计卡片。
void MainWindow::refreshHistory()
{
    if (!ui->historyListLayout) {
        return;
    }

    int totalActions = 0;
    int totalScore = 0;
    int bestScore = 0;
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        totalActions += record.validReps;
        totalScore += record.score;
        bestScore = std::max(bestScore, record.score);
    }

    const int sessionCount = m_records.size();
    const int averageScore = sessionCount > 0 ? (totalScore + sessionCount / 2) / sessionCount : 0;
    if (m_summaryValues.size() >= 4) {
        m_summaryValues.at(0)->setText(QString::number(sessionCount));
        m_summaryValues.at(1)->setText(QString::number(totalActions));
        m_summaryValues.at(2)->setText(QString::number(averageScore));
        m_summaryValues.at(3)->setText(QString::number(bestScore));
    }

    clearLayout(ui->historyListLayout);

    if (m_records.isEmpty()) {
        auto *emptyLabel = new QLabel(m_historyTotalCount == 0
                                          ? QStringLiteral("暂无匹配的训练历史记录。\n请调整筛选条件，或开始一次训练并点击“保存记录”。")
                                          : QStringLiteral("当前页暂无训练历史记录。\n请返回上一页或调整筛选条件。"),
                                      ui->historyPage);
        emptyLabel->setProperty("role", "emptyBox");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        ui->historyListLayout->addWidget(emptyLabel);
        return;
    }

    const int visibleCount = m_records.size();
    for (int i = 0; i < visibleCount; ++i) {
        const SessionHistoryItem &record = m_records.at(i);
        const QVector<ActionRepetition> repetitions = m_trainingRepository && m_trainingRepository->isOpen()
                                                          ? m_trainingRepository->reviewedRepetitionsForSession(record.id)
                                                          : QVector<ActionRepetition>();

        auto *card = new QFrame(ui->historyPage);
        setRole(card, "historyItem");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 14, 14, 14);
        cardLayout->setSpacing(10);

        auto *headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);

        auto *timeLabel = new QLabel(QStringLiteral("%1 · %2 · %3")
                                         .arg(record.time, record.athleteName, record.actionName),
                                     card);
        timeLabel->setWordWrap(true);
        timeLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto *scoreTag = new QLabel(QStringLiteral("%1 分").arg(record.score), card);
        scoreTag->setProperty("role", "scoreTag");
        scoreTag->setAlignment(Qt::AlignCenter);
        scoreTag->setMinimumWidth(kScoreTagWidth);
        scoreTag->setMaximumWidth(kScoreTagWidth);
        scoreTag->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto *headerActions = new QWidget(card);
        auto *headerActionsLayout = new QHBoxLayout(headerActions);
        headerActionsLayout->setContentsMargins(0, 0, 0, 0);
        headerActionsLayout->setSpacing(6);
        headerActions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        const bool hasNvrPlayback = !buildNvrPlaybackUrl(m_sharedCameraSettings,
                                                         m_cameraSlotSettings,
                                                         record).url.trimmed().isEmpty();

        auto *playButton = new QPushButton(QStringLiteral("回看视频"), card);
        playButton->setProperty("role", "secondaryButton");
        configureStableButton(playButton, kHistoryActionButtonWidth, 32, QSize(0, 0));
        playButton->setEnabled(hasNvrPlayback
                               || !record.videoSource.trimmed().isEmpty()
                               || !record.videoFallbackSource.trimmed().isEmpty());
        connect(playButton, &QPushButton::clicked, this, [this, record]() {
            openSessionVideo(record);
        });

        auto *reviewButton = new QPushButton(QStringLiteral("复盘校准"), card);
        reviewButton->setProperty("role", "secondaryButton");
        configureStableButton(reviewButton, kHistoryActionButtonWidth, 32, QSize(0, 0));
        reviewButton->setEnabled(m_trainingRepository && m_trainingRepository->isOpen());
        connect(reviewButton, &QPushButton::clicked, this, [this, record]() {
            openTrainingReview(record);
        });

        auto *commentButton = new QPushButton(QStringLiteral("教练批注"), card);
        commentButton->setProperty("role", "secondaryButton");
        configureStableButton(commentButton, kHistoryActionButtonWidth, 32, QSize(0, 0));
        connect(commentButton, &QPushButton::clicked, this, [this, record]() {
            editCoachComment(record.id);
        });

        auto *exportButton = new QPushButton(QStringLiteral("导出报告"), card);
        exportButton->setProperty("role", "secondaryButton");
        configureStableButton(exportButton, kHistoryActionButtonWidth, 32, QSize(0, 0));
        connect(exportButton, &QPushButton::clicked, this, [this, record]() {
            exportTrainingReport(record.id);
        });

        headerActionsLayout->addWidget(playButton);
        headerActionsLayout->addWidget(reviewButton);
        headerActionsLayout->addWidget(commentButton);
        headerActionsLayout->addWidget(exportButton);
        if (ui->historyPage && ui->historyPage->width() < 900) {
            headerLayout->addWidget(timeLabel, 1);
            headerLayout->addWidget(scoreTag, 0, Qt::AlignRight | Qt::AlignTop);
            cardLayout->addLayout(headerLayout);
            cardLayout->addWidget(headerActions, 0, Qt::AlignRight);
        } else {
            headerLayout->addWidget(timeLabel, 1);
            headerLayout->addWidget(headerActions, 0, Qt::AlignRight | Qt::AlignTop);
            headerLayout->addWidget(scoreTag, 0, Qt::AlignRight | Qt::AlignTop);
            cardLayout->addLayout(headerLayout);
        }

        const QString sourceLabel = !record.videoCameraName.trimmed().isEmpty()
                                        ? record.videoCameraName.trimmed()
                                        : (record.camera > 0
                                               ? QStringLiteral("CAM %1").arg(pad(record.camera))
                                               : QStringLiteral("离线视频"));
        auto *metaLabel = new QLabel(
            QStringLiteral("时长 %1   有效/总动作 %2/%3   目标 %4 次/%5 分   最佳 %6 分   来源 %7   精度 %8   分析 %9 FPS")
                .arg(formatTime(record.duration))
                .arg(record.validReps)
                .arg(record.totalReps)
                .arg(record.targetReps)
                .arg(record.targetScore)
                .arg(record.bestScore)
                .arg(sourceLabel)
                .arg(precisionLabel(record.modelPrecision))
                .arg(record.fps),
            card);
        metaLabel->setProperty("role", "muted");
        metaLabel->setWordWrap(true);
        cardLayout->addWidget(metaLabel);

        auto *feedbackLabel = new QLabel(QStringLiteral("标准 v%1 · %2 · %3\n场地：%4   阶段：%5   目标：%6\n备注：%7\n视频：%8\n反馈：%9")
                                             .arg(record.standardVersion)
                                             .arg(record.actionCategory)
                                             .arg(record.coachName.isEmpty() ? QStringLiteral("未指定教练") : record.coachName)
                                             .arg(record.site.isEmpty() ? QStringLiteral("未填写") : record.site)
                                             .arg(record.trainingPhase.isEmpty() ? QStringLiteral("未填写") : record.trainingPhase)
                                             .arg(record.goal.isEmpty() ? QStringLiteral("未填写") : record.goal)
                                             .arg(record.notes.isEmpty() ? QStringLiteral("未填写") : record.notes)
                                             .arg(hasNvrPlayback
                                                      ? QStringLiteral("NVR 回放已配置")
                                                      : displayMediaSource(record.videoSource))
                                             .arg(record.feedback),
                                         card);
        feedbackLabel->setWordWrap(true);
        cardLayout->addWidget(feedbackLabel);

        const QString competitionDate = record.competitionDate.isValid()
                                            ? record.competitionDate.toString(QStringLiteral("yyyy-MM-dd"))
                                            : QStringLiteral("未填日期");
        auto *competitionLabel = new QLabel(QStringLiteral("比赛：%1   地点：%2   日期：%3   类型：%4")
                                                .arg(record.competitionName.trimmed().isEmpty() ? QStringLiteral("未关联比赛") : record.competitionName.trimmed(),
                                                     record.competitionLocation.trimmed().isEmpty() ? QStringLiteral("未填写") : record.competitionLocation.trimmed(),
                                                     record.competitionName.trimmed().isEmpty() ? QStringLiteral("未关联") : competitionDate,
                                                     record.competitionType.trimmed().isEmpty() ? QStringLiteral("未填写") : record.competitionType.trimmed()),
                                            card);
        competitionLabel->setProperty("role", "muted");
        competitionLabel->setWordWrap(true);
        cardLayout->addWidget(competitionLabel);

        auto *eventLabel = new QLabel(QStringLiteral("场次：%1   项目：%2   轮次：%3   分组：%4   参赛号：%5   道次：%6   成绩/名次：%7/%8")
                                          .arg(record.raceName.trimmed().isEmpty() ? QStringLiteral("未关联场次") : record.raceName.trimmed(),
                                               record.eventName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.eventName.trimmed(),
                                               record.heatName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.heatName.trimmed(),
                                               record.groupName.trimmed().isEmpty() ? QStringLiteral("未填写") : record.groupName.trimmed(),
                                               record.bibNumber.trimmed().isEmpty() ? QStringLiteral("未填写") : record.bibNumber.trimmed(),
                                               record.laneNumber.trimmed().isEmpty() ? QStringLiteral("未填写") : record.laneNumber.trimmed(),
                                               record.resultScore >= 0 ? QString::number(record.resultScore) : QStringLiteral("未填"),
                                               record.resultRank >= 0 ? QString::number(record.resultRank) : QStringLiteral("未填")),
                                      card);
        eventLabel->setProperty("role", "muted");
        eventLabel->setWordWrap(true);
        cardLayout->addWidget(eventLabel);

        auto *sourceTypeLabel = new QLabel(QStringLiteral("分析归属：%1   对象：%2")
                                               .arg(sessionSourceTypeLabel(record.sourceType),
                                                    sessionSourceDisplay(record)),
                                           card);
        sourceTypeLabel->setProperty("role", "muted");
        sourceTypeLabel->setWordWrap(true);
        cardLayout->addWidget(sourceTypeLabel);

        if (repetitions.isEmpty()) {
            auto *emptyReviewLabel = new QLabel(QStringLiteral("本次尚未保存动作实例。复盘会先展示 session 摘要，后续采集到动作计数后会自动列出动作明细、最好/最差动作和错误时间轴。"),
                                                card);
            emptyReviewLabel->setProperty("role", "muted");
            emptyReviewLabel->setWordWrap(true);
            cardLayout->addWidget(emptyReviewLabel);
        } else {
            const auto bestIt = std::max_element(repetitions.cbegin(),
                                                 repetitions.cend(),
                                                 [](const ActionRepetition &lhs, const ActionRepetition &rhs) {
                                                     return lhs.effectiveScore() < rhs.effectiveScore();
                                                 });
            const auto worstIt = std::min_element(repetitions.cbegin(),
                                                  repetitions.cend(),
                                                  [](const ActionRepetition &lhs, const ActionRepetition &rhs) {
                                                      return lhs.effectiveScore() < rhs.effectiveScore();
                                                  });

            const ActionRepetition best = bestIt != repetitions.cend() ? *bestIt : ActionRepetition();
            const ActionRepetition worst = worstIt != repetitions.cend() ? *worstIt : ActionRepetition();
            const int bestIndex = bestIt != repetitions.cend()
                                      ? static_cast<int>(std::distance(repetitions.cbegin(), bestIt))
                                      : 0;
            const int worstIndex = worstIt != repetitions.cend()
                                       ? static_cast<int>(std::distance(repetitions.cbegin(), worstIt))
                                       : 0;
            auto *reviewLabel = new QLabel(QStringLiteral("代表动作：最好 #%1 %2-%3 · %4 分 · %5\n待纠正：最差 #%6 %7-%8 · %9 分 · %10")
                                               .arg(bestIndex + 1)
                                               .arg(formatMilliseconds(best.effectiveStartedMs()))
                                               .arg(formatMilliseconds(best.effectiveEndedMs()))
                                               .arg(best.effectiveScore())
                                               .arg(best.effectiveFeedback().trimmed().isEmpty() ? QStringLiteral("反馈为空") : best.effectiveFeedback())
                                               .arg(worstIndex + 1)
                                               .arg(formatMilliseconds(worst.effectiveStartedMs()))
                                               .arg(formatMilliseconds(worst.effectiveEndedMs()))
                                               .arg(worst.effectiveScore())
                                               .arg(worst.effectiveFeedback().trimmed().isEmpty() ? issueSummary(worst.effectiveErrorCodes()) : worst.effectiveFeedback()),
                                           card);
            reviewLabel->setProperty("role", "reviewSummary");
            reviewLabel->setWordWrap(true);
            cardLayout->addWidget(reviewLabel);

            QStringList timelineLines;
            for (int repIndex = 0; repIndex < repetitions.size() && timelineLines.size() < 5; ++repIndex) {
                const ActionRepetition &repetition = repetitions.at(repIndex);
                if (repetition.effectiveValid() && repetition.effectiveErrorCodes().trimmed().isEmpty()) {
                    continue;
                }
                timelineLines.append(QStringLiteral("#%1 关键帧 %2 · %3 · %4")
                                         .arg(repIndex + 1)
                                         .arg(formatMilliseconds(repetition.keyFrameMs > 0 ? repetition.keyFrameMs : repetition.effectiveStartedMs()))
                                         .arg(issueSummary(repetition.effectiveErrorCodes()))
                                         .arg(repetition.effectiveFeedback().trimmed().isEmpty()
                                                  ? qualityLabel(repetition.effectiveScore(), repetition.effectiveValid())
                                                  : repetition.effectiveFeedback()));
            }
            auto *timelineLabel = new QLabel(timelineLines.isEmpty()
                                                 ? QStringLiteral("关键错误时间轴：本次动作未触发明显错误点。")
                                                 : QStringLiteral("关键错误时间轴：\n%1").arg(timelineLines.join(QLatin1Char('\n'))),
                                             card);
            timelineLabel->setProperty("role", "muted");
            timelineLabel->setWordWrap(true);
            cardLayout->addWidget(timelineLabel);

            auto *detailTitle = new QLabel(QStringLiteral("动作明细"), card);
            detailTitle->setProperty("role", "sectionTitle");
            cardLayout->addWidget(detailTitle);

            const int detailCount = std::min(8, static_cast<int>(repetitions.size()));
            for (int repIndex = 0; repIndex < detailCount; ++repIndex) {
                const ActionRepetition repetition = repetitions.at(repIndex);
                auto *row = new QWidget(card);
                auto *rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                rowLayout->setSpacing(8);

                auto *detailLabel = new QLabel(repetitionReportLine(repetition,
                                                                    repIndex,
                                                                    [this](int ms) { return formatMilliseconds(ms); }),
                                               row);
                detailLabel->setWordWrap(true);
                detailLabel->setProperty("role", "muted");

                auto *clipButton = new QPushButton(QStringLiteral("定位片段"), row);
                clipButton->setProperty("role", "secondaryButton");
                configureStableButton(clipButton, kHistoryActionButtonWidth, 32, QSize(0, 0));
                clipButton->setEnabled(hasNvrPlayback
                                       || !record.videoSource.trimmed().isEmpty()
                                       || !record.videoFallbackSource.trimmed().isEmpty());
                connect(clipButton, &QPushButton::clicked, this, [this, record, repetition]() {
                    openSessionVideo(record, repetition.videoClipStartMs, repetition.videoClipEndMs);
                });

                rowLayout->addWidget(detailLabel, 1);
                rowLayout->addWidget(clipButton, 0, Qt::AlignRight | Qt::AlignTop);
                cardLayout->addWidget(row);
            }
            if (repetitions.size() > detailCount) {
                auto *moreLabel = new QLabel(QStringLiteral("还有 %1 个动作实例，可通过导出报告查看完整明细。")
                                                 .arg(repetitions.size() - detailCount),
                                             card);
                moreLabel->setProperty("role", "muted");
                cardLayout->addWidget(moreLabel);
            }
        }

        auto *coachCommentLabel = new QLabel(QStringLiteral("教练批注：%1")
                                                 .arg(record.coachComment.trimmed().isEmpty()
                                                          ? QStringLiteral("未填写")
                                                          : record.coachComment.trimmed()),
                                             card);
        coachCommentLabel->setWordWrap(true);
        coachCommentLabel->setProperty("role", "muted");
        cardLayout->addWidget(coachCommentLabel);

        ui->historyListLayout->addWidget(card);
    }
}

// 刷新动作纠正建议列表。
void MainWindow::refreshSuggestions()
{
    if (!ui->suggestionListLayout) {
        return;
    }

    clearLayout(ui->suggestionListLayout);

    if (m_records.isEmpty()) {
        auto *emptyLabel = new QLabel(QStringLiteral("暂无可生成建议的训练记录。\n保存至少一条训练记录后，这里会自动给出动作纠正与下次训练建议。"),
                                      ui->suggestionPage);
        emptyLabel->setProperty("role", "emptyBox");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        ui->suggestionListLayout->addWidget(emptyLabel);
        return;
    }

    const SessionHistoryItem &latest = m_records.first();
    int totalScore = 0;
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        totalScore += record.score;
    }
    const int averageScore = (totalScore + m_records.size() / 2) / m_records.size();

    auto addSuggestionCard = [this](const QString &title, const QString &tag, const QString &body) {
        auto *card = new QFrame(ui->suggestionPage);
        setRole(card, "suggestionCard");

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 14, 14, 14);
        cardLayout->setSpacing(10);

        auto *headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);

        auto *titleLabel = new QLabel(title, card);
        titleLabel->setProperty("role", "sectionTitle");

        headerLayout->addWidget(titleLabel, 1);
        if (!tag.isEmpty()) {
            auto *tagLabel = new QLabel(tag, card);
            tagLabel->setProperty("role", "scoreTag");
            tagLabel->setAlignment(Qt::AlignCenter);
            headerLayout->addWidget(tagLabel, 0, Qt::AlignRight | Qt::AlignTop);
        }
        cardLayout->addLayout(headerLayout);

        auto *bodyLabel = new QLabel(body, card);
        bodyLabel->setWordWrap(true);
        cardLayout->addWidget(bodyLabel);

        ui->suggestionListLayout->addWidget(card);
    };

    QString summaryText;
    if (latest.score >= 90) {
        summaryText = QStringLiteral("最新训练中“%1”整体表现优秀，可以在保持稳定的前提下继续打磨动作细节。").arg(latest.actionName);
    } else if (latest.score >= 75) {
        summaryText = QStringLiteral("最新训练中“%1”整体较稳定，已经具备继续提分的基础，适合做针对性微调。").arg(latest.actionName);
    } else if (latest.score >= 60) {
        summaryText = QStringLiteral("最新训练中“%1”基础动作已建立，但还需要更有针对性的纠正训练来提升完成度。").arg(latest.actionName);
    } else {
        summaryText = QStringLiteral("最新训练中“%1”得分偏低，建议先放慢节奏，优先保证动作质量和姿态完整性。").arg(latest.actionName);
    }
    addSuggestionCard(QStringLiteral("综合结论"),
                      QStringLiteral("%1 分").arg(latest.score),
                      QStringLiteral("%1 运动员：%2。当前反馈：%3")
                          .arg(summaryText, latest.athleteName, latest.feedback));

    if (m_trainingRepository && m_trainingRepository->isOpen()) {
        const TrainingTrendWindow sevenDayTrend = m_trainingRepository->trendForRecentDays(7);
        const TrainingTrendWindow thirtyDayTrend = m_trainingRepository->trendForRecentDays(30);
        addSuggestionCard(QStringLiteral("训练趋势"),
                          QStringLiteral("7/30 天"),
                          QStringLiteral("%1\n%2\n弱项变化：%3")
                              .arg(trendWindowLine(sevenDayTrend),
                                   trendWindowLine(thirtyDayTrend),
                                   weakTrendSummary(sevenDayTrend, thirtyDayTrend)));
    }

    struct MetricAdvice {
        QString name;
        int value = 0;
        QString advice;
    };

    QVector<MetricAdvice> metrics = {
        {QStringLiteral("关键点"), latest.detectionScore,
         QStringLiteral("优先检查站位是否完整入镜，并保持机位稳定，确保关键点持续可见。")},
        {QStringLiteral("对称"), latest.symmetryScore,
         QStringLiteral("加强左右发力和肢体展开的一致性，减少单侧代偿和发力不均。")},
        {QStringLiteral("重心"), latest.balanceScore,
         QStringLiteral("训练时注意核心收紧与落刃重心控制，减少重心前后漂移。")},
        {QStringLiteral("稳定"), latest.stabilityScore,
         QStringLiteral("先放慢节奏，减少多余摆动，稳定住主干后再逐步提速。")},
        {QStringLiteral("3D"), latest.depthScore,
         QStringLiteral("调整相机角度并保持身体朝向清晰，提升立体姿态的可辨识度。")}
    };

    MetricAdvice weakestMetric = metrics.first();
    for (const MetricAdvice &metric : std::as_const(metrics)) {
        if (metric.value < weakestMetric.value) {
            weakestMetric = metric;
        }
    }
    addSuggestionCard(QStringLiteral("重点纠正"),
                      QStringLiteral("%1 %2 分").arg(weakestMetric.name).arg(weakestMetric.value),
                      QStringLiteral("问题部位：%1。触发原因：%2 分低于动作标准预期。建议动作：%3 优先级：P1。")
                          .arg(weakestMetric.name)
                          .arg(weakestMetric.name)
                          .arg(weakestMetric.advice));

    QString nextTrainingText;
    if (latest.duration < 300) {
        nextTrainingText = QStringLiteral("本次有效训练时长为 %1，建议下次先把单次有效训练稳定提升到 5 分钟以上，再观察动作质量变化。")
                               .arg(formatTime(latest.duration));
    } else if (latest.targetReps > 0 && latest.validReps < latest.targetReps) {
        nextTrainingText = QStringLiteral("本次有效动作 %1/%2，建议下次先把同一动作做到足量达标，再增加节奏或难度。")
                               .arg(latest.validReps)
                               .arg(latest.targetReps);
    } else if (averageScore - latest.score >= 8) {
        nextTrainingText = QStringLiteral("最新得分比历史均分低 %1 分，建议下次放慢节奏，优先做纠正训练，再逐步恢复速度。")
                               .arg(averageScore - latest.score);
    } else {
        nextTrainingText = QStringLiteral("当前训练节奏较稳，建议保持现有强度，继续围绕“%1”细化动作，冲击更高分。")
                               .arg(weakestMetric.name);
    }
    addSuggestionCard(QStringLiteral("下次训练建议"),
                      QStringLiteral("均分 %1").arg(averageScore),
                      nextTrainingText);
}

void MainWindow::openSessionVideo(const SessionHistoryItem &record, int offsetMs, int endOffsetMs)
{
    const NvrPlaybackResult nvr = buildNvrPlaybackUrl(m_sharedCameraSettings,
                                                      m_cameraSlotSettings,
                                                      record,
                                                      offsetMs > 0 ? offsetMs : -1,
                                                      endOffsetMs);
    const bool useNvr = !nvr.url.trimmed().isEmpty();
    const QString source = useNvr
                               ? nvr.url.trimmed()
                               : (!record.videoSource.trimmed().isEmpty()
                                      ? record.videoSource.trimmed()
                                      : record.videoFallbackSource.trimmed());
    if (source.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("暂无视频引用"), QStringLiteral("这条训练记录没有保存可回看的视频引用。"));
        return;
    }

    const bool sourceIsUrl = hasMediaUrlScheme(source);
    const QString sourceTitle = record.videoCameraName.trimmed().isEmpty()
                                    ? (sourceIsUrl ? QStringLiteral("训练回看") : offlineVideoDisplayName(source))
                                    : record.videoCameraName.trimmed();
    ui->mainImageLabel->setPlaceholderText(sourceTitle);

    if (!sourceIsUrl) {
        const QFileInfo fileInfo(source);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            ui->mainImageLabel->stopPlayback();
            ui->mainImageLabel->setPlaceholderText(QStringLiteral("训练回看\n文件不存在"));
            if (ui->focusTitleLabel) {
                ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(sourceTitle));
            }
            if (m_handAnalysisManager) {
                m_handAnalysisManager->setPaused(true);
                m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
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

    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(true);
        m_handAnalysisManager->setActiveStreams(QVector<HandAnalysisManager::AnalysisStream>());
    }
    if (ui->focusTitleLabel) {
        ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(sourceTitle));
    }
    switchPage(kCapturePage);

    QString offsetTip;
    if (offsetMs > 0) {
        if (useNvr) {
            offsetTip = QStringLiteral("。已打开 NVR 片段窗口 %1-%2。")
                            .arg(formatMilliseconds(nvr.startOffsetMs),
                                 formatMilliseconds(nvr.endOffsetMs));
        } else if (sourceIsUrl) {
            offsetTip = QStringLiteral("。RTSP/网络视频暂不支持自动定位，已打开视频源；片段起点 %1 可作为人工回看参考。")
                            .arg(formatMilliseconds(offsetMs));
        } else {
            offsetTip = QStringLiteral("。已请求定位到片段起点 %1。").arg(formatMilliseconds(offsetMs));
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
        out << "\n## 综合反馈\n\n" << (record.feedback.trimmed().isEmpty() ? QStringLiteral("未填写") : record.feedback.trimmed()) << "\n\n";
        out << "## 教练批注\n\n"
            << (record.coachComment.trimmed().isEmpty() ? QStringLiteral("未填写") : record.coachComment.trimmed())
            << "\n\n";
        out << "## 动作明细\n\n";
        if (repetitions.isEmpty()) {
            out << "本次未保存动作实例。\n";
        } else {
            out << "| # | 来源 | 复核状态 | 时间 | AI原始 | 复核后 | 错误项 | 反馈 | 教练备注 |\n";
            out << "|---|---|---|---|---|---|---|---|---|\n";
            for (int i = 0; i < repetitions.size(); ++i) {
                const ActionRepetition &rep = repetitions.at(i);
                out << "| " << (i + 1)
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
            QStringLiteral("action"),
            QStringLiteral("standard_version"),
            QStringLiteral("duration"),
            QStringLiteral("total_reps"),
            QStringLiteral("valid_reps"),
            QStringLiteral("session_average_score"),
            QStringLiteral("session_best_score"),
            QStringLiteral("training_notes"),
            QStringLiteral("video_source"),
            QStringLiteral("rep_index"),
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
                << csvField(QStringLiteral("%1/%2").arg(record.actionCategory, record.actionName))
                << csvField(QString::number(record.standardVersion))
                << csvField(formatTime(record.duration))
                << csvField(QString::number(record.totalReps))
                << csvField(QString::number(record.validReps))
                << csvField(QString::number(record.score))
                << csvField(QString::number(record.bestScore))
                << csvField(record.notes)
                << csvField(displayMediaSource(record.videoSource));
            if (!rep) {
                for (int i = 0; i < 29; ++i) {
                    row << csvField(QString());
                }
                out << row.join(QLatin1Char(',')) << "\n";
                return;
            }
            row << csvField(QString::number(index + 1))
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
            {QStringLiteral("视频"), displayMediaSource(record.videoSource)}
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
            out << "<table><tr><th>#</th><th>来源</th><th>复核</th><th>时间</th><th>AI原始</th><th>复核后</th><th>错误项</th><th>反馈/备注</th></tr>";
            for (int i = 0; i < repetitions.size(); ++i) {
                const ActionRepetition &rep = repetitions.at(i);
                out << "<tr><td>" << (i + 1) << "</td>"
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

// 根据侧栏显示状态刷新顶部侧栏按钮：默认仅显示灰色图标，悬停时显示文字并变为蓝色。
void MainWindow::refreshSidebarButton()
{
    const bool hovered = ui->toggleSidebarButton->underMouse();
    const QString label = m_sidebarVisible ? QStringLiteral("隐藏侧栏") : QStringLiteral("显示侧栏");
    ui->toggleSidebarButton->setText(label);
    ui->toggleSidebarButton->setIcon(makeNormalizedTintedSvgIcon(QStringLiteral(":/icons/sidebar.svg"),
                                                                 QColor(QString::fromLatin1(hovered ? kHoverActionColor : kMutedInactiveColor)),
                                                                 22,
                                                                 18));
    ui->toggleSidebarButton->setIconSize(QSize(22, 22));
    ui->toggleSidebarButton->setToolTip(label);
    ui->toggleSidebarButton->setStatusTip(label);
    ui->toggleSidebarButton->setAccessibleName(label);
    configureStableButton(ui->toggleSidebarButton, kTopIconButtonWidth, 40, QSize(22, 22));
}

// 根据当前窗口状态刷新顶部全屏按钮：默认仅显示灰色图标，悬停时显示文字并变为蓝色。
void MainWindow::refreshFullScreenButton()
{
    if (!m_fullScreenButton) {
        return;
    }

    const bool hovered = m_fullScreenButton->underMouse();
    const bool fullScreen = isFullScreen();
    const QString label = fullScreen ? QStringLiteral("退出全屏") : QStringLiteral("F11全屏");
    const QString iconPath = fullScreen
                                 ? QStringLiteral(":/icons/exit_fullscreen.svg")
                                 : QStringLiteral(":/icons/fullscreen.svg");

    m_fullScreenButton->setText(label);
    m_fullScreenButton->setIcon(makeNormalizedTintedSvgIcon(iconPath,
                                                            QColor(QString::fromLatin1(hovered ? kHoverActionColor : kMutedInactiveColor)),
                                                            22,
                                                            18));
    m_fullScreenButton->setIconSize(QSize(22, 22));
    m_fullScreenButton->setToolTip(fullScreen
                                       ? QStringLiteral("退出全屏显示（Esc）")
                                       : QStringLiteral("进入全屏显示（F11）"));
    m_fullScreenButton->setStatusTip(m_fullScreenButton->toolTip());
    m_fullScreenButton->setAccessibleName(label);
    configureStableButton(m_fullScreenButton, kTopIconButtonWidth, 40, QSize(22, 22));
}

void MainWindow::refreshModelStatus(const QString &statusText)
{
    if (!ui || !ui->modelStatusLabel) {
        return;
    }
    ui->modelStatusLabel->setTextFormat(Qt::RichText);
    ui->modelStatusLabel->setText(topbarModuleHtml(QStringLiteral("AI"),
                                                   QStringLiteral("模型状态 Status"),
                                                   statusText));
}

void MainWindow::clearRealtimePose()
{
    ui->mainImageLabel->setPoseFrame({});
    if (m_skeletonView) {
        m_skeletonView->clearPoseFrame();
    }
    if (m_trajectoryWidget) {
        m_trajectoryWidget->clearPoseFrame();
    }
    m_realtimeScore = 0;
    m_detectionScore = 0;
    m_symmetryScore = 0;
    m_balanceScore = 0;
    m_stabilityScore = 0;
    m_depthScore = 0;
    m_feedbackText = QStringLiteral("等待姿态");
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
