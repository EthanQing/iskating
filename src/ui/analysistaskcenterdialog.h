#ifndef ANALYSISTASKCENTERDIALOG_H
#define ANALYSISTASKCENTERDIALOG_H

#include "framelessdialog.h"
#include "trainingdomain.h"

class AnalysisTaskManager;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

class AnalysisTaskCenterDialog : public FramelessDialog
{
    Q_OBJECT

public:
    explicit AnalysisTaskCenterDialog(AnalysisTaskManager *manager, QWidget *parent = nullptr);

signals:
    void openSessionRequested(const QString &sessionId);

private:
    void reload(const QString &preferredTaskId = {});
    void showTask(const QString &taskId);
    void updateActions();
    QString selectedTaskId() const;

    AnalysisTaskManager *m_manager = nullptr;
    QVector<AnalysisTask> m_visibleTasks;
    QComboBox *m_filter = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_syncStatus = nullptr;
    QListWidget *m_taskList = nullptr;
    QStackedWidget *m_detailStack = nullptr;
    QLabel *m_detailType = nullptr;
    QLabel *m_detailStatus = nullptr;
    QLabel *m_progressText = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_taskId = nullptr;
    QLabel *m_createdAt = nullptr;
    QLabel *m_updatedAt = nullptr;
    QWidget *m_inputFields = nullptr;
    QVBoxLayout *m_inputFieldsLayout = nullptr;
    QPushButton *m_rawInputToggle = nullptr;
    QPlainTextEdit *m_rawInput = nullptr;
    QLabel *m_outputSession = nullptr;
    QPlainTextEdit *m_errorText = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_resumeButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_openSessionButton = nullptr;
};

#endif // ANALYSISTASKCENTERDIALOG_H
