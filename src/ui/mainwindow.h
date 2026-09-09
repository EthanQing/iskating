#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "athleteanalysisresult.h"
#include "systemsettingsdialog.h"
#include "trainingdomain.h"
#include "offlinevideoprobe.h"
#include "cameraconnectivitytester.h"

#include <QHash>
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
class QDateTime;
class QThread;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QPlainTextEdit;
class QEvent;
class QMoveEvent;
class QResizeEvent;
class QPushButton;
class QScrollArea;
class QGridLayout;
class QSplitter;
class QVariantAnimation;
class QVBoxLayout;
class FramelessDialog;
class VideoOpenGLWidget;
class TrajectoryWidget;
class AthleteAnalysisManager;
class AnalysisTaskManager;
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
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUiState();
    void setupConnections();
    void installTrainingContextPanel();
    void installHistorySearchPanel();
    void rebuildWorkspaceLayout();
    void updateCameraGrid();
    void openTrainingSettings();
    void closeTrainingSettings();
    void applyStyleSheet();
    void loadCameraSettings();
    void refreshCameraConfigurationStatus();
    void refreshCameraRuntimeStatus();
    void startEnvironmentCheck();
    void invalidateCameraConnectivityResults();
    void refreshCameraConnectivityStatus();
    void refreshAiCapabilityStatus();
    void saveCameraSettings();
    void saveCameraSetting(int cameraIndex);
    void loadTrainingRecords();
    void initializeTrainingRepository();
    void refreshTrainingServiceStatus(const QDateTime &checkedAt);
    void reloadTrainingContext();
    void reloadHistorySearchOptions();
    SessionSearchFilters currentHistorySearchFilters() const;
    SessionSearchSort currentHistorySearchSort() const;
    void resetHistorySearch();
    int historyMaxPage() const;
    void refreshHistoryPager();
    void refreshTrainingContextDetails();
    void reloadAthleteIdentityGallery();
    void editManualIdentityBindings();
    void addAthleteFromDialog();
    void addCoachFromDialog();
    void openPersonManagement();
    void openCompetitionManagement();
    void openRepetitionSearchDialog();
    ActionStandard selectedActionStandard() const;
    QString selectedAthleteId() const;
    QString selectedCoachId() const;
    QString selectedCompetitionId() const;
    QString selectedCompetitionEventId() const;
    QString selectedEventAthleteId() const;
    QVector<TrainingSessionParticipant> currentSessionParticipants() const;
    void resetCurrentTrainingSession();
    void recordAthleteFrames(const AthleteFrameResult &athleteFrame);
    void refreshTrajectoryView();
    void importOfflineVideo();
    void openOfflineAnalysisManager();
    void openAnalysisTaskCenter();
    void showOfflineVideoInMainView(bool autoPlay = true);
    void showCameraInMainView(int cameraIndex, bool autoPlay = true);
    void applyCameraSettingsToWidgets(bool restorePlayback);
    void syncAnalysisStreams();
    QString cameraReadinessSummary() const;
    void applyCapturePreferencesToUi();
    void openSystemSettings();
    void persistSystemSettings() const;
    CapturePreferenceSettings capturePreferenceSettingsFromUi() const;
    QString videoStorageRootDir() const;
    void refreshVideoStorageStatus();
    void refreshAnalysisTaskStatus();
    QString videoStorageStatusSummary(const VideoStorageSettings &settings) const;
    QVector<VideoFileCleanupCandidate> videoCleanupCandidates(const VideoStorageSettings &settings,
                                                              qint64 *existingBytes = nullptr) const;
    void showVideoCleanupCandidates(SystemSettingsDialog *dialog);

    void switchPage(int pageIndex);
    void toggleSidebar();
    void toggleFullScreen();
    void exitFullScreenMode();
    void selectCamera(int cameraId);
    void startCapture();
    void pauseCapture();
    void stopCapture();
    void saveRecord();
    void tick();

    void refreshNavButtons();
    void refreshCameraButtons();
    void refreshStats();
    void showTrajectorySnapshot();
    void refreshHistory();
    void selectHistorySession(const QString &sessionId);
    void refreshHistorySessionDetail();
    const SessionHistoryItem *historySessionById(const QString &sessionId) const;
    bool historySessionHasPlayableVideo(const SessionHistoryItem &record) const;
    void refreshSuggestions();
    void openSessionVideo(const SessionHistoryItem &record,
                          int offsetMs = 0,
                          int endOffsetMs = -1,
                          const QString &videoFileId = QString(),
                          int videoIndex = 0);
    void openTrainingReview(const SessionHistoryItem &record);
    void openTrackPointReview(const SessionHistoryItem &record);
    void editActionStandard();
    void editCoachComment(const QString &sessionId);
    void exportTrainingReport(const QString &sessionId);
    void openMetricReportCenter();
    void refreshSidebarButton();
    void refreshFullScreenButton();
    void refreshModelStatus(const QString &statusText);
    void clearRealtimeAnalysisFrame();
    void startOfflineAnalysisOverlay(const SessionHistoryItem &record, int cameraId);
    void refreshOfflineAnalysisOverlay();
    void clearOfflineAnalysisOverlay();
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
    QPushButton *m_fullScreenButton = nullptr;
    QPushButton *m_importVideoButton = nullptr;
    QPushButton *m_fullRateAnalysisButton = nullptr;
    QPushButton *m_taskCenterButton = nullptr;
    std::unique_ptr<AthleteAnalysisManager> m_athleteAnalysisManager;
    std::unique_ptr<AnalysisTaskManager> m_analysisTaskManager;
    std::unique_ptr<TrainingRepository> m_trainingRepository;
    QWidget *m_trainingContextPanel = nullptr;
    QComboBox *m_athleteComboBox = nullptr;
    QComboBox *m_drawerAthleteComboBox = nullptr;
    QComboBox *m_coachComboBox = nullptr;
    QComboBox *m_competitionComboBox = nullptr;
    QComboBox *m_competitionEventComboBox = nullptr;
    QComboBox *m_actionStandardComboBox = nullptr;
    QVector<QComboBox *> m_participantComboBoxes;
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
    QLabel *m_trainingStateLabel = nullptr;
    QLabel *m_speedStatusLabel = nullptr;
    QLabel *m_athleteNameLabel = nullptr;
    QLabel *m_identityAvailabilityLabel = nullptr;
    QScrollArea *m_saveTipScrollArea = nullptr;
    QSplitter *m_workspaceSplitter = nullptr;
    QWidget *m_cameraGridContainer = nullptr;
    QGridLayout *m_cameraGridLayout = nullptr;
    int m_cameraGridColumns = 0;
    QSize m_cameraTileSize;
    bool m_compactWorkspace = false;
    FramelessDialog *m_trainingSettingsDialog = nullptr;
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
    QListWidget *m_historySessionList = nullptr;
    QSplitter *m_historyWorkspace = nullptr;
    QWidget *m_historyDetailContent = nullptr;
    QWidget *m_historyDetailEmptyState = nullptr;
    QLabel *m_historyDetailAthleteLabel = nullptr;
    QLabel *m_historyDetailTimeLabel = nullptr;
    QLabel *m_historyDetailSourceLabel = nullptr;
    QLabel *m_historyDetailSummaryLabel = nullptr;
    QLabel *m_historyDetailTrainingInfoLabel = nullptr;
    QLabel *m_historyDetailSourceInfoLabel = nullptr;
    QLabel *m_historyDetailCommentLabel = nullptr;
    QLabel *m_historyEmptyTitleLabel = nullptr;
    QLabel *m_historyEmptyBodyLabel = nullptr;
    QPushButton *m_historyEmptyResetButton = nullptr;
    QPushButton *m_historyPlayButton = nullptr;
    QPushButton *m_historyReviewButton = nullptr;
    QPushButton *m_historyTrackButton = nullptr;
    QPushButton *m_historyCommentButton = nullptr;
    QPushButton *m_historyExportButton = nullptr;
    QPushButton *m_historySearchButton = nullptr;
    QPushButton *m_historyRepetitionSearchButton = nullptr;
    QPushButton *m_historyCompetitionManagementButton = nullptr;
    QPushButton *m_historyReportCenterButton = nullptr;
    QString m_selectedHistorySessionId;
    bool m_historyServiceAvailable = false;

    QTimer m_timer;
    QTimer m_statusTimer;
    QTimer m_analysisOverlayTimer;
    QVector<SessionHistoryItem> m_records;
    QVector<AthleteProfile> m_athletes;
    QVector<CoachProfile> m_coaches;
    QVector<Competition> m_competitions;
    QVector<CompetitionEvent> m_competitionEvents;
    QVector<EventAthlete> m_eventAthletes;
    QVector<ActionStandard> m_actionStandards;
    QVector<TrackPoint> m_currentTrackPoints;
    QVector<SpeedMetric> m_currentSpeedMetrics;
    QHash<QString, qint64> m_trackPointSampleTimes;
    QHash<QString, TrackPoint> m_latestTrackPoints;
    TrajectoryWidget *m_trajectoryWidget = nullptr;
    AthleteFrameResult m_lastAthleteFrame;
    qint64 m_lastSelectedFrameReceivedAtMsec = 0;
    QVector<AthleteIdentityBinding> m_manualIdentityBindings;
    QString m_lastSavedAt;
    int m_historyPageNumber = 1;
    int m_historyPageSize = 10;
    int m_historyTotalCount = 0;

    int m_activePage = 0;
    bool m_sidebarVisible = true;
    QVariantAnimation *m_sidebarAnimation = nullptr;
    QVariantAnimation *m_settingsAnimation = nullptr;
    bool m_settingsExpanded = false;
    bool m_aiAnalysisReady = false;
    bool m_aiModelFailed = false;
    QString m_lastModelStatusText;
    QPushButton *m_environmentCheckButton = nullptr;
    QLabel *m_environmentCheckLabel = nullptr;
    QThread *m_environmentCheckThread = nullptr;
    QVector<CameraConnectivityResult> m_lastCameraConnectivityResults;
    QDateTime m_lastEnvironmentCheckAt;
    int m_cameraConfigurationRevision = 0;
    bool m_cameraConnectivityChecked = false;
    bool m_cameraConnectivityInvalidated = false;
    bool m_identityRecognitionKnown = false;
    bool m_identityRecognitionAvailable = false;
    int m_cameraGridNormalSpacing = 10;
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
    int m_realtimeScore = 0;
    int m_detectionScore = 0;
    int m_symmetryScore = 0;
    int m_balanceScore = 0;
    int m_stabilityScore = 0;
    int m_depthScore = 0;
    QString m_feedbackText = QStringLiteral("运动员检测与身份识别");
    QString m_offlineVideoPath;
    QString m_offlineVideoName;
    OfflineVideoProbeResult m_offlineVideoProbe;
    OfflineAnalysisTask m_offlineAnalysisTask;
    QString m_offlineAnalysisBatchId;
    QString m_offlineAnalysisRunId;
    QString m_analysisOverlayRunId;
    int m_analysisOverlayCameraId = 0;
    OfflineAnalysisFrameWindow m_analysisOverlayWindow;
    SharedCameraSettings m_sharedCameraSettings;
    QVector<CameraSlotSettings> m_cameraSlotSettings;
    CapturePreferenceSettings m_capturePreferenceSettings;
    VideoStorageSettings m_videoStorageSettings;
};

#endif // MAINWINDOW_H
