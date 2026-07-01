#ifndef TRAININGDOMAIN_H
#define TRAININGDOMAIN_H

#include <QDateTime>
#include <QString>
#include <QVector>

enum class SessionSearchSortField
{
    SavedAt,
    AverageScore,
    BestScore,
    ValidReps,
    DurationSec
};

struct SessionSearchFilters
{
    QString athleteId;
    QString coachId;
    QString actionStandardId;
    QDateTime savedFrom;
    QDateTime savedTo;
    QString competitionText;
    int minScore = -1;
    int maxScore = -1;
};

struct SessionSearchPage
{
    int pageNumber = 1;
    int pageSize = 10;
};

struct SessionSearchSort
{
    SessionSearchSortField field = SessionSearchSortField::SavedAt;
    bool descending = true;
};

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
    bool active = true;
};

struct CoachProfile
{
    QString id;
    QString name;
    QString code;
    QString specialty;
    QString phone;
    QString notes;
    bool active = true;
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
    QString referenceVideoSource;
    QString referenceRepetitionId;
    QString referenceNotes;
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
    int videoClipStartMs = 0;
    int videoClipEndMs = 0;
    QString source = QStringLiteral("ai");
    QString reviewStatus = QStringLiteral("unreviewed");
    QString reviewerCoachId;
    QDateTime reviewedAt;
    int manualStartedMs = -1;
    int manualEndedMs = -1;
    int manualValid = -1;
    int manualScore = -1;
    int manualDetectionScore = -1;
    int manualSymmetryScore = -1;
    int manualBalanceScore = -1;
    int manualStabilityScore = -1;
    int manualDepthScore = -1;
    QString manualErrorCodes;
    QString manualFeedback;
    QString coachNote;
    QString keyFramePoseJson;

    bool hasManualReview() const
    {
        return reviewStatus == QStringLiteral("reviewed")
               || source == QStringLiteral("coach")
               || manualStartedMs >= 0
               || manualEndedMs >= 0
               || manualValid >= 0
               || manualScore >= 0
               || manualDetectionScore >= 0
               || manualSymmetryScore >= 0
               || manualBalanceScore >= 0
               || manualStabilityScore >= 0
               || manualDepthScore >= 0
               || !manualErrorCodes.trimmed().isEmpty()
               || !manualFeedback.trimmed().isEmpty()
               || !coachNote.trimmed().isEmpty();
    }

    int effectiveStartedMs() const { return manualStartedMs >= 0 ? manualStartedMs : startedMs; }
    int effectiveEndedMs() const { return manualEndedMs >= 0 ? manualEndedMs : endedMs; }
    bool effectiveValid() const { return manualValid >= 0 ? manualValid != 0 : valid; }
    int effectiveScore() const { return manualScore >= 0 ? manualScore : score; }
    int effectiveDetectionScore() const { return manualDetectionScore >= 0 ? manualDetectionScore : detectionScore; }
    int effectiveSymmetryScore() const { return manualSymmetryScore >= 0 ? manualSymmetryScore : symmetryScore; }
    int effectiveBalanceScore() const { return manualBalanceScore >= 0 ? manualBalanceScore : balanceScore; }
    int effectiveStabilityScore() const { return manualStabilityScore >= 0 ? manualStabilityScore : stabilityScore; }
    int effectiveDepthScore() const { return manualDepthScore >= 0 ? manualDepthScore : depthScore; }
    QString effectiveErrorCodes() const
    {
        return manualErrorCodes.trimmed().isEmpty() ? errorCodes : manualErrorCodes;
    }
    QString effectiveFeedback() const
    {
        return manualFeedback.trimmed().isEmpty() ? feedback : manualFeedback;
    }
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
    QString videoSource;
    QString videoFallbackSource;
    QString videoCameraName;
    QString feedback;
    QString notes;
    QString coachComment;
};

struct SessionHistoryItem
{
    QString id;
    QString athleteId;
    QString coachId;
    QString planId;
    QString taskId;
    QString actionStandardId;
    QString athleteName;
    QString coachName;
    QString actionName;
    QString actionCategory;
    int standardVersion = 1;
    QString time;
    QDateTime startedAt;
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
    QString videoSource;
    QString videoFallbackSource;
    QString videoCameraName;
    QString feedback;
    QString notes;
    QString coachComment;
};

struct TrainingBaseline
{
    int sessionCount = 0;
    int averageScore = 0;
    int averageValidReps = 0;
};

struct TrainingTrendWindow
{
    int days = 0;
    int sessionCount = 0;
    int averageScore = 0;
    int bestScore = 0;
    int completedReps = 0;
    int detectionScore = 0;
    int symmetryScore = 0;
    int balanceScore = 0;
    int stabilityScore = 0;
    int depthScore = 0;
    QString weakestMetricName;
    int weakestMetricScore = 0;
};

struct SessionSearchResult
{
    QVector<SessionHistoryItem> items;
    int totalCount = 0;
    int pageNumber = 1;
    int pageSize = 10;
};

#endif // TRAININGDOMAIN_H
