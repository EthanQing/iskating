#ifndef ANALYSISTASKMANAGER_H
#define ANALYSISTASKMANAGER_H

#include "offlinevideoprobe.h"
#include "trainingdomain.h"

#include <QHash>
#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <QWaitCondition>

class AnalysisTaskManager : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisTaskManager(QObject *parent = nullptr);
    ~AnalysisTaskManager() override;

    QVector<AnalysisTask> tasks() const;
    void restorePendingTasks();
    void refreshTasks();
    QString enqueueOfflineImport(const QString &filePath);
    void enqueueFullRateBatch(const OfflineAnalysisBatch &batch, const QVector<QString> &athleteIds);
    void trackFullRateRun(const QString &taskId, const QString &runId);
    void pauseTask(const QString &taskId);
    void resumeTask(const QString &taskId);
    void cancelTask(const QString &taskId);

signals:
    void taskUpdated(const QString &taskId);
    void offlineImportReady(const OfflineAnalysisTask &task, const OfflineVideoProbeResult &probe);
    void fullRateRunReady(const OfflineAnalysisBatch &batch, const OfflineAnalysisRun &run);
    void taskError(const QString &taskId, const QString &message);

private:
    enum class JobKind { OfflineImport, FullRateBatch, FullRateRun, Restore, Refresh };
    struct Job {
        JobKind kind = JobKind::Refresh;
        AnalysisTask task;
        QString filePath;
        QString runId;
        OfflineAnalysisBatch batch;
        QVector<QString> athleteIds;
    };

    void enqueue(const Job &job);
    void run();
    void runJob(const Job &job, class TrainingRepository &repository);
    void runOfflineImport(const Job &job, class TrainingRepository &repository);
    void runFullRateBatch(const Job &job, class TrainingRepository &repository);
    void runFullRateRun(const Job &job, class TrainingRepository &repository);
    void updateTask(AnalysisTask task, class TrainingRepository &repository);
    void updateCachedTask(const AnalysisTask &task);
    bool isPaused(const QString &taskId) const;
    bool isCancelled(const QString &taskId) const;
    void finishJob(const QString &taskId);

    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QQueue<Job> m_queue;
    QHash<QString, Job> m_knownJobs;
    QHash<QString, AnalysisTask> m_tasks;
    QSet<QString> m_pausedTasks;
    QSet<QString> m_cancelledTasks;
    QThread *m_thread = nullptr;
    bool m_stopping = false;
};

Q_DECLARE_METATYPE(OfflineAnalysisTask)
Q_DECLARE_METATYPE(OfflineVideoProbeResult)
Q_DECLARE_METATYPE(OfflineAnalysisBatch)
Q_DECLARE_METATYPE(OfflineAnalysisRun)

#endif // ANALYSISTASKMANAGER_H
