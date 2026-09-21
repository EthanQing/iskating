#ifndef TRAININGREVIEWDIALOG_H
#define TRAININGREVIEWDIALOG_H

#include "framelessdialog.h"
#include "trainingdomain.h"
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

class QButtonGroup;

class TrainingReviewDialog : public FramelessDialog {
public:
  TrainingReviewDialog(const SessionHistoryItem &record,
                       const ActionStandard &standard,
                       TrainingRepository *repository,
                       QWidget *parent = nullptr);

private:
  void buildUi();
  void loadRepetitions(const QString &preferredId = {});
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
  const TrainingVideoFile *
  videoFileForRepetition(const ActionRepetition &repetition) const;
  QString localVideoFileForRepetition(const ActionRepetition &repetition) const;
  QString videoSource() const;
  bool videoSourceIsUrl() const;
  bool hasLocalVideo() const;
  bool repositoryAvailable() const;
  void updateMediaControls();
  void updateReviewFormState();
  void setPlaybackRate(double rate);
  void updatePlaybackRateButtons();

  SessionHistoryItem m_record;
  ActionStandard m_standard;
  TrainingRepository *m_repository = nullptr;
  QVector<ActionRepetition> m_repetitions;
  int m_currentRow = -1;
  QString m_loadedMainVideoSource;
  bool m_loadedMainVideoIsLocal = false;
  double m_playbackRate = 1.0;

  VideoOpenGLWidget *m_mainVideo = nullptr;
  VideoOpenGLWidget *m_referenceVideo = nullptr;
  QTableWidget *m_table = nullptr;
  QLabel *m_statusLabel = nullptr;
  QLabel *m_mainSourceLabel = nullptr;
  QLabel *m_positionLabel = nullptr;
  QLabel *m_startTimeLabel = nullptr;
  QLabel *m_endTimeLabel = nullptr;
  QLabel *m_repetitionEmptyLabel = nullptr;
  QLabel *m_rawDetailsLabel = nullptr;
  QLabel *m_referenceEmptyLabel = nullptr;
  QLabel *m_referenceSourceLabel = nullptr;
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
  QPushButton *m_playButton = nullptr;
  QPushButton *m_pauseButton = nullptr;
  QPushButton *m_keyFrameButton = nullptr;
  QPushButton *m_stepButton = nullptr;
  QPushButton *m_quarterRateButton = nullptr;
  QPushButton *m_halfRateButton = nullptr;
  QPushButton *m_normalRateButton = nullptr;
  QPushButton *m_saveReviewButton = nullptr;
  QPushButton *m_manualButton = nullptr;
  QPushButton *m_saveReferenceButton = nullptr;
  QButtonGroup *m_rateButtonGroup = nullptr;
  QTimer *m_positionTimer = nullptr;
};

#endif // TRAININGREVIEWDIALOG_H
