#include "tensortrthandposebackend.h"
#include "tensorrtrunner.h"

#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

std::vector<float> imageToNhwcFloat(const QImage &image, const QSize &targetSize)
{
    const QImage scaled = image.convertToFormat(QImage::Format_RGB888)
                              .scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    std::vector<float> result(static_cast<size_t>(targetSize.width() * targetSize.height() * 3), 0.0f);
    size_t index = 0;
    for (int y = 0; y < scaled.height(); ++y) {
        const uchar *line = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            result[index++] = line[x * 3 + 0] / 255.0f;
            result[index++] = line[x * 3 + 1] / 255.0f;
            result[index++] = line[x * 3 + 2] / 255.0f;
        }
    }
    return result;
}

struct PalmPreprocess
{
    std::vector<float> tensor;
    QPointF padBias;
    float scale = 1.0f;
    QSize inputSize;
};

struct PalmDetection
{
    QRectF box;
    std::array<QPointF, 7> landmarks{};
    float score = 0.0f;
};

struct LandmarkInput
{
    std::vector<float> tensor;
    QRectF roi;
};

float sigmoid(float value)
{
    value = std::clamp(value, -80.0f, 80.0f);
    return 1.0f / (1.0f + std::exp(-value));
}

const std::vector<QPointF> &palmAnchors()
{
    static const std::vector<QPointF> anchors = [] {
        std::vector<QPointF> result;
        result.reserve(2016);
        auto appendGrid = [&result](int gridSize, int repeats) {
            for (int y = 0; y < gridSize; ++y) {
                for (int x = 0; x < gridSize; ++x) {
                    const QPointF center((x + 0.5) / gridSize, (y + 0.5) / gridSize);
                    for (int i = 0; i < repeats; ++i) {
                        result.push_back(center);
                    }
                }
            }
        };
        appendGrid(24, 2);
        appendGrid(12, 6);
        return result;
    }();
    return anchors;
}

PalmPreprocess preprocessPalm(const QImage &image, const QSize &targetSize)
{
    PalmPreprocess result;
    result.inputSize = targetSize;
    if (image.isNull() || targetSize.isEmpty()) {
        return result;
    }

    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    const float ratio = std::min(static_cast<float>(targetSize.width()) / std::max(1, rgb.width()),
                                 static_cast<float>(targetSize.height()) / std::max(1, rgb.height()));
    const QSize scaledSize(std::max(1, static_cast<int>(std::round(rgb.width() * ratio))),
                           std::max(1, static_cast<int>(std::round(rgb.height() * ratio))));
    const int padLeft = (targetSize.width() - scaledSize.width()) / 2;
    const int padTop = (targetSize.height() - scaledSize.height()) / 2;

    QImage letterboxed(targetSize, QImage::Format_RGB888);
    letterboxed.fill(Qt::black);
    QPainter painter(&letterboxed);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(QPoint(padLeft, padTop), scaledSize), rgb);
    painter.end();

    result.tensor = imageToNhwcFloat(letterboxed, targetSize);
    result.padBias = QPointF(padLeft / ratio, padTop / ratio);
    result.scale = static_cast<float>(std::max(rgb.width(), rgb.height()));
    return result;
}

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

QVector<PalmDetection> decodePalmDetections(const std::vector<TensorRtOutput> &outputs,
                                            const PalmPreprocess &preprocess,
                                            const QSize &frameSize)
{
    constexpr float kPalmScoreThreshold = 0.55f;
    constexpr float kNmsIouThreshold = 0.30f;
    constexpr int kMaxHands = 2;

    const auto &anchors = palmAnchors();
    const std::vector<float> *boxData = nullptr;
    const std::vector<float> *scoreData = nullptr;
    for (const TensorRtOutput &output : outputs) {
        if (output.values.size() == anchors.size()) {
            scoreData = &output.values;
        } else if (output.values.size() == anchors.size() * 18) {
            boxData = &output.values;
        }
    }
    if (!boxData || !scoreData || preprocess.inputSize.isEmpty()) {
        return {};
    }

    QVector<PalmDetection> candidates;
    const float inputW = static_cast<float>(preprocess.inputSize.width());
    const float inputH = static_cast<float>(preprocess.inputSize.height());
    for (int i = 0; i < static_cast<int>(anchors.size()); ++i) {
        const float score = sigmoid(scoreData->at(static_cast<size_t>(i)));
        if (score < kPalmScoreThreshold) {
            continue;
        }

        const float *raw = boxData->data() + static_cast<size_t>(i) * 18;
        const QPointF anchor = anchors[static_cast<size_t>(i)];
        const float cx = raw[0] / inputW + static_cast<float>(anchor.x());
        const float cy = raw[1] / inputH + static_cast<float>(anchor.y());
        const float w = raw[2] / inputW;
        const float h = raw[3] / inputH;

        PalmDetection detection;
        detection.score = score;
        detection.box = QRectF((cx - w * 0.5f) * preprocess.scale - preprocess.padBias.x(),
                               (cy - h * 0.5f) * preprocess.scale - preprocess.padBias.y(),
                               w * preprocess.scale,
                               h * preprocess.scale);
        detection.box = detection.box.intersected(QRectF(QPointF(0.0, 0.0), QSizeF(frameSize)));
        for (int landmark = 0; landmark < 7; ++landmark) {
            const float lx = raw[4 + landmark * 2 + 0] / inputW + static_cast<float>(anchor.x());
            const float ly = raw[4 + landmark * 2 + 1] / inputH + static_cast<float>(anchor.y());
            detection.landmarks[static_cast<size_t>(landmark)] = QPointF(lx * preprocess.scale - preprocess.padBias.x(),
                                                                         ly * preprocess.scale - preprocess.padBias.y());
        }
        if (detection.box.width() > 2.0 && detection.box.height() > 2.0) {
            candidates.push_back(detection);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const PalmDetection &a, const PalmDetection &b) {
        return a.score > b.score;
    });

    QVector<PalmDetection> kept;
    for (const PalmDetection &candidate : candidates) {
        bool suppressed = false;
        for (const PalmDetection &existing : kept) {
            if (intersectionOverUnion(candidate.box, existing.box) > kNmsIouThreshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(candidate);
            if (kept.size() >= kMaxHands) {
                break;
            }
        }
    }
    return kept;
}

bool makeLandmarkInput(const QImage &image,
                       const PalmDetection &palm,
                       const QSize &targetSize,
                       LandmarkInput *landmarkInput)
{
    if (!landmarkInput || image.isNull() || targetSize.isEmpty() || !palm.box.isValid()) {
        return false;
    }

    const QPointF center = palm.box.center() + QPointF(0.0, -0.4 * palm.box.height());
    const float side = static_cast<float>(std::max(palm.box.width(), palm.box.height()) * 3.0);
    if (side < 8.0f) {
        return false;
    }

    const QRectF roi(center.x() - side * 0.5f, center.y() - side * 0.5f, side, side);
    const QRect imageRect = image.rect();
    const QRect sourceRect = roi.toAlignedRect().intersected(imageRect);
    if (sourceRect.isEmpty()) {
        return false;
    }

    const int sidePixels = std::max(8, static_cast<int>(std::ceil(side)));
    QImage square(sidePixels, sidePixels, QImage::Format_RGB888);
    square.fill(Qt::black);

    QPainter painter(&square);
    painter.drawImage(QPointF(sourceRect.left() - roi.left(), sourceRect.top() - roi.top()),
                      image.convertToFormat(QImage::Format_RGB888),
                      sourceRect);
    painter.end();

    landmarkInput->roi = roi;
    landmarkInput->tensor = imageToNhwcFloat(square, targetSize);
    return true;
}

const TensorRtOutput *findNamedOutput(const std::vector<TensorRtOutput> &outputs,
                                      qsizetype valueCount,
                                      const QString &namePart)
{
    for (const TensorRtOutput &output : outputs) {
        if (output.values.size() == static_cast<size_t>(valueCount)
            && output.name.contains(namePart, Qt::CaseInsensitive)) {
            return &output;
        }
    }
    return nullptr;
}

HandPoseResult decodeLandmarkResult(const std::vector<TensorRtOutput> &outputs,
                                    const PalmDetection &palm,
                                    const LandmarkInput &landmarkInput,
                                    const QSize &frameSize,
                                    int cameraId,
                                    qint64 timestampMs)
{
    const TensorRtOutput *landmarks = nullptr;
    for (const TensorRtOutput &output : outputs) {
        if (output.values.size() == 63
            && output.name.compare(QStringLiteral("Identity"), Qt::CaseInsensitive) == 0) {
            landmarks = &output;
            break;
        }
    }
    if (!landmarks) {
        for (const TensorRtOutput &output : outputs) {
            if (output.values.size() == 63 && !output.name.contains(QStringLiteral("Identity_3"), Qt::CaseInsensitive)) {
                landmarks = &output;
                break;
            }
        }
    }

    const TensorRtOutput *confidenceOutput = findNamedOutput(outputs, 1, QStringLiteral("handflag"));
    const TensorRtOutput *handednessOutput = findNamedOutput(outputs, 1, QStringLiteral("handedness"));
    if (!confidenceOutput || !handednessOutput) {
        QVector<const TensorRtOutput *> singles;
        for (const TensorRtOutput &output : outputs) {
            if (output.values.size() == 1) {
                singles.push_back(&output);
            }
        }
        if (!confidenceOutput && !singles.isEmpty()) {
            confidenceOutput = singles.first();
        }
        if (!handednessOutput && singles.size() > 1) {
            handednessOutput = singles.at(1);
        }
    }

    if (!landmarks || landmarks->values.size() < 63) {
        return {};
    }

    const float landmarkConfidence = confidenceOutput ? confidenceOutput->values.front() : 1.0f;
    if (landmarkConfidence < 0.45f) {
        return {};
    }

    HandPoseResult result;
    result.cameraId = cameraId;
    result.timestampMs = timestampMs;
    result.confidence = std::clamp(palm.score * landmarkConfidence, 0.0f, 1.0f);
    result.handedness = Handedness::Unknown;
    if (handednessOutput && !handednessOutput->values.empty()) {
        result.handedness = handednessOutput->values.front() >= 0.5f ? Handedness::Right : Handedness::Left;
    }
    result.frameSize = QSizeF(frameSize);
    result.landmarks.resize(21);

    const float sx = static_cast<float>(landmarkInput.roi.width()) / 224.0f;
    const float sy = static_cast<float>(landmarkInput.roi.height()) / 224.0f;
    qreal minX = std::numeric_limits<qreal>::max();
    qreal minY = std::numeric_limits<qreal>::max();
    qreal maxX = std::numeric_limits<qreal>::lowest();
    qreal maxY = std::numeric_limits<qreal>::lowest();
    for (int i = 0; i < 21; ++i) {
        const float x = landmarks->values[static_cast<size_t>(i) * 3 + 0];
        const float y = landmarks->values[static_cast<size_t>(i) * 3 + 1];
        const QPointF point(landmarkInput.roi.left() + x * sx, landmarkInput.roi.top() + y * sy);
        result.landmarks[i] = point;
        minX = std::min(minX, point.x());
        minY = std::min(minY, point.y());
        maxX = std::max(maxX, point.x());
        maxY = std::max(maxY, point.y());
    }

    QRectF handBox(QPointF(minX, minY), QPointF(maxX, maxY));
    const qreal expandX = handBox.width() * 0.20;
    const qreal expandY = handBox.height() * 0.20;
    handBox.adjust(-expandX, -expandY, expandX, expandY);
    result.handBox = handBox.intersected(QRectF(QPointF(0.0, 0.0), QSizeF(frameSize)));
    return result;
}

} // namespace

TensorRtHandPoseBackend::TensorRtHandPoseBackend() = default;

TensorRtHandPoseBackend::~TensorRtHandPoseBackend() = default;

bool TensorRtHandPoseBackend::initialize(const QString &modelDir, QString *error)
{
    QMutexLocker locker(&m_mutex);
    const QDir dir(modelDir);
    const QString palmOnnx = dir.absoluteFilePath(QStringLiteral("palm_detector.onnx"));
    const QString landmarkOnnx = dir.absoluteFilePath(QStringLiteral("hand_landmark.onnx"));

    m_palmRunner = std::make_unique<TensorRtRunner>();
    if (!m_palmRunner->initialize(palmOnnx, error)) {
        m_statusText = error ? *error : QStringLiteral("手掌检测模型加载失败");
        return false;
    }
    m_landmarkRunner = std::make_unique<TensorRtRunner>();
    if (!m_landmarkRunner->initialize(landmarkOnnx, error)) {
        m_statusText = error ? *error : QStringLiteral("手部关键点模型加载失败");
        return false;
    }

    m_ready = true;
    m_statusText = QStringLiteral("手部骨架 TensorRT 已就绪");
    return true;
}

bool TensorRtHandPoseBackend::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

QString TensorRtHandPoseBackend::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_statusText;
}

QVector<HandPoseResult> TensorRtHandPoseBackend::infer(const QImage &rgbFrame, int cameraId, qint64 timestampMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_ready || !m_palmRunner || !m_landmarkRunner || rgbFrame.isNull()) {
        return {};
    }

    QString error;
    std::vector<TensorRtOutput> palmOutputs;
    const QSize palmSize = m_palmRunner->inputImageSize().isValid() ? m_palmRunner->inputImageSize() : QSize(192, 192);
    const PalmPreprocess palmInput = preprocessPalm(rgbFrame, palmSize);
    if (!m_palmRunner->infer(palmInput.tensor, &palmOutputs, &error)) {
        m_statusText = QStringLiteral("手掌检测失败：%1").arg(error);
        qWarning() << "[HandPose]" << m_statusText;
        return {};
    }

    const QVector<PalmDetection> palms = decodePalmDetections(palmOutputs, palmInput, rgbFrame.size());
    if (palms.isEmpty()) {
        m_statusText = QStringLiteral("手部骨架 TensorRT 运行中：未检测到手");
        return {};
    }

    QVector<HandPoseResult> results;
    const QSize landmarkSize = m_landmarkRunner->inputImageSize().isValid() ? m_landmarkRunner->inputImageSize() : QSize(224, 224);
    for (const PalmDetection &palm : palms) {
        LandmarkInput landmarkInput;
        if (!makeLandmarkInput(rgbFrame, palm, landmarkSize, &landmarkInput)) {
            continue;
        }

        std::vector<TensorRtOutput> landmarkOutputs;
        if (!m_landmarkRunner->infer(landmarkInput.tensor, &landmarkOutputs, &error)) {
            m_statusText = QStringLiteral("手部关键点失败：%1").arg(error);
            qWarning() << "[HandPose]" << m_statusText;
            return {};
        }

        HandPoseResult result = decodeLandmarkResult(landmarkOutputs,
                                                     palm,
                                                     landmarkInput,
                                                     rgbFrame.size(),
                                                     cameraId,
                                                     timestampMs);
        if (result.landmarks.size() == 21) {
            results.push_back(result);
        }
    }

    m_statusText = results.isEmpty()
                       ? QStringLiteral("手部骨架 TensorRT 运行中：手部置信度不足")
                       : QStringLiteral("手部骨架 TensorRT 运行中：%1 只手").arg(results.size());
    return results;
}
