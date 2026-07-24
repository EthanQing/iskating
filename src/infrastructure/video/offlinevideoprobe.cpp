#include "offlinevideoprobe.h"
#include "d3d11videodevice.h"

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

namespace {

QString avError(int rc)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

qint64 mediaDurationMs(const AVFormatContext *format)
{
    if (!format || format->duration == AV_NOPTS_VALUE || format->duration <= 0) {
        return -1;
    }
    return av_rescale_q(format->duration, AVRational{1, AV_TIME_BASE}, AVRational{1, 1000});
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

enum AVPixelFormat d3d11GetFormat(AVCodecContext *, const enum AVPixelFormat *formats)
{
    for (const enum AVPixelFormat *fmt = formats; *fmt != AV_PIX_FMT_NONE; ++fmt) {
        if (*fmt == AV_PIX_FMT_D3D11) {
            return *fmt;
        }
    }
    return AV_PIX_FMT_NONE;
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

    explicit AvBufferRefPtr(AVBufferRef *ref = nullptr)
        : ptr(ref)
    {
    }

    AVBufferRef *get() const { return ptr; }
    AVBufferRef *ptr = nullptr;
};

struct ProbeContext
{
    QElapsedTimer timer;
    int timeoutMs = 5000;
};

int interruptProbe(void *opaque)
{
    auto *context = static_cast<ProbeContext *>(opaque);
    if (!context || !context->timer.isValid()) {
        return 0;
    }
    return context->timer.elapsed() > context->timeoutMs ? 1 : 0;
}

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

void setFailure(OfflineVideoProbeResult *result, const QString &message)
{
    result->success = false;
    result->message = message;
}

bool timedOut(const ProbeContext &context)
{
    return context.timer.isValid() && context.timer.elapsed() > context.timeoutMs;
}

} // namespace

OfflineVideoProbeResult OfflineVideoProbe::probe(const QString &filePath, int timeoutMs)
{
    OfflineVideoProbeResult result;
    const QFileInfo fileInfo(filePath);
    result.filePath = fileInfo.absoluteFilePath();
    result.fileSize = fileInfo.exists() ? fileInfo.size() : 0;
    result.lastModified = fileInfo.exists() ? fileInfo.lastModified() : QDateTime();

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        setFailure(&result, QStringLiteral("找不到所选视频文件。"));
        return result;
    }
    if (!fileInfo.isReadable()) {
        setFailure(&result, QStringLiteral("视频文件不可读取，请检查文件权限。"));
        return result;
    }
    if (fileInfo.size() <= 0) {
        setFailure(&result, QStringLiteral("视频文件为空。"));
        return result;
    }

    ProbeContext probeContext;
    probeContext.timeoutMs = std::max(1000, timeoutMs);
    probeContext.timer.start();

    AvPtr<AVFormatContext, freeFormat> format;
    format.ptr = avformat_alloc_context();
    if (!format.get()) {
        setFailure(&result, QStringLiteral("创建 FFmpeg 输入上下文失败。"));
        return result;
    }
    format->interrupt_callback.callback = &interruptProbe;
    format->interrupt_callback.opaque = &probeContext;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "probesize", "32768", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);
    AVFormatContext *rawFormat = format.get();
    int rc = avformat_open_input(&rawFormat, result.filePath.toUtf8().constData(), nullptr, &options);
    format.ptr = rawFormat;
    av_dict_free(&options);
    if (rc < 0) {
        setFailure(&result, timedOut(probeContext)
                                ? QStringLiteral("读取视频信息超时。")
                                : QStringLiteral("打开视频文件失败：%1").arg(avError(rc)));
        return result;
    }

    rc = avformat_find_stream_info(format.get(), nullptr);
    if (rc < 0) {
        setFailure(&result, timedOut(probeContext)
                                ? QStringLiteral("读取视频信息超时。")
                                : QStringLiteral("读取视频流信息失败：%1").arg(avError(rc)));
        return result;
    }

    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        setFailure(&result, QStringLiteral("视频文件没有可用视频轨道。"));
        return result;
    }

    AVStream *stream = format->streams[streamIndex];
    result.durationMs = mediaDurationMs(format.get());
    result.seekable = result.durationMs > 0 && (format->pb == nullptr || format->pb->seekable != 0);
    if (result.durationMs <= 0) {
        setFailure(&result, QStringLiteral("无法读取视频时长，不适合作为离线训练视频。"));
        return result;
    }
    if (!result.seekable) {
        setFailure(&result, QStringLiteral("该视频文件不支持 seek，不适合作为离线训练视频。"));
        return result;
    }

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        setFailure(&result, QStringLiteral("找不到视频解码器。"));
        return result;
    }
    result.codecName = QString::fromLatin1(codec->name);
    if (!codecSupportsD3D11(codec)) {
        setFailure(&result, QStringLiteral("当前解码器不支持 D3D11VA 硬解：%1。").arg(result.codecName));
        return result;
    }

    QString hwError;
    AvBufferRefPtr hwDevice(D3D11VideoDevice::createFfmpegHwDevice(&hwError));
    if (!hwDevice.get()) {
        setFailure(&result, QStringLiteral("D3D11VA 不可用：%1").arg(hwError));
        return result;
    }

    AvPtr<AVCodecContext, freeCodec> codecContext;
    codecContext.ptr = avcodec_alloc_context3(codec);
    if (!codecContext.get()) {
        setFailure(&result, QStringLiteral("创建解码上下文失败。"));
        return result;
    }
    rc = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
    if (rc < 0) {
        setFailure(&result, QStringLiteral("复制解码参数失败：%1").arg(avError(rc)));
        return result;
    }
    result.resolution = QStringLiteral("%1x%2").arg(codecContext->width).arg(codecContext->height);
    if (codecContext->width <= 0 || codecContext->height <= 0) {
        setFailure(&result, QStringLiteral("视频分辨率无效。"));
        return result;
    }

    codecContext->thread_count = 1;
    codecContext->get_format = d3d11GetFormat;
    codecContext->hw_device_ctx = av_buffer_ref(hwDevice.get());
    if (!codecContext->hw_device_ctx) {
        setFailure(&result, QStringLiteral("复制 D3D11VA 上下文失败。"));
        return result;
    }

    AVDictionary *codecOptions = nullptr;
    av_dict_set(&codecOptions, "threads", "1", 0);
    rc = avcodec_open2(codecContext.get(), codec, &codecOptions);
    av_dict_free(&codecOptions);
    if (rc < 0) {
        setFailure(&result, QStringLiteral("打开 D3D11VA 解码器失败：%1").arg(avError(rc)));
        return result;
    }

    AvPtr<AVPacket, freePacket> packet;
    packet.ptr = av_packet_alloc();
    AvPtr<AVFrame, freeFrame> frame;
    frame.ptr = av_frame_alloc();
    if (!packet.get() || !frame.get()) {
        setFailure(&result, QStringLiteral("分配 FFmpeg 帧缓存失败。"));
        return result;
    }

    int packetsRead = 0;
    while (packetsRead < 180) {
        if (timedOut(probeContext)) {
            setFailure(&result, QStringLiteral("读取视频信息超时。"));
            return result;
        }
        rc = av_read_frame(format.get(), packet.get());
        if (rc < 0) {
            setFailure(&result, QStringLiteral("读取首帧失败：%1").arg(avError(rc)));
            return result;
        }
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet.get());
            continue;
        }
        ++packetsRead;
        rc = avcodec_send_packet(codecContext.get(), packet.get());
        av_packet_unref(packet.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            setFailure(&result, QStringLiteral("送入解码器失败：%1").arg(avError(rc)));
            return result;
        }
        while (true) {
            rc = avcodec_receive_frame(codecContext.get(), frame.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                break;
            }
            if (rc < 0) {
                setFailure(&result, QStringLiteral("读取解码帧失败：%1").arg(avError(rc)));
                return result;
            }
            if (frame->format != AV_PIX_FMT_D3D11) {
                setFailure(&result, QStringLiteral("解码器没有输出 D3D11 硬件帧。"));
                return result;
            }
            result.d3d11vaReady = true;
            result.success = true;
            result.message = QStringLiteral("视频文件校验通过。");
            av_frame_unref(frame.get());
            return result;
        }
    }

    setFailure(&result, QStringLiteral("未能在视频开头读取到可解码画面。"));
    return result;
}
