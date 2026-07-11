#include "athleteanalysismanager.h"

#include "d3dframeextractor.h"
#include "rtspstream.h"
#include "tensortrtathletebackend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

constexpr int kDefaultAnalysisIntervalMs = 66;
constexpr int kDefaultStreamTargetFps = 5;

QString modelDirPath()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString deployed = appDir.absoluteFilePath(QStringLiteral("models/athlete"));
    if (QDir(deployed).exists()) {
        return deployed;
    }
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../models/athlete"));
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

int frameIntervalMsForFps(int fps)
{
    return std::max(1, 1000 / (fps > 0 ? fps : kDefaultStreamTargetFps));
}

} // namespace

class AthleteAnalysisWorker
{
public:
    using ResultCallback = AthleteAnalysisManager::ResultCallback;
    using StatusCallback = AthleteAnalysisManager::StatusCallback;

    explicit AthleteAnalysisWorker(QObject *receiver)
        : m_receiver(receiver)
    {
    }

    ~AthleteAnalysisWorker()
    {
        stop();
    }

    void setCallbacks(ResultCallback resultCallback, StatusCallback statusCallback)
    {
        QMutexLocker locker(&m_mutex);
        m_resultCallback = std::move(resultCallback);
        m_statusCallback = std::move(statusCallback);
    }

    void setGallery(const QVector<AthleteGalleryEntry> &gallery)
    {
        QMutexLocker locker(&m_mutex);
        m_gallery = gallery;
    }

    void setManualBindings(const QVector<AthleteIdentityBinding> &bindings)
    {
        QMutexLocker locker(&m_mutex);
        m_manualBindings = bindings;
    }

    void resetTracking()
    {
        m_backend.resetTracking();
    }

    void start()
    {
        if (m_thread) {
            return;
        }
        m_stopRequested = false;
        m_thread = QThread::create([this]() { run(); });
        m_thread->setObjectName(QStringLiteral("AthleteAnalysisWorker"));
        m_thread->start();
    }

    void stop()
    {
        m_stopRequested = true;
        if (!m_thread) {
            return;
        }
        if (!m_thread->wait(180000)) {
            qWarning() << "[AthleteAnalysis] worker did not stop while TensorRT was shutting down";
            m_thread->terminate();
            m_thread->wait();
        }
        delete m_thread;
        m_thread = nullptr;
        publishResults({});
    }

    void setActiveStreams(const QVector<AthleteAnalysisManager::AnalysisStream> &streams)
    {
        QVector<StreamState> states;
        states.reserve(streams.size());
        for (const AthleteAnalysisManager::AnalysisStream &stream : streams) {
            if (!stream.stream) {
                continue;
            }
            StreamState state;
            state.cameraId = stream.cameraId;
            state.sourceName = stream.sourceName.trimmed();
            state.targetFps = stream.targetFps > 0 ? stream.targetFps : kDefaultStreamTargetFps;
            state.priority = stream.priority;
            state.autoDegrade = stream.autoDegrade;
            state.stream = stream.stream;
            states.append(state);
        }
        QMutexLocker locker(&m_mutex);
        m_streams = std::move(states);
        m_nextStreamIndex = 0;
        m_lastResultMsec = 0;
    }

    void setAnalysisProfile(const QString &profile)
    {
        m_analysisIntervalMs.store(intervalForProfile(profile));
    }

    void setPaused(bool paused)
    {
        m_paused = paused;
    }

private:
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

    void run()
    {
        const QString modelPath = modelDirPath();
        QFile metadataFile(QDir(modelPath).absoluteFilePath(QStringLiteral("athlete_models.json")));
        if (metadataFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll());
            const QJsonObject runtime = document.object().value(QStringLiteral("runtime")).toObject();
            m_resultTtlMs = std::max<qint64>(1, static_cast<qint64>(runtime.value(QStringLiteral("resultTtlMs")).toDouble(m_resultTtlMs)));
        }
        publishStatus(QStringLiteral("运动员识别 AI 初始化中：%1").arg(modelPath), true);
        QString error;
        m_backend.setStatusCallback([this](const QString &status) { publishStatus(status, true); });
        if (!m_backend.initialize(modelPath, &error)) {
            publishStatus(QStringLiteral("运动员识别 AI 初始化失败：%1").arg(error), true);
            while (!m_stopRequested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return;
        }
        publishStatus(m_backend.statusText(), true);

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
        const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
        {
            QMutexLocker locker(&m_mutex);
            const int streamCount = m_streams.size();
            for (int attempt = 0; attempt < streamCount; ++attempt) {
                const int index = (m_nextStreamIndex + attempt) % streamCount;
                const StreamState &candidate = m_streams.at(index);
                const auto latestFrame = candidate.stream ? candidate.stream->latestFrame() : nullptr;
                if (!latestFrame || latestFrame->receivedMsec <= 0 || latestFrame->receivedMsec == candidate.lastFrameMsec) {
                    continue;
                }
                const int minimumInterval = frameIntervalMsForFps(candidate.targetFps)
                                            * (candidate.priority > 0 ? candidate.degradeLevel + 1 : 1);
                if (candidate.lastAnalyzedMsec > 0 && nowMsec - candidate.lastAnalyzedMsec < minimumInterval) {
                    continue;
                }
                selectedIndex = index;
                selectedStream = candidate;
                frame = latestFrame;
                m_nextStreamIndex = (index + 1) % streamCount;
                break;
            }
        }

        if (!frame || selectedIndex < 0) {
            expireResultsIfNeeded();
            return;
        }

        QString error;
        const auto started = std::chrono::steady_clock::now();
        const QImage rgb = m_extractor.copyToRgb(frame, &error);
        if (rgb.isNull()) {
            publishStatus(QStringLiteral("运动员识别 AI 取帧失败：%1").arg(error));
            return;
        }

        QVector<AthleteGalleryEntry> gallery;
        QVector<AthleteIdentityBinding> manualBindings;
        {
            QMutexLocker locker(&m_mutex);
            gallery = m_gallery;
            manualBindings = m_manualBindings;
        }
        m_backend.setGallery(gallery);
        m_backend.setManualBindings(manualBindings);
        AthleteFrameResult results = m_backend.infer(rgb, selectedStream.cameraId, frame->receivedMsec);
        results.sourceName = selectedStream.sourceName;
        const int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::steady_clock::now() - started)
                                                   .count());
        {
            QMutexLocker locker(&m_mutex);
            if (selectedIndex < m_streams.size()) {
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
        QMutexLocker locker(&m_mutex);
        if (m_lastResultMsec <= 0 || QDateTime::currentMSecsSinceEpoch() - m_lastResultMsec <= m_resultTtlMs) {
            return;
        }
        m_lastResultMsec = 0;
        locker.unlock();
        publishResults({});
    }

    void publishResults(const AthleteFrameResult &results)
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
    QVector<StreamState> m_streams;
    QVector<AthleteGalleryEntry> m_gallery;
    QVector<AthleteIdentityBinding> m_manualBindings;
    int m_nextStreamIndex = 0;
    qint64 m_lastResultMsec = 0;
    qint64 m_resultTtlMs = 350;
    QString m_lastPublishedStatus;
    ResultCallback m_resultCallback;
    StatusCallback m_statusCallback;
    std::atomic_bool m_stopRequested = false;
    std::atomic_bool m_paused = false;
    std::atomic_int m_analysisIntervalMs{kDefaultAnalysisIntervalMs};
    TensorRtAthleteBackend m_backend;
    D3DFrameExtractor m_extractor;
};

AthleteAnalysisManager::AthleteAnalysisManager(QObject *parent)
    : QObject(parent)
    , m_worker(new AthleteAnalysisWorker(this))
{
    m_worker->start();
}

AthleteAnalysisManager::~AthleteAnalysisManager()
{
    stop();
    delete m_worker;
    m_worker = nullptr;
}

void AthleteAnalysisManager::setResultCallback(ResultCallback callback)
{
    m_resultCallback = std::move(callback);
    if (m_worker) {
        m_worker->setCallbacks(m_resultCallback, m_statusCallback);
    }
}

void AthleteAnalysisManager::setStatusCallback(StatusCallback callback)
{
    m_statusCallback = std::move(callback);
    if (m_worker) {
        m_worker->setCallbacks(m_resultCallback, m_statusCallback);
    }
}

void AthleteAnalysisManager::setGallery(const QVector<AthleteGalleryEntry> &gallery)
{
    if (m_worker) {
        m_worker->setGallery(gallery);
    }
}

void AthleteAnalysisManager::setManualBindings(const QVector<AthleteIdentityBinding> &bindings)
{
    if (m_worker) {
        m_worker->setManualBindings(bindings);
    }
}

void AthleteAnalysisManager::resetTracking()
{
    if (m_worker) {
        m_worker->resetTracking();
    }
}

void AthleteAnalysisManager::setActiveStreams(const QVector<AnalysisStream> &streams)
{
    if (m_worker) {
        m_worker->setActiveStreams(streams);
    }
}

void AthleteAnalysisManager::setAnalysisProfile(const QString &profile)
{
    if (m_worker) {
        m_worker->setAnalysisProfile(profile);
    }
}

void AthleteAnalysisManager::setPaused(bool paused)
{
    if (m_worker) {
        m_worker->setPaused(paused);
    }
}

void AthleteAnalysisManager::stop()
{
    if (m_worker) {
        m_worker->stop();
    }
}
