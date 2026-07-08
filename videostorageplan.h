#ifndef VIDEOSTORAGEPLAN_H
#define VIDEOSTORAGEPLAN_H

#include "trainingdomain.h"

#include <QDateTime>
#include <QString>

struct VideoStoragePlanInput
{
    QString sessionId;
    QString athleteId;
    QString athleteName;
    QDateTime startedAt;
    int camera = 0;
    QString cameraName;
    QString sourceUrl;
    QString fallbackUrl;
    bool externalFile = false;
    int durationSec = 0;
};

TrainingVideoFile buildTrainingVideoFilePlan(const VideoStoragePlanInput &input);

#endif // VIDEOSTORAGEPLAN_H
