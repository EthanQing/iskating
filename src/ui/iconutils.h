#ifndef ICONUTILS_H
#define ICONUTILS_H

#include <QColor>
#include <QIcon>
#include <QString>

QIcon makeNormalizedTintedSvgIcon(const QString &path,
                                  const QColor &color,
                                  int canvasSize,
                                  int visualSize);

#endif // ICONUTILS_H
