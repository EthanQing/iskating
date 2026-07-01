#include "cameraconnectivitytester.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMetaType>
#include <QUrl>

#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace {

constexpr int kProbeTimeoutMs = 3000;
constexpr int kMaxReadFrames = 180;

QString normalizedRtspPath(const QString &path)
{
    QString normalized = path.trimmed();
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    return QStringLiteral("/%1").arg(normalized);
}

QString avError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

QString classifyOpenError(int code)
{
    const QString error = avError(code);
    const QString lower = error.toLower();
    if (lower.contains(QStringLiteral("unauthorized")) || lower.contains(QStringLiteral("401"))) {
        return QStringLiteral("RTSP 鉴权失败，请检查账号或密码：%1").arg(error);
    }
    if (lower.contains(QStringLiteral("not found")) || lower.contains(QStringLiteral("404"))) {
        return QStringLiteral("RTSP 路径不存在，请检查预览路径：%1").arg(error);
    }
    if (lower.contains(QStringLiteral("timed out")) || lower.contains(QStringLiteral("timeout"))) {
        return QStringLiteral("连接超时，请检查 IP、网络或 RTSP 服务：%1").arg(error);
    }
    if (lower.contains(QStringLiteral("host")) || lower.contains(QStringLiteral("resolve"))) {
        return QStringLiteral("DNS/IP 解析失败，请检查相机地址：%1").arg(error);
    }
    if (lower.contains(QStringLiteral("refused"))) {
        return QStringLiteral("RTSP 端口拒绝连接，请检查端口和服务状态：%1").arg(error);
    }
    return QStringLiteral("RTSP open 失败：%1").arg(error);
}

void setProbeOptions(AVDictionary **options, const char *transport)
{
    av_dict_set(options, "fflags", "nobuffer", 0);
    av_dict_set(options, "flags", "low_delay", 0);
    av_dict_set(options, "max_delay", "0", 0);
    av_dict_set(options, "probesize", "32768", 0);
    av_dict_set(options, "analyzeduration", "0", 0);
    av_dict_set(options, "timeout", "3000000", 0);
    av_dict_set(options, "reorder_queue_size", "0", 0);
    av_dict_set(options, "buffer_size", "1048576", 0);
    av_dict_set(options, "rtsp_transport", transport, 0);
}

template <typename T, void (*FreeFn)(T **)>
struct AvPtr
{
    ~AvPtr() { reset(); }
    T *get() const { return ptr; }
    T **put()
    {
        reset();
        return &ptr;
    }
    T *operator->() const { return ptr; }
    void reset()
    {
        if (ptr) {
            FreeFn(&ptr);
        }
    }
    T *ptr = nullptr;
};

struct ProbeContext
{
    QElapsedTimer timer;
};

int interruptProbe(void *opaque)
{
    auto *context = static_cast<ProbeContext *>(opaque);
    if (!context || !context->timer.isValid()) {
        return 0;
    }
    return context->timer.elapsed() > kProbeTimeoutMs ? 1 : 0;
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

QString frameRateText(AVStream *stream)
{
    if (!stream) {
        return QStringLiteral("-");
    }
    AVRational rate = stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0
                          ? stream->avg_frame_rate
                          : stream->r_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
        return QStringLiteral("-");
    }
    const double fps = av_q2d(rate);
    if (fps <= 0.0) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1 FPS").arg(fps, 0, 'f', fps >= 10.0 ? 1 : 2);
}

bool openInputWithTransport(const QString &url,
                            const char *transport,
                            ProbeContext *probeContext,
                            AvPtr<AVFormatContext, freeFormat> *format,
                            QString *error)
{
    if (!format) {
        return false;
    }
    format->reset();
    format->ptr = avformat_alloc_context();
    if (!format->get()) {
        if (error) {
            *error = QStringLiteral("创建 FFmpeg 输入上下文失败");
        }
        return false;
    }
    format->get()->interrupt_callback.callback = interruptProbe;
    format->get()->interrupt_callback.opaque = probeContext;

    AVDictionary *options = nullptr;
    setProbeOptions(&options, transport);
    AVFormatContext *rawFormat = format->get();
    const int rc = avformat_open_input(&rawFormat, url.toUtf8().constData(), nullptr, &options);
    format->ptr = rawFormat;
    av_dict_free(&options);
    if (rc < 0) {
        if (error) {
            *error = classifyOpenError(rc);
        }
        return false;
    }
    return true;
}

} // namespace

CameraConnectivityTester::CameraConnectivityTester(SharedCameraSettings sharedSettings,
                                                   QVector<CameraSlotSettings> cameraSettings,
                                                   QObject *parent)
    : QObject(parent)
    , m_sharedSettings(std::move(sharedSettings))
    , m_cameraSettings(std::move(cameraSettings))
{
    qRegisterMetaType<CameraConnectivityResult>("CameraConnectivityResult");
    qRegisterMetaType<QVector<CameraConnectivityResult>>("QVector<CameraConnectivityResult>");
}

void CameraConnectivityTester::run()
{
    QVector<CameraConnectivityResult> results;
    results.reserve(m_cameraSettings.size());
    for (int i = 0; i < m_cameraSettings.size(); ++i) {
        CameraConnectivityResult result = testCamera(i, m_cameraSettings.at(i));
        results.append(result);
        emit progress(i + 1, m_cameraSettings.size(), result);
    }
    emit finished(results);
}

CameraConnectivityResult CameraConnectivityTester::testCamera(int cameraIndex, const CameraSlotSettings &camera) const
{
    CameraConnectivityResult result;
    result.cameraIndex = cameraIndex;
    result.ip = camera.ip.trimmed();
    result.transport = QStringLiteral("-");
    result.resolution = QStringLiteral("-");
    result.frameRate = QStringLiteral("-");

    if (result.ip.isEmpty()) {
        result.status = QStringLiteral("未配置");
        result.message = QStringLiteral("未填写 IP，已跳过");
        result.skipped = true;
        return result;
    }
    if (m_sharedSettings.previewPath.trimmed().isEmpty()) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("预览路径为空，无法生成 RTSP URL");
        return result;
    }

    const QString url = composeCameraPreviewTestUrl(m_sharedSettings, result.ip);
    qDebug() << "[CameraConnectivityTester] probe start" << safeCameraTestUrlForLog(url)
             << "camera=" << cameraIndex + 1;

    ProbeContext probeContext;
    probeContext.timer.start();

    AvPtr<AVFormatContext, freeFormat> format;
    QString openError;
    result.transport = QStringLiteral("UDP");
    if (!openInputWithTransport(url, "udp", &probeContext, &format, &openError)) {
        qWarning() << "[CameraConnectivityTester] udp failed, retry tcp"
                   << safeCameraTestUrlForLog(url)
                   << "camera=" << cameraIndex + 1
                   << "error=" << openError;
        probeContext.timer.restart();
        result.transport = QStringLiteral("TCP");
        if (!openInputWithTransport(url, "tcp", &probeContext, &format, &openError)) {
            result.status = QStringLiteral("失败");
            result.message = openError;
            return result;
        }
    }

    format->flags |= AVFMT_FLAG_NOBUFFER;
    int rc = avformat_find_stream_info(format.get(), nullptr);
    if (rc < 0) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("读取视频流信息失败：%1").arg(avError(rc));
        return result;
    }

    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("视频源没有可用视频轨道");
        return result;
    }

    AVStream *stream = format->streams[streamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("找不到视频解码器");
        return result;
    }

    AvPtr<AVCodecContext, freeCodec> codecContext;
    codecContext.ptr = avcodec_alloc_context3(codec);
    if (!codecContext.get()) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("创建解码上下文失败");
        return result;
    }
    rc = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
    if (rc < 0) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("复制解码参数失败：%1").arg(avError(rc));
        return result;
    }

    AVDictionary *codecOptions = nullptr;
    av_dict_set(&codecOptions, "threads", "1", 0);
    rc = avcodec_open2(codecContext.get(), codec, &codecOptions);
    av_dict_free(&codecOptions);
    if (rc < 0) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("打开解码器失败：%1").arg(avError(rc));
        return result;
    }

    AvPtr<AVPacket, freePacket> packet;
    packet.ptr = av_packet_alloc();
    AvPtr<AVFrame, freeFrame> frame;
    frame.ptr = av_frame_alloc();
    if (!packet.get() || !frame.get()) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("分配 FFmpeg 帧缓存失败");
        return result;
    }

    bool gotFrame = false;
    int readFrames = 0;
    while (readFrames < kMaxReadFrames) {
        ++readFrames;
        rc = av_read_frame(format.get(), packet.get());
        if (rc < 0) {
            result.status = QStringLiteral("失败");
            result.message = QStringLiteral("读取首帧失败：%1").arg(avError(rc));
            return result;
        }
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet.get());
            continue;
        }
        rc = avcodec_send_packet(codecContext.get(), packet.get());
        av_packet_unref(packet.get());
        if (rc < 0) {
            result.status = QStringLiteral("失败");
            result.message = QStringLiteral("发送解码包失败：%1").arg(avError(rc));
            return result;
        }
        while (rc >= 0) {
            rc = avcodec_receive_frame(codecContext.get(), frame.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                break;
            }
            if (rc < 0) {
                result.status = QStringLiteral("失败");
                result.message = QStringLiteral("解码首帧失败：%1").arg(avError(rc));
                return result;
            }
            gotFrame = true;
            result.resolution = QStringLiteral("%1 x %2").arg(frame->width).arg(frame->height);
            av_frame_unref(frame.get());
            break;
        }
        if (gotFrame) {
            break;
        }
    }

    if (!gotFrame) {
        result.status = QStringLiteral("失败");
        result.message = QStringLiteral("未在限定帧数内读取到首帧");
        return result;
    }

    result.status = QStringLiteral("成功");
    result.frameRate = frameRateText(stream);
    result.message = QStringLiteral("首帧读取成功");
    result.success = true;
    qDebug() << "[CameraConnectivityTester] probe success" << safeCameraTestUrlForLog(url)
             << "camera=" << cameraIndex + 1
             << "transport=" << result.transport
             << "resolution=" << result.resolution
             << "fps=" << result.frameRate;
    return result;
}

QString composeCameraPreviewTestUrl(const SharedCameraSettings &sharedSettings, const QString &ip)
{
    const QString trimmedIp = ip.trimmed();
    if (trimmedIp.isEmpty()) {
        return QString();
    }

    QUrl url;
    url.setScheme(QStringLiteral("rtsp"));
    if (!sharedSettings.username.trimmed().isEmpty()) {
        url.setUserName(sharedSettings.username.trimmed());
    }
    if (!sharedSettings.password.isEmpty()) {
        url.setPassword(sharedSettings.password);
    }
    url.setHost(trimmedIp);

    bool ok = false;
    const int port = sharedSettings.port.trimmed().toInt(&ok);
    url.setPort(ok && port > 0 ? port : 554);
    url.setPath(normalizedRtspPath(sharedSettings.previewPath));
    return url.toString(QUrl::FullyEncoded);
}

QString safeCameraTestUrlForLog(const QString &source)
{
    QUrl url = QUrl::fromEncoded(source.toUtf8(), QUrl::TolerantMode);
    if (!url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
        return url.toString(QUrl::FullyEncoded);
    }
    return source;
}
