#ifndef ATHLETEDETECTIONROI_H
#define ATHLETEDETECTIONROI_H

#include <QHash>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

struct AthleteDetectionRoi
{
    QSize referenceFrameSize;
    QPolygonF polygon;

    bool isValid() const;
};

using AthleteDetectionRoiMap = QHash<int, AthleteDetectionRoi>;

AthleteDetectionRoiMap loadAthleteDetectionRois(const QString &filePath,
                                                QStringList *warnings = nullptr);
bool isAthleteDetectionInsideRoi(const AthleteDetectionRoi &roi,
                                 const QRectF &box,
                                 const QSize &frameSize);

#endif // ATHLETEDETECTIONROI_H
