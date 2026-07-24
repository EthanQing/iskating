#ifndef CAMERACONFIGTEMPLATE_H
#define CAMERACONFIGTEMPLATE_H

#include "systemsettingsdialog.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct CameraConfigTemplateData
{
    SharedCameraSettings shared;
    CapturePreferenceSettings capture;
    QVector<CameraSlotSettings> cameras;
    int sourceCameraCount = 0;
    QStringList warnings;
};

struct CameraConfigTemplateResult
{
    bool ok = false;
    CameraConfigTemplateData data;
    QString error;
};

QString cameraConfigTemplateFileFilter();
CameraConfigTemplateResult loadCameraConfigTemplate(const QString &filePath, int cameraCount);
bool saveCameraConfigTemplate(const QString &filePath,
                              const SharedCameraSettings &shared,
                              const CapturePreferenceSettings &capture,
                              const QVector<CameraSlotSettings> &cameras,
                              QString *errorMessage);
QString cameraConfigTemplateSummary(const CameraConfigTemplateData &data);

#endif // CAMERACONFIGTEMPLATE_H
