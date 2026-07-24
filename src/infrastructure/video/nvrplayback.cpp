#include "nvrplayback.h"

#include <QDateTime>
#include <QUrl>

#include <algorithm>

namespace {

QString nvrTimestamp(const QDateTime &value)
{
    return value.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

QString cameraChannelToken(int camera)
{
    return QStringLiteral("%1").arg(camera, 2, 10, QLatin1Char('0'));
}

QDateTime sessionStartTime(const SessionHistoryItem &record)
{
    QDateTime started = record.startedAt;
    if (!started.isValid()) {
        started = QDateTime::fromString(record.time, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (started.isValid()) {
            started = started.addSecs(-std::max(0, record.duration));
        }
    }
    return started;
}

} // namespace

NvrPlaybackResult buildNvrPlaybackUrl(const SharedCameraSettings &sharedSettings,
                                      const QVector<CameraSlotSettings> &cameraSlotSettings,
                                      const SessionHistoryItem &record,
                                      int clipStartMs,
                                      int clipEndMs)
{
    NvrPlaybackResult result;
    const QString templ = sharedSettings.nvrPlaybackTemplate.trimmed();
    if (templ.isEmpty()) {
        result.error = QStringLiteral("未配置 NVR 回放模板");
        return result;
    }
    if (record.camera <= 0) {
        result.error = QStringLiteral("离线视频或未知机位不使用 NVR 回放");
        return result;
    }
    if (record.camera > cameraSlotSettings.size()) {
        result.error = QStringLiteral("训练记录机位超出当前相机配置");
        return result;
    }

    const CameraSlotSettings slot = cameraSlotSettings.at(record.camera - 1);
    const QString ip = slot.ip.trimmed();
    if (ip.isEmpty()) {
        result.error = QStringLiteral("训练记录机位未配置 IP");
        return result;
    }

    const QDateTime baseStart = sessionStartTime(record);
    if (!baseStart.isValid()) {
        result.error = QStringLiteral("训练记录缺少可用于 NVR 回放的开始时间");
        return result;
    }

    const int durationMs = std::max(0, record.duration) * 1000;
    const int startOffsetMs = clipStartMs >= 0 ? std::max(0, clipStartMs) : 0;
    int endOffsetMs = clipEndMs >= 0 ? std::max(clipEndMs, startOffsetMs + 1000) : durationMs;
    if (endOffsetMs <= startOffsetMs) {
        endOffsetMs = startOffsetMs + 1000;
    }
    if (durationMs > 0 && clipEndMs < 0) {
        endOffsetMs = durationMs;
    }

    const QDateTime start = baseStart.addMSecs(startOffsetMs);
    const QDateTime end = baseStart.addMSecs(endOffsetMs);
    QString url = templ;
    url.replace(QStringLiteral("{user}"), QString::fromUtf8(QUrl::toPercentEncoding(sharedSettings.username.trimmed())));
    url.replace(QStringLiteral("{password}"), QString::fromUtf8(QUrl::toPercentEncoding(sharedSettings.password)));
    url.replace(QStringLiteral("{ip}"), ip);
    url.replace(QStringLiteral("{port}"),
                sharedSettings.port.trimmed().isEmpty() ? QStringLiteral("554") : sharedSettings.port.trimmed());
    url.replace(QStringLiteral("{channel}"), cameraChannelToken(record.camera));
    url.replace(QStringLiteral("{start}"), nvrTimestamp(start));
    url.replace(QStringLiteral("{end}"), nvrTimestamp(end));

    result.url = url;
    result.startOffsetMs = startOffsetMs;
    result.endOffsetMs = endOffsetMs;
    return result;
}
