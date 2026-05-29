#ifndef TRAJECTORYWIDGET_H
#define TRAJECTORYWIDGET_H

#include "poseresult.h"

#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <QWidget>

class QPaintEvent;

class TrajectoryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrajectoryWidget(QWidget *parent = nullptr);

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
        QPointF anchorImagePoint;
        QVector<KeypointTrace> keypoints;
    };

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void appendSample(const PoseFrameResult &frame);
    void trimHistory(qint64 latestTimestampMs);

    QVector<TrajectorySample> m_samples;
};

#endif // TRAJECTORYWIDGET_H
