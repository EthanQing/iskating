#ifndef TENSORRTATHLETEBACKEND_H
#define TENSORRTATHLETEBACKEND_H

#include "athleteanalysisresult.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class TensorRtRunner;

class TensorRtAthleteBackend
{
public:
    using StatusCallback = std::function<void(const QString &)>;

    TensorRtAthleteBackend();
    ~TensorRtAthleteBackend();

    void setStatusCallback(StatusCallback callback);
    void setGallery(const QVector<AthleteGalleryEntry> &gallery);
    void setManualBindings(const QVector<AthleteIdentityBinding> &bindings);
    void resetTracking();
    bool initialize(const QString &modelDir, QString *error);
    bool isReady() const;
    bool hasReid() const;
    QString statusText() const;
    AthleteFrameResult infer(const QImage &rgbFrame, int cameraId, qint64 timestampMs);
    QVector<float> extractEmbedding(const QImage &rgbImage, QString *error = nullptr);

private:
    struct Detection
    {
        QRectF box;
        float score = 0.0f;
        int classId = -1;
    };

    struct TrackState
    {
        int trackId = -1;
        QRectF box;
        qint64 lastSeenMs = 0;
        QString athleteId;
        QString participantId;
        QString label;
    };

    QVector<Detection> detect(const QImage &rgbFrame, QString *error);
    QVector<float> embeddingFor(const QImage &rgbFrame, const QRectF &box, QString *error);
    void updateTrack(AthleteInstance *instance, int cameraId, qint64 timestampMs);
    void setStatus(const QString &status, bool force = false);

    mutable QMutex m_mutex;
    std::unique_ptr<TensorRtRunner> m_detector;
    std::unique_ptr<TensorRtRunner> m_reid;
    QVector<AthleteGalleryEntry> m_gallery;
    QVector<AthleteIdentityBinding> m_manualBindings;
    QHash<int, QVector<TrackState>> m_tracks;
    StatusCallback m_statusCallback;
    QString m_statusText;
    bool m_ready = false;
    bool m_reidReady = false;
    float m_detectorThreshold = 0.35f;
    float m_reidThreshold = 0.60f;
    float m_ambiguousMargin = 0.05f;
    qint64 m_trackTtlMs = 1200;
    int m_nextTrackId = 1;
};

#endif // TENSORRTATHLETEBACKEND_H
