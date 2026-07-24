#ifndef TRAJECTORYWIDGET_H
#define TRAJECTORYWIDGET_H

#include "trainingdomain.h"

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;

class TrajectoryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrajectoryWidget(QWidget *parent = nullptr);

    struct CameraSegment
    {
        int cameraId = 0;
        bool enabled = true;
        QString role;
        double fieldStartM = 0.0;
        double fieldEndM = 0.0;
        double lateralOffsetM = 0.0;
    };

    void setCameraSegments(const QVector<CameraSegment> &segments);
    void setTrackPoints(const QVector<TrackPoint> &points);

    struct TrajectorySample
    {
        qint64 timestampMs = 0;
        int cameraId = 0;
        QPointF fieldPoint;
        bool hasFieldPoint = false;
    };

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void trimHistory(qint64 latestTimestampMs);

    QVector<TrajectorySample> m_samples;
    QVector<CameraSegment> m_cameraSegments;
};

#endif // TRAJECTORYWIDGET_H
