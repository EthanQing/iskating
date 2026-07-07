#include "trainingrepository.h"

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
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
        {QStringLiteral("sourceType"), session.sourceType},
        {QStringLiteral("sourceRef"), session.sourceRef},
        {QStringLiteral("feedback"), session.feedback},
        {QStringLiteral("notes"), session.notes},
        {QStringLiteral("coachComment"), session.coachComment}
    };
}

QJsonObject repetitionToJson(const ActionRepetition &repetition)
{
    return {
        {QStringLiteral("id"), repetition.id},
        {QStringLiteral("sessionId"), repetition.sessionId},
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
        {QStringLiteral("keyFramePoseJson"), repetition.keyFramePoseJson}
    };
}

ActionRepetition repetitionFromJson(const QJsonObject &object)
{
    ActionRepetition repetition;
    repetition.id = jsonString(object, QStringLiteral("id"));
    repetition.sessionId = jsonString(object, QStringLiteral("sessionId"));
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
    return repetition;
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
    item.sourceType = jsonString(object, QStringLiteral("sourceType"));
    if (item.sourceType.trimmed().isEmpty()) {
        item.sourceType = QStringLiteral("training");
    }
    item.sourceRef = jsonString(object, QStringLiteral("sourceRef"));
    item.sourceLabel = jsonString(object, QStringLiteral("sourceLabel"));
    item.feedback = jsonString(object, QStringLiteral("feedback"));
    item.notes = jsonString(object, QStringLiteral("notes"));
    item.coachComment = jsonString(object, QStringLiteral("coachComment"));
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
    QJsonArray reps;
    for (ActionRepetition repetition : repetitions) {
        repetition.id = ensureId(repetition.id);
        repetition.sessionId = session->id;
        reps.append(repetitionToJson(repetition));
    }
    const QJsonObject body{{QStringLiteral("session"), sessionToJson(*session)}, {QStringLiteral("repetitions"), reps}};
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
