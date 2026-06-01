#ifndef ACTIONSTANDARDSCORER_H
#define ACTIONSTANDARDSCORER_H

#include "poseresult.h"
#include "trainingdomain.h"

#include <QStringList>

struct PoseStandardnessResult;

class ActionStandardScorer
{
public:
    ActionAssessment score(const PoseStandardnessResult &baseResult,
                           const ActionStandard &standard) const;
};

class ActionRepetitionTracker
{
public:
    void reset(const ActionStandard &standard);
    void clear();
    bool update(const PoseFrameResult &poseFrame,
                const ActionAssessment &assessment,
                qint64 nowMsec,
                qint64 sessionStartMsec,
                ActionRepetition *completedRepetition);

private:
    void beginRepetition(int relativeMs, const PoseFrameResult &poseFrame, const ActionAssessment &assessment);
    void accumulate(int relativeMs, const PoseFrameResult &poseFrame, const ActionAssessment &assessment);
    ActionRepetition complete(int relativeMs, const ActionAssessment &assessment);

    ActionStandard m_standard;
    bool m_hasStandard = false;
    bool m_armed = false;
    int m_startedMs = 0;
    int m_frameCount = 0;
    int m_scoreSum = 0;
    int m_detectionSum = 0;
    int m_symmetrySum = 0;
    int m_balanceSum = 0;
    int m_stabilitySum = 0;
    int m_depthSum = 0;
    int m_lowestScore = 101;
    int m_keyFrameMs = 0;
    PoseFrameResult m_keyFramePoseFrame;
    QString m_feedback;
    QStringList m_issueTitles;
    qint64 m_lastCompletedMsec = 0;
};

#endif // ACTIONSTANDARDSCORER_H
