#ifndef TRAININGREVIEWDIALOG_H
#define TRAININGREVIEWDIALOG_H

#include "trainingdomain.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class TrainingRepository;
class VideoOpenGLWidget;

class TrainingReviewDialog : public QDialog
{
public:
    TrainingReviewDialog(const SessionHistoryItem &record,
                         const ActionStandard &standard,
                         TrainingRepository *repository,
                         QWidget *parent = nullptr);

private:
    void buildUi();
    void loadRepetitions();
    void populateTable();
    void selectRepetition(int row);
    void seekToRepetition(const ActionRepetition &repetition, bool keyFrame);
    void saveCurrentReview();
    void createManualRepetition();
    void chooseReferenceVideo();
    void saveReference();
    void refreshPositionLabel();
    void loadMainVideo(int offsetMs = 0);
    void loadReferenceVideo();
    ActionRepetition formRepetition() const;
    const TrainingVideoFile *videoFileForRepetition(const ActionRepetition &repetition) const;
    QString localVideoFileForRepetition(const ActionRepetition &repetition) const;
    QString videoSource() const;
    bool videoSourceIsUrl() const;
    bool hasLocalVideo() const;

    SessionHistoryItem m_record;
    ActionStandard m_standard;
    TrainingRepository *m_repository = nullptr;
    QVector<ActionRepetition> m_repetitions;
    int m_currentRow = -1;
    QString m_loadedMainVideoSource;
    bool m_loadedMainVideoIsLocal = false;

    VideoOpenGLWidget *m_mainVideo = nullptr;
    VideoOpenGLWidget *m_referenceVideo = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_positionLabel = nullptr;
    QSpinBox *m_startSpinBox = nullptr;
    QSpinBox *m_endSpinBox = nullptr;
    QCheckBox *m_validCheckBox = nullptr;
    QSpinBox *m_scoreSpinBox = nullptr;
    QSpinBox *m_detectionSpinBox = nullptr;
    QSpinBox *m_symmetrySpinBox = nullptr;
    QSpinBox *m_balanceSpinBox = nullptr;
    QSpinBox *m_stabilitySpinBox = nullptr;
    QSpinBox *m_depthSpinBox = nullptr;
    QLineEdit *m_errorLineEdit = nullptr;
    QPlainTextEdit *m_feedbackEdit = nullptr;
    QPlainTextEdit *m_noteEdit = nullptr;
    QLineEdit *m_referencePathEdit = nullptr;
    QPlainTextEdit *m_referenceNotesEdit = nullptr;
    QTimer *m_positionTimer = nullptr;
};

#endif // TRAININGREVIEWDIALOG_H
