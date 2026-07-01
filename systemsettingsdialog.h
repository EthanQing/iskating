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
    QString nvrPlaybackTemplate;
};

struct CameraSlotSettings
{
    QString ip;
    bool trajectoryEnabled = true;
    QString role = QStringLiteral("轨迹分段");
    double fieldStartM = 0.0;
    double fieldEndM = 5.0;
    double lateralOffsetM = 0.0;
    double mountHeightM = 2.8;
    double yawDeg = 0.0;
    double pitchDeg = -8.0;
    QString qualityNote;
    QString compatibilityNote;
};

struct CapturePreferenceSettings
{
    QString modelPrecision = QStringLiteral("balanced");
    int fps = 120;
};

class QComboBox;
class QLineEdit;
class QTableWidget;

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
    void importCameraTemplate();
    void exportCameraTemplate();

    int m_cameraCount = 0;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLineEdit *m_previewPathEdit = nullptr;
    QComboBox *m_previewFpsComboBox = nullptr;
    QLineEdit *m_mainPathEdit = nullptr;
    QComboBox *m_mainFpsComboBox = nullptr;
    QLineEdit *m_nvrPlaybackTemplateEdit = nullptr;
    QComboBox *m_precisionComboBox = nullptr;
    QComboBox *m_fpsComboBox = nullptr;
    QVector<QLineEdit *> m_ipEdits;
    QTableWidget *m_cameraFieldTable = nullptr;
};

#endif // SYSTEMSETTINGSDIALOG_H
