#ifndef ATHLETEANALYSISRESULT_H
#define ATHLETEANALYSISRESULT_H

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

struct AthleteGalleryEntry
{
    QString athleteId;
    QString participantId;
    QString label;
    QVector<float> embedding;
};

struct AthleteIdentityBinding
{
    int cameraId = 0;
    int trackId = -1;
    QString participantId;
    QString athleteId;
    QString label;
};

struct AthleteInstance
{
    int trackId = -1;
    int classId = -1;
    QString athleteId;
    QString participantId;
    QString label;
    QString identityStatus = QStringLiteral("unknown");
    QString identitySource;
    QRectF box;
    float detectionConfidence = 0.0f;
    float identityConfidence = 0.0f;
    float reidSimilarity = 0.0f;
};

struct AthleteFrameResult
{
    int cameraId = 0;
    qint64 timestampMs = 0;
    QSizeF frameSize;
    QVector<AthleteInstance> instances;
    QString sourceName;
};

#endif // ATHLETEANALYSISRESULT_H
