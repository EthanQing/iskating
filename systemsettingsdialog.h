#ifndef SYSTEMSETTINGSDIALOG_H
#define SYSTEMSETTINGSDIALOG_H

#include "framelessdialog.h"

#include <functional>
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
    QString calibrationJson;
    QString qualityNote;
    QString compatibilityNote;
};

struct CapturePreferenceSettings
{
    QString modelPrecision = QStringLiteral("balanced");
    QString analysisSource = QStringLiteral("preview");
    int fps = 5;
    int analysisTargetFps = 5;
    int analysisMaxStreams = 12;
    bool analysisAutoDegrade = true;
};

struct VideoStorageSettings
{
    QString rootDir;
    int capacityLimitGb = 50;
    int retentionDays = 60;
};

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
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

    void setVideoStorageSettings(const VideoStorageSettings &settings);
    VideoStorageSettings videoStorageSettings() const;
    void setVideoStorageStatus(const QString &status);
    void setVideoStorageActionsEnabled(bool enabled);

    std::function<void()> onBrowseVideoStorageRoot;
    std::function<void()> onScanVideoStorage;
    std::function<void()> onShowVideoCleanupCandidates;

private:
    bool validateAndAccept();
    void importCameraTemplate();
    void exportCameraTemplate();
    void testCameraConnectivity();

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
    QComboBox *m_analysisSourceComboBox = nullptr;
    QComboBox *m_fpsComboBox = nullptr;
    QComboBox *m_analysisMaxStreamsComboBox = nullptr;
    QCheckBox *m_analysisAutoDegradeCheckBox = nullptr;
    QLineEdit *m_videoStorageRootEdit = nullptr;
    QSpinBox *m_videoStorageCapacitySpinBox = nullptr;
    QSpinBox *m_videoStorageRetentionSpinBox = nullptr;
    QLabel *m_videoStorageStatusLabel = nullptr;
    QPushButton *m_videoStorageScanButton = nullptr;
    QPushButton *m_videoStorageCleanupButton = nullptr;
    QPushButton *m_connectivityTestButton = nullptr;
    QVector<QLineEdit *> m_ipEdits;
    QTableWidget *m_cameraFieldTable = nullptr;
    QVector<QString> m_cameraCalibrationJson;
};

#endif // SYSTEMSETTINGSDIALOG_H
