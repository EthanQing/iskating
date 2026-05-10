#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSize>
#include <QTimer>
#include <QVector>

class QLabel;
class QPushButton;
class QVBoxLayout;
class TrajectoryWidget;
class VideoOpenGLWidget;

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

private:
    void setupUiState();
    void setupConnections();
    void installTrajectoryWidget();
    void installStaticImages();
    void applyStyleSheet();

    void switchPage(int pageIndex);
    void toggleSidebar();
    void selectCamera(int cameraId);
    void setTrajectoryExpanded(bool expanded);
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
    void repolish(QWidget *widget) const;

    QString pad(int num) const;
    QString formatTime(int seconds) const;
    QString precisionLabel(const QString &value) const;

private:
    Ui::MainWindow *ui = nullptr;

    QVector<QPushButton *> m_navButtons;
    QVector<VideoOpenGLWidget *> m_cameraButtons;
    QVector<QLabel *> m_summaryValues;

    TrajectoryWidget *m_trajectoryWidget = nullptr;
    QTimer m_timer;
    QVector<TrainingRecord> m_records;
    QString m_lastSavedAt;

    int m_activePage = 0;
    bool m_sidebarVisible = true;
    bool m_trajectoryExpanded = false;
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
