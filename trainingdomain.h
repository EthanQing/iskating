#ifndef TRAININGDOMAIN_H
#define TRAININGDOMAIN_H

#include <QDateTime>
#include <QString>
#include <QVector>

struct AthleteProfile
{
    QString id;
    QString name;
    QString code;
    QString ageGroup;
    double heightCm = 0.0;
    double weightKg = 0.0;
    QString discipline;
    QString level;
    QString preferredRotation;
    QString preferredTakeoffFoot;
    QString injuryNotes;
    QString goals;
};

struct CoachProfile
{
    QString id;
    QString name;
    QString code;
};

struct ActionStandard
{
    QString id;
    QString code;
    QString name;
    QString categoryId;
    QString categoryName;
    QString level;
    QString purpose;
    int version = 1;
    int targetReps = 10;
    int targetScore = 80;
    int setCount = 1;
    int restSeconds = 60;
    double armThreshold = 0.28;
    double releaseThreshold = 0.18;
    int debounceMs = 900;
    double detectionWeight = 0.28;
    double symmetryWeight = 0.18;
    double balanceWeight = 0.22;
    double stabilityWeight = 0.17;
    double depthWeight = 0.15;
    int detectionMin = 55;
    int symmetryMin = 60;
    int balanceMin = 60;
    int stabilityMin = 60;
    int depthMin = 55;
    QString phases;
    QString keyPoints;
    QString issueTitle;
    QString issueBodyPart;
    QString issueCause;
    QString issueCorrection;
    int issuePriority = 2;
};

struct TrainingTask
{
    QString id;
    QString planId;
    QString actionStandardId;
    int standardVersion = 1;
    int targetReps = 10;
    int targetScore = 80;
    int setCount = 1;
    int restSeconds = 60;
    QString status;
};

struct ActionIssue
{
    QString title;
    QString bodyPart;
    QString cause;
    QString correction;
    int priority = 2;
};

struct ActionAssessment
{
    int score = 0;
    int detectionScore = 0;
    int symmetryScore = 0;
    int balanceScore = 0;
    int stabilityScore = 0;
    int depthScore = 0;
    QString feedback;
    QVector<ActionIssue> issues;
    bool valid = false;
};

struct ActionRepetition
{
    QString id;
    QString sessionId;
    QString actionStandardId;
    int standardVersion = 1;
    int startedMs = 0;
    int endedMs = 0;
    bool valid = false;
    int score = 0;
    int detectionScore = 0;
    int symmetryScore = 0;
    int balanceScore = 0;
    int stabilityScore = 0;
    int depthScore = 0;
    QString errorCodes;
    QString feedback;
    int keyFrameMs = 0;
};

struct TrainingSession
{
    QString id;
    QString athleteId;
    QString coachId;
    QString planId;
    QString taskId;
    QString actionStandardId;
    int standardVersion = 1;
    qint64 legacyQsettingsId = 0;
    QDateTime startedAt;
    QDateTime savedAt;
    int durationSec = 0;
    int totalReps = 0;
    int validReps = 0;
    int averageScore = 0;
    int bestScore = 0;
    int camera = 1;
    QString modelPrecision;
    int fps = 30;
    int detectionScore = 0;
    int symmetryScore = 0;
    int balanceScore = 0;
    int stabilityScore = 0;
    int depthScore = 0;
    QString site;
    QString trainingPhase;
    QString goal;
    int targetReps = 0;
    int targetScore = 0;
    int setCount = 1;
    int restSeconds = 60;
    QString feedback;
    QString notes;
};

struct SessionHistoryItem
{
    QString id;
    QString athleteName;
    QString coachName;
    QString actionName;
    QString actionCategory;
    int standardVersion = 1;
    QString time;
    int duration = 0;
    int totalReps = 0;
    int validReps = 0;
    int targetReps = 0;
    int targetScore = 0;
    int score = 0;
    int bestScore = 0;
    int camera = 1;
    QString modelPrecision;
    int fps = 30;
    int detectionScore = 0;
    int symmetryScore = 0;
    int balanceScore = 0;
    int stabilityScore = 0;
    int depthScore = 0;
    QString site;
    QString trainingPhase;
    QString goal;
    QString feedback;
};

struct TrainingBaseline
{
    int sessionCount = 0;
    int averageScore = 0;
    int averageValidReps = 0;
};

#endif // TRAININGDOMAIN_H
