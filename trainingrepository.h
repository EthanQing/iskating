#ifndef TRAININGREPOSITORY_H
#define TRAININGREPOSITORY_H

#include "trainingdomain.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

class TrainingRepository
{
public:
    TrainingRepository();
    ~TrainingRepository();

    bool open(QString *errorMessage = nullptr);
    bool isOpen() const;
    QString databasePath() const;
    QString lastError() const;

    QVector<AthleteProfile> athletes() const;
    QVector<CoachProfile> coaches() const;
    QVector<Competition> competitions(bool includeInactive = false, const QString &query = QString()) const;
    QVector<CompetitionEvent> competitionEvents(const QString &competitionId = QString(),
                                                bool includeInactive = false,
                                                const QString &query = QString()) const;
    QVector<EventAthlete> eventAthletes(const QString &eventId = QString(),
                                        const QString &athleteId = QString(),
                                        bool includeInactive = false) const;
    QVector<QString> athleteIdsForCoach(const QString &coachId) const;
    QVector<ActionStandard> actionStandards() const;
    SessionSearchResult searchSessions(const SessionSearchFilters &filters,
                                       const SessionSearchPage &page,
                                       const SessionSearchSort &sort) const;
    QVector<SessionHistoryItem> recentSessions(int limit) const;
    QVector<ActionRepetition> repetitionsForSession(const QString &sessionId) const;
    QVector<ActionRepetition> reviewedRepetitionsForSession(const QString &sessionId) const;
    TrainingTrendWindow trendForRecentDays(int days) const;
    TrainingBaseline baselineFor(const QString &athleteId, const QString &actionStandardId) const;
    bool saveCoachComment(const QString &sessionId,
                          const QString &comment,
                          QString *errorMessage = nullptr);
    bool saveRepetitionReview(const ActionRepetition &repetition,
                              QString *errorMessage = nullptr);
    bool createManualRepetition(const QString &sessionId,
                                const QString &actionStandardId,
                                int standardVersion,
                                ActionRepetition *repetition,
                                QString *errorMessage = nullptr);
    bool saveActionStandard(ActionStandard *standard,
                            QString *errorMessage = nullptr);
    bool recalculateSessionSummary(const QString &sessionId,
                                   QString *errorMessage = nullptr);

    bool createAthlete(const QString &name, QString *athleteId, QString *errorMessage = nullptr);
    bool createCoach(const QString &name, QString *coachId, QString *errorMessage = nullptr);
    bool saveAthleteProfile(AthleteProfile *athlete,
                            QString *errorMessage = nullptr);
    bool archiveAthlete(const QString &athleteId,
                        QString *errorMessage = nullptr);
    bool saveCoachProfile(CoachProfile *coach,
                          const QVector<QString> &athleteIds,
                          QString *errorMessage = nullptr);
    bool archiveCoach(const QString &coachId,
                      QString *errorMessage = nullptr);
    bool saveCompetition(Competition *competition,
                         QString *errorMessage = nullptr);
    bool archiveCompetition(const QString &competitionId,
                            QString *errorMessage = nullptr);
    bool saveCompetitionEvent(CompetitionEvent *event,
                              QString *errorMessage = nullptr);
    bool archiveCompetitionEvent(const QString &eventId,
                                 QString *errorMessage = nullptr);
    bool saveEventAthlete(EventAthlete *eventAthlete,
                          QString *errorMessage = nullptr);
    bool archiveEventAthlete(const QString &eventAthleteId,
                             QString *errorMessage = nullptr);
    bool ensureDailyTask(const QString &athleteId,
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
                         QString *errorMessage = nullptr);
    bool saveTrainingSession(TrainingSession *session,
                             const QVector<ActionRepetition> &repetitions,
                             QString *errorMessage = nullptr);

private:
    bool login(QString *errorMessage);
    QJsonObject requestObject(const QString &method,
                              const QString &path,
                              const QJsonObject &body = {},
                              const QVariantMap &query = {},
                              bool *ok = nullptr,
                              QString *errorMessage = nullptr) const;
    QJsonArray requestArray(const QString &path,
                            const QVariantMap &query = {},
                            bool *ok = nullptr,
                            QString *errorMessage = nullptr) const;
    QString ensureId(const QString &id = QString()) const;

    mutable QNetworkAccessManager m_network;
    QString m_baseUrl;
    QString m_accessToken;
    bool m_open = false;
    QString m_lastError;
};

#endif // TRAININGREPOSITORY_H
