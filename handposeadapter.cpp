#include "handposeadapter.h"

#include <QStringList>

namespace {

PoseInstanceKind kindForHand(Handedness handedness)
{
    switch (handedness) {
    case Handedness::Left:
        return PoseInstanceKind::LeftHand;
    case Handedness::Right:
        return PoseInstanceKind::RightHand;
    case Handedness::Unknown:
    default:
        return PoseInstanceKind::Unknown;
    }
}

QString handKeypointName(int index)
{
    static const QStringList names = {
        QStringLiteral("wrist"),
        QStringLiteral("thumb_cmc"),
        QStringLiteral("thumb_mcp"),
        QStringLiteral("thumb_ip"),
        QStringLiteral("thumb_tip"),
        QStringLiteral("index_mcp"),
        QStringLiteral("index_pip"),
        QStringLiteral("index_dip"),
        QStringLiteral("index_tip"),
        QStringLiteral("middle_mcp"),
        QStringLiteral("middle_pip"),
        QStringLiteral("middle_dip"),
        QStringLiteral("middle_tip"),
        QStringLiteral("ring_mcp"),
        QStringLiteral("ring_pip"),
        QStringLiteral("ring_dip"),
        QStringLiteral("ring_tip"),
        QStringLiteral("pinky_mcp"),
        QStringLiteral("pinky_pip"),
        QStringLiteral("pinky_dip"),
        QStringLiteral("pinky_tip"),
    };
    return index >= 0 && index < names.size() ? names.at(index) : QStringLiteral("point_%1").arg(index);
}

} // namespace

PoseFrameResult handPoseResultsToPoseFrame(const QVector<HandPoseResult> &hands)
{
    PoseFrameResult frame;
    frame.skeletonType = PoseSkeletonType::Hand21;
    frame.sourceName = QStringLiteral("hand_pose");
    if (hands.isEmpty()) {
        return frame;
    }

    frame.cameraId = hands.first().cameraId;
    frame.timestampMs = hands.first().timestampMs;
    frame.frameSize = hands.first().frameSize;
    frame.instances.reserve(hands.size());
    for (int handIndex = 0; handIndex < hands.size(); ++handIndex) {
        const HandPoseResult &hand = hands.at(handIndex);
        if (hand.landmarks.isEmpty()) {
            continue;
        }

        PoseInstance instance;
        instance.trackId = handIndex;
        instance.skeletonType = PoseSkeletonType::Hand21;
        instance.kind = kindForHand(hand.handedness);
        instance.box = hand.handBox;
        instance.confidence = hand.confidence;
        instance.keypoints.reserve(hand.landmarks.size());
        for (int i = 0; i < hand.landmarks.size(); ++i) {
            PoseKeypoint keypoint;
            keypoint.index = i;
            keypoint.name = handKeypointName(i);
            keypoint.imagePoint = hand.landmarks.at(i);
            keypoint.confidence = hand.confidence;
            keypoint.valid = true;
            instance.keypoints.push_back(keypoint);
        }
        frame.instances.push_back(instance);
    }

    return frame;
}
