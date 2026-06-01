#include "videoopenglwidget.h"
#include "d3dvideosurface.h"
#include "framelessdialog.h"
#include "iconutils.h"
#include "rtspstream.h"
#include "streamregistry.h"

#include <QColor>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
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
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

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
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_videoSurface->hide();
    m_videoSurface->lower();

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
    m_stream.reset();
    m_playing = false;
    m_statusText = QStringLiteral("已暂停");
    m_renderTimer->stop();
    m_videoSurface->setPoseFrame({});
    m_videoSurface->hide();
    notifyStreamChanged();
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
    m_videoSurface->setPoseFrame({});
    m_videoSurface->clearFrame();
    m_videoSurface->hide();
    notifyStreamChanged();
    update();
}

QString VideoOpenGLWidget::currentVideoPath() const
{
    return m_videoPath.isEmpty() ? previewUrl() : m_videoPath;
}

std::shared_ptr<RtspStream> VideoOpenGLWidget::activeStream() const
{
    return m_stream;
}

void VideoOpenGLWidget::setPoseFrame(const PoseFrameResult &frame)
{
    if (m_videoSurface) {
        m_videoSurface->setPoseFrame(frame);
    }
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
    m_overlayControlsVisible = visible;
    for (auto *button : {m_playButton, m_pauseButton, m_stopButton}) {
        if (button) {
            button->setVisible(visible);
            button->raise();
        }
    }
    if (m_configButton) {
        m_configButton->setVisible(visible && m_configButtonVisible);
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
    if (initialSeekMs > 0 && !hasUrlScheme(normalizedSource)) {
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
        m_videoSurface->setGeometry(rect().adjusted(1, 1, -1, -1));
        m_videoSurface->show();
        m_videoSurface->lower();
        for (auto *button : {m_playButton, m_pauseButton, m_stopButton, m_configButton}) {
            if (button) {
                button->raise();
            }
        }
    }
    m_videoSurface->presentFrame(frame);
}

void VideoOpenGLWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool cameraTile = objectName().startsWith(QLatin1String("cameraButton"));
    const QColor backgroundColor = cameraTile && underMouse()
                                       ? QColor(QStringLiteral("#172338"))
                                       : QColor(QStringLiteral("#14171d"));
    const QColor borderColor = cameraTile && underMouse()
                                   ? QColor(QStringLiteral("#31578f"))
                                   : QColor(QStringLiteral("#1e222a"));

    painter.fillRect(rect(), backgroundColor);
    QPen borderPen(borderColor);
    borderPen.setWidth(1);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    if (cameraTile) {
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    } else {
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
    }

    if (m_videoSurface->isVisible()) {
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
    m_videoSurface->setGeometry(rect().adjusted(1, 1, -1, -1));
    layoutOverlayControls();
}

void VideoOpenGLWidget::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    if (objectName().startsWith(QLatin1String("cameraButton"))) {
        update();
    }
}

void VideoOpenGLWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (objectName().startsWith(QLatin1String("cameraButton"))) {
        update();
    }
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
    background: #1e57b3;
}
QToolButton:pressed {
    background: #153d7d;
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
        playDefaultVideo();
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
