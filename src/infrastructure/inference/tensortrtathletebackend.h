#ifndef TENSORRTATHLETEBACKEND_H
#define TENSORRTATHLETEBACKEND_H

#include "athleteanalysisresult.h"
#include "athletedetectionroi.h"
#include "athletetracker.h"

#include <QImage>
#include <QMutex>
#include <QSet>
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

    QVector<Detection> detect(const QImage &rgbFrame, QString *error);
    QVector<Detection> filterDetectionsByRoi(const QVector<Detection> &detections,
                                              int cameraId,
                                              const QSize &frameSize);
    QVector<float> embeddingFor(const QImage &rgbFrame, const QRectF &box, QString *error);
    bool isReidCandidate(const Detection &detection, const QSize &frameSize) const;
    void updateTrackIdentity(const QImage &rgbFrame,
                             const Detection &detection,
                             AthleteTrackState *track,
                             qint64 timestampMs,
                             QString *error);
    void applyTrackIdentity(const AthleteTrackState &track, AthleteInstance *instance) const;
    void applyManualBinding(AthleteInstance *instance, int cameraId) const;
    void setStatus(const QString &status, bool force = false);

    mutable QMutex m_mutex;
    std::unique_ptr<TensorRtRunner> m_detector;
    std::unique_ptr<TensorRtRunner> m_reid;
    QVector<AthleteGalleryEntry> m_gallery;
    QVector<AthleteIdentityBinding> m_manualBindings;
    AthleteTracker m_tracker;
    AthleteDetectionRoiMap m_detectionRois;
    QSet<int> m_missingRoiCameraIds;
    StatusCallback m_statusCallback;
    QString m_statusText;
    bool m_ready = false;
    bool m_reidReady = false;
    bool m_detectionRoiEnabled = false;
    bool m_trackAssistedReid = true;
    float m_detectorThreshold = 0.35f;
    float m_reidThreshold = 0.60f;
    float m_ambiguousMargin = 0.05f;
    float m_reidMinDetectionConfidence = 0.30f;
    float m_reidMinBoxAreaRatio = 0.0004f;
    float m_reidCropPaddingRatio = 0.15f;
    AthleteTrackReidPolicy m_reidPolicy;
};

#endif // TENSORRTATHLETEBACKEND_H
