#include "athletedetectionroi.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

void addWarning(QStringList *warnings, const QString &message)
{
    if (warnings) {
        warnings->append(message);
    }
}

bool cameraIdFromKey(const QString &key, int *cameraId)
{
    if (!key.startsWith(QStringLiteral("cam"), Qt::CaseInsensitive)) {
        return false;
    }

    bool ok = false;
    const int parsed = key.mid(3).toInt(&ok);
    if (!ok || parsed <= 0) {
        return false;
    }

    if (cameraId) {
        *cameraId = parsed;
    }
    return true;
}

bool readPolygon(const QJsonArray &points, QPolygonF *polygon)
{
    if (!polygon || points.size() < 3) {
        return false;
    }

    QPolygonF result;
    result.reserve(points.size());
    for (const QJsonValue &value : points) {
        const QJsonArray point = value.toArray();
        if (point.size() != 2 || !point.at(0).isDouble() || !point.at(1).isDouble()) {
            return false;
        }
        result.append(QPointF(point.at(0).toDouble(), point.at(1).toDouble()));
    }

    *polygon = result;
    return true;
}

} // namespace

bool AthleteDetectionRoi::isValid() const
{
    return referenceFrameSize.width() > 0
           && referenceFrameSize.height() > 0
           && polygon.size() >= 3;
}

AthleteDetectionRoiMap loadAthleteDetectionRois(const QString &filePath, QStringList *warnings)
{
    AthleteDetectionRoiMap rois;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        addWarning(warnings, QStringLiteral("无法打开检测 ROI 配置：%1").arg(file.errorString()));
        return rois;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addWarning(warnings, QStringLiteral("检测 ROI 配置 JSON 无效：%1").arg(parseError.errorString()));
        return rois;
    }

    const QJsonObject cameras = document.object().value(QStringLiteral("cameras")).toObject();
    if (cameras.isEmpty()) {
        addWarning(warnings, QStringLiteral("检测 ROI 配置未包含 cameras 对象。"));
        return rois;
    }

    for (auto iterator = cameras.constBegin(); iterator != cameras.constEnd(); ++iterator) {
        int cameraId = 0;
        if (!cameraIdFromKey(iterator.key(), &cameraId)) {
            addWarning(warnings, QStringLiteral("忽略无效 ROI 相机键：%1").arg(iterator.key()));
            continue;
        }

        const QJsonObject object = iterator.value().toObject();
        AthleteDetectionRoi roi;
        roi.referenceFrameSize = QSize(object.value(QStringLiteral("frame_width")).toInt(),
                                       object.value(QStringLiteral("frame_height")).toInt());
        if (!readPolygon(object.value(QStringLiteral("polygon")).toArray(), &roi.polygon)
            || !roi.isValid()) {
            addWarning(warnings, QStringLiteral("忽略无效 ROI：%1").arg(iterator.key()));
            continue;
        }

        if (rois.contains(cameraId)) {
            addWarning(warnings, QStringLiteral("忽略重复 ROI 相机：%1").arg(iterator.key()));
            continue;
        }
        rois.insert(cameraId, roi);
    }

    return rois;
}

bool isAthleteDetectionInsideRoi(const AthleteDetectionRoi &roi,
                                 const QRectF &box,
                                 const QSize &frameSize)
{
    if (!roi.isValid() || !box.isValid() || frameSize.width() <= 0 || frameSize.height() <= 0) {
        return false;
    }

    const qreal scaleX = static_cast<qreal>(frameSize.width()) / roi.referenceFrameSize.width();
    const qreal scaleY = static_cast<qreal>(frameSize.height()) / roi.referenceFrameSize.height();
    QPolygonF scaledPolygon;
    scaledPolygon.reserve(roi.polygon.size());
    for (const QPointF &point : roi.polygon) {
        scaledPolygon.append(QPointF(point.x() * scaleX, point.y() * scaleY));
    }

    const QPointF footPoint(box.center().x(), box.bottom());
    return scaledPolygon.containsPoint(footPoint, Qt::OddEvenFill);
}
