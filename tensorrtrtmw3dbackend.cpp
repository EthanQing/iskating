#include "tensorrtrtmw3dbackend.h"

#include "tensorrtrunner.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMutexLocker>
#include <QPainter>
#include <QRect>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr int kBodyKeypointCount = 17;
constexpr int kWholeBodyKeypointCount = 133;
constexpr int kInputWidth = 288;
constexpr int kInputHeight = 384;
constexpr float kBoxPaddingScale = 1.25f;
constexpr float kSimccSplitRatio = 2.0f;
constexpr float kZCenter = 144.0f;

const std::array<float, 3> kImageNetMean = {123.675f, 116.28f, 103.53f};
const std::array<float, 3> kImageNetStd = {58.395f, 57.12f, 57.375f};

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

struct CropTransform
{
    QRectF sourceRect;
    QSize inputSize = QSize(kInputWidth, kInputHeight);
};

QRectF expandedAspectBox(const QRectF &box, const QSize &frameSize)
{
    const QRectF frameRect(QPointF(0.0, 0.0), QSizeF(frameSize));
    QRectF normalized = box.normalized().intersected(frameRect);
    if (!normalized.isValid() || normalized.width() < 2.0 || normalized.height() < 2.0) {
        normalized = frameRect;
    }

    const QPointF center = normalized.center();
    const qreal targetAspect = static_cast<qreal>(kInputWidth) / static_cast<qreal>(kInputHeight);
    qreal width = normalized.width() * kBoxPaddingScale;
    qreal height = normalized.height() * kBoxPaddingScale;
    if (width / std::max<qreal>(1.0, height) > targetAspect) {
        height = width / targetAspect;
    } else {
        width = height * targetAspect;
    }

    QRectF expanded(center.x() - width * 0.5,
                    center.y() - height * 0.5,
                    width,
                    height);
    return expanded.intersected(frameRect);
}

QImage cropToInput(const QImage &image, const CropTransform &transform)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    QImage crop(transform.inputSize, QImage::Format_RGB888);
    crop.fill(Qt::black);

    QPainter painter(&crop);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(QPoint(0, 0), transform.inputSize), rgb, transform.sourceRect);
    painter.end();
    return crop;
}

std::vector<float> imageToRtmw3dNchw(const QImage &image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    const int width = rgb.width();
    const int height = rgb.height();
    const int planeSize = width * height;
    std::vector<float> result(static_cast<size_t>(planeSize * 3), 0.0f);
    for (int y = 0; y < height; ++y) {
        const uchar *line = rgb.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const int pixelIndex = y * width + x;
            for (int c = 0; c < 3; ++c) {
                const float value = static_cast<float>(line[x * 3 + c]);
                result[static_cast<size_t>(c * planeSize + pixelIndex)] = (value - kImageNetMean[static_cast<size_t>(c)])
                                                                          / kImageNetStd[static_cast<size_t>(c)];
            }
        }
    }
    return result;
}

const TensorRtOutput *findSimccOutput(const std::vector<TensorRtOutput> &outputs,
                                      int expectedAxisBins,
                                      const TensorRtOutput *exclude = nullptr)
{
    for (const TensorRtOutput &output : outputs) {
        if (&output == exclude) {
            continue;
        }
        const int valueCount = static_cast<int>(output.values.size());
        if (valueCount == kWholeBodyKeypointCount * expectedAxisBins) {
            return &output;
        }
        if (output.dims.nbDims >= 3
            && static_cast<int>(output.dims.d[1]) == kWholeBodyKeypointCount
            && static_cast<int>(output.dims.d[2]) == expectedAxisBins) {
            return &output;
        }
    }
    return nullptr;
}

int argmaxIndex(const TensorRtOutput &output, int keypointIndex, int bins, float *score)
{
    const size_t base = static_cast<size_t>(keypointIndex * bins);
    int bestIndex = 0;
    float bestValue = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < bins; ++i) {
        const float value = output.values.at(base + static_cast<size_t>(i));
        if (value > bestValue) {
            bestValue = value;
            bestIndex = i;
        }
    }
    if (score) {
        *score = bestValue;
    }
    return bestIndex;
}

QPointF mapCropPointToFrame(float x, float y, const CropTransform &transform)
{
    const qreal px = transform.sourceRect.left()
                     + x / std::max<qreal>(1.0, transform.inputSize.width()) * transform.sourceRect.width();
    const qreal py = transform.sourceRect.top()
                     + y / std::max<qreal>(1.0, transform.inputSize.height()) * transform.sourceRect.height();
    return QPointF(px, py);
}

PoseInstance *primaryPerson(PoseFrameResult *frame)
{
    if (!frame) {
        return nullptr;
    }
    PoseInstance *best = nullptr;
    for (PoseInstance &instance : frame->instances) {
        if (instance.kind != PoseInstanceKind::Person) {
            continue;
        }
        if (!best || instance.confidence > best->confidence) {
            best = &instance;
        }
    }
    return best;
}

} // namespace

TensorRtRtmw3dBackend::TensorRtRtmw3dBackend() = default;

TensorRtRtmw3dBackend::~TensorRtRtmw3dBackend() = default;

void TensorRtRtmw3dBackend::setStatusCallback(StatusCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_statusCallback = std::move(callback);
}

bool TensorRtRtmw3dBackend::initialize(const QString &modelDir, QString *error)
{
    const QDir dir(modelDir);
    const QString onnxPath = dir.absoluteFilePath(QStringLiteral("rtmw3d-x.onnx"));
    if (!QFileInfo::exists(onnxPath)) {
        const QString status = QStringLiteral("RTMW3D模型缺失：%1").arg(onnxPath);
        QMutexLocker locker(&m_mutex);
        m_statusText = status;
        if (error) {
            *error = status;
        }
        return false;
    }

    auto runner = std::make_unique<TensorRtRunner>();
    runner->setStatusCallback([this](const QString &status) {
        StatusCallback callback;
        {
            QMutexLocker locker(&m_mutex);
            m_statusText = QStringLiteral("RTMW3D：%1").arg(status);
            callback = m_statusCallback;
        }
        if (callback) {
            callback(m_statusText);
        }
    });
    if (!runner->initialize(onnxPath, error)) {
        QMutexLocker locker(&m_mutex);
        m_statusText = error ? *error : QStringLiteral("RTMW3D模型加载失败");
        return false;
    }

    QMutexLocker locker(&m_mutex);
    m_runner = std::move(runner);
    m_ready = true;
    m_statusText = QStringLiteral("RTMW3D TensorRT 已就绪");
    qDebug() << "[RTMW3D]" << m_statusText << m_runner->ioSummary();
    return true;
}

bool TensorRtRtmw3dBackend::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

QString TensorRtRtmw3dBackend::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_statusText;
}

bool TensorRtRtmw3dBackend::infer(const QImage &rgbFrame, PoseFrameResult *frame, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_ready || !m_runner) {
        if (error) {
            *error = m_statusText;
        }
        return false;
    }
    if (!frame || frame->instances.isEmpty() || rgbFrame.isNull()) {
        if (error) {
            *error = QStringLiteral("没有可用于RTMW3D的人体框");
        }
        return false;
    }

    PoseInstance *person = primaryPerson(frame);
    if (!person) {
        if (error) {
            *error = QStringLiteral("没有可用于RTMW3D的人体实例");
        }
        return false;
    }

    CropTransform transform;
    transform.sourceRect = expandedAspectBox(person->box, rgbFrame.size());
    const QImage inputImage = cropToInput(rgbFrame, transform);
    const std::vector<float> inputTensor = imageToRtmw3dNchw(inputImage);

    std::vector<TensorRtOutput> outputs;
    QString inferError;
    if (!m_runner->infer(inputTensor, &outputs, &inferError)) {
        m_statusText = QStringLiteral("RTMW3D推理失败：%1").arg(inferError);
        if (error) {
            *error = m_statusText;
        }
        return false;
    }

    const TensorRtOutput *xOutput = findSimccOutput(outputs, 576);
    const TensorRtOutput *yOutput = findSimccOutput(outputs, 768);
    const TensorRtOutput *zOutput = findSimccOutput(outputs, 576, xOutput);
    if (!xOutput || !yOutput || !zOutput) {
        m_statusText = QStringLiteral("RTMW3D输出维度不符合SimCC 133点");
        if (error) {
            *error = m_statusText;
        }
        return false;
    }

    const QStringList &names = bodyKeypointNames();
    if (person->keypoints.size() < kBodyKeypointCount) {
        person->keypoints.resize(kBodyKeypointCount);
    }

    for (int i = 0; i < kBodyKeypointCount; ++i) {
        float xScore = 0.0f;
        float yScore = 0.0f;
        float zScore = 0.0f;
        const float x = argmaxIndex(*xOutput, i, 576, &xScore) / kSimccSplitRatio;
        const float y = argmaxIndex(*yOutput, i, 768, &yScore) / kSimccSplitRatio;
        const float z = argmaxIndex(*zOutput, i, 576, &zScore) / kSimccSplitRatio - kZCenter;
        const QPointF imagePoint = mapCropPointToFrame(x, y, transform);

        PoseKeypoint &keypoint = person->keypoints[i];
        keypoint.index = i;
        keypoint.name = names.at(i);
        keypoint.imagePoint = imagePoint;
        keypoint.point3d = QVector3D(x - kInputWidth * 0.5f, -(y - kInputHeight * 0.5f), z);
        keypoint.confidence = std::max(0.0f, std::min({xScore, yScore, zScore}));
        keypoint.valid = imagePoint.x() >= 0.0
                         && imagePoint.y() >= 0.0
                         && imagePoint.x() <= rgbFrame.width()
                         && imagePoint.y() <= rgbFrame.height();
        keypoint.hasPoint3d = keypoint.valid;
    }

    person->skeletonType = PoseSkeletonType::Body17;
    frame->skeletonType = PoseSkeletonType::Body17;
    frame->sourceName = QStringLiteral("yolov8n_pose_body17+rtmw3d_x");
    m_statusText = QStringLiteral("RTMW3D TensorRT 运行中");
    return true;
}
