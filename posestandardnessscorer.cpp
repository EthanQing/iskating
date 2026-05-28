#include "posestandardnessscorer.h"

#include <QtMath>

#include <algorithm>

namespace {

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
        return 17.0f;
    case PoseSkeletonType::Body25:
        return 25.0f;
    case PoseSkeletonType::Body33:
    case PoseSkeletonType::FullBody3D:
        return 33.0f;
    case PoseSkeletonType::Unknown:
    default:
        return 1.0f;
    }
}

QString feedbackForScore(int score)
{
    if (score >= 90) {
        return QStringLiteral("姿态稳定");
    }
    if (score >= 75) {
        return QStringLiteral("姿态基本稳定");
    }
    if (score >= 55) {
        return QStringLiteral("关键点不完整");
    }
    return QStringLiteral("姿态识别不足");
}

} // namespace

PoseStandardnessResult PoseStandardnessScorer::scoreFrame(const PoseFrameResult &frame) const
{
    PoseStandardnessResult result;
    if (frame.instances.isEmpty()) {
        result.feedback = QStringLiteral("等待姿态");
        return result;
    }

    float bestScore = 0.0f;
    for (const PoseInstance &instance : frame.instances) {
        const float expected = expectedPointCount(instance.skeletonType);
        const float completeness = std::clamp(validKeypointCount(instance) / expected, 0.0f, 1.0f);
        const float confidence = averageConfidence(instance);
        const float instanceScore = 100.0f * (0.62f * confidence + 0.38f * completeness);
        bestScore = std::max(bestScore, instanceScore);
    }

    result.score = std::clamp(qRound(bestScore), 0, 100);
    result.feedback = feedbackForScore(result.score);
    result.valid = result.score > 0;
    return result;
}
