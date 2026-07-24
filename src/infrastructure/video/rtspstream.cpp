#include "rtspstream.h"
#include "d3d11videodevice.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

namespace {

constexpr qint64 kLongOutageThresholdMs = 30000;

QString avError(int rc)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

bool hasUrlScheme(const QString &source)
{
    return source.trimmed().contains(QStringLiteral("://"));
}

QString safeUrlForLog(const QString &source)
{
    QUrl url = QUrl::fromEncoded(source.toUtf8(), QUrl::TolerantMode);
    if (!url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
        return url.toString(QUrl::FullyEncoded);
    }
    return source;
}

QString sourceForFfmpeg(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (hasUrlScheme(trimmed)) {
        return trimmed;
    }
    return QFileInfo(trimmed).absoluteFilePath();
}

QString formatMilliseconds(qint64 milliseconds)
{
    const qint64 clamped = std::max<qint64>(0, milliseconds);
    const qint64 totalSeconds = clamped / 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg((clamped % 1000) / 100, 1, 10, QLatin1Char('0'));
}

QString formatDurationMs(qint64 milliseconds)
{
    const qint64 clamped = std::max<qint64>(0, milliseconds);
    if (clamped < 1000) {
        return QStringLiteral("%1 ms").arg(clamped);
    }
    return QStringLiteral("%1 秒").arg((clamped + 999) / 1000);
}

bool isRealtimeRtspSource(const QString &source)
{
    return source.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive);
}

qint64 mediaDurationMs(const AVFormatContext *format)
{
    if (!format || format->duration == AV_NOPTS_VALUE || format->duration <= 0) {
        return -1;
    }
    return av_rescale_q(format->duration, AVRational{1, AV_TIME_BASE}, AVRational{1, 1000});
}

qint64 frameMediaTimeMs(const AVFrame *frame, const AVStream *stream)
{
    if (!frame || !stream) {
        return -1;
    }
    int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) {
        timestamp = frame->pts;
    }
    if (timestamp == AV_NOPTS_VALUE) {
        return -1;
    }
    if (stream->start_time != AV_NOPTS_VALUE) {
        timestamp -= stream->start_time;
    }
    return std::max<qint64>(0, av_rescale_q(timestamp, stream->time_base, AVRational{1, 1000}));
}

enum AVPixelFormat d3d11GetFormat(AVCodecContext *, const enum AVPixelFormat *formats)
{
    for (const enum AVPixelFormat *fmt = formats; *fmt != AV_PIX_FMT_NONE; ++fmt) {
        if (*fmt == AV_PIX_FMT_D3D11) {
            return *fmt;
        }
    }
    return AV_PIX_FMT_NONE;
}

bool codecSupportsD3D11(const AVCodec *codec)
{
    for (int i = 0;; ++i) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) {
            break;
        }
        if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA
            && config->pix_fmt == AV_PIX_FMT_D3D11
            && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            return true;
        }
    }
    return false;
}

void setRtspOptions(AVDictionary **options, const QString &url, const char *transport)
{
    av_dict_set(options, "fflags", "nobuffer", 0);
    av_dict_set(options, "flags", "low_delay", 0);
    av_dict_set(options, "max_delay", "0", 0);
    av_dict_set(options, "probesize", "32768", 0);
    av_dict_set(options, "analyzeduration", "0", 0);
    av_dict_set(options, "timeout", "3000000", 0);
    av_dict_set(options, "reorder_queue_size", "0", 0);
    av_dict_set(options, "buffer_size", "1048576", 0);

    if (url.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive)) {
        av_dict_set(options, "rtsp_transport", transport, 0);
    }
}

template <typename T, void (*FreeFn)(T **)>
struct AvPtr
{
    ~AvPtr() { reset(); }
    T *get() const { return ptr; }
    T **put() { reset(); return &ptr; }
    T *operator->() const { return ptr; }
    void reset()
    {
        if (ptr) {
            FreeFn(&ptr);
        }
    }
    T *ptr = nullptr;
};

struct AvBufferRefPtr
{
    ~AvBufferRefPtr()
    {
        if (ptr) {
            av_buffer_unref(&ptr);
        }
    }

    AvBufferRefPtr(const AvBufferRefPtr &) = delete;
    AvBufferRefPtr &operator=(const AvBufferRefPtr &) = delete;

    explicit AvBufferRefPtr(AVBufferRef *ref = nullptr)
        : ptr(ref)
    {
    }

    AVBufferRef *get() const { return ptr; }
    AVBufferRef *ptr = nullptr;
};

void freeFormat(AVFormatContext **ctx)
{
    if (ctx && *ctx) {
        avformat_close_input(ctx);
    }
}

void freeCodec(AVCodecContext **ctx)
{
    avcodec_free_context(ctx);
}

void freePacket(AVPacket **packet)
{
    av_packet_free(packet);
}

void freeFrame(AVFrame **frame)
{
    av_frame_free(frame);
}

} // namespace

RtspStream::RtspStream(QString url, qint64 initialSeekMs)
    : m_url(std::move(url))
    , m_initialSeekMs(std::max<qint64>(0, initialSeekMs))
{
}

RtspStream::~RtspStream()
{
    stop();
}

void RtspStream::start()
{
    if (m_thread) {
        return;
    }

    m_stopRequested = false;
    m_fatalError = false;
    m_thread = QThread::create([this]() { run(); });
    m_thread->setObjectName(QStringLiteral("RtspStream:%1").arg(safeUrlForLog(m_url)));
    m_thread->start();
}

void RtspStream::stop()
{
    m_stopRequested = true;
    if (m_thread) {
        if (!m_thread->wait(8000)) {
            qWarning() << "[RtspStream] worker did not stop after FFmpeg interrupt, terminating as last resort"
                       << safeUrlForLog(m_url);
            m_thread->terminate();
            m_thread->wait();
        }
        delete m_thread;
        m_thread = nullptr;
    }
    setLatestFrame({});
    {
        QMutexLocker locker(&m_mutex);
        m_positionMs = -1;
    }
    setState(State::Stopped, QStringLiteral("已停止"));
}

void RtspStream::pause(bool paused)
{
    m_paused = paused;
    setState(paused ? State::Idle : State::Playing, paused ? QStringLiteral("已暂停") : QStringLiteral("播放中"));
}

void RtspStream::seekTo(qint64 positionMs)
{
    if (!isSeekable()) {
        return;
    }
    m_pendingSeekMs = std::max<qint64>(0, positionMs);
    m_paused = false;
}

void RtspStream::setPlaybackRate(double rate)
{
    m_playbackRate = std::clamp(rate, 0.1, 4.0);
}

void RtspStream::stepForward()
{
    if (!isSeekable()) {
        return;
    }
    m_pendingStepFrames.fetch_add(1);
    m_paused = false;
}

QString RtspStream::url() const
{
    return m_url;
}

RtspStream::State RtspStream::state() const
{
    QMutexLocker locker(&m_mutex);
    return m_state;
}

QString RtspStream::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_statusText;
}

QString RtspStream::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

qint64 RtspStream::positionMs() const
{
    QMutexLocker locker(&m_mutex);
    return m_positionMs;
}

qint64 RtspStream::durationMs() const
{
    QMutexLocker locker(&m_mutex);
    return m_durationMs;
}

bool RtspStream::isSeekable() const
{
    QMutexLocker locker(&m_mutex);
    return m_seekable;
}

std::shared_ptr<D3DFrame> RtspStream::latestFrame() const
{
    QMutexLocker locker(&m_mutex);
    return m_latestFrame;
}

void RtspStream::run()
{
    const int reconnectDelaysMs[] = {1000, 2000, 5000, 5000};
    int reconnectAttempt = 0;

    while (!m_stopRequested && !m_fatalError) {
        const bool ok = openAndDecodeOnce();
        if (m_stopRequested || m_fatalError) {
            break;
        }

        if (ok) {
            reconnectAttempt = 0;
            continue;
        }

        const int delay = reconnectDelaysMs[std::min(reconnectAttempt, 3)];
        ++reconnectAttempt;
        recordReconnectScheduled(delay);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
        while (!m_stopRequested && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

int RtspStream::ffmpegInterruptCallback(void *opaque)
{
    auto *stream = static_cast<RtspStream *>(opaque);
    return stream && stream->m_stopRequested ? 1 : 0;
}

void RtspStream::setState(State state, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    m_state = state;
    if (!message.isEmpty()) {
        m_statusText = message;
        if (state == State::Error) {
            m_lastError = message;
        }
    }
}

void RtspStream::setFatalError(const QString &message)
{
    m_fatalError = true;
    setState(State::Error, message);
}

void RtspStream::setLatestFrame(std::shared_ptr<D3DFrame> frame)
{
    QMutexLocker locker(&m_mutex);
    if (frame && frame->mediaTimeMs >= 0) {
        m_positionMs = frame->mediaTimeMs;
    }
    m_latestFrame = std::move(frame);
}

void RtspStream::setCurrentTransport(const QString &transport)
{
    QMutexLocker locker(&m_mutex);
    m_currentTransport = transport;
}

void RtspStream::recordStreamInterrupted(const QString &message)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QString transport;
    bool newOutage = false;
    {
        QMutexLocker locker(&m_mutex);
        transport = m_currentTransport;
        m_lastError = message;
        if (m_lastDisconnectUnixMs < 0 || m_lastRecoverUnixMs > m_lastDisconnectUnixMs) {
            m_lastDisconnectUnixMs = now;
            m_longOutage = false;
            newOutage = true;
        }
    }

    qWarning() << "[RtspStream] stream interrupted"
               << safeUrlForLog(m_url)
               << "transport=" << (transport.isEmpty() ? QStringLiteral("unknown") : transport)
               << "newOutage=" << newOutage
               << "error=" << message;
}

void RtspStream::recordReconnectScheduled(int delayMs)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int attempt = 0;
    int total = 0;
    qint64 outageMs = 0;
    bool longOutage = false;
    QString transport;
    {
        QMutexLocker locker(&m_mutex);
        ++m_consecutiveReconnects;
        ++m_totalReconnects;
        attempt = m_consecutiveReconnects;
        total = m_totalReconnects;
        transport = m_currentTransport;
        if (m_lastDisconnectUnixMs < 0) {
            m_lastDisconnectUnixMs = now;
        }
        outageMs = now - m_lastDisconnectUnixMs;
        longOutage = outageMs >= kLongOutageThresholdMs;
        if (longOutage) {
            m_longOutage = true;
            m_statusText = QStringLiteral("长时间断流，请检查摄像头网络或 RTSP 配置（第 %1 次重连）").arg(attempt);
        } else {
            m_statusText = QStringLiteral("断流重连中，%1 秒后重连（第 %2 次）").arg(delayMs / 1000).arg(attempt);
        }
        m_state = State::Reconnecting;
    }

    qWarning() << "[RtspStream] reconnect scheduled"
               << safeUrlForLog(m_url)
               << "transport=" << (transport.isEmpty() ? QStringLiteral("unknown") : transport)
               << "attempt=" << attempt
               << "total=" << total
               << "delayMs=" << delayMs
               << "outageDuration=" << formatDurationMs(outageMs);
    if (longOutage) {
        qWarning() << "[RtspStream] long outage"
                   << safeUrlForLog(m_url)
                   << "transport=" << (transport.isEmpty() ? QStringLiteral("unknown") : transport)
                   << "attempt=" << attempt
                   << "outageDuration=" << formatDurationMs(outageMs);
    }
}

void RtspStream::recordStreamRecovered(const QString &transport)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int previousAttempts = 0;
    int total = 0;
    qint64 outageMs = 0;
    bool hadOutage = false;
    {
        QMutexLocker locker(&m_mutex);
        hadOutage = m_lastDisconnectUnixMs >= 0 && m_lastRecoverUnixMs <= m_lastDisconnectUnixMs;
        if (hadOutage) {
            outageMs = now - m_lastDisconnectUnixMs;
        }
        previousAttempts = m_consecutiveReconnects;
        total = m_totalReconnects;
        m_lastRecoverUnixMs = now;
        m_consecutiveReconnects = 0;
        m_longOutage = false;
        m_currentTransport = transport;
    }

    if (hadOutage) {
        qDebug() << "[RtspStream] stream recovered"
                 << safeUrlForLog(m_url)
                 << "transport=" << transport
                 << "attempts=" << previousAttempts
                 << "total=" << total
                 << "outageDuration=" << formatDurationMs(outageMs);
    }
}

bool RtspStream::openAndDecodeOnce()
{
    const QString ffmpegSource = sourceForFfmpeg(m_url);
    const bool realtimeRtsp = ffmpegSource.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive);
    const QString initialTransport = realtimeRtsp ? QStringLiteral("udp") : QStringLiteral("file");
    setCurrentTransport(initialTransport);
    setState(State::Connecting, realtimeRtsp ? QStringLiteral("UDP 连接中") : QStringLiteral("连接中"));

    QString hwError;
    AVBufferRef *rawHwDevice = D3D11VideoDevice::createFfmpegHwDevice(&hwError);
    if (!rawHwDevice) {
        setFatalError(QStringLiteral("D3D11VA 不可用：%1").arg(hwError));
        return false;
    }
    AvBufferRefPtr hwDevice(rawHwDevice);

    AvPtr<AVFormatContext, freeFormat> format;
    format.ptr = avformat_alloc_context();
    if (!format.get()) {
        setState(State::Error, QStringLiteral("创建 FFmpeg 输入上下文失败"));
        return false;
    }
    format->interrupt_callback.callback = &RtspStream::ffmpegInterruptCallback;
    format->interrupt_callback.opaque = this;

    AVDictionary *options = nullptr;
    int attemptForLog = 1;
    {
        QMutexLocker locker(&m_mutex);
        attemptForLog = m_consecutiveReconnects + 1;
    }
    qDebug() << "[RtspStream] open attempt"
             << safeUrlForLog(m_url)
             << "transport=" << initialTransport
             << "attempt=" << attemptForLog;
    setRtspOptions(&options, ffmpegSource, "udp");
    AVFormatContext *rawFormat = format.get();
    int rc = avformat_open_input(&rawFormat, ffmpegSource.toUtf8().constData(), nullptr, &options);
    format.ptr = rawFormat;
    av_dict_free(&options);
    if (rc < 0 && !m_stopRequested && realtimeRtsp) {
        const QString udpError = avError(rc);
        qWarning() << "[RtspStream] udp failed, retry tcp"
                   << safeUrlForLog(m_url)
                   << "transport=udp"
                   << "attempt=" << attemptForLog
                   << "error=" << udpError;
        setCurrentTransport(QStringLiteral("tcp"));
        setState(State::Connecting, QStringLiteral("UDP 失败，切换 TCP"));
        format.reset();
        format.ptr = avformat_alloc_context();
        if (!format.get()) {
            setState(State::Error, QStringLiteral("创建 FFmpeg 输入上下文失败"));
            return false;
        }
        format->interrupt_callback.callback = &RtspStream::ffmpegInterruptCallback;
        format->interrupt_callback.opaque = this;
        qDebug() << "[RtspStream] open attempt"
                 << safeUrlForLog(m_url)
                 << "transport=tcp"
                 << "attempt=" << attemptForLog;
        setRtspOptions(&options, ffmpegSource, "tcp");
        rawFormat = format.get();
        rc = avformat_open_input(&rawFormat, ffmpegSource.toUtf8().constData(), nullptr, &options);
        format.ptr = rawFormat;
        av_dict_free(&options);
    }
    if (rc < 0) {
        const QString error = QStringLiteral("打开视频源失败：%1").arg(avError(rc));
        recordStreamInterrupted(error);
        setState(State::Error, error);
        return false;
    }

    format->flags |= AVFMT_FLAG_NOBUFFER;
    rc = avformat_find_stream_info(format.get(), nullptr);
    if (rc < 0) {
        const QString error = QStringLiteral("读取视频流信息失败：%1").arg(avError(rc));
        recordStreamInterrupted(error);
        setState(State::Error, error);
        return false;
    }

    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        const QString error = QStringLiteral("视频源没有可用视频轨道");
        recordStreamInterrupted(error);
        setState(State::Error, error);
        return false;
    }

    AVStream *stream = format->streams[streamIndex];
    const bool seekableSource = !isRealtimeRtspSource(ffmpegSource);
    {
        QMutexLocker locker(&m_mutex);
        m_seekable = seekableSource;
        m_durationMs = seekableSource ? mediaDurationMs(format.get()) : -1;
        m_positionMs = -1;
    }
    QString initialSeekStatus;
    if (m_initialSeekMs > 0) {
        if (ffmpegSource.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive)) {
            initialSeekStatus = QStringLiteral("RTSP 实时流不支持片段定位，已从实时画面播放");
        } else {
            const AVRational millisecondTimeBase = {1, 1000};
            qint64 targetTimestamp = av_rescale_q(m_initialSeekMs,
                                                  millisecondTimeBase,
                                                  stream->time_base);
            if (stream->start_time != AV_NOPTS_VALUE) {
                targetTimestamp += stream->start_time;
            }

            rc = av_seek_frame(format.get(), streamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
            if (rc < 0) {
                initialSeekStatus = QStringLiteral("定位片段失败，已从头播放：%1").arg(avError(rc));
                qWarning() << "[RtspStream] initial seek failed"
                           << safeUrlForLog(m_url)
                           << formatMilliseconds(m_initialSeekMs)
                           << avError(rc);
            } else {
                initialSeekStatus = QStringLiteral("已定位到片段起点 %1").arg(formatMilliseconds(m_initialSeekMs));
            }
        }
    }

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        setState(State::Error, QStringLiteral("找不到解码器"));
        return false;
    }
    if (!codecSupportsD3D11(codec)) {
        setFatalError(QStringLiteral("当前解码器不支持 D3D11VA 硬解：%1").arg(QString::fromLatin1(codec->name)));
        return false;
    }

    AvPtr<AVCodecContext, freeCodec> codecContext;
    codecContext.ptr = avcodec_alloc_context3(codec);
    if (!codecContext.get()) {
        setState(State::Error, QStringLiteral("创建解码上下文失败"));
        return false;
    }
    rc = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
    if (rc < 0) {
        setState(State::Error, QStringLiteral("复制解码参数失败：%1").arg(avError(rc)));
        return false;
    }

    codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecContext->thread_count = 1;
    codecContext->get_format = d3d11GetFormat;
    codecContext->hw_device_ctx = av_buffer_ref(hwDevice.get());
    if (!codecContext->hw_device_ctx) {
        setFatalError(QStringLiteral("复制 D3D11VA 上下文失败"));
        return false;
    }

    AVDictionary *codecOptions = nullptr;
    av_dict_set(&codecOptions, "threads", "1", 0);
    rc = avcodec_open2(codecContext.get(), codec, &codecOptions);
    av_dict_free(&codecOptions);
    if (rc < 0) {
        setFatalError(QStringLiteral("打开 D3D11VA 解码器失败：%1").arg(avError(rc)));
        return false;
    }

    AvPtr<AVPacket, freePacket> packet;
    packet.ptr = av_packet_alloc();
    AvPtr<AVFrame, freeFrame> frame;
    frame.ptr = av_frame_alloc();
    if (!packet.get() || !frame.get()) {
        setState(State::Error, QStringLiteral("分配 FFmpeg 帧缓存失败"));
        return false;
    }

    QString transport;
    {
        QMutexLocker locker(&m_mutex);
        transport = m_currentTransport;
    }
    if (transport.isEmpty()) {
        transport = ffmpegSource.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive)
                        ? QStringLiteral("udp")
                        : QStringLiteral("file");
        setCurrentTransport(transport);
    }
    QString playingStatus = QStringLiteral("播放中：D3D11VA");
    if (ffmpegSource.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive)) {
        playingStatus = QStringLiteral("%1 播放中：D3D11VA").arg(transport.toUpper());
    }
    if (!initialSeekStatus.isEmpty()) {
        playingStatus += QStringLiteral("（%1）").arg(initialSeekStatus);
    }
    recordStreamRecovered(transport);
    setState(State::Playing, playingStatus);
    qDebug() << "[RtspStream] playing with D3D11VA" << safeUrlForLog(m_url)
             << "transport=" << transport
             << "codec=" << codec->name
             << "size=" << codecContext->width << "x" << codecContext->height;

    qint64 lastMediaTimeMs = -1;
    auto lastWallClock = std::chrono::steady_clock::now();
    auto performSeek = [&](qint64 targetMs) {
        if (!seekableSource || targetMs < 0) {
            return;
        }
        const AVRational millisecondTimeBase = {1, 1000};
        qint64 targetTimestamp = av_rescale_q(targetMs, millisecondTimeBase, stream->time_base);
        if (stream->start_time != AV_NOPTS_VALUE) {
            targetTimestamp += stream->start_time;
        }
        const int seekRc = av_seek_frame(format.get(), streamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
        if (seekRc < 0) {
            setState(State::Error, QStringLiteral("定位失败：%1").arg(avError(seekRc)));
            return;
        }
        avcodec_flush_buffers(codecContext.get());
        lastMediaTimeMs = -1;
        lastWallClock = std::chrono::steady_clock::now();
        {
            QMutexLocker locker(&m_mutex);
            m_positionMs = std::max<qint64>(0, targetMs);
            m_latestFrame.reset();
        }
        setState(State::Playing, QStringLiteral("已定位到 %1").arg(formatMilliseconds(targetMs)));
    };

    while (!m_stopRequested) {
        if (m_paused) {
            const qint64 pendingSeek = m_pendingSeekMs.exchange(-1);
            if (pendingSeek >= 0) {
                performSeek(pendingSeek);
                continue;
            }
            if (m_pendingStepFrames.load() > 0) {
                m_paused = false;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        const qint64 pendingSeek = m_pendingSeekMs.exchange(-1);
        if (pendingSeek >= 0) {
            performSeek(pendingSeek);
            continue;
        }

        rc = av_read_frame(format.get(), packet.get());
        if (rc < 0) {
            const QString error = QStringLiteral("读取视频包失败：%1").arg(avError(rc));
            recordStreamInterrupted(error);
            setState(State::Error, error);
            break;
        }

        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet.get());
            continue;
        }

        rc = avcodec_send_packet(codecContext.get(), packet.get());
        av_packet_unref(packet.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            setState(State::Error, QStringLiteral("送入解码器失败：%1").arg(avError(rc)));
            break;
        }

        while (!m_stopRequested) {
            rc = avcodec_receive_frame(codecContext.get(), frame.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                break;
            }
            if (rc < 0) {
                setState(State::Error, QStringLiteral("硬解输出失败：%1").arg(avError(rc)));
                return false;
            }

            if (frame->format != AV_PIX_FMT_D3D11) {
                setFatalError(QStringLiteral("解码器没有输出 D3D11 硬件帧"));
                av_frame_unref(frame.get());
                return false;
            }

            const qint64 mediaTimeMs = frameMediaTimeMs(frame.get(), stream);
            if (seekableSource && mediaTimeMs >= 0 && lastMediaTimeMs >= 0 && mediaTimeMs > lastMediaTimeMs) {
                const double rate = std::max(0.1, m_playbackRate.load());
                const auto targetDelay = std::chrono::milliseconds(
                    static_cast<qint64>((mediaTimeMs - lastMediaTimeMs) / rate));
                const auto elapsed = std::chrono::steady_clock::now() - lastWallClock;
                if (elapsed < targetDelay) {
                    std::this_thread::sleep_for(targetDelay - elapsed);
                }
            }
            lastMediaTimeMs = mediaTimeMs >= 0 ? mediaTimeMs : lastMediaTimeMs;
            lastWallClock = std::chrono::steady_clock::now();

            auto d3dFrame = D3DFrame::fromAvFrame(frame.get(), mediaTimeMs);
            av_frame_unref(frame.get());
            if (d3dFrame) {
                setLatestFrame(std::move(d3dFrame));
            }
            if (seekableSource && m_pendingStepFrames.load() > 0) {
                m_pendingStepFrames.fetch_sub(1);
                m_paused = true;
                setState(State::Idle, QStringLiteral("已暂停"));
                break;
            }
        }
    }

    return false;
}
