#ifndef HANDANALYSISMANAGER_H
#define HANDANALYSISMANAGER_H

#include "handposeresult.h"

#include <QObject>
#include <QVector>

#include <functional>
#include <memory>

class RtspStream;
class HandAnalysisWorker;

class HandAnalysisManager : public QObject
{
    Q_OBJECT

public:
    using ResultCallback = std::function<void(const QVector<HandPoseResult> &)>;
    using StatusCallback = std::function<void(const QString &)>;

    explicit HandAnalysisManager(QObject *parent = nullptr);
    ~HandAnalysisManager() override;

    void setResultCallback(ResultCallback callback);
    void setStatusCallback(StatusCallback callback);
    void setActiveStream(int cameraId, std::shared_ptr<RtspStream> stream);
    void setPaused(bool paused);
    void stop();

private:
    HandAnalysisWorker *m_worker = nullptr;
    ResultCallback m_resultCallback;
    StatusCallback m_statusCallback;
};

#endif // HANDANALYSISMANAGER_H
