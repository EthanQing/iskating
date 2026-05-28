#include "skeletonviewwidget.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QRectF>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace {

QColor colorForInstance(const PoseInstance &instance)
{
    switch (instance.kind) {
    case PoseInstanceKind::LeftHand:
        return QColor(79, 140, 255);
    case PoseInstanceKind::RightHand:
        return QColor(34, 216, 137);
    case PoseInstanceKind::Person:
        return QColor(112, 204, 255);
    case PoseInstanceKind::Unknown:
    default:
        return QColor(255, 190, 69);
    }
}

QRectF keypointBounds(const QVector<PoseKeypoint> &keypoints)
{
    bool hasPoint = false;
    qreal minX = 0.0;
    qreal maxX = minX;
    qreal minY = 0.0;
    qreal maxY = minY;
    for (const PoseKeypoint &keypoint : keypoints) {
        if (!keypoint.valid) {
            continue;
        }
        const QPointF point = keypoint.imagePoint;
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
    if (bounds.width() < 1.0) {
        bounds.adjust(-0.5, 0.0, 0.5, 0.0);
    }
    if (bounds.height() < 1.0) {
        bounds.adjust(0.0, -0.5, 0.0, 0.5);
    }
    return bounds;
}

QPointF projectedPoint(const QPointF &point, const QRectF &sourceBounds, const QRectF &targetRect)
{
    const QPointF center = sourceBounds.center();
    const qreal sourceSpan = std::max(sourceBounds.width(), sourceBounds.height());
    const qreal targetSpan = std::min(targetRect.width(), targetRect.height()) * 0.62;
    const qreal scale = targetSpan / std::max<qreal>(1.0, sourceSpan);
    const qreal nx = (point.x() - center.x()) * scale;
    const qreal ny = (point.y() - center.y()) * scale;

    const qreal normalizedY = (point.y() - sourceBounds.top()) / std::max<qreal>(1.0, sourceBounds.height());
    const qreal depth = (0.5 - normalizedY) * targetSpan * 0.24;
    return QPointF(targetRect.center().x() + nx + depth * 0.40,
                   targetRect.center().y() + ny - depth * 0.28);
}

void drawStage(QPainter *painter, const QRectF &rect)
{
    QLinearGradient background(rect.topLeft(), rect.bottomRight());
    background.setColorAt(0.0, QColor(15, 20, 29));
    background.setColorAt(1.0, QColor(9, 12, 18));
    painter->fillRect(rect, background);

    const QRectF floor = rect.adjusted(rect.width() * 0.12,
                                       rect.height() * 0.52,
                                       -rect.width() * 0.12,
                                       -rect.height() * 0.10);
    QPainterPath floorPath;
    floorPath.moveTo(floor.left() + floor.width() * 0.18, floor.top());
    floorPath.lineTo(floor.right() - floor.width() * 0.18, floor.top());
    floorPath.lineTo(floor.right(), floor.bottom());
    floorPath.lineTo(floor.left(), floor.bottom());
    floorPath.closeSubpath();

    painter->setPen(QPen(QColor(48, 63, 86, 150), 1.0));
    painter->setBrush(QColor(18, 25, 37, 90));
    painter->drawPath(floorPath);

    for (int i = 1; i < 6; ++i) {
        const qreal t = i / 6.0;
        const QPointF left(floor.left() * (1.0 - t) + (floor.left() + floor.width() * 0.18) * t,
                           floor.bottom() * (1.0 - t) + floor.top() * t);
        const QPointF right(floor.right() * (1.0 - t) + (floor.right() - floor.width() * 0.18) * t,
                            floor.bottom() * (1.0 - t) + floor.top() * t);
        painter->drawLine(left, right);
    }

    for (int i = 1; i < 5; ++i) {
        const qreal t = i / 5.0;
        painter->drawLine(QPointF(floor.left() + floor.width() * t, floor.bottom()),
                          QPointF(floor.left() + floor.width() * (0.18 + 0.64 * t), floor.top()));
    }
}

void drawEmptyPose(QPainter *painter, const QRectF &rect)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QPointF center = rect.center();
    const qreal scale = std::min(rect.width(), rect.height()) * 0.28;
    const QColor color(88, 110, 143, 135);
    painter->setPen(QPen(color, std::max<qreal>(2.0, scale * 0.025), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);

    const QPointF wrist(center.x(), center.y() + scale * 0.56);
    const QPointF palm(center.x(), center.y() + scale * 0.10);
    painter->drawLine(wrist, palm);
    for (int finger = -2; finger <= 2; ++finger) {
        const qreal xOffset = finger * scale * 0.14;
        const QPointF base(palm.x() + xOffset, palm.y() - std::abs(finger) * scale * 0.03);
        const QPointF tip(base.x() + finger * scale * 0.05, base.y() - scale * (0.58 - std::abs(finger) * 0.05));
        painter->drawLine(palm, base);
        painter->drawLine(base, tip);
    }

    painter->restore();
}

} // namespace

SkeletonViewWidget::SkeletonViewWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("skeletonViewWidget"));
    setMinimumSize(120, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void SkeletonViewWidget::setPoseFrame(const PoseFrameResult &frame)
{
    m_frame = frame;
    update();
}

void SkeletonViewWidget::clearPoseFrame()
{
    m_frame = {};
    update();
}

void SkeletonViewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF viewRect = rect();
    drawStage(&painter, viewRect);

    const QRectF contentRect = viewRect.adjusted(12.0, 10.0, -12.0, -12.0);
    if (m_frame.instances.isEmpty()) {
        drawEmptyPose(&painter, contentRect);
        return;
    }

    QVector<PoseInstance> drawableInstances;
    drawableInstances.reserve(std::min<qsizetype>(2, m_frame.instances.size()));
    for (const PoseInstance &instance : m_frame.instances) {
        if (!instance.keypoints.isEmpty()) {
            drawableInstances.push_back(instance);
            if (drawableInstances.size() == 2) {
                break;
            }
        }
    }

    if (drawableInstances.isEmpty()) {
        drawEmptyPose(&painter, contentRect);
        return;
    }

    const qreal laneWidth = contentRect.width() / drawableInstances.size();
    for (int instanceIndex = 0; instanceIndex < drawableInstances.size(); ++instanceIndex) {
        const PoseInstance &instance = drawableInstances.at(instanceIndex);
        QRectF lane(contentRect.left() + instanceIndex * laneWidth,
                    contentRect.top(),
                    laneWidth,
                    contentRect.height());
        lane.adjust(8.0, 4.0, -8.0, -4.0);

        const QRectF bounds = keypointBounds(instance.keypoints);
        if (!bounds.isValid()) {
            continue;
        }

        const QColor baseColor = colorForInstance(instance);
        QVector<QPointF> points;
        points.resize(instance.keypoints.size());
        QVector<bool> valid;
        valid.resize(instance.keypoints.size());
        for (int i = 0; i < instance.keypoints.size(); ++i) {
            const PoseKeypoint &keypoint = instance.keypoints.at(i);
            valid[i] = keypoint.valid;
            if (keypoint.valid) {
                points[i] = projectedPoint(keypoint.imagePoint, bounds, lane);
            }
        }

        QPen bonePen(baseColor, std::max<qreal>(2.2, std::min(width(), height()) * 0.012));
        bonePen.setCapStyle(Qt::RoundCap);
        bonePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(bonePen);
        for (const QPair<int, int> &bone : poseSkeletonBones(instance.skeletonType)) {
            if (bone.first < 0 || bone.second < 0
                || bone.first >= points.size() || bone.second >= points.size()
                || !valid.at(bone.first) || !valid.at(bone.second)) {
                continue;
            }
            painter.drawLine(points.at(bone.first), points.at(bone.second));
        }

        const qreal jointRadius = std::max<qreal>(2.8, std::min(width(), height()) * 0.016);
        painter.setPen(QPen(QColor(244, 250, 255, 220), 1.0));
        painter.setBrush(QColor(baseColor.red(), baseColor.green(), baseColor.blue(), 230));
        for (int i = 0; i < points.size(); ++i) {
            if (valid.at(i)) {
                painter.drawEllipse(points.at(i), jointRadius, jointRadius);
            }
        }

        const QString confidence = QString::number(std::round(instance.confidence * 100.0f) / 100.0f, 'f', 2);
        QFont labelFont = painter.font();
        labelFont.setPixelSize(11);
        labelFont.setBold(true);
        painter.setFont(labelFont);
        painter.setPen(QColor(206, 222, 244, 185));
        painter.drawText(lane.adjusted(4.0, 4.0, -4.0, -4.0),
                         Qt::AlignLeft | Qt::AlignTop,
                         confidence);
    }
}
