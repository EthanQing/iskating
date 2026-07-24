#ifndef OFFLINEANALYSISDIALOG_H
#define OFFLINEANALYSISDIALOG_H

#include "trainingdomain.h"

#include <QDialog>
#include <QTimer>
#include <QVector>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class TrainingRepository;
class AnalysisTaskManager;

class OfflineAnalysisDialog : public QDialog
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
    OfflineAnalysisBatch batchFromTable(QString *errorMessage) const;

    TrainingRepository *m_repository = nullptr;
    AnalysisTaskManager *m_taskManager = nullptr;
    QVector<QString> m_athleteIds;
    QTableWidget *m_sourcesTable = nullptr;
    QLineEdit *m_nasRootEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_activateButton = nullptr;
    QTimer m_refreshTimer;
    OfflineAnalysisBatch m_batch;
    OfflineAnalysisRun m_run;
};

#endif // OFFLINEANALYSISDIALOG_H
