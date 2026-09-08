#include "trajectorywidget.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
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
    titleFont.setPixelSize(14);
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(QColor(QStringLiteral("#F4F7FA")));
    const QRectF titleRect = rect.adjusted(8, 0, -8, 0);
    const int titleHeight = titleFont.pixelSize() + 4;
    const int titleTop = std::max(static_cast<int>(rect.top()),
                                  static_cast<int>(rect.center().y()) - titleHeight);
    painter->drawText(QRectF(titleRect.left(), titleTop, titleRect.width(), titleHeight),
                      Qt::AlignHCenter | Qt::AlignVCenter,
                      hasSamples ? QStringLiteral("等待场地标定")
                                 : QStringLiteral("等待二维轨迹数据"));

    QFont hintFont = painter->font();
    hintFont.setPixelSize(11);
    hintFont.setWeight(QFont::Normal);
    painter->setFont(hintFont);
    painter->setPen(QColor(QStringLiteral("#A2AFBF")));
    const QRectF hintRect = rect.adjusted(8, 0, -8, 0);
    const int hintHeight = hintFont.pixelSize() + 4;
    const int hintTop = std::min(static_cast<int>(rect.bottom()) - hintHeight,
                                 titleTop + titleHeight + 4);
    const QString hint = hasSamples ? QStringLiteral("需要运动员身份与场地标定")
                                    : QStringLiteral("需要已识别运动员与场地标定");
    painter->drawText(QRectF(hintRect.left(), hintTop, hintRect.width(), hintHeight),
                      Qt::AlignHCenter | Qt::AlignVCenter,
                      QFontMetrics(hintFont).elidedText(hint,
                                                        Qt::ElideRight,
                                                        static_cast<int>(hintRect.width())));
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

void TrajectoryWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF viewRect = rect();
    painter.fillRect(viewRect, QColor(QStringLiteral("#101720")));

    const bool fieldMode = hasFieldSamples(m_samples);
    const QRectF bounds = fieldMode ? fieldBounds(m_cameraSegments, m_samples) : QRectF();
    const QRectF plotRect = viewRect.adjusted(28.0, 22.0, -20.0, -24.0);
    if (!fieldMode || !bounds.isValid() || plotRect.width() <= 0.0 || plotRect.height() <= 0.0) {
        drawEmptyState(&painter, viewRect, !m_samples.isEmpty());
        return;
    }

    painter.fillRect(plotRect, QColor(QStringLiteral("#0C1118")));
    painter.setPen(QPen(QColor(QStringLiteral("#202C3A")), 1.0));
    for (int column = 0; column <= 8; ++column) {
        const qreal x = plotRect.left() + plotRect.width() * column / 8.0;
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
    }
    for (int row = 0; row <= 4; ++row) {
        const qreal y = plotRect.top() + plotRect.height() * row / 4.0;
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    const QVector<QColor> segmentColors = {
        QColor(56, 189, 248, 22),
        QColor(50, 213, 131, 18),
        QColor(253, 176, 34, 18),
    };
    QFont segmentFont = painter.font();
    segmentFont.setPixelSize(9);
    painter.setFont(segmentFont);
    for (int index = 0; index < m_cameraSegments.size(); ++index) {
        const CameraSegment &segment = m_cameraSegments.at(index);
        if (!segment.enabled || segment.fieldEndM <= segment.fieldStartM) {
            continue;
        }
        const qreal x0 = mapFieldToView(QPointF(segment.fieldStartM, bounds.center().y()), bounds, plotRect).x();
        const qreal x1 = mapFieldToView(QPointF(segment.fieldEndM, bounds.center().y()), bounds, plotRect).x();
        QRectF band(QPointF(std::min(x0, x1), plotRect.top()),
                    QPointF(std::max(x0, x1), plotRect.bottom()));
        if (band.width() < 2.0) {
            continue;
        }
        painter.fillRect(band, segmentColors.at(index % segmentColors.size()));
        painter.setPen(QColor(QStringLiteral("#A2AFBF")));
        painter.drawText(band.adjusted(4.0, 3.0, -4.0, -3.0),
                         Qt::AlignLeft | Qt::AlignTop,
                         QStringLiteral("CAM %1").arg(segment.cameraId, 2, 10, QLatin1Char('0')));
    }

    painter.setPen(QPen(QColor(QStringLiteral("#2D4054")), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plotRect);

    QFont axisFont = painter.font();
    axisFont.setPixelSize(8);
    axisFont.setWeight(QFont::Normal);
    painter.setFont(axisFont);
    painter.setPen(QColor(QStringLiteral("#667586")));
    const QString startLabel = QStringLiteral("%1 m").arg(bounds.left(), 0, 'f', 1);
    const QString endLabel = QStringLiteral("%1 m").arg(bounds.right(), 0, 'f', 1);
    painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 4.0, plotRect.width() * 0.5, 14.0),
                     Qt::AlignLeft | Qt::AlignTop,
                     startLabel);
    painter.drawText(QRectF(plotRect.left() + plotRect.width() * 0.5,
                            plotRect.bottom() + 4.0,
                            plotRect.width() * 0.5,
                            14.0),
                     Qt::AlignRight | Qt::AlignTop,
                     endLabel);

    QVector<QPointF> samples;
    samples.reserve(m_samples.size());
    for (const TrajectorySample &sample : m_samples) {
        if (sample.hasFieldPoint) {
            samples.append(mapFieldToView(sample.fieldPoint, bounds, plotRect));
        }
    }

    if (samples.isEmpty()) {
        drawEmptyState(&painter, plotRect, true);
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

    QFont labelFont = painter.font();
    labelFont.setPixelSize(10);
    labelFont.setWeight(QFont::DemiBold);
    painter.setFont(labelFont);
    painter.setPen(QColor(QStringLiteral("#A2AFBF")));
    painter.drawText(viewRect.adjusted(28.0, 7.0, -20.0, 0.0),
                     Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("二维滑行轨迹"));
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
