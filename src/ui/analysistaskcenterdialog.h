#ifndef ANALYSISTASKCENTERDIALOG_H
#define ANALYSISTASKCENTERDIALOG_H

#include <QDialog>

class AnalysisTaskManager;
class QTableWidget;
class QPushButton;

class AnalysisTaskCenterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AnalysisTaskCenterDialog(AnalysisTaskManager *manager, QWidget *parent = nullptr);

signals:
    void openSessionRequested(const QString &sessionId);

private:
    void reload();
    QString selectedTaskId() const;

    AnalysisTaskManager *m_manager = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_resumeButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_openSessionButton = nullptr;
};

#endif // ANALYSISTASKCENTERDIALOG_H
