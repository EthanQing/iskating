#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "systemsettingsdialog.h"
#include "trainingdomain.h"

#include <QMainWindow>
#include <QMargins>
#include <QSize>
#include <QTimer>
#include <QVector>

#include <memory>

class QLabel;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QSpinBox;
class QPlainTextEdit;
class PoseStandardnessScorer;
struct PoseFrameResult;
class QEvent;
class QProgressBar;
class QPushButton;
class SkeletonViewWidget;
class QVBoxLayout;
class TrajectoryWidget;
class VideoOpenGLWidget;
class HandAnalysisManager;
class ActionStandardScorer;
class ActionRepetitionTracker;
class TrainingRepository;

namespace Ui {
class MainWindow;
}

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
    void installMetricBars();
    void installTrainingContextPanel();
    void installHistorySearchPanel();
    void installTrajectoryWidget();
    void installSkeletonView();
    void installStaticImages();
    void applyStyleSheet();
    void loadCameraSettings();
    void saveCameraSettings();
    void saveCameraSetting(int cameraIndex);
    void loadTrainingRecords();
    void initializeTrainingRepository();
    void reloadTrainingContext();
    void reloadHistorySearchOptions();
    SessionSearchFilters currentHistorySearchFilters() const;
    SessionSearchSort currentHistorySearchSort() const;
    void resetHistorySearch();
    int historyMaxPage() const;
    void refreshHistoryPager();
    void refreshTrainingContextDetails();
    void addAthleteFromDialog();
    void addCoachFromDialog();
    void openPersonManagement();
    void openCompetitionManagement();
    ActionStandard selectedActionStandard() const;
    QString selectedAthleteId() const;
    QString selectedCoachId() const;
    QString selectedCompetitionId() const;
    QString selectedCompetitionEventId() const;
    QString selectedEventAthleteId() const;
    void resetCurrentTrainingSession();
    void recordCompletedRepetition(const ActionRepetition &repetition);
    void importOfflineVideo();
    void showOfflineVideoInMainView(bool autoPlay = true);
    void showCameraInMainView(int cameraIndex, bool autoPlay = true);
    void applyCameraSettingsToWidgets(bool restorePlayback);
    void updateTrajectoryCameraSegments();
    void syncAnalysisStreams();
    QString cameraReadinessSummary() const;
    void applyCapturePreferencesToUi();
    void openSystemSettings();
    void persistSystemSettings() const;
    CapturePreferenceSettings capturePreferenceSettingsFromUi() const;

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
    void updateActionCounter(const PoseFrameResult &poseFrame);

    void refreshNavButtons();
    void refreshCameraButtons();
    void refreshStats();
    void refreshHistory();
    void refreshSuggestions();
    void openSessionVideo(const SessionHistoryItem &record, int offsetMs = 0, int endOffsetMs = -1);
    void openTrainingReview(const SessionHistoryItem &record);
    void editActionStandard();
    void editCoachComment(const QString &sessionId);
    void exportTrainingReport(const QString &sessionId);
    void refreshSidebarButton();
    void refreshFullScreenButton();
    void refreshModelStatus(const QString &statusText);
    void clearRealtimePose();
    void repolish(QWidget *widget) const;

    QString pad(int num) const;
    QString formatTime(int seconds) const;
    QString formatMilliseconds(int milliseconds) const;
    QString precisionLabel(const QString &value) const;

private:
    Ui::MainWindow *ui = nullptr;

    QVector<QPushButton *> m_navButtons;
    QVector<VideoOpenGLWidget *> m_cameraButtons;
    QVector<QLabel *> m_summaryValues;
    QVector<QProgressBar *> m_metricBars;
    QVector<QLabel *> m_metricValueLabels;
    QPushButton *m_fullScreenButton = nullptr;
    QPushButton *m_importVideoButton = nullptr;
    std::unique_ptr<HandAnalysisManager> m_handAnalysisManager;
    std::unique_ptr<PoseStandardnessScorer> m_poseStandardnessScorer;
    std::unique_ptr<ActionStandardScorer> m_actionStandardScorer;
    std::unique_ptr<ActionRepetitionTracker> m_actionRepetitionTracker;
    std::unique_ptr<TrainingRepository> m_trainingRepository;
    SkeletonViewWidget *m_skeletonView = nullptr;
    QWidget *m_trainingContextPanel = nullptr;
    QComboBox *m_athleteComboBox = nullptr;
    QComboBox *m_coachComboBox = nullptr;
    QComboBox *m_competitionComboBox = nullptr;
    QComboBox *m_competitionEventComboBox = nullptr;
    QComboBox *m_actionStandardComboBox = nullptr;
    QLineEdit *m_siteLineEdit = nullptr;
    QComboBox *m_trainingPhaseComboBox = nullptr;
    QLineEdit *m_goalLineEdit = nullptr;
    QSpinBox *m_targetRepsSpinBox = nullptr;
    QSpinBox *m_targetScoreSpinBox = nullptr;
    QSpinBox *m_setCountSpinBox = nullptr;
    QSpinBox *m_restSecondsSpinBox = nullptr;
    QPlainTextEdit *m_trainingNotesEdit = nullptr;
    QLabel *m_standardDetailLabel = nullptr;
    QLabel *m_trainingTargetLabel = nullptr;
    QWidget *m_historySearchPanel = nullptr;
    QComboBox *m_historyAthleteComboBox = nullptr;
    QComboBox *m_historyCoachComboBox = nullptr;
    QComboBox *m_historyActionComboBox = nullptr;
    QComboBox *m_historyCompetitionComboBox = nullptr;
    QComboBox *m_historyCompetitionEventComboBox = nullptr;
    QComboBox *m_historySourceTypeComboBox = nullptr;
    QComboBox *m_historySortComboBox = nullptr;
    QLineEdit *m_historyCompetitionLineEdit = nullptr;
    QSpinBox *m_historyMinScoreSpinBox = nullptr;
    QSpinBox *m_historyMaxScoreSpinBox = nullptr;
    QCheckBox *m_historyFromCheckBox = nullptr;
    QCheckBox *m_historyToCheckBox = nullptr;
    QDateEdit *m_historyFromDateEdit = nullptr;
    QDateEdit *m_historyToDateEdit = nullptr;
    QLabel *m_historyPageLabel = nullptr;
    QPushButton *m_historyPreviousPageButton = nullptr;
    QPushButton *m_historyNextPageButton = nullptr;

    TrajectoryWidget *m_trajectoryWidget = nullptr;
    QTimer m_timer;
    QVector<SessionHistoryItem> m_records;
    QVector<AthleteProfile> m_athletes;
    QVector<CoachProfile> m_coaches;
    QVector<Competition> m_competitions;
    QVector<CompetitionEvent> m_competitionEvents;
    QVector<EventAthlete> m_eventAthletes;
    QVector<ActionStandard> m_actionStandards;
    QVector<ActionRepetition> m_currentRepetitions;
    QString m_lastSavedAt;
    int m_historyPageNumber = 1;
    int m_historyPageSize = 10;
    int m_historyTotalCount = 0;

    int m_activePage = 0;
    bool m_sidebarVisible = true;
    bool m_sidebarMetricsCaptured = false;
    QMargins m_sidebarLayoutMargins;
    int m_sidebarLayoutSpacing = 10;
    int m_sidebarNormalMinimumWidth = 0;
    int m_sidebarNormalMaximumWidth = QWIDGETSIZE_MAX;
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
    qint64 m_recordingStartedAtMsec = 0;
    QDateTime m_recordingStartedAt;
    int m_actionCount = 0;
    int m_validActionCount = 0;
    int m_bestActionScore = 0;
    int m_actionScoreTotal = 0;
    int m_realtimeScore = 90;
    int m_detectionScore = 0;
    int m_symmetryScore = 0;
    int m_balanceScore = 0;
    int m_stabilityScore = 0;
    int m_depthScore = 0;
    qreal m_previousKneeBend = 0.0;
    bool m_actionArmed = false;
    qint64 m_lastActionMsec = 0;
    QString m_feedbackText = QStringLiteral("动作标准");
    QString m_offlineVideoPath;
    QString m_offlineVideoName;
    SharedCameraSettings m_sharedCameraSettings;
    QVector<CameraSlotSettings> m_cameraSlotSettings;
    CapturePreferenceSettings m_capturePreferenceSettings;
};

#endif // MAINWINDOW_H
