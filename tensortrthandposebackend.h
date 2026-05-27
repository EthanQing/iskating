#ifndef TENSORRTHANDPOSEBACKEND_H
#define TENSORRTHANDPOSEBACKEND_H

#include "handposeresult.h"

#include <QImage>
#include <QMutex>
#include <QString>

#include <memory>
#include <vector>

class TensorRtRunner;

class TensorRtHandPoseBackend
{
public:
    TensorRtHandPoseBackend();
    ~TensorRtHandPoseBackend();

    bool initialize(const QString &modelDir, QString *error);
    bool isReady() const;
    QString statusText() const;
    QVector<HandPoseResult> infer(const QImage &rgbFrame, int cameraId, qint64 timestampMs);

private:
    mutable QMutex m_mutex;
    std::unique_ptr<TensorRtRunner> m_palmRunner;
    std::unique_ptr<TensorRtRunner> m_landmarkRunner;
    bool m_ready = false;
    QString m_statusText = QStringLiteral("手部模型未加载");
};

#endif // TENSORRTHANDPOSEBACKEND_H
