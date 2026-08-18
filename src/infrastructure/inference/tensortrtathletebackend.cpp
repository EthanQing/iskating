#include "tensortrtathletebackend.h"

#include "tensorrtrunner.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QImage cropForReid(const QImage &image, const QRectF &box, float paddingRatio)
{
    const qreal horizontalPadding = box.width() * paddingRatio;
    const qreal verticalPadding = box.height() * paddingRatio;
    const QRect bounds = box.adjusted(-horizontalPadding,
                                      -verticalPadding,
                                      horizontalPadding,
                                      verticalPadding)
                             .toAlignedRect()
                             .intersected(image.rect());
    if (bounds.isEmpty()) {
        return {};
    }
    return image.copy(bounds);
}

float cosineSimilarity(const QVector<float> &first, const QVector<float> &second)
{
    if (first.isEmpty() || first.size() != static_cast<int>(second.size())) {
        return -1.0f;
    }

    double dot = 0.0;
    double firstNorm = 0.0;
    double secondNorm = 0.0;
    for (int index = 0; index < first.size(); ++index) {
        dot += static_cast<double>(first.at(index)) * second.at(index);
        firstNorm += static_cast<double>(first.at(index)) * first.at(index);
        secondNorm += static_cast<double>(second.at(index)) * second.at(index);
    }
    if (firstNorm <= 0.0 || secondNorm <= 0.0) {
        return -1.0f;
    }
    return static_cast<float>(dot / std::sqrt(firstNorm * secondNorm));
}

} // namespace

TensorRtAthleteBackend::TensorRtAthleteBackend() = default;

TensorRtAthleteBackend::~TensorRtAthleteBackend() = default;

void TensorRtAthleteBackend::setStatusCallback(StatusCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_statusCallback = std::move(callback);
}

void TensorRtAthleteBackend::setGallery(const QVector<AthleteGalleryEntry> &gallery)
{
    QMutexLocker locker(&m_mutex);
    m_gallery = gallery;
}

void TensorRtAthleteBackend::setManualBindings(const QVector<AthleteIdentityBinding> &bindings)
{
    QMutexLocker locker(&m_mutex);
    m_manualBindings = bindings;
}

void TensorRtAthleteBackend::resetTracking()
{
    QMutexLocker locker(&m_mutex);
    m_tracker.reset();
}

bool TensorRtAthleteBackend::initialize(const QString &modelDir, QString *error)
{
    const QDir dir(modelDir);
    const QString detectorPath = dir.absoluteFilePath(QStringLiteral("yolo26x.onnx"));
    const QString reidPath = dir.absoluteFilePath(QStringLiteral("personvit_msmt17_vit_base.onnx"));

    float detectorThreshold = 0.35f;
    float reidThreshold = 0.60f;
    float ambiguousMargin = 0.05f;
    qint64 trackTtlMs = 1200;
    float trackIouThreshold = 0.20f;
    bool detectionRoiEnabled = false;
    QString detectionRoiFile;
    bool trackAssistedReid = true;
    AthleteTrackReidPolicy reidPolicy;
    float reidMinDetectionConfidence = 0.30f;
    float reidMinBoxAreaRatio = 0.0004f;
    float reidCropPaddingRatio = 0.15f;
    QFile metadataFile(dir.absoluteFilePath(QStringLiteral("athlete_models.json")));
    if (metadataFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll());
        const QJsonObject runtime = document.object().value(QStringLiteral("runtime")).toObject();
        detectorThreshold = static_cast<float>(runtime.value(QStringLiteral("detectorThreshold")).toDouble(detectorThreshold));
        reidThreshold = static_cast<float>(runtime.value(QStringLiteral("reidThreshold")).toDouble(reidThreshold));
        ambiguousMargin = static_cast<float>(runtime.value(QStringLiteral("ambiguousMargin")).toDouble(ambiguousMargin));
        trackTtlMs = static_cast<qint64>(runtime.value(QStringLiteral("trackTtlMs")).toDouble(trackTtlMs));
        trackIouThreshold = static_cast<float>(runtime.value(QStringLiteral("trackIouThreshold")).toDouble(trackIouThreshold));
        detectionRoiEnabled = runtime.value(QStringLiteral("detectionRoiEnabled")).toBool(detectionRoiEnabled);
        detectionRoiFile = runtime.value(QStringLiteral("detectionRoiFile")).toString().trimmed();
        trackAssistedReid = runtime.value(QStringLiteral("trackAssistedReid")).toBool(trackAssistedReid);
        reidPolicy.minTrackHits = std::max(1, runtime.value(QStringLiteral("reidMinTrackHits")).toInt(reidPolicy.minTrackHits));
        reidPolicy.maxAttempts = std::max(0, runtime.value(QStringLiteral("reidMaxAttempts")).toInt(reidPolicy.maxAttempts));
        reidPolicy.retryIntervalMs = std::max<qint64>(0,
                                                      static_cast<qint64>(runtime.value(QStringLiteral("reidRetryMs"))
                                                                              .toDouble(reidPolicy.retryIntervalMs)));
        reidMinDetectionConfidence = static_cast<float>(runtime.value(QStringLiteral("reidMinDetectionConfidence"))
                                                              .toDouble(reidMinDetectionConfidence));
        reidMinBoxAreaRatio = static_cast<float>(runtime.value(QStringLiteral("reidMinBoxAreaRatio"))
                                                       .toDouble(reidMinBoxAreaRatio));
        reidCropPaddingRatio = static_cast<float>(runtime.value(QStringLiteral("reidCropPaddingRatio"))
                                                        .toDouble(reidCropPaddingRatio));
    }

    AthleteDetectionRoiMap detectionRois;
    if (detectionRoiEnabled) {
        QStringList roiWarnings;
        const QString roiPath = dir.absoluteFilePath(detectionRoiFile.isEmpty()
                                                          ? QStringLiteral("camera_detect_rois.json")
                                                          : detectionRoiFile);
        detectionRois = loadAthleteDetectionRois(roiPath, &roiWarnings);
        for (const QString &warning : roiWarnings) {
            qWarning() << "[AthleteAnalysis]" << warning;
        }
    }
    const int detectionRoiCount = detectionRois.size();

    auto detector = std::make_unique<TensorRtRunner>();
    if (!detector->initialize(detectorPath, error)) {
        QMutexLocker locker(&m_mutex);
        m_statusText = error ? *error : QStringLiteral("YOLO26x模型加载失败");
        return false;
    }

    auto reid = std::make_unique<TensorRtRunner>();
    QString reidError;
    const bool reidReady = reid->initialize(reidPath, &reidError);
    if (!reidReady) {
        qWarning() << "[AthleteAnalysis] ReID model unavailable:" << reidError;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_detector = std::move(detector);
        m_reid = reidReady ? std::move(reid) : nullptr;
        m_reidReady = reidReady;
        m_detectorThreshold = detectorThreshold;
        m_reidThreshold = reidThreshold;
        m_ambiguousMargin = ambiguousMargin;
        m_detectionRoiEnabled = detectionRoiEnabled;
        m_detectionRois = std::move(detectionRois);
        m_missingRoiCameraIds.clear();
        m_trackAssistedReid = trackAssistedReid;
        m_reidPolicy = reidPolicy;
        m_reidMinDetectionConfidence = std::clamp(reidMinDetectionConfidence, 0.0f, 1.0f);
        m_reidMinBoxAreaRatio = std::max(0.0f, reidMinBoxAreaRatio);
        m_reidCropPaddingRatio = std::clamp(reidCropPaddingRatio, 0.0f, 0.5f);
        m_tracker.reset();
        m_tracker.setTrackTtlMs(trackTtlMs);
        m_tracker.setIouThreshold(trackIouThreshold);
        m_ready = true;
    }
    const QString roiStatus = detectionRoiEnabled && detectionRoiCount > 0
                                  ? QStringLiteral("，已加载 %1 路检测 ROI").arg(detectionRoiCount)
                                  : QString();
    setStatus(reidReady
                  ? QStringLiteral("YOLO26x + PersonViT ReID 已就绪%1").arg(roiStatus)
                  : QStringLiteral("YOLO26x 已就绪，PersonViT ReID不可用：%1%2").arg(reidError, roiStatus),
              true);
    return true;
}

bool TensorRtAthleteBackend::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

bool TensorRtAthleteBackend::hasReid() const
{
    QMutexLocker locker(&m_mutex);
    return m_reidReady;
}

QString TensorRtAthleteBackend::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_statusText;
}

QVector<TensorRtAthleteBackend::Detection> TensorRtAthleteBackend::detect(const QImage &rgbFrame, QString *error)
{
    const QSize inputSize = m_detector->inputImageSize().isValid()
                                ? m_detector->inputImageSize()
                                : QSize(640, 640);
    const QImage resized = rgbFrame.convertToFormat(QImage::Format_RGB888)
                               .scaled(inputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const std::vector<float> input = imageToNchwFloat(resized, inputSize);
    std::vector<TensorRtOutput> outputs;
    if (!m_detector->infer(input, &outputs, error)) {
        return {};
    }

    const TensorRtOutput *output = nullptr;
    for (const TensorRtOutput &candidate : outputs) {
        if (candidate.values.size() == 300 * 6) {
            output = &candidate;
            break;
        }
    }
    if (!output) {
        if (error) {
            *error = QStringLiteral("YOLO26x输出不是预期的300x6格式");
        }
        return {};
    }

    QVector<Detection> detections;
    const float scaleX = static_cast<float>(rgbFrame.width()) / std::max(1, inputSize.width());
    const float scaleY = static_cast<float>(rgbFrame.height()) / std::max(1, inputSize.height());
    for (int index = 0; index < 300; ++index) {
        const size_t offset = static_cast<size_t>(index * 6);
        const float score = output->values.at(offset + 4);
        const int classId = static_cast<int>(std::round(output->values.at(offset + 5)));
        if (classId != 0 || score < m_detectorThreshold) {
            continue;
        }
        const float x0 = output->values.at(offset) * scaleX;
        const float y0 = output->values.at(offset + 1) * scaleY;
        const float x1 = output->values.at(offset + 2) * scaleX;
        const float y1 = output->values.at(offset + 3) * scaleY;
        const QRectF box(QPointF(std::clamp(x0, 0.0f, static_cast<float>(rgbFrame.width())),
                                 std::clamp(y0, 0.0f, static_cast<float>(rgbFrame.height()))),
                         QPointF(std::clamp(x1, 0.0f, static_cast<float>(rgbFrame.width())),
                                 std::clamp(y1, 0.0f, static_cast<float>(rgbFrame.height()))));
        if (box.width() > 1.0 && box.height() > 1.0) {
            detections.append({box, score, classId});
        }
    }
    return detections;
}

QVector<TensorRtAthleteBackend::Detection> TensorRtAthleteBackend::filterDetectionsByRoi(
    const QVector<Detection> &detections,
    int cameraId,
    const QSize &frameSize)
{
    if (!m_detectionRoiEnabled || cameraId <= 0) {
        return detections;
    }

    const auto roi = m_detectionRois.constFind(cameraId);
    if (roi == m_detectionRois.cend()) {
        if (!m_missingRoiCameraIds.contains(cameraId)) {
            m_missingRoiCameraIds.insert(cameraId);
            qWarning() << "[AthleteAnalysis] camera" << cameraId
                       << "has no detection ROI; detections remain unfiltered";
        }
        return detections;
    }

    QVector<Detection> filtered;
    filtered.reserve(detections.size());
    for (const Detection &detection : detections) {
        if (isAthleteDetectionInsideRoi(roi.value(), detection.box, frameSize)) {
            filtered.append(detection);
        }
    }
    return filtered;
}

QVector<float> TensorRtAthleteBackend::embeddingFor(const QImage &rgbFrame,
                                                     const QRectF &box,
                                                     QString *error)
{
    if (!m_reid) {
        return {};
    }
    const QImage crop = cropForReid(rgbFrame, box, m_reidCropPaddingRatio);
    if (crop.isNull()) {
        return {};
    }
    const QSize inputSize = m_reid->inputImageSize().isValid()
                                ? m_reid->inputImageSize()
                                : QSize(128, 256);
    const QImage resized = crop.convertToFormat(QImage::Format_RGB888)
                               .scaled(inputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    std::vector<float> input = imageToNchwFloat(resized, inputSize);
    for (float &value : input) {
        value = value * 2.0f - 1.0f;
    }
    std::vector<TensorRtOutput> outputs;
    if (!m_reid->infer(input, &outputs, error) || outputs.empty()) {
        return {};
    }
    const TensorRtOutput *best = &outputs.front();
    for (const TensorRtOutput &candidate : outputs) {
        if (candidate.values.size() > best->values.size()) {
            best = &candidate;
        }
    }
    QVector<float> embedding;
    embedding.reserve(static_cast<int>(best->values.size()));
    double norm = 0.0;
    for (float value : best->values) {
        norm += static_cast<double>(value) * value;
    }
    norm = std::sqrt(norm);
    for (float value : best->values) {
        embedding.append(norm > 0.0 ? static_cast<float>(value / norm) : value);
    }
    return embedding;
}

bool TensorRtAthleteBackend::isReidCandidate(const Detection &detection, const QSize &frameSize) const
{
    if (detection.score < m_reidMinDetectionConfidence || frameSize.width() <= 0 || frameSize.height() <= 0) {
        return false;
    }
    const qreal frameArea = static_cast<qreal>(frameSize.width()) * frameSize.height();
    const qreal boxArea = detection.box.width() * detection.box.height();
    return frameArea > 0.0 && boxArea / frameArea >= m_reidMinBoxAreaRatio;
}

void TensorRtAthleteBackend::updateTrackIdentity(const QImage &rgbFrame,
                                                  const Detection &detection,
                                                  AthleteTrackState *track,
                                                  qint64 timestampMs,
                                                  QString *error)
{
    if (!track) {
        return;
    }

    ++track->reidAttempts;
    track->lastReidAttemptMs = timestampMs;
    track->athleteId.clear();
    track->participantId.clear();
    track->label.clear();
    track->identityStatus = QStringLiteral("unknown");
    track->identitySource = QStringLiteral("personvit");
    track->identityConfidence = 0.0f;
    track->reidSimilarity = 0.0f;

    const QVector<float> embedding = embeddingFor(rgbFrame, detection.box, error);
    if (embedding.isEmpty()) {
        return;
    }

    float best = -1.0f;
    float second = -1.0f;
    const AthleteGalleryEntry *bestEntry = nullptr;
    for (const AthleteGalleryEntry &entry : m_gallery) {
        const float similarity = cosineSimilarity(entry.embedding, embedding);
        if (similarity > best) {
            second = best;
            best = similarity;
            bestEntry = &entry;
        } else if (similarity > second) {
            second = similarity;
        }
    }

    track->reidSimilarity = std::max(0.0f, best);
    track->identityConfidence = track->reidSimilarity;
    if (!bestEntry || best < m_reidThreshold) {
        return;
    }
    if (best - second < m_ambiguousMargin) {
        track->identityStatus = QStringLiteral("ambiguous");
        return;
    }

    track->athleteId = bestEntry->athleteId;
    track->participantId = bestEntry->participantId;
    track->label = bestEntry->label;
    track->identityStatus = QStringLiteral("identified");
}

void TensorRtAthleteBackend::applyTrackIdentity(const AthleteTrackState &track,
                                                 AthleteInstance *instance) const
{
    if (!instance) {
        return;
    }
    instance->athleteId = track.athleteId;
    instance->participantId = track.participantId;
    instance->label = track.label;
    instance->identityStatus = track.identityStatus;
    instance->identitySource = track.identitySource;
    instance->identityConfidence = track.identityConfidence;
    instance->reidSimilarity = track.reidSimilarity;
}

void TensorRtAthleteBackend::applyManualBinding(AthleteInstance *instance, int cameraId) const
{
    if (!instance || instance->identityStatus == QStringLiteral("identified")) {
        return;
    }
    for (const AthleteIdentityBinding &binding : m_manualBindings) {
        if (binding.cameraId != cameraId || binding.trackId != instance->trackId) {
            continue;
        }
        instance->athleteId = binding.athleteId;
        instance->participantId = binding.participantId;
        instance->label = binding.label;
        instance->identityStatus = QStringLiteral("identified");
        instance->identityConfidence = 1.0f;
        instance->identitySource = QStringLiteral("manual");
        return;
    }
}

AthleteFrameResult TensorRtAthleteBackend::infer(const QImage &rgbFrame, int cameraId, qint64 timestampMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_ready || !m_detector || rgbFrame.isNull()) {
        return {};
    }

    QString error;
    const QVector<Detection> detected = detect(rgbFrame, &error);
    if (!error.isEmpty()) {
        m_statusText = QStringLiteral("YOLO26x推理失败：%1").arg(error);
        return {};
    }
    const QVector<Detection> detections = filterDetectionsByRoi(detected, cameraId, rgbFrame.size());

    QVector<QRectF> boxes;
    boxes.reserve(detections.size());
    for (const Detection &detection : detections) {
        boxes.append(detection.box);
    }
    const QVector<int> trackIds = m_tracker.update(cameraId, boxes, timestampMs);

    AthleteFrameResult frame;
    frame.cameraId = cameraId;
    frame.timestampMs = timestampMs;
    frame.frameSize = rgbFrame.size();
    for (int index = 0; index < detections.size(); ++index) {
        const Detection &detection = detections.at(index);
        AthleteTrackState *track = m_tracker.track(cameraId, trackIds.at(index));
        if (!track) {
            continue;
        }

        AthleteInstance instance;
        instance.trackId = track->trackId;
        instance.classId = detection.classId;
        instance.box = detection.box;
        instance.detectionConfidence = detection.score;

        if (m_gallery.isEmpty()) {
            track->athleteId.clear();
            track->participantId.clear();
            track->label.clear();
            track->identityStatus = QStringLiteral("unknown");
            track->identitySource.clear();
            track->identityConfidence = 0.0f;
            track->reidSimilarity = 0.0f;
            track->reidAttempts = 0;
            track->lastReidAttemptMs = 0;
        }

        applyTrackIdentity(*track, &instance);
        applyManualBinding(&instance, cameraId);

        const bool shouldRunReid = m_reidReady
                                   && !m_gallery.isEmpty()
                                   && instance.identityStatus != QStringLiteral("identified")
                                   && (!m_trackAssistedReid
                                       || (isReidCandidate(detection, rgbFrame.size())
                                           && shouldAttemptAthleteTrackReid(*track, m_reidPolicy, timestampMs)));
        if (shouldRunReid) {
            QString reidError;
            updateTrackIdentity(rgbFrame, detection, track, timestampMs, &reidError);
            if (!reidError.isEmpty()) {
                qWarning() << "[AthleteAnalysis] PersonViT ReID failed:" << reidError;
            }
            applyTrackIdentity(*track, &instance);
            applyManualBinding(&instance, cameraId);
        }
        frame.instances.append(instance);
    }

    m_statusText = QStringLiteral("运动员识别运行中：%1 人").arg(frame.instances.size());
    return frame;
}

QVector<float> TensorRtAthleteBackend::extractEmbedding(const QImage &rgbImage, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_reidReady || !m_reid || rgbImage.isNull()) {
        if (error) {
            *error = QStringLiteral("PersonViT ReID 模型未就绪或样本图片为空");
        }
        return {};
    }
    return embeddingFor(rgbImage, QRectF(QPointF(0, 0), QSizeF(rgbImage.size())), error);
}

void TensorRtAthleteBackend::setStatus(const QString &status, bool force)
{
    StatusCallback callback;
    {
        QMutexLocker locker(&m_mutex);
        if (!force && m_statusText == status) {
            return;
        }
        m_statusText = status;
        callback = m_statusCallback;
    }
    if (callback) {
        callback(status);
    }
}
