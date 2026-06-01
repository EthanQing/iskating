#ifndef TRAJECTORYWIDGET_H
#define TRAJECTORYWIDGET_H

#include "poseresult.h"

#include <QPointF>
#include <QString>
#include <QVector>
#include <QVector3D>
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
    void setPoseFrame(const PoseFrameResult &frame);
    void clearPoseFrame();

    struct KeypointTrace
    {
        QPointF point2d;
        QVector3D offset3d;
        bool valid = false;
        bool has3d = false;
    };

    struct TrajectorySample
    {
        qint64 timestampMs = 0;
        int cameraId = 0;
        QPointF anchorImagePoint;
        QPointF fieldPoint;
        bool hasFieldPoint = false;
        QVector<KeypointTrace> keypoints;
    };

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void appendSample(const PoseFrameResult &frame);
    void trimHistory(qint64 latestTimestampMs);

    QVector<TrajectorySample> m_samples;
    QVector<CameraSegment> m_cameraSegments;
};

#endif // TRAJECTORYWIDGET_H
