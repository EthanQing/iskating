#ifndef TRAININGREPOSITORY_H
#define TRAININGREPOSITORY_H

#include "trainingdomain.h"

#include <QSqlDatabase>
#include <QString>
#include <QVariant>
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
    QVector<ActionStandard> actionStandards() const;
    QVector<SessionHistoryItem> recentSessions(int limit) const;
    QVector<ActionRepetition> repetitionsForSession(const QString &sessionId) const;
    TrainingBaseline baselineFor(const QString &athleteId, const QString &actionStandardId) const;

    bool createAthlete(const QString &name, QString *athleteId, QString *errorMessage = nullptr);
    bool createCoach(const QString &name, QString *coachId, QString *errorMessage = nullptr);
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
    bool migrate(QString *errorMessage);
    bool seedDefaults(QString *errorMessage);
    bool migrateLegacyTrainingHistory(QString *errorMessage);
    bool execute(const QString &sql, QString *errorMessage) const;
    bool hasMetaValue(const QString &key) const;
    bool setMetaValue(const QString &key, const QString &value, QString *errorMessage) const;
    QString ensureId(const QString &id = QString()) const;
    QString scalarString(const QString &sql, const QVariantList &args = {}) const;
    int scalarInt(const QString &sql, const QVariantList &args = {}, int defaultValue = 0) const;
    void refreshBaseline(const QString &athleteId, const QString &actionStandardId);

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_databasePath;
    QString m_lastError;
};

#endif // TRAININGREPOSITORY_H
