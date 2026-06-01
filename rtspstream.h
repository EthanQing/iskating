#ifndef RTSPSTREAM_H
#define RTSPSTREAM_H

#include "d3dframe.h"

#include <QMutex>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

class RtspStream
{
public:
    enum class State {
        Idle,
        Connecting,
        Playing,
        Reconnecting,
        Stopped,
        Error,
    };

    explicit RtspStream(QString url, qint64 initialSeekMs = 0);
    ~RtspStream();

    RtspStream(const RtspStream &) = delete;
    RtspStream &operator=(const RtspStream &) = delete;

    void start();
    void stop();
    void pause(bool paused);

    QString url() const;
    State state() const;
    QString statusText() const;
    QString lastError() const;
    std::shared_ptr<D3DFrame> latestFrame() const;

private:
    static int ffmpegInterruptCallback(void *opaque);

    void run();
    void setState(State state, const QString &message = QString());
    void setFatalError(const QString &message);
    void setLatestFrame(std::shared_ptr<D3DFrame> frame);
    bool openAndDecodeOnce();

    QString m_url;
    qint64 m_initialSeekMs = 0;
    mutable QMutex m_mutex;
    std::shared_ptr<D3DFrame> m_latestFrame;
    State m_state = State::Idle;
    QString m_statusText = QStringLiteral("未连接");
    QString m_lastError;
    QThread *m_thread = nullptr;
    std::atomic_bool m_stopRequested = false;
    std::atomic_bool m_fatalError = false;
    std::atomic_bool m_paused = false;
};

#endif // RTSPSTREAM_H
