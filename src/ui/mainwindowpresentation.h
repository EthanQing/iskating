#ifndef MAINWINDOWPRESENTATION_H
#define MAINWINDOWPRESENTATION_H
#include <QObject>
#include <QVariantList>

// View state only. MainWindow handles requests through its existing business flow.
class MainWindowPresentation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString selectedAthleteId READ selectedAthleteId NOTIFY selectedAthleteIdChanged)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY pageTitleChanged)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(QString athleteName READ athleteName NOTIFY athleteNameChanged)
    Q_PROPERTY(QString serviceText READ serviceText NOTIFY serviceTextChanged)
    Q_PROPERTY(QString serviceState READ serviceState NOTIFY serviceStateChanged)
    Q_PROPERTY(QString trainingState READ trainingState NOTIFY trainingStateChanged)
    Q_PROPERTY(QString trainingRole READ trainingRole NOTIFY trainingRoleChanged)
    Q_PROPERTY(QString duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString detectionCount READ detectionCount NOTIFY detectionCountChanged)
    Q_PROPERTY(QString detectionTip READ detectionTip NOTIFY detectionTipChanged)
    Q_PROPERTY(QString identity READ identity NOTIFY identityChanged)
    Q_PROPERTY(QString identityState READ identityState NOTIFY identityStateChanged)
    Q_PROPERTY(QString speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QString speedState READ speedState NOTIFY speedStateChanged)
    Q_PROPERTY(QString modelText READ modelText NOTIFY modelTextChanged)
    Q_PROPERTY(QString modelTip READ modelTip NOTIFY modelTipChanged)
    Q_PROPERTY(QString modelState READ modelState NOTIFY modelStateChanged)
    Q_PROPERTY(QString identityAvailability READ identityAvailability NOTIFY identityAvailabilityChanged)
    Q_PROPERTY(QString identityAvailabilityState READ identityAvailabilityState NOTIFY identityAvailabilityStateChanged)
    Q_PROPERTY(QString startText READ startText NOTIFY startTextChanged)
    Q_PROPERTY(QString saveTip READ saveTip NOTIFY saveTipChanged)
    Q_PROPERTY(QString checkText READ checkText NOTIFY checkTextChanged)
    Q_PROPERTY(QString checkResult READ checkResult NOTIFY checkResultChanged)
    Q_PROPERTY(int activePage READ activePage NOTIFY activePageChanged)
    Q_PROPERTY(QVariantList athletes READ athletes NOTIFY athletesChanged)
    Q_PROPERTY(bool sidebarExpanded READ sidebarExpanded NOTIFY sidebarExpandedChanged)
    Q_PROPERTY(bool fullScreen READ fullScreen NOTIFY fullScreenChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY canStartChanged)
    Q_PROPERTY(bool canPause READ canPause NOTIFY canPauseChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY canStopChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY canSaveChanged)
    Q_PROPERTY(bool settingsExpanded READ settingsExpanded NOTIFY settingsExpandedChanged)
    Q_PROPERTY(bool canCheck READ canCheck NOTIFY canCheckChanged)
public:
    explicit MainWindowPresentation(QObject *parent = nullptr);
    QString selectedAthleteId() const { return m_selectedAthleteId; }
    void setSelectedAthleteId(const QString &value);
    QString pageTitle() const { return m_pageTitle; }
    void setPageTitle(const QString &value);
    QString source() const { return m_source; }
    void setSource(const QString &value);
    QString athleteName() const { return m_athleteName; }
    void setAthleteName(const QString &value);
    QString serviceText() const { return m_serviceText; }
    void setServiceText(const QString &value);
    QString serviceState() const { return m_serviceState; }
    void setServiceState(const QString &value);
    QString trainingState() const { return m_trainingState; }
    void setTrainingState(const QString &value);
    QString trainingRole() const { return m_trainingRole; }
    void setTrainingRole(const QString &value);
    QString duration() const { return m_duration; }
    void setDuration(const QString &value);
    QString detectionCount() const { return m_detectionCount; }
    void setDetectionCount(const QString &value);
    QString detectionTip() const { return m_detectionTip; }
    void setDetectionTip(const QString &value);
    QString identity() const { return m_identity; }
    void setIdentity(const QString &value);
    QString identityState() const { return m_identityState; }
    void setIdentityState(const QString &value);
    QString speed() const { return m_speed; }
    void setSpeed(const QString &value);
    QString speedState() const { return m_speedState; }
    void setSpeedState(const QString &value);
    QString modelText() const { return m_modelText; }
    void setModelText(const QString &value);
    QString modelTip() const { return m_modelTip; }
    void setModelTip(const QString &value);
    QString modelState() const { return m_modelState; }
    void setModelState(const QString &value);
    QString identityAvailability() const { return m_identityAvailability; }
    void setIdentityAvailability(const QString &value);
    QString identityAvailabilityState() const { return m_identityAvailabilityState; }
    void setIdentityAvailabilityState(const QString &value);
    QString startText() const { return m_startText; }
    void setStartText(const QString &value);
    QString saveTip() const { return m_saveTip; }
    void setSaveTip(const QString &value);
    QString checkText() const { return m_checkText; }
    void setCheckText(const QString &value);
    QString checkResult() const { return m_checkResult; }
    void setCheckResult(const QString &value);
    int activePage() const { return m_activePage; }
    void setActivePage(const int &value);
    QVariantList athletes() const { return m_athletes; }
    void setAthletes(const QVariantList &value);
    bool sidebarExpanded() const { return m_sidebarExpanded; }
    void setSidebarExpanded(const bool &value);
    bool fullScreen() const { return m_fullScreen; }
    void setFullScreen(const bool &value);
    bool canStart() const { return m_canStart; }
    void setCanStart(const bool &value);
    bool canPause() const { return m_canPause; }
    void setCanPause(const bool &value);
    bool canStop() const { return m_canStop; }
    void setCanStop(const bool &value);
    bool canSave() const { return m_canSave; }
    void setCanSave(const bool &value);
    bool settingsExpanded() const { return m_settingsExpanded; }
    void setSettingsExpanded(const bool &value);
    bool canCheck() const { return m_canCheck; }
    void setCanCheck(const bool &value);
    Q_INVOKABLE void selectAthlete(const QString &id);
    Q_INVOKABLE void navigate(int page);
    Q_INVOKABLE void requestStart();
    Q_INVOKABLE void requestPause();
    Q_INVOKABLE void requestStop();
    Q_INVOKABLE void requestSave();
    Q_INVOKABLE void requestCheck();
    Q_INVOKABLE void requestToggleSidebar();
    Q_INVOKABLE void requestToggleFullScreen();
    Q_INVOKABLE void requestTrainingSettings();
    Q_INVOKABLE void requestManualIdentity();
    Q_INVOKABLE void requestPersonManagement();
    Q_INVOKABLE void requestCompetitionManagement();
    Q_INVOKABLE void requestSystemSettings();
    Q_INVOKABLE void requestImportVideo();
    Q_INVOKABLE void requestOfflineAnalysis();
    Q_INVOKABLE void requestTaskCenter();
signals:
    void selectedAthleteIdChanged();
    void pageTitleChanged();
    void sourceChanged();
    void athleteNameChanged();
    void serviceTextChanged();
    void serviceStateChanged();
    void trainingStateChanged();
    void trainingRoleChanged();
    void durationChanged();
    void detectionCountChanged();
    void detectionTipChanged();
    void identityChanged();
    void identityStateChanged();
    void speedChanged();
    void speedStateChanged();
    void modelTextChanged();
    void modelTipChanged();
    void modelStateChanged();
    void identityAvailabilityChanged();
    void identityAvailabilityStateChanged();
    void startTextChanged();
    void saveTipChanged();
    void checkTextChanged();
    void checkResultChanged();
    void activePageChanged();
    void athletesChanged();
    void sidebarExpandedChanged();
    void fullScreenChanged();
    void canStartChanged();
    void canPauseChanged();
    void canStopChanged();
    void canSaveChanged();
    void settingsExpandedChanged();
    void canCheckChanged();
    void athleteSelected(const QString &id);
    void pageRequested(int page);
    void startRequested();
    void pauseRequested();
    void stopRequested();
    void saveRequested();
    void checkRequested();
    void toggleSidebarRequested();
    void toggleFullScreenRequested();
    void trainingSettingsRequested();
    void manualIdentityRequested();
    void personManagementRequested();
    void competitionManagementRequested();
    void systemSettingsRequested();
    void importVideoRequested();
    void offlineAnalysisRequested();
    void taskCenterRequested();
private:
    QString m_selectedAthleteId = {};
    QString m_pageTitle = {};
    QString m_source = {};
    QString m_athleteName = {};
    QString m_serviceText = {};
    QString m_serviceState = {};
    QString m_trainingState = {};
    QString m_trainingRole = {};
    QString m_duration = {};
    QString m_detectionCount = {};
    QString m_detectionTip = {};
    QString m_identity = {};
    QString m_identityState = {};
    QString m_speed = {};
    QString m_speedState = {};
    QString m_modelText = {};
    QString m_modelTip = {};
    QString m_modelState = {};
    QString m_identityAvailability = {};
    QString m_identityAvailabilityState = {};
    QString m_startText = {};
    QString m_saveTip = {};
    QString m_checkText = {};
    QString m_checkResult = {};
    int m_activePage = 0;
    QVariantList m_athletes = {};
    bool m_sidebarExpanded = true;
    bool m_fullScreen = false;
    bool m_canStart = true;
    bool m_canPause = false;
    bool m_canStop = false;
    bool m_canSave = false;
    bool m_settingsExpanded = false;
    bool m_canCheck = true;
};
#endif
