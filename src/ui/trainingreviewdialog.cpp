#include "trainingreviewdialog.h"
#include "nvrplayback.h"
#include "trainingrepository.h"
#include "videoopenglwidget.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class ElidingLabel final : public QLabel {
public:
  explicit ElidingLabel(QWidget *parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setPen(palette().color(foregroundRole()));
    painter.drawText(contentsRect(), alignment() | Qt::AlignVCenter,
                     fontMetrics().elidedText(text(), Qt::ElideMiddle,
                                              contentsRect().width()));
  }
};

bool hasMediaUrlScheme(const QString &source) {
  return source.trimmed().contains(QStringLiteral("://"));
}

QString displaySource(const QString &source) {
  const QString s = source.trimmed();
  if (s.isEmpty())
    return QStringLiteral("—");
  if (!hasMediaUrlScheme(s))
    return QDir::toNativeSeparators(QFileInfo(s).absoluteFilePath());
  QUrl url = QUrl::fromEncoded(s.toUtf8(), QUrl::TolerantMode);
  if (!url.password().isEmpty())
    url.setPassword(QStringLiteral("***"));
  return url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
}
QString formatMilliseconds(int milliseconds) {
  const int ms = std::max(0, milliseconds);
  const int seconds = ms / 1000;
  return QStringLiteral("%1:%2.%3")
      .arg(seconds / 60, 2, 10, QLatin1Char('0'))
      .arg(seconds % 60, 2, 10, QLatin1Char('0'))
      .arg((ms % 1000) / 100);
}
QString issueSummary(const QString &errorCodes) {
  const QStringList values =
      errorCodes.split(QStringLiteral("|"), Qt::SkipEmptyParts);
  return values.isEmpty() ? QStringLiteral("—")
                          : values.join(QStringLiteral("、"));
}
QSpinBox *makeScoreSpinBox(QWidget *parent) {
  auto *spinBox = new QSpinBox(parent);
  spinBox->setRange(0, 100);
  return spinBox;
}
QTableWidgetItem *item(const QString &s) {
  auto *i = new QTableWidgetItem(s);
  i->setFlags(i->flags() & ~Qt::ItemIsEditable);
  i->setToolTip(s);
  return i;
}
QLabel *muted(const QString &s, QWidget *p) {
  auto *l = new QLabel(s, p);
  l->setTextFormat(Qt::PlainText);
  l->setProperty("role", "muted");
  return l;
}
QFrame *section(QWidget *p) {
  auto *f = new QFrame(p);
  f->setProperty("role", "surface");
  return f;
}
QString cameraSettingsGroup(int cameraIndex) {
  return QStringLiteral("cameras/camera%1")
      .arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
}

SharedCameraSettings loadSharedCameraSettings() {
  SharedCameraSettings s;
  QSettings q;
  q.beginGroup(QStringLiteral("cameraDefaults"));
  s.username = q.value(QStringLiteral("username")).toString().trimmed();
  s.password = q.value(QStringLiteral("password")).toString();
  s.port = q.value(QStringLiteral("port"), QStringLiteral("554"))
               .toString()
               .trimmed();
  s.nvrPlaybackTemplate =
      q.value(QStringLiteral("nvrPlaybackTemplate")).toString().trimmed();
  q.endGroup();
  if (s.port.isEmpty())
    s.port = QStringLiteral("554");
  return s;
}
QVector<CameraSlotSettings> loadCameraSlotSettings(int cameraCount) {
  QVector<CameraSlotSettings> result;
  QSettings q;
  for (int i = 0; i < cameraCount; ++i) {
    CameraSlotSettings s;
    q.beginGroup(cameraSettingsGroup(i));
    s.ip = q.value(QStringLiteral("ip")).toString().trimmed();
    q.endGroup();
    result.append(s);
  }
  return result;
}
QString sourceType(const SessionHistoryItem &r) {
  if (r.sourceType == QStringLiteral("competition"))
    return QStringLiteral("比赛");
  if (r.sourceType == QStringLiteral("offline_import"))
    return QStringLiteral("导入视频");
  return QStringLiteral("训练");
}
QString sourceDetail(const SessionHistoryItem &r) {
  for (const QString &s :
       {r.sourceLabel, r.raceName, r.eventName, r.videoCameraName})
    if (!s.trimmed().isEmpty())
      return s.trimmed();
  return r.camera > 0
             ? QStringLiteral("CAM %1").arg(r.camera, 2, 10, QLatin1Char('0'))
             : QString();
}
} // namespace

TrainingReviewDialog::TrainingReviewDialog(const SessionHistoryItem &record,
                                           const ActionStandard &standard,
                                           TrainingRepository *repository,
                                           QWidget *parent)
    : FramelessDialog(parent), m_record(record), m_standard(standard),
      m_repository(repository) {
  setDialogTitle(QStringLiteral("训练复盘"));
  setMinimumSize(1120, 720);
  resize(1320, 840);
  setSizeGripEnabled(true);
  buildUi();
  loadMainVideo();
  loadRepetitions();
  loadReferenceVideo();
}

void TrainingReviewDialog::buildUi() {
  auto *root = contentLayout();
  root->setSpacing(8);
  auto *header = new QHBoxLayout;
  auto *headText = new QVBoxLayout;
  auto *name = new QLabel(
      QStringLiteral("%1%2%3").arg(
          m_record.athleteName.isEmpty() ? QStringLiteral("未命名运动员")
                                         : m_record.athleteName,
          m_record.actionName.isEmpty() ? QString() : QStringLiteral(" · "),
          m_record.actionName),
      this);
  name->setProperty("role", "sectionTitle");
  name->setTextFormat(Qt::PlainText);
  name->setWordWrap(true);
  const QString when =
      m_record.startedAt.isValid()
          ? m_record.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
          : m_record.time;
  headText->addWidget(name);
  headText->addWidget(
      muted(when.isEmpty() ? QStringLiteral("时间未记录") : when, this));
  header->addLayout(headText, 1);
  const QString detail = sourceDetail(m_record);
  auto *badge = new QLabel(detail.isEmpty() ? sourceType(m_record)
                                            : QStringLiteral("%1 · %2").arg(
                                                  sourceType(m_record), detail),
                           this);
  badge->setProperty("role", "scoreTag");
  badge->setTextFormat(Qt::PlainText);
  badge->setWordWrap(true);
  badge->setMaximumWidth(330);
  header->addWidget(badge, 0, Qt::AlignTop);
  root->addLayout(header);
  root->addWidget(
      muted(QStringLiteral("逐段查看训练录像并进行人工复核"), this));

  auto *videos = new QHBoxLayout;
  videos->setSpacing(8);
  auto *mainBox = section(this);
  auto *mainLayout = new QVBoxLayout(mainBox);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  auto *mainHead = new QHBoxLayout;
  auto *mainTitle = new QLabel(QStringLiteral("训练回放"), mainBox);
  mainTitle->setProperty("role", "sectionTitle");
  m_mainSourceLabel = new ElidingLabel(mainBox);
  m_mainSourceLabel->setProperty("role", "muted");
  mainHead->addWidget(mainTitle);
  mainHead->addStretch();
  mainHead->addWidget(m_mainSourceLabel, 1);
  mainLayout->addLayout(mainHead);
  m_mainVideo = new VideoOpenGLWidget(mainBox);
  m_mainVideo->setMinimumSize(560, 260);
  m_mainVideo->setOverlayControlsVisible(true);
  m_mainVideo->setConfigButtonVisible(false);
  m_mainVideo->setPlaceholderText(QStringLiteral("训练回放"));
  mainLayout->addWidget(m_mainVideo, 1);
  videos->addWidget(mainBox, 2);
  auto *refBox = section(this);
  auto *refLayout = new QVBoxLayout(refBox);
  refLayout->setContentsMargins(8, 8, 8, 8);
  refLayout->setSpacing(4);
  auto *refTitle = new QLabel(QStringLiteral("标准参考"), refBox);
  refTitle->setProperty("role", "sectionTitle");
  refLayout->addWidget(refTitle);
  m_referenceVideo = new VideoOpenGLWidget(refBox);
  m_referenceVideo->setMinimumSize(260, 100);
  m_referenceVideo->setOverlayControlsVisible(true);
  m_referenceVideo->setConfigButtonVisible(false);
  refLayout->addWidget(m_referenceVideo, 1);
  m_referenceEmptyLabel = new QLabel(
      QStringLiteral("尚未配置标准参考视频\n可选择视频作为动作对照"), refBox);
  m_referenceEmptyLabel->setAlignment(Qt::AlignCenter);
  m_referenceEmptyLabel->setProperty("role", "muted");
  m_referenceEmptyLabel->setMinimumHeight(100);
  refLayout->addWidget(m_referenceEmptyLabel, 1);
  m_referenceSourceLabel = new ElidingLabel(refBox);
  m_referenceSourceLabel->setProperty("role", "muted");
  refLayout->addWidget(m_referenceSourceLabel);
  m_referencePathEdit = new QLineEdit(refBox);
  m_referencePathEdit->setText(m_standard.referenceVideoSource);
  m_referencePathEdit->setPlaceholderText(QStringLiteral("参考视频路径或 URL"));
  m_referencePathEdit->hide();
  refLayout->addWidget(m_referencePathEdit);
  auto *refActions = new QHBoxLayout;
  auto *chooseRef = new QPushButton(QStringLiteral("选择参考"), refBox);
  auto *editReferenceSourceButton =
      new QPushButton(QStringLiteral("编辑来源"), refBox);
  editReferenceSourceButton->setCheckable(true);
  editReferenceSourceButton->setProperty("variant", "subtle");
  m_saveReferenceButton = new QPushButton(QStringLiteral("保存参考"), refBox);
  chooseRef->setProperty("variant", "secondary");
  m_saveReferenceButton->setProperty("variant", "secondary");
  refActions->addWidget(chooseRef);
  refActions->addWidget(editReferenceSourceButton);
  refActions->addWidget(m_saveReferenceButton);
  refActions->addStretch();
  refLayout->addLayout(refActions);
  m_referenceNotesEdit = new QPlainTextEdit(refBox);
  m_referenceNotesEdit->setPlainText(m_standard.referenceNotes);
  m_referenceNotesEdit->setPlaceholderText(QStringLiteral("参考说明"));
  m_referenceNotesEdit->setMaximumHeight(64);
  refLayout->addWidget(m_referenceNotesEdit);
  videos->addWidget(refBox, 1);
  root->addLayout(videos, 5);

  auto *controlBox = section(this);
  auto *controls = new QHBoxLayout(controlBox);
  controls->setContentsMargins(8, 5, 8, 5);
  controls->setSpacing(5);
  m_playButton = new QPushButton(QStringLiteral("播放"), controlBox);
  m_pauseButton = new QPushButton(QStringLiteral("暂停"), controlBox);
  m_playButton->setProperty("variant", "primary");
  m_pauseButton->setProperty("variant", "secondary");
  controls->addWidget(m_playButton);
  controls->addWidget(m_pauseButton);
  controls->addSpacing(7);
  auto *prev = new QPushButton(QStringLiteral("上一段"), controlBox);
  auto *next = new QPushButton(QStringLiteral("下一段"), controlBox);
  m_keyFrameButton = new QPushButton(QStringLiteral("关键帧"), controlBox);
  m_stepButton = new QPushButton(QStringLiteral("逐帧"), controlBox);
  for (auto *b : {prev, next, m_keyFrameButton, m_stepButton}) {
    b->setProperty("variant", "subtle");
    controls->addWidget(b);
  }
  controls->addSpacing(7);
  controls->addWidget(muted(QStringLiteral("速度"), controlBox));
  m_rateButtonGroup = new QButtonGroup(this);
  m_rateButtonGroup->setExclusive(true);
  m_quarterRateButton = new QPushButton(QStringLiteral("0.25x"), controlBox);
  m_halfRateButton = new QPushButton(QStringLiteral("0.5x"), controlBox);
  m_normalRateButton = new QPushButton(QStringLiteral("1x"), controlBox);
  for (auto *b : {m_quarterRateButton, m_halfRateButton, m_normalRateButton}) {
    b->setCheckable(true);
    m_rateButtonGroup->addButton(b);
    controls->addWidget(b);
  }
  m_normalRateButton->setChecked(true);
  updatePlaybackRateButtons();
  m_positionLabel = muted(QStringLiteral("00:00.0 / --:--"), controlBox);
  controls->addStretch();
  controls->addWidget(m_positionLabel);
  root->addWidget(controlBox);

  auto *body = new QHBoxLayout;
  body->setSpacing(8);
  auto *listBox = section(this);
  listBox->setMinimumHeight(180);
  auto *listLayout = new QVBoxLayout(listBox);
  listLayout->setContentsMargins(8, 8, 8, 8);
  auto *listTitle = new QLabel(QStringLiteral("动作片段"), listBox);
  listTitle->setProperty("role", "sectionTitle");
  listLayout->addWidget(listTitle);
  m_table = new QTableWidget(listBox);
  m_table->setColumnCount(6);
  m_table->setHorizontalHeaderLabels(
      {QStringLiteral("#"), QStringLiteral("来源"), QStringLiteral("时间"),
       QStringLiteral("结果"), QStringLiteral("分数"), QStringLiteral("问题")});
  m_table->verticalHeader()->hide();
  m_table->setShowGrid(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setMinimumHeight(130);
  m_table->setTextElideMode(Qt::ElideRight);
  m_table->setWordWrap(false);
  m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  m_table->setColumnWidth(0, 30);
  m_table->setColumnWidth(1, 54);
  m_table->setColumnWidth(2, 112);
  m_table->setColumnWidth(3, 105);
  m_table->setColumnWidth(4, 44);
  m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
  listLayout->addWidget(m_table, 1);
  m_repetitionEmptyLabel = new QLabel(
      QStringLiteral("暂无动作片段\n可以新增手动动作开始复核"), listBox);
  m_repetitionEmptyLabel->setAlignment(Qt::AlignCenter);
  m_repetitionEmptyLabel->setProperty("role", "emptyBox");
  listLayout->addWidget(m_repetitionEmptyLabel, 1);
  body->addWidget(listBox, 2);

  auto *reviewBox = section(this);
  auto *reviewOuter = new QVBoxLayout(reviewBox);
  reviewOuter->setContentsMargins(0, 0, 0, 0);
  auto *scroll = new QScrollArea(reviewBox);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *review = new QWidget(scroll);
  auto *form = new QVBoxLayout(review);
  form->setContentsMargins(8, 8, 8, 8);
  auto *formTitle = new QLabel(QStringLiteral("当前动作复核"), review);
  formTitle->setProperty("role", "sectionTitle");
  form->addWidget(formTitle);
  auto *times = new QGridLayout;
  m_startSpinBox = new QSpinBox(review);
  m_endSpinBox = new QSpinBox(review);
  for (auto *s : {m_startSpinBox, m_endSpinBox}) {
    s->setRange(0, 86400000);
    s->setSingleStep(100);
    s->setSuffix(QStringLiteral(" ms"));
  }
  m_startTimeLabel = muted(QStringLiteral("00:00.0"), review);
  m_endTimeLabel = muted(QStringLiteral("00:00.0"), review);
  times->addWidget(muted(QStringLiteral("开始"), review), 0, 0);
  times->addWidget(muted(QStringLiteral("结束"), review), 0, 1);
  times->addWidget(m_startSpinBox, 1, 0);
  times->addWidget(m_endSpinBox, 1, 1);
  times->addWidget(m_startTimeLabel, 2, 0);
  times->addWidget(m_endTimeLabel, 2, 1);
  form->addLayout(times);
  m_validCheckBox = new QCheckBox(QStringLiteral("有效动作"), review);
  form->addWidget(m_validCheckBox);
  m_scoreSpinBox = makeScoreSpinBox(review);
  m_detectionSpinBox = makeScoreSpinBox(review);
  m_symmetrySpinBox = makeScoreSpinBox(review);
  m_balanceSpinBox = makeScoreSpinBox(review);
  m_stabilitySpinBox = makeScoreSpinBox(review);
  m_depthSpinBox = makeScoreSpinBox(review);
  auto *scores = new QGridLayout;
  const QVector<QPair<QString, QSpinBox *>> fields = {
      {QStringLiteral("总分"), m_scoreSpinBox},
      {QStringLiteral("关键点"), m_detectionSpinBox},
      {QStringLiteral("对称"), m_symmetrySpinBox},
      {QStringLiteral("重心"), m_balanceSpinBox},
      {QStringLiteral("稳定"), m_stabilitySpinBox},
      {QStringLiteral("3D"), m_depthSpinBox}};
  for (int i = 0; i < fields.size(); ++i) {
    int c = i % 3, r = (i / 3) * 2;
    scores->addWidget(muted(fields[i].first, review), r, c);
    scores->addWidget(fields[i].second, r + 1, c);
  }
  form->addLayout(scores);
  form->addWidget(
      muted(QStringLiteral("问题标签 · 多个项目用 | 分隔"), review));
  m_errorLineEdit = new QLineEdit(review);
  form->addWidget(m_errorLineEdit);
  form->addWidget(muted(QStringLiteral("复核反馈"), review));
  m_feedbackEdit = new QPlainTextEdit(review);
  m_feedbackEdit->setMinimumHeight(80);
  m_feedbackEdit->setMaximumHeight(100);
  form->addWidget(m_feedbackEdit);
  form->addWidget(muted(QStringLiteral("教练备注"), review));
  m_noteEdit = new QPlainTextEdit(review);
  m_noteEdit->setMinimumHeight(80);
  m_noteEdit->setMaximumHeight(100);
  form->addWidget(m_noteEdit);
  auto *rawDetailsButton = new QPushButton(QStringLiteral("原始记录"), review);
  rawDetailsButton->setCheckable(true);
  rawDetailsButton->setProperty("variant", "subtle");
  form->addWidget(rawDetailsButton, 0, Qt::AlignLeft);
  m_rawDetailsLabel = muted(QStringLiteral("选择动作片段后查看"), review);
  m_rawDetailsLabel->setWordWrap(true);
  m_rawDetailsLabel->setTextFormat(Qt::PlainText);
  m_rawDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_rawDetailsLabel->hide();
  form->addWidget(m_rawDetailsLabel);
  form->addStretch();
  scroll->setWidget(review);
  reviewOuter->addWidget(scroll, 1);
  auto *actions = new QHBoxLayout;
  actions->setContentsMargins(8, 5, 8, 8);
  m_manualButton = new QPushButton(QStringLiteral("新增手动动作"), reviewBox);
  m_saveReviewButton = new QPushButton(QStringLiteral("保存复核"), reviewBox);
  m_manualButton->setProperty("variant", "secondary");
  m_saveReviewButton->setProperty("variant", "primary");
  actions->addWidget(m_manualButton);
  actions->addStretch();
  actions->addWidget(m_saveReviewButton);
  reviewOuter->addLayout(actions);
  body->addWidget(reviewBox, 3);
  root->addLayout(body, 4);
  m_statusLabel = muted(QString(), this);
  root->addWidget(m_statusLabel);

  connect(m_playButton, &QPushButton::clicked, this,
          [this] { m_mainVideo->setPlaying(true); });
  connect(m_pauseButton, &QPushButton::clicked, this,
          [this] { m_mainVideo->pausePlayback(); });
  connect(prev, &QPushButton::clicked, this,
          [this] { selectRepetition(std::max(0, m_currentRow - 1)); });
  connect(next, &QPushButton::clicked, this, [this] {
    if (!m_repetitions.isEmpty())
      selectRepetition(std::min(static_cast<int>(m_repetitions.size()) - 1,
                                m_currentRow + 1));
  });
  connect(m_keyFrameButton, &QPushButton::clicked, this, [this] {
    if (m_currentRow >= 0 && m_currentRow < m_repetitions.size())
      seekToRepetition(m_repetitions[m_currentRow], true);
  });
  connect(m_stepButton, &QPushButton::clicked, this,
          [this] { m_mainVideo->stepForward(); });
  connect(m_quarterRateButton, &QPushButton::clicked, this,
          [this] { setPlaybackRate(.25); });
  connect(m_halfRateButton, &QPushButton::clicked, this,
          [this] { setPlaybackRate(.5); });
  connect(m_normalRateButton, &QPushButton::clicked, this,
          [this] { setPlaybackRate(1); });
  connect(m_table, &QTableWidget::currentCellChanged, this,
          [this](int row, int, int, int) {
            if (row >= 0 && row != m_currentRow)
              selectRepetition(row);
          });
  connect(m_startSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int v) { m_startTimeLabel->setText(formatMilliseconds(v)); });
  connect(m_endSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int v) { m_endTimeLabel->setText(formatMilliseconds(v)); });
  connect(m_saveReviewButton, &QPushButton::clicked, this,
          [this] { saveCurrentReview(); });
  connect(m_manualButton, &QPushButton::clicked, this,
          [this] { createManualRepetition(); });
  connect(chooseRef, &QPushButton::clicked, this,
          [this] { chooseReferenceVideo(); });
  connect(editReferenceSourceButton, &QPushButton::toggled, m_referencePathEdit,
          &QLineEdit::setVisible);
  connect(rawDetailsButton, &QPushButton::toggled, m_rawDetailsLabel,
          &QLabel::setVisible);
  connect(m_saveReferenceButton, &QPushButton::clicked, this,
          [this] { saveReference(); });
  connect(m_referencePathEdit, &QLineEdit::textChanged, this,
          [this](const QString &s) {
            m_referencePathEdit->setEchoMode(
                hasMediaUrlScheme(s) ? QLineEdit::Password : QLineEdit::Normal);
          });
  m_referencePathEdit->setEchoMode(
      hasMediaUrlScheme(m_referencePathEdit->text()) ? QLineEdit::Password
                                                     : QLineEdit::Normal);
  m_positionTimer = new QTimer(this);
  m_positionTimer->setInterval(250);
  connect(m_positionTimer, &QTimer::timeout, this,
          [this] { refreshPositionLabel(); });
  m_positionTimer->start();
  updateMediaControls();
  updateReviewFormState();
  m_saveReferenceButton->setEnabled(repositoryAvailable());
}

void TrainingReviewDialog::loadRepetitions(const QString &preferredId) {
  if (repositoryAvailable())
    m_repetitions = m_repository->reviewedRepetitionsForSession(m_record.id);
  populateTable();
  if (m_repetitions.isEmpty()) {
    m_currentRow = -1;
    updateReviewFormState();
    return;
  }
  int row = 0;
  for (int i = 0; !preferredId.isEmpty() && i < m_repetitions.size(); ++i)
    if (m_repetitions[i].id == preferredId) {
      row = i;
      break;
    }
  selectRepetition(row);
}
void TrainingReviewDialog::populateTable() {
  QSignalBlocker blocker(m_table);
  m_table->clearContents();
  m_table->setRowCount(m_repetitions.size());
  for (int r = 0; r < m_repetitions.size(); ++r) {
    const auto &rep = m_repetitions[r];
    m_table->setItem(r, 0, item(QString::number(r + 1)));
    m_table->setItem(r, 1,
                     item(rep.source == QStringLiteral("coach")
                              ? QStringLiteral("手动 / 教练")
                              : QStringLiteral("AI")));
    m_table->setItem(r, 2,
                     item(QStringLiteral("%1–%2").arg(
                         formatMilliseconds(rep.effectiveStartedMs()),
                         formatMilliseconds(rep.effectiveEndedMs()))));
    m_table->setItem(r, 3,
                     item(QStringLiteral("%1 · %2").arg(
                         rep.effectiveValid() ? QStringLiteral("有效")
                                              : QStringLiteral("无效"),
                         rep.hasManualReview() ? QStringLiteral("已复核")
                                               : QStringLiteral("未复核"))));
    m_table->setItem(r, 4, item(QString::number(rep.effectiveScore())));
    m_table->setItem(r, 5, item(issueSummary(rep.effectiveErrorCodes())));
  }
  const bool empty = m_repetitions.isEmpty();
  m_table->setVisible(!empty);
  m_repetitionEmptyLabel->setVisible(empty);
}
void TrainingReviewDialog::selectRepetition(int row) {
  if (row < 0 || row >= m_repetitions.size())
    return;
  m_currentRow = row;
  {
    QSignalBlocker b(m_table);
    m_table->selectRow(row);
    m_table->setCurrentCell(row, 0);
    m_table->scrollToItem(m_table->item(row, 0));
  }
  const auto &r = m_repetitions[row];
  m_startSpinBox->setValue(r.effectiveStartedMs());
  m_endSpinBox->setValue(r.effectiveEndedMs());
  m_validCheckBox->setChecked(r.effectiveValid());
  m_scoreSpinBox->setValue(std::clamp(r.effectiveScore(), 0, 100));
  m_detectionSpinBox->setValue(std::clamp(r.effectiveDetectionScore(), 0, 100));
  m_symmetrySpinBox->setValue(std::clamp(r.effectiveSymmetryScore(), 0, 100));
  m_balanceSpinBox->setValue(std::clamp(r.effectiveBalanceScore(), 0, 100));
  m_stabilitySpinBox->setValue(std::clamp(r.effectiveStabilityScore(), 0, 100));
  m_depthSpinBox->setValue(std::clamp(r.effectiveDepthScore(), 0, 100));
  m_errorLineEdit->setText(r.effectiveErrorCodes());
  m_feedbackEdit->setPlainText(r.effectiveFeedback());
  m_noteEdit->setPlainText(r.coachNote);
  m_rawDetailsLabel->setText(
      QStringLiteral("时间 %1–%2 · %3 · 总分 %4\n关键点 %5 · 对称 %6 · 重心 %7 "
                     "· 稳定 %8 · 3D %9\n问题：%10\n反馈：%11")
          .arg(formatMilliseconds(r.startedMs), formatMilliseconds(r.endedMs),
               r.valid ? QStringLiteral("有效") : QStringLiteral("无效"))
          .arg(r.score)
          .arg(r.detectionScore)
          .arg(r.symmetryScore)
          .arg(r.balanceScore)
          .arg(r.stabilityScore)
          .arg(r.depthScore)
          .arg(issueSummary(r.errorCodes),
               r.feedback.isEmpty() ? QStringLiteral("—") : r.feedback));
  updateReviewFormState();
  seekToRepetition(r, false);
}

void TrainingReviewDialog::seekToRepetition(const ActionRepetition &r,
                                            bool key) {
  const int start =
      std::max(0, key && r.keyFrameMs > 0 ? r.keyFrameMs : r.videoClipStartMs);
  const QString local = localVideoFileForRepetition(r);
  if (!local.isEmpty()) {
    if (m_loadedMainVideoSource != local || !m_loadedMainVideoIsLocal) {
      m_mainVideo->playFile(local, start);
      m_loadedMainVideoSource = local;
      m_loadedMainVideoIsLocal = true;
    } else if (m_mainVideo->isSeekable())
      m_mainVideo->seekTo(start);
    else
      m_mainVideo->playFile(local, start);
    m_mainVideo->setPlaybackRate(m_playbackRate);
    m_mainSourceLabel->setText(
        QStringLiteral("本地录像 · %1").arg(displaySource(local)));
    m_mainSourceLabel->setToolTip(m_mainSourceLabel->text());
    m_statusLabel->setText(QStringLiteral("已定位到视频 #%1 的 %2")
                               .arg(r.videoIndex)
                               .arg(formatMilliseconds(start)));
    updateMediaControls();
    return;
  }
  const NvrPlaybackResult nvr = buildNvrPlaybackUrl(
      loadSharedCameraSettings(), loadCameraSlotSettings(12), m_record, start,
      r.videoClipEndMs);
  if (!nvr.url.isEmpty()) {
    m_mainVideo->playMainUrlWithFallback(nvr.url, m_record.videoFallbackSource);
    m_loadedMainVideoSource = nvr.url;
    m_loadedMainVideoIsLocal = false;
    m_mainVideo->setPlaybackRate(m_playbackRate);
    m_mainSourceLabel->setText(
        QStringLiteral("NVR 回放 · %1").arg(displaySource(nvr.url)));
    m_mainSourceLabel->setToolTip(m_mainSourceLabel->text());
    m_statusLabel->setText(QStringLiteral("已打开 NVR 片段 %1–%2")
                               .arg(formatMilliseconds(nvr.startOffsetMs),
                                    formatMilliseconds(nvr.endOffsetMs)));
  } else if (hasLocalVideo())
    loadMainVideo(start);
  else if (!videoSource().isEmpty()) {
    m_mainVideo->playMainUrlWithFallback(videoSource(),
                                         m_record.videoFallbackSource);
    m_loadedMainVideoSource = videoSource();
    m_loadedMainVideoIsLocal = false;
    m_mainVideo->setPlaybackRate(m_playbackRate);
    m_statusLabel->setText(QStringLiteral("网络录像显示片段起点 %1")
                               .arg(formatMilliseconds(start)));
  } else
    m_statusLabel->setText(
        QStringLiteral("当前训练没有可用录像；仍可进行人工复核"));
  updateMediaControls();
}
void TrainingReviewDialog::saveCurrentReview() {
  if (!repositoryAvailable() || m_currentRow < 0 ||
      m_currentRow >= m_repetitions.size())
    return;
  const QString id = m_repetitions[m_currentRow].id;
  auto r = formRepetition();
  r.id = id;
  r.sessionId = m_record.id;
  r.reviewerCoachId = m_record.coachId;
  QString e;
  if (!m_repository->saveRepetitionReview(r, &e)) {
    QMessageBox::warning(this, QStringLiteral("保存失败"), e);
    return;
  }
  loadRepetitions(id);
  m_statusLabel->setText(QStringLiteral("复核已保存"));
}
void TrainingReviewDialog::createManualRepetition() {
  if (!repositoryAvailable())
    return;
  auto r = formRepetition();
  r.reviewerCoachId = m_record.coachId;
  QString e;
  if (!m_repository->createManualRepetition(m_record.id,
                                            m_record.actionStandardId,
                                            m_record.standardVersion, &r, &e)) {
    QMessageBox::warning(this, QStringLiteral("新增失败"), e);
    return;
  }
  loadRepetitions(r.id);
  m_statusLabel->setText(QStringLiteral("已新增手动动作"));
}
void TrainingReviewDialog::chooseReferenceVideo() {
  const QString p = QFileDialog::getOpenFileName(
      this, QStringLiteral("选择标准参考视频"), QDir::homePath(),
      QStringLiteral("视频文件 (*.mp4 *.mov *.avi *.mkv *.m4v *.wmv *.flv "
                     "*.webm);;所有文件 (*.*)"));
  if (p.isEmpty())
    return;
  m_referencePathEdit->setText(QFileInfo(p).absoluteFilePath());
  loadReferenceVideo();
}
void TrainingReviewDialog::saveReference() {
  if (!repositoryAvailable())
    return;
  m_standard.referenceVideoSource = m_referencePathEdit->text().trimmed();
  m_standard.referenceNotes = m_referenceNotesEdit->toPlainText().trimmed();
  QString e;
  if (!m_repository->saveActionStandard(&m_standard, &e)) {
    QMessageBox::warning(this, QStringLiteral("保存失败"), e);
    return;
  }
  m_statusLabel->setText(
      QStringLiteral("标准参考已保存 · v%1").arg(m_standard.version));
  loadReferenceVideo();
}
void TrainingReviewDialog::refreshPositionLabel() {
  const qint64 p = m_mainVideo ? m_mainVideo->positionMs() : -1,
               d = m_mainVideo ? m_mainVideo->durationMs() : -1;
  m_positionLabel->setText(QStringLiteral("%1 / %2").arg(
      formatMilliseconds(int(std::max<qint64>(0, p))),
      d >= 0 ? formatMilliseconds(int(d)) : QStringLiteral("--:--")));
}
void TrainingReviewDialog::loadMainVideo(int offset) {
  const QString s = videoSource();
  const QString kind =
      hasMediaUrlScheme(s) ? QStringLiteral("URL") : QStringLiteral("本地录像");
  m_mainSourceLabel->setText(
      QStringLiteral("%1 · %2").arg(kind, displaySource(s)));
  m_mainSourceLabel->setToolTip(m_mainSourceLabel->text());
  if (s.isEmpty()) {
    m_loadedMainVideoSource.clear();
    m_loadedMainVideoIsLocal = false;
    m_mainVideo->stopPlayback();
    m_mainVideo->setPlaceholderText(QStringLiteral("当前训练没有可用录像"));
    updateMediaControls();
    return;
  }
  if (hasMediaUrlScheme(s)) {
    m_mainVideo->playMainUrlWithFallback(s, m_record.videoFallbackSource);
    m_loadedMainVideoSource = s;
    m_loadedMainVideoIsLocal = false;
  } else {
    QFileInfo f(s);
    if (!f.exists() || !f.isFile()) {
      m_loadedMainVideoSource.clear();
      m_loadedMainVideoIsLocal = false;
      m_mainVideo->stopPlayback();
      m_mainVideo->setPlaceholderText(QStringLiteral("当前训练没有可用录像"));
      updateMediaControls();
      return;
    }
    m_mainVideo->playFile(f.absoluteFilePath(), offset);
    m_loadedMainVideoSource = f.absoluteFilePath();
    m_loadedMainVideoIsLocal = true;
  }
  m_mainVideo->setPlaybackRate(m_playbackRate);
  updateMediaControls();
}
void TrainingReviewDialog::loadReferenceVideo() {
  const QString s = m_referencePathEdit ? m_referencePathEdit->text().trimmed()
                                        : m_standard.referenceVideoSource;
  m_referenceSourceLabel->setText(
      QStringLiteral("来源：%1").arg(displaySource(s)));
  m_referenceSourceLabel->setToolTip(m_referenceSourceLabel->text());
  m_referenceEmptyLabel->setText(
      QStringLiteral("尚未配置标准参考视频\n可选择视频作为动作对照"));
  m_referenceEmptyLabel->setVisible(s.isEmpty());
  m_referenceVideo->setVisible(!s.isEmpty());
  if (s.isEmpty()) {
    m_referenceVideo->stopPlayback();
    return;
  }
  if (hasMediaUrlScheme(s)) {
    m_referenceVideo->playMainUrlWithFallback(s, QString());
    return;
  }
  QFileInfo f(s);
  if (!f.exists() || !f.isFile()) {
    m_referenceVideo->stopPlayback();
    m_referenceVideo->hide();
    m_referenceEmptyLabel->setText(
        QStringLiteral("参考视频文件不存在\n请重新选择参考视频"));
    m_referenceEmptyLabel->show();
    return;
  }
  m_referenceVideo->playFile(f.absoluteFilePath());
}
ActionRepetition TrainingReviewDialog::formRepetition() const {
  ActionRepetition rep;
  rep.sessionId = m_record.id;
  rep.actionStandardId = m_record.actionStandardId;
  rep.standardVersion = m_record.standardVersion;
  rep.manualStartedMs =
      std::min(m_startSpinBox->value(), m_endSpinBox->value());
  rep.manualEndedMs = std::max(m_startSpinBox->value(), m_endSpinBox->value());
  rep.manualValid = m_validCheckBox->isChecked() ? 1 : 0;
  rep.manualScore = m_scoreSpinBox->value();
  rep.manualDetectionScore = m_detectionSpinBox->value();
  rep.manualSymmetryScore = m_symmetrySpinBox->value();
  rep.manualBalanceScore = m_balanceSpinBox->value();
  rep.manualStabilityScore = m_stabilitySpinBox->value();
  rep.manualDepthScore = m_depthSpinBox->value();
  rep.manualErrorCodes = m_errorLineEdit->text().trimmed();
  rep.manualFeedback = m_feedbackEdit->toPlainText().trimmed();
  rep.coachNote = m_noteEdit->toPlainText().trimmed();
  rep.keyFrameMs = rep.manualStartedMs;
  rep.videoClipStartMs = std::max(0, rep.manualStartedMs - 1500);
  rep.videoClipEndMs = rep.manualEndedMs + 1500;
  if (m_currentRow >= 0 && m_currentRow < m_repetitions.size()) {
    const ActionRepetition &current = m_repetitions.at(m_currentRow);
    rep.videoFileId = current.videoFileId;
    rep.videoIndex = current.videoIndex > 0 ? current.videoIndex : 1;
  } else if (!m_record.videoFiles.isEmpty()) {
    rep.videoFileId = m_record.videoFiles.first().id;
    rep.videoIndex = m_record.videoFiles.first().videoIndex > 0
                         ? m_record.videoFiles.first().videoIndex
                         : 1;
  }
  return rep;
}

const TrainingVideoFile *TrainingReviewDialog::videoFileForRepetition(
    const ActionRepetition &repetition) const {
  for (const TrainingVideoFile &file : m_record.videoFiles) {
    if (!repetition.videoFileId.trimmed().isEmpty() &&
        file.id == repetition.videoFileId) {
      return &file;
    }
  }
  for (const TrainingVideoFile &file : m_record.videoFiles) {
    if (repetition.videoIndex > 0 && file.videoIndex == repetition.videoIndex) {
      return &file;
    }
  }
  return nullptr;
}

QString TrainingReviewDialog::localVideoFileForRepetition(
    const ActionRepetition &repetition) const {
  const TrainingVideoFile *file = videoFileForRepetition(repetition);
  if (!file || file->filePath.trimmed().isEmpty()) {
    return {};
  }
  const QFileInfo fileInfo(file->filePath.trimmed());
  return fileInfo.exists() && fileInfo.isFile() ? fileInfo.absoluteFilePath()
                                                : QString();
}

QString TrainingReviewDialog::videoSource() const {
  for (const TrainingVideoFile &file : m_record.videoFiles) {
    if (file.filePath.trimmed().isEmpty()) {
      continue;
    }
    const QFileInfo fileInfo(file.filePath.trimmed());
    if (fileInfo.exists() && fileInfo.isFile()) {
      return fileInfo.absoluteFilePath();
    }
  }
  return !m_record.videoSource.trimmed().isEmpty()
             ? m_record.videoSource.trimmed()
             : m_record.videoFallbackSource.trimmed();
}

bool TrainingReviewDialog::videoSourceIsUrl() const {
  return hasMediaUrlScheme(videoSource());
}

bool TrainingReviewDialog::hasLocalVideo() const {
  const QString source = videoSource();
  return !source.trimmed().isEmpty() && !hasMediaUrlScheme(source);
}
bool TrainingReviewDialog::repositoryAvailable() const {
  return m_repository && m_repository->isOpen();
}
void TrainingReviewDialog::updateMediaControls() {
  const QString s = m_loadedMainVideoSource.isEmpty() ? videoSource()
                                                      : m_loadedMainVideoSource;
  const bool ok =
      !s.isEmpty() && (hasMediaUrlScheme(s) ||
                       (QFileInfo(s).exists() && QFileInfo(s).isFile()));
  if (m_mainVideo)
    m_mainVideo->setEnabled(ok);
  for (auto *b : {m_playButton, m_pauseButton, m_keyFrameButton, m_stepButton,
                  m_quarterRateButton, m_halfRateButton, m_normalRateButton})
    if (b)
      b->setEnabled(ok);
  if (m_playButton)
    m_playButton->setToolTip(ok ? QString()
                                : QStringLiteral("当前训练没有可用录像"));
}
void TrainingReviewDialog::updateReviewFormState() {
  const bool selected =
      m_currentRow >= 0 && m_currentRow < m_repetitions.size();
  if (m_saveReviewButton)
    m_saveReviewButton->setEnabled(selected && repositoryAvailable());
  if (m_manualButton)
    m_manualButton->setEnabled(repositoryAvailable());
  if (!selected) {
    for (auto *s : {m_startSpinBox, m_endSpinBox, m_scoreSpinBox,
                    m_detectionSpinBox, m_symmetrySpinBox, m_balanceSpinBox,
                    m_stabilitySpinBox, m_depthSpinBox})
      if (s)
        s->setValue(0);
    if (m_validCheckBox)
      m_validCheckBox->setChecked(true);
    if (m_errorLineEdit)
      m_errorLineEdit->clear();
    if (m_feedbackEdit)
      m_feedbackEdit->clear();
    if (m_noteEdit)
      m_noteEdit->clear();
    if (m_rawDetailsLabel)
      m_rawDetailsLabel->setText(
          QStringLiteral("暂无动作片段；填写后可新增手动动作"));
  }
  if (!repositoryAvailable() && m_statusLabel)
    m_statusLabel->setText(
        QStringLiteral("训练数据服务不可用；当前内容仅供查看"));
}
void TrainingReviewDialog::setPlaybackRate(double r) {
  m_playbackRate = r;
  if (m_mainVideo)
    m_mainVideo->setPlaybackRate(r);
  updatePlaybackRateButtons();
}
void TrainingReviewDialog::updatePlaybackRateButtons() {
  for (const auto &e :
       QVector<QPair<QPushButton *, double>>{{m_quarterRateButton, .25},
                                             {m_halfRateButton, .5},
                                             {m_normalRateButton, 1.0}}) {
    if (!e.first)
      continue;
    const bool on = qFuzzyCompare(m_playbackRate, e.second);
    e.first->setChecked(on);
    e.first->setProperty("variant", on ? "primary" : "subtle");
    e.first->style()->unpolish(e.first);
    e.first->style()->polish(e.first);
  }
}
