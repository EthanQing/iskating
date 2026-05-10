#include "videoopenglwidget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QResizeEvent>
#include <QSize>
#include <QSizeF>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

VideoOpenGLWidget::VideoOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_placeholderRenderer(QStringLiteral(":/icons/video.svg"))
{
    setAutoFillBackground(false);
    setupOverlayControls();
}

bool VideoOpenGLWidget::isPlaying() const
{
    return m_playing;
}

void VideoOpenGLWidget::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    update();
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

void VideoOpenGLWidget::setOverlayControlsVisible(bool visible)
{
    m_overlayControlsVisible = visible;
    for (auto *button : {m_playButton, m_stopButton, m_configButton}) {
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

void VideoOpenGLWidget::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    painter.fillRect(rect(), QColor(QStringLiteral("#0e131b")));
    QPen borderPen(QColor(QStringLiteral("#202a3a")));
    borderPen.setWidth(1);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);

    if (m_playing) {
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

    if (m_placeholderRenderer.isValid()) {
        QImage iconMask(iconRect.size(), QImage::Format_ARGB32_Premultiplied);
        iconMask.fill(Qt::transparent);

        QPainter iconPainter(&iconMask);
        iconPainter.setRenderHint(QPainter::Antialiasing, true);
        iconPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        m_placeholderRenderer.render(&iconPainter, QRectF(QPointF(0, 0), QSizeF(iconRect.size())));

        // The raw SVG is white. Tint it to a muted dark gray to indicate idle/no playback.
        iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        iconPainter.fillRect(iconMask.rect(), QColor(QStringLiteral("#56606e")));
        iconPainter.end();

        painter.setOpacity(0.72);
        painter.drawImage(iconRect, iconMask);
        painter.setOpacity(1.0);
    }

    if (!m_placeholderText.isEmpty() && !m_overlayControlsVisible) {
        const QRect textRect(8, iconRect.bottom() + gap, width() - 16, std::max(20, height() - iconRect.bottom() - gap - 8));
        painter.setPen(QColor(QStringLiteral("#6f7a89")));
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
        const int totalWidth = labelWidth + buttonWidth * 3 + buttonGap * 3;
        const int labelX = std::max(8, (width() - totalWidth) / 2);
        const int labelY = std::max(8, height() - 22 - 8);
        const QRect labelRect(labelX, labelY, labelWidth, 22);
        painter.setPen(QColor(QStringLiteral("#7f8998")));
        painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, m_placeholderText);
    }
}

void VideoOpenGLWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    layoutOverlayControls();
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
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(15, 15));
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
    m_stopButton = makeButton(QStringLiteral("停止"), QStringLiteral(":/icons/stop.svg"), QStringLiteral("停止当前视频"));
    m_configButton = makeButton(QStringLiteral("配置"), QStringLiteral(":/icons/settings.svg"), QStringLiteral("配置当前视频源"));

    connect(m_playButton, &QToolButton::clicked, this, [this]() { setPlaying(true); });
    connect(m_stopButton, &QToolButton::clicked, this, [this]() { setPlaying(false); });
    connect(m_configButton, &QToolButton::clicked, this, [this]() { openConfigDialog(); });
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
    const int totalWidth = labelWidth + m_playButton->width() + m_stopButton->width() + m_configButton->width() + gap * 3;
    int x = std::max(margin, (width() - totalWidth) / 2);
    const int y = std::max(margin, height() - m_playButton->height() - margin);

    x += labelWidth + gap;
    m_playButton->move(x, y);
    x += m_playButton->width() + gap;
    m_stopButton->move(x, y);
    x += m_stopButton->width() + gap;
    m_configButton->move(x, y);

    m_playButton->raise();
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
