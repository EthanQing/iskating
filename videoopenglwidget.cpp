#include "videoopenglwidget.h"
#include "iconutils.h"

#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QDebug>
#include <QRectF>
#include <QResizeEvent>
#include <QSize>
#include <QSizeF>
#include <QToolButton>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

VideoOpenGLWidget::VideoOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_placeholderRenderer(QStringLiteral(":/icons/video.svg"))
    , m_mediaPlayer(new QMediaPlayer(this))
    , m_videoSink(new QVideoSink(this))
{
    setAutoFillBackground(false);
    m_mediaPlayer->setVideoOutput(m_videoSink);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        QImage image = frame.toImage();
        if (!image.isNull()) {
            qDebug() << "[VideoOpenGLWidget] frame received"
                     << (m_channelName.isEmpty() ? objectName() : m_channelName)
                     << image.size();
            m_currentFrame = image;
            update();
        } else {
            qDebug() << "[VideoOpenGLWidget] empty frame"
                     << (m_channelName.isEmpty() ? objectName() : m_channelName);
        }
    });
    connect(m_mediaPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        qDebug() << "[VideoOpenGLWidget] playback state"
                 << (m_channelName.isEmpty() ? objectName() : m_channelName)
                 << state;
        m_playing = (state == QMediaPlayer::PlayingState);
        update();
    });
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        qDebug() << "[VideoOpenGLWidget] media status"
                 << (m_channelName.isEmpty() ? objectName() : m_channelName)
                 << status
                 << m_mediaPlayer->source();
    });
    connect(m_mediaPlayer, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
        qDebug() << "[VideoOpenGLWidget] media error"
                 << (m_channelName.isEmpty() ? objectName() : m_channelName)
                 << error
                 << errorString
                 << m_mediaPlayer->source();
    });
    setupOverlayControls();
}

QString VideoOpenGLWidget::defaultVideoPath()
{
    const QString currentDirPath = QDir::current().absoluteFilePath(QStringLiteral("1.mp4"));
    if (QFileInfo::exists(currentDirPath)) {
        return currentDirPath;
    }

    const QString appDirPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("1.mp4"));
    return appDirPath;
}

bool VideoOpenGLWidget::isPlaying() const
{
    return m_mediaPlayer && m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState;
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
    QImage image(imagePath);
    if (image.isNull()) {
        qDebug() << "[VideoOpenGLWidget] failed to load still image"
                 << (m_channelName.isEmpty() ? objectName() : m_channelName)
                 << imagePath;
        return;
    }

    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
    }
    m_videoPath = imagePath;
    m_currentFrame = image;
    m_playing = false;
    update();
}

void VideoOpenGLWidget::playDefaultVideo()
{
    qDebug() << "[VideoOpenGLWidget] playDefaultVideo"
             << (m_channelName.isEmpty() ? objectName() : m_channelName)
             << defaultVideoPath();
    playFile(defaultVideoPath());
}

void VideoOpenGLWidget::playFile(const QString &filePath)
{
    if (!m_mediaPlayer) {
        return;
    }

    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    const QFileInfo fileInfo(absolutePath);
    qDebug() << "[VideoOpenGLWidget] playFile request"
             << (m_channelName.isEmpty() ? objectName() : m_channelName)
             << absolutePath
             << "exists=" << fileInfo.exists()
             << "size=" << (fileInfo.exists() ? fileInfo.size() : -1);
    if (!fileInfo.exists()) {
        qDebug() << "[VideoOpenGLWidget] file not found:" << absolutePath;
    }
    if (m_videoPath != absolutePath || m_mediaPlayer->source().isEmpty()) {
        m_videoPath = absolutePath;
        m_mediaPlayer->setSource(QUrl::fromLocalFile(m_videoPath));
        qDebug() << "[VideoOpenGLWidget] set source" << m_mediaPlayer->source();
    }
    m_mediaPlayer->play();
    qDebug() << "[VideoOpenGLWidget] play called"
             << (m_channelName.isEmpty() ? objectName() : m_channelName);
}

void VideoOpenGLWidget::pausePlayback()
{
    if (m_mediaPlayer) {
        qDebug() << "[VideoOpenGLWidget] pause"
                 << (m_channelName.isEmpty() ? objectName() : m_channelName);
        m_mediaPlayer->pause();
    }
}

void VideoOpenGLWidget::stopPlayback()
{
    if (m_mediaPlayer) {
        qDebug() << "[VideoOpenGLWidget] stop"
                 << (m_channelName.isEmpty() ? objectName() : m_channelName);
        m_mediaPlayer->stop();
    }
    m_playing = false;
    m_currentFrame = QImage();
    update();
}

QString VideoOpenGLWidget::currentVideoPath() const
{
    return m_videoPath.isEmpty() ? defaultVideoPath() : m_videoPath;
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
    for (auto *button : {m_playButton, m_pauseButton, m_stopButton, m_configButton}) {
        if (button) {
            button->setVisible(visible);
        }
    }
    layoutOverlayControls();
}

bool VideoOpenGLWidget::overlayControlsVisible() const
{
    return m_overlayControlsVisible;
}

void VideoOpenGLWidget::setChannelName(const QString &name)
{
    m_channelName = name;
}

QString VideoOpenGLWidget::channelName() const
{
    return m_channelName;
}

void VideoOpenGLWidget::setDoubleClickHandler(std::function<void(VideoOpenGLWidget *)> handler)
{
    m_doubleClickHandler = std::move(handler);
}

void VideoOpenGLWidget::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    painter.fillRect(rect(), QColor(QStringLiteral("#14171d")));
    QPen borderPen(QColor(QStringLiteral("#1e222a")));
    borderPen.setWidth(1);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);

    if (!m_currentFrame.isNull()) {
        const QRect target = rect().adjusted(1, 1, -1, -1);
        QSize drawSize = m_currentFrame.size();
        drawSize.scale(target.size(), Qt::KeepAspectRatioByExpanding);
        const QRect drawRect(target.center().x() - drawSize.width() / 2,
                             target.center().y() - drawSize.height() / 2,
                             drawSize.width(),
                             drawSize.height());
        painter.save();
        painter.setClipRect(target);
        painter.drawImage(drawRect, m_currentFrame);
        painter.restore();
    }

    if (m_playing || !m_currentFrame.isNull()) {
        return;
    }

    QFont textFont = painter.font();
    textFont.setFamily(QStringLiteral("Microsoft YaHei"));
    textFont.setPointSize(width() > 240 ? 12 : 9);
    textFont.setBold(true);
    painter.setFont(textFont);

    const QFontMetrics fm(textFont);
    const int textHeight = m_placeholderText.isEmpty() ? 0 : fm.boundingRect(QRect(0, 0, width(), height()),
                                                                              Qt::AlignCenter | Qt::TextWordWrap,
                                                                              m_placeholderText)
                                                            .height();
    const int gap = m_placeholderText.isEmpty() ? 0 : 8;
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

        // The raw SVG is white. Tint it to unified inactive gray to indicate idle/no playback.
        iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        iconPainter.fillRect(iconMask.rect(), QColor(QStringLiteral("#8c8c8c")));
        iconPainter.end();

        painter.setOpacity(0.72);
        painter.drawImage(iconRect, iconMask);
        painter.setOpacity(1.0);
    }

    if (!m_placeholderText.isEmpty() && !m_overlayControlsVisible) {
        const QRect textRect(8, iconRect.bottom() + gap, width() - 16, std::max(20, height() - iconRect.bottom() - gap - 8));
        painter.setPen(QColor(QStringLiteral("#8c8c8c")));
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_placeholderText);
    }

    if (!m_placeholderText.isEmpty() && m_overlayControlsVisible) {
        QFont labelFont = painter.font();
        labelFont.setFamily(QStringLiteral("Microsoft YaHei"));
        labelFont.setPointSize(width() > 240 ? 11 : 9);
        labelFont.setBold(true);
        painter.setFont(labelFont);

        const QFontMetrics labelMetrics(labelFont);
        const int labelWidth = labelMetrics.horizontalAdvance(m_placeholderText) + 8;
        constexpr int buttonWidth = 22;
        constexpr int buttonGap = 3;
        const int totalWidth = labelWidth + buttonWidth * 4 + buttonGap * 4;
        const int labelX = std::max(8, (width() - totalWidth) / 2);
        const int labelY = std::max(8, height() - 22 - 8);
        const QRect labelRect(labelX, labelY, labelWidth, 22);
        painter.setPen(QColor(QStringLiteral("#8c8c8c")));
        painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, m_placeholderText);
    }
}

void VideoOpenGLWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    layoutOverlayControls();
}

void VideoOpenGLWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_doubleClickHandler) {
        m_doubleClickHandler(this);
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
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

    m_playButton = makeButton(QStringLiteral("播放"), QStringLiteral(":/icons/start_cap.svg"), QStringLiteral("播放当前视频"));
    m_pauseButton = makeButton(QStringLiteral("暂停"), QStringLiteral(":/icons/suspend.svg"), QStringLiteral("暂停当前视频"));
    m_stopButton = makeButton(QStringLiteral("停止"), QStringLiteral(":/icons/stop.svg"), QStringLiteral("停止当前视频"));
    m_configButton = makeButton(QStringLiteral("配置"), QStringLiteral(":/icons/settings.svg"), QStringLiteral("配置当前视频源"));

    connect(m_playButton, &QToolButton::clicked, this, [this]() {
        qDebug() << "[VideoOpenGLWidget] play button clicked" << (m_channelName.isEmpty() ? objectName() : m_channelName);
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
    const int labelWidth = m_placeholderText.isEmpty() ? 0 : QFontMetrics(labelFont).horizontalAdvance(m_placeholderText) + 8;
    const int totalWidth = labelWidth + m_playButton->width() + m_pauseButton->width() + m_stopButton->width() + m_configButton->width() + gap * 4;
    int x = std::max(margin, (width() - totalWidth) / 2);
    const int y = std::max(margin, height() - m_playButton->height() - margin);

    x += labelWidth + gap;
    m_playButton->move(x, y);
    x += m_playButton->width() + gap;
    m_pauseButton->move(x, y);
    x += m_pauseButton->width() + gap;
    m_stopButton->move(x, y);
    x += m_stopButton->width() + gap;
    m_configButton->move(x, y);

    m_playButton->raise();
    m_pauseButton->raise();
    m_stopButton->raise();
    m_configButton->raise();
}

void VideoOpenGLWidget::openConfigDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(m_channelName.isEmpty()
                              ? QStringLiteral("视频源配置")
                              : QStringLiteral("%1 配置").arg(m_channelName));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *ipEdit = new QLineEdit(m_streamIp, &dialog);
    auto *portEdit = new QLineEdit(m_streamPort, &dialog);
    auto *pathEdit = new QLineEdit(m_streamPath, &dialog);

    form->addRow(QStringLiteral("IP 地址"), ipEdit);
    form->addRow(QStringLiteral("端口"), portEdit);
    form->addRow(QStringLiteral("路径/其它"), pathEdit);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        m_streamIp = ipEdit->text().trimmed();
        m_streamPort = portEdit->text().trimmed();
        m_streamPath = pathEdit->text().trimmed();
        setToolTip(QStringLiteral("%1\nrtsp://%2:%3%4")
                       .arg(m_channelName.isEmpty() ? QStringLiteral("视频源") : m_channelName,
                            m_streamIp,
                            m_streamPort,
                            m_streamPath));
    }
}
