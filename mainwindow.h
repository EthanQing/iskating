#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMargins>
#include <QSize>
#include <QTimer>
#include <QVector>

#include <memory>

class QLabel;
class PoseStandardnessScorer;
class QEvent;
class QPushButton;
class SkeletonViewWidget;
class QVBoxLayout;
class TrajectoryWidget;
class VideoOpenGLWidget;
class HandAnalysisManager;

namespace Ui {
class MainWindow;
}

struct TrainingRecord
{
    qint64 id = 0;
    QString time;
    int duration = 0;
    int actions = 0;
    int score = 0;
    int camera = 1;
    QString modelPrecision;
    int fps = 30;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void setupUiState();
    void setupConnections();
    void installTrajectoryWidget();
    void installSkeletonView();
    void installStaticImages();
    void applyStyleSheet();
    void loadCameraSettings();
    void saveCameraSettings() const;
    void saveCameraSetting(int cameraIndex) const;
    void showCameraInMainView(int cameraIndex);

    void switchPage(int pageIndex);
    void toggleSidebar();
    void toggleFullScreen();
    void exitFullScreenMode();
    void selectCamera(int cameraId);
    void setTrajectoryExpanded(bool expanded);
    void cycleTrajectoryMode();
    void setTrajectoryMode(int mode);
    void refreshTrajectoryModeButton();
    void startCapture();
    void pauseCapture();
    void stopCapture();
    void saveRecord();
    void tick();

    void refreshNavButtons();
    void refreshCameraButtons();
    void refreshStats();
    void refreshHistory();
    void refreshSuggestions();
    void refreshSidebarButton();
    void refreshFullScreenButton();
    void refreshModelStatus(const QString &statusText);
    void clearRealtimePose();
    void repolish(QWidget *widget) const;

    QString pad(int num) const;
    QString formatTime(int seconds) const;
    QString precisionLabel(const QString &value) const;

private:
    Ui::MainWindow *ui = nullptr;

    QVector<QPushButton *> m_navButtons;
    QVector<VideoOpenGLWidget *> m_cameraButtons;
    QVector<QLabel *> m_summaryValues;
    QPushButton *m_fullScreenButton = nullptr;
    std::unique_ptr<HandAnalysisManager> m_handAnalysisManager;
    std::unique_ptr<PoseStandardnessScorer> m_poseStandardnessScorer;
    SkeletonViewWidget *m_skeletonView = nullptr;

    TrajectoryWidget *m_trajectoryWidget = nullptr;
    QTimer m_timer;
    QVector<TrainingRecord> m_records;
    QString m_lastSavedAt;

    int m_activePage = 0;
    bool m_sidebarVisible = true;
    bool m_sidebarMetricsCaptured = false;
    QMargins m_sidebarLayoutMargins;
    int m_sidebarLayoutSpacing = 10;
    int m_trajectoryMode = -1;
    int m_middleLayoutNormalSpacing = 14;
    int m_middleLayoutNormalStretch0 = 0;
    int m_middleLayoutNormalStretch1 = 0;
    int m_cameraGridNormalSpacing = 10;
    QSize m_trajectoryViewNormalMinSize;
    QSize m_trajectoryViewNormalMaxSize;
    QSize m_trajectoryCardNormalMinSize;
    QSize m_trajectoryCardNormalMaxSize;
    int m_selectedCamera = 1;
    bool m_isRecording = false;
    bool m_isPaused = false;
    int m_durationSec = 0;
    int m_actionCount = 0;
    int m_realtimeScore = 90;
    QString m_feedbackText = QStringLiteral("动作标准");
};

#endif // MAINWINDOW_H
