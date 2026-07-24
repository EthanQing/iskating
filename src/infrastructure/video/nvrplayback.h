#ifndef NVRPLAYBACK_H
#define NVRPLAYBACK_H

#include "systemsettingsdialog.h"
#include "trainingdomain.h"

#include <QString>
#include <QVector>

struct NvrPlaybackResult
{
    QString url;
    QString error;
    int startOffsetMs = 0;
    int endOffsetMs = 0;
};

NvrPlaybackResult buildNvrPlaybackUrl(const SharedCameraSettings &sharedSettings,
                                      const QVector<CameraSlotSettings> &cameraSlotSettings,
                                      const SessionHistoryItem &record,
                                      int clipStartMs = -1,
                                      int clipEndMs = -1);

#endif // NVRPLAYBACK_H
