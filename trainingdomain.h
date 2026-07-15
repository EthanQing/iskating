#ifndef TRAININGDOMAIN_H
#define TRAININGDOMAIN_H

#include <QDate>
#include <QDateTime>
#include <QPair>
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
    QString competitionId;
    QString competitionEventId;
    QString eventAthleteId;
    QString sourceType;
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

struct AthleteIdentitySample
{
    QString id;
    QString athleteId;
    QString filePath;
    QString fileName;
    QString modelVersion;
    QString preprocessingVersion;
    int embeddingDimension = 0;
    QString dataBase64;
    QDateTime createdAt;
};

struct AthleteIdentityEmbedding
{
    QString sampleId;
    QString athleteId;
    QVector<float> embedding;
    int embeddingDimension = 0;
    QString modelVersion;
    QString preprocessingVersion;
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

struct Competition
{
    QString id;
    QString name;
    QString location;
    QDate competitionDate;
    QString competitionType;
    QString notes;
    bool active = true;
};

struct CompetitionEvent
{
    QString id;
    QString competitionId;
    QString raceName;
    QString eventName;
    QString heatName;
    QString groupName;
    QDateTime scheduledAt;
    QString notes;
    bool active = true;
};

struct EventAthlete
{
    QString id;
    QString eventId;
    QString athleteId;
    QString athleteName;
    QString bibNumber;
    QString laneNumber;
    int sortOrder = 0;
    int resultScore = -1;
    int resultRank = -1;
    QString notes;
    bool active = true;
};

struct TrainingSessionParticipant
{
    QString id;
    QString sessionId;
    QString athleteId;
    QString athleteName;
    int slotIndex = 1;
    QString role = QStringLiteral("participant");
    QString trackLabel;
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

struct TrainingVideoFile
{
    QString id;
    QString sessionId;
    int videoIndex = 1;
    int camera = 0;
    QString cameraName;
    QString sourceUrl;
    QString fallbackUrl;
    QString storageRoot;
    QString relativeDir;
    QString fileName;
    QString filePath;
    QString metadataPath;
    QString status = QStringLiteral("planned");
    int sessionStartMs = 0;
    int sessionEndMs = 0;
    int durationMs = 0;
    qint64 fileSizeBytes = -1;
    QDateTime fileModifiedAt;
    QString checksumAlgorithm;
    QString checksumValue;
    QString metadataJson;
};

struct VideoFileCleanupCandidate
{
    QString id;
    QString sessionId;
    int videoIndex = 1;
    QString athleteName;
    QDateTime sessionStartedAt;
    QString status;
    QString filePath;
    qint64 fileSizeBytes = -1;
    QDateTime fileModifiedAt;
    int actionCount = 0;
    QString metadataJson;
};

struct OfflineAnalysisTask
{
    QString id;
    QString batchId;
    int cameraId = 0;
    int timeOffsetMs = 0;
    QString videoPath;
    QString fileName;
    qint64 fileSizeBytes = -1;
    QDateTime fileModifiedAt;
    int durationMs = 0;
    QString status = QStringLiteral("imported");
    QString probeMetadataJson;
    QString summaryMetadataJson;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct OfflineAnalysisBatchSource
{
    QString id;
    int cameraId = 0;
    QString sourceUri;
    QString fileName;
    qint64 fileSizeBytes = -1;
    QDateTime fileModifiedAt;
    int width = 1920;
    int height = 1080;
    double fps = 60.0;
    int durationMs = 0;
    qint64 totalFrames = 0;
    QDateTime sourceStartedAt;
    int manualCorrectionMs = 0;
    QString status = QStringLiteral("imported");
};

struct OfflineAnalysisRunSource
{
    QString id;
    QString taskId;
    int cameraId = 0;
    QString sourceUri;
    QDateTime sourceStartedAt;
    int manualCorrectionMs = 0;
    QString status;
    qint64 totalFrames = 0;
    qint64 processedFrames = 0;
    double progress = 0.0;
    qint64 lastFrameIndex = -1;
    qint64 lastPtsMs = -1;
    qint64 completedThroughMs = -1;
    QString errorMessage;
    int retryCount = 0;
};

struct OfflineAnalysisRun
{
    QString id;
    QString batchId;
    QString status;
    QString modelVersion;
    QString preprocessingVersion;
    QString gallerySnapshotHash;
    QString configurationJson;
    qint64 totalFrames = 0;
    qint64 processedFrames = 0;
    double progress = 0.0;
    double throughputFps = 0.0;
    int estimatedRemainingSeconds = -1;
    QString errorMessage;
    QString artifactRootUri;
    bool cancelRequested = false;
    QDateTime startedAt;
    QDateTime completedAt;
    QVector<OfflineAnalysisRunSource> sources;
};

struct OfflineAnalysisBatch
{
    QString id;
    QString status;
    QDateTime sourceStartedAt;
    QString activeRunId;
    QString metadataJson;
    QVector<OfflineAnalysisBatchSource> sources;
    QVector<OfflineAnalysisRun> runs;
};

struct OfflineAnalysisObject
{
    int classId = 0;
    qint64 trackId = -1;
    double detectionConfidence = 0.0;
    double bboxX = 0.0;
    double bboxY = 0.0;
    double bboxWidth = 0.0;
    double bboxHeight = 0.0;
    QString athleteId;
    QString label;
    QString identityStatus = QStringLiteral("unknown");
    double identityConfidence = 0.0;
    QString identitySource;
    bool reidExecuted = false;
};

struct OfflineAnalysisFrame
{
    qint64 frameIndex = -1;
    qint64 sourcePtsMs = -1;
    qint64 batchTimeMs = -1;
    int cameraId = 0;
    int width = 0;
    int height = 0;
    QVector<OfflineAnalysisObject> objects;
};

struct OfflineAnalysisFrameWindow
{
    QString runId;
    int cameraId = 0;
    qint64 fromMs = 0;
    qint64 toMs = 0;
    qint64 completedThroughMs = -1;
    QString sourceStatus;
    QVector<OfflineAnalysisFrame> frames;
    QVector<QPair<qint64, qint64>> gaps;
};

struct ActionRepetition
{
    QString id;
    QString sessionId;
    QString participantId;
    QString athleteId;
    QString athleteName;
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
    QString videoFileId;
    int videoIndex = 1;
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
    int trackId = -1;
    int cameraId = -1;
    qint64 frameTimeMs = -1;
    QString identityStatus = QStringLiteral("unknown");
    double identityConfidence = -1.0;
    QString identitySource;

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

struct ParticipantRepetition : ActionRepetition
{
    QString participantRepetitionId;
    QString actionRepetitionId;
};

struct ParticipantPoseFrame
{
    QString id;
    QString sessionId;
    QString participantId;
    QString athleteId;
    QString athleteName;
    QString videoFileId;
    int videoIndex = 1;
    qint64 frameTimeMs = 0;
    int cameraId = 0;
    int trackId = -1;
    QString identityStatus = QStringLiteral("unknown");
    double identityConfidence = -1.0;
    QString identitySource;
    double bboxX = 0.0;
    double bboxY = 0.0;
    double bboxWidth = 0.0;
    double bboxHeight = 0.0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    double fieldX = 0.0;
    double fieldY = 0.0;
    bool hasFieldPoint = false;
    double poseConfidence = -1.0;
    QString poseSummaryJson;
};

struct TrackPoint
{
    QString id;
    QString participantId;
    qint64 timestampMs = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    QString speedSource = QStringLiteral("position_delta");
    int cameraId = 0;
    double confidence = -1.0;
};

struct TrainingSession
{
    QString id;
    QString athleteId;
    QString coachId;
    QString competitionId;
    QString competitionEventId;
    QString eventAthleteId;
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
    QString analysisTaskId;
    QString analysisBatchId;
    QString analysisRunId;
    QString sourceType = QStringLiteral("training");
    QString sourceRef;
    QString sourceLabel;
    QString feedback;
    QString notes;
    QString coachComment;
    QVector<TrainingSessionParticipant> participants;
    QVector<ParticipantRepetition> participantRepetitions;
    QVector<ParticipantPoseFrame> participantPoseFrames;
    QVector<TrackPoint> trackPoints;
    QVector<TrainingVideoFile> videoFiles;
};

struct SessionHistoryItem
{
    QString id;
    QString athleteId;
    QString coachId;
    QString competitionId;
    QString planId;
    QString taskId;
    QString analysisBatchId;
    QString analysisRunId;
    QString actionStandardId;
    QString athleteName;
    QString coachName;
    QString competitionName;
    QString competitionLocation;
    QDate competitionDate;
    QString competitionType;
    QString competitionNotes;
    QString competitionEventId;
    QString eventAthleteId;
    QString raceName;
    QString eventName;
    QString heatName;
    QString groupName;
    QDateTime eventScheduledAt;
    QString eventNotes;
    QString bibNumber;
    QString laneNumber;
    int resultScore = -1;
    int resultRank = -1;
    QString eventAthleteNotes;
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
    QString analysisTaskId;
    QString analysisTaskStatus;
    QString analysisTaskBatchId;
    int analysisTaskCameraId = 0;
    int analysisTaskTimeOffsetMs = 0;
    QString sourceType = QStringLiteral("training");
    QString sourceRef;
    QString sourceLabel;
    QString feedback;
    QString notes;
    QString coachComment;
    QVector<TrainingSessionParticipant> participants;
    QVector<TrainingVideoFile> videoFiles;
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

struct RepetitionSearchFilters
{
    QString sessionId;
    QString athleteId;
    QString coachId;
    QString actionStandardId;
    QString competitionId;
    QString competitionEventId;
    QString eventAthleteId;
    QString sourceType;
    QString validState;
    QString reviewStatus;
    QString repetitionSource;
    QDateTime savedFrom;
    QDateTime savedTo;
    QString errorText;
    int minScore = -1;
    int maxScore = -1;
    int clipFromMs = -1;
    int clipToMs = -1;
};

struct RepetitionSearchItem : ActionRepetition
{
    QString time;
    QDateTime startedAt;
    QString athleteId;
    QString athleteName;
    QString participantId;
    QString coachId;
    QString coachName;
    QString competitionId;
    QString competitionName;
    QString competitionEventId;
    QString raceName;
    QString eventName;
    QString heatName;
    QString groupName;
    QString eventAthleteId;
    QString bibNumber;
    QString laneNumber;
    QString actionName;
    QString actionCategory;
    QString videoSource;
    QString videoFallbackSource;
    QString videoCameraName;
    QString videoFileStatus;
    QString videoFilePath;
    QString sessionSourceType;
    QString sessionSourceRef;
    int effectiveStartedMsValue = 0;
    int effectiveEndedMsValue = 0;
    bool effectiveValidValue = false;
    int effectiveScoreValue = 0;
    int effectiveDetectionScoreValue = 0;
    int effectiveSymmetryScoreValue = 0;
    int effectiveBalanceScoreValue = 0;
    int effectiveStabilityScoreValue = 0;
    int effectiveDepthScoreValue = 0;
    QString effectiveErrorCodesValue;
    QString effectiveFeedbackValue;
};

struct RepetitionSearchResult
{
    QVector<RepetitionSearchItem> items;
    int totalCount = 0;
    int pageNumber = 1;
    int pageSize = 50;
};

#endif // TRAININGDOMAIN_H
