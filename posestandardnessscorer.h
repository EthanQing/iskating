#ifndef POSESTANDARDNESSSCORER_H
#define POSESTANDARDNESSSCORER_H

#include "poseresult.h"

#include <QString>

struct PoseStandardnessResult
{
    int score = 0;
    QString feedback;
    bool valid = false;
};

class PoseStandardnessScorer
{
public:
    PoseStandardnessResult scoreFrame(const PoseFrameResult &frame) const;
};

#endif // POSESTANDARDNESSSCORER_H
