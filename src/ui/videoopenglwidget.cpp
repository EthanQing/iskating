#include "videoopenglwidget.h"
#include "d3dvideosurface.h"
#include "framelessdialog.h"
#include "iconutils.h"
#include "rtspstream.h"
#include "streamregistry.h"

#include <QColor>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QDebug>
#include <QRectF>
#include <QResizeEvent>
#include <QSize>
#include <QSizeF>
#include <QPushButton>
#include <QMenu>
#include <QToolButton>
#include <QTimer>
#include <QVariantAnimation>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

QColor blendColor(const QColor &from, const QColor &to, qreal amount)
{
    const qreal t = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

bool hasUrlScheme(const QString &source)
{
    return source.trimmed().contains(QStringLiteral("://"));
}

QString composeStreamUrl(const QString &ipOrUrl, const QString &port, const QString &path)
{
    const QString trimmedPath = path.trimmed();
    const QString trimmedIp = ipOrUrl.trimmed();
    const QString trimmedPort = port.trimmed();

    if (hasUrlScheme(trimmedPath)) {
        return trimmedPath;
    }

    if (hasUrlScheme(trimmedIp)) {
        QString url = trimmedIp;
        if (!trimmedPath.isEmpty()) {
            if (!url.endsWith(QLatin1Char('/')) && !trimmedPath.startsWith(QLatin1Char('/'))) {
                url += QLatin1Char('/');
            } else if (url.endsWith(QLatin1Char('/')) && trimmedPath.startsWith(QLatin1Char('/'))) {
                url.chop(1);
            }
            url += trimmedPath;
        }
        return url;
    }

    if (trimmedIp.isEmpty()) {
        return trimmedPath;
    }

    QString url = QStringLiteral("rtsp://") + trimmedIp;
    if (!trimmedPort.isEmpty()) {
        url += QStringLiteral(":") + trimmedPort;
    }
    if (!trimmedPath.isEmpty()) {
        if (!trimmedPath.startsWith(QLatin1Char('/'))) {
            url += QLatin1Char('/');
        }
        url += trimmedPath;
    }
    return url;
}

QString normalizedMediaSource(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty() || hasUrlScheme(trimmed)) {
        return trimmed;
    }
    return QFileInfo(trimmed).absoluteFilePath();
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

} // namespace

VideoOpenGLWidget::VideoOpenGLWidget(QWidget *parent)
    : QWidget(parent)
    , m_placeholderRenderer(QStringLiteral(":/icons/video.svg"))
    , m_videoSurface(new D3DVideoSurface(this))
    , m_renderTimer(new QTimer(this))
    , m_tileHoverAnimation(new QVariantAnimation(this))
    , m_tilePressAnimation(new QVariantAnimation(this))
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    m_videoSurface->hide();
    m_videoSurface->lower();
    m_videoSurface->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_sourceOverlayLabel = new QLabel(this);
    m_sourceOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_sourceOverlayLabel->setAttribute(Qt::WA_NativeWindow);
    m_sourceOverlayLabel->setAttribute(Qt::WA_OpaquePaintEvent);
    m_sourceOverlayLabel->setObjectName(QStringLiteral("videoSourceOverlay"));
    m_sourceOverlayLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #F4F7FA; background: #0C1118; "
        "border: none; border-radius: 5px; padding: 5px 9px; font-size: 13px; "
        "font-weight: 600; }"));
    m_sourceOverlayLabel->hide();

    m_tileHoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_tilePressAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_tileHoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_tileHoverProgress = value.toReal();
        update();
    });
    connect(m_tilePressAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_tilePressProgress = value.toReal();
        update();
    });

    for (int index = 0; index < 4; ++index) {
        auto *label = new QLabel(this);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setStyleSheet(QStringLiteral("QLabel { color: white; background: rgba(20, 24, 32, 210); border: 1px solid rgba(127, 183, 255, 190); border-radius: 3px; padding: 2px 5px; }"));
        label->setFont(QFont(QStringLiteral("Microsoft YaHei"), 9, QFont::DemiBold));
        label->hide();
        m_athleteLabels.append(label);
    }

    m_renderTimer->setInterval(15);
    connect(m_renderTimer, &QTimer::timeout, this, [this]() { refreshVideoFrame(); });

    setupOverlayControls();
}

QString VideoOpenGLWidget::defaultVideoPath()
{
    const QString currentDirPath = QDir::current().absoluteFilePath(QStringLiteral("1.mp4"));
    if (QFileInfo::exists(currentDirPath)) {
        return currentDirPath;
    }

    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("1.mp4"));
}

bool VideoOpenGLWidget::isPlaying() const
{
    return m_playing && m_stream != nullptr;
}

void VideoOpenGLWidget::setPlaying(bool playing)
{
    if (playing) {
        if (m_stream) {
            m_stream->pause(false);
            m_playing = true;
            m_statusText = QStringLiteral("播放中");
            m_renderTimer->start();
            notifyStreamChanged();
            refreshSourceOverlay();
            update();
            return;
        }
        playDefaultVideo();
    } else {
        stopPlayback();
    }
}

void VideoOpenGLWidget::setStillImage(const QString &imagePath)
{
    stopPlayback();
    m_videoPath = imagePath;
    m_statusText = QFileInfo(imagePath).fileName();
    update();
}

void VideoOpenGLWidget::playDefaultVideo()
{
    attachStream(previewUrl());
}

void VideoOpenGLWidget::playMainUrl()
{
    attachStream(mainUrl());
}

void VideoOpenGLWidget::playMainUrlWithFallback(const QString &mainUrl, const QString &fallbackUrl)
{
    attachStream(mainUrl, fallbackUrl);
}

void VideoOpenGLWidget::playFile(const QString &filePath)
{
    playFile(filePath, 0);
}

void VideoOpenGLWidget::playFile(const QString &filePath, qint64 startPositionMs)
{
    attachStream(filePath, QString(), startPositionMs);
}

void VideoOpenGLWidget::pausePlayback()
{
    if (m_stream) {
        m_stream->pause(true);
    }
    m_playing = false;
    m_statusText = QStringLiteral("已暂停");
    notifyStreamChanged();
    refreshSourceOverlay();
    update();
}

void VideoOpenGLWidget::stopPlayback()
{
    m_stream.reset();
    m_playing = false;
    m_lastPresentedMsec = 0;
    m_fallbackVideoPath.clear();
    m_usingFallback = false;
    m_statusText.clear();
    m_renderTimer->stop();
    m_videoSurface->setAthleteFrame({});
    m_athleteFrame = {};
    refreshAthleteLabels();
    m_videoSurface->clearFrame();
    m_videoSurface->hide();
    notifyStreamChanged();
    refreshSourceOverlay();
    update();
}

void VideoOpenGLWidget::seekTo(qint64 positionMs)
{
    if (!m_stream || !m_stream->isSeekable()) {
        return;
    }
    m_stream->seekTo(positionMs);
    m_playing = true;
    m_renderTimer->start();
    notifyStreamChanged();
}

void VideoOpenGLWidget::setPlaybackRate(double rate)
{
    if (m_stream) {
        m_stream->setPlaybackRate(rate);
    }
}

void VideoOpenGLWidget::stepForward()
{
    if (!m_stream || !m_stream->isSeekable()) {
        return;
    }
    m_stream->stepForward();
    m_renderTimer->start();
}

qint64 VideoOpenGLWidget::positionMs() const
{
    return m_stream ? m_stream->positionMs() : -1;
}

qint64 VideoOpenGLWidget::durationMs() const
{
    return m_stream ? m_stream->durationMs() : -1;
}

bool VideoOpenGLWidget::isSeekable() const
{
    return m_stream && m_stream->isSeekable();
}

QString VideoOpenGLWidget::currentVideoPath() const
{
    return m_videoPath.isEmpty() ? previewUrl() : m_videoPath;
}

std::shared_ptr<RtspStream> VideoOpenGLWidget::activeStream() const
{
    return m_stream;
}

void VideoOpenGLWidget::setAthleteFrame(const AthleteFrameResult &frame)
{
    if (m_videoSurface) {
        m_videoSurface->setAthleteFrame(frame);
    }
    m_athleteFrame = frame;
    refreshAthleteLabels();
}

void VideoOpenGLWidget::setSourceTile(bool enabled)
{
    if (m_sourceTile == enabled) {
        layoutVideoSurface();
        return;
    }

    m_sourceTile = enabled;
    setProperty("sourceTile", m_sourceTile);
    setCursor(m_sourceTile ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setFocusPolicy(m_sourceTile ? Qt::StrongFocus : Qt::NoFocus);
    setAccessibleName(cameraDisplayName());
    if (m_sourceTile && toolTip().isEmpty()) {
        setToolTip(cameraDisplayName());
    }
    if (m_sourceTile) {
        setOverlayControlsVisible(false);
    }
    layoutVideoSurface();
    refreshSourceOverlay();
    update();
}

bool VideoOpenGLWidget::isSourceTile() const
{
    return m_sourceTile || objectName().startsWith(QLatin1String("cameraButton"));
}

void VideoOpenGLWidget::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    setProperty("selected", m_selected);
    update();
}

bool VideoOpenGLWidget::isSelected() const
{
    return m_selected;
}

void VideoOpenGLWidget::setClickHandler(std::function<void(VideoOpenGLWidget *)> handler)
{
    m_clickHandler = std::move(handler);
}

QString VideoOpenGLWidget::sourceState() const
{
    if (previewUrl().trimmed().isEmpty() && m_videoPath.trimmed().isEmpty()) {
        return QStringLiteral("unconfigured");
    }
    if (!m_stream) {
        return QStringLiteral("offline");
    }
    if (!m_playing) {
        return QStringLiteral("offline");
    }

    switch (m_stream->state()) {
    case RtspStream::State::Playing:
        return QStringLiteral("online");
    case RtspStream::State::Error:
        return QStringLiteral("error");
    case RtspStream::State::Connecting:
    case RtspStream::State::Reconnecting:
    case RtspStream::State::Idle:
    case RtspStream::State::Stopped:
        return QStringLiteral("offline");
    }
    return QStringLiteral("offline");
}

QString VideoOpenGLWidget::placeholderText() const
{
    return m_placeholderText;
}

void VideoOpenGLWidget::setPlaceholderText(const QString &text)
{
    if (m_placeholderText == text) {
        return;
    }
    m_placeholderText = text;
    update();
}

void VideoOpenGLWidget::setPlaceholderIconVisible(bool visible)
{
    if (m_placeholderIconVisible == visible) {
        return;
    }
    m_placeholderIconVisible = visible;
    update();
}

void VideoOpenGLWidget::setOverlayControlsVisible(bool visible)
{
    m_overlayControlsVisible = visible && !m_sourceTile;
    for (auto *button : {m_playButton, m_pauseButton, m_stopButton}) {
        if (button) {
            button->setVisible(m_overlayControlsVisible);
            button->raise();
        }
    }
    if (m_configButton) {
        m_configButton->setVisible(m_overlayControlsVisible && m_configButtonVisible);
        m_configButton->raise();
    }
    layoutOverlayControls();
}

bool VideoOpenGLWidget::overlayControlsVisible() const
{
    return m_overlayControlsVisible;
}

void VideoOpenGLWidget::setConfigButtonVisible(bool visible)
{
    if (m_configButtonVisible == visible) {
        return;
    }

    m_configButtonVisible = visible;
    if (m_configButton) {
        m_configButton->setVisible(m_overlayControlsVisible && m_configButtonVisible);
    }
    layoutOverlayControls();
    update();
}

void VideoOpenGLWidget::setChannelName(const QString &name)
{
    m_channelName = name;
    setAccessibleName(cameraDisplayName());
    if (isSourceTile()) {
        setToolTip(cameraDisplayName());
    }
    refreshSourceOverlay();
    update();
}

QString VideoOpenGLWidget::channelName() const
{
    return m_channelName;
}

QString VideoOpenGLWidget::streamIp() const
{
    return m_streamIp;
}

QString VideoOpenGLWidget::streamPort() const
{
    return m_streamPort;
}

QString VideoOpenGLWidget::streamPath() const
{
    return m_streamPath;
}

QString VideoOpenGLWidget::streamUrl() const
{
    return previewUrl();
}

QString VideoOpenGLWidget::previewUrl() const
{
    return m_previewUrl.isEmpty() ? composeStreamUrl(m_streamIp, m_streamPort, m_streamPath) : m_previewUrl;
}

QString VideoOpenGLWidget::mainUrl() const
{
    return m_mainUrl.isEmpty() ? previewUrl() : m_mainUrl;
}

void VideoOpenGLWidget::setStreamConfig(const QString &ip, const QString &port, const QString &path)
{
    m_streamIp = ip.trimmed();
    m_streamPort = port.trimmed();
    m_streamPath = path.trimmed();
    m_previewUrl = composeStreamUrl(m_streamIp, m_streamPort, m_streamPath);
    if (m_mainUrl.isEmpty()) {
        m_mainUrl = m_previewUrl;
    }
    m_videoPath.clear();

    setToolTip(QStringLiteral("%1\n预览：%2\n主码流：%3")
                   .arg(m_channelName.isEmpty() ? QStringLiteral("视频源") : m_channelName,
                        safeUrlForLog(previewUrl()),
                        safeUrlForLog(mainUrl())));
}

void VideoOpenGLWidget::setStreamUrls(const QString &previewUrl, const QString &mainUrl)
{
    m_previewUrl = previewUrl.trimmed();
    m_mainUrl = mainUrl.trimmed().isEmpty() ? m_previewUrl : mainUrl.trimmed();
    m_streamIp = m_previewUrl;
    m_streamPort.clear();
    m_streamPath.clear();
    m_videoPath.clear();
    setToolTip(QStringLiteral("%1\n预览：%2\n主码流：%3")
                   .arg(m_channelName.isEmpty() ? QStringLiteral("视频源") : m_channelName,
                        safeUrlForLog(this->previewUrl()),
                        safeUrlForLog(this->mainUrl())));
}

void VideoOpenGLWidget::setDoubleClickHandler(std::function<void(VideoOpenGLWidget *)> handler)
{
    m_doubleClickHandler = std::move(handler);
}

void VideoOpenGLWidget::setConfigChangedHandler(std::function<void(VideoOpenGLWidget *)> handler)
{
    m_configChangedHandler = std::move(handler);
}

void VideoOpenGLWidget::setStreamChangedHandler(std::function<void(VideoOpenGLWidget *)> handler)
{
    m_streamChangedHandler = std::move(handler);
}

void VideoOpenGLWidget::attachStream(const QString &source, const QString &fallbackSource, qint64 startPositionMs)
{
    const QString normalizedSource = normalizedMediaSource(source);
    if (normalizedSource.isEmpty()) {
        m_statusText = QStringLiteral("视频源为空");
        update();
        return;
    }

    if (!hasUrlScheme(normalizedSource) && !QFileInfo::exists(normalizedSource)) {
        m_statusText = QStringLiteral("文件不存在");
        update();
        qWarning() << "[VideoOpenGLWidget] local file not found" << normalizedSource;
        return;
    }

    qDebug() << "[VideoOpenGLWidget] attach low-latency D3D11 stream"
             << (m_channelName.isEmpty() ? objectName() : m_channelName)
             << safeUrlForLog(normalizedSource);
    const qint64 initialSeekMs = std::max<qint64>(0, startPositionMs);
    if (!hasUrlScheme(normalizedSource)) {
        auto stream = std::make_shared<RtspStream>(normalizedSource, initialSeekMs);
        stream->start();
        m_stream = std::move(stream);
    } else {
        m_stream = StreamRegistry::instance().acquire(normalizedSource);
    }
    m_videoPath = normalizedSource;
    const QString normalizedFallback = normalizedMediaSource(fallbackSource);
    m_fallbackVideoPath = normalizedFallback == normalizedSource ? QString() : normalizedFallback;
    m_usingFallback = false;
    m_statusText = QStringLiteral("连接中");
    m_playing = true;
    m_lastPresentedMsec = 0;
    m_renderTimer->start();
    notifyStreamChanged();
    refreshSourceOverlay();
    update();
}

void VideoOpenGLWidget::notifyStreamChanged()
{
    if (m_streamChangedHandler) {
        m_streamChangedHandler(this);
    }
}

void VideoOpenGLWidget::refreshVideoFrame()
{
    if (!m_stream) {
        return;
    }

    const QString streamStatus = m_stream->statusText();
    if (!streamStatus.isEmpty() && streamStatus != m_statusText) {
        m_statusText = streamStatus;
        refreshSourceOverlay();
        update();
    }

    const auto streamState = m_stream->state();
    if (streamState == RtspStream::State::Error
        && !m_usingFallback
        && !m_fallbackVideoPath.trimmed().isEmpty()
        && (streamStatus.contains(QStringLiteral("D3D11VA"))
            || streamStatus.contains(QStringLiteral("硬解"))
            || streamStatus.contains(QStringLiteral("D3D11")))) {
        const QString fallback = m_fallbackVideoPath;
        qWarning() << "[VideoOpenGLWidget] main stream unavailable, falling back to preview stream"
                   << (m_channelName.isEmpty() ? objectName() : m_channelName)
                   << safeUrlForLog(m_videoPath)
                   << "->"
                   << safeUrlForLog(fallback)
                   << streamStatus;
        m_usingFallback = true;
        m_fallbackVideoPath.clear();
        m_stream = StreamRegistry::instance().acquire(fallback);
        m_videoPath = fallback;
        m_statusText = QStringLiteral("主码流不可用，已回退预览码流");
        m_lastPresentedMsec = 0;
        notifyStreamChanged();
        refreshSourceOverlay();
        update();
        return;
    }

    auto frame = m_stream->latestFrame();
    if (!frame) {
        return;
    }
    if (frame->receivedMsec == m_lastPresentedMsec) {
        return;
    }

    m_lastPresentedMsec = frame->receivedMsec;
    if (!m_videoSurface->isVisible()) {
        layoutVideoSurface();
        m_videoSurface->show();
        m_videoSurface->lower();
        for (auto *button : {m_playButton, m_pauseButton, m_stopButton, m_configButton}) {
            if (button) {
                button->raise();
            }
        }
        refreshSourceOverlay();
    }
    layoutVideoSurface();
    m_videoSurface->presentFrame(frame);
}

void VideoOpenGLWidget::refreshAthleteLabels()
{
    for (QLabel *label : std::as_const(m_athleteLabels)) {
        label->hide();
    }
    if (m_athleteFrame.instances.isEmpty() || m_athleteFrame.frameSize.width() <= 0.0
        || m_athleteFrame.frameSize.height() <= 0.0 || width() <= 0 || height() <= 0) {
        return;
    }

    const float widgetAspect = static_cast<float>(width()) / static_cast<float>(height());
    const float frameAspect = static_cast<float>(m_athleteFrame.frameSize.width())
                              / static_cast<float>(m_athleteFrame.frameSize.height());
    float u0 = 0.0f;
    float u1 = 1.0f;
    float v0 = 0.0f;
    float v1 = 1.0f;
    if (frameAspect > widgetAspect) {
        const float visibleWidth = widgetAspect / frameAspect;
        u0 = (1.0f - visibleWidth) * 0.5f;
        u1 = 1.0f - u0;
    } else if (frameAspect < widgetAspect) {
        const float visibleHeight = frameAspect / widgetAspect;
        v0 = (1.0f - visibleHeight) * 0.5f;
        v1 = 1.0f - v0;
    }

    const int count = std::min(m_athleteFrame.instances.size(), m_athleteLabels.size());
    for (int index = 0; index < count; ++index) {
        const AthleteInstance &instance = m_athleteFrame.instances.at(index);
        if (!instance.box.isValid()) {
            continue;
        }
        const QRectF &box = instance.box;
        const float normalizedX = static_cast<float>(box.x() / m_athleteFrame.frameSize.width());
        const float normalizedY = static_cast<float>(box.y() / m_athleteFrame.frameSize.height());
        if (normalizedX < u0 || normalizedY < v0
            || normalizedX + box.width() / m_athleteFrame.frameSize.width() > u1
            || normalizedY + box.height() / m_athleteFrame.frameSize.height() > v1) {
            continue;
        }
        auto *label = m_athleteLabels.at(index);
        const QString identity = instance.label.trimmed().isEmpty()
                                     ? QStringLiteral("未识别")
                                     : instance.label.trimmed();
        const QString track = instance.trackId >= 0
                                  ? QStringLiteral(" · track %1").arg(instance.trackId)
                                  : QString();
        label->setText(identity + track);
        label->adjustSize();
        const int x = static_cast<int>(((normalizedX - u0) / (u1 - u0)) * width());
        const int y = static_cast<int>(((normalizedY - v0) / (v1 - v0)) * height());
        label->move(std::clamp(x, 0, std::max(0, width() - label->width())),
                    std::clamp(y - label->height(), 0, std::max(0, height() - label->height())));
        label->show();
        label->raise();
    }
}

void VideoOpenGLWidget::refreshSourceOverlay()
{
    if (!m_sourceOverlayLabel || isSourceTile() || m_channelName.trimmed().isEmpty()) {
        if (m_sourceOverlayLabel) {
            m_sourceOverlayLabel->hide();
        }
        return;
    }

    const QString source = currentVideoPath().trimmed();
    const bool isRtspSource =
        QUrl(source).scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) == 0;
    const QString sourceStatus = sourceState();
    QString stateLabel;
    if (sourceStatus == QStringLiteral("unconfigured")) {
        stateLabel = QStringLiteral("未配置");
    } else if (sourceStatus == QStringLiteral("error")) {
        stateLabel = QStringLiteral("连接失败");
    } else if (sourceStatus == QStringLiteral("online")) {
        stateLabel = isRtspSource ? QStringLiteral("LIVE") : QStringLiteral("播放中");
    } else if (m_stream && !m_playing) {
        stateLabel = QStringLiteral("已暂停");
    } else if (m_stream && m_playing) {
        stateLabel = QStringLiteral("连接中");
    } else {
        stateLabel = QStringLiteral("离线");
    }

    QFont overlayFont = m_sourceOverlayLabel->font();
    overlayFont.setPixelSize(13);
    overlayFont.setWeight(QFont::DemiBold);
    const int maximumWidth = std::max(80, std::min(400, width() - 28));
    const QString fullText = QStringLiteral("%1 · %2").arg(cameraDisplayName(), stateLabel);
    const QString displayText =
        QFontMetrics(overlayFont).elidedText(fullText, Qt::ElideRight, maximumWidth - 18);
    m_sourceOverlayLabel->setFont(overlayFont);
    m_sourceOverlayLabel->setText(displayText);
    m_sourceOverlayLabel->setToolTip(fullText);
    const int overlayWidth =
        std::min(maximumWidth, QFontMetrics(overlayFont).horizontalAdvance(displayText) + 18);
    m_sourceOverlayLabel->setFixedSize(overlayWidth, 31);
    m_sourceOverlayLabel->move(14, 14);
    m_sourceOverlayLabel->show();
    m_sourceOverlayLabel->raise();
}

void VideoOpenGLWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool cameraTile = isSourceTile();
    const QColor baseBackground(QStringLiteral("#101720"));
    const QColor hoverBackground(QStringLiteral("#192533"));
    const QColor selectedBackground(QStringLiteral("#173348"));
    const QColor pressedBackground(QStringLiteral("#0C141D"));
    QColor backgroundColor = blendColor(baseBackground, hoverBackground, m_tileHoverProgress);
    if (cameraTile && m_selected) {
        backgroundColor = blendColor(backgroundColor, selectedBackground, 0.8);
    }
    if (cameraTile) {
        backgroundColor = blendColor(backgroundColor, pressedBackground, m_tilePressProgress);
    }
    QColor borderColor(QStringLiteral("#202C3A"));
    if (cameraTile && m_selected) {
        borderColor = QColor(QStringLiteral("#26739B"));
    } else if (cameraTile) {
        borderColor = blendColor(borderColor, QColor(QStringLiteral("#2D4054")), m_tileHoverProgress);
    }
    if (cameraTile && m_tilePressProgress > 0.0) {
        borderColor = blendColor(borderColor,
                                 QColor(QStringLiteral("#0EA5E9")),
                                 m_tilePressProgress * 0.8);
    }

    painter.fillRect(rect(), backgroundColor);
    QPen borderPen(borderColor);
    borderPen.setWidth(1);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    if (cameraTile) {
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        if (hasFocus()) {
            painter.setPen(QPen(QColor(QStringLiteral("#38BDF8")), 1));
            painter.drawRect(rect().adjusted(2, 2, -3, -3));
        }
    } else {
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
    }

    if (cameraTile) {
        if (m_selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#38BDF8")));
            painter.drawRoundedRect(
                QRectF(0.0, 5.0, 3.0, std::max(0, height() - 10)), 1.5, 1.5);
        }

        const int stripHeight = std::min(30, std::max(0, height() - 4));
        const QRect stripRect(2,
                              std::max(2, height() - stripHeight - 2),
                              std::max(0, width() - 4),
                              stripHeight);
        QColor stripBackground = blendColor(QColor(QStringLiteral("#101720")),
                                            QColor(QStringLiteral("#192533")),
                                            m_tileHoverProgress);
        if (m_selected) {
            stripBackground = blendColor(stripBackground,
                                         QColor(QStringLiteral("#1D3042")),
                                         0.9);
        }
        stripBackground = blendColor(stripBackground,
                                     QColor(QStringLiteral("#0C141D")),
                                     m_tilePressProgress);
        painter.fillRect(stripRect, stripBackground);
        painter.setPen(QPen(blendColor(QColor(QStringLiteral("#202C3A")),
                                       QColor(QStringLiteral("#38BDF8")),
                                       std::max(m_tileHoverProgress * 0.45, m_tilePressProgress * 0.8)),
                       1));
        painter.drawLine(stripRect.topLeft(), stripRect.topRight());

        QFont stripFont = painter.font();
        stripFont.setPixelSize(13);
        stripFont.setWeight(QFont::DemiBold);
        painter.setFont(stripFont);
        painter.setPen(m_selected
                           ? QColor(QStringLiteral("#38BDF8"))
                           : blendColor(QColor(QStringLiteral("#F4F7FA")),
                                        QColor(QStringLiteral("#B9E6FE")),
                                        m_tileHoverProgress));
        const int dotX = stripRect.right() - 66;
        const QRect nameRect = stripRect.adjusted(8, 0, -(stripRect.right() - dotX + 12), 0);
        const QString cameraName = QFontMetrics(stripFont).elidedText(cameraDisplayName(),
                                                                       Qt::ElideRight,
                                                                       std::max(0, nameRect.width()));
        painter.drawText(nameRect,
                         Qt::AlignVCenter | Qt::AlignLeft,
                         cameraName);

        const QString state = sourceState();
        const QColor stateColor = state == QStringLiteral("online")
                                      ? QColor(QStringLiteral("#32D583"))
                                      : state == QStringLiteral("error")
                                            ? QColor(QStringLiteral("#F97066"))
                                            : QColor(QStringLiteral("#667586"));
        const QString stateLabel = state == QStringLiteral("online")
                                       ? QStringLiteral("在线")
                                       : state == QStringLiteral("error")
                                             ? QStringLiteral("离线")
                                             : state == QStringLiteral("unconfigured")
                                                   ? QStringLiteral("未配置")
                                                   : QStringLiteral("离线");
        painter.setPen(Qt::NoPen);
        painter.setBrush(stateColor);
        painter.drawEllipse(QRectF(dotX, stripRect.center().y() - 3, 6, 6));
        painter.setPen(QColor(QStringLiteral("#A2AFBF")));
        painter.drawText(QRect(dotX + 11,
                               stripRect.top(),
                               std::max(0, stripRect.right() - dotX - 11),
                               stripRect.height()),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         stateLabel);
    }

    if (m_videoSurface->isVisible()) {
        return;
    }

    if (cameraTile) {
        const int stripHeight = std::min(30, std::max(0, height() - 4));
        const QRect imageRect(6,
                              6,
                              std::max(0, width() - 12),
                              std::max(0, height() - stripHeight - 12));
        const int iconSize = std::min(22, std::max(14, imageRect.height() / 2));
        const QRect iconRect(imageRect.center().x() - iconSize / 2,
                             imageRect.top() + std::max(0, (imageRect.height() - iconSize - 18) / 2),
                             iconSize,
                             iconSize);
        if (m_placeholderIconVisible && m_placeholderRenderer.isValid() && iconRect.isValid()) {
            QImage iconMask(iconRect.size(), QImage::Format_ARGB32_Premultiplied);
            iconMask.fill(Qt::transparent);
            QPainter iconPainter(&iconMask);
            iconPainter.setRenderHint(QPainter::Antialiasing, true);
            iconPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            m_placeholderRenderer.render(&iconPainter, QRectF(QPointF(0, 0), QSizeF(iconRect.size())));
            iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            iconPainter.fillRect(iconMask.rect(), QColor(QStringLiteral("#667586")));
            iconPainter.end();
            painter.setOpacity(0.68);
            painter.drawImage(iconRect, iconMask);
            painter.setOpacity(1.0);
        }

        QFont tileFont = painter.font();
        tileFont.setPixelSize(13);
        tileFont.setWeight(QFont::Normal);
        painter.setFont(tileFont);
        painter.setPen(QColor(QStringLiteral("#A2AFBF")));
        const QString state = sourceState();
        const QString stateLabel = state == QStringLiteral("online")
                                       ? QStringLiteral("在线")
                                       : state == QStringLiteral("error")
                                             ? QStringLiteral("错误")
                                             : state == QStringLiteral("unconfigured")
                                                   ? QStringLiteral("未配置")
                                                   : QStringLiteral("离线");
        const QRect stateRect(imageRect.left(),
                              std::max(imageRect.top(), imageRect.bottom() - 16),
                              imageRect.width(),
                              16);
        painter.drawText(stateRect,
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         QFontMetrics(tileFont).elidedText(stateLabel,
                                                           Qt::ElideRight,
                                                           stateRect.width()));
        return;
    }

    QFont textFont = painter.font();
    textFont.setFamily(QStringLiteral("Microsoft YaHei"));
    textFont.setPointSize(width() > 240 ? 12 : 9);
    textFont.setBold(true);
    painter.setFont(textFont);

    const QString displayText = m_statusText.isEmpty() ? m_placeholderText : m_statusText;
    const QFontMetrics fm(textFont);
    const int textHeight = displayText.isEmpty()
                               ? 0
                               : fm.boundingRect(QRect(0, 0, width(), height()),
                                                 Qt::AlignCenter | Qt::TextWordWrap,
                                                 displayText)
                                     .height();
    const int gap = displayText.isEmpty() ? 0 : 8;
    const int reservedBottom = m_overlayControlsVisible ? 34 : 0;
    const int side = std::max(24, std::min({width() / 3, std::max(1, (height() - reservedBottom) / 3), 58}));
    const int blockHeight = side + gap + (m_overlayControlsVisible ? 0 : textHeight);
    const int iconTop = std::max(6, (height() - blockHeight) / 2);
    const QRect iconRect((width() - side) / 2, iconTop, side, side);

    if (m_placeholderIconVisible && m_placeholderRenderer.isValid()) {
        QImage iconMask(iconRect.size(), QImage::Format_ARGB32_Premultiplied);
        iconMask.fill(Qt::transparent);

        QPainter iconPainter(&iconMask);
        iconPainter.setRenderHint(QPainter::Antialiasing, true);
        iconPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        m_placeholderRenderer.render(&iconPainter, QRectF(QPointF(0, 0), QSizeF(iconRect.size())));
        iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        iconPainter.fillRect(iconMask.rect(), QColor(QStringLiteral("#8c8c8c")));
        iconPainter.end();

        painter.setOpacity(0.72);
        painter.drawImage(iconRect, iconMask);
        painter.setOpacity(1.0);
    }

    if (!displayText.isEmpty() && !m_overlayControlsVisible) {
        const QRect textRect(8, iconRect.bottom() + gap, width() - 16, std::max(20, height() - iconRect.bottom() - gap - 8));
        painter.setPen(QColor(QStringLiteral("#8c8c8c")));
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, displayText);
    }

    if (!m_placeholderText.isEmpty() && m_overlayControlsVisible) {
        QFont labelFont = painter.font();
        labelFont.setFamily(QStringLiteral("Microsoft YaHei"));
        labelFont.setPointSize(width() > 240 ? 11 : 9);
        labelFont.setBold(true);
        painter.setFont(labelFont);

        const QFontMetrics labelMetrics(labelFont);
        constexpr int buttonWidth = 22;
        constexpr int buttonGap = 3;
        const int buttonCount = m_configButtonVisible ? 4 : 3;
        const int controlsWidth = buttonWidth * buttonCount + buttonGap * std::max(0, buttonCount - 1);
        const int availableLabelWidth = width() - controlsWidth - buttonGap - 16;
        if (availableLabelWidth <= 34) {
            return;
        }

        const QString labelText = labelMetrics.elidedText(m_placeholderText,
                                                          Qt::ElideRight,
                                                          availableLabelWidth);
        const int labelWidth = std::min(labelMetrics.horizontalAdvance(labelText) + 8, availableLabelWidth);
        const int totalWidth = labelWidth + controlsWidth + buttonGap;
        const int labelX = std::max(8, (width() - totalWidth) / 2);
        const int labelY = std::max(8, height() - 22 - 8);
        const QRect labelRect(labelX, labelY, labelWidth, 22);
        painter.setPen(QColor(QStringLiteral("#8c8c8c")));
        painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, labelText);
    }
}

void VideoOpenGLWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutVideoSurface();
    layoutOverlayControls();
    refreshAthleteLabels();
    refreshSourceOverlay();
}

void VideoOpenGLWidget::keyPressEvent(QKeyEvent *event)
{
    if (isSourceTile() && isEnabled() && !event->isAutoRepeat()
        && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter)) {
        m_tilePressAnimation->stop();
        m_tilePressAnimation->setDuration(80);
        m_tilePressAnimation->setStartValue(m_tilePressProgress);
        m_tilePressAnimation->setEndValue(1.0);
        m_tilePressAnimation->start();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void VideoOpenGLWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (isSourceTile() && isEnabled() && !event->isAutoRepeat()
        && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter)) {
        m_tilePressAnimation->stop();
        m_tilePressAnimation->setDuration(110);
        m_tilePressAnimation->setStartValue(m_tilePressProgress);
        m_tilePressAnimation->setEndValue(0.0);
        m_tilePressAnimation->start();
        if (m_clickHandler) {
            m_clickHandler(this);
        }
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void VideoOpenGLWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!isSourceTile()) {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction *playAction = menu.addAction(QStringLiteral("播放"));
    QAction *pauseAction = menu.addAction(QStringLiteral("暂停"));
    QAction *stopAction = menu.addAction(QStringLiteral("停止"));
    connect(playAction, &QAction::triggered, this, [this]() { setPlaying(true); });
    connect(pauseAction, &QAction::triggered, this, [this]() { pausePlayback(); });
    connect(stopAction, &QAction::triggered, this, [this]() { stopPlayback(); });
    QAction *configAction = nullptr;
    if (m_configButtonVisible) {
        menu.addSeparator();
        configAction = menu.addAction(QStringLiteral("配置视频源"));
        connect(configAction, &QAction::triggered, this, [this]() { openConfigDialog(); });
    }
    playAction->setEnabled(isEnabled() && (m_stream != nullptr || !previewUrl().trimmed().isEmpty()));
    pauseAction->setEnabled(isEnabled() && m_stream != nullptr && m_playing);
    stopAction->setEnabled(isEnabled() && m_stream != nullptr);
    if (configAction) {
        configAction->setEnabled(isEnabled());
    }
    menu.exec(event->globalPos());
    event->accept();
}

void VideoOpenGLWidget::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    if (isSourceTile()) {
        animateTile(1.0, 140);
    }
}

void VideoOpenGLWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (isSourceTile()) {
        animateTile(0.0, 140);
        m_tilePressAnimation->stop();
        m_tilePressAnimation->setDuration(110);
        m_tilePressAnimation->setStartValue(m_tilePressProgress);
        m_tilePressAnimation->setEndValue(0.0);
        m_tilePressAnimation->start();
    }
}

void VideoOpenGLWidget::mousePressEvent(QMouseEvent *event)
{
    if (isSourceTile() && event->button() == Qt::LeftButton && isEnabled()) {
        m_tilePressAnimation->stop();
        m_tilePressAnimation->setDuration(80);
        m_tilePressAnimation->setStartValue(m_tilePressProgress);
        m_tilePressAnimation->setEndValue(1.0);
        m_tilePressAnimation->start();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void VideoOpenGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (isSourceTile() && event->button() == Qt::LeftButton && isEnabled()) {
        m_tilePressAnimation->stop();
        m_tilePressAnimation->setDuration(110);
        m_tilePressAnimation->setStartValue(m_tilePressProgress);
        m_tilePressAnimation->setEndValue(0.0);
        m_tilePressAnimation->start();
        if (rect().contains(event->position().toPoint()) && m_clickHandler) {
            m_clickHandler(this);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void VideoOpenGLWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_doubleClickHandler) {
        m_doubleClickHandler(this);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void VideoOpenGLWidget::layoutVideoSurface()
{
    if (!m_videoSurface) {
        return;
    }

    if (isSourceTile()) {
        const int surfaceWidth = std::max(0, width() - 4);
        const int surfaceHeight = std::max(0, height() - 34);
        m_videoSurface->setGeometry(2, 2, surfaceWidth, surfaceHeight);
    } else {
        m_videoSurface->setGeometry(rect().adjusted(1, 1, -1, -1));
    }
}

QString VideoOpenGLWidget::cameraDisplayName() const
{
    const QString configuredName = m_channelName.trimmed();
    if (!configuredName.isEmpty()) {
        return configuredName;
    }

    const QString name = objectName();
    if (name.startsWith(QStringLiteral("cameraButton"))) {
        bool ok = false;
        const int cameraNumber = name.mid(QStringLiteral("cameraButton").size()).toInt(&ok);
        if (ok) {
            return QStringLiteral("CAM %1").arg(cameraNumber, 2, 10, QLatin1Char('0'));
        }
    }
    return name.isEmpty() ? QStringLiteral("Video source") : name;
}

void VideoOpenGLWidget::animateTile(qreal target, int duration)
{
    m_tileHoverAnimation->stop();
    m_tileHoverAnimation->setDuration(duration);
    m_tileHoverAnimation->setStartValue(m_tileHoverProgress);
    m_tileHoverAnimation->setEndValue(target);
    m_tileHoverAnimation->start();
}

void VideoOpenGLWidget::setupOverlayControls()
{
    const QString buttonStyle = QStringLiteral(R"QSS(
QToolButton {
    border: none;
    border-radius: 6px;
    background: transparent;
    padding: 0;
}
QToolButton:hover {
    background: #192533;
}
QToolButton:pressed {
    background: #1d3042;
}
)QSS");

    auto makeButton = [this, &buttonStyle](const QString &text, const QString &iconPath, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setText(QString());
        button->setIcon(makeNormalizedTintedSvgIcon(iconPath, QColor(QStringLiteral("#8c8c8c")), 18, 15));
        button->setIconSize(QSize(18, 18));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setToolTip(tip);
        button->setAccessibleName(text);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        button->setStyleSheet(buttonStyle);
        button->setFixedSize(22, 22);
        button->hide();
        return button;
    };

    m_playButton = makeButton(QStringLiteral("播放"), QStringLiteral(":/icons/start_cap.svg"), QStringLiteral("接入当前视频流"));
    m_pauseButton = makeButton(QStringLiteral("暂停"), QStringLiteral(":/icons/suspend.svg"), QStringLiteral("暂停当前视频"));
    m_stopButton = makeButton(QStringLiteral("停止"), QStringLiteral(":/icons/stop.svg"), QStringLiteral("停止当前视频"));
    m_configButton = makeButton(QStringLiteral("配置"), QStringLiteral(":/icons/settings.svg"), QStringLiteral("配置当前视频源"));

    connect(m_playButton, &QToolButton::clicked, this, [this]() {
        qDebug() << "[VideoOpenGLWidget] play button clicked"
                 << (m_channelName.isEmpty() ? objectName() : m_channelName)
                 << safeUrlForLog(previewUrl());
        setPlaying(true);
    });
    connect(m_pauseButton, &QToolButton::clicked, this, [this]() {
        qDebug() << "[VideoOpenGLWidget] pause button clicked" << (m_channelName.isEmpty() ? objectName() : m_channelName);
        pausePlayback();
    });
    connect(m_stopButton, &QToolButton::clicked, this, [this]() {
        qDebug() << "[VideoOpenGLWidget] stop button clicked" << (m_channelName.isEmpty() ? objectName() : m_channelName);
        stopPlayback();
    });
    connect(m_configButton, &QToolButton::clicked, this, [this]() {
        qDebug() << "[VideoOpenGLWidget] config button clicked" << (m_channelName.isEmpty() ? objectName() : m_channelName);
        openConfigDialog();
    });
}

void VideoOpenGLWidget::layoutOverlayControls()
{
    if (!m_overlayControlsVisible) {
        return;
    }

    constexpr int margin = 8;
    constexpr int gap = 3;
    QFont labelFont = font();
    labelFont.setFamily(QStringLiteral("Microsoft YaHei"));
    labelFont.setPointSize(width() > 240 ? 11 : 9);
    labelFont.setBold(true);
    const int rawLabelWidth = m_placeholderText.isEmpty()
                                  ? 0
                                  : QFontMetrics(labelFont).horizontalAdvance(m_placeholderText) + 8;
    const int buttonWidths = m_playButton->width() + m_pauseButton->width() + m_stopButton->width()
                             + (m_configButtonVisible ? m_configButton->width() : 0);
    const int buttonCount = m_configButtonVisible ? 4 : 3;
    const int controlsWidth = buttonWidths + gap * std::max(0, buttonCount - 1);
    const int availableLabelWidth = width() - controlsWidth - gap - margin * 2;
    const int labelWidth = availableLabelWidth > 34 ? std::min(rawLabelWidth, availableLabelWidth) : 0;
    const int totalWidth = labelWidth > 0 ? labelWidth + controlsWidth + gap : controlsWidth;
    int x = std::max(margin, (width() - totalWidth) / 2);
    const int y = std::max(margin, height() - m_playButton->height() - margin);

    if (labelWidth > 0) {
        x += labelWidth + gap;
    }
    m_playButton->move(x, y);
    x += m_playButton->width() + gap;
    m_pauseButton->move(x, y);
    x += m_pauseButton->width() + gap;
    m_stopButton->move(x, y);
    if (m_configButtonVisible) {
        x += m_stopButton->width() + gap;
        m_configButton->move(x, y);
    } else if (m_configButton) {
        m_configButton->hide();
    }

    for (auto *button : {m_playButton, m_pauseButton, m_stopButton, m_configButton}) {
        if (button) {
            button->raise();
        }
    }
}

void VideoOpenGLWidget::openConfigDialog()
{
    FramelessDialog dialog(this);
    dialog.setWindowTitle(m_channelName.isEmpty()
                              ? QStringLiteral("视频源配置")
                              : QStringLiteral("%1 配置").arg(m_channelName));
    dialog.setDialogTitle(dialog.windowTitle());
    dialog.setModal(true);
    dialog.setMinimumWidth(460);

    auto *layout = dialog.contentLayout();
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(12);

    auto *previewEdit = new QLineEdit(previewUrl(), &dialog);
    previewEdit->setPlaceholderText(QStringLiteral("预览子码流，例如 rtsp://user:pwd@192.168.1.100:554/sub"));
    previewEdit->setClearButtonEnabled(true);
    auto *mainEdit = new QLineEdit(mainUrl(), &dialog);
    mainEdit->setPlaceholderText(QStringLiteral("主码流，可留空沿用预览子码流"));
    mainEdit->setClearButtonEnabled(true);

    form->addRow(QStringLiteral("预览子码流"), previewEdit);
    form->addRow(QStringLiteral("主画面码流"), mainEdit);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, previewEdit, mainEdit]() {
        const QString preview = previewEdit->text().trimmed();
        const QString main = mainEdit->text().trimmed();
        if (preview.isEmpty() || !hasUrlScheme(preview)) {
            QMessageBox::warning(&dialog,
                                 QStringLiteral("预览 URL 格式不正确"),
                                 QStringLiteral("请输入包含协议的预览子码流 URL，例如：\nrtsp://user:pwd@192.168.1.100:554/sub"));
            previewEdit->setFocus();
            return;
        }
        if (!main.isEmpty() && !hasUrlScheme(main)) {
            QMessageBox::warning(&dialog,
                                 QStringLiteral("主码流 URL 格式不正确"),
                                 QStringLiteral("主画面码流需要包含协议，或留空沿用预览子码流。"));
            mainEdit->setFocus();
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        setStreamUrls(previewEdit->text(), mainEdit->text());
        if (m_configChangedHandler) {
            m_configChangedHandler(this);
        }
    }
}
