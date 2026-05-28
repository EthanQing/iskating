#include "posestandardnessscorer.h"

#include <QLineF>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

const PoseInstance *primaryInstance(const PoseFrameResult &frame)
{
    const PoseInstance *best = nullptr;
    for (const PoseInstance &instance : frame.instances) {
        if (!best || instance.confidence > best->confidence) {
            best = &instance;
        }
    }
    return best;
}

const PoseKeypoint *keypointAt(const PoseInstance &instance, int index)
{
    if (index < 0 || index >= instance.keypoints.size()) {
        return nullptr;
    }
    const PoseKeypoint &keypoint = instance.keypoints.at(index);
    return keypoint.valid ? &keypoint : nullptr;
}

int validKeypointCount(const PoseInstance &instance)
{
    int count = 0;
    for (const PoseKeypoint &keypoint : instance.keypoints) {
        if (keypoint.valid && keypoint.confidence > 0.0f) {
            ++count;
        }
    }
    return count;
}

float averageConfidence(const PoseInstance &instance)
{
    float total = 0.0f;
    int count = 0;
    for (const PoseKeypoint &keypoint : instance.keypoints) {
        if (keypoint.valid) {
            total += std::clamp(keypoint.confidence, 0.0f, 1.0f);
            ++count;
        }
    }
    return count > 0 ? total / count : 0.0f;
}

float expectedPointCount(PoseSkeletonType skeletonType)
{
    switch (skeletonType) {
    case PoseSkeletonType::Hand21:
        return 21.0f;
    case PoseSkeletonType::Body17:
    case PoseSkeletonType::FullBody3D:
        return 17.0f;
    case PoseSkeletonType::Body25:
        return 25.0f;
    case PoseSkeletonType::Body33:
        return 33.0f;
    case PoseSkeletonType::Unknown:
    default:
        return 1.0f;
    }
}

int scoreFromError(float error, float tolerance)
{
    return std::clamp(qRound(100.0f * (1.0f - error / std::max(0.001f, tolerance))), 0, 100);
}

int detectionScore(const PoseInstance &instance)
{
    const float completeness = std::clamp(validKeypointCount(instance) / expectedPointCount(instance.skeletonType), 0.0f, 1.0f);
    const float confidence = averageConfidence(instance);
    return std::clamp(qRound(100.0f * (0.62f * confidence + 0.38f * completeness)), 0, 100);
}

int symmetryScore(const PoseInstance &instance)
{
    const PoseKeypoint *leftShoulder = keypointAt(instance, 5);
    const PoseKeypoint *rightShoulder = keypointAt(instance, 6);
    const PoseKeypoint *leftHip = keypointAt(instance, 11);
    const PoseKeypoint *rightHip = keypointAt(instance, 12);
    if (!leftShoulder || !rightShoulder || !leftHip || !rightHip) {
        return 45;
    }

    const float shoulderWidth = static_cast<float>(QLineF(leftShoulder->imagePoint, rightShoulder->imagePoint).length());
    const float hipWidth = static_cast<float>(QLineF(leftHip->imagePoint, rightHip->imagePoint).length());
    const float scale = std::max(1.0f, std::max(shoulderWidth, hipWidth));
    return scoreFromError(std::abs(shoulderWidth - hipWidth) / scale, 0.55f);
}

int balanceScore(const PoseInstance &instance)
{
    const PoseKeypoint *leftShoulder = keypointAt(instance, 5);
    const PoseKeypoint *rightShoulder = keypointAt(instance, 6);
    const PoseKeypoint *leftHip = keypointAt(instance, 11);
    const PoseKeypoint *rightHip = keypointAt(instance, 12);
    if (!leftShoulder || !rightShoulder || !leftHip || !rightHip) {
        return 45;
    }

    const QPointF shoulderCenter = (leftShoulder->imagePoint + rightShoulder->imagePoint) * 0.5;
    const QPointF hipCenter = (leftHip->imagePoint + rightHip->imagePoint) * 0.5;
    const float bodyScale = std::max(1.0, QLineF(shoulderCenter, hipCenter).length());
    const float lateralOffset = static_cast<float>(std::abs(shoulderCenter.x() - hipCenter.x()) / bodyScale);
    return scoreFromError(lateralOffset, 0.65f);
}

int depthScore(const PoseInstance &instance)
{
    int count = 0;
    float zSpan = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    for (const PoseKeypoint &keypoint : instance.keypoints) {
        if (!keypoint.valid || !keypoint.hasPoint3d) {
            continue;
        }
        if (count == 0) {
            minZ = maxZ = keypoint.point3d.z();
        } else {
            minZ = std::min(minZ, keypoint.point3d.z());
            maxZ = std::max(maxZ, keypoint.point3d.z());
        }
        ++count;
    }
    zSpan = maxZ - minZ;
    if (count < 8) {
        return 35;
    }
    return std::clamp(65 + qRound(std::min(1.0f, zSpan / 0.22f) * 35.0f), 0, 100);
}

int stabilityScore(const PoseFrameResult &current, const PoseFrameResult &previous)
{
    const PoseInstance *a = primaryInstance(current);
    const PoseInstance *b = primaryInstance(previous);
    if (!a || !b || current.timestampMs <= 0 || previous.timestampMs <= 0) {
        return 80;
    }

    float totalMotion = 0.0f;
    int count = 0;
    const int n = std::min(a->keypoints.size(), b->keypoints.size());
    const float scale = std::max(1.0, std::max(a->box.width(), a->box.height()));
    for (int i = 0; i < n; ++i) {
        const PoseKeypoint &ka = a->keypoints.at(i);
        const PoseKeypoint &kb = b->keypoints.at(i);
        if (!ka.valid || !kb.valid) {
            continue;
        }
        totalMotion += static_cast<float>(QLineF(ka.imagePoint, kb.imagePoint).length() / scale);
        ++count;
    }
    if (count < 6) {
        return 55;
    }

    const float avgMotion = totalMotion / count;
    return scoreFromError(avgMotion, 0.18f);
}

QString feedbackForScores(const PoseStandardnessResult &result)
{
    if (!result.valid) {
        return QStringLiteral("等待姿态");
    }
    if (result.detectionScore < 55) {
        return QStringLiteral("关键点不足");
    }
    if (result.balanceScore < 60) {
        return QStringLiteral("重心偏移");
    }
    if (result.symmetryScore < 60) {
        return QStringLiteral("左右不对称");
    }
    if (result.stabilityScore < 60) {
        return QStringLiteral("动作波动大");
    }
    if (result.depthScore < 60) {
        return QStringLiteral("3D深度不足");
    }
    return result.score >= 85 ? QStringLiteral("姿态稳定") : QStringLiteral("姿态基本稳定");
}

} // namespace

PoseStandardnessResult PoseStandardnessScorer::scoreFrame(const PoseFrameResult &frame) const
{
    PoseStandardnessResult result;
    const PoseInstance *instance = primaryInstance(frame);
    if (!instance) {
        result.feedback = QStringLiteral("等待姿态");
        return result;
    }

    result.detectionScore = detectionScore(*instance);
    result.symmetryScore = symmetryScore(*instance);
    result.balanceScore = balanceScore(*instance);
    result.stabilityScore = stabilityScore(frame, m_previousFrame);
    result.depthScore = depthScore(*instance);
    result.score = std::clamp(qRound(result.detectionScore * 0.28f
                                     + result.symmetryScore * 0.18f
                                     + result.balanceScore * 0.22f
                                     + result.stabilityScore * 0.17f
                                     + result.depthScore * 0.15f),
                              0,
                              100);
    result.valid = true;
    result.feedback = feedbackForScores(result);
    m_previousFrame = frame;
    return result;
}
