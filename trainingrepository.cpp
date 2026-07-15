#include "trainingrepository.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDate>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>

namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString apiPath(const QString &path)
{
    return path.startsWith(QLatin1Char('/')) ? path : QStringLiteral("/") + path;
}

QString dateTimeToIso(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs) : QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QDateTime dateTimeFromJson(const QJsonValue &value)
{
    const QDateTime parsed = QDateTime::fromString(value.toString(), Qt::ISODate);
    return parsed.isValid() ? parsed : QDateTime();
}

QString dateToIso(const QDate &value)
{
    return value.isValid() ? value.toString(Qt::ISODate) : QString();
}

QDate dateFromJson(const QJsonValue &value)
{
    const QDate parsed = QDate::fromString(value.toString(), Qt::ISODate);
    return parsed.isValid() ? parsed : QDate();
}

int jsonInt(const QJsonObject &object, const QString &key, int fallback = 0)
{
    return object.contains(key) ? object.value(key).toInt(fallback) : fallback;
}

qint64 jsonInt64(const QJsonObject &object, const QString &key, qint64 fallback = 0)
{
    if (!object.contains(key)) {
        return fallback;
    }
    const QJsonValue value = object.value(key);
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        return ok ? parsed : fallback;
    }
    return static_cast<qint64>(value.toDouble(fallback));
}

double jsonDouble(const QJsonObject &object, const QString &key, double fallback = 0.0)
{
    return object.contains(key) ? object.value(key).toDouble(fallback) : fallback;
}

QString jsonString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString();
}

QJsonObject athleteToJson(const AthleteProfile &athlete)
{
    return {
        {QStringLiteral("id"), athlete.id},
        {QStringLiteral("name"), athlete.name},
        {QStringLiteral("code"), athlete.code},
        {QStringLiteral("ageGroup"), athlete.ageGroup},
        {QStringLiteral("heightCm"), athlete.heightCm},
        {QStringLiteral("weightKg"), athlete.weightKg},
        {QStringLiteral("discipline"), athlete.discipline},
        {QStringLiteral("level"), athlete.level},
        {QStringLiteral("preferredRotation"), athlete.preferredRotation},
        {QStringLiteral("preferredTakeoffFoot"), athlete.preferredTakeoffFoot},
        {QStringLiteral("injuryNotes"), athlete.injuryNotes},
        {QStringLiteral("goals"), athlete.goals},
        {QStringLiteral("active"), athlete.active}
    };
}

AthleteProfile athleteFromJson(const QJsonObject &object)
{
    AthleteProfile athlete;
    athlete.id = jsonString(object, QStringLiteral("id"));
    athlete.name = jsonString(object, QStringLiteral("name"));
    athlete.code = jsonString(object, QStringLiteral("code"));
    athlete.ageGroup = jsonString(object, QStringLiteral("ageGroup"));
    athlete.heightCm = jsonDouble(object, QStringLiteral("heightCm"));
    athlete.weightKg = jsonDouble(object, QStringLiteral("weightKg"));
    athlete.discipline = jsonString(object, QStringLiteral("discipline"));
    athlete.level = jsonString(object, QStringLiteral("level"));
    athlete.preferredRotation = jsonString(object, QStringLiteral("preferredRotation"));
    athlete.preferredTakeoffFoot = jsonString(object, QStringLiteral("preferredTakeoffFoot"));
    athlete.injuryNotes = jsonString(object, QStringLiteral("injuryNotes"));
    athlete.goals = jsonString(object, QStringLiteral("goals"));
    athlete.active = object.value(QStringLiteral("active")).toBool(true);
    return athlete;
}

AthleteIdentitySample identitySampleFromJson(const QJsonObject &object)
{
    AthleteIdentitySample sample;
    sample.id = jsonString(object, QStringLiteral("id"));
    sample.athleteId = jsonString(object, QStringLiteral("athleteId"));
    sample.filePath = jsonString(object, QStringLiteral("filePath"));
    sample.fileName = jsonString(object, QStringLiteral("fileName"));
    sample.modelVersion = jsonString(object, QStringLiteral("modelVersion"));
    sample.preprocessingVersion = jsonString(object, QStringLiteral("preprocessingVersion"));
    sample.embeddingDimension = jsonInt(object, QStringLiteral("embeddingDimension"));
    sample.createdAt = dateTimeFromJson(object.value(QStringLiteral("createdAt")));
    return sample;
}

AthleteIdentityEmbedding identityEmbeddingFromJson(const QJsonObject &object)
{
    AthleteIdentityEmbedding galleryEntry;
    galleryEntry.sampleId = jsonString(object, QStringLiteral("sampleId"));
    galleryEntry.athleteId = jsonString(object, QStringLiteral("athleteId"));
    galleryEntry.embeddingDimension = jsonInt(object, QStringLiteral("embeddingDimension"));
    galleryEntry.modelVersion = jsonString(object, QStringLiteral("modelVersion"));
    galleryEntry.preprocessingVersion = jsonString(object, QStringLiteral("preprocessingVersion"));
    const QJsonArray values = object.value(QStringLiteral("embedding")).toArray();
    galleryEntry.embedding.reserve(values.size());
    for (const QJsonValue &value : values) {
        galleryEntry.embedding.append(static_cast<float>(value.toDouble()));
    }
    return galleryEntry;
}

QJsonObject coachToJson(const CoachProfile &coach, const QVector<QString> &athleteIds = {})
{
    QJsonArray ids;
    for (const QString &id : athleteIds) {
        ids.append(id);
    }
    QJsonObject object{
        {QStringLiteral("id"), coach.id},
        {QStringLiteral("name"), coach.name},
        {QStringLiteral("code"), coach.code},
        {QStringLiteral("specialty"), coach.specialty},
        {QStringLiteral("phone"), coach.phone},
        {QStringLiteral("notes"), coach.notes},
        {QStringLiteral("active"), coach.active}
    };
    object.insert(QStringLiteral("athleteIds"), ids);
    return object;
}

CoachProfile coachFromJson(const QJsonObject &object)
{
    CoachProfile coach;
    coach.id = jsonString(object, QStringLiteral("id"));
    coach.name = jsonString(object, QStringLiteral("name"));
    coach.code = jsonString(object, QStringLiteral("code"));
    coach.specialty = jsonString(object, QStringLiteral("specialty"));
    coach.phone = jsonString(object, QStringLiteral("phone"));
    coach.notes = jsonString(object, QStringLiteral("notes"));
    coach.active = object.value(QStringLiteral("active")).toBool(true);
    return coach;
}

QJsonObject competitionToJson(const Competition &competition)
{
    return {
        {QStringLiteral("id"), competition.id},
        {QStringLiteral("name"), competition.name},
        {QStringLiteral("location"), competition.location},
        {QStringLiteral("competitionDate"), dateToIso(competition.competitionDate)},
        {QStringLiteral("competitionType"), competition.competitionType},
        {QStringLiteral("notes"), competition.notes},
        {QStringLiteral("active"), competition.active}
    };
}

Competition competitionFromJson(const QJsonObject &object)
{
    Competition competition;
    competition.id = jsonString(object, QStringLiteral("id"));
    competition.name = jsonString(object, QStringLiteral("name"));
    competition.location = jsonString(object, QStringLiteral("location"));
    competition.competitionDate = dateFromJson(object.value(QStringLiteral("competitionDate")));
    competition.competitionType = jsonString(object, QStringLiteral("competitionType"));
    competition.notes = jsonString(object, QStringLiteral("notes"));
    competition.active = object.value(QStringLiteral("active")).toBool(true);
    return competition;
}

QJsonObject competitionEventToJson(const CompetitionEvent &event)
{
    return {
        {QStringLiteral("id"), event.id},
        {QStringLiteral("competitionId"), event.competitionId},
        {QStringLiteral("raceName"), event.raceName},
        {QStringLiteral("eventName"), event.eventName},
        {QStringLiteral("heatName"), event.heatName},
        {QStringLiteral("groupName"), event.groupName},
        {QStringLiteral("scheduledAt"), event.scheduledAt.isValid() ? dateTimeToIso(event.scheduledAt) : QString()},
        {QStringLiteral("notes"), event.notes},
        {QStringLiteral("active"), event.active}
    };
}

CompetitionEvent competitionEventFromJson(const QJsonObject &object)
{
    CompetitionEvent event;
    event.id = jsonString(object, QStringLiteral("id"));
    event.competitionId = jsonString(object, QStringLiteral("competitionId"));
    event.raceName = jsonString(object, QStringLiteral("raceName"));
    event.eventName = jsonString(object, QStringLiteral("eventName"));
    event.heatName = jsonString(object, QStringLiteral("heatName"));
    event.groupName = jsonString(object, QStringLiteral("groupName"));
    event.scheduledAt = dateTimeFromJson(object.value(QStringLiteral("scheduledAt")));
    event.notes = jsonString(object, QStringLiteral("notes"));
    event.active = object.value(QStringLiteral("active")).toBool(true);
    return event;
}

QJsonObject eventAthleteToJson(const EventAthlete &eventAthlete)
{
    return {
        {QStringLiteral("id"), eventAthlete.id},
        {QStringLiteral("eventId"), eventAthlete.eventId},
        {QStringLiteral("athleteId"), eventAthlete.athleteId},
        {QStringLiteral("bibNumber"), eventAthlete.bibNumber},
        {QStringLiteral("laneNumber"), eventAthlete.laneNumber},
        {QStringLiteral("sortOrder"), eventAthlete.sortOrder},
        {QStringLiteral("resultScore"), eventAthlete.resultScore},
        {QStringLiteral("resultRank"), eventAthlete.resultRank},
        {QStringLiteral("notes"), eventAthlete.notes},
        {QStringLiteral("active"), eventAthlete.active}
    };
}

EventAthlete eventAthleteFromJson(const QJsonObject &object)
{
    EventAthlete eventAthlete;
    eventAthlete.id = jsonString(object, QStringLiteral("id"));
    eventAthlete.eventId = jsonString(object, QStringLiteral("eventId"));
    eventAthlete.athleteId = jsonString(object, QStringLiteral("athleteId"));
    eventAthlete.athleteName = jsonString(object, QStringLiteral("athleteName"));
    eventAthlete.bibNumber = jsonString(object, QStringLiteral("bibNumber"));
    eventAthlete.laneNumber = jsonString(object, QStringLiteral("laneNumber"));
    eventAthlete.sortOrder = jsonInt(object, QStringLiteral("sortOrder"));
    eventAthlete.resultScore = jsonInt(object, QStringLiteral("resultScore"), -1);
    eventAthlete.resultRank = jsonInt(object, QStringLiteral("resultRank"), -1);
    eventAthlete.notes = jsonString(object, QStringLiteral("notes"));
    eventAthlete.active = object.value(QStringLiteral("active")).toBool(true);
    return eventAthlete;
}

QJsonObject standardToJson(const ActionStandard &standard)
{
    return {
        {QStringLiteral("id"), standard.id},
        {QStringLiteral("code"), standard.code},
        {QStringLiteral("name"), standard.name},
        {QStringLiteral("categoryId"), standard.categoryId},
        {QStringLiteral("level"), standard.level},
        {QStringLiteral("purpose"), standard.purpose},
        {QStringLiteral("version"), standard.version},
        {QStringLiteral("targetReps"), standard.targetReps},
        {QStringLiteral("targetScore"), standard.targetScore},
        {QStringLiteral("setCount"), standard.setCount},
        {QStringLiteral("restSeconds"), standard.restSeconds},
        {QStringLiteral("armThreshold"), standard.armThreshold},
        {QStringLiteral("releaseThreshold"), standard.releaseThreshold},
        {QStringLiteral("debounceMs"), standard.debounceMs},
        {QStringLiteral("detectionWeight"), standard.detectionWeight},
        {QStringLiteral("symmetryWeight"), standard.symmetryWeight},
        {QStringLiteral("balanceWeight"), standard.balanceWeight},
        {QStringLiteral("stabilityWeight"), standard.stabilityWeight},
        {QStringLiteral("depthWeight"), standard.depthWeight},
        {QStringLiteral("detectionMin"), standard.detectionMin},
        {QStringLiteral("symmetryMin"), standard.symmetryMin},
        {QStringLiteral("balanceMin"), standard.balanceMin},
        {QStringLiteral("stabilityMin"), standard.stabilityMin},
        {QStringLiteral("depthMin"), standard.depthMin},
        {QStringLiteral("phases"), standard.phases},
        {QStringLiteral("keyPoints"), standard.keyPoints},
        {QStringLiteral("issueTitle"), standard.issueTitle},
        {QStringLiteral("issueBodyPart"), standard.issueBodyPart},
        {QStringLiteral("issueCause"), standard.issueCause},
        {QStringLiteral("issueCorrection"), standard.issueCorrection},
        {QStringLiteral("issuePriority"), standard.issuePriority},
        {QStringLiteral("referenceVideoSource"), standard.referenceVideoSource},
        {QStringLiteral("referenceRepetitionId"), standard.referenceRepetitionId},
        {QStringLiteral("referenceNotes"), standard.referenceNotes}
    };
}

ActionStandard standardFromJson(const QJsonObject &object)
{
    ActionStandard standard;
    standard.id = jsonString(object, QStringLiteral("id"));
    standard.code = jsonString(object, QStringLiteral("code"));
    standard.name = jsonString(object, QStringLiteral("name"));
    standard.categoryId = jsonString(object, QStringLiteral("categoryId"));
    standard.categoryName = jsonString(object, QStringLiteral("categoryName"));
    standard.level = jsonString(object, QStringLiteral("level"));
    standard.purpose = jsonString(object, QStringLiteral("purpose"));
    standard.version = jsonInt(object, QStringLiteral("version"), 1);
    standard.targetReps = jsonInt(object, QStringLiteral("targetReps"), 10);
    standard.targetScore = jsonInt(object, QStringLiteral("targetScore"), 80);
    standard.setCount = jsonInt(object, QStringLiteral("setCount"), 1);
    standard.restSeconds = jsonInt(object, QStringLiteral("restSeconds"), 60);
    standard.armThreshold = jsonDouble(object, QStringLiteral("armThreshold"), 0.28);
    standard.releaseThreshold = jsonDouble(object, QStringLiteral("releaseThreshold"), 0.18);
    standard.debounceMs = jsonInt(object, QStringLiteral("debounceMs"), 900);
    standard.detectionWeight = jsonDouble(object, QStringLiteral("detectionWeight"), 0.28);
    standard.symmetryWeight = jsonDouble(object, QStringLiteral("symmetryWeight"), 0.18);
    standard.balanceWeight = jsonDouble(object, QStringLiteral("balanceWeight"), 0.22);
    standard.stabilityWeight = jsonDouble(object, QStringLiteral("stabilityWeight"), 0.17);
    standard.depthWeight = jsonDouble(object, QStringLiteral("depthWeight"), 0.15);
    standard.detectionMin = jsonInt(object, QStringLiteral("detectionMin"), 55);
    standard.symmetryMin = jsonInt(object, QStringLiteral("symmetryMin"), 60);
    standard.balanceMin = jsonInt(object, QStringLiteral("balanceMin"), 60);
    standard.stabilityMin = jsonInt(object, QStringLiteral("stabilityMin"), 60);
    standard.depthMin = jsonInt(object, QStringLiteral("depthMin"), 55);
    standard.phases = jsonString(object, QStringLiteral("phases"));
    standard.keyPoints = jsonString(object, QStringLiteral("keyPoints"));
    standard.issueTitle = jsonString(object, QStringLiteral("issueTitle"));
    standard.issueBodyPart = jsonString(object, QStringLiteral("issueBodyPart"));
    standard.issueCause = jsonString(object, QStringLiteral("issueCause"));
    standard.issueCorrection = jsonString(object, QStringLiteral("issueCorrection"));
    standard.issuePriority = jsonInt(object, QStringLiteral("issuePriority"), 2);
    standard.referenceVideoSource = jsonString(object, QStringLiteral("referenceVideoSource"));
    standard.referenceRepetitionId = jsonString(object, QStringLiteral("referenceRepetitionId"));
    standard.referenceNotes = jsonString(object, QStringLiteral("referenceNotes"));
    return standard;
}

QJsonObject sessionToJson(const TrainingSession &session)
{
    QJsonArray participants;
    for (const TrainingSessionParticipant &participant : session.participants) {
        participants.append(QJsonObject{
            {QStringLiteral("id"), participant.id},
            {QStringLiteral("sessionId"), participant.sessionId},
            {QStringLiteral("athleteId"), participant.athleteId},
            {QStringLiteral("athleteName"), participant.athleteName},
            {QStringLiteral("slotIndex"), participant.slotIndex},
            {QStringLiteral("role"), participant.role},
            {QStringLiteral("trackLabel"), participant.trackLabel},
            {QStringLiteral("notes"), participant.notes},
            {QStringLiteral("active"), participant.active}
        });
    }
    QJsonArray videoFiles;
    for (const TrainingVideoFile &file : session.videoFiles) {
        QJsonObject metadata;
        const QJsonDocument metadataDocument = QJsonDocument::fromJson(file.metadataJson.toUtf8());
        if (metadataDocument.isObject()) {
            metadata = metadataDocument.object();
        }
        videoFiles.append(QJsonObject{
            {QStringLiteral("id"), file.id},
            {QStringLiteral("sessionId"), file.sessionId},
            {QStringLiteral("videoIndex"), file.videoIndex},
            {QStringLiteral("camera"), file.camera},
            {QStringLiteral("cameraName"), file.cameraName},
            {QStringLiteral("sourceUrl"), file.sourceUrl},
            {QStringLiteral("fallbackUrl"), file.fallbackUrl},
            {QStringLiteral("storageRoot"), file.storageRoot},
            {QStringLiteral("relativeDir"), file.relativeDir},
            {QStringLiteral("fileName"), file.fileName},
            {QStringLiteral("filePath"), file.filePath},
            {QStringLiteral("metadataPath"), file.metadataPath},
            {QStringLiteral("status"), file.status},
            {QStringLiteral("sessionStartMs"), file.sessionStartMs},
            {QStringLiteral("sessionEndMs"), file.sessionEndMs},
            {QStringLiteral("durationMs"), file.durationMs},
            {QStringLiteral("fileSizeBytes"), QString::number(file.fileSizeBytes)},
            {QStringLiteral("fileModifiedAt"), file.fileModifiedAt.isValid() ? file.fileModifiedAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {QStringLiteral("checksumAlgorithm"), file.checksumAlgorithm},
            {QStringLiteral("checksumValue"), file.checksumValue},
            {QStringLiteral("metadata"), metadata}
        });
    }
    return {
        {QStringLiteral("id"), session.id},
        {QStringLiteral("athleteId"), session.athleteId},
        {QStringLiteral("coachId"), session.coachId},
        {QStringLiteral("competitionId"), session.competitionId},
        {QStringLiteral("competitionEventId"), session.competitionEventId},
        {QStringLiteral("eventAthleteId"), session.eventAthleteId},
        {QStringLiteral("planId"), session.planId},
        {QStringLiteral("taskId"), session.taskId},
        {QStringLiteral("actionStandardId"), session.actionStandardId},
        {QStringLiteral("standardVersion"), session.standardVersion},
        {QStringLiteral("legacyQsettingsId"), QString::number(session.legacyQsettingsId)},
        {QStringLiteral("startedAt"), dateTimeToIso(session.startedAt)},
        {QStringLiteral("savedAt"), dateTimeToIso(session.savedAt)},
        {QStringLiteral("durationSec"), session.durationSec},
        {QStringLiteral("totalReps"), session.totalReps},
        {QStringLiteral("validReps"), session.validReps},
        {QStringLiteral("averageScore"), session.averageScore},
        {QStringLiteral("bestScore"), session.bestScore},
        {QStringLiteral("camera"), session.camera},
        {QStringLiteral("modelPrecision"), session.modelPrecision},
        {QStringLiteral("fps"), session.fps},
        {QStringLiteral("detectionScore"), session.detectionScore},
        {QStringLiteral("symmetryScore"), session.symmetryScore},
        {QStringLiteral("balanceScore"), session.balanceScore},
        {QStringLiteral("stabilityScore"), session.stabilityScore},
        {QStringLiteral("depthScore"), session.depthScore},
        {QStringLiteral("site"), session.site},
        {QStringLiteral("trainingPhase"), session.trainingPhase},
        {QStringLiteral("goal"), session.goal},
        {QStringLiteral("targetReps"), session.targetReps},
        {QStringLiteral("targetScore"), session.targetScore},
        {QStringLiteral("setCount"), session.setCount},
        {QStringLiteral("restSeconds"), session.restSeconds},
        {QStringLiteral("videoSource"), session.videoSource},
        {QStringLiteral("videoFallbackSource"), session.videoFallbackSource},
        {QStringLiteral("videoCameraName"), session.videoCameraName},
        {QStringLiteral("analysisTaskId"), session.analysisTaskId},
        {QStringLiteral("analysisBatchId"), session.analysisBatchId},
        {QStringLiteral("analysisRunId"), session.analysisRunId},
        {QStringLiteral("sourceType"), session.sourceType},
        {QStringLiteral("sourceRef"), session.sourceRef},
        {QStringLiteral("feedback"), session.feedback},
        {QStringLiteral("notes"), session.notes},
        {QStringLiteral("coachComment"), session.coachComment},
        {QStringLiteral("participants"), participants},
        {QStringLiteral("videoFiles"), videoFiles}
    };
}

QJsonObject repetitionToJson(const ActionRepetition &repetition)
{
    return {
        {QStringLiteral("id"), repetition.id},
        {QStringLiteral("sessionId"), repetition.sessionId},
        {QStringLiteral("participantId"), repetition.participantId},
        {QStringLiteral("athleteId"), repetition.athleteId},
        {QStringLiteral("actionStandardId"), repetition.actionStandardId},
        {QStringLiteral("standardVersion"), repetition.standardVersion},
        {QStringLiteral("startedMs"), repetition.startedMs},
        {QStringLiteral("endedMs"), repetition.endedMs},
        {QStringLiteral("valid"), repetition.valid},
        {QStringLiteral("score"), repetition.score},
        {QStringLiteral("detectionScore"), repetition.detectionScore},
        {QStringLiteral("symmetryScore"), repetition.symmetryScore},
        {QStringLiteral("balanceScore"), repetition.balanceScore},
        {QStringLiteral("stabilityScore"), repetition.stabilityScore},
        {QStringLiteral("depthScore"), repetition.depthScore},
        {QStringLiteral("errorCodes"), repetition.errorCodes},
        {QStringLiteral("feedback"), repetition.feedback},
        {QStringLiteral("keyFrameMs"), repetition.keyFrameMs},
        {QStringLiteral("videoFileId"), repetition.videoFileId},
        {QStringLiteral("videoIndex"), repetition.videoIndex},
        {QStringLiteral("videoClipStartMs"), repetition.videoClipStartMs},
        {QStringLiteral("videoClipEndMs"), repetition.videoClipEndMs},
        {QStringLiteral("source"), repetition.source},
        {QStringLiteral("reviewStatus"), repetition.reviewStatus},
        {QStringLiteral("reviewerCoachId"), repetition.reviewerCoachId},
        {QStringLiteral("reviewedAt"), dateTimeToIso(repetition.reviewedAt)},
        {QStringLiteral("manualStartedMs"), repetition.manualStartedMs},
        {QStringLiteral("manualEndedMs"), repetition.manualEndedMs},
        {QStringLiteral("manualValid"), repetition.manualValid},
        {QStringLiteral("manualScore"), repetition.manualScore},
        {QStringLiteral("manualDetectionScore"), repetition.manualDetectionScore},
        {QStringLiteral("manualSymmetryScore"), repetition.manualSymmetryScore},
        {QStringLiteral("manualBalanceScore"), repetition.manualBalanceScore},
        {QStringLiteral("manualStabilityScore"), repetition.manualStabilityScore},
        {QStringLiteral("manualDepthScore"), repetition.manualDepthScore},
        {QStringLiteral("manualErrorCodes"), repetition.manualErrorCodes},
        {QStringLiteral("manualFeedback"), repetition.manualFeedback},
        {QStringLiteral("coachNote"), repetition.coachNote},
        {QStringLiteral("keyFramePoseJson"), repetition.keyFramePoseJson},
        {QStringLiteral("trackId"), repetition.trackId},
        {QStringLiteral("cameraId"), repetition.cameraId},
        {QStringLiteral("frameTimeMs"), QString::number(repetition.frameTimeMs)},
        {QStringLiteral("identityStatus"), repetition.identityStatus},
        {QStringLiteral("identityConfidence"), repetition.identityConfidence},
        {QStringLiteral("identitySource"), repetition.identitySource}
    };
}

QJsonObject participantRepetitionToJson(const ParticipantRepetition &repetition)
{
    QJsonObject object = repetitionToJson(repetition);
    object.insert(QStringLiteral("participantRepetitionId"), repetition.participantRepetitionId);
    object.insert(QStringLiteral("actionRepetitionId"), repetition.actionRepetitionId);
    return object;
}

QJsonObject participantPoseFrameToJson(const ParticipantPoseFrame &frame)
{
    QJsonObject poseSummary;
    const QJsonDocument poseDocument = QJsonDocument::fromJson(frame.poseSummaryJson.toUtf8());
    if (poseDocument.isObject()) {
        poseSummary = poseDocument.object();
    }
    return {
        {QStringLiteral("id"), frame.id},
        {QStringLiteral("sessionId"), frame.sessionId},
        {QStringLiteral("participantId"), frame.participantId},
        {QStringLiteral("athleteId"), frame.athleteId},
        {QStringLiteral("videoFileId"), frame.videoFileId},
        {QStringLiteral("videoIndex"), frame.videoIndex},
        {QStringLiteral("frameTimeMs"), QString::number(frame.frameTimeMs)},
        {QStringLiteral("cameraId"), frame.cameraId},
        {QStringLiteral("trackId"), frame.trackId},
        {QStringLiteral("identityStatus"), frame.identityStatus},
        {QStringLiteral("identityConfidence"), frame.identityConfidence},
        {QStringLiteral("identitySource"), frame.identitySource},
        {QStringLiteral("bboxX"), frame.bboxX},
        {QStringLiteral("bboxY"), frame.bboxY},
        {QStringLiteral("bboxWidth"), frame.bboxWidth},
        {QStringLiteral("bboxHeight"), frame.bboxHeight},
        {QStringLiteral("anchorX"), frame.anchorX},
        {QStringLiteral("anchorY"), frame.anchorY},
        {QStringLiteral("fieldX"), frame.fieldX},
        {QStringLiteral("fieldY"), frame.fieldY},
        {QStringLiteral("hasFieldPoint"), frame.hasFieldPoint},
        {QStringLiteral("poseConfidence"), frame.poseConfidence},
        {QStringLiteral("poseSummary"), poseSummary}
    };
}

QJsonObject trackPointToJson(const TrackPoint &point)
{
    return {
        {QStringLiteral("id"), point.id},
        {QStringLiteral("participantId"), point.participantId},
        {QStringLiteral("tMs"), QString::number(point.timestampMs)},
        {QStringLiteral("x"), point.x},
        {QStringLiteral("y"), point.y},
        {QStringLiteral("z"), point.z},
        {QStringLiteral("speedSource"), point.speedSource},
        {QStringLiteral("cameraId"), point.cameraId},
        {QStringLiteral("confidence"), point.confidence}
    };
}

QJsonObject offlineAnalysisTaskToJson(const OfflineAnalysisTask &task)
{
    QJsonObject probeMetadata;
    const QJsonDocument probeDocument = QJsonDocument::fromJson(task.probeMetadataJson.toUtf8());
    if (probeDocument.isObject()) {
        probeMetadata = probeDocument.object();
    }
    QJsonObject summaryMetadata;
    const QJsonDocument summaryDocument = QJsonDocument::fromJson(task.summaryMetadataJson.toUtf8());
    if (summaryDocument.isObject()) {
        summaryMetadata = summaryDocument.object();
    }
    return {
        {QStringLiteral("id"), task.id},
        {QStringLiteral("batchId"), task.batchId},
        {QStringLiteral("cameraId"), task.cameraId},
        {QStringLiteral("timeOffsetMs"), task.timeOffsetMs},
        {QStringLiteral("videoPath"), task.videoPath},
        {QStringLiteral("fileName"), task.fileName},
        {QStringLiteral("fileSizeBytes"), QString::number(task.fileSizeBytes)},
        {QStringLiteral("fileModifiedAt"), task.fileModifiedAt.isValid() ? task.fileModifiedAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
        {QStringLiteral("durationMs"), task.durationMs},
        {QStringLiteral("status"), task.status},
        {QStringLiteral("probeMetadata"), probeMetadata},
        {QStringLiteral("summaryMetadata"), summaryMetadata}
    };
}

OfflineAnalysisTask offlineAnalysisTaskFromJson(const QJsonObject &object)
{
    OfflineAnalysisTask task;
    task.id = jsonString(object, QStringLiteral("id"));
    task.batchId = jsonString(object, QStringLiteral("batchId"));
    task.cameraId = jsonInt(object, QStringLiteral("cameraId"));
    task.timeOffsetMs = jsonInt(object, QStringLiteral("timeOffsetMs"));
    task.videoPath = jsonString(object, QStringLiteral("videoPath"));
    task.fileName = jsonString(object, QStringLiteral("fileName"));
    task.fileSizeBytes = jsonInt64(object, QStringLiteral("fileSizeBytes"), -1);
    task.fileModifiedAt = dateTimeFromJson(object.value(QStringLiteral("fileModifiedAt")));
    task.durationMs = jsonInt(object, QStringLiteral("durationMs"));
    task.status = jsonString(object, QStringLiteral("status"));
    if (task.status.trimmed().isEmpty()) {
        task.status = QStringLiteral("imported");
    }
    const QJsonValue probeMetadata = object.value(QStringLiteral("probeMetadata"));
    if (probeMetadata.isObject()) {
        task.probeMetadataJson = QString::fromUtf8(QJsonDocument(probeMetadata.toObject()).toJson(QJsonDocument::Compact));
    }
    const QJsonValue summaryMetadata = object.value(QStringLiteral("summaryMetadata"));
    if (summaryMetadata.isObject()) {
        task.summaryMetadataJson = QString::fromUtf8(QJsonDocument(summaryMetadata.toObject()).toJson(QJsonDocument::Compact));
    }
    task.createdAt = dateTimeFromJson(object.value(QStringLiteral("createdAt")));
    task.updatedAt = dateTimeFromJson(object.value(QStringLiteral("updatedAt")));
    return task;
}

QJsonObject offlineAnalysisBatchSourceToJson(const OfflineAnalysisBatchSource &source)
{
    return {
        {QStringLiteral("id"), source.id},
        {QStringLiteral("cameraId"), source.cameraId},
        {QStringLiteral("sourceUri"), source.sourceUri},
        {QStringLiteral("fileName"), source.fileName},
        {QStringLiteral("fileSizeBytes"), QString::number(source.fileSizeBytes)},
        {QStringLiteral("fileModifiedAt"), source.fileModifiedAt.isValid() ? dateTimeToIso(source.fileModifiedAt) : QString()},
        {QStringLiteral("durationMs"), source.durationMs},
        {QStringLiteral("totalFrames"), QString::number(source.totalFrames)},
        {QStringLiteral("sourceStartedAt"), source.sourceStartedAt.isValid() ? dateTimeToIso(source.sourceStartedAt) : QString()},
        {QStringLiteral("manualCorrectionMs"), source.manualCorrectionMs},
        {QStringLiteral("probeMetadata"), QJsonObject{
            {QStringLiteral("width"), source.width},
            {QStringLiteral("height"), source.height},
            {QStringLiteral("fps"), source.fps}
        }}
    };
}

OfflineAnalysisBatchSource offlineAnalysisBatchSourceFromJson(const QJsonObject &object)
{
    OfflineAnalysisBatchSource source;
    source.id = jsonString(object, QStringLiteral("id"));
    source.cameraId = jsonInt(object, QStringLiteral("cameraId"));
    source.sourceUri = jsonString(object, QStringLiteral("sourceUri"));
    if (source.sourceUri.isEmpty()) {
        source.sourceUri = jsonString(object, QStringLiteral("videoPath"));
    }
    source.fileName = jsonString(object, QStringLiteral("fileName"));
    source.fileSizeBytes = jsonInt64(object, QStringLiteral("fileSizeBytes"), -1);
    source.fileModifiedAt = dateTimeFromJson(object.value(QStringLiteral("fileModifiedAt")));
    source.durationMs = jsonInt(object, QStringLiteral("durationMs"));
    source.totalFrames = jsonInt64(object, QStringLiteral("totalFrames"));
    source.sourceStartedAt = dateTimeFromJson(object.value(QStringLiteral("sourceStartedAt")));
    source.manualCorrectionMs = jsonInt(object, QStringLiteral("manualCorrectionMs"),
                                        jsonInt(object, QStringLiteral("timeOffsetMs")));
    source.status = jsonString(object, QStringLiteral("status"));
    const QJsonObject probe = object.value(QStringLiteral("probeMetadata")).toObject();
    source.width = jsonInt(probe, QStringLiteral("width"), 1920);
    source.height = jsonInt(probe, QStringLiteral("height"), 1080);
    source.fps = jsonDouble(probe, QStringLiteral("fps"), 60.0);
    if (source.totalFrames <= 0) {
        source.totalFrames = jsonInt64(probe, QStringLiteral("totalFrames"));
    }
    if (!source.sourceStartedAt.isValid()) {
        source.sourceStartedAt = dateTimeFromJson(probe.value(QStringLiteral("sourceStartedAt")));
    }
    return source;
}

OfflineAnalysisRunSource offlineAnalysisRunSourceFromJson(const QJsonObject &object)
{
    OfflineAnalysisRunSource source;
    source.id = jsonString(object, QStringLiteral("id"));
    source.taskId = jsonString(object, QStringLiteral("taskId"));
    source.cameraId = jsonInt(object, QStringLiteral("cameraId"));
    source.sourceUri = jsonString(object, QStringLiteral("sourceUri"));
    source.sourceStartedAt = dateTimeFromJson(object.value(QStringLiteral("sourceStartedAt")));
    source.manualCorrectionMs = jsonInt(object, QStringLiteral("manualCorrectionMs"));
    source.status = jsonString(object, QStringLiteral("status"));
    source.totalFrames = jsonInt64(object, QStringLiteral("totalFrames"));
    source.processedFrames = jsonInt64(object, QStringLiteral("processedFrames"));
    source.progress = jsonDouble(object, QStringLiteral("progress"));
    source.lastFrameIndex = jsonInt64(object, QStringLiteral("lastFrameIndex"), -1);
    source.lastPtsMs = jsonInt64(object, QStringLiteral("lastPtsMs"), -1);
    source.completedThroughMs = jsonInt64(object, QStringLiteral("completedThroughMs"), -1);
    source.errorMessage = jsonString(object, QStringLiteral("errorMessage"));
    source.retryCount = jsonInt(object, QStringLiteral("retryCount"));
    return source;
}

OfflineAnalysisRun offlineAnalysisRunFromJson(const QJsonObject &object)
{
    OfflineAnalysisRun run;
    run.id = jsonString(object, QStringLiteral("id"));
    run.batchId = jsonString(object, QStringLiteral("batchId"));
    run.status = jsonString(object, QStringLiteral("status"));
    run.modelVersion = jsonString(object, QStringLiteral("modelVersion"));
    run.preprocessingVersion = jsonString(object, QStringLiteral("preprocessingVersion"));
    run.gallerySnapshotHash = jsonString(object, QStringLiteral("gallerySnapshotHash"));
    run.configurationJson = QString::fromUtf8(QJsonDocument(object.value(QStringLiteral("configuration")).toObject())
                                                  .toJson(QJsonDocument::Compact));
    run.totalFrames = jsonInt64(object, QStringLiteral("totalFrames"));
    run.processedFrames = jsonInt64(object, QStringLiteral("processedFrames"));
    run.progress = jsonDouble(object, QStringLiteral("progress"));
    run.throughputFps = jsonDouble(object, QStringLiteral("throughputFps"));
    run.estimatedRemainingSeconds = jsonInt(object, QStringLiteral("estimatedRemainingSeconds"), -1);
    run.errorMessage = jsonString(object, QStringLiteral("errorMessage"));
    run.artifactRootUri = jsonString(object, QStringLiteral("artifactRootUri"));
    run.cancelRequested = object.value(QStringLiteral("cancelRequested")).toBool(false);
    run.startedAt = dateTimeFromJson(object.value(QStringLiteral("startedAt")));
    run.completedAt = dateTimeFromJson(object.value(QStringLiteral("completedAt")));
    for (const QJsonValue &value : object.value(QStringLiteral("sources")).toArray()) {
        run.sources.append(offlineAnalysisRunSourceFromJson(value.toObject()));
    }
    return run;
}

OfflineAnalysisBatch offlineAnalysisBatchFromJson(const QJsonObject &object)
{
    OfflineAnalysisBatch batch;
    batch.id = jsonString(object, QStringLiteral("id"));
    batch.status = jsonString(object, QStringLiteral("status"));
    batch.sourceStartedAt = dateTimeFromJson(object.value(QStringLiteral("sourceStartedAt")));
    batch.activeRunId = jsonString(object, QStringLiteral("activeRunId"));
    batch.metadataJson = QString::fromUtf8(QJsonDocument(object.value(QStringLiteral("metadata")).toObject())
                                               .toJson(QJsonDocument::Compact));
    for (const QJsonValue &value : object.value(QStringLiteral("sources")).toArray()) {
        batch.sources.append(offlineAnalysisBatchSourceFromJson(value.toObject()));
    }
    for (const QJsonValue &value : object.value(QStringLiteral("runs")).toArray()) {
        batch.runs.append(offlineAnalysisRunFromJson(value.toObject()));
    }
    return batch;
}

OfflineAnalysisFrameWindow offlineAnalysisFrameWindowFromJson(const QJsonObject &object)
{
    OfflineAnalysisFrameWindow window;
    window.runId = jsonString(object, QStringLiteral("runId"));
    window.cameraId = jsonInt(object, QStringLiteral("cameraId"));
    window.fromMs = jsonInt64(object, QStringLiteral("fromMs"));
    window.toMs = jsonInt64(object, QStringLiteral("toMs"));
    window.completedThroughMs = jsonInt64(object, QStringLiteral("completedThroughMs"), -1);
    window.sourceStatus = jsonString(object, QStringLiteral("sourceStatus"));
    for (const QJsonValue &frameValue : object.value(QStringLiteral("frames")).toArray()) {
        const QJsonObject frameObject = frameValue.toObject();
        OfflineAnalysisFrame frame;
        frame.frameIndex = jsonInt64(frameObject, QStringLiteral("frameIndex"), -1);
        frame.sourcePtsMs = jsonInt64(frameObject, QStringLiteral("sourcePtsMs"), -1);
        frame.batchTimeMs = jsonInt64(frameObject, QStringLiteral("batchTimeMs"), -1);
        frame.cameraId = jsonInt(frameObject, QStringLiteral("cameraId"));
        frame.width = jsonInt(frameObject, QStringLiteral("width"));
        frame.height = jsonInt(frameObject, QStringLiteral("height"));
        for (const QJsonValue &objectValue : frameObject.value(QStringLiteral("objects")).toArray()) {
            const QJsonObject value = objectValue.toObject();
            OfflineAnalysisObject instance;
            instance.classId = jsonInt(value, QStringLiteral("classId"));
            instance.trackId = jsonInt64(value, QStringLiteral("trackId"), -1);
            instance.detectionConfidence = jsonDouble(value, QStringLiteral("detectionConfidence"));
            instance.bboxX = jsonDouble(value, QStringLiteral("bboxX"));
            instance.bboxY = jsonDouble(value, QStringLiteral("bboxY"));
            instance.bboxWidth = jsonDouble(value, QStringLiteral("bboxWidth"));
            instance.bboxHeight = jsonDouble(value, QStringLiteral("bboxHeight"));
            instance.athleteId = jsonString(value, QStringLiteral("athleteId"));
            instance.label = jsonString(value, QStringLiteral("label"));
            instance.identityStatus = jsonString(value, QStringLiteral("identityStatus"));
            instance.identityConfidence = jsonDouble(value, QStringLiteral("identityConfidence"));
            instance.identitySource = jsonString(value, QStringLiteral("identitySource"));
            instance.reidExecuted = value.value(QStringLiteral("reidExecuted")).toBool(false);
            frame.objects.append(instance);
        }
        window.frames.append(frame);
    }
    for (const QJsonValue &gapValue : object.value(QStringLiteral("gaps")).toArray()) {
        const QJsonObject gap = gapValue.toObject();
        window.gaps.append({jsonInt64(gap, QStringLiteral("fromMs")), jsonInt64(gap, QStringLiteral("toMs"))});
    }
    return window;
}

ActionRepetition repetitionFromJson(const QJsonObject &object)
{
    ActionRepetition repetition;
    repetition.id = jsonString(object, QStringLiteral("id"));
    repetition.sessionId = jsonString(object, QStringLiteral("sessionId"));
    repetition.participantId = jsonString(object, QStringLiteral("participantId"));
    repetition.athleteId = jsonString(object, QStringLiteral("athleteId"));
    repetition.athleteName = jsonString(object, QStringLiteral("athleteName"));
    repetition.actionStandardId = jsonString(object, QStringLiteral("actionStandardId"));
    repetition.standardVersion = jsonInt(object, QStringLiteral("standardVersion"), 1);
    repetition.startedMs = jsonInt(object, QStringLiteral("startedMs"));
    repetition.endedMs = jsonInt(object, QStringLiteral("endedMs"));
    repetition.valid = object.value(QStringLiteral("valid")).toBool(false);
    repetition.score = jsonInt(object, QStringLiteral("score"));
    repetition.detectionScore = jsonInt(object, QStringLiteral("detectionScore"));
    repetition.symmetryScore = jsonInt(object, QStringLiteral("symmetryScore"));
    repetition.balanceScore = jsonInt(object, QStringLiteral("balanceScore"));
    repetition.stabilityScore = jsonInt(object, QStringLiteral("stabilityScore"));
    repetition.depthScore = jsonInt(object, QStringLiteral("depthScore"));
    repetition.errorCodes = jsonString(object, QStringLiteral("errorCodes"));
    repetition.feedback = jsonString(object, QStringLiteral("feedback"));
    repetition.keyFrameMs = jsonInt(object, QStringLiteral("keyFrameMs"));
    repetition.videoFileId = jsonString(object, QStringLiteral("videoFileId"));
    repetition.videoIndex = jsonInt(object, QStringLiteral("videoIndex"), 1);
    repetition.videoClipStartMs = jsonInt(object, QStringLiteral("videoClipStartMs"));
    repetition.videoClipEndMs = jsonInt(object, QStringLiteral("videoClipEndMs"));
    repetition.source = jsonString(object, QStringLiteral("source"));
    repetition.reviewStatus = jsonString(object, QStringLiteral("reviewStatus"));
    repetition.reviewerCoachId = jsonString(object, QStringLiteral("reviewerCoachId"));
    repetition.reviewedAt = dateTimeFromJson(object.value(QStringLiteral("reviewedAt")));
    repetition.manualStartedMs = jsonInt(object, QStringLiteral("manualStartedMs"), -1);
    repetition.manualEndedMs = jsonInt(object, QStringLiteral("manualEndedMs"), -1);
    repetition.manualValid = jsonInt(object, QStringLiteral("manualValid"), -1);
    repetition.manualScore = jsonInt(object, QStringLiteral("manualScore"), -1);
    repetition.manualDetectionScore = jsonInt(object, QStringLiteral("manualDetectionScore"), -1);
    repetition.manualSymmetryScore = jsonInt(object, QStringLiteral("manualSymmetryScore"), -1);
    repetition.manualBalanceScore = jsonInt(object, QStringLiteral("manualBalanceScore"), -1);
    repetition.manualStabilityScore = jsonInt(object, QStringLiteral("manualStabilityScore"), -1);
    repetition.manualDepthScore = jsonInt(object, QStringLiteral("manualDepthScore"), -1);
    repetition.manualErrorCodes = jsonString(object, QStringLiteral("manualErrorCodes"));
    repetition.manualFeedback = jsonString(object, QStringLiteral("manualFeedback"));
    repetition.coachNote = jsonString(object, QStringLiteral("coachNote"));
    repetition.keyFramePoseJson = jsonString(object, QStringLiteral("keyFramePoseJson"));
    repetition.trackId = jsonInt(object, QStringLiteral("trackId"), -1);
    repetition.cameraId = jsonInt(object, QStringLiteral("cameraId"), -1);
    repetition.frameTimeMs = jsonString(object, QStringLiteral("frameTimeMs")).toLongLong();
    if (repetition.frameTimeMs == 0 && object.contains(QStringLiteral("frameTimeMs"))) {
        repetition.frameTimeMs = static_cast<qint64>(object.value(QStringLiteral("frameTimeMs")).toDouble(-1));
    }
    repetition.identityStatus = jsonString(object, QStringLiteral("identityStatus"));
    if (repetition.identityStatus.isEmpty()) {
        repetition.identityStatus = QStringLiteral("unknown");
    }
    repetition.identityConfidence = jsonDouble(object, QStringLiteral("identityConfidence"), -1.0);
    repetition.identitySource = jsonString(object, QStringLiteral("identitySource"));
    return repetition;
}

TrainingSessionParticipant participantFromJson(const QJsonObject &object)
{
    TrainingSessionParticipant participant;
    participant.id = jsonString(object, QStringLiteral("id"));
    participant.sessionId = jsonString(object, QStringLiteral("sessionId"));
    participant.athleteId = jsonString(object, QStringLiteral("athleteId"));
    participant.athleteName = jsonString(object, QStringLiteral("athleteName"));
    participant.slotIndex = jsonInt(object, QStringLiteral("slotIndex"), 1);
    participant.role = jsonString(object, QStringLiteral("role"));
    if (participant.role.isEmpty()) {
        participant.role = QStringLiteral("participant");
    }
    participant.trackLabel = jsonString(object, QStringLiteral("trackLabel"));
    participant.notes = jsonString(object, QStringLiteral("notes"));
    participant.active = object.value(QStringLiteral("active")).toBool(true);
    return participant;
}

TrainingVideoFile videoFileFromJson(const QJsonObject &object)
{
    TrainingVideoFile file;
    file.id = jsonString(object, QStringLiteral("id"));
    file.sessionId = jsonString(object, QStringLiteral("sessionId"));
    file.videoIndex = jsonInt(object, QStringLiteral("videoIndex"), 1);
    file.camera = jsonInt(object, QStringLiteral("camera"));
    file.cameraName = jsonString(object, QStringLiteral("cameraName"));
    file.sourceUrl = jsonString(object, QStringLiteral("sourceUrl"));
    file.fallbackUrl = jsonString(object, QStringLiteral("fallbackUrl"));
    file.storageRoot = jsonString(object, QStringLiteral("storageRoot"));
    file.relativeDir = jsonString(object, QStringLiteral("relativeDir"));
    file.fileName = jsonString(object, QStringLiteral("fileName"));
    file.filePath = jsonString(object, QStringLiteral("filePath"));
    file.metadataPath = jsonString(object, QStringLiteral("metadataPath"));
    file.status = jsonString(object, QStringLiteral("status"));
    if (file.status.trimmed().isEmpty()) {
        file.status = QStringLiteral("planned");
    }
    file.sessionStartMs = jsonInt(object, QStringLiteral("sessionStartMs"));
    file.sessionEndMs = jsonInt(object, QStringLiteral("sessionEndMs"));
    file.durationMs = jsonInt(object, QStringLiteral("durationMs"));
    file.fileSizeBytes = jsonInt64(object, QStringLiteral("fileSizeBytes"), -1);
    file.fileModifiedAt = dateTimeFromJson(object.value(QStringLiteral("fileModifiedAt")));
    file.checksumAlgorithm = jsonString(object, QStringLiteral("checksumAlgorithm"));
    file.checksumValue = jsonString(object, QStringLiteral("checksumValue"));
    const QJsonValue metadata = object.value(QStringLiteral("metadata"));
    if (metadata.isObject()) {
        file.metadataJson = QString::fromUtf8(QJsonDocument(metadata.toObject()).toJson(QJsonDocument::Compact));
    } else {
        file.metadataJson = jsonString(object, QStringLiteral("metadata"));
    }
    return file;
}

ParticipantPoseFrame participantPoseFrameFromJson(const QJsonObject &object)
{
    ParticipantPoseFrame frame;
    frame.id = jsonString(object, QStringLiteral("id"));
    frame.sessionId = jsonString(object, QStringLiteral("sessionId"));
    frame.participantId = jsonString(object, QStringLiteral("participantId"));
    frame.athleteId = jsonString(object, QStringLiteral("athleteId"));
    frame.athleteName = jsonString(object, QStringLiteral("athleteName"));
    frame.videoFileId = jsonString(object, QStringLiteral("videoFileId"));
    frame.videoIndex = jsonInt(object, QStringLiteral("videoIndex"), 1);
    frame.frameTimeMs = jsonString(object, QStringLiteral("frameTimeMs")).toLongLong();
    if (frame.frameTimeMs == 0 && object.contains(QStringLiteral("frameTimeMs"))) {
        frame.frameTimeMs = static_cast<qint64>(object.value(QStringLiteral("frameTimeMs")).toDouble(0));
    }
    frame.cameraId = jsonInt(object, QStringLiteral("cameraId"));
    frame.trackId = jsonInt(object, QStringLiteral("trackId"), -1);
    frame.identityStatus = jsonString(object, QStringLiteral("identityStatus"));
    if (frame.identityStatus.isEmpty()) {
        frame.identityStatus = QStringLiteral("unknown");
    }
    frame.identityConfidence = jsonDouble(object, QStringLiteral("identityConfidence"), -1.0);
    frame.identitySource = jsonString(object, QStringLiteral("identitySource"));
    frame.bboxX = jsonDouble(object, QStringLiteral("bboxX"));
    frame.bboxY = jsonDouble(object, QStringLiteral("bboxY"));
    frame.bboxWidth = jsonDouble(object, QStringLiteral("bboxWidth"));
    frame.bboxHeight = jsonDouble(object, QStringLiteral("bboxHeight"));
    frame.anchorX = jsonDouble(object, QStringLiteral("anchorX"));
    frame.anchorY = jsonDouble(object, QStringLiteral("anchorY"));
    frame.fieldX = jsonDouble(object, QStringLiteral("fieldX"));
    frame.fieldY = jsonDouble(object, QStringLiteral("fieldY"));
    frame.hasFieldPoint = object.value(QStringLiteral("hasFieldPoint")).toBool(false);
    frame.poseConfidence = jsonDouble(object, QStringLiteral("poseConfidence"), -1.0);
    const QJsonValue poseSummary = object.value(QStringLiteral("poseSummary"));
    if (poseSummary.isObject()) {
        frame.poseSummaryJson = QString::fromUtf8(QJsonDocument(poseSummary.toObject()).toJson(QJsonDocument::Compact));
    } else {
        frame.poseSummaryJson = jsonString(object, QStringLiteral("poseSummary"));
    }
    return frame;
}

TrackPoint trackPointFromJson(const QJsonObject &object)
{
    TrackPoint point;
    point.id = jsonString(object, QStringLiteral("id"));
    point.participantId = jsonString(object, QStringLiteral("participantId"));
    point.timestampMs = jsonInt64(object, QStringLiteral("tMs"));
    point.x = jsonDouble(object, QStringLiteral("x"));
    point.y = jsonDouble(object, QStringLiteral("y"));
    point.z = jsonDouble(object, QStringLiteral("z"));
    point.speedSource = jsonString(object, QStringLiteral("speedSource"));
    if (point.speedSource.isEmpty()) {
        point.speedSource = QStringLiteral("position_delta");
    }
    point.cameraId = jsonInt(object, QStringLiteral("cameraId"));
    point.confidence = jsonDouble(object, QStringLiteral("confidence"), -1.0);
    return point;
}

VideoFileCleanupCandidate cleanupCandidateFromJson(const QJsonObject &object)
{
    VideoFileCleanupCandidate item;
    item.id = jsonString(object, QStringLiteral("id"));
    item.sessionId = jsonString(object, QStringLiteral("sessionId"));
    item.videoIndex = jsonInt(object, QStringLiteral("videoIndex"), 1);
    item.athleteName = jsonString(object, QStringLiteral("athleteName"));
    item.sessionStartedAt = dateTimeFromJson(object.value(QStringLiteral("sessionStartedAt")));
    item.status = jsonString(object, QStringLiteral("status"));
    item.filePath = jsonString(object, QStringLiteral("filePath"));
    item.fileSizeBytes = jsonInt64(object, QStringLiteral("fileSizeBytes"), -1);
    item.fileModifiedAt = dateTimeFromJson(object.value(QStringLiteral("fileModifiedAt")));
    item.actionCount = jsonInt(object, QStringLiteral("actionCount"));
    const QJsonValue metadata = object.value(QStringLiteral("metadata"));
    if (metadata.isObject()) {
        item.metadataJson = QString::fromUtf8(QJsonDocument(metadata.toObject()).toJson(QJsonDocument::Compact));
    } else {
        item.metadataJson = jsonString(object, QStringLiteral("metadata"));
    }
    return item;
}

RepetitionSearchItem repetitionSearchItemFromJson(const QJsonObject &object)
{
    RepetitionSearchItem item;
    static_cast<ActionRepetition &>(item) = repetitionFromJson(object);
    item.time = jsonString(object, QStringLiteral("time"));
    item.startedAt = dateTimeFromJson(object.value(QStringLiteral("startedAt")));
    item.athleteId = jsonString(object, QStringLiteral("athleteId"));
    item.athleteName = jsonString(object, QStringLiteral("athleteName"));
    item.participantId = jsonString(object, QStringLiteral("participantId"));
    item.coachId = jsonString(object, QStringLiteral("coachId"));
    item.coachName = jsonString(object, QStringLiteral("coachName"));
    item.competitionId = jsonString(object, QStringLiteral("competitionId"));
    item.competitionName = jsonString(object, QStringLiteral("competitionName"));
    item.competitionEventId = jsonString(object, QStringLiteral("competitionEventId"));
    item.raceName = jsonString(object, QStringLiteral("raceName"));
    item.eventName = jsonString(object, QStringLiteral("eventName"));
    item.heatName = jsonString(object, QStringLiteral("heatName"));
    item.groupName = jsonString(object, QStringLiteral("groupName"));
    item.eventAthleteId = jsonString(object, QStringLiteral("eventAthleteId"));
    item.bibNumber = jsonString(object, QStringLiteral("bibNumber"));
    item.laneNumber = jsonString(object, QStringLiteral("laneNumber"));
    item.actionName = jsonString(object, QStringLiteral("actionName"));
    item.actionCategory = jsonString(object, QStringLiteral("actionCategory"));
    item.videoSource = jsonString(object, QStringLiteral("videoSource"));
    item.videoFallbackSource = jsonString(object, QStringLiteral("videoFallbackSource"));
    item.videoCameraName = jsonString(object, QStringLiteral("videoCameraName"));
    item.videoFileStatus = jsonString(object, QStringLiteral("videoFileStatus"));
    item.videoFilePath = jsonString(object, QStringLiteral("videoFilePath"));
    item.sessionSourceType = jsonString(object, QStringLiteral("sessionSourceType"));
    item.sessionSourceRef = jsonString(object, QStringLiteral("sessionSourceRef"));
    item.effectiveStartedMsValue = jsonInt(object, QStringLiteral("effectiveStartedMs"));
    item.effectiveEndedMsValue = jsonInt(object, QStringLiteral("effectiveEndedMs"));
    item.effectiveValidValue = object.value(QStringLiteral("effectiveValid")).toBool(false);
    item.effectiveScoreValue = jsonInt(object, QStringLiteral("effectiveScore"));
    item.effectiveDetectionScoreValue = jsonInt(object, QStringLiteral("effectiveDetectionScore"));
    item.effectiveSymmetryScoreValue = jsonInt(object, QStringLiteral("effectiveSymmetryScore"));
    item.effectiveBalanceScoreValue = jsonInt(object, QStringLiteral("effectiveBalanceScore"));
    item.effectiveStabilityScoreValue = jsonInt(object, QStringLiteral("effectiveStabilityScore"));
    item.effectiveDepthScoreValue = jsonInt(object, QStringLiteral("effectiveDepthScore"));
    item.effectiveErrorCodesValue = jsonString(object, QStringLiteral("effectiveErrorCodes"));
    item.effectiveFeedbackValue = jsonString(object, QStringLiteral("effectiveFeedback"));
    return item;
}

SessionHistoryItem historyFromJson(const QJsonObject &object)
{
    SessionHistoryItem item;
    item.id = jsonString(object, QStringLiteral("id"));
    item.athleteId = jsonString(object, QStringLiteral("athleteId"));
    item.coachId = jsonString(object, QStringLiteral("coachId"));
    item.competitionId = jsonString(object, QStringLiteral("competitionId"));
    item.competitionEventId = jsonString(object, QStringLiteral("competitionEventId"));
    item.eventAthleteId = jsonString(object, QStringLiteral("eventAthleteId"));
    item.planId = jsonString(object, QStringLiteral("planId"));
    item.taskId = jsonString(object, QStringLiteral("taskId"));
    item.actionStandardId = jsonString(object, QStringLiteral("actionStandardId"));
    item.athleteName = jsonString(object, QStringLiteral("athleteName"));
    item.coachName = jsonString(object, QStringLiteral("coachName"));
    item.competitionName = jsonString(object, QStringLiteral("competitionName"));
    item.competitionLocation = jsonString(object, QStringLiteral("competitionLocation"));
    item.competitionDate = dateFromJson(object.value(QStringLiteral("competitionDate")));
    item.competitionType = jsonString(object, QStringLiteral("competitionType"));
    item.competitionNotes = jsonString(object, QStringLiteral("competitionNotes"));
    item.raceName = jsonString(object, QStringLiteral("raceName"));
    item.eventName = jsonString(object, QStringLiteral("eventName"));
    item.heatName = jsonString(object, QStringLiteral("heatName"));
    item.groupName = jsonString(object, QStringLiteral("groupName"));
    item.eventScheduledAt = dateTimeFromJson(object.value(QStringLiteral("eventScheduledAt")));
    item.eventNotes = jsonString(object, QStringLiteral("eventNotes"));
    item.bibNumber = jsonString(object, QStringLiteral("bibNumber"));
    item.laneNumber = jsonString(object, QStringLiteral("laneNumber"));
    item.resultScore = jsonInt(object, QStringLiteral("resultScore"), -1);
    item.resultRank = jsonInt(object, QStringLiteral("resultRank"), -1);
    item.eventAthleteNotes = jsonString(object, QStringLiteral("eventAthleteNotes"));
    item.actionName = jsonString(object, QStringLiteral("actionName"));
    item.actionCategory = jsonString(object, QStringLiteral("actionCategory"));
    item.standardVersion = jsonInt(object, QStringLiteral("standardVersion"), 1);
    item.time = jsonString(object, QStringLiteral("time"));
    item.startedAt = dateTimeFromJson(object.value(QStringLiteral("startedAt")));
    item.duration = jsonInt(object, QStringLiteral("duration"));
    item.totalReps = jsonInt(object, QStringLiteral("totalReps"));
    item.validReps = jsonInt(object, QStringLiteral("validReps"));
    item.targetReps = jsonInt(object, QStringLiteral("targetReps"));
    item.targetScore = jsonInt(object, QStringLiteral("targetScore"));
    item.score = jsonInt(object, QStringLiteral("score"));
    item.bestScore = jsonInt(object, QStringLiteral("bestScore"));
    item.camera = jsonInt(object, QStringLiteral("camera"), 1);
    item.modelPrecision = jsonString(object, QStringLiteral("modelPrecision"));
    item.fps = jsonInt(object, QStringLiteral("fps"), 30);
    item.detectionScore = jsonInt(object, QStringLiteral("detectionScore"));
    item.symmetryScore = jsonInt(object, QStringLiteral("symmetryScore"));
    item.balanceScore = jsonInt(object, QStringLiteral("balanceScore"));
    item.stabilityScore = jsonInt(object, QStringLiteral("stabilityScore"));
    item.depthScore = jsonInt(object, QStringLiteral("depthScore"));
    item.site = jsonString(object, QStringLiteral("site"));
    item.trainingPhase = jsonString(object, QStringLiteral("trainingPhase"));
    item.goal = jsonString(object, QStringLiteral("goal"));
    item.videoSource = jsonString(object, QStringLiteral("videoSource"));
    item.videoFallbackSource = jsonString(object, QStringLiteral("videoFallbackSource"));
    item.videoCameraName = jsonString(object, QStringLiteral("videoCameraName"));
    item.analysisTaskId = jsonString(object, QStringLiteral("analysisTaskId"));
    item.analysisBatchId = jsonString(object, QStringLiteral("analysisBatchId"));
    item.analysisRunId = jsonString(object, QStringLiteral("analysisRunId"));
    item.analysisTaskStatus = jsonString(object, QStringLiteral("analysisTaskStatus"));
    item.analysisTaskBatchId = jsonString(object, QStringLiteral("analysisTaskBatchId"));
    item.analysisTaskCameraId = jsonInt(object, QStringLiteral("analysisTaskCameraId"));
    item.analysisTaskTimeOffsetMs = jsonInt(object, QStringLiteral("analysisTaskTimeOffsetMs"));
    item.sourceType = jsonString(object, QStringLiteral("sourceType"));
    if (item.sourceType.trimmed().isEmpty()) {
        item.sourceType = QStringLiteral("training");
    }
    item.sourceRef = jsonString(object, QStringLiteral("sourceRef"));
    item.sourceLabel = jsonString(object, QStringLiteral("sourceLabel"));
    item.feedback = jsonString(object, QStringLiteral("feedback"));
    item.notes = jsonString(object, QStringLiteral("notes"));
    item.coachComment = jsonString(object, QStringLiteral("coachComment"));
    const QJsonArray participants = object.value(QStringLiteral("participants")).toArray();
    for (const QJsonValue &value : participants) {
        item.participants.append(participantFromJson(value.toObject()));
    }
    const QJsonArray videoFiles = object.value(QStringLiteral("videoFiles")).toArray();
    for (const QJsonValue &value : videoFiles) {
        item.videoFiles.append(videoFileFromJson(value.toObject()));
    }
    return item;
}

QString sortFieldToApi(SessionSearchSortField field)
{
    switch (field) {
    case SessionSearchSortField::AverageScore:
        return QStringLiteral("averageScore");
    case SessionSearchSortField::BestScore:
        return QStringLiteral("bestScore");
    case SessionSearchSortField::ValidReps:
        return QStringLiteral("validReps");
    case SessionSearchSortField::DurationSec:
        return QStringLiteral("durationSec");
    case SessionSearchSortField::SavedAt:
    default:
        return QStringLiteral("savedAt");
    }
}

} // namespace

TrainingRepository::TrainingRepository() = default;

TrainingRepository::~TrainingRepository() = default;

bool TrainingRepository::open(QString *errorMessage)
{
    QSettings settings;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    m_baseUrl = settings.value(QStringLiteral("server/baseUrl"),
                               env.value(QStringLiteral("ISKATING_API_BASE_URL"),
                                         QStringLiteral("http://127.0.0.1:8000")))
                    .toString()
                    .trimmed();
    while (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
    m_accessToken = settings.value(QStringLiteral("auth/accessToken"),
                                   env.value(QStringLiteral("ISKATING_API_TOKEN")))
                        .toString()
                        .trimmed();

    bool ok = false;
    requestObject(QStringLiteral("GET"), QStringLiteral("/health"), {}, {}, &ok, errorMessage);
    if (!ok) {
        m_open = false;
        m_lastError = errorMessage ? *errorMessage : QStringLiteral("训练服务不可用。");
        return false;
    }

    if (m_accessToken.isEmpty() && !login(errorMessage)) {
        m_open = false;
        m_lastError = errorMessage ? *errorMessage : QStringLiteral("训练服务登录失败。");
        return false;
    }

    requestArray(QStringLiteral("/athletes"), {}, &ok, errorMessage);
    m_open = ok;
    if (!ok) {
        m_lastError = errorMessage ? *errorMessage : QStringLiteral("训练服务认证失败。");
        return false;
    }

    settings.setValue(QStringLiteral("server/baseUrl"), m_baseUrl);
    settings.setValue(QStringLiteral("auth/accessToken"), m_accessToken);
    m_lastError.clear();
    return true;
}

bool TrainingRepository::login(QString *errorMessage)
{
    QSettings settings;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString username = settings.value(QStringLiteral("auth/username"),
                                            env.value(QStringLiteral("ISKATING_API_USERNAME"),
                                                      QStringLiteral("admin")))
                                 .toString();
    const QString password = settings.value(QStringLiteral("auth/password"),
                                            env.value(QStringLiteral("ISKATING_API_PASSWORD"),
                                                      QStringLiteral("admin123")))
                                 .toString();
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/auth/login"),
                                               {{QStringLiteral("username"), username},
                                                {QStringLiteral("password"), password}},
                                               {},
                                               &ok,
                                               errorMessage);
    if (!ok) {
        return false;
    }
    m_accessToken = response.value(QStringLiteral("accessToken")).toString();
    if (m_accessToken.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练服务未返回访问令牌。");
        }
        return false;
    }
    settings.setValue(QStringLiteral("auth/accessToken"), m_accessToken);
    return true;
}

bool TrainingRepository::isOpen() const
{
    return m_open;
}

QString TrainingRepository::databasePath() const
{
    return m_baseUrl;
}

QString TrainingRepository::lastError() const
{
    return m_lastError;
}

QJsonObject TrainingRepository::requestObject(const QString &method,
                                              const QString &path,
                                              const QJsonObject &body,
                                              const QVariantMap &query,
                                              bool *ok,
                                              QString *errorMessage) const
{
    if (ok) {
        *ok = false;
    }
    QUrl url(m_baseUrl + apiPath(path));
    if (!query.isEmpty()) {
        QUrlQuery urlQuery;
        for (auto it = query.cbegin(); it != query.cend(); ++it) {
            const QString value = it.value().toString();
            if (!value.isEmpty()) {
                urlQuery.addQueryItem(it.key(), value);
            }
        }
        url.setQuery(urlQuery);
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_accessToken.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    }

    QNetworkReply *reply = nullptr;
    const QByteArray payload = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (method == QStringLiteral("GET")) {
        reply = m_network.get(request);
    } else if (method == QStringLiteral("PATCH")) {
        reply = m_network.sendCustomRequest(request, "PATCH", payload);
    } else if (method == QStringLiteral("DELETE")) {
        reply = m_network.sendCustomRequest(request, "DELETE", payload);
    } else {
        reply = m_network.post(request, payload);
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练服务请求超时：%1").arg(url.toString());
        }
        return {};
    }
    timer.stop();

    const QByteArray bytes = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
        QString detail = QString::fromUtf8(bytes);
        if (document.isObject()) {
            detail = document.object().value(QStringLiteral("detail")).toString(detail);
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练服务请求失败（HTTP %1）：%2").arg(status).arg(detail);
        }
        return {};
    }
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练服务返回格式无效：%1").arg(parseError.errorString());
        }
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return document.object();
}

QJsonArray TrainingRepository::requestArray(const QString &path,
                                            const QVariantMap &query,
                                            bool *ok,
                                            QString *errorMessage) const
{
    if (ok) {
        *ok = false;
    }
    QUrl url(m_baseUrl + apiPath(path));
    if (!query.isEmpty()) {
        QUrlQuery urlQuery;
        for (auto it = query.cbegin(); it != query.cend(); ++it) {
            const QString value = it.value().toString();
            if (!value.isEmpty()) {
                urlQuery.addQueryItem(it.key(), value);
            }
        }
        url.setQuery(urlQuery);
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_accessToken.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    }

    QNetworkReply *reply = m_network.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);
    loop.exec();
    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练服务请求超时：%1").arg(url.toString());
        }
        return {};
    }
    timer.stop();

    const QByteArray bytes = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练服务请求失败（HTTP %1）：%2").arg(status).arg(QString::fromUtf8(bytes));
        }
        return {};
    }
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练服务返回格式无效：%1").arg(parseError.errorString());
        }
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return document.array();
}

QVector<AthleteProfile> TrainingRepository::athletes() const
{
    QVector<AthleteProfile> result;
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/athletes"), {}, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(athleteFromJson(value.toObject()));
    }
    return result;
}

QVector<AthleteIdentitySample> TrainingRepository::identitySamples(const QString &athleteId, bool includeData) const
{
    QVector<AthleteIdentitySample> result;
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/athletes/%1/identity-samples").arg(athleteId), {}, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        AthleteIdentitySample sample = identitySampleFromJson(value.toObject());
        if (!includeData) {
            sample.dataBase64.clear();
        }
        result.append(sample);
    }
    return result;
}

QVector<AthleteIdentityEmbedding> TrainingRepository::identityGallery(const QString &athleteId,
                                                                       const QString &modelVersion,
                                                                       const QString &preprocessingVersion) const
{
    QVector<AthleteIdentityEmbedding> result;
    QVariantMap query{{QStringLiteral("modelVersion"), modelVersion},
                      {QStringLiteral("preprocessingVersion"), preprocessingVersion}};
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/athletes/%1/identity-gallery").arg(athleteId), query, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(identityEmbeddingFromJson(value.toObject()));
    }
    return result;
}

QVector<CoachProfile> TrainingRepository::coaches() const
{
    QVector<CoachProfile> result;
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/coaches"), {}, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(coachFromJson(value.toObject()));
    }
    return result;
}

QVector<Competition> TrainingRepository::competitions(bool includeInactive, const QString &query) const
{
    QVector<Competition> result;
    bool ok = false;
    const QVariantMap params{
        {QStringLiteral("includeInactive"), includeInactive ? QStringLiteral("true") : QStringLiteral("false")},
        {QStringLiteral("q"), query}
    };
    const QJsonArray array = requestArray(QStringLiteral("/competitions"), params, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(competitionFromJson(value.toObject()));
    }
    return result;
}

QVector<CompetitionEvent> TrainingRepository::competitionEvents(const QString &competitionId,
                                                                bool includeInactive,
                                                                const QString &query) const
{
    QVector<CompetitionEvent> result;
    bool ok = false;
    const QVariantMap params{
        {QStringLiteral("competitionId"), competitionId},
        {QStringLiteral("includeInactive"), includeInactive ? QStringLiteral("true") : QStringLiteral("false")},
        {QStringLiteral("q"), query}
    };
    const QJsonArray array = requestArray(QStringLiteral("/competition-events"), params, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(competitionEventFromJson(value.toObject()));
    }
    return result;
}

QVector<EventAthlete> TrainingRepository::eventAthletes(const QString &eventId,
                                                       const QString &athleteId,
                                                       bool includeInactive) const
{
    QVector<EventAthlete> result;
    bool ok = false;
    const QVariantMap params{
        {QStringLiteral("eventId"), eventId},
        {QStringLiteral("athleteId"), athleteId},
        {QStringLiteral("includeInactive"), includeInactive ? QStringLiteral("true") : QStringLiteral("false")}
    };
    const QJsonArray array = requestArray(QStringLiteral("/event-athletes"), params, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(eventAthleteFromJson(value.toObject()));
    }
    return result;
}

QVector<QString> TrainingRepository::athleteIdsForCoach(const QString &coachId) const
{
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("GET"),
                                               QStringLiteral("/coaches/%1/athletes").arg(coachId),
                                               {},
                                               {},
                                               &ok,
                                               nullptr);
    QVector<QString> result;
    if (!ok) {
        return result;
    }
    const QJsonArray ids = response.value(QStringLiteral("athleteIds")).toArray();
    for (const QJsonValue &id : ids) {
        result.append(id.toString());
    }
    return result;
}

QVector<ActionStandard> TrainingRepository::actionStandards() const
{
    QVector<ActionStandard> result;
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/action-standards"), {}, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(standardFromJson(value.toObject()));
    }
    return result;
}

SessionSearchResult TrainingRepository::searchSessions(const SessionSearchFilters &filters,
                                                       const SessionSearchPage &page,
                                                       const SessionSearchSort &sort) const
{
    SessionSearchResult result;
    QVariantMap query{
        {QStringLiteral("athleteId"), filters.athleteId},
        {QStringLiteral("coachId"), filters.coachId},
        {QStringLiteral("actionStandardId"), filters.actionStandardId},
        {QStringLiteral("competitionId"), filters.competitionId},
        {QStringLiteral("competitionEventId"), filters.competitionEventId},
        {QStringLiteral("eventAthleteId"), filters.eventAthleteId},
        {QStringLiteral("sourceType"), filters.sourceType},
        {QStringLiteral("competitionText"), filters.competitionText},
        {QStringLiteral("pageNumber"), page.pageNumber},
        {QStringLiteral("pageSize"), page.pageSize},
        {QStringLiteral("sortField"), sortFieldToApi(sort.field)},
        {QStringLiteral("descending"), sort.descending ? QStringLiteral("true") : QStringLiteral("false")}
    };
    if (filters.savedFrom.isValid()) {
        query.insert(QStringLiteral("savedFrom"), dateTimeToIso(filters.savedFrom));
    }
    if (filters.savedTo.isValid()) {
        query.insert(QStringLiteral("savedTo"), dateTimeToIso(filters.savedTo));
    }
    if (filters.minScore >= 0) {
        query.insert(QStringLiteral("minScore"), filters.minScore);
    }
    if (filters.maxScore >= 0) {
        query.insert(QStringLiteral("maxScore"), filters.maxScore);
    }

    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("GET"), QStringLiteral("/training/sessions"), {}, query, &ok, nullptr);
    if (!ok) {
        return result;
    }
    result.totalCount = jsonInt(response, QStringLiteral("totalCount"));
    result.pageNumber = jsonInt(response, QStringLiteral("pageNumber"), 1);
    result.pageSize = jsonInt(response, QStringLiteral("pageSize"), 10);
    const QJsonArray items = response.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        result.items.append(historyFromJson(value.toObject()));
    }
    return result;
}

RepetitionSearchResult TrainingRepository::searchRepetitions(const RepetitionSearchFilters &filters,
                                                             const SessionSearchPage &page) const
{
    RepetitionSearchResult result;
    QVariantMap query{
        {QStringLiteral("sessionId"), filters.sessionId},
        {QStringLiteral("athleteId"), filters.athleteId},
        {QStringLiteral("coachId"), filters.coachId},
        {QStringLiteral("actionStandardId"), filters.actionStandardId},
        {QStringLiteral("competitionId"), filters.competitionId},
        {QStringLiteral("competitionEventId"), filters.competitionEventId},
        {QStringLiteral("eventAthleteId"), filters.eventAthleteId},
        {QStringLiteral("sourceType"), filters.sourceType},
        {QStringLiteral("validState"), filters.validState.isEmpty() ? QStringLiteral("all") : filters.validState},
        {QStringLiteral("reviewStatus"), filters.reviewStatus},
        {QStringLiteral("repetitionSource"), filters.repetitionSource},
        {QStringLiteral("errorText"), filters.errorText},
        {QStringLiteral("pageNumber"), page.pageNumber},
        {QStringLiteral("pageSize"), page.pageSize}
    };
    if (filters.savedFrom.isValid()) {
        query.insert(QStringLiteral("savedFrom"), dateTimeToIso(filters.savedFrom));
    }
    if (filters.savedTo.isValid()) {
        query.insert(QStringLiteral("savedTo"), dateTimeToIso(filters.savedTo));
    }
    if (filters.minScore >= 0) {
        query.insert(QStringLiteral("minScore"), filters.minScore);
    }
    if (filters.maxScore >= 0) {
        query.insert(QStringLiteral("maxScore"), filters.maxScore);
    }
    if (filters.clipFromMs >= 0) {
        query.insert(QStringLiteral("clipFromMs"), filters.clipFromMs);
    }
    if (filters.clipToMs >= 0) {
        query.insert(QStringLiteral("clipToMs"), filters.clipToMs);
    }

    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("GET"), QStringLiteral("/training/repetitions"), {}, query, &ok, nullptr);
    if (!ok) {
        return result;
    }
    result.totalCount = jsonInt(response, QStringLiteral("totalCount"));
    result.pageNumber = jsonInt(response, QStringLiteral("pageNumber"), 1);
    result.pageSize = jsonInt(response, QStringLiteral("pageSize"), 50);
    const QJsonArray items = response.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        result.items.append(repetitionSearchItemFromJson(value.toObject()));
    }
    return result;
}

QVector<SessionHistoryItem> TrainingRepository::recentSessions(int limit) const
{
    SessionSearchPage page;
    page.pageSize = std::max(1, limit);
    return searchSessions({}, page, {}).items;
}

QVector<ActionRepetition> TrainingRepository::repetitionsForSession(const QString &sessionId) const
{
    QVector<ActionRepetition> result;
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/training/sessions/%1/repetitions").arg(sessionId), {}, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(repetitionFromJson(value.toObject()));
    }
    return result;
}

QVector<ActionRepetition> TrainingRepository::reviewedRepetitionsForSession(const QString &sessionId) const
{
    return repetitionsForSession(sessionId);
}

QVector<ParticipantPoseFrame> TrainingRepository::poseFramesForSession(const QString &sessionId,
                                                                       const QString &participantId,
                                                                       const QString &athleteId,
                                                                       int fromMs,
                                                                       int toMs,
                                                                       int limit) const
{
    QVariantMap query;
    if (!participantId.trimmed().isEmpty()) {
        query.insert(QStringLiteral("participantId"), participantId);
    }
    if (!athleteId.trimmed().isEmpty()) {
        query.insert(QStringLiteral("athleteId"), athleteId);
    }
    if (fromMs >= 0) {
        query.insert(QStringLiteral("fromMs"), fromMs);
    }
    if (toMs >= 0) {
        query.insert(QStringLiteral("toMs"), toMs);
    }
    query.insert(QStringLiteral("limit"), limit);
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/training/sessions/%1/pose-frames").arg(sessionId), query, &ok);
    QVector<ParticipantPoseFrame> frames;
    if (!ok) {
        return frames;
    }
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            frames.append(participantPoseFrameFromJson(value.toObject()));
        }
    }
    return frames;
}

QVector<TrackPoint> TrainingRepository::trackPointsForSession(const QString &sessionId,
                                                               const QString &participantId,
                                                               int fromMs,
                                                               int toMs,
                                                               int limit) const
{
    QVariantMap query;
    if (!participantId.trimmed().isEmpty()) {
        query.insert(QStringLiteral("participantId"), participantId);
    }
    if (fromMs >= 0) {
        query.insert(QStringLiteral("fromMs"), fromMs);
    }
    if (toMs >= 0) {
        query.insert(QStringLiteral("toMs"), toMs);
    }
    query.insert(QStringLiteral("limit"), limit);
    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/training/sessions/%1/track-points").arg(sessionId), query, &ok);
    QVector<TrackPoint> points;
    if (!ok) {
        return points;
    }
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            points.append(trackPointFromJson(value.toObject()));
        }
    }
    return points;
}

QVector<VideoFileCleanupCandidate> TrainingRepository::videoFiles(const QString &status,
                                                                  bool withLocalPathOnly,
                                                                  const QDateTime &modifiedBefore) const
{
    QVector<VideoFileCleanupCandidate> result;
    QVariantMap query{
        {QStringLiteral("status"), status},
        {QStringLiteral("withLocalPathOnly"), withLocalPathOnly ? QStringLiteral("true") : QStringLiteral("false")}
    };
    if (modifiedBefore.isValid()) {
        query.insert(QStringLiteral("modifiedBefore"), dateTimeToIso(modifiedBefore));
    }

    bool ok = false;
    const QJsonArray array = requestArray(QStringLiteral("/training/video-files"), query, &ok, nullptr);
    if (!ok) {
        return result;
    }
    for (const QJsonValue &value : array) {
        result.append(cleanupCandidateFromJson(value.toObject()));
    }
    return result;
}

TrainingTrendWindow TrainingRepository::trendForRecentDays(int days) const
{
    TrainingTrendWindow trend;
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("GET"),
                                               QStringLiteral("/training/trends"),
                                               {},
                                               {{QStringLiteral("days"), days}},
                                               &ok,
                                               nullptr);
    if (!ok) {
        return trend;
    }
    trend.days = jsonInt(response, QStringLiteral("days"), days);
    trend.sessionCount = jsonInt(response, QStringLiteral("sessionCount"));
    trend.averageScore = jsonInt(response, QStringLiteral("averageScore"));
    trend.bestScore = jsonInt(response, QStringLiteral("bestScore"));
    trend.completedReps = jsonInt(response, QStringLiteral("completedReps"));
    trend.detectionScore = jsonInt(response, QStringLiteral("detectionScore"));
    trend.symmetryScore = jsonInt(response, QStringLiteral("symmetryScore"));
    trend.balanceScore = jsonInt(response, QStringLiteral("balanceScore"));
    trend.stabilityScore = jsonInt(response, QStringLiteral("stabilityScore"));
    trend.depthScore = jsonInt(response, QStringLiteral("depthScore"));
    trend.weakestMetricName = jsonString(response, QStringLiteral("weakestMetricName"));
    trend.weakestMetricScore = jsonInt(response, QStringLiteral("weakestMetricScore"));
    return trend;
}

TrainingBaseline TrainingRepository::baselineFor(const QString &athleteId, const QString &actionStandardId) const
{
    TrainingBaseline baseline;
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("GET"),
                                               QStringLiteral("/training/baselines"),
                                               {},
                                               {{QStringLiteral("athleteId"), athleteId},
                                                {QStringLiteral("actionStandardId"), actionStandardId}},
                                               &ok,
                                               nullptr);
    if (!ok) {
        return baseline;
    }
    baseline.sessionCount = jsonInt(response, QStringLiteral("sessionCount"));
    baseline.averageScore = jsonInt(response, QStringLiteral("averageScore"));
    baseline.averageValidReps = jsonInt(response, QStringLiteral("averageValidReps"));
    return baseline;
}

bool TrainingRepository::saveCoachComment(const QString &sessionId, const QString &comment, QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("POST"),
                  QStringLiteral("/training/sessions/%1/coach-comment").arg(sessionId),
                  {{QStringLiteral("comment"), comment}},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::saveRepetitionReview(const ActionRepetition &repetition, QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("POST"),
                  QStringLiteral("/training/repetitions/%1/review").arg(repetition.id),
                  repetitionToJson(repetition),
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::createManualRepetition(const QString &sessionId,
                                                const QString &actionStandardId,
                                                int standardVersion,
                                                ActionRepetition *repetition,
                                                QString *errorMessage)
{
    if (!repetition) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("动作实例为空。");
        }
        return false;
    }
    repetition->id = ensureId(repetition->id);
    repetition->sessionId = sessionId;
    repetition->actionStandardId = actionStandardId;
    repetition->standardVersion = standardVersion;
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/training/sessions/%1/manual-repetitions").arg(sessionId),
                                               repetitionToJson(*repetition),
                                               {},
                                               &ok,
                                               errorMessage);
    if (ok) {
        repetition->id = response.value(QStringLiteral("id")).toString(repetition->id);
    }
    return ok;
}

bool TrainingRepository::saveActionStandard(ActionStandard *standard, QString *errorMessage)
{
    if (!standard) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("动作标准为空。");
        }
        return false;
    }
    standard->id = ensureId(standard->id);
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/action-standards"),
                                               standardToJson(*standard),
                                               {},
                                               &ok,
                                               errorMessage);
    if (ok) {
        *standard = standardFromJson(response);
    }
    return ok;
}

bool TrainingRepository::recalculateSessionSummary(const QString &sessionId, QString *errorMessage)
{
    Q_UNUSED(sessionId)
    Q_UNUSED(errorMessage)
    return true;
}

bool TrainingRepository::markVideoFileCleaned(const QString &videoFileId,
                                              const QString &reason,
                                              QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("POST"),
                  QStringLiteral("/training/video-files/%1/cleanup").arg(videoFileId),
                  {{QStringLiteral("reason"), reason}},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::saveOfflineAnalysisTask(OfflineAnalysisTask *task, QString *errorMessage)
{
    if (!task) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("离线分析任务为空。");
        }
        return false;
    }
    task->id = ensureId(task->id);
    if (task->status.trimmed().isEmpty()) {
        task->status = QStringLiteral("imported");
    }
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/offline-analysis/tasks"),
                                               offlineAnalysisTaskToJson(*task),
                                               {},
                                               &ok,
                                               errorMessage);
    if (ok) {
        *task = offlineAnalysisTaskFromJson(response);
    }
    return ok;
}

bool TrainingRepository::createOfflineAnalysisBatch(OfflineAnalysisBatch *batch,
                                                    const QVector<QString> &athleteIds,
                                                    QString *errorMessage)
{
    if (!batch || batch->sources.size() != 12) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("完整帧率分析批次必须包含 12 路视频。");
        }
        return false;
    }
    QJsonArray sources;
    for (OfflineAnalysisBatchSource &source : batch->sources) {
        source.id = ensureId(source.id);
        sources.append(offlineAnalysisBatchSourceToJson(source));
    }
    QJsonArray athletes;
    for (const QString &athleteId : athleteIds) {
        if (!athleteId.trimmed().isEmpty()) {
            athletes.append(athleteId.trimmed());
        }
    }
    QJsonObject metadata{{QStringLiteral("mode"), QStringLiteral("synchronized_12_camera")},
                         {QStringLiteral("athleteIds"), athletes}};
    const QJsonDocument customMetadata = QJsonDocument::fromJson(batch->metadataJson.toUtf8());
    if (customMetadata.isObject()) {
        for (auto it = customMetadata.object().constBegin(); it != customMetadata.object().constEnd(); ++it) {
            metadata.insert(it.key(), it.value());
        }
    }
    bool ok = false;
    const QJsonObject response = requestObject(
        QStringLiteral("POST"),
        QStringLiteral("/offline-analysis/batches"),
        {{QStringLiteral("id"), batch->id},
         {QStringLiteral("sourceStartedAt"), batch->sourceStartedAt.isValid() ? dateTimeToIso(batch->sourceStartedAt) : QString()},
         {QStringLiteral("metadata"), metadata},
         {QStringLiteral("sources"), sources}},
        {}, &ok, errorMessage);
    if (ok) {
        *batch = offlineAnalysisBatchFromJson(response);
    }
    return ok;
}

OfflineAnalysisBatch TrainingRepository::offlineAnalysisBatch(const QString &batchId, QString *errorMessage) const
{
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("GET"),
                                               QStringLiteral("/offline-analysis/batches/%1").arg(batchId),
                                               {}, {}, &ok, errorMessage);
    return ok ? offlineAnalysisBatchFromJson(response) : OfflineAnalysisBatch();
}

bool TrainingRepository::createOfflineAnalysisRun(const QString &batchId,
                                                  const QString &modelVersion,
                                                  const QString &preprocessingVersion,
                                                  OfflineAnalysisRun *run,
                                                  QString *errorMessage)
{
    if (!run) {
        return false;
    }
    bool ok = false;
    const QJsonObject response = requestObject(
        QStringLiteral("POST"),
        QStringLiteral("/offline-analysis/batches/%1/runs").arg(batchId),
        {{QStringLiteral("modelVersion"), modelVersion},
         {QStringLiteral("preprocessingVersion"), preprocessingVersion},
         {QStringLiteral("configuration"), QJsonObject{
             {QStringLiteral("analysisMode"), QStringLiteral("strict_full_frame")},
             {QStringLiteral("reidPolicy"), QStringLiteral("track_assisted")}
         }}},
        {}, &ok, errorMessage);
    if (ok) {
        *run = offlineAnalysisRunFromJson(response);
    }
    return ok;
}

OfflineAnalysisRun TrainingRepository::offlineAnalysisRun(const QString &runId, QString *errorMessage) const
{
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("GET"),
                                               QStringLiteral("/offline-analysis/runs/%1").arg(runId),
                                               {}, {}, &ok, errorMessage);
    return ok ? offlineAnalysisRunFromJson(response) : OfflineAnalysisRun();
}

bool TrainingRepository::cancelOfflineAnalysisRun(const QString &runId,
                                                  OfflineAnalysisRun *run,
                                                  QString *errorMessage)
{
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/offline-analysis/runs/%1/cancel").arg(runId),
                                               {}, {}, &ok, errorMessage);
    if (ok && run) {
        *run = offlineAnalysisRunFromJson(response);
    }
    return ok;
}

bool TrainingRepository::retryOfflineAnalysisRun(const QString &runId,
                                                 OfflineAnalysisRun *run,
                                                 QString *errorMessage)
{
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/offline-analysis/runs/%1/retry").arg(runId),
                                               {}, {}, &ok, errorMessage);
    if (ok && run) {
        *run = offlineAnalysisRunFromJson(response);
    }
    return ok;
}

bool TrainingRepository::activateOfflineAnalysisRun(const QString &runId,
                                                    OfflineAnalysisRun *run,
                                                    QString *errorMessage)
{
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/offline-analysis/runs/%1/activate").arg(runId),
                                               {}, {}, &ok, errorMessage);
    if (ok && run) {
        *run = offlineAnalysisRunFromJson(response);
    }
    return ok;
}

OfflineAnalysisFrameWindow TrainingRepository::offlineAnalysisFrames(const QString &runId,
                                                                     int cameraId,
                                                                     qint64 fromMs,
                                                                     qint64 toMs,
                                                                     QString *errorMessage) const
{
    bool ok = false;
    const QJsonObject response = requestObject(
        QStringLiteral("GET"),
        QStringLiteral("/offline-analysis/runs/%1/frames").arg(runId),
        {},
        {{QStringLiteral("cameraId"), cameraId},
         {QStringLiteral("fromMs"), QString::number(fromMs)},
         {QStringLiteral("toMs"), QString::number(toMs)}},
        &ok, errorMessage);
    return ok ? offlineAnalysisFrameWindowFromJson(response) : OfflineAnalysisFrameWindow();
}

bool TrainingRepository::createAthlete(const QString &name, QString *athleteId, QString *errorMessage)
{
    AthleteProfile athlete;
    athlete.id = newId();
    athlete.name = name;
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"), QStringLiteral("/athletes"), athleteToJson(athlete), {}, &ok, errorMessage);
    if (ok && athleteId) {
        *athleteId = response.value(QStringLiteral("id")).toString(athlete.id);
    }
    return ok;
}

bool TrainingRepository::createCoach(const QString &name, QString *coachId, QString *errorMessage)
{
    CoachProfile coach;
    coach.id = newId();
    coach.name = name;
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"), QStringLiteral("/coaches"), coachToJson(coach), {}, &ok, errorMessage);
    if (ok && coachId) {
        *coachId = response.value(QStringLiteral("id")).toString(coach.id);
    }
    return ok;
}

bool TrainingRepository::saveAthleteProfile(AthleteProfile *athlete, QString *errorMessage)
{
    if (!athlete) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("运动员档案为空。");
        }
        return false;
    }
    athlete->id = ensureId(athlete->id);
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"), QStringLiteral("/athletes"), athleteToJson(*athlete), {}, &ok, errorMessage);
    if (ok) {
        *athlete = athleteFromJson(response);
    }
    return ok;
}

bool TrainingRepository::archiveAthlete(const QString &athleteId, QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("PATCH"),
                  QStringLiteral("/athletes/%1").arg(athleteId),
                  {{QStringLiteral("active"), false}},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::uploadIdentitySample(const QString &athleteId,
                                              const QString &fileName,
                                              const QByteArray &data,
                                              const QString &modelVersion,
                                              const QString &preprocessingVersion,
                                              AthleteIdentitySample *sample,
                                              QString *errorMessage)
{
    const QJsonObject body{{QStringLiteral("fileName"), fileName},
                           {QStringLiteral("dataBase64"), QString::fromLatin1(data.toBase64())},
                           {QStringLiteral("modelVersion"), modelVersion},
                           {QStringLiteral("preprocessingVersion"), preprocessingVersion}};
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/athletes/%1/identity-samples").arg(athleteId),
                                               body,
                                               {},
                                               &ok,
                                               errorMessage);
    if (ok && sample) {
        *sample = identitySampleFromJson(response);
    }
    return ok;
}

bool TrainingRepository::saveIdentityEmbedding(const QString &athleteId,
                                               const QString &sampleId,
                                               const QVector<float> &embedding,
                                               const QString &modelVersion,
                                               const QString &preprocessingVersion,
                                               QString *errorMessage)
{
    QJsonArray values;
    for (const float value : embedding) {
        values.append(value);
    }
    const QJsonObject body{{QStringLiteral("embedding"), values},
                           {QStringLiteral("modelVersion"), modelVersion},
                           {QStringLiteral("preprocessingVersion"), preprocessingVersion}};
    bool ok = false;
    requestObject(QStringLiteral("POST"),
                  QStringLiteral("/athletes/%1/identity-samples/%2/embedding").arg(athleteId, sampleId),
                  body,
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::deleteIdentitySample(const QString &athleteId,
                                              const QString &sampleId,
                                              QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("DELETE"),
                  QStringLiteral("/athletes/%1/identity-samples/%2").arg(athleteId, sampleId),
                  {},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::saveCoachProfile(CoachProfile *coach, const QVector<QString> &athleteIds, QString *errorMessage)
{
    if (!coach) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("教练档案为空。");
        }
        return false;
    }
    coach->id = ensureId(coach->id);
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"), QStringLiteral("/coaches"), coachToJson(*coach, athleteIds), {}, &ok, errorMessage);
    if (ok) {
        *coach = coachFromJson(response);
    }
    return ok;
}

bool TrainingRepository::archiveCoach(const QString &coachId, QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("PATCH"),
                  QStringLiteral("/coaches/%1").arg(coachId),
                  {{QStringLiteral("active"), false}},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::saveCompetition(Competition *competition, QString *errorMessage)
{
    if (!competition) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("比赛信息为空。");
        }
        return false;
    }
    competition->id = ensureId(competition->id);
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/competitions"),
                                               competitionToJson(*competition),
                                               {},
                                               &ok,
                                               errorMessage);
    if (ok) {
        *competition = competitionFromJson(response);
    }
    return ok;
}

bool TrainingRepository::archiveCompetition(const QString &competitionId, QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("PATCH"),
                  QStringLiteral("/competitions/%1").arg(competitionId),
                  {{QStringLiteral("active"), false}},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::saveCompetitionEvent(CompetitionEvent *event, QString *errorMessage)
{
    if (!event) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("比赛场次为空。");
        }
        return false;
    }
    event->id = ensureId(event->id);
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/competition-events"),
                                               competitionEventToJson(*event),
                                               {},
                                               &ok,
                                               errorMessage);
    if (ok) {
        *event = competitionEventFromJson(response);
    }
    return ok;
}

bool TrainingRepository::archiveCompetitionEvent(const QString &eventId, QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("PATCH"),
                  QStringLiteral("/competition-events/%1").arg(eventId),
                  {{QStringLiteral("active"), false}},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::saveEventAthlete(EventAthlete *eventAthlete, QString *errorMessage)
{
    if (!eventAthlete) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("参赛运动员关系为空。");
        }
        return false;
    }
    eventAthlete->id = ensureId(eventAthlete->id);
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"),
                                               QStringLiteral("/event-athletes"),
                                               eventAthleteToJson(*eventAthlete),
                                               {},
                                               &ok,
                                               errorMessage);
    if (ok) {
        *eventAthlete = eventAthleteFromJson(response);
    }
    return ok;
}

bool TrainingRepository::archiveEventAthlete(const QString &eventAthleteId, QString *errorMessage)
{
    bool ok = false;
    requestObject(QStringLiteral("PATCH"),
                  QStringLiteral("/event-athletes/%1").arg(eventAthleteId),
                  {{QStringLiteral("active"), false}},
                  {},
                  &ok,
                  errorMessage);
    return ok;
}

bool TrainingRepository::ensureDailyTask(const QString &athleteId,
                                         const QString &coachId,
                                         const QString &actionStandardId,
                                         int standardVersion,
                                         int targetReps,
                                         int targetScore,
                                         int setCount,
                                         int restSeconds,
                                         const QString &site,
                                         const QString &trainingPhase,
                                         const QString &goal,
                                         QString *planId,
                                         QString *taskId,
                                         QString *errorMessage)
{
    const QJsonObject body{
        {QStringLiteral("athleteId"), athleteId},
        {QStringLiteral("coachId"), coachId},
        {QStringLiteral("actionStandardId"), actionStandardId},
        {QStringLiteral("standardVersion"), standardVersion},
        {QStringLiteral("targetReps"), targetReps},
        {QStringLiteral("targetScore"), targetScore},
        {QStringLiteral("setCount"), setCount},
        {QStringLiteral("restSeconds"), restSeconds},
        {QStringLiteral("site"), site},
        {QStringLiteral("trainingPhase"), trainingPhase},
        {QStringLiteral("goal"), goal},
        {QStringLiteral("date"), QDate::currentDate().toString(Qt::ISODate)}
    };
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"), QStringLiteral("/training/tasks/ensure-daily"), body, {}, &ok, errorMessage);
    if (!ok) {
        return false;
    }
    if (planId) {
        *planId = jsonString(response, QStringLiteral("planId"));
    }
    if (taskId) {
        *taskId = jsonString(response, QStringLiteral("taskId"));
    }
    return true;
}

bool TrainingRepository::saveTrainingSession(TrainingSession *session,
                                             const QVector<ActionRepetition> &repetitions,
                                             QString *errorMessage)
{
    if (!session) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("训练记录为空。");
        }
        return false;
    }
    session->id = ensureId(session->id);
    QHash<QString, QString> participantIds;
    for (TrainingSessionParticipant &participant : session->participants) {
        participant.id = ensureId(participant.id);
        participant.sessionId = session->id;
        participantIds.insert(participant.athleteId, participant.id);
    }
    QString defaultVideoFileId;
    int defaultVideoIndex = 1;
    for (TrainingVideoFile &file : session->videoFiles) {
        file.id = ensureId(file.id);
        file.sessionId = session->id;
        if (defaultVideoFileId.isEmpty() || file.videoIndex == 1) {
            defaultVideoFileId = file.id;
            defaultVideoIndex = file.videoIndex > 0 ? file.videoIndex : 1;
        }
    }
    QJsonArray reps;
    for (ActionRepetition repetition : repetitions) {
        repetition.id = ensureId(repetition.id);
        repetition.sessionId = session->id;
        if (repetition.videoIndex <= 0) {
            repetition.videoIndex = defaultVideoIndex;
        }
        if (repetition.videoFileId.trimmed().isEmpty()) {
            repetition.videoFileId = defaultVideoFileId;
        }
        reps.append(repetitionToJson(repetition));
    }
    QJsonArray participantReps;
    QVector<ParticipantRepetition> participantRepetitions = session->participantRepetitions;
    if (participantRepetitions.isEmpty()) {
        for (const ActionRepetition &repetition : repetitions) {
            ParticipantRepetition participantRepetition;
            static_cast<ActionRepetition &>(participantRepetition) = repetition;
            participantRepetitions.append(participantRepetition);
        }
    }
    for (ParticipantRepetition repetition : participantRepetitions) {
        if (repetition.participantRepetitionId.trimmed().isEmpty()) {
            repetition.participantRepetitionId = ensureId();
        }
        repetition.sessionId = session->id;
        if (repetition.videoIndex <= 0) {
            repetition.videoIndex = defaultVideoIndex;
        }
        if (repetition.videoFileId.trimmed().isEmpty()) {
            repetition.videoFileId = defaultVideoFileId;
        }
        participantReps.append(participantRepetitionToJson(repetition));
    }
    QJsonArray participantPoseFrames;
    const QVector<ParticipantPoseFrame> framesToSave = session->analysisBatchId.trimmed().isEmpty()
                                                           ? session->participantPoseFrames
                                                           : QVector<ParticipantPoseFrame>();
    for (ParticipantPoseFrame frame : framesToSave) {
        frame.id = ensureId(frame.id);
        frame.sessionId = session->id;
        if (frame.videoIndex <= 0) {
            frame.videoIndex = defaultVideoIndex;
        }
        if (frame.videoFileId.trimmed().isEmpty()) {
            frame.videoFileId = defaultVideoFileId;
        }
        participantPoseFrames.append(participantPoseFrameToJson(frame));
    }
    QJsonArray trackPoints;
    for (TrackPoint point : session->trackPoints) {
        point.id = ensureId(point.id);
        point.participantId = participantIds.value(point.participantId, point.participantId);
        if (point.participantId.trimmed().isEmpty()) {
            continue;
        }
        trackPoints.append(trackPointToJson(point));
    }
    const QJsonObject body{{QStringLiteral("session"), sessionToJson(*session)},
                           {QStringLiteral("repetitions"), reps},
                           {QStringLiteral("participantRepetitions"), participantReps},
                           {QStringLiteral("participantPoseFrames"), participantPoseFrames},
                           {QStringLiteral("trackPoints"), trackPoints}};
    bool ok = false;
    const QJsonObject response = requestObject(QStringLiteral("POST"), QStringLiteral("/training/sessions"), body, {}, &ok, errorMessage);
    if (ok) {
        session->id = response.value(QStringLiteral("id")).toString(session->id);
    }
    return ok;
}

QString TrainingRepository::ensureId(const QString &id) const
{
    const QString trimmed = id.trimmed();
    return trimmed.isEmpty() ? newId() : trimmed;
}
