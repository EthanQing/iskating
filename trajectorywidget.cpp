#include "trajectorywidget.h"

#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QSizePolicy>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr qint64 kHistoryWindowMs = 6000;
constexpr qint64 kFieldHistoryWindowMs = 600000;
constexpr qint64 kMinSampleIntervalMs = 100;
constexpr int kMaxFieldSamples = 5000;
constexpr qreal kDefaultFieldWidthM = 12.0;
constexpr std::array<int, 7> kTrackedKeypointIndices = {0, 5, 6, 11, 12, 15, 16};

struct ProjectedOffset
{
    QPointF point;
    qreal depth = 0.0;
};

const PoseInstance *primaryPerson(const PoseFrameResult &frame)
{
    const PoseInstance *best = nullptr;
    for (const PoseInstance &instance : frame.instances) {
        if (instance.kind != PoseInstanceKind::Person) {
            continue;
        }
        if (!best || instance.confidence > best->confidence) {
            best = &instance;
        }
    }
    return best;
}

const PoseKeypoint *keypointAt(const PoseInstance &instance, int index)
{
    if (index < 0 || index >= instance.keypoints.size()) {
        return nullptr;
    }
    const PoseKeypoint &keypoint = instance.keypoints.at(index);
    return keypoint.valid ? &keypoint : nullptr;
}

QPointF anchorPointFor(const PoseInstance &instance)
{
    const PoseKeypoint *leftHip = keypointAt(instance, 11);
    const PoseKeypoint *rightHip = keypointAt(instance, 12);
    if (leftHip && rightHip) {
        return (leftHip->imagePoint + rightHip->imagePoint) * 0.5;
    }
    return instance.box.isValid() ? instance.box.center() : QPointF();
}

bool rootPoint3dFor(const PoseInstance &instance, QVector3D *rootPoint)
{
    if (!rootPoint) {
        return false;
    }

    const PoseKeypoint *leftHip = keypointAt(instance, 11);
    const PoseKeypoint *rightHip = keypointAt(instance, 12);
    if (!leftHip || !rightHip || !leftHip->hasPoint3d || !rightHip->hasPoint3d) {
        return false;
    }

    *rootPoint = (leftHip->point3d + rightHip->point3d) * 0.5f;
    return true;
}

QRectF anchorBounds(const QVector<TrajectoryWidget::TrajectorySample> &samples)
{
    if (samples.isEmpty()) {
        return {};
    }

    bool hasPoint = false;
    qreal minX = 0.0;
    qreal minY = 0.0;
    qreal maxX = 0.0;
    qreal maxY = 0.0;
    for (const TrajectoryWidget::TrajectorySample &sample : samples) {
        const QPointF point = sample.anchorImagePoint;
        if (!hasPoint) {
            minX = maxX = point.x();
            minY = maxY = point.y();
            hasPoint = true;
        } else {
            minX = std::min(minX, point.x());
            maxX = std::max(maxX, point.x());
            minY = std::min(minY, point.y());
            maxY = std::max(maxY, point.y());
        }
    }

    if (!hasPoint) {
        return {};
    }

    QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
    const qreal padX = std::max<qreal>(36.0, bounds.width() * 0.18);
    const qreal padY = std::max<qreal>(30.0, bounds.height() * 0.16);
    bounds.adjust(-padX, -padY, padX, padY);
    if (bounds.width() < 1.0) {
        bounds.adjust(-0.5, 0.0, 0.5, 0.0);
    }
    if (bounds.height() < 1.0) {
        bounds.adjust(0.0, -0.5, 0.0, 0.5);
    }
    return bounds;
}

const TrajectoryWidget::CameraSegment *segmentForCamera(const QVector<TrajectoryWidget::CameraSegment> &segments,
                                                        int cameraId)
{
    for (const TrajectoryWidget::CameraSegment &segment : segments) {
        if (segment.enabled && segment.cameraId == cameraId && segment.fieldEndM > segment.fieldStartM) {
            return &segment;
        }
    }
    return nullptr;
}

bool hasFieldSamples(const QVector<TrajectoryWidget::TrajectorySample> &samples)
{
    return std::any_of(samples.cbegin(), samples.cend(), [](const TrajectoryWidget::TrajectorySample &sample) {
        return sample.hasFieldPoint;
    });
}

QRectF fieldBounds(const QVector<TrajectoryWidget::CameraSegment> &segments,
                   const QVector<TrajectoryWidget::TrajectorySample> &samples)
{
    bool hasValue = false;
    qreal minX = 0.0;
    qreal maxX = 0.0;
    qreal minY = -kDefaultFieldWidthM * 0.5;
    qreal maxY = kDefaultFieldWidthM * 0.5;

    auto includePoint = [&](qreal x, qreal y) {
        if (!hasValue) {
            minX = maxX = x;
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            hasValue = true;
            return;
        }
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };

    for (const TrajectoryWidget::CameraSegment &segment : segments) {
        if (!segment.enabled || segment.fieldEndM <= segment.fieldStartM) {
            continue;
        }
        includePoint(segment.fieldStartM, segment.lateralOffsetM);
        includePoint(segment.fieldEndM, segment.lateralOffsetM);
    }

    for (const TrajectoryWidget::TrajectorySample &sample : samples) {
        if (sample.hasFieldPoint) {
            includePoint(sample.fieldPoint.x(), sample.fieldPoint.y());
        }
    }

    if (!hasValue) {
        return {};
    }

    QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
    const qreal padX = std::max<qreal>(5.0, bounds.width() * 0.04);
    const qreal padY = std::max<qreal>(2.0, bounds.height() * 0.20);
    bounds.adjust(-padX, -padY, padX, padY);
    if (bounds.width() < 1.0) {
        bounds.adjust(-0.5, 0.0, 0.5, 0.0);
    }
    if (bounds.height() < 1.0) {
        bounds.adjust(0.0, -0.5, 0.0, 0.5);
    }
    return bounds;
}

QPointF mapFieldToTrack(const QPointF &fieldPoint, const QRectF &bounds, const QRectF &trackRect)
{
    const qreal nx = (fieldPoint.x() - bounds.left()) / std::max<qreal>(1.0, bounds.width());
    const qreal ny = (fieldPoint.y() - bounds.top()) / std::max<qreal>(1.0, bounds.height());
    return QPointF(trackRect.left() + std::clamp(nx, 0.0, 1.0) * trackRect.width(),
                   trackRect.bottom() - std::clamp(ny, 0.0, 1.0) * trackRect.height());
}

QRectF makeTrackRect(const QRectF &rect)
{
    return rect.adjusted(rect.width() * 0.10,
                         rect.height() * 0.13,
                         -rect.width() * 0.08,
                         -rect.height() * 0.16);
}

QPolygonF makeLanePolygon(const QRectF &trackRect, qreal inset)
{
    const QRectF lane = trackRect.adjusted(inset, inset * 0.6, -inset, -inset * 0.25);
    return QPolygonF({
        QPointF(lane.left() + lane.width() * 0.18, lane.top()),
        QPointF(lane.right() - lane.width() * 0.18, lane.top()),
        QPointF(lane.right(), lane.bottom()),
        QPointF(lane.left(), lane.bottom()),
    });
}

QPainterPath pathForLane(const QPolygonF &polygon)
{
    QPainterPath path;
    if (polygon.isEmpty()) {
        return path;
    }
    path.moveTo(polygon.first());
    for (int i = 1; i < polygon.size(); ++i) {
        path.lineTo(polygon.at(i));
    }
    path.closeSubpath();
    return path;
}

QPointF mapAnchorToTrack(const QPointF &anchor, const QRectF &bounds, const QRectF &trackRect)
{
    const qreal nx = (anchor.x() - bounds.left()) / std::max<qreal>(1.0, bounds.width());
    const qreal ny = (anchor.y() - bounds.top()) / std::max<qreal>(1.0, bounds.height());

    const qreal bottomWidth = trackRect.width();
    const qreal topWidth = trackRect.width() * 0.64;
    const qreal widthAtY = bottomWidth * (1.0 - ny) + topWidth * ny;
    const qreal centerX = trackRect.left() + trackRect.width() * (0.50 + (ny - 0.5) * 0.08);
    const qreal x = centerX + (nx - 0.5) * widthAtY;
    const qreal y = trackRect.bottom() - ny * trackRect.height();
    return QPointF(x, y);
}

ProjectedOffset projectOffset(const TrajectoryWidget::KeypointTrace &keypoint)
{
    if (keypoint.has3d) {
        const QVector3D &v = keypoint.offset3d;
        return {
            QPointF(v.x() * 0.22 + v.z() * 0.42, -v.y() * 0.18 - v.z() * 0.14),
            static_cast<qreal>(v.z())
        };
    }
    return {QPointF(keypoint.point2d.x() * 0.16, keypoint.point2d.y() * 0.16), 0.0};
}

QColor ageColor(const QColor &base, int index, int count, qreal alphaMin, qreal alphaMax)
{
    if (count <= 1) {
        QColor color = base;
        color.setAlphaF(alphaMax);
        return color;
    }
    const qreal t = static_cast<qreal>(index) / static_cast<qreal>(count - 1);
    QColor color = base.lighter(100 + static_cast<int>(t * 30.0));
    color.setAlphaF(alphaMin + (alphaMax - alphaMin) * t);
    return color;
}

void drawBackground(QPainter *painter, const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, QColor(12, 17, 26));
    gradient.setColorAt(0.62, QColor(14, 20, 31));
    gradient.setColorAt(1.0, QColor(8, 11, 18));
    painter->fillRect(rect, gradient);
}

void drawTrack(QPainter *painter, const QRectF &trackRect)
{
    const QPolygonF outerLane = makeLanePolygon(trackRect, 0.0);
    const QPolygonF innerLane = makeLanePolygon(trackRect, trackRect.width() * 0.11);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(17, 27, 43, 170));
    painter->drawPath(pathForLane(outerLane));
    painter->setBrush(QColor(11, 18, 30, 210));
    painter->drawPath(pathForLane(innerLane));

    painter->setPen(QPen(QColor(80, 102, 138, 110), 1.1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPolyline(outerLane);
    painter->drawPolyline(innerLane);

    for (int i = 1; i < 6; ++i) {
        const qreal t = i / 6.0;
        const QPointF leftOuter = outerLane.at(3) * (1.0 - t) + outerLane.at(0) * t;
        const QPointF rightOuter = outerLane.at(2) * (1.0 - t) + outerLane.at(1) * t;
        const QPointF leftInner = innerLane.at(3) * (1.0 - t) + innerLane.at(0) * t;
        const QPointF rightInner = innerLane.at(2) * (1.0 - t) + innerLane.at(1) * t;
        painter->drawLine(leftOuter, leftInner);
        painter->drawLine(rightOuter, rightInner);
    }

    for (int i = 1; i < 5; ++i) {
        const qreal t = i / 5.0;
        const QPointF lower = outerLane.at(3) * (1.0 - t) + outerLane.at(2) * t;
        const QPointF upper = outerLane.at(0) * (1.0 - t) + outerLane.at(1) * t;
        painter->drawLine(lower, upper);
    }
    painter->restore();
}

void drawFieldTrack(QPainter *painter,
                    const QRectF &trackRect,
                    const QRectF &bounds,
                    const QVector<TrajectoryWidget::CameraSegment> &segments)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(12, 23, 38, 225));
    painter->drawRoundedRect(trackRect, 8.0, 8.0);

    painter->setBrush(QColor(21, 34, 52, 180));
    painter->drawRoundedRect(trackRect.adjusted(0.0, trackRect.height() * 0.18, 0.0, -trackRect.height() * 0.18),
                             5.0,
                             5.0);

    painter->setPen(QPen(QColor(80, 102, 138, 120), 1.0));
    for (int i = 0; i <= 6; ++i) {
        const qreal t = i / 6.0;
        const qreal x = trackRect.left() + t * trackRect.width();
        painter->drawLine(QPointF(x, trackRect.top()), QPointF(x, trackRect.bottom()));
    }
    for (int i = 1; i < 4; ++i) {
        const qreal t = i / 4.0;
        const qreal y = trackRect.top() + t * trackRect.height();
        painter->drawLine(QPointF(trackRect.left(), y), QPointF(trackRect.right(), y));
    }

    const QVector<QColor> palette = {
        QColor(64, 153, 255, 42),
        QColor(95, 209, 170, 42),
        QColor(245, 192, 90, 42)
    };
    QFont segmentFont = painter->font();
    segmentFont.setPixelSize(9);
    painter->setFont(segmentFont);
    for (int i = 0; i < segments.size(); ++i) {
        const TrajectoryWidget::CameraSegment &segment = segments.at(i);
        if (!segment.enabled || segment.fieldEndM <= segment.fieldStartM) {
            continue;
        }
        const QPointF start = mapFieldToTrack(QPointF(segment.fieldStartM, bounds.center().y()), bounds, trackRect);
        const QPointF end = mapFieldToTrack(QPointF(segment.fieldEndM, bounds.center().y()), bounds, trackRect);
        QRectF band(QPointF(std::min(start.x(), end.x()), trackRect.top()),
                    QPointF(std::max(start.x(), end.x()), trackRect.bottom()));
        if (band.width() < 2.0) {
            continue;
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.at(i % palette.size()));
        painter->drawRect(band.adjusted(1.0, 1.0, -1.0, -1.0));

        painter->setPen(QColor(164, 184, 213, 155));
        painter->drawText(band.adjusted(2.0, 2.0, -2.0, -2.0),
                          Qt::AlignLeft | Qt::AlignTop,
                          QStringLiteral("C%1").arg(segment.cameraId, 2, 10, QLatin1Char('0')));
    }

    painter->setPen(QPen(QColor(97, 120, 155, 180), 1.2));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(trackRect, 8.0, 8.0);
    painter->restore();
}

void drawArrow(QPainter *painter, const QRectF &trackRect)
{
    const QPointF start(trackRect.left() + trackRect.width() * 0.08, trackRect.bottom() - trackRect.height() * 0.10);
    const QPointF end(trackRect.right() - trackRect.width() * 0.12, trackRect.top() + trackRect.height() * 0.20);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(81, 151, 255, 168), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawLine(start, end);

    const QPointF unit = (end - start) / std::max<qreal>(1.0, std::hypot(end.x() - start.x(), end.y() - start.y()));
    const QPointF side(-unit.y(), unit.x());
    QPolygonF arrowHead;
    arrowHead << end
              << end - unit * 13.0 + side * 6.0
              << end - unit * 13.0 - side * 6.0;
    painter->setBrush(QColor(81, 151, 255, 190));
    painter->drawPolygon(arrowHead);
    painter->restore();
}

void drawEmptyState(QPainter *painter, const QRectF &rect)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QFont titleFont = painter->font();
    titleFont.setPixelSize(15);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(QColor(208, 221, 242, 190));
    painter->drawText(rect.adjusted(0.0, rect.height() * 0.10, 0.0, 0.0),
                      Qt::AlignHCenter | Qt::AlignTop,
                      QStringLiteral("等待轨迹数据"));

    QFont hintFont = painter->font();
    hintFont.setPixelSize(11);
    hintFont.setBold(false);
    painter->setFont(hintFont);
    painter->setPen(QColor(131, 149, 175, 170));
    painter->drawText(rect.adjusted(rect.width() * 0.12, rect.height() * 0.22, -rect.width() * 0.12, 0.0),
                      Qt::AlignHCenter | Qt::TextWordWrap,
                      QStringLiteral("开始采集后，这里会显示主运动员最近6秒的滑行轨迹和关键点尾迹"));
    painter->restore();
}

} // namespace

TrajectoryWidget::TrajectoryWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("trajectoryWidget"));
    setProperty("trajectoryView", true);
    setMinimumSize(180, 140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void TrajectoryWidget::setCameraSegments(const QVector<CameraSegment> &segments)
{
    m_cameraSegments = segments;
    update();
}

void TrajectoryWidget::setPoseFrame(const PoseFrameResult &frame)
{
    if (frame.instances.isEmpty()) {
        clearPoseFrame();
        return;
    }

    appendSample(frame);
    update();
}

void TrajectoryWidget::setTrackPoints(const QVector<TrackPoint> &points)
{
    m_samples.clear();
    m_samples.reserve(points.size());
    for (const TrackPoint &point : points) {
        TrajectorySample sample;
        sample.timestampMs = point.timestampMs;
        sample.cameraId = point.cameraId;
        sample.fieldPoint = QPointF(point.x, point.y);
        sample.hasFieldPoint = true;
        m_samples.append(sample);
    }
    update();
}

void TrajectoryWidget::clearPoseFrame()
{
    if (m_samples.isEmpty()) {
        update();
        return;
    }
    m_samples.clear();
    update();
}

void TrajectoryWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF viewRect = rect();
    drawBackground(&painter, viewRect);

    const QRectF contentRect = viewRect.adjusted(10.0, 10.0, -10.0, -10.0);
    const QRectF trackRect = makeTrackRect(contentRect);
    const bool fieldMode = hasFieldSamples(m_samples);
    const QRectF calibratedBounds = fieldMode ? fieldBounds(m_cameraSegments, m_samples) : QRectF();
    if (fieldMode && calibratedBounds.isValid()) {
        drawFieldTrack(&painter, trackRect, calibratedBounds, m_cameraSegments);
    } else {
        drawTrack(&painter, trackRect);
    }
    drawArrow(&painter, trackRect);

    if (m_samples.isEmpty()) {
        drawEmptyState(&painter, contentRect);
        return;
    }

    const QRectF bounds = fieldMode && calibratedBounds.isValid() ? calibratedBounds : anchorBounds(m_samples);
    if (!bounds.isValid()) {
        drawEmptyState(&painter, contentRect);
        return;
    }

    QVector<QPointF> anchorPoints;
    anchorPoints.reserve(m_samples.size());
    for (const TrajectorySample &sample : m_samples) {
        if (fieldMode && sample.hasFieldPoint) {
            anchorPoints.push_back(mapFieldToTrack(sample.fieldPoint, bounds, trackRect));
        } else {
            anchorPoints.push_back(mapAnchorToTrack(sample.anchorImagePoint, bounds, trackRect));
        }
    }

    for (int keypointIndex = 0; keypointIndex < kTrackedKeypointIndices.size(); ++keypointIndex) {
        QVector<QPointF> tracePoints;
        QVector<QColor> traceColors;
        tracePoints.reserve(m_samples.size());
        traceColors.reserve(m_samples.size());

        for (int sampleIndex = 0; sampleIndex < m_samples.size(); ++sampleIndex) {
            const TrajectorySample &sample = m_samples.at(sampleIndex);
            if (keypointIndex >= sample.keypoints.size()) {
                continue;
            }
            const KeypointTrace &keypoint = sample.keypoints.at(keypointIndex);
            if (!keypoint.valid) {
                continue;
            }

            const ProjectedOffset projected = projectOffset(keypoint);
            const qreal ageDenominator = std::max<qreal>(1.0, static_cast<qreal>(m_samples.size() - 1));
            const qreal ageScale = 0.70 + static_cast<qreal>(sampleIndex) / ageDenominator * 0.60;
            tracePoints.push_back(anchorPoints.at(sampleIndex) + projected.point * ageScale);
            traceColors.push_back(ageColor(QColor(170, 210, 255), sampleIndex, m_samples.size(), 0.12, 0.58));
        }

        if (tracePoints.size() < 2) {
            continue;
        }

        for (int i = 1; i < tracePoints.size(); ++i) {
            painter.setPen(QPen(traceColors.at(i),
                                1.1 + static_cast<qreal>(i) / tracePoints.size() * 0.9,
                                Qt::SolidLine,
                                Qt::RoundCap,
                                Qt::RoundJoin));
            painter.drawLine(tracePoints.at(i - 1), tracePoints.at(i));
        }

        for (int i = 0; i < tracePoints.size(); ++i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(traceColors.at(i));
            const qreal radius = 1.4 + static_cast<qreal>(i) / tracePoints.size() * 1.6;
            painter.drawEllipse(tracePoints.at(i), radius, radius);
        }
    }

    for (int i = 1; i < anchorPoints.size(); ++i) {
        const QColor lineColor = ageColor(QColor(90, 176, 255), i, anchorPoints.size(), 0.24, 0.95);
        painter.setPen(QPen(lineColor,
                            2.0 + static_cast<qreal>(i) / anchorPoints.size() * 2.0,
                            Qt::SolidLine,
                            Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawLine(anchorPoints.at(i - 1), anchorPoints.at(i));
    }

    for (int i = 0; i < anchorPoints.size(); ++i) {
        const QColor pointColor = ageColor(QColor(115, 196, 255), i, anchorPoints.size(), 0.25, 1.0);
        painter.setPen(QPen(QColor(241, 249, 255, 180), 1.0));
        painter.setBrush(pointColor);
        const qreal pointDenominator = std::max<qreal>(1.0, static_cast<qreal>(anchorPoints.size() - 1));
        const qreal radius = 2.4 + static_cast<qreal>(i) / pointDenominator * 2.0;
        painter.drawEllipse(anchorPoints.at(i), radius, radius);
    }

    const QPointF latest = anchorPoints.last();
    painter.setPen(QPen(QColor(248, 252, 255, 220), 1.4));
    painter.setBrush(QColor(110, 202, 255, 220));
    painter.drawEllipse(latest, 5.2, 5.2);

    QFont labelFont = painter.font();
    labelFont.setPixelSize(11);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.setPen(QColor(194, 211, 233, 190));
    painter.drawText(contentRect.adjusted(2.0, 2.0, -2.0, -2.0),
                     Qt::AlignLeft | Qt::AlignTop,
                     fieldMode ? QStringLiteral("全场轨迹")
                               : QStringLiteral("最近6秒"));
}

void TrajectoryWidget::appendSample(const PoseFrameResult &frame)
{
    const PoseInstance *person = primaryPerson(frame);
    if (!person) {
        clearPoseFrame();
        return;
    }

    const qint64 timestampMs = frame.timestampMs > 0 ? frame.timestampMs : (m_samples.isEmpty() ? 0 : m_samples.last().timestampMs);
    if (!m_samples.isEmpty() && timestampMs > 0 && timestampMs - m_samples.last().timestampMs < kMinSampleIntervalMs) {
        return;
    }

    TrajectorySample sample;
    sample.timestampMs = timestampMs;
    sample.cameraId = frame.cameraId;
    sample.anchorImagePoint = anchorPointFor(*person);
    const CameraSegment *segment = segmentForCamera(m_cameraSegments, frame.cameraId);
    if (segment && frame.frameSize.width() > 1.0 && frame.frameSize.height() > 1.0) {
        const qreal nx = std::clamp(sample.anchorImagePoint.x() / frame.frameSize.width(), 0.0, 1.0);
        const qreal ny = std::clamp(sample.anchorImagePoint.y() / frame.frameSize.height(), 0.0, 1.0);
        const qreal fieldDistance = segment->fieldStartM
                                    + (1.0 - ny) * (segment->fieldEndM - segment->fieldStartM);
        const qreal lateral = segment->lateralOffsetM + (nx - 0.5) * kDefaultFieldWidthM;
        sample.fieldPoint = QPointF(fieldDistance, lateral);
        sample.hasFieldPoint = true;
    }
    sample.keypoints.reserve(static_cast<qsizetype>(kTrackedKeypointIndices.size()));

    QVector3D rootPoint3d;
    const bool hasRoot3d = rootPoint3dFor(*person, &rootPoint3d);
    for (const int keypointIndex : kTrackedKeypointIndices) {
        KeypointTrace trace;
        const PoseKeypoint *keypoint = keypointAt(*person, keypointIndex);
        if (keypoint) {
            trace.valid = true;
            trace.point2d = keypoint->imagePoint - sample.anchorImagePoint;
            trace.has3d = hasRoot3d && keypoint->hasPoint3d;
            if (trace.has3d) {
                trace.offset3d = keypoint->point3d - rootPoint3d;
            }
        }
        sample.keypoints.push_back(trace);
    }

    m_samples.push_back(sample);
    trimHistory(sample.timestampMs);
}

void TrajectoryWidget::trimHistory(qint64 latestTimestampMs)
{
    if (m_samples.isEmpty()) {
        return;
    }

    const bool fieldMode = hasFieldSamples(m_samples);
    if (latestTimestampMs <= 0) {
        const int maxSamples = fieldMode ? kMaxFieldSamples : 60;
        while (m_samples.size() > maxSamples) {
            m_samples.removeFirst();
        }
        return;
    }

    const qint64 cutoff = latestTimestampMs - (fieldMode ? kFieldHistoryWindowMs : kHistoryWindowMs);
    while (!m_samples.isEmpty() && m_samples.first().timestampMs < cutoff) {
        m_samples.removeFirst();
    }
    while (fieldMode && m_samples.size() > kMaxFieldSamples) {
        m_samples.removeFirst();
    }
}
