#ifndef POSESTANDARDNESSSCORER_H
#define POSESTANDARDNESSSCORER_H

#include "poseresult.h"

#include <QString>

struct PoseStandardnessResult
{
    int score = 0;
    int detectionScore = 0;
    int symmetryScore = 0;
    int balanceScore = 0;
    int stabilityScore = 0;
    int depthScore = 0;
    QString feedback;
    bool valid = false;
};

class PoseStandardnessScorer
{
public:
    PoseStandardnessResult scoreFrame(const PoseFrameResult &frame) const;

private:
    mutable PoseFrameResult m_previousFrame;
};

#endif // POSESTANDARDNESSSCORER_H
