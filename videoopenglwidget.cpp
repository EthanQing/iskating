#include "videoopenglwidget.h"

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

VideoOpenGLWidget::VideoOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_placeholderRenderer(QStringLiteral(":/icons/video.svg"))
{
    setAutoFillBackground(false);
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
    const int side = std::max(24, std::min({width() / 3, height() / 3, 58}));
    const int blockHeight = side + gap + textHeight;
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

    if (!m_placeholderText.isEmpty()) {
        const QRect textRect(8, iconRect.bottom() + gap, width() - 16, std::max(20, height() - iconRect.bottom() - gap - 8));
        painter.setPen(QColor(QStringLiteral("#6f7a89")));
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_placeholderText);
    }
}
