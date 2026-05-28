#ifndef SKELETONVIEWWIDGET_H
#define SKELETONVIEWWIDGET_H

#include "poseresult.h"

#include <QWidget>
#include <QVector>

class SkeletonViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SkeletonViewWidget(QWidget *parent = nullptr);

    void setPoseFrame(const PoseFrameResult &frame);
    void clearPoseFrame();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    PoseFrameResult m_frame;
};

#endif // SKELETONVIEWWIDGET_H
