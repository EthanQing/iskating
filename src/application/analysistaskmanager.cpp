#include "analysistaskmanager.h"

#include "trainingrepository.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {

QString probeMetadata(const OfflineVideoProbeResult &probe)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("codec"), probe.codecName},
        {QStringLiteral("resolution"), probe.resolution},
        {QStringLiteral("durationMs"), static_cast<double>(probe.durationMs)},
        {QStringLiteral("seekable"), probe.seekable},
        {QStringLiteral("d3d11vaReady"), probe.d3d11vaReady}
    }).toJson(QJsonDocument::Compact));
}

bool remoteRunIsActive(const QString &status)
{
    return status == QStringLiteral("queued") || status == QStringLiteral("running") || status == QStringLiteral("partial");
}

}

AnalysisTaskManager::AnalysisTaskManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<OfflineAnalysisTask>();
    qRegisterMetaType<OfflineVideoProbeResult>();
    qRegisterMetaType<OfflineAnalysisBatch>();
    qRegisterMetaType<OfflineAnalysisRun>();
    m_thread = QThread::create([this]() { run(); });
    m_thread->start();
}

AnalysisTaskManager::~AnalysisTaskManager()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stopping = true;
        m_waitCondition.wakeAll();
    }
    m_thread->wait(5000);
    delete m_thread;
}

QVector<AnalysisTask> AnalysisTaskManager::tasks() const
{
    QMutexLocker locker(&m_mutex);
    return m_tasks.values().toVector();
}

void AnalysisTaskManager::restorePendingTasks()
{
    enqueue(Job{JobKind::Restore});
}

void AnalysisTaskManager::refreshTasks()
{
    enqueue(Job{JobKind::Refresh});
}

QString AnalysisTaskManager::enqueueOfflineImport(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    AnalysisTask task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.type = QStringLiteral("offline_import");
    task.status = QStringLiteral("queued");
    task.inputJson = QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("videoPath"), fileInfo.absoluteFilePath()},
        {QStringLiteral("fileName"), fileInfo.fileName()}
    }).toJson(QJsonDocument::Compact));
    updateCachedTask(task);
    enqueue(Job{JobKind::OfflineImport, task, fileInfo.absoluteFilePath()});
    return task.id;
}

void AnalysisTaskManager::enqueueFullRateBatch(const OfflineAnalysisBatch &batch, const QVector<QString> &athleteIds)
{
    Job job;
    job.kind = JobKind::FullRateBatch;
    job.batch = batch;
    job.athleteIds = athleteIds;
    enqueue(job);
}

void AnalysisTaskManager::trackFullRateRun(const QString &taskId, const QString &runId)
{
    if (taskId.isEmpty() || runId.isEmpty()) return;
    AnalysisTask task;
    task.id = taskId;
    task.type = QStringLiteral("full_rate_batch");
    task.status = QStringLiteral("queued");
    task.inputJson = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("runId"), runId}}).toJson(QJsonDocument::Compact));
    enqueue(Job{JobKind::FullRateRun, task, {}, runId});
}

void AnalysisTaskManager::pauseTask(const QString &taskId)
{
    if (taskId.isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    m_pausedTasks.insert(taskId);
    if (m_tasks.contains(taskId)) {
        m_tasks[taskId].status = QStringLiteral("paused");
    }
    locker.unlock();
    emit taskUpdated(taskId);
}

void AnalysisTaskManager::resumeTask(const QString &taskId)
{
    Job job;
    {
        QMutexLocker locker(&m_mutex);
        m_pausedTasks.remove(taskId);
        if (!m_knownJobs.contains(taskId)) return;
        job = m_knownJobs.value(taskId);
        if (m_tasks.contains(taskId)) m_tasks[taskId].status = QStringLiteral("queued");
    }
    emit taskUpdated(taskId);
    enqueue(job);
}

void AnalysisTaskManager::cancelTask(const QString &taskId)
{
    if (taskId.isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    m_cancelledTasks.insert(taskId);
    m_pausedTasks.remove(taskId);
    if (m_tasks.contains(taskId)) m_tasks[taskId].status = QStringLiteral("cancelled");
    locker.unlock();
    emit taskUpdated(taskId);
    m_waitCondition.wakeAll();
}

void AnalysisTaskManager::enqueue(const Job &job)
{
    QMutexLocker locker(&m_mutex);
    if (!job.task.id.isEmpty()) {
        m_knownJobs.insert(job.task.id, job);
        for (const Job &queued : std::as_const(m_queue)) {
            if (queued.task.id == job.task.id) return;
        }
    }
    m_queue.enqueue(job);
    m_waitCondition.wakeOne();
}

void AnalysisTaskManager::run()
{
    TrainingRepository repository;
    QString error;
    if (!repository.open(&error)) {
        emit taskError(QString(), error);
    }
    while (true) {
        Job job;
        {
            QMutexLocker locker(&m_mutex);
            while (!m_stopping && m_queue.isEmpty()) m_waitCondition.wait(&m_mutex);
            if (m_stopping) return;
            job = m_queue.dequeue();
        }
        if (!repository.isOpen() && !repository.open(&error)) {
            emit taskError(job.task.id, error);
            continue;
        }
        runJob(job, repository);
    }
}

void AnalysisTaskManager::runJob(const Job &job, TrainingRepository &repository)
{
    if (job.kind == JobKind::Restore) {
        for (const QString &status : {QStringLiteral("running"), QStringLiteral("paused")}) {
            for (AnalysisTask task : repository.analysisTasks({}, status)) {
                task.status = QStringLiteral("paused");
                updateTask(task, repository);
            }
        }
        refreshTasks();
        return;
    }
    if (job.kind == JobKind::Refresh) {
        for (const AnalysisTask &task : repository.analysisTasks()) updateCachedTask(task);
        return;
    }
    if (isCancelled(job.task.id)) {
        AnalysisTask task = job.task;
        task.status = QStringLiteral("cancelled");
        updateTask(task, repository);
        finishJob(task.id);
        return;
    }
    if (isPaused(job.task.id)) {
        AnalysisTask task = job.task;
        task.status = QStringLiteral("paused");
        updateTask(task, repository);
        return;
    }
    if (job.kind == JobKind::OfflineImport) runOfflineImport(job, repository);
    if (job.kind == JobKind::FullRateBatch) runFullRateBatch(job, repository);
    if (job.kind == JobKind::FullRateRun) runFullRateRun(job, repository);
}

void AnalysisTaskManager::runOfflineImport(const Job &job, TrainingRepository &repository)
{
    AnalysisTask task = job.task;
    task.status = QStringLiteral("running");
    task.progress = 5;
    updateTask(task, repository);
    const QFileInfo fileInfo(job.filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        task.status = QStringLiteral("failed");
        task.errorMessage = QStringLiteral("找不到导入的视频文件。");
        updateTask(task, repository);
        finishJob(task.id);
        return;
    }
    task.progress = 25;
    updateTask(task, repository);
    const OfflineVideoProbeResult probe = OfflineVideoProbe::probe(fileInfo.absoluteFilePath());
    if (!probe.success) {
        task.status = QStringLiteral("failed");
        task.errorMessage = probe.message;
        updateTask(task, repository);
        finishJob(task.id);
        return;
    }
    if (isCancelled(task.id) || isPaused(task.id)) {
        task.status = isCancelled(task.id) ? QStringLiteral("cancelled") : QStringLiteral("paused");
        updateTask(task, repository);
        if (task.status == QStringLiteral("cancelled")) finishJob(task.id);
        return;
    }
    task.progress = 65;
    updateTask(task, repository);
    OfflineAnalysisTask offlineTask;
    offlineTask.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    offlineTask.analysisTaskId = task.id;
    offlineTask.batchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    offlineTask.videoPath = fileInfo.absoluteFilePath();
    offlineTask.fileName = fileInfo.fileName();
    offlineTask.fileSizeBytes = fileInfo.size();
    offlineTask.fileModifiedAt = fileInfo.lastModified();
    offlineTask.durationMs = static_cast<int>(std::max<qint64>(0, probe.durationMs));
    offlineTask.probeMetadataJson = probeMetadata(probe);
    offlineTask.summaryMetadataJson = QStringLiteral("{\"mode\":\"single_video\",\"multiVideoReserved\":true}");
    QString error;
    if (!repository.saveOfflineAnalysisTask(&offlineTask, &error)) {
        task.status = QStringLiteral("failed");
        task.errorMessage = error;
        updateTask(task, repository);
        finishJob(task.id);
        return;
    }
    task.status = QStringLiteral("completed");
    task.progress = 100;
    task.errorMessage.clear();
    updateTask(task, repository);
    emit offlineImportReady(offlineTask, probe);
    finishJob(task.id);
}

void AnalysisTaskManager::runFullRateBatch(const Job &job, TrainingRepository &repository)
{
    OfflineAnalysisBatch batch = job.batch;
    QString error;
    if (!repository.createOfflineAnalysisBatch(&batch, job.athleteIds, &error)) {
        emit taskError(QString(), error);
        return;
    }
    OfflineAnalysisRun run;
    if (!repository.createOfflineAnalysisRun(batch.id,
                                             QStringLiteral("personvit-msmt17-vit-base-v1"),
                                             QStringLiteral("rgb-256x128-mean0.5-std0.5-l2-v1"),
                                             &run, &error)) {
        AnalysisTask task;
        task.id = batch.analysisTaskId;
        task.type = QStringLiteral("full_rate_batch");
        task.status = QStringLiteral("failed");
        task.errorMessage = error;
        updateTask(task, repository);
        emit taskError(task.id, error);
        return;
    }
    AnalysisTask task;
    task.id = batch.analysisTaskId;
    task.type = QStringLiteral("full_rate_batch");
    task.status = QStringLiteral("queued");
    task.inputJson = QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("batchId"), batch.id}, {QStringLiteral("runId"), run.id}
    }).toJson(QJsonDocument::Compact));
    updateTask(task, repository);
    emit fullRateRunReady(batch, run);
    runFullRateRun(Job{JobKind::FullRateRun, task, {}, run.id}, repository);
}

void AnalysisTaskManager::runFullRateRun(const Job &job, TrainingRepository &repository)
{
    AnalysisTask task = job.task;
    while (true) {
        if (isCancelled(task.id)) {
            OfflineAnalysisRun run;
            QString error;
            repository.cancelOfflineAnalysisRun(job.runId, &run, &error);
            task.status = QStringLiteral("cancelled");
            task.errorMessage = error;
            updateTask(task, repository);
            finishJob(task.id);
            return;
        }
        if (isPaused(task.id)) {
            task.status = QStringLiteral("paused");
            updateTask(task, repository);
            return;
        }
        QString error;
        const OfflineAnalysisRun run = repository.offlineAnalysisRun(job.runId, &error);
        if (run.id.isEmpty()) {
            task.status = QStringLiteral("failed");
            task.errorMessage = error.isEmpty() ? QStringLiteral("无法读取远端分析运行。") : error;
            updateTask(task, repository);
            finishJob(task.id);
            return;
        }
        task.status = remoteRunIsActive(run.status) ? QStringLiteral("running") : run.status;
        if (!remoteRunIsActive(run.status) && task.status != QStringLiteral("completed") && task.status != QStringLiteral("failed")
            && task.status != QStringLiteral("cancelled")) {
            task.status = QStringLiteral("failed");
        }
        task.progress = run.progress;
        task.errorMessage = run.errorMessage;
        updateTask(task, repository);
        if (!remoteRunIsActive(run.status)) {
            finishJob(task.id);
            return;
        }
        QThread::msleep(2000);
    }
}

void AnalysisTaskManager::updateTask(AnalysisTask task, TrainingRepository &repository)
{
    QString error;
    if (!repository.saveAnalysisTask(&task, &error)) {
        task.errorMessage = error;
        emit taskError(task.id, error);
    }
    updateCachedTask(task);
}

void AnalysisTaskManager::updateCachedTask(const AnalysisTask &task)
{
    {
        QMutexLocker locker(&m_mutex);
        m_tasks.insert(task.id, task);
    }
    emit taskUpdated(task.id);
}

bool AnalysisTaskManager::isPaused(const QString &taskId) const
{
    QMutexLocker locker(&m_mutex);
    return m_pausedTasks.contains(taskId);
}

bool AnalysisTaskManager::isCancelled(const QString &taskId) const
{
    QMutexLocker locker(&m_mutex);
    return m_cancelledTasks.contains(taskId);
}

void AnalysisTaskManager::finishJob(const QString &taskId)
{
    QMutexLocker locker(&m_mutex);
    m_knownJobs.remove(taskId);
    m_pausedTasks.remove(taskId);
    m_cancelledTasks.remove(taskId);
}
