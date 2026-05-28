#ifndef TENSORRTBODYPOSEBACKEND_H
#define TENSORRTBODYPOSEBACKEND_H

#include "poseresult.h"
#include "tensorrtrtmw3dbackend.h"

#include <QImage>
#include <QMutex>
#include <QString>

#include <memory>

class TensorRtRunner;

class TensorRtBodyPoseBackend
{
public:
    TensorRtBodyPoseBackend();
    ~TensorRtBodyPoseBackend();

    bool initialize(const QString &modelDir, QString *error);
    bool isReady() const;
    QString statusText() const;
    PoseFrameResult infer(const QImage &rgbFrame, int cameraId, qint64 timestampMs);

private:
    mutable QMutex m_mutex;
    std::unique_ptr<TensorRtRunner> m_runner;
    TensorRtRtmw3dBackend m_rtmw3dBackend;
    bool m_ready = false;
    bool m_rtmw3dReady = false;
    QString m_statusText = QStringLiteral("人体姿态模型未加载");
};

#endif // TENSORRTBODYPOSEBACKEND_H
