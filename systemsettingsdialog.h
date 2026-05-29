#ifndef SYSTEMSETTINGSDIALOG_H
#define SYSTEMSETTINGSDIALOG_H

#include "framelessdialog.h"

#include <QString>
#include <QVector>

struct SharedCameraSettings
{
    QString username;
    QString password;
    QString port = QStringLiteral("554");
    QString previewPath;
    int previewFps = 30;
    QString mainPath;
    int mainFps = 120;
};

struct CameraSlotSettings
{
    QString ip;
};

struct CapturePreferenceSettings
{
    QString modelPrecision = QStringLiteral("balanced");
    int fps = 120;
};

class QComboBox;
class QLineEdit;

class SystemSettingsDialog : public FramelessDialog
{
public:
    explicit SystemSettingsDialog(int cameraCount, QWidget *parent = nullptr);

    void setSharedCameraSettings(const SharedCameraSettings &settings);
    SharedCameraSettings sharedCameraSettings() const;

    void setCameraSlotSettings(const QVector<CameraSlotSettings> &settings);
    QVector<CameraSlotSettings> cameraSlotSettings() const;

    void setCapturePreferenceSettings(const CapturePreferenceSettings &settings);
    CapturePreferenceSettings capturePreferenceSettings() const;

private:
    bool validateAndAccept();

    int m_cameraCount = 0;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLineEdit *m_previewPathEdit = nullptr;
    QComboBox *m_previewFpsComboBox = nullptr;
    QLineEdit *m_mainPathEdit = nullptr;
    QComboBox *m_mainFpsComboBox = nullptr;
    QComboBox *m_precisionComboBox = nullptr;
    QComboBox *m_fpsComboBox = nullptr;
    QVector<QLineEdit *> m_ipEdits;
};

#endif // SYSTEMSETTINGSDIALOG_H
