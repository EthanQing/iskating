#ifndef HANDPOSERESULT_H
#define HANDPOSERESULT_H

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

enum class Handedness {
    Left,
    Right,
    Unknown,
};

struct HandPoseResult
{
    int cameraId = 0;
    qint64 timestampMs = 0;
    QVector<QPointF> landmarks;
    QRectF handBox;
    float confidence = 0.0f;
    Handedness handedness = Handedness::Unknown;
    QSizeF frameSize;
};

#endif // HANDPOSERESULT_H
