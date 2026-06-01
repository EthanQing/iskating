#include "rtspstream.h"
#include "d3d11videodevice.h"

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
    setState(State::Stopped, QStringLiteral("已停止"));
}

void RtspStream::pause(bool paused)
{
    m_paused = paused;
    setState(paused ? State::Idle : State::Playing, paused ? QStringLiteral("已暂停") : QStringLiteral("播放中"));
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
        setState(State::Reconnecting, QStringLiteral("断流，%1 秒后重连").arg(delay / 1000));
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
    m_latestFrame = std::move(frame);
}

bool RtspStream::openAndDecodeOnce()
{
    const QString ffmpegSource = sourceForFfmpeg(m_url);
    setState(State::Connecting, QStringLiteral("连接中"));

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
    setRtspOptions(&options, ffmpegSource, "udp");
    AVFormatContext *rawFormat = format.get();
    int rc = avformat_open_input(&rawFormat, ffmpegSource.toUtf8().constData(), nullptr, &options);
    format.ptr = rawFormat;
    av_dict_free(&options);
    if (rc < 0 && !m_stopRequested && ffmpegSource.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive)) {
        qWarning() << "[RtspStream] UDP open failed, retry TCP" << safeUrlForLog(m_url) << avError(rc);
        format.reset();
        format.ptr = avformat_alloc_context();
        if (!format.get()) {
            setState(State::Error, QStringLiteral("创建 FFmpeg 输入上下文失败"));
            return false;
        }
        format->interrupt_callback.callback = &RtspStream::ffmpegInterruptCallback;
        format->interrupt_callback.opaque = this;
        setRtspOptions(&options, ffmpegSource, "tcp");
        rawFormat = format.get();
        rc = avformat_open_input(&rawFormat, ffmpegSource.toUtf8().constData(), nullptr, &options);
        format.ptr = rawFormat;
        av_dict_free(&options);
    }
    if (rc < 0) {
        setState(State::Error, QStringLiteral("打开视频源失败：%1").arg(avError(rc)));
        return false;
    }

    format->flags |= AVFMT_FLAG_NOBUFFER;
    rc = avformat_find_stream_info(format.get(), nullptr);
    if (rc < 0) {
        setState(State::Error, QStringLiteral("读取视频流信息失败：%1").arg(avError(rc)));
        return false;
    }

    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        setState(State::Error, QStringLiteral("视频源没有可用视频轨道"));
        return false;
    }

    AVStream *stream = format->streams[streamIndex];
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

    QString playingStatus = QStringLiteral("播放中：D3D11VA");
    if (!initialSeekStatus.isEmpty()) {
        playingStatus += QStringLiteral("（%1）").arg(initialSeekStatus);
    }
    setState(State::Playing, playingStatus);
    qDebug() << "[RtspStream] playing with D3D11VA" << safeUrlForLog(m_url)
             << "codec=" << codec->name
             << "size=" << codecContext->width << "x" << codecContext->height;

    while (!m_stopRequested) {
        if (m_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        rc = av_read_frame(format.get(), packet.get());
        if (rc < 0) {
            setState(State::Error, QStringLiteral("读取视频包失败：%1").arg(avError(rc)));
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

            auto d3dFrame = D3DFrame::fromAvFrame(frame.get());
            av_frame_unref(frame.get());
            if (d3dFrame) {
                setLatestFrame(std::move(d3dFrame));
            }
        }
    }

    return false;
}
