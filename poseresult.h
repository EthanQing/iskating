#ifndef POSERESULT_H
#define POSERESULT_H

#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <QVector3D>

enum class PoseSkeletonType {
    Unknown,
    Hand21,
    Body17,
    Body25,
    Body33,
    FullBody3D,
};

enum class PoseInstanceKind {
    Unknown,
    Person,
    LeftHand,
    RightHand,
};

struct PoseKeypoint
{
    int index = -1;
    QString name;
    QPointF imagePoint;
    QVector3D point3d;
    float confidence = 0.0f;
    bool valid = false;
    bool hasPoint3d = false;
};

struct PoseInstance
{
    int trackId = -1;
    PoseSkeletonType skeletonType = PoseSkeletonType::Unknown;
    PoseInstanceKind kind = PoseInstanceKind::Unknown;
    QVector<PoseKeypoint> keypoints;
    QRectF box;
    float confidence = 0.0f;
};

struct PoseFrameResult
{
    int cameraId = 0;
    qint64 timestampMs = 0;
    QSizeF frameSize;
    PoseSkeletonType skeletonType = PoseSkeletonType::Unknown;
    QVector<PoseInstance> instances;
    QString sourceName;
};

QVector<QPair<int, int>> poseSkeletonBones(PoseSkeletonType skeletonType);
QString poseSkeletonTypeName(PoseSkeletonType skeletonType);
QString poseInstanceKindName(PoseInstanceKind kind);

#endif // POSERESULT_H
