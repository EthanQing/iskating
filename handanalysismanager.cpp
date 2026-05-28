#include "handanalysismanager.h"

#include "d3dframeextractor.h"
#include "rtspstream.h"
#include "tensorrtbodyposebackend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

namespace {

constexpr int kAnalysisIntervalMs = 66;
constexpr qint64 kResultTtlMs = 350;

QString modelDirPath()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString deployed = appDir.absoluteFilePath(QStringLiteral("models/body"));
    if (QDir(deployed).exists()) {
        return deployed;
    }
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../models/body"));
}

} // namespace

class HandAnalysisWorker
{
public:
    using ResultCallback = HandAnalysisManager::ResultCallback;
    using StatusCallback = HandAnalysisManager::StatusCallback;

    explicit HandAnalysisWorker(QObject *receiver)
        : m_receiver(receiver)
    {
    }

    ~HandAnalysisWorker()
    {
        stop();
    }

    void setCallbacks(ResultCallback resultCallback, StatusCallback statusCallback)
    {
        QString statusToReplay;
        QMutexLocker locker(&m_mutex);
        m_resultCallback = std::move(resultCallback);
        m_statusCallback = std::move(statusCallback);
        if (m_statusCallback && !m_lastPublishedStatus.isEmpty()) {
            statusToReplay = m_lastPublishedStatus;
        }
        locker.unlock();
        if (!statusToReplay.isEmpty()) {
            publishStatus(statusToReplay, true);
        }
    }

    void start()
    {
        if (m_thread) {
            return;
        }
        m_stopRequested = false;
        m_thread = QThread::create([this]() { run(); });
        m_thread->setObjectName(QStringLiteral("HandAnalysisWorker"));
        m_thread->start();
    }

    void stop()
    {
        m_stopRequested = true;
        if (m_thread) {
            if (!m_thread->wait(180000)) {
                qWarning() << "[HandAnalysis] worker did not stop while TensorRT was shutting down";
                m_thread->terminate();
                m_thread->wait();
            }
            delete m_thread;
            m_thread = nullptr;
        }
        publishResults({});
    }

    void setActiveStream(int cameraId, std::shared_ptr<RtspStream> stream)
    {
        QMutexLocker locker(&m_mutex);
        m_cameraId = cameraId;
        m_stream = std::move(stream);
        m_lastFrameMsec = 0;
        m_lastResultMsec = 0;
    }

    void setPaused(bool paused)
    {
        m_paused = paused;
        if (paused) {
            publishStatus(QStringLiteral("人体姿态 AI 已暂停"));
        }
    }

private:
    void run()
    {
        const QString modelPath = modelDirPath();
        publishStatus(QStringLiteral("人体姿态 AI 初始化中：%1").arg(modelPath));

        QString error;
        m_backend.setStatusCallback([this](const QString &status) {
            publishStatus(status, true);
        });
        if (!m_backend.initialize(modelPath, &error)) {
            publishStatus(QStringLiteral("人体姿态 AI 初始化失败：%1").arg(error));
            qWarning() << "[HandAnalysis]" << error;
            while (!m_stopRequested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return;
        }
        publishStatus(m_backend.statusText());

        while (!m_stopRequested) {
            const auto started = std::chrono::steady_clock::now();
            if (!m_paused) {
                analyzeLatestFrame();
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
            const int sleepMs = std::max<int>(1, kAnalysisIntervalMs - static_cast<int>(elapsed));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }

    void analyzeLatestFrame()
    {
        std::shared_ptr<RtspStream> stream;
        int cameraId = 0;
        qint64 lastFrameMsec = 0;
        {
            QMutexLocker locker(&m_mutex);
            stream = m_stream;
            cameraId = m_cameraId;
            lastFrameMsec = m_lastFrameMsec;
        }

        if (!stream) {
            expireResultsIfNeeded();
            return;
        }

        const auto frame = stream->latestFrame();
        if (!frame || frame->receivedMsec <= 0 || frame->receivedMsec == lastFrameMsec) {
            expireResultsIfNeeded();
            return;
        }

        QString error;
        QImage rgb = m_extractor.copyToRgb(frame, &error);
        if (rgb.isNull()) {
            publishStatus(QStringLiteral("人体姿态 AI 取帧失败：%1").arg(error));
            return;
        }

        PoseFrameResult results = m_backend.infer(rgb, cameraId, frame->receivedMsec);
        {
            QMutexLocker locker(&m_mutex);
            m_lastFrameMsec = frame->receivedMsec;
            m_lastResultMsec = results.instances.isEmpty() ? m_lastResultMsec : QDateTime::currentMSecsSinceEpoch();
        }
        publishResults(results);
        publishStatus(m_backend.statusText());
    }

    void expireResultsIfNeeded()
    {
        qint64 lastResultMsec = 0;
        {
            QMutexLocker locker(&m_mutex);
            lastResultMsec = m_lastResultMsec;
        }
        if (lastResultMsec > 0 && QDateTime::currentMSecsSinceEpoch() - lastResultMsec > kResultTtlMs) {
            {
                QMutexLocker locker(&m_mutex);
                m_lastResultMsec = 0;
            }
            publishResults({});
        }
    }

    void publishResults(const PoseFrameResult &results)
    {
        ResultCallback callback;
        {
            QMutexLocker locker(&m_mutex);
            callback = m_resultCallback;
        }
        if (!callback || !m_receiver) {
            return;
        }
        QMetaObject::invokeMethod(m_receiver,
                                  [callback, results]() { callback(results); },
                                  Qt::QueuedConnection);
    }

    void publishStatus(const QString &status, bool force = false)
    {
        StatusCallback callback;
        {
            QMutexLocker locker(&m_mutex);
            if (!force && status == m_lastPublishedStatus) {
                return;
            }
            m_lastPublishedStatus = status;
            callback = m_statusCallback;
        }
        if (!callback || !m_receiver) {
            return;
        }
        QMetaObject::invokeMethod(m_receiver,
                                  [callback, status]() { callback(status); },
                                  Qt::QueuedConnection);
    }

    QObject *m_receiver = nullptr;
    QThread *m_thread = nullptr;
    QMutex m_mutex;
    std::shared_ptr<RtspStream> m_stream;
    int m_cameraId = 0;
    qint64 m_lastFrameMsec = 0;
    qint64 m_lastResultMsec = 0;
    QString m_lastPublishedStatus;
    ResultCallback m_resultCallback;
    StatusCallback m_statusCallback;
    std::atomic_bool m_stopRequested = false;
    std::atomic_bool m_paused = false;
    TensorRtBodyPoseBackend m_backend;
    D3DFrameExtractor m_extractor;
};

HandAnalysisManager::HandAnalysisManager(QObject *parent)
    : QObject(parent)
    , m_worker(new HandAnalysisWorker(this))
{
    m_worker->start();
}

HandAnalysisManager::~HandAnalysisManager()
{
    stop();
    delete m_worker;
    m_worker = nullptr;
}

void HandAnalysisManager::setResultCallback(ResultCallback callback)
{
    m_resultCallback = std::move(callback);
    if (m_worker) {
        m_worker->setCallbacks(m_resultCallback, m_statusCallback);
    }
}

void HandAnalysisManager::setStatusCallback(StatusCallback callback)
{
    m_statusCallback = std::move(callback);
    if (m_worker) {
        m_worker->setCallbacks(m_resultCallback, m_statusCallback);
    }
}

void HandAnalysisManager::setActiveStream(int cameraId, std::shared_ptr<RtspStream> stream)
{
    if (m_worker) {
        m_worker->setActiveStream(cameraId, std::move(stream));
    }
}

void HandAnalysisManager::setPaused(bool paused)
{
    if (m_worker) {
        m_worker->setPaused(paused);
    }
}

void HandAnalysisManager::stop()
{
    if (m_worker) {
        m_worker->stop();
    }
}
