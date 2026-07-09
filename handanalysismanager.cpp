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

constexpr int kDefaultAnalysisIntervalMs = 66;
constexpr int kDefaultStreamTargetFps = 5;
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

int intervalForProfile(const QString &profile)
{
    const QString normalized = profile.trimmed().toLower();
    if (normalized == QStringLiteral("fast")) {
        return 100;
    }
    if (normalized == QStringLiteral("high")) {
        return 33;
    }
    return kDefaultAnalysisIntervalMs;
}

QString profileLabel(const QString &profile)
{
    const QString normalized = profile.trimmed().toLower();
    if (normalized == QStringLiteral("fast")) {
        return QStringLiteral("快速");
    }
    if (normalized == QStringLiteral("high")) {
        return QStringLiteral("高精度");
    }
    return QStringLiteral("平衡");
}

int frameIntervalMsForFps(int fps)
{
    if (fps <= 0) {
        fps = kDefaultStreamTargetFps;
    }
    return std::max(1, 1000 / fps);
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
        HandAnalysisManager::AnalysisStream analysisStream;
        analysisStream.cameraId = cameraId;
        analysisStream.sourceName = cameraId > 0
                                        ? QStringLiteral("CAM %1").arg(cameraId, 2, 10, QLatin1Char('0'))
                                        : QStringLiteral("离线视频");
        analysisStream.targetFps = kDefaultStreamTargetFps;
        analysisStream.priority = 0;
        analysisStream.autoDegrade = true;
        analysisStream.stream = std::move(stream);
        QVector<HandAnalysisManager::AnalysisStream> streams;
        streams.append(analysisStream);
        setActiveStreams(streams);
    }

    void setActiveStreams(const QVector<HandAnalysisManager::AnalysisStream> &streams)
    {
        QVector<StreamState> states;
        states.reserve(streams.size());
        for (const HandAnalysisManager::AnalysisStream &stream : streams) {
            if (!stream.stream) {
                continue;
            }
            StreamState state;
            state.cameraId = stream.cameraId;
            state.sourceName = stream.sourceName.trimmed().isEmpty()
                                   ? (stream.cameraId > 0
                                          ? QStringLiteral("CAM %1").arg(stream.cameraId, 2, 10, QLatin1Char('0'))
                                          : QStringLiteral("离线视频"))
                                   : stream.sourceName.trimmed();
            state.targetFps = stream.targetFps > 0 ? stream.targetFps : kDefaultStreamTargetFps;
            state.priority = stream.priority;
            state.autoDegrade = stream.autoDegrade;
            state.stream = stream.stream;
            states.append(state);
        }

        const int streamCount = states.size();
        const int targetFps = streamCount > 0 ? states.first().targetFps : kDefaultStreamTargetFps;
        const bool autoDegrade = streamCount > 0 ? states.first().autoDegrade : true;
        {
            QMutexLocker locker(&m_mutex);
            m_streams = std::move(states);
            m_nextStreamIndex = 0;
            m_lastResultMsec = 0;
        }
        if (streamCount > 0) {
            publishStatus(QStringLiteral("人体姿态 AI：%1 路，目标 %2 FPS，主机位优先%3")
                              .arg(streamCount)
                              .arg(targetFps)
                              .arg(autoDegrade ? QStringLiteral("，自动降级") : QString()),
                          true);
        }
    }

    void setAnalysisProfile(const QString &profile)
    {
        const int intervalMs = intervalForProfile(profile);
        m_analysisIntervalMs.store(intervalMs);
        publishStatus(QStringLiteral("人体姿态 AI 分析档位：%1（约 %2 ms/轮询）")
                          .arg(profileLabel(profile))
                          .arg(intervalMs),
                      true);
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
            const int sleepMs = std::max<int>(1, m_analysisIntervalMs.load() - static_cast<int>(elapsed));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }

    void analyzeLatestFrame()
    {
        std::shared_ptr<D3DFrame> frame;
        StreamState selectedStream;
        int selectedIndex = -1;
        bool hasStreams = true;
        const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
        {
            QMutexLocker locker(&m_mutex);
            const int streamCount = m_streams.size();
            if (streamCount <= 0) {
                hasStreams = false;
            } else {
                for (int attempt = 0; attempt < streamCount; ++attempt) {
                    const int index = (m_nextStreamIndex + attempt) % streamCount;
                    const StreamState &candidate = m_streams.at(index);
                    if (!candidate.stream) {
                        continue;
                    }

                    const auto latestFrame = candidate.stream->latestFrame();
                    if (!latestFrame || latestFrame->receivedMsec <= 0 || latestFrame->receivedMsec == candidate.lastFrameMsec) {
                        continue;
                    }
                    const int degradeMultiplier = candidate.priority > 0 ? (candidate.degradeLevel + 1) : 1;
                    const int minIntervalMs = frameIntervalMsForFps(candidate.targetFps) * degradeMultiplier;
                    if (candidate.lastAnalyzedMsec > 0 && nowMsec - candidate.lastAnalyzedMsec < minIntervalMs) {
                        continue;
                    }

                    selectedIndex = index;
                    selectedStream = candidate;
                    frame = latestFrame;
                    m_nextStreamIndex = (index + 1) % streamCount;
                    break;
                }
            }
        }

        if (!hasStreams || !frame || selectedIndex < 0) {
            expireResultsIfNeeded();
            return;
        }

        QString error;
        const auto started = std::chrono::steady_clock::now();
        QImage rgb = m_extractor.copyToRgb(frame, &error);
        if (rgb.isNull()) {
            publishStatus(QStringLiteral("人体姿态 AI 取帧失败：%1").arg(error));
            return;
        }

        PoseFrameResult results = m_backend.infer(rgb, selectedStream.cameraId, frame->receivedMsec);
        const int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::steady_clock::now() - started)
                                                   .count());
        results.sourceName = selectedStream.sourceName;
        {
            QMutexLocker locker(&m_mutex);
            if (selectedIndex >= 0
                && selectedIndex < m_streams.size()
                && m_streams.at(selectedIndex).stream == selectedStream.stream
                && m_streams.at(selectedIndex).cameraId == selectedStream.cameraId) {
                m_streams[selectedIndex].lastFrameMsec = frame->receivedMsec;
                m_streams[selectedIndex].lastAnalyzedMsec = QDateTime::currentMSecsSinceEpoch();
                if (m_streams[selectedIndex].autoDegrade && m_streams[selectedIndex].priority > 0) {
                    const int budgetMs = frameIntervalMsForFps(m_streams[selectedIndex].targetFps);
                    if (elapsedMs > budgetMs) {
                        m_streams[selectedIndex].degradeLevel = std::min(3, m_streams[selectedIndex].degradeLevel + 1);
                    } else if (m_streams[selectedIndex].degradeLevel > 0) {
                        --m_streams[selectedIndex].degradeLevel;
                    }
                }
            }
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
    struct StreamState
    {
        int cameraId = 0;
        QString sourceName;
        int targetFps = kDefaultStreamTargetFps;
        int priority = 0;
        bool autoDegrade = true;
        std::shared_ptr<RtspStream> stream;
        qint64 lastFrameMsec = 0;
        qint64 lastAnalyzedMsec = 0;
        int degradeLevel = 0;
    };
    QVector<StreamState> m_streams;
    int m_nextStreamIndex = 0;
    qint64 m_lastResultMsec = 0;
    QString m_lastPublishedStatus;
    ResultCallback m_resultCallback;
    StatusCallback m_statusCallback;
    std::atomic_bool m_stopRequested = false;
    std::atomic_bool m_paused = false;
    std::atomic_int m_analysisIntervalMs{kDefaultAnalysisIntervalMs};
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

void HandAnalysisManager::setActiveStreams(const QVector<AnalysisStream> &streams)
{
    if (m_worker) {
        m_worker->setActiveStreams(streams);
    }
}

void HandAnalysisManager::setAnalysisProfile(const QString &profile)
{
    if (m_worker) {
        m_worker->setAnalysisProfile(profile);
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
