#include "mainwindowpresentation.h"

MainWindowPresentation::MainWindowPresentation(QObject *parent) : QObject(parent) {}

void MainWindowPresentation::setSelectedAthleteId(const QString &value)
{
    if (m_selectedAthleteId == value) return;
    m_selectedAthleteId = value;
    emit selectedAthleteIdChanged();
}

void MainWindowPresentation::setPageTitle(const QString &value)
{
    if (m_pageTitle == value) return;
    m_pageTitle = value;
    emit pageTitleChanged();
}

void MainWindowPresentation::setSource(const QString &value)
{
    if (m_source == value) return;
    m_source = value;
    emit sourceChanged();
}

void MainWindowPresentation::setAthleteName(const QString &value)
{
    if (m_athleteName == value) return;
    m_athleteName = value;
    emit athleteNameChanged();
}

void MainWindowPresentation::setServiceText(const QString &value)
{
    if (m_serviceText == value) return;
    m_serviceText = value;
    emit serviceTextChanged();
}

void MainWindowPresentation::setServiceState(const QString &value)
{
    if (m_serviceState == value) return;
    m_serviceState = value;
    emit serviceStateChanged();
}

void MainWindowPresentation::setTrainingState(const QString &value)
{
    if (m_trainingState == value) return;
    m_trainingState = value;
    emit trainingStateChanged();
}

void MainWindowPresentation::setTrainingRole(const QString &value)
{
    if (m_trainingRole == value) return;
    m_trainingRole = value;
    emit trainingRoleChanged();
}

void MainWindowPresentation::setDuration(const QString &value)
{
    if (m_duration == value) return;
    m_duration = value;
    emit durationChanged();
}

void MainWindowPresentation::setDetectionCount(const QString &value)
{
    if (m_detectionCount == value) return;
    m_detectionCount = value;
    emit detectionCountChanged();
}

void MainWindowPresentation::setDetectionTip(const QString &value)
{
    if (m_detectionTip == value) return;
    m_detectionTip = value;
    emit detectionTipChanged();
}

void MainWindowPresentation::setIdentity(const QString &value)
{
    if (m_identity == value) return;
    m_identity = value;
    emit identityChanged();
}

void MainWindowPresentation::setIdentityState(const QString &value)
{
    if (m_identityState == value) return;
    m_identityState = value;
    emit identityStateChanged();
}

void MainWindowPresentation::setSpeed(const QString &value)
{
    if (m_speed == value) return;
    m_speed = value;
    emit speedChanged();
}

void MainWindowPresentation::setSpeedState(const QString &value)
{
    if (m_speedState == value) return;
    m_speedState = value;
    emit speedStateChanged();
}

void MainWindowPresentation::setModelText(const QString &value)
{
    if (m_modelText == value) return;
    m_modelText = value;
    emit modelTextChanged();
}

void MainWindowPresentation::setModelTip(const QString &value)
{
    if (m_modelTip == value) return;
    m_modelTip = value;
    emit modelTipChanged();
}

void MainWindowPresentation::setModelState(const QString &value)
{
    if (m_modelState == value) return;
    m_modelState = value;
    emit modelStateChanged();
}

void MainWindowPresentation::setIdentityAvailability(const QString &value)
{
    if (m_identityAvailability == value) return;
    m_identityAvailability = value;
    emit identityAvailabilityChanged();
}

void MainWindowPresentation::setIdentityAvailabilityState(const QString &value)
{
    if (m_identityAvailabilityState == value) return;
    m_identityAvailabilityState = value;
    emit identityAvailabilityStateChanged();
}

void MainWindowPresentation::setStartText(const QString &value)
{
    if (m_startText == value) return;
    m_startText = value;
    emit startTextChanged();
}

void MainWindowPresentation::setSaveTip(const QString &value)
{
    if (m_saveTip == value) return;
    m_saveTip = value;
    emit saveTipChanged();
}

void MainWindowPresentation::setCheckText(const QString &value)
{
    if (m_checkText == value) return;
    m_checkText = value;
    emit checkTextChanged();
}

void MainWindowPresentation::setCheckResult(const QString &value)
{
    if (m_checkResult == value) return;
    m_checkResult = value;
    emit checkResultChanged();
}

void MainWindowPresentation::setActivePage(const int &value)
{
    if (m_activePage == value) return;
    m_activePage = value;
    emit activePageChanged();
}

void MainWindowPresentation::setAthletes(const QVariantList &value)
{
    if (m_athletes == value) return;
    m_athletes = value;
    emit athletesChanged();
}

void MainWindowPresentation::setSidebarExpanded(const bool &value)
{
    if (m_sidebarExpanded == value) return;
    m_sidebarExpanded = value;
    emit sidebarExpandedChanged();
}

void MainWindowPresentation::setFullScreen(const bool &value)
{
    if (m_fullScreen == value) return;
    m_fullScreen = value;
    emit fullScreenChanged();
}

void MainWindowPresentation::setCanStart(const bool &value)
{
    if (m_canStart == value) return;
    m_canStart = value;
    emit canStartChanged();
}

void MainWindowPresentation::setCanPause(const bool &value)
{
    if (m_canPause == value) return;
    m_canPause = value;
    emit canPauseChanged();
}

void MainWindowPresentation::setCanStop(const bool &value)
{
    if (m_canStop == value) return;
    m_canStop = value;
    emit canStopChanged();
}

void MainWindowPresentation::setCanSave(const bool &value)
{
    if (m_canSave == value) return;
    m_canSave = value;
    emit canSaveChanged();
}

void MainWindowPresentation::setSettingsExpanded(const bool &value)
{
    if (m_settingsExpanded == value) return;
    m_settingsExpanded = value;
    emit settingsExpandedChanged();
}

void MainWindowPresentation::setCanCheck(const bool &value)
{
    if (m_canCheck == value) return;
    m_canCheck = value;
    emit canCheckChanged();
}

void MainWindowPresentation::selectAthlete(const QString &id)
{
    for (const QVariant &entry : m_athletes) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == id) {
            if (id != m_selectedAthleteId) emit athleteSelected(id);
            return;
        }
    }
}

void MainWindowPresentation::navigate(int page)
{
    if ((page == 0 || page == 1 || page == 3) && page != m_activePage)
        emit pageRequested(page);
}

void MainWindowPresentation::requestStart()
{
    if (m_canStart) emit startRequested();
}

void MainWindowPresentation::requestPause()
{
    if (m_canPause) emit pauseRequested();
}

void MainWindowPresentation::requestStop()
{
    if (m_canStop) emit stopRequested();
}

void MainWindowPresentation::requestSave()
{
    if (m_canSave) emit saveRequested();
}

void MainWindowPresentation::requestCheck()
{
    if (m_canCheck) emit checkRequested();
}

void MainWindowPresentation::requestToggleSidebar()
{
    emit toggleSidebarRequested();
}

void MainWindowPresentation::requestToggleFullScreen()
{
    emit toggleFullScreenRequested();
}

void MainWindowPresentation::requestTrainingSettings()
{
    emit trainingSettingsRequested();
}

void MainWindowPresentation::requestManualIdentity()
{
    emit manualIdentityRequested();
}

void MainWindowPresentation::requestPersonManagement()
{
    emit personManagementRequested();
}

void MainWindowPresentation::requestCompetitionManagement()
{
    emit competitionManagementRequested();
}

void MainWindowPresentation::requestSystemSettings()
{
    emit systemSettingsRequested();
}

void MainWindowPresentation::requestImportVideo()
{
    emit importVideoRequested();
}

void MainWindowPresentation::requestOfflineAnalysis()
{
    emit offlineAnalysisRequested();
}

void MainWindowPresentation::requestTaskCenter()
{
    emit taskCenterRequested();
}
