#include "trajectorywidget.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QRectF>
#include <QSizePolicy>

#include <algorithm>

namespace {

constexpr qint64 kHistoryWindowMs = 6000;
constexpr qint64 kFieldHistoryWindowMs = 600000;
constexpr int kMaxFieldSamples = 5000;
constexpr qreal kDefaultFieldWidthM = 12.0;

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

QPointF mapFieldToView(const QPointF &fieldPoint, const QRectF &bounds, const QRectF &viewRect)
{
    const qreal nx = (fieldPoint.x() - bounds.left()) / std::max<qreal>(1.0, bounds.width());
    const qreal ny = (fieldPoint.y() - bounds.top()) / std::max<qreal>(1.0, bounds.height());
    return QPointF(viewRect.left() + std::clamp(nx, 0.0, 1.0) * viewRect.width(),
                   viewRect.bottom() - std::clamp(ny, 0.0, 1.0) * viewRect.height());
}

void drawEmptyState(QPainter *painter, const QRectF &rect, bool hasSamples)
{
    painter->save();
    QFont titleFont = painter->font();
    titleFont.setPixelSize(16);
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(QColor(QStringLiteral("#F4F7FA")));
    const QRectF titleRect = rect.adjusted(8, 0, -8, 0);
    const int titleHeight = titleFont.pixelSize() + 4;
    const int hintHeight = 13 * 2 + 10;
    const int contentHeight = titleHeight + 4 + hintHeight;
    const int titleTop = std::max(static_cast<int>(rect.top()),
                                  static_cast<int>(rect.center().y()) - contentHeight / 2);
    painter->drawText(QRectF(titleRect.left(), titleTop, titleRect.width(), titleHeight),
                      Qt::AlignHCenter | Qt::AlignVCenter,
                      hasSamples ? QStringLiteral("等待场地标定")
                                 : QStringLiteral("暂无轨迹数据"));

    QFont hintFont = painter->font();
    hintFont.setPixelSize(13);
    hintFont.setWeight(QFont::Normal);
    painter->setFont(hintFont);
    painter->setPen(QColor(QStringLiteral("#B6C2D0")));
    const QRectF hintRect = rect.adjusted(8, 0, -8, 0);
    const int hintTop = titleTop + titleHeight + 4;
    const QString hint = hasSamples
                             ? QStringLiteral("已收到运动员位置，\n完成场地标定后将显示米制轨迹。")
                             : QStringLiteral("运动员识别并完成场地标定后，\n轨迹将在这里实时显示。");
    painter->drawText(QRectF(hintRect.left(), hintTop, hintRect.width(), hintHeight),
                      Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                      hint);
    painter->restore();
}

} // namespace

TrajectoryWidget::TrajectoryWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("trajectoryWidget"));
    setProperty("trajectoryView", true);
    setMinimumSize(180, 100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void TrajectoryWidget::setCameraSegments(const QVector<CameraSegment> &segments)
{
    m_cameraSegments = segments;
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
    if (!m_samples.isEmpty()) {
        trimHistory(m_samples.last().timestampMs);
    }
    update();
}

void TrajectoryWidget::setAwaitingCalibration(bool awaiting)
{
    if (m_awaitingCalibration == awaiting) {
        return;
    }
    m_awaitingCalibration = awaiting;
    update();
}

void TrajectoryWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF viewRect = rect();
    painter.fillRect(viewRect, QColor(QStringLiteral("#111A24")));

    const bool fieldMode = hasFieldSamples(m_samples);
    const QRectF bounds = fieldBounds(m_cameraSegments, m_samples);
    const QRectF plotRect = viewRect.adjusted(12.0, 8.0, -12.0, -22.0);
    const QRectF trackRect = plotRect.adjusted(3.0, 17.0, -3.0, -8.0);
    if (trackRect.width() <= 0.0 || trackRect.height() <= 0.0) {
        return;
    }

    QPainterPath icePath;
    icePath.addRoundedRect(trackRect, trackRect.height() * 0.5, trackRect.height() * 0.5);
    painter.setPen(QPen(QColor(QStringLiteral("#31445A")), 1.5));
    painter.setBrush(QColor(QStringLiteral("#162230")));
    painter.drawPath(icePath);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(QStringLiteral("#243244")), 1.0));
    for (qreal inset : {7.0, 14.0}) {
        const QRectF laneRect = trackRect.adjusted(inset, inset, -inset, -inset);
        if (laneRect.width() > 0.0 && laneRect.height() > 0.0) {
            QPainterPath lanePath;
            lanePath.addRoundedRect(laneRect, laneRect.height() * 0.5, laneRect.height() * 0.5);
            painter.drawPath(lanePath);
        }
    }

    if (bounds.isValid()) {
        QFont segmentFont = painter.font();
        segmentFont.setPixelSize(10);
        painter.setFont(segmentFont);
        const QFontMetrics metrics(segmentFont);
        qreal lastLabelRight = plotRect.left() - 4.0;
        painter.setPen(QPen(QColor(QStringLiteral("#7E8FA3")), 1.0));
        for (const CameraSegment &segment : m_cameraSegments) {
            if (!segment.enabled || segment.fieldEndM <= segment.fieldStartM) {
                continue;
            }
            const qreal x0 = mapFieldToView(QPointF(segment.fieldStartM, bounds.center().y()),
                                            bounds,
                                            plotRect).x();
            const qreal x1 = mapFieldToView(QPointF(segment.fieldEndM, bounds.center().y()),
                                            bounds,
                                            plotRect).x();
            painter.drawLine(QPointF(x0, trackRect.top() - 3.0), QPointF(x0, trackRect.top() + 4.0));
            painter.drawLine(QPointF(x1, trackRect.top() - 3.0), QPointF(x1, trackRect.top() + 4.0));
            const QString label = QStringLiteral("CAM %1").arg(segment.cameraId, 2, 10, QLatin1Char('0'));
            const qreal labelLeft = std::min(x0, x1) + 3.0;
            const qreal labelWidth = metrics.horizontalAdvance(label);
            if (labelLeft > lastLabelRight + 4.0 && labelLeft + labelWidth <= plotRect.right()) {
                painter.drawText(QPointF(labelLeft, trackRect.top() - 5.0), label);
                lastLabelRight = labelLeft + labelWidth;
            }
        }

        QFont axisFont = painter.font();
        axisFont.setPixelSize(10);
        painter.setFont(axisFont);
        painter.setPen(QColor(QStringLiteral("#7E8FA3")));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 4.0, plotRect.width() * 0.5, 13.0),
                         Qt::AlignLeft | Qt::AlignTop,
                         QStringLiteral("%1 m").arg(bounds.left(), 0, 'f', 1));
        painter.drawText(QRectF(plotRect.center().x(), plotRect.bottom() + 4.0,
                                plotRect.width() * 0.5, 13.0),
                         Qt::AlignRight | Qt::AlignTop,
                         QStringLiteral("%1 m").arg(bounds.right(), 0, 'f', 1));
    }

    if (!fieldMode || !bounds.isValid()) {
        drawEmptyState(&painter, trackRect, m_awaitingCalibration || !m_samples.isEmpty());
        return;
    }

    QVector<QPointF> samples;
    samples.reserve(m_samples.size());
    for (const TrajectorySample &sample : m_samples) {
        if (sample.hasFieldPoint) {
            samples.append(mapFieldToView(sample.fieldPoint, bounds, plotRect));
        }
    }

    if (samples.isEmpty()) {
        drawEmptyState(&painter, trackRect, true);
        return;
    }

    painter.setPen(QPen(QColor(QStringLiteral("#38BDF8")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (int index = 1; index < samples.size(); ++index) {
        painter.drawLine(samples.at(index - 1), samples.at(index));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#38BDF8")));
    for (const QPointF &sample : samples) {
        painter.drawEllipse(sample, 2.5, 2.5);
    }
    painter.setBrush(QColor(QStringLiteral("#F4F7FA")));
    painter.drawEllipse(samples.last(), 4.0, 4.0);
    painter.setBrush(QColor(QStringLiteral("#38BDF8")));
    painter.drawEllipse(samples.last(), 2.5, 2.5);

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
