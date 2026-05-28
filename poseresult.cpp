#include "poseresult.h"

QVector<QPair<int, int>> poseSkeletonBones(PoseSkeletonType skeletonType)
{
    switch (skeletonType) {
    case PoseSkeletonType::Hand21:
        return {
            {0, 1},
            {1, 2},
            {2, 3},
            {3, 4},
            {0, 5},
            {5, 6},
            {6, 7},
            {7, 8},
            {0, 9},
            {9, 10},
            {10, 11},
            {11, 12},
            {0, 13},
            {13, 14},
            {14, 15},
            {15, 16},
            {0, 17},
            {17, 18},
            {18, 19},
            {19, 20},
        };
    case PoseSkeletonType::Body17:
        return {
            {5, 7},
            {7, 9},
            {6, 8},
            {8, 10},
            {5, 6},
            {5, 11},
            {6, 12},
            {11, 12},
            {11, 13},
            {13, 15},
            {12, 14},
            {14, 16},
            {0, 1},
            {0, 2},
            {1, 3},
            {2, 4},
        };
    case PoseSkeletonType::Body25:
        return {
            {1, 8},
            {1, 2},
            {2, 3},
            {3, 4},
            {1, 5},
            {5, 6},
            {6, 7},
            {8, 9},
            {9, 10},
            {10, 11},
            {8, 12},
            {12, 13},
            {13, 14},
            {1, 0},
            {0, 15},
            {15, 17},
            {0, 16},
            {16, 18},
        };
    case PoseSkeletonType::Body33:
    case PoseSkeletonType::FullBody3D:
        return {
            {11, 12},
            {11, 13},
            {13, 15},
            {12, 14},
            {14, 16},
            {11, 23},
            {12, 24},
            {23, 24},
            {23, 25},
            {25, 27},
            {24, 26},
            {26, 28},
            {27, 29},
            {29, 31},
            {28, 30},
            {30, 32},
            {0, 7},
            {0, 8},
            {7, 9},
            {8, 10},
        };
    case PoseSkeletonType::Unknown:
    default:
        return {};
    }
}

QString poseSkeletonTypeName(PoseSkeletonType skeletonType)
{
    switch (skeletonType) {
    case PoseSkeletonType::Hand21:
        return QStringLiteral("Hand21");
    case PoseSkeletonType::Body17:
        return QStringLiteral("Body17");
    case PoseSkeletonType::Body25:
        return QStringLiteral("Body25");
    case PoseSkeletonType::Body33:
        return QStringLiteral("Body33");
    case PoseSkeletonType::FullBody3D:
        return QStringLiteral("FullBody3D");
    case PoseSkeletonType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString poseInstanceKindName(PoseInstanceKind kind)
{
    switch (kind) {
    case PoseInstanceKind::Person:
        return QStringLiteral("Person");
    case PoseInstanceKind::LeftHand:
        return QStringLiteral("LeftHand");
    case PoseInstanceKind::RightHand:
        return QStringLiteral("RightHand");
    case PoseInstanceKind::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}
