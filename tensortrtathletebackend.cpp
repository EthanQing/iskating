#include "tensortrtathletebackend.h"

#include "tensorrtrunner.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QMutexLocker>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float kTrackIouThreshold = 0.20f;

float intersectionOverUnion(const QRectF &first, const QRectF &second)
{
    const QRectF intersection = first.intersected(second);
    const double intersectionArea = std::max(0.0, intersection.width()) * std::max(0.0, intersection.height());
    const double unionArea = first.width() * first.height() + second.width() * second.height() - intersectionArea;
    return unionArea > 0.0 ? static_cast<float>(intersectionArea / unionArea) : 0.0f;
}

QImage cropForReid(const QImage &image, const QRectF &box)
{
    const QRect bounds = box.toAlignedRect().intersected(image.rect());
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
    m_tracks.clear();
    m_nextTrackId = 1;
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
    QFile metadataFile(dir.absoluteFilePath(QStringLiteral("athlete_models.json")));
    if (metadataFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll());
        const QJsonObject runtime = document.object().value(QStringLiteral("runtime")).toObject();
        detectorThreshold = static_cast<float>(runtime.value(QStringLiteral("detectorThreshold")).toDouble(detectorThreshold));
        reidThreshold = static_cast<float>(runtime.value(QStringLiteral("reidThreshold")).toDouble(reidThreshold));
        ambiguousMargin = static_cast<float>(runtime.value(QStringLiteral("ambiguousMargin")).toDouble(ambiguousMargin));
        trackTtlMs = static_cast<qint64>(runtime.value(QStringLiteral("trackTtlMs")).toDouble(trackTtlMs));
    }

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
        m_trackTtlMs = trackTtlMs;
        m_ready = true;
    }
    setStatus(reidReady
                  ? QStringLiteral("YOLO26x + PersonViT ReID 已就绪")
                  : QStringLiteral("YOLO26x 已就绪，PersonViT ReID不可用：%1").arg(reidError),
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

QVector<float> TensorRtAthleteBackend::embeddingFor(const QImage &rgbFrame,
                                                     const QRectF &box,
                                                     QString *error)
{
    if (!m_reid) {
        return {};
    }
    const QImage crop = cropForReid(rgbFrame, box);
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

void TensorRtAthleteBackend::updateTrack(AthleteInstance *instance, int cameraId, qint64 timestampMs)
{
    QVector<TrackState> &tracks = m_tracks[cameraId];
    for (int index = tracks.size() - 1; index >= 0; --index) {
        if (timestampMs - tracks.at(index).lastSeenMs > m_trackTtlMs) {
            tracks.removeAt(index);
        }
    }

    int bestIndex = -1;
    float bestIou = kTrackIouThreshold;
    for (int index = 0; index < tracks.size(); ++index) {
        const float iou = intersectionOverUnion(instance->box, tracks.at(index).box);
        if (iou > bestIou) {
            bestIou = iou;
            bestIndex = index;
        }
    }
    if (bestIndex < 0) {
        TrackState state;
        state.trackId = m_nextTrackId++;
        state.box = instance->box;
        state.lastSeenMs = timestampMs;
        state.athleteId = instance->athleteId;
        state.participantId = instance->participantId;
        state.label = instance->label;
        tracks.append(state);
        instance->trackId = state.trackId;
        return;
    }

    TrackState &state = tracks[bestIndex];
    state.box = instance->box;
    state.lastSeenMs = timestampMs;
    if (!instance->athleteId.isEmpty()) {
        state.athleteId = instance->athleteId;
        state.participantId = instance->participantId;
        state.label = instance->label;
    } else {
        instance->athleteId = state.athleteId;
        instance->participantId = state.participantId;
        instance->label = state.label;
    }
    instance->trackId = state.trackId;
}

AthleteFrameResult TensorRtAthleteBackend::infer(const QImage &rgbFrame, int cameraId, qint64 timestampMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_ready || !m_detector || rgbFrame.isNull()) {
        return {};
    }

    QString error;
    const QVector<Detection> detections = detect(rgbFrame, &error);
    if (!error.isEmpty()) {
        m_statusText = QStringLiteral("YOLO26x推理失败：%1").arg(error);
        return {};
    }

    AthleteFrameResult frame;
    frame.cameraId = cameraId;
    frame.timestampMs = timestampMs;
    frame.frameSize = rgbFrame.size();
    for (const Detection &detection : detections) {
        AthleteInstance instance;
        instance.classId = detection.classId;
        instance.box = detection.box;
        instance.detectionConfidence = detection.score;

        if (m_reidReady && !m_gallery.isEmpty()) {
            const QVector<float> embedding = embeddingFor(rgbFrame, detection.box, &error);
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
            instance.reidSimilarity = std::max(0.0f, best);
            instance.identityConfidence = instance.reidSimilarity;
            if (bestEntry && best >= m_reidThreshold) {
                if (best - second < m_ambiguousMargin) {
                    instance.identityStatus = QStringLiteral("ambiguous");
                    instance.identitySource = QStringLiteral("personvit");
                } else {
                    instance.athleteId = bestEntry->athleteId;
                    instance.participantId = bestEntry->participantId;
                    instance.label = bestEntry->label;
                    instance.identityStatus = QStringLiteral("identified");
                    instance.identitySource = QStringLiteral("personvit");
                }
            }
        }
        updateTrack(&instance, cameraId, timestampMs);
        if (instance.identityStatus != QStringLiteral("identified")) {
            for (const AthleteIdentityBinding &binding : m_manualBindings) {
                if (binding.cameraId != cameraId || binding.trackId != instance.trackId) {
                    continue;
                }
                instance.athleteId = binding.athleteId;
                instance.participantId = binding.participantId;
                instance.label = binding.label;
                instance.identityStatus = QStringLiteral("identified");
                instance.identityConfidence = 1.0f;
                instance.identitySource = QStringLiteral("manual");
                break;
            }
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
