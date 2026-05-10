#include "iconutils.h"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSvgRenderer>

QIcon makeNormalizedTintedSvgIcon(const QString &path,
                                  const QColor &color,
                                  int canvasSize,
                                  int visualSize)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        return QIcon(path);
    }

    constexpr int renderSize = 128;
    QImage rendered(renderSize, renderSize, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);

    QPainter renderPainter(&rendered);
    renderPainter.setRenderHint(QPainter::Antialiasing, true);
    renderPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&renderPainter, QRectF(0, 0, renderSize, renderSize));
    renderPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    renderPainter.fillRect(rendered.rect(), color);
    renderPainter.end();

    QRect alphaBounds;
    for (int y = 0; y < rendered.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(rendered.constScanLine(y));
        for (int x = 0; x < rendered.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                alphaBounds = alphaBounds.isNull()
                                  ? QRect(x, y, 1, 1)
                                  : alphaBounds.united(QRect(x, y, 1, 1));
            }
        }
    }

    if (alphaBounds.isNull()) {
        return QIcon(QPixmap::fromImage(rendered.scaled(canvasSize,
                                                        canvasSize,
                                                        Qt::KeepAspectRatio,
                                                        Qt::SmoothTransformation)));
    }

    const QImage cropped = rendered.copy(alphaBounds);
    const QImage normalized = cropped.scaled(visualSize,
                                            visualSize,
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);

    QImage canvas(canvasSize, canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter canvasPainter(&canvas);
    canvasPainter.drawImage((canvasSize - normalized.width()) / 2,
                            (canvasSize - normalized.height()) / 2,
                            normalized);
    canvasPainter.end();

    const QPixmap pixmap = QPixmap::fromImage(canvas);
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::Off);
    return icon;
}
