#ifndef ATHLETEANALYSISMANAGER_H
#define ATHLETEANALYSISMANAGER_H

#include "athleteanalysisresult.h"

#include <QObject>
#include <QVector>

#include <functional>
#include <memory>

class RtspStream;
class AthleteAnalysisWorker;

class AthleteAnalysisManager : public QObject
{
    Q_OBJECT

public:
    using ResultCallback = std::function<void(const AthleteFrameResult &)>;
    using StatusCallback = std::function<void(const QString &)>;

    struct AnalysisStream
    {
        int cameraId = 0;
        QString sourceName;
        int targetFps = 5;
        int priority = 0;
        bool autoDegrade = true;
        std::shared_ptr<RtspStream> stream;
    };

    explicit AthleteAnalysisManager(QObject *parent = nullptr);
    ~AthleteAnalysisManager() override;

    void setResultCallback(ResultCallback callback);
    void setStatusCallback(StatusCallback callback);
    void setGallery(const QVector<AthleteGalleryEntry> &gallery);
    void setManualBindings(const QVector<AthleteIdentityBinding> &bindings);
    void resetTracking();
    void setActiveStreams(const QVector<AnalysisStream> &streams);
    void setAnalysisProfile(const QString &profile);
    void setPaused(bool paused);
    void stop();

private:
    AthleteAnalysisWorker *m_worker = nullptr;
    ResultCallback m_resultCallback;
    StatusCallback m_statusCallback;
};

#endif // ATHLETEANALYSISMANAGER_H
