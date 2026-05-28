#include "tensorrtbodyposebackend.h"
#include "tensorrtrunner.h"

#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr int kBodyKeypointCount = 17;
constexpr float kBoxScoreThreshold = 0.35f;
constexpr float kKeypointThreshold = 0.25f;
constexpr float kNmsIouThreshold = 0.45f;
constexpr int kMaxPersons = 2;

const QStringList &bodyKeypointNames()
{
    static const QStringList names = {
        QStringLiteral("nose"),
        QStringLiteral("left_eye"),
        QStringLiteral("right_eye"),
        QStringLiteral("left_ear"),
        QStringLiteral("right_ear"),
        QStringLiteral("left_shoulder"),
        QStringLiteral("right_shoulder"),
        QStringLiteral("left_elbow"),
        QStringLiteral("right_elbow"),
        QStringLiteral("left_wrist"),
        QStringLiteral("right_wrist"),
        QStringLiteral("left_hip"),
        QStringLiteral("right_hip"),
        QStringLiteral("left_knee"),
        QStringLiteral("right_knee"),
        QStringLiteral("left_ankle"),
        QStringLiteral("right_ankle"),
    };
    return names;
}

struct BodyPreprocess
{
    std::vector<float> tensor;
    QSize inputSize;
    QSize frameSize;
    float ratio = 1.0f;
    int padLeft = 0;
    int padTop = 0;
};

struct BodyDetection
{
    QRectF box;
    std::array<QPointF, kBodyKeypointCount> points{};
    std::array<float, kBodyKeypointCount> pointScores{};
    float score = 0.0f;
};

float intersectionOverUnion(const QRectF &a, const QRectF &b)
{
    const QRectF intersection = a.intersected(b);
    const double interArea = std::max(0.0, intersection.width()) * std::max(0.0, intersection.height());
    const double unionArea = a.width() * a.height() + b.width() * b.height() - interArea;
    if (unionArea <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(interArea / unionArea);
}

BodyPreprocess preprocessBody(const QImage &image, const QSize &targetSize)
{
    BodyPreprocess result;
    result.inputSize = targetSize;
    result.frameSize = image.size();
    if (image.isNull() || targetSize.isEmpty()) {
        return result;
    }

    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    result.ratio = std::min(static_cast<float>(targetSize.width()) / std::max(1, rgb.width()),
                            static_cast<float>(targetSize.height()) / std::max(1, rgb.height()));
    const QSize scaledSize(std::max(1, static_cast<int>(std::round(rgb.width() * result.ratio))),
                           std::max(1, static_cast<int>(std::round(rgb.height() * result.ratio))));
    result.padLeft = (targetSize.width() - scaledSize.width()) / 2;
    result.padTop = (targetSize.height() - scaledSize.height()) / 2;

    QImage letterboxed(targetSize, QImage::Format_RGB888);
    letterboxed.fill(QColor(114, 114, 114));
    QPainter painter(&letterboxed);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(QPoint(result.padLeft, result.padTop), scaledSize), rgb);
    painter.end();

    result.tensor = imageToNchwFloat(letterboxed, targetSize);
    return result;
}

QPointF mapModelPointToFrame(float x, float y, const BodyPreprocess &preprocess)
{
    return QPointF((x - preprocess.padLeft) / std::max(0.0001f, preprocess.ratio),
                   (y - preprocess.padTop) / std::max(0.0001f, preprocess.ratio));
}

QVector<BodyDetection> decodeYoloPoseOutput(const std::vector<TensorRtOutput> &outputs,
                                            const BodyPreprocess &preprocess)
{
    const TensorRtOutput *output = nullptr;
    for (const TensorRtOutput &candidate : outputs) {
        if (candidate.values.size() >= 56 * 1000) {
            output = &candidate;
            break;
        }
    }
    if (!output) {
        return {};
    }

    int channels = 56;
    int anchors = 0;
    if (output->dims.nbDims == 3) {
        channels = static_cast<int>(output->dims.d[1]);
        anchors = static_cast<int>(output->dims.d[2]);
    } else if (output->dims.nbDims == 2) {
        channels = static_cast<int>(output->dims.d[0]);
        anchors = static_cast<int>(output->dims.d[1]);
    } else {
        anchors = static_cast<int>(output->values.size() / channels);
    }
    if (channels < 56 || anchors <= 0) {
        return {};
    }

    auto valueAt = [output, channels, anchors](int channel, int anchor) {
        return output->values.at(static_cast<size_t>(channel * anchors + anchor));
    };

    QVector<BodyDetection> candidates;
    const QRectF frameRect(QPointF(0.0, 0.0), QSizeF(preprocess.frameSize));
    for (int anchor = 0; anchor < anchors; ++anchor) {
        const float score = valueAt(4, anchor);
        if (score < kBoxScoreThreshold) {
            continue;
        }

        const float cx = valueAt(0, anchor);
        const float cy = valueAt(1, anchor);
        const float bw = valueAt(2, anchor);
        const float bh = valueAt(3, anchor);
        const QPointF topLeft = mapModelPointToFrame(cx - bw * 0.5f, cy - bh * 0.5f, preprocess);
        const QPointF bottomRight = mapModelPointToFrame(cx + bw * 0.5f, cy + bh * 0.5f, preprocess);

        BodyDetection detection;
        detection.score = score;
        detection.box = QRectF(topLeft, bottomRight).normalized().intersected(frameRect);
        if (detection.box.width() < 4.0 || detection.box.height() < 4.0) {
            continue;
        }

        for (int k = 0; k < kBodyKeypointCount; ++k) {
            const int base = 5 + k * 3;
            detection.points[static_cast<size_t>(k)] = mapModelPointToFrame(valueAt(base + 0, anchor),
                                                                            valueAt(base + 1, anchor),
                                                                            preprocess);
            detection.pointScores[static_cast<size_t>(k)] = valueAt(base + 2, anchor);
        }
        candidates.push_back(detection);
    }

    std::sort(candidates.begin(), candidates.end(), [](const BodyDetection &a, const BodyDetection &b) {
        return a.score > b.score;
    });

    QVector<BodyDetection> kept;
    for (const BodyDetection &candidate : candidates) {
        bool suppressed = false;
        for (const BodyDetection &existing : kept) {
            if (intersectionOverUnion(candidate.box, existing.box) > kNmsIouThreshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(candidate);
            if (kept.size() >= kMaxPersons) {
                break;
            }
        }
    }
    return kept;
}

PoseFrameResult detectionsToPoseFrame(const QVector<BodyDetection> &detections,
                                      const QSize &frameSize,
                                      int cameraId,
                                      qint64 timestampMs)
{
    PoseFrameResult frame;
    frame.cameraId = cameraId;
    frame.timestampMs = timestampMs;
    frame.frameSize = QSizeF(frameSize);
    frame.skeletonType = PoseSkeletonType::Body17;
    frame.sourceName = QStringLiteral("yolov8n_pose_body17");
    frame.instances.reserve(detections.size());

    const QStringList &names = bodyKeypointNames();
    for (int personIndex = 0; personIndex < detections.size(); ++personIndex) {
        const BodyDetection &detection = detections.at(personIndex);
        PoseInstance instance;
        instance.trackId = personIndex;
        instance.skeletonType = PoseSkeletonType::Body17;
        instance.kind = PoseInstanceKind::Person;
        instance.box = detection.box;
        instance.confidence = detection.score;
        instance.keypoints.reserve(kBodyKeypointCount);
        for (int i = 0; i < kBodyKeypointCount; ++i) {
            const QPointF point = detection.points[static_cast<size_t>(i)];
            const float score = detection.pointScores[static_cast<size_t>(i)];
            PoseKeypoint keypoint;
            keypoint.index = i;
            keypoint.name = names.at(i);
            keypoint.imagePoint = point;
            keypoint.confidence = score;
            keypoint.valid = score >= kKeypointThreshold
                             && point.x() >= 0.0
                             && point.y() >= 0.0
                             && point.x() <= frameSize.width()
                             && point.y() <= frameSize.height();
            instance.keypoints.push_back(keypoint);
        }
        frame.instances.push_back(instance);
    }

    return frame;
}

} // namespace

TensorRtBodyPoseBackend::TensorRtBodyPoseBackend() = default;

TensorRtBodyPoseBackend::~TensorRtBodyPoseBackend() = default;

bool TensorRtBodyPoseBackend::initialize(const QString &modelDir, QString *error)
{
    QMutexLocker locker(&m_mutex);
    const QDir dir(modelDir);
    const QString onnxPath = dir.absoluteFilePath(QStringLiteral("yolov8n-pose.onnx"));

    m_runner = std::make_unique<TensorRtRunner>();
    if (!m_runner->initialize(onnxPath, error)) {
        m_statusText = error ? *error : QStringLiteral("人体姿态模型加载失败");
        return false;
    }

    QString rtmw3dError;
    m_rtmw3dReady = m_rtmw3dBackend.initialize(modelDir, &rtmw3dError);
    if (!m_rtmw3dReady) {
        qWarning() << "[BodyPose]" << rtmw3dError;
    }

    m_ready = true;
    m_statusText = m_rtmw3dReady
                       ? QStringLiteral("人体 2D + RTMW3D TensorRT 已就绪")
                       : QStringLiteral("人体 2D TensorRT 已就绪，RTMW3D模型缺失：%1").arg(rtmw3dError);
    return true;
}

bool TensorRtBodyPoseBackend::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

QString TensorRtBodyPoseBackend::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_statusText;
}

PoseFrameResult TensorRtBodyPoseBackend::infer(const QImage &rgbFrame, int cameraId, qint64 timestampMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_ready || !m_runner || rgbFrame.isNull()) {
        return {};
    }

    QString error;
    std::vector<TensorRtOutput> outputs;
    const QSize inputSize = m_runner->inputImageSize().isValid() ? m_runner->inputImageSize() : QSize(640, 640);
    const BodyPreprocess preprocess = preprocessBody(rgbFrame, inputSize);
    if (!m_runner->infer(preprocess.tensor, &outputs, &error)) {
        m_statusText = QStringLiteral("人体姿态推理失败：%1").arg(error);
        qWarning() << "[BodyPose]" << m_statusText;
        return {};
    }

    const QVector<BodyDetection> detections = decodeYoloPoseOutput(outputs, preprocess);
    PoseFrameResult frame = detectionsToPoseFrame(detections, rgbFrame.size(), cameraId, timestampMs);
    QString rtmw3dError;
    const bool has3d = m_rtmw3dReady && m_rtmw3dBackend.infer(rgbFrame, &frame, &rtmw3dError);
    m_statusText = frame.instances.isEmpty()
                       ? QStringLiteral("人体姿态 TensorRT 运行中：未检测到人体")
                       : (has3d
                              ? QStringLiteral("人体姿态 TensorRT 运行中：%1 人，RTMW3D 3D已输出")
                                    .arg(frame.instances.size())
                              : QStringLiteral("人体姿态 TensorRT 运行中：%1 人，等待RTMW3D：%2")
                                    .arg(frame.instances.size())
                                    .arg(rtmw3dError.isEmpty() ? m_rtmw3dBackend.statusText() : rtmw3dError));
    return frame;
}
