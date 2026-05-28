#ifndef TENSORRTRTMW3DBACKEND_H
#define TENSORRTRTMW3DBACKEND_H

#include "poseresult.h"

#include <QImage>
#include <QMutex>
#include <QString>

#include <memory>

class TensorRtRunner;

class TensorRtRtmw3dBackend
{
public:
    TensorRtRtmw3dBackend();
    ~TensorRtRtmw3dBackend();

    bool initialize(const QString &modelDir, QString *error);
    bool isReady() const;
    QString statusText() const;

    bool infer(const QImage &rgbFrame, PoseFrameResult *frame, QString *error);

private:
    mutable QMutex m_mutex;
    std::unique_ptr<TensorRtRunner> m_runner;
    bool m_ready = false;
    QString m_statusText = QStringLiteral("RTMW3D模型未加载");
};

#endif // TENSORRTRTMW3DBACKEND_H
