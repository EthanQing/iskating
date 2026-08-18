#ifndef ATHLETETRACKER_H
#define ATHLETETRACKER_H

#include <QHash>
#include <QRectF>
#include <QString>
#include <QVector>

struct AthleteTrackState
{
    int trackId = -1;
    QRectF box;
    qint64 lastSeenMs = 0;
    int hits = 0;
    QString athleteId;
    QString participantId;
    QString label;
    QString identityStatus = QStringLiteral("unknown");
    QString identitySource;
    float identityConfidence = 0.0f;
    float reidSimilarity = 0.0f;
    int reidAttempts = 0;
    qint64 lastReidAttemptMs = 0;
};

struct AthleteTrackReidPolicy
{
    int minTrackHits = 2;
    int maxAttempts = 3;
    qint64 retryIntervalMs = 1000;
};

bool shouldAttemptAthleteTrackReid(const AthleteTrackState &track,
                                   const AthleteTrackReidPolicy &policy,
                                   qint64 timestampMs);

class AthleteTracker
{
public:
    void reset();
    void setTrackTtlMs(qint64 ttlMs);
    void setIouThreshold(float threshold);
    QVector<int> update(int cameraId, const QVector<QRectF> &boxes, qint64 timestampMs);
    AthleteTrackState *track(int cameraId, int trackId);
    const AthleteTrackState *track(int cameraId, int trackId) const;

private:
    QHash<int, QHash<int, AthleteTrackState>> m_tracks;
    qint64 m_trackTtlMs = 1200;
    float m_iouThreshold = 0.20f;
    int m_nextTrackId = 1;
};

#endif // ATHLETETRACKER_H
