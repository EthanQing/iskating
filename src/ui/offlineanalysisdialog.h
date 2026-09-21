#ifndef OFFLINEANALYSISDIALOG_H
#define OFFLINEANALYSISDIALOG_H

#include "framelessdialog.h"
#include "trainingdomain.h"

#include <QTimer>
#include <QSet>
#include <QVector>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class TrainingRepository;
class AnalysisTaskManager;

class OfflineAnalysisDialog : public FramelessDialog
{
public:
    OfflineAnalysisDialog(TrainingRepository *repository,
                          QVector<QString> athleteIds,
                          AnalysisTaskManager *taskManager = nullptr,
                          QWidget *parent = nullptr);
    QString batchId() const { return m_batch.id; }
    QString activeRunId() const { return m_batch.activeRunId; }

private:
    void importManifest();
    void createBatch();
    void refreshRun();
    void cancelRun();
    void retryRun();
    void activateRun();
    void setBatch(const OfflineAnalysisBatch &batch);
    void setRun(const OfflineAnalysisRun &run);
    void updateRunPresentation();
    OfflineAnalysisBatch batchFromTable(QString *errorMessage) const;

    TrainingRepository *m_repository = nullptr;
    AnalysisTaskManager *m_taskManager = nullptr;
    QVector<QString> m_athleteIds;
    QTableWidget *m_sourcesTable = nullptr;
    QLineEdit *m_nasRootEdit = nullptr;
    QLabel *m_inlineStatus = nullptr;
    QLabel *m_emptyRunLabel = nullptr;
    QWidget *m_runDetails = nullptr;
    QLabel *m_runStatus = nullptr;
    QLabel *m_progressText = nullptr;
    QLabel *m_batchIdLabel = nullptr;
    QLabel *m_runIdLabel = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_startedAtLabel = nullptr;
    QLabel *m_framesLabel = nullptr;
    QLabel *m_throughputLabel = nullptr;
    QLabel *m_etaLabel = nullptr;
    QPlainTextEdit *m_errorText = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_activateButton = nullptr;
    QTimer m_refreshTimer;
    OfflineAnalysisBatch m_batch;
    OfflineAnalysisRun m_run;
    bool m_submissionPending = false;
    QSet<QString> m_tasksBeforeSubmission;
};

#endif // OFFLINEANALYSISDIALOG_H
