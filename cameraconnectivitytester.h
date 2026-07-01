#ifndef CAMERACONNECTIVITYTESTER_H
#define CAMERACONNECTIVITYTESTER_H

#include "systemsettingsdialog.h"

#include <QObject>
#include <QString>
#include <QVector>

struct CameraConnectivityResult
{
    int cameraIndex = 0;
    QString ip;
    QString status;
    QString transport;
    QString resolution;
    QString frameRate;
    QString message;
    bool success = false;
    bool skipped = false;
};
Q_DECLARE_METATYPE(CameraConnectivityResult)
Q_DECLARE_METATYPE(QVector<CameraConnectivityResult>)

class CameraConnectivityTester : public QObject
{
    Q_OBJECT

public:
    CameraConnectivityTester(SharedCameraSettings sharedSettings,
                             QVector<CameraSlotSettings> cameraSettings,
                             QObject *parent = nullptr);

public slots:
    void run();

signals:
    void progress(int completed, int total, const CameraConnectivityResult &result);
    void finished(const QVector<CameraConnectivityResult> &results);

private:
    CameraConnectivityResult testCamera(int cameraIndex, const CameraSlotSettings &camera) const;

    SharedCameraSettings m_sharedSettings;
    QVector<CameraSlotSettings> m_cameraSettings;
};

QString composeCameraPreviewTestUrl(const SharedCameraSettings &sharedSettings, const QString &ip);
QString safeCameraTestUrlForLog(const QString &source);

#endif // CAMERACONNECTIVITYTESTER_H
