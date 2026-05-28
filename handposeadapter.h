#ifndef HANDPOSEADAPTER_H
#define HANDPOSEADAPTER_H

#include "handposeresult.h"
#include "poseresult.h"

#include <QVector>

PoseFrameResult handPoseResultsToPoseFrame(const QVector<HandPoseResult> &hands);

#endif // HANDPOSEADAPTER_H
